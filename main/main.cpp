// the setup function runs once when you press reset or power the board

#include "trv.h"
#include "pins.h"

#include "esp_sleep.h"
#include "hal/uart_types.h"
#include "esp_netif.h"
#include "esp_pm.h"
#include "esp_app_desc.h"
#include "nvs_flash.h"

#include "common/gpio/gpio.hpp"

#include "src/WithTask.h"
#include "src/CaptiveWifi.h"
#include "src/trv-state.h"
#include "net/esp-now.hpp"

extern "C" {
  const char *TAG = "TRV";
}

// Sample the touch ADC for up to 1050ms to see if it was touched for the whole second.
// Bails returning false if it was released/not touched during that period.
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
  if (messgageChecks >= checkEvery) {
    trv->checkAutoState();
    messgageChecks = 0;
  } else {
    messgageChecks += 1;
  }
  WithTask::waitForAllTasks();
  net->sendStateToHub(trv->getState(false));
  trv->saveState();
  delete net;
}

static RTC_DATA_ATTR int wakeCount = 0;
char versionDetail[110] = {0};

extern "C" void app_main() {
//  esp_log_level_set("*", ESP_LOG_WARN);
  esp_log_level_set(TAG, ESP_LOG_INFO);

  auto wakeCause = esp_sleep_get_wakeup_cause();
  auto resetCause = esp_reset_reason();
  wakeCount += 1;
  GPIO::pinMode(LED_BUILTIN, OUTPUT);
  GPIO::digitalWrite(LED_BUILTIN, false);

  const auto app = esp_app_get_description();
  snprintf((char *)versionDetail, sizeof versionDetail, "%s %s %s", app->version, app->date, app->time);
  ESP_LOGI(TAG, "Build: %s", versionDetail);
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
  ESP_LOGI(TAG, "Create TRV");
  Trv *trv = new Trv();
  uint32_t dreamSecs = 1;

  if (trv->flatBattery() && !trv->is_charging()) {
    ESP_LOGI(TAG, "Battery exhausted");
    dreamSecs = 60 * 60;
  } else {
    ESP_LOGI(TAG, "Check touch button/device name");
    if (!trv->deviceName()[0] || touchButtonPressed()) {
      ESP_LOGI(TAG, "Touch button pressed / device name '%s'", trv->deviceName());
      CaptivePortal portal(trv, trv->deviceName());
      switch (portal.exitStatus) {
        case exit_status_t::CALIBRATE: {
            trv->calibrate();
            dreamSecs = 1;
          }
          break;
        case exit_status_t::TEST_MODE: {
            auto state = trv->getState(true);
            delete trv;
            ESP_LOGI(TAG, "Enter test mode");
            // In test mode, we just cycle the valve and print the count
            BatteryMonitor* battery = new BatteryMonitor();
            uint8_t currentPosition = 0;
            MotorController* motor = new MotorController(battery, currentPosition, state.config.motor);
            int count = 0;
            bool failed = false;
            GPIO::digitalWrite(LED_BUILTIN, false);
            while (true) {
              if (failed) {
                  GPIO::digitalWrite(LED_BUILTIN, true);
                  delay(500);
                  GPIO::digitalWrite(LED_BUILTIN, false);
                  delay(200);
              } else {
                ESP_LOGI(TAG, "Test cycle %d", count++);

                float temp = 0;
                DallasOneWire tempSensor(temp);

                const int target = count & 1 ? 100 : 0;
                motor->setValvePosition(target);
                motor->wait();
                if (motor->getValvePosition() != target) {
                  ESP_LOGW(TAG, "Failed to reach target %d, got %d. Sleeping.", target, motor->getValvePosition());
                  //failed = true;
                }
                delay(2000);
              }
              if (touchButtonPressed()) {
                ESP_LOGI(TAG, "Exit test mode on touch");
                dreamSecs = 1;
                break;
              }
            }
          }
          break;
        case exit_status_t::POWER_OFF:
          ESP_LOGI(TAG, "Power off requested");
          dreamSecs = 0x7FFFFFFF; // 30 * 24 * 60 * 60;
          break;
        case exit_status_t::CLOSED:
        case exit_status_t::NONE:
        case exit_status_t::TIME_OUT:
          dreamSecs = 1;
          break;
      }
    } else {
      if (resetCause != ESP_RST_DEEPSLEEP) {
        resetCause = ESP_RST_DEEPSLEEP;  // To suppress further reset in no-sleep mode
        trv->resetValve();
      }

      checkForMessages(trv);
      dreamSecs = trv->getState(true).config.sleep_time;
    }
  }

  delete trv;

  // Prepare to sleep. Wake on touch or timeout
  GPIO::digitalWrite(LED_BUILTIN, true);
  GPIO::pinMode(TOUCH_PIN, INPUT);

  // Ideally, we'd wake on CHARGING changed, but in the current h/w this is not an RTC_GPIO
  esp_sleep_enable_ext1_wakeup(1ULL << TOUCH_PIN, ESP_EXT1_WAKEUP_ANY_HIGH);
  esp_sleep_enable_timer_wakeup(dreamSecs * 1000000ULL);
  ESP_LOGI(TAG, "deep sleep %u secs", dreamSecs);

  esp_deep_sleep_start();
}
