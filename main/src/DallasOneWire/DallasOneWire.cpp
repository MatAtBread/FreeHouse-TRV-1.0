#include "../../trv.h"

#include <math.h>

#include "DallasOneWire.h"

extern "C" {
#include "ds18b20.h"
#include "ow_rom.h"
}

DallasOneWire::DallasOneWire(const uint8_t pin, float& temp): pin(pin), temp(temp) {
  if (ow_init(&ow, pin))
    StartTask(DallasOneWire);
}

DallasOneWire::~DallasOneWire() {
  wait();
  ESP_ERROR_CHECK_WITHOUT_ABORT(rmt_disable(ow.tx_channel));
  ESP_ERROR_CHECK_WITHOUT_ABORT(rmt_disable(ow.rx_channel));
  ESP_ERROR_CHECK_WITHOUT_ABORT(rmt_del_channel(ow.tx_channel));
  ESP_ERROR_CHECK_WITHOUT_ABORT(rmt_del_channel(ow.rx_channel));
}

float DallasOneWire::readTemp() {
  wait();
  return temp;
}

void DallasOneWire::task() {
  uint64_t romcode = 0ULL;
  if (!ow_reset(&ow)) {
    ESP_LOGI(TAG, "DallasOneWire: FAILED TO RESET DS18B20");
    return;
  }

  ow_romsearch(&ow, &romcode, 1, OW_SEARCH_ROM);
  if (romcode == 0ULL) {
    ESP_LOGI(TAG, "DallasOneWire: FAILED TO FIND DS18B20");
    return;
  }

  // Set the temp conversion resolution
  ow_send(&ow, DS18B20_WRITE_SCRATCHPAD);
  ow_send(&ow, 0xFF);  // TH register (unused)
  ow_send(&ow, 0xFF);  // TL register (unused)
  ow_send(&ow, 0x3F);  // Configuration byte for 9-bit resolution [2][3][4]

  ow_reset(&ow);
  ow_send(&ow, OW_SKIP_ROM);
  ow_send(&ow, DS18B20_CONVERT_T);
  do { delay(10); } while (ow_read(&ow) == 0);

  ow_reset(&ow);
  ow_send(&ow, OW_MATCH_ROM);
  for (int b = 0; b < 64; b += 8) {
    ow_send(&ow, romcode >> b);
  }
  ow_send(&ow, DS18B20_READ_SCRATCHPAD);
  temp = (signed)(ow_read(&ow) | (ow_read(&ow) << 8)) / 16.0;
  ESP_LOGI(TAG,"Temp is %f", temp);
}
