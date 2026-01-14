#include "../trv.h"
#include "esp_err.h"
#include <unistd.h>

#include "nvs_flash.h"
#include "fs.h"

TrvFS::TrvFS() {
}

TrvFS::~TrvFS() {
}

__SIZE_TYPE__ TrvFS::read(const char *name, void *p, __SIZE_TYPE__ size) {
  nvs_handle_t nvs_handle = 0;
  __SIZE_TYPE__ len = size;

  auto err = nvs_open("storage", NVS_READONLY, &nvs_handle);
  if (err == ESP_ERR_NVS_NOT_INITIALIZED) {
      nvs_flash_init();
      err = nvs_open("storage", NVS_READONLY, &nvs_handle);
  }

  auto failed = err != ESP_OK
    || nvs_get_blob(nvs_handle, "trv1", p, &len) != ESP_OK;
  if (nvs_handle) nvs_close(nvs_handle);
  return failed ? 0 : len; // We allow mis-sized reads for versioning
}

bool TrvFS::write(const char *name, void *p, __SIZE_TYPE__ size) {
  nvs_handle_t nvs_handle = 0;
  auto err = nvs_open("storage", NVS_READWRITE, &nvs_handle);
  if (err == ESP_ERR_NVS_NOT_INITIALIZED) {
      nvs_flash_init();
      err = nvs_open("storage", NVS_READWRITE, &nvs_handle);
  }
  auto failed = err != ESP_OK
    || nvs_set_blob(nvs_handle, "trv1", p, size) != ESP_OK;
  if (nvs_handle) nvs_close(nvs_handle);
  return !failed;
}
