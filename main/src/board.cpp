#include "board.h"
#include "../../common/gpio/gpio.hpp"

esp_err_t dev_wifi_init(const wifi_init_config_t *config) {
    GPIO::pinMode(3, OUTPUT);
    GPIO::pinMode(14, OUTPUT);
    GPIO::digitalWrite(3, 0);
    GPIO::digitalWrite(14, 0);
    auto err = esp_wifi_init(config);
    if (err == ESP_OK) {
        wifi_country_t country = {
        .cc = "GB",
        .schan = 1,
        .nchan = 13,
        .policy = WIFI_COUNTRY_POLICY_MANUAL
        };
        err = esp_wifi_set_country(&country);
    }
    return err;
}

esp_err_t dev_wifi_deinit(void) {
    GPIO::pinMode(3, INPUT);
    GPIO::pinMode(14, INPUT);
    return esp_wifi_deinit();
}
