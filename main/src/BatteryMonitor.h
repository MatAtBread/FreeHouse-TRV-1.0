#ifndef BATTERY_MONITOR_H
#define BATTERY_MONITOR_H

#include <stdint.h>

#define NO_VALUE -1
class BatteryMonitor {
 protected:
  int getRawValue();

 public:
  BatteryMonitor();
  bool is_charging();
  uint8_t getPercent(int raw = NO_VALUE);
  int getValue(int samples = 3); // Get an average value
};

#endif