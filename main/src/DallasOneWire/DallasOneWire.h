#ifndef DALLAS_ONE_WIRE_H
#define DALLAS_ONE_WIRE_H

#include "../WithTask.h"

extern "C" {
#include "onewire.h"
}

class DallasOneWire: public WithTask {
 protected:
  uint8_t pin;
  OW ow;
  float& temp;

 public:
  DallasOneWire(const uint8_t pin, float &temp);
  ~DallasOneWire();
  float readTemp();
  void task();
};
#endif