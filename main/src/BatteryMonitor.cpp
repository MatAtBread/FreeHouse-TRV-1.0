#include "pins.h"
#include "../trv.h"
#include "../common/gpio/gpio.hpp"
#include "BatteryMonitor.h"

#define DISCHARGE_FLOOR 3200

BatteryMonitor::BatteryMonitor() {
  GPIO::pinMode(CHARGING, INPUT);
  getRawValue();
};

bool BatteryMonitor::is_charging() {
  return GPIO::digitalRead(CHARGING) != 0;
}

int BatteryMonitor::getRawValue() {
  return GPIO::analogReadMilliVolts(BATTERY) * 2;
}

int BatteryMonitor::getValue(int samples) {
  auto a = 0;
  for (int i=0; i<samples; i++) {
    delay(10);
    a += getRawValue();
  }

  return a / samples;
}

uint8_t BatteryMonitor::getPercent(int raw) {
  if (raw == NO_VALUE)
    raw = getRawValue();

  if (raw < 0) {
    // Something bad happend - just ignore it for now
    return 50;
  }

  if (is_charging()) {
    raw -= 150; // 0.15v is a typical charge bias voltage for constant current charge circuit
  }

  auto percent = 100 * (raw - DISCHARGE_FLOOR) / (3900 - DISCHARGE_FLOOR);
  if (percent < 0)
    percent = 0;
  else if (percent > 100)
    percent = 100;
  return (uint8_t)percent;
}
