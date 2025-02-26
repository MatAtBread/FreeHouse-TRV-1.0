#ifndef BATTERY_MONITOR_H
#define BATTERY_MONITOR_H

#include <stdint.h>

#define NO_VALUE (uint32_t)-1
class BatteryMonitor {
 protected:
  uint8_t adc;
  uint8_t pin;
  uint32_t getRawValue();

 public:
  BatteryMonitor(uint8_t adc, uint8_t chargeIoPin);
  bool isCharging();
  uint8_t getPercent(uint32_t raw = NO_VALUE);
  uint32_t getValue(); // Get an average value
};

#endif