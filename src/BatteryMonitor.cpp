#include "BatteryMonitor.h"

#include <esp32-hal-adc.h>
#include <esp32-hal-gpio.h>

BatteryMonitor::BatteryMonitor(uint8_t adc, uint8_t chargeIoPin) : adc(adc), pin(chargeIoPin) {
  analogRead(adc);
  pinMode(pin, INPUT);
};

bool BatteryMonitor::isCharging() {
  return digitalRead(pin) != 0;
}

uint32_t BatteryMonitor::getRawValue() {
  return analogReadMilliVolts(adc) * 2;
}

uint32_t BatteryMonitor::getValue() {
  auto a = getRawValue();
  int n = 1;
  for (int i=0; i<5; i++) {
    uint32_t b = getRawValue();
    if (b>0) {
      a += b;
      n += 1;
    }
    delay(50);
  }
  return a/n;
}

uint8_t BatteryMonitor::getPercent(uint32_t raw) {
  if (raw == NO_VALUE)
    raw = analogReadMilliVolts(adc) * 2;

  auto percent = (raw - 3000U) / 8;
  if (percent < 0)
    percent = 0;
  else if (percent > 100)
    percent = 100;
  return (uint8_t)percent;
}
