#include "board.h"
#include "../../common/gpio/gpio.hpp"

esp_err_t dev_wifi_init(const wifi_init_config_t *config) {
    GPIO::pinMode(3, OUTPUT);
    GPIO::pinMode(14, OUTPUT);
    GPIO::digitalWrite(3, 0);
    GPIO::digitalWrite(14, 0);
    return esp_wifi_init(config);
}

esp_err_t dev_wifi_deinit(void) {
    GPIO::pinMode(3, INPUT);
    GPIO::pinMode(14, INPUT);
    return esp_wifi_deinit();
}
