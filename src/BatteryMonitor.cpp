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

uint16_t BatteryMonitor::getRawValue() {
  return analogReadMilliVolts(adc) * 2;
}

uint16_t BatteryMonitor::getValue() {
  unsigned int a = getRawValue();
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

uint16_t BatteryMonitor::getPercent(int raw) {
  if (raw < 0)
    raw = analogReadMilliVolts(adc) * 2;

  auto percent = ((int)raw - 3000) / 8;
  if (percent < 0)
    percent = 0;
  else if (percent > 100)
    percent = 100;
  return percent;
}
