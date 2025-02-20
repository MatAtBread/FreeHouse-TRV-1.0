// #include "freertos/FreeRTOS.h"
// #include "freertos/task.h"

#include <math.h>

#include "DallasOneWire.h"

extern "C" {
#include "ds18b20.h"
#include "ow_rom.h"
}

DallasOneWire::DallasOneWire(const uint8_t pin): pin(pin) {
  romcode = 0ULL;
  ow_init(&ow, pin);
  if (ow_reset(&ow))
    ow_romsearch(&ow, &romcode, 1, OW_SEARCH_ROM);
}

float DallasOneWire::readTemp() {
  if (romcode == 0ULL)
    return INFINITY;

  ow_reset(&ow);
  ow_send(&ow, OW_SKIP_ROM);
  ow_send(&ow, DS18B20_CONVERT_T);
  while (ow_read(&ow) == 0);

  ow_reset(&ow);
  ow_send(&ow, OW_MATCH_ROM);
  for (int b = 0; b < 64; b += 8) {
    ow_send(&ow, romcode >> b);
  }
  ow_send(&ow, DS18B20_READ_SCRATCHPAD);
  int16_t temp = 0;
  temp = ow_read(&ow) | (ow_read(&ow) << 8);
  return temp / 16.0;
}
