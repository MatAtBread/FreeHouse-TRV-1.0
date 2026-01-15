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

extern "C" {
const char* TAG = "TRV";
}

// Sample the touch ADC for up to 420ms to see if it was touched for the whole
// second. Bails returning false if it was released/not touched during that
// period.
typedef enum { WAIT,
               PRESSED,
               NOT_PRESSED } TouchState;
class TouchButton : public WithTask {
 protected:
  TouchState state;

  void task() override {
    for (int i = 0;; i++) {
      auto n = GPIO::analogRead(TOUCH_PIN);
      ESP_LOGI(TAG, "Touch test %d", n);
      if (n >= 0 && n < 256) {
        state = NOT_PRESSED;
        return;
      }
      if (i == 6) {
        ESP_LOGI(TAG, "Touch button pressed");
        state = PRESSED;
        return;
      }
      delay(70);
    }
  }

 public:
  TouchButton() : WithTask() {
    reset();
  }
  bool pressed() {
    ESP_LOGI(TAG, "Touch button (1) state %d running %p", state, running);
    wait();
    ESP_LOGI(TAG, "Touch button (2) state %d running %p", state, running);
    return state == PRESSED;
  };
  void reset() {
    state = WAIT;
    StartTask(TouchButton);
  }
};

static RTC_DATA_ATTR int messgageChecks = 0;
void checkForMessages(Trv* trv) {
  // Create `net` based on config
  // auto state = Trv::getLastState();
  // ESP_LOGI(TAG, "checkIncomingMessages using netMode %u", state->netMode);
  // switch (state->netMode) {
  //   case NET_MODE_ESP_NOW:
  EspNet net(trv);
  //   break;
  //   case NET_MODE_MQTT:
  //   break;
  //   case NET_MODE_ZIGBEE:
  //   break;
  // }

  net.checkMessages();
  // Note, if there were any messages that confifgured the heat settings,
  // checkAutoState wuill have been called as part of their processing. So here,
  // we only need to check the auto state for temperature changes. We do this
  // every 60-120 seconds (min, might be more if sleep_time is large) to avoid
  // excessive checking and the valve moving too often, and to give the device
  // temperature time to settle after a change (which drives up the internal
  // temperature and causes resonance)
  // Check every 60 seconds, which is (usually) inferred from the config sleep_time
  const auto config = trv->getState(true).config;
  int checkEvery = 60 / config.sleep_time;
  ESP_LOGI(TAG, "checkForMessages %d / %d", messgageChecks, checkEvery);
  if (messgageChecks >= checkEvery) {
    trv->setSystemMode(config.system_mode);
    messgageChecks = 0;
  } else {
    messgageChecks += 1;
  }
  WithTask::waitForAllTasks();
  net.sendStateToHub(trv->getState(false));
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

  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());

  // ESP_LOGI(TAG,"Heap %lu",esp_get_free_heap_size());
  ESP_LOGI(TAG, "Create TRV");
  Trv* trv = new Trv();
  uint32_t dreamSecs = 1;

  if (trv->flatBattery() && !trv->is_charging()) {
    ESP_LOGI(TAG, "Battery exhausted");
    dreamSecs = 60 * 60;
  } else {
    TouchButton touchButton;
    ESP_LOGI(TAG, "Check touch button/device name");
    if (!trv->deviceName()[0] || touchButton.pressed() == PRESSED) {
      if (resetCause == ESP_RST_DEEPSLEEP) {
         nvs_flash_init();
      }
      ESP_LOGI(TAG, "Touch button pressed / device name '%s'",
               trv->deviceName());
      CaptivePortal portal(trv, trv->deviceName());
      switch (portal.exitStatus) {
        case exit_status_t::CALIBRATE: {
          trv->calibrate();
          dreamSecs = 1;
        } break;
        case exit_status_t::TEST_MODE: {
          auto state = trv->getState(true);
          delete trv;
          ESP_LOGI(TAG, "Enter test mode");
          // In test mode, we just cycle the valve and print the count
          BatteryMonitor* battery = new BatteryMonitor();
          uint8_t currentPosition = 0;
          MotorController* motor =
              new MotorController(battery, currentPosition, state.config.motor);
          int count = 0;
          GPIO::digitalWrite(LED_BUILTIN, false);
          while (true) {
            ESP_LOGI(TAG, "Test cycle %d", count++);

            float temp = 0;
            DallasOneWire tempSensor(temp);

            const int target = count & 1 ? 100 : 0;
            motor->setValvePosition(target);
            motor->wait();
            if (motor->getValvePosition() != target) {
              ESP_LOGW(TAG, "Failed to reach target %d, got %d. Sleeping.",
                       target, motor->getValvePosition());
            }
            delay(2000);
          }
          touchButton.reset();
          if (touchButton.wait() == PRESSED) {
            ESP_LOGI(TAG, "Exit test mode on touch");
            dreamSecs = 1;
            break;
          }
        } break;
        case exit_status_t::POWER_OFF:
          ESP_LOGI(TAG, "Power off requested");
          dreamSecs = 0x7FFFFFFF;  // 30 * 24 * 60 * 60;
          break;
        case exit_status_t::CLOSED:
        case exit_status_t::NONE:
        case exit_status_t::TIME_OUT:
          dreamSecs = 1;
          break;
      }
      EspNet net(trv);
      net.sendStateToHub(trv->getState(false));
    } else {
      const auto config = trv->getState(true).config;
      if (resetCause != ESP_RST_DEEPSLEEP) {
        resetCause = ESP_RST_DEEPSLEEP;  // To suppress further reset in no-sleep mode
        trv->setSystemMode(config.system_mode);
      }
      checkForMessages(trv);
      dreamSecs = config.sleep_time;
    }
  }

  delete trv;

  // Prepare to sleep. Wake on touch or timeout
  GPIO::digitalWrite(LED_BUILTIN, true);
  GPIO::pinMode(TOUCH_PIN, INPUT);

  // Ideally, we'd wake on CHARGING changed, but in the current h/w this is not
  // an RTC_GPIO
  esp_sleep_enable_ext1_wakeup(1ULL << TOUCH_PIN, ESP_EXT1_WAKEUP_ANY_HIGH);
  esp_sleep_enable_timer_wakeup(dreamSecs * 1000000ULL);
  ESP_LOGI(TAG, "deep sleep %u secs", dreamSecs);

  esp_deep_sleep_start();
}
