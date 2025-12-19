#include "pwm.h"
#include "driver/ledc.h"

// Global state for the PWM channel
static ledc_channel_t pwm_channel = LEDC_CHANNEL_MAX;
static ledc_timer_t pwm_timer = LEDC_TIMER_MAX;
static uint8_t last_duty = 0;

esp_err_t pwm_init(gpio_num_t gpio_pin, uint8_t duty) {
    if (pwm_channel != LEDC_CHANNEL_MAX) return ESP_ERR_INVALID_STATE;  // Already init
    gpio_reset_pin(gpio_pin);

    // Configure timer: 1kHz, 6-bit resolution (0-64 max)
    ledc_timer_config_t timer_config = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_6_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = 8000,
        .clk_cfg = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK_WITHOUT_ABORT(ledc_timer_config(&timer_config));

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
    ESP_ERROR_CHECK_WITHOUT_ABORT(ledc_channel_config(&channel_config));

    pwm_channel = LEDC_CHANNEL_0;
    pwm_timer = LEDC_TIMER_0;
    pwm_set_duty(gpio_pin, duty);
    return ESP_OK;
}

void pwm_set_duty(gpio_num_t gpio_pin, uint8_t duty) {  // 0=off, 64=100% on
    if (pwm_channel == LEDC_CHANNEL_MAX) return;
    duty = (duty > 64) ? 64 : duty;  // Clamp
    if (pwm_get_duty(gpio_pin) != duty) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(ledc_set_duty(LEDC_LOW_SPEED_MODE, pwm_channel, last_duty = duty));
        ESP_ERROR_CHECK_WITHOUT_ABORT(ledc_update_duty(LEDC_LOW_SPEED_MODE, pwm_channel));
    }
}

uint8_t pwm_get_duty(gpio_num_t gpio_pin) {
    return last_duty;
}