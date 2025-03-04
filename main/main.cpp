#include "Arduino.h"

#define TAG "TRV"
#define _log( format, ... ) ESP_LOG_LEVEL_LOCAL(ESP_LOG_INFO,    TAG, format __VA_OPT__(,) __VA_ARGS__)


extern "C" void app_main() {
  static const uint8_t LED_BUILTIN = 15;
  esp_log_level_set(TAG, ESP_LOG_INFO);

  initArduino();

  pinMode(LED_BUILTIN, OUTPUT);
  int x = 0;
  while (1) {
    _log("Hello world %d", x++);
    digitalWrite(LED_BUILTIN, LOW);
    delay(100);
    digitalWrite(LED_BUILTIN, HIGH);
    delay(1000);
  }
}