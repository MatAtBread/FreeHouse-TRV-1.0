// Helper macros for double-stringification
#ifndef BUILD_FREEHOUSE_MODEL
#error "BUILD_FREEHOUSE_MODEL is not defined! Check your build system/command line."
#endif

#define STRINGIZE_HELPER(x) #x
#define STRINGIZE(x) STRINGIZE_HELPER(x)
#define FREEHOUSE_MODEL STRINGIZE(BUILD_FREEHOUSE_MODEL)

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

#ifdef __cplusplus
extern "C" {
#endif

extern const char *TAG;
extern char versionDetail[];

#ifdef __cplusplus
}
#endif
