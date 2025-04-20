#ifndef BATTERY_MONITOR_H
#define BATTERY_MONITOR_H

#include <stdint.h>

#define NO_VALUE -1
class BatteryMonitor {
 protected:
  uint8_t adc;
  uint8_t pin;
  int getRawValue();

 public:
  BatteryMonitor(uint8_t adc, uint8_t chargeIoPin);
  bool is_charging();
  uint8_t getPercent(int raw = NO_VALUE);
  int getValue(); // Get an average value
};

#endif