#ifndef DALLAS_ONE_WIRE_H
#define DALLAS_ONE_WIRE_H

#include "../WithTask.h"

extern "C" {
#include "onewire.h"
}

class DallasOneWire: public WithTask {
 protected:
  OW ow;
  float& temp;
  bool configuring = false;
  /*
  00: 9-bit resolution (0.5°C, 93.75ms conversion time)
  01: 10-bit resolution (0.25°C, 187.5ms conversion time)
  10: 11-bit resolution (0.125°C, 375ms conversion time)
  11: 12-bit resolution (0.0625°C, 750ms conversion time, power-up default)
  */

 public:
  DallasOneWire(float &temp);
  ~DallasOneWire();
  void setResolution(uint8_t res);
  float readTemp();
  void task();
};
#endif