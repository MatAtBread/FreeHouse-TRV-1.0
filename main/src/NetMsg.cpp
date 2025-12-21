#include "esp_log.h"

#include "helpers.h"

#include "string.h"
#include "trv-state.h"
#include "trv.h"
#include "../common/gpio/gpio.hpp"
#include "cJSON.h"

extern const char *systemModes[];

#define FIELD(N) static const char field_##N[] = #N;

FIELD(current_heating_setpoint);
FIELD(local_temperature_calibration);
FIELD(system_mode);
FIELD(sleep_time);
FIELD(resolution);
FIELD(unpair);
FIELD(shunt_milliohms);
FIELD(motor_dc_milliohms);
FIELD(motor_reversed);

const char* Trv::writeable[] = {
    field_current_heating_setpoint,
    field_local_temperature_calibration,
    field_system_mode,
    field_sleep_time,
    field_resolution,
    field_unpair,
    field_shunt_milliohms,
//    field_motor_dc_milliohms,
    field_motor_reversed,
    NULL
};

void Trv::processNetMessage(const char *json) {
  cJSON *root = cJSON_Parse(json);
  if (!root) {
    ESP_LOGW(TAG, "JSON parse failed: %s", json);
    return;
  }
  ESP_LOGI(TAG, "JSON message: %s", json);

  cJSON *current_heating_setpoint = cJSON_GetObjectItem(root, field_current_heating_setpoint);
  cJSON *local_temperature_calibration = cJSON_GetObjectItem(root, field_local_temperature_calibration);
  cJSON *system_mode = cJSON_GetObjectItem(root, field_system_mode);
  cJSON *sleep_time = cJSON_GetObjectItem(root, field_sleep_time);
  cJSON *resolution = cJSON_GetObjectItem(root, field_resolution);
  cJSON *unpair = cJSON_GetObjectItem(root, field_unpair);
  cJSON *shunt_milliohms = cJSON_GetObjectItem(root, field_shunt_milliohms);
//  cJSON *motor_dc_milliohms = cJSON_GetObjectItem(root, field_motor_dc_milliohms);
  cJSON *motor_reversed = cJSON_GetObjectItem(root, field_motor_reversed);

  auto unpairRequest = cJSON_IsTrue(unpair);

  if (cJSON_IsString(system_mode) && (system_mode->valuestring != NULL)) {
    for (esp_zb_zcl_thermostat_system_mode_t mode = ESP_ZB_ZCL_THERMOSTAT_SYSTEM_MODE_OFF;
         mode <= ESP_ZB_ZCL_THERMOSTAT_SYSTEM_MODE_SLEEP;
         mode = (esp_zb_zcl_thermostat_system_mode_t)(mode + 1)) {
      if (!strcasecmp(systemModes[mode], system_mode->valuestring)) {
        ESP_LOGI(TAG, "system_mode %s (%d)", system_mode->valuestring, mode);
        setSystemMode(mode);
      }
    }
  }

  if (cJSON_IsNumber(current_heating_setpoint)) {
    setHeatingSetpoint((float)current_heating_setpoint->valuedouble);
  }

  if (cJSON_IsNumber(local_temperature_calibration)) {
    setTempCalibration((float)local_temperature_calibration->valuedouble);
  }

  if (cJSON_IsNumber(sleep_time)) {
    setSleepTime(sleep_time->valueint);
  }

  if (cJSON_IsNumber(resolution)) {
    int res = -1;
    if (resolution->valuedouble >= 0.5) res = 0;
    else if (resolution->valuedouble >= 0.25) res = 1;
    else if (resolution->valuedouble >= 0.125) res = 2;
    else res = 3;
    if (res >= 0 && res <= 3)
      setTempResolution(res);
  }

  auto shunt_value = cJSON_IsNumber(shunt_milliohms) ? shunt_milliohms->valueint : 0;
  //auto motor_value = cJSON_IsNumber(motor_dc_milliohms) ? motor_dc_milliohms->valueint : 0;
  auto reversed_value = cJSON_IsBool(motor_reversed) ? cJSON_IsTrue(motor_reversed) : cJSON_IsFalse(motor_reversed) ? 0 : -1;
  if (shunt_value || reversed_value != -1) {
    setMotorParameters(shunt_value, reversed_value);
  }

  cJSON *ota = cJSON_GetObjectItem(root, "ota");
  if (cJSON_IsObject(ota)) {
    cJSON *url = cJSON_GetObjectItem(ota, "url");
    cJSON *ssid = cJSON_GetObjectItem(ota, "ssid");
    cJSON *pwd = cJSON_GetObjectItem(ota, "pwd");
    if (cJSON_IsString(url) && (url->valuestring != NULL)
      && cJSON_IsString(ssid) && (ssid->valuestring != NULL)
      && cJSON_IsString(pwd) && (pwd->valuestring != NULL)) {
        ESP_LOGI(TAG, "OTA URL: %s, Wifi %s", url->valuestring, ssid->valuestring);
        doUpdate(url->valuestring, ssid->valuestring, pwd->valuestring) ;
    }
  }
  // Free the root object
  cJSON_Delete(root);

  if (unpairRequest) {
    doUnpair();
  }
}
