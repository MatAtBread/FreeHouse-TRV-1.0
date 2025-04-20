#ifndef TRV_H
#define TRV_H

#include "portmacro.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"


#define _log(format, ...) esp_log_write(ESP_LOG_INFO, TAG, LOG_FORMAT(I, format), esp_log_timestamp(), TAG __VA_OPT__(,) __VA_ARGS__)
#define delay(n)  vTaskDelay(n / portTICK_PERIOD_MS)
#define millis()  esp_log_timestamp() // (unsigned long)(esp_timer_get_time() / 1000ULL)

#endif // TRV_H

#undef LED_BUILTIN
#define LED_BUILTIN 15

extern "C" const char *TAG;