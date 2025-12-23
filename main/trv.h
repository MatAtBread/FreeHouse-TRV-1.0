#undef MODEL_L1 // Set for Lockshield hardware, unset for TRV hardware

#ifdef MODEL_L1
#define FREEHOUSE_MODEL "TRV4"
#else
#define FREEHOUSE_MODEL "TRV1"
#endif

#ifndef TRV_H
#define TRV_H

#include "portmacro.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"

#define _log(format, ...) esp_log_write(ESP_LOG_INFO, TAG, LOG_FORMAT(I, format), esp_log_timestamp(), TAG __VA_OPT__(,) __VA_ARGS__)
#define delay(n)  vTaskDelay(pdMS_TO_TICKS(n))
#define millis()  esp_log_timestamp() // (unsigned long)(esp_timer_get_time() / 1000ULL)

#endif // TRV_H

#undef LED_BUILTIN
#define LED_BUILTIN 15

#ifdef __cplusplus
extern "C" {
#endif

extern const char *TAG;
extern char versionDetail[];

#ifdef __cplusplus
}
#endif
