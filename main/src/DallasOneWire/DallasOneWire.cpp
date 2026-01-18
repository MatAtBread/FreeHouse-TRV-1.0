#include "pins.h"
#include "DallasOneWire.h"

#include <math.h>

#include "../../trv.h"

extern "C" {
#include "ds18b20.h"
#include "ow_rom.h"
}

static void retryReset(OW *ow) {
    int i = 0;
    for (i = 0; i < 5; i++) {
      delay(45);
      if (ow_reset(ow) == ESP_OK)
        break;
    }
    if (i == 5) {
      ESP_LOGE(TAG, "DallasOneWire: RESET FAILED AFTER RETRIES");
      return;
    }
  }

DallasOneWire::DallasOneWire(float& temp, uint8_t resolution) : temp(temp), resolution(resolution) {
  if (ow_init(&ow, DTEMP) != ESP_OK) {
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
  if (ow_reset(&ow) != ESP_OK) goto fail;
  ow_send(&ow, OW_SKIP_ROM);
  ow_send(&ow, DS18B20_WRITE_SCRATCHPAD);
  ow_send(&ow, 0x70);
  ow_send(&ow, 0x90);
  ow_send(&ow, 0x1F | (res << 5));  // Configuration byte resolution

  if (ow_reset(&ow) != ESP_OK) goto fail;
  ow_send(&ow, OW_SKIP_ROM);
  ow_send(&ow, DS18B20_COPY_SCRATCHPAD);
  do {
    delay(2);
  } while (ow_read(&ow) == 0);
  ESP_LOGI(TAG, "DallasOneWire: Set resolution complete");
  if (ow_reset(&ow) != ESP_OK) goto fail;
  configuring = false;
  return;

fail:
  ESP_LOGW(TAG, "DallasOneWire: FAILED TO SET RESOLUTION");
  configuring = false;
  return;
}

void DallasOneWire::task() {
  uint16_t data;
  uint8_t targetConfig = 0x1F | (resolution << 5);

  // Apply resolution on boot (Volatile only)
  if (ow_reset(&ow) == ESP_OK) {
    ow_send(&ow, OW_SKIP_ROM);
    ow_send(&ow, DS18B20_WRITE_SCRATCHPAD);
    ow_send(&ow, 0x70); // TH (Don't care)
    ow_send(&ow, 0x90); // TL (Don't care)
    ow_send(&ow, targetConfig);
  }

  if (ow_reset(&ow) != ESP_OK) goto fail;

  ow_send(&ow, OW_SKIP_ROM);
  ow_send(&ow, DS18B20_CONVERT_T);
  do {
    delay(2);
  } while (ow_read(&ow) == 0);

  if (ow_reset(&ow) != ESP_OK) goto fail;
  ow_send(&ow, OW_SKIP_ROM);
  ow_send(&ow, DS18B20_READ_SCRATCHPAD);

  // Read first 5 bytes to get temp and verify config
  uint8_t scratchpad[5];
  for(int i=0; i<5; i++) scratchpad[i] = ow_read(&ow);

  data = scratchpad[0] | (scratchpad[1] << 8);
  if (data == 0xFFFF) {
    ESP_LOGE(TAG, "DallasOneWire: READ FAILED");
    retryReset(&ow);
    goto fail;
  } else {
    temp = (signed)(data) / 16.0;
    ESP_LOGI(TAG, "Temp is %f", temp);
  }
  return;

fail:
  ESP_LOGW(TAG, "DallasOneWire: FAILED TO RESET");
  return;
}
