#include "../trv.h"
#include "../common/gpio/gpio.hpp"
#include "BatteryMonitor.h"

BatteryMonitor::BatteryMonitor(uint8_t adc, uint8_t chargeIoPin) : adc(adc), pin(chargeIoPin) {
  GPIO::pinMode(pin, INPUT);
  getRawValue();
};

bool BatteryMonitor::is_charging() {
  return GPIO::digitalRead(pin) != 0;
}

int BatteryMonitor::getRawValue() {
  return GPIO::analogReadMilliVolts(adc) * 2;
}

int BatteryMonitor::getValue() {
  auto a = 0;
  for (int i=0; i<10; i++)
    a += getRawValue();

  return a / 10;
}

uint8_t BatteryMonitor::getPercent(int raw) {
  if (raw == NO_VALUE)
    raw = getRawValue();

  if (raw < 0) {
    // Something bad happend - just ignore it for now
    return 50;
  }

  if (is_charging()) {
    raw -= 150; // 0.15v is a typical charge bias voltage for CC charge circuit
  }

  auto percent = (raw - 3100) / 8; // 3.1-3.9v
  if (percent < 0)
    percent = 0;
  else if (percent > 100)
    percent = 100;
  return (uint8_t)percent;
}
