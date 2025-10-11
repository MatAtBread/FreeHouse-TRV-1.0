#include "../../trv.h"

#include <math.h>

#include "DallasOneWire.h"
#include "driver/gpio.h"
#include "hal/gpio_types.h"

extern "C" {
#include "ds18b20.h"
#include "ow_rom.h"
}

DallasOneWire::DallasOneWire(const uint8_t pin, float& temp): pin(pin), temp(temp) {
  if (!ow_init(&ow, pin)) {
    ESP_LOGW(TAG, "DallasOneWire: FAILED TO INIT DS18B20");
    return;
  }
  StartTask(DallasOneWire);
}

DallasOneWire::~DallasOneWire() {
  wait();
  while (configuring) delay(50);
  ow_deinit(&ow);
}

float DallasOneWire::readTemp() {
  wait();
  return temp;
}

void DallasOneWire::setResolution(uint8_t res /* 0-3 */) {
  configuring = true;
  // Wait for any pending temperature reads to complete
  wait();

  ESP_LOGI(TAG, "DallasOneWire: Set resolution to %u", res);
  ow_reset(&ow);
  ow_send(&ow, OW_SKIP_ROM);
  ow_send(&ow, DS18B20_WRITE_SCRATCHPAD);
  ow_send(&ow, 0x70);
  ow_send(&ow, 0x90);
  ow_send(&ow, 0x1F | (res << 5));  // Configuration byte resolution

  ow_reset(&ow);
  ow_send(&ow, OW_SKIP_ROM);
  ow_send(&ow, DS18B20_COPY_SCRATCHPAD);
  do { delay(10); } while (ow_read(&ow) == 0);
  ESP_LOGI(TAG, "DallasOneWire: Set resolution complete");
  ow_reset(&ow);
  configuring = false;
}

void DallasOneWire::task() {
  if (!ow_reset(&ow)) {
    ESP_LOGW(TAG, "DallasOneWire: FAILED TO RESET DS18B20");
    return;
  }

  // uint64_t romcode = 0ULL;
  // ow_romsearch(&ow, &romcode, 1, OW_SEARCH_ROM);
  // if (romcode == 0ULL) {
  //   ESP_LOGW(TAG, "DallasOneWire: FAILED TO FIND DS18B20");
  //   return;
  // }

  ow_send(&ow, OW_SKIP_ROM);
  ow_send(&ow, DS18B20_CONVERT_T);
  do { delay(10); } while (ow_read(&ow) == 0);

  ow_reset(&ow);
  ow_send(&ow, OW_SKIP_ROM);
  ow_send(&ow, DS18B20_READ_SCRATCHPAD);
  temp = (signed)(ow_read(&ow) | (ow_read(&ow) << 8)) / 16.0;
  ESP_LOGI(TAG,"Temp is %f", temp);
  ESP_LOGI(TAG, "DS18B20 scratchpad %02x %02x %02x", ow_read(&ow), ow_read(&ow), ow_read(&ow));
  ow_reset(&ow);
}
