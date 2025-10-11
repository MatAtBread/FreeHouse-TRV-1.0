// the setup function runs once when you press reset or power the board
#define TOUCH_PIN 1

#include "trv.h"

#include "esp_sleep.h"
#include "hal/uart_types.h"
#include "esp_netif.h"
#include "esp_pm.h"
#include "nvs_flash.h"

#include "../common/gpio/gpio.hpp"

#include "src/WithTask.h"
#include "src/CaptiveWifi.h"
#include "src/trv-state.h"
#include "net/esp-now.hpp"

extern "C" {
  const char *TAG = "TRV";
}

// Sample the touch ADC for up to 1050ms to see if it was touched for the whole second
// bails returning false if it was released/not touched suring that period.
bool touchButtonPressed() {
  for (int i = 0;; i++) {
    auto n = GPIO::analogRead(TOUCH_PIN);
    ESP_LOGI(TAG, "Touch test %d", n);
    if (n >= 0 && n < 256) {
      return false;
    }
    if (i == 6) {
      ESP_LOGI(TAG, "Touch button pressed");
      return true;
    }
    delay(70);
  }
}

static RTC_DATA_ATTR int messgageChecks = 0;
void checkForMessages(Trv *trv) {

  // Create `net` based on config
  // auto state = Trv::getLastState();
  // ESP_LOGI(TAG, "checkIncomingMessages using netMode %u", state->netMode);
  // switch (state->netMode) {
  //   case NET_MODE_ESP_NOW:
  EspNet *net = new EspNet(trv);
  //   break;
  //   case NET_MODE_MQTT:
  //   break;
  //   case NET_MODE_ZIGBEE:
  //   break;
  // }

  int checkEvery = 60 / trv->getState(true).config.sleep_time;  // Check every 60 seconds, which is (usually) inferred from the config sleep_time
  net->checkMessages();
  // Note, if there were any messages that confifgured the heat settings, checkAutoState wuill have been called as part of their processing.
  // So here, we only need to check the auto state for temperature changes. We do this every 60-120 seconds (min, might be more if sleep_time is large)
  // to avoid excessive checking and the valve moving too often, and to give the device temperature time to settle after a change (which
  // drives up the internal temperature and causes resonance)
  ESP_LOGI(TAG, "checkForMessages %d / %d", messgageChecks, checkEvery);
  if (net->getMessageCount() == 0) {
    if (messgageChecks >= checkEvery) {
      trv->checkAutoState();
      messgageChecks = 0;
    } else {
      messgageChecks += 1;
    }
  } else {
    // We defer checking the auto state if we received messages as they will have called checkAutoState()
    messgageChecks = 0;
  }
  WithTask::waitForAllTasks();
  net->sendStateToHub(trv->getState(false));
  trv->saveState();
  delete net;
}

static RTC_DATA_ATTR int wakeCount = 0;

extern "C" void app_main() {
//  esp_log_level_set("*", ESP_LOG_WARN);
  esp_log_level_set(TAG, ESP_LOG_INFO);

  auto wakeCause = esp_sleep_get_wakeup_cause();
  auto resetCause = esp_reset_reason();
  wakeCount += 1;
  GPIO::pinMode(LED_BUILTIN, OUTPUT);
  GPIO::digitalWrite(LED_BUILTIN, false);

  {
    ESP_LOGI(TAG, "DEBUG DELAY START");
    for (int i=0; i<50; i++) {
      GPIO::digitalWrite(LED_BUILTIN, i & 1);
      delay(100);
    }
    ESP_LOGI(TAG, "DEBUG DELAY END");
  }

  ESP_LOGI(TAG, "Build %s", versionDetail);
  ESP_LOGI(TAG, "Wake: %d reset: %d count: %d", wakeCause, resetCause, wakeCount);

  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());

  // ESP_LOGI(TAG,"Heap %lu",esp_get_free_heap_size());
  GPIO::digitalWrite(LED_BUILTIN, false);
  ESP_LOGI(TAG, "Create TRV");
  Trv *trv = new Trv();
  uint32_t dreamTime;

  if (trv->flatBattery() && !trv->is_charging()) {
    ESP_LOGI(TAG, "Battery exhausted");
    dreamTime = 60 * 60 * 1000000UL;  // 1 hour
  } else {
    if (resetCause != ESP_RST_DEEPSLEEP) {
      resetCause = ESP_RST_DEEPSLEEP;  // To suppress further reset in no-sleep mode
      trv->resetValve();
    }

    ESP_LOGI(TAG, "Check touch button/device name");
    if (!trv->deviceName()[0] || touchButtonPressed()) {
      ESP_LOGI(TAG, "Touch button pressed");
      new CaptivePortal(trv, trv->deviceName());
      // wait for max 5 mins. The CaptivePortal will restart the device under user control
      for (int i = 0; i < 10 * 60; i++) {
        GPIO::digitalWrite(LED_BUILTIN, i & 1);
        delay(i & 1 ? 900 : 100);
      }
      dreamTime = 1;
    } else {
      checkForMessages(trv);
      dreamTime = trv->getState(true).config.sleep_time * 1000000UL;  // 20 seconds
    }
  }

  delete trv;

  // Prepare to sleep. Wake on touch or timeout
  GPIO::digitalWrite(LED_BUILTIN, true);
  GPIO::pinMode(TOUCH_PIN, INPUT);

  esp_sleep_enable_ext1_wakeup(1ULL << TOUCH_PIN, ESP_EXT1_WAKEUP_ANY_HIGH);
  esp_sleep_enable_timer_wakeup(dreamTime);
  ESP_LOGI(TAG, "%s %lu", "deep sleep", dreamTime);

  esp_deep_sleep_start();
}
