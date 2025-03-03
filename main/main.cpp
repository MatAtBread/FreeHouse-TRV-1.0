#include "Arduino.h"

extern "C" void app_main() {
  static const uint8_t LED_BUILTIN = 15;
  initArduino();
  pinMode(LED_BUILTIN, OUTPUT);
  int x = 0;
  while (1) {
    Serial.printf("Hello world %d\n", x++);
    digitalWrite(LED_BUILTIN, LOW);
    delay(1000);
    digitalWrite(LED_BUILTIN, HIGH);
    delay(1000);
  }
}