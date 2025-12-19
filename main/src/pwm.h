#include "esp_err.h"
#include "esp_err.h"
#include "hal/gpio_types.h"

esp_err_t pwm_init(gpio_num_t gpio_pin, uint8_t duty);
void pwm_set_duty(gpio_num_t gpio_pin, uint8_t duty);  // 0=off, 64=100% on
uint8_t pwm_get_duty(gpio_num_t gpio_pin);
