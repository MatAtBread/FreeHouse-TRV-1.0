// the setup function runs once when you press reset or power the board

#include "common/gpio/gpio.hpp"
#include "esp_app_desc.h"
#include "esp_netif.h"
#include "esp_sleep.h"
#include "net/esp-now.hpp"
#include "nvs_flash.h"
#include "pins.h"
#include "src/CaptiveWifi.h"
#include "src/WithTask.h"
#include "src/trv-state.h"
#include "trv.h"
#include "src/TouchButton.hpp"

extern "C" {
const char* TAG = "TRV";
}

static RTC_DATA_ATTR int messgageChecks = 0;
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
  snprintf((char*)versionDetail, sizeof versionDetail, "%s %s %s",
           app->version, app->date, app->time);
  ESP_LOGI(TAG, "Build: %s", versionDetail);
  ESP_LOGI(TAG, "Wake: %d reset: %d count: %d", wakeCause, resetCause,
           wakeCount);

  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  TouchButton touchButton;
  Trv* trv = new Trv();
  trv->wait();
  EspNet net; // Start Wi-Fi based on Trv state (loaded above)
  uint32_t dreamSecs;

  if (trv->flatBattery() && !trv->is_charging()) {
    ESP_LOGI(TAG, "Battery exhausted");
    trv = NULL; // Skip tidy up - we're dead
    dreamSecs = 0x7FFFFFFF;  // Forever
  } else {
    dreamSecs = 1;

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ESP_LOGI(TAG, "Check touch button/device name");
    if (!trv->deviceName()[0] || touchButton.pressed() == PRESSED) {
      ESP_LOGI(TAG, "Touch button pressed / device name '%s'", trv->deviceName());
      CaptivePortal portal(trv, trv->deviceName());
      switch (portal.exitStatus) {
        case exit_status_t::CALIBRATE:
          trv->calibrate();
          break;
        case exit_status_t::TEST_MODE:
          trv->testMode(touchButton);
          break;
        case exit_status_t::POWER_OFF:
          ESP_LOGI(TAG, "Power off requested");
          dreamSecs = 0x7FFFFFFF;  // Forever
          break;
        case exit_status_t::CLOSED:
        case exit_status_t::NONE:
        case exit_status_t::TIME_OUT:
          break;
      }
    } else {
      const auto config = trv->getState(true).config;
      net.checkMessages(trv);
      // Note, if there were any messages that confifgured the heat settings,
      // checkAutoState wuill have been called as part of their processing. So here,
      // we only need to check the auto state for temperature changes. We do this
      // every 60-120 seconds (min, might be more if sleep_time is large) to avoid
      // excessive checking and the valve moving too often, and to give the device
      // temperature time to settle after a change (which drives up the internal
      // temperature and causes resonance)
      // Check every 60 seconds, which is (usually) inferred from the config sleep_time
      int checkEvery = 60 / config.sleep_time;
      ESP_LOGI(TAG, "checkForMessages %d / %d", messgageChecks, checkEvery);
      if (messgageChecks >= checkEvery) {
        trv->setSystemMode(config.system_mode);
        messgageChecks = 0;
      } else {
        messgageChecks += 1;
      }
      dreamSecs = config.sleep_time;
    }
    WithTask::waitForAllTasks();
    net.sendStateToHub(trv, trv->getState(false));
  }

  uint64_t ext1WakeMask = (1ULL << TOUCH_PIN);
  if (trv) {
    if (!trv->is_charging()) {
      // Ideally, we'd wake on CHARGING changed, but in the current h/w this is not
      // an RTC_GPIO. On Rev3.2, the CHARGIBG pin is connected to GPIO2, so we can use that.
      ext1WakeMask |= (1ULL << 2);
    }
    delete trv;
  }

  // Prepare to sleep. Wake on touch or timeout
  GPIO::digitalWrite(LED_BUILTIN, true);
  GPIO::pinMode(TOUCH_PIN, INPUT);

  // Ideally, we'd wake on CHARGING changed, but in the current h/w this is not
  // an RTC_GPIO. On Rev3.2, the CHARGIBG pin is connected to GPIO2, so we can use that.
  esp_sleep_enable_ext1_wakeup(ext1WakeMask, ESP_EXT1_WAKEUP_ANY_HIGH);
  esp_sleep_enable_timer_wakeup(dreamSecs * 1000000ULL);
  ESP_LOGI(TAG, "deep sleep %u secs", dreamSecs);

  esp_deep_sleep_start();
}
