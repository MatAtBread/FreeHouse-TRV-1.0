#include "pins.h"
#include "driver/gpio.h"
#include "esp_rom_sys.h"
#include "DallasOneWire.h"

#include <math.h>

#include "../../trv.h"
#include "gpio.hpp"

extern "C" {
#include "ds18b20.h"
#include "ow_rom.h"
}


DallasOneWire::DallasOneWire(float& temp, uint8_t resolution) : temp(temp), resolution(resolution) {
  // Bit-bang a reset pulse BEFORE RMT init to recover DS18B20 from any
  // stuck protocol state persisting across deep sleep.
  // Uses esp_rom_delay_us() because FreeRTOS delay(1) rounds to 0 at 500Hz tick rate.
  gpio_set_direction(DTEMP, GPIO_MODE_OUTPUT_OD);
  gpio_set_level(DTEMP, 0);           // Pull bus low
  esp_rom_delay_us(960);              // 2x minimum reset pulse (480µs) for reliability
  gpio_set_level(DTEMP, 1);           // Release bus
  esp_rom_delay_us(480);              // Wait for presence pulse window to pass

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
  GPIO::sleepState(DTEMP, true);
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
  {
    uint32_t copyStart = millis();
    do {
      delay(2);
      if (millis() - copyStart > 100) {
        ESP_LOGE(TAG, "DallasOneWire: copy scratchpad timeout");
        goto fail;
      }
    } while (ow_read(&ow) == 0);
  }
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

  // Establish communication with retry + recovery.
  // On first attempt, just try a reset. If it fails, flush any pending
  // DS18B20 data (in case it's stuck mid-transfer from before deep sleep),
  // then retry. Flush only happens after a failed reset — sending 0xFF to
  // an idle DS18B20 would be interpreted as an invalid ROM command.
  int attempts;
  for (attempts = 0; attempts < 3; attempts++) {
    if (ow_reset(&ow) == ESP_OK) break;
    ESP_LOGW(TAG, "DallasOneWire: reset attempt %d failed, flushing bus", attempts + 1);
    for (int i = 0; i < 10; i++) ow_send(&ow, 0xFF);
    delay(10);
  }
  if (attempts == 3) goto fail;
  if (attempts > 0) ESP_LOGI(TAG, "DallasOneWire: recovered after %d retries", attempts);

  // Apply resolution on boot (Volatile only)
  ow_send(&ow, OW_SKIP_ROM);
  ow_send(&ow, DS18B20_WRITE_SCRATCHPAD);
  ow_send(&ow, 0x70); // TH (Don't care)
  ow_send(&ow, 0x90); // TL (Don't care)
  ow_send(&ow, targetConfig);

  uint32_t t;
  if (ow_reset(&ow) != ESP_OK) goto fail;

  ow_send(&ow, OW_SKIP_ROM);
  ow_send(&ow, DS18B20_CONVERT_T);
  t = millis();
  do {
    delay(2);
    if (millis() - t > 1000) {
      ESP_LOGE(TAG, "DallasOneWire: conversion timeout");
      goto fail;
    }
  } while (ow_read(&ow) == 0);
  t = millis() - t;
  if (ow_reset(&ow) != ESP_OK) goto fail;
  ow_send(&ow, OW_SKIP_ROM);
  ow_send(&ow, DS18B20_READ_SCRATCHPAD);

  // Read first 5 bytes to get temp and verify config
  uint8_t scratchpad[5];
  for(int i=0; i<5; i++) scratchpad[i] = ow_read(&ow);

  data = scratchpad[0] | (scratchpad[1] << 8);
  if (data == 0xFFFF) {
    ESP_LOGE(TAG, "DallasOneWire: READ FAILED");
    goto fail;
  } else {
    temp = (signed)(data) / 16.0;
    ESP_LOGI(TAG, "Temp is %f, r=0x%02x [0x%02x 0x%02x 0x%02x], t=%lu", temp, targetConfig, scratchpad[2], scratchpad[3], scratchpad[4], t);
  }
  return;

fail:
  ESP_LOGW(TAG, "DallasOneWire: FAILED TO RESET");
  return;
}
