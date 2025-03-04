// the setup function runs once when you press reset or power the board
#define TOUCH_PIN A1

#include "trv.h"

#include <WiFi.h>
#include <ZigbeeEP.h>
#include <esp_coexist.h>

#include "src/ZigbeeTRV.h"
#include "src/WithTask.h"
#include "src/CaptiveWifi.h"
#include "src/trv-state.h"

RTC_DATA_ATTR int wakeCount = 0;
#undef LED_BUILTIN
#define LED_BUILTIN 15

class HeartMonitor: public Heartbeat {
  public:
    HeartMonitor() {
      // Default - do a short flash every 500ms
      // ledcAttach(LED_BUILTIN, 2 /* Hz */, 8);
      // ledcWrite(LED_BUILTIN, 16);
      pinMode(LED_BUILTIN, OUTPUT);
      digitalWrite(LED_BUILTIN, LOW);
    }

    ~HeartMonitor() {
      // In reality, this won't get called as the heartbeat only stops when we go into deep sleep
      digitalWrite(LED_BUILTIN, HIGH);
      // ledcWrite(LED_BUILTIN, 0);
      // ledcDetach(LED_BUILTIN);
    }
};

ZigbeeTRV* zbTrv;

void tryZigbee() {
  zbTrv = new ZigbeeTRV(1);
  _log("Zigbee coexistance %d...", esp_coex_wifi_i154_enable());

  zbTrv->setManufacturerAndModel("FreeHouse", "TRV");
  zbTrv->setPowerSource(zb_power_source_t::ZB_POWER_SOURCE_BATTERY);
  Zigbee.addEndpoint(zbTrv);

  Zigbee.begin(ZIGBEE_END_DEVICE);
  _log("Startied zigbee");
}

// Sample the touch ADC for up to 1050ms to see if it was touched for the whole second
// bails returning false if it was released/not touched suring that period.
bool touchButtonPressed() {
  bool touch = false;
  for (int i = 0; i <= 6; i++) {
    auto n = analogRead(TOUCH_PIN);
    _log("Touch test %u", (unsigned int)n);
    if (n < 0x100)
      break;
    if (i == 6) {
      _log("Touch button pressed");
      touch = true;
      break;
    }
    delay(150);
  }
  return touch;
}

void setup() {
  auto wakeCause = esp_sleep_get_wakeup_cause();
  wakeCount += 1;
  analogRead(TOUCH_PIN);
  // Serial.begin(115200);
  // ddelay(1000); // Debug delay for Serial

  // Enter WiFi config mode on hard reset or touch
  bool touch = touchButtonPressed() || wakeCause == ESP_SLEEP_WAKEUP_UNDEFINED;
  bool dreaming = (wakeCount & 7)==0;
  _log("Wake cause: %d #%d, touch: %d, dreaming: %d", wakeCause, wakeCount, touch, dreaming);
  Heartbeat* heartbeat = new HeartMonitor();

  if (touch || dreaming) {
    Trv* trv = new Trv(heartbeat);
    if (wakeCause == ESP_SLEEP_WAKEUP_UNDEFINED) {
      trv->resetValve();
    }

    if (touch) {
      _log("Touch button pressed");
      // Note: we DON'T tryZigbee() here as ESP32-C6 can't do a WiFi AP mode and start the ZB stack at the same time
      new CaptivePortal(heartbeat, trv);
    } else if (dreaming) {
      _log("Dreaming");
      if (trv->flatBattery()) {
        _log("Battery exhausted");
        heartbeat->cardiacArrest(86400000U);
      } else {
        //ToDo
        // Process any zigbee msgs...
        tryZigbee();
        // Every 8 * 7.5 (one minute) check if the valve needs updating in auto-mode.
        trv->checkAutoState();

        // auto state = trv->getState();
        // and update ZB state esp_zb_zcl_update_reporting_info;
        heartbeat->ping(50);
      }
    }
  } else {
    _log("Ping");
    // Process any zigbee msgs...
    tryZigbee();
    // ...and go to sleep
    heartbeat->ping(0);
  }
}

extern "C" void app_main() {
  esp_log_level_set(TAG, ESP_LOG_INFO);

  initArduino();
  setup();
  vTaskDelete(NULL);
  //while(1) vTaskDelay(portMAX_DELAY);
}

