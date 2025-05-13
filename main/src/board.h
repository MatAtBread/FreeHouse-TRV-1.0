#ifndef BOARD_H
#define BOARD_H

// #ifdef __cplusplus
// extern "C" {
// #endif

#include "esp_wifi.h"

esp_err_t dev_wifi_init(const wifi_init_config_t *config);
esp_err_t dev_wifi_deinit(void);

// #ifdef __cplusplus
// }
// #endif
#endif