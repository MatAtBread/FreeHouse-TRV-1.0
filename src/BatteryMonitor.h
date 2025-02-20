#ifndef BATTERY_MONITOR_H
#define BATTERY_MONITOR_H

#include <stdint.h>

class BatteryMonitor {
 protected:
  uint8_t adc;
  uint8_t pin;

 public:
  BatteryMonitor(uint8_t adc, uint8_t chargeIoPin);
  bool isCharging();
  uint16_t getRawValue();
  uint16_t getPercent(int raw = -1);
  uint16_t getValue(); // Get an average value
};

#endif