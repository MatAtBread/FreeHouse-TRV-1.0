// the setup function runs once when you press reset or power the board

#include "common/gpio/gpio.hpp"
#include "esp_app_desc.h"
#include "esp_netif.h"
#include "esp_sleep.h"
#include "net/esp-now.hpp"
#include "nvs_flash.h"
#include "pins.h"
#include "src/CaptiveWifi.h"
#include "src/WithTask.hpp"
#include "src/trv-state.h"
#include "trv.h"
#include "src/TouchButton.hpp"

extern "C" {
const char* TAG = "TRV";
}

static RTC_DATA_ATTR int messgageChecks = 0;
static RTC_DATA_ATTR int wakeCount = 0;
char versionDetail[110] = {0};

uint32_t woken() {
  Trv trv; // Loads static state from FS
  if (debugFlag(DEBUG_LOG_INFO))
    esp_log_level_set(TAG, ESP_LOG_INFO);

  if (trv.flatBattery() && !trv.is_charging()) {
    ESP_LOGW(TAG, "Battery exhausted");
    // Skip tidy up - we're dead
    esp_sleep_enable_ext1_wakeup(1ULL << TOUCH_PIN, ESP_EXT1_WAKEUP_ANY_HIGH);
    return 0x7FFFFFFF;
  }

  GPIO::pinMode(LED_BUILTIN, OUTPUT);
  GPIO::digitalWrite(LED_BUILTIN, false);
  EspNet net; // Start Wi-Fi based on Trv state (loaded above)

  uint32_t dreamSecs = 1;
  TouchButton touchButton;
  if (!trv.deviceName()[0] || touchButton.pressed() == PRESSED) {
    ESP_LOGI(TAG, "Touch button pressed / device name '%s'", trv.deviceName());
    CaptivePortal portal(&trv, trv.deviceName());
    switch (portal.exitStatus) {
      case exit_status_t::CALIBRATE:
        trv.calibrate();
        break;
      case exit_status_t::TEST_MODE:
        trv.testMode(touchButton);
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
    const auto config = trv.getConfig();
    net.checkMessages(&trv);
    int checkEvery = 60 / config.sleep_time;
    ESP_LOGI(TAG, "checkForMessages %d / %d", messgageChecks, checkEvery);
    if (messgageChecks >= checkEvery) {
      // Every 60 seconds re-set the system-mode to ensure the TRV acts to correct things like motor time-outs or temperature changes
      trv.setSystemMode(config.system_mode);
      messgageChecks = 0;
    } else {
      messgageChecks += 1;
    }
    dreamSecs = config.sleep_time;
  }
  WithTask::waitForAllTasks();
  net.sendStateToHub(&trv);

  uint64_t ext1WakeMask = (1ULL << TOUCH_PIN);
  if (!trv.is_charging()) {
    // Ideally, we'd wake on CHARGING changed, but in the current h/w this is not
    // an RTC_GPIO. On Rev3.2, the CHARGING pin is connected to GPIO2, so we can use that
    // if we're not alreday charghing (if we are, it would wake immediately)
    ext1WakeMask |= (1ULL << 2);
  }

  // Prepare to sleep. Wake on touch or timeout
  // Ideally, we'd wake on CHARGING changed, but in the current h/w this is not
  // an RTC_GPIO. On Rev3.2, the CHARGIBG pin is connected to GPIO2, so we can use that.
  esp_sleep_enable_ext1_wakeup(ext1WakeMask, ESP_EXT1_WAKEUP_ANY_HIGH);

  GPIO::digitalWrite(LED_BUILTIN, true);
  GPIO::pinMode(TOUCH_PIN, INPUT);
  return dreamSecs;
}

extern "C" void app_main() {
  esp_log_level_set("*", ESP_LOG_WARN);
  esp_log_level_set("wifi", ESP_LOG_ERROR);

  auto wakeCause = esp_sleep_get_wakeup_cause();
  auto resetCause = esp_reset_reason();
  wakeCount += 1;

  const auto app = esp_app_get_description();
  snprintf((char*)versionDetail, sizeof versionDetail, "%s %s %s",
           app->version, app->date, app->time);
  ESP_LOGI(TAG, "Build: %s. Wake: %d reset: %d count: %d",
    versionDetail, wakeCause, resetCause, wakeCount);

  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());

  auto dreamSecs = woken();

  esp_sleep_enable_timer_wakeup(dreamSecs * 1000000ULL);
  ESP_LOGW(TAG, "TRV device '%s' dbg=0x%04x. Deep sleep %u secs\n", Trv::deviceName(), debugFlag(DEBUG_ALL), dreamSecs);

  esp_deep_sleep_start();
}

