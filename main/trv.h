#include <Arduino.h>

#define TAG "TRV"
//#define _log(format, ...) esp_log_write(ESP_LOG_INFO, TAG, LOG_FORMAT(I, format), esp_log_timestamp(), TAG __VA_OPT__(,) __VA_ARGS__)
#define _log(format, ...) esp_log_write(ESP_LOG_INFO, TAG, "[%s] " format "\n", TAG __VA_OPT__(,) __VA_ARGS__)
