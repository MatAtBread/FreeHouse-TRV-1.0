#include "driver/ledc.h"
#include "esp_err.h"

// Global state for the PWM channel
static ledc_channel_t pwm_channel = LEDC_CHANNEL_MAX;
static ledc_timer_t pwm_timer = LEDC_TIMER_MAX;

esp_err_t pwm_init(gpio_num_t gpio_pin) {
    if (pwm_channel != LEDC_CHANNEL_MAX) return ESP_ERR_INVALID_STATE;  // Already init

    // Configure timer: 1kHz, 6-bit resolution (0-64 max)
    ledc_timer_config_t timer_config = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_6_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = 1000,
        .clk_cfg = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_config));

    // Configure channel on specified GPIO
    ledc_channel_config_t channel_config = {
        .gpio_num = gpio_pin,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .hpoint = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&channel_config));

    pwm_channel = LEDC_CHANNEL_0;
    pwm_timer = LEDC_TIMER_0;
    return ESP_OK;
}

void pwm_set_duty(uint8_t duty) {  // 0=off, 64=100% on
    if (pwm_channel == LEDC_CHANNEL_MAX) return;
    duty = (duty > 64) ? 64 : duty;  // Clamp
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, pwm_channel, duty));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, pwm_channel));
}
