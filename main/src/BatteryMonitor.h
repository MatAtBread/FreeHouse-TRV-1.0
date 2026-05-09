#ifndef BATTERY_MONITOR_H
#define BATTERY_MONITOR_H

#include <stdint.h>

class BatteryMonitor {
 private:
  int getRawValue();

 public:
  BatteryMonitor();
  bool is_charging();
  uint8_t getPercent(int raw);
  int getValue(int samples = 3); // Get an average value
};

#endif