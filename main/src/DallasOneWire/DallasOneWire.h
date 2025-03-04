#ifndef DALLAS_ONE_WIRE_H
#define DALLAS_ONE_WIRE_H

extern "C" {
#include "onewire.h"
}

class DallasOneWire {
 protected:
  uint8_t pin;
  OW ow;
  uint64_t romcode;

 public:
  DallasOneWire(const uint8_t pin);
  float readTemp();
};
#endif