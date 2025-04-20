#include "esp_log.h"
#include "string.h"
#include "trv-state.h"
#include "trv.h"

#include "cJSON.h"
//#include "json_parser.h"

extern const char *systemModes[];

void processNetMessage(const char *json, Trv *trv) {
  cJSON *root = cJSON_Parse(json);
  if (!root) {
    ESP_LOGW(TAG, "JSON parse failed: %s\n", json);
    return;
  }

  cJSON *system_mode = cJSON_GetObjectItem(root, "system_mode");
  cJSON *current_heating_setpoint = cJSON_GetObjectItem(root, "current_heating_setpoint");
  cJSON *local_temperature_calibration = cJSON_GetObjectItem(root, "local_temperature_calibration");

  if (cJSON_IsString(system_mode) && (system_mode->valuestring != NULL)) {
    for (esp_zb_zcl_thermostat_system_mode_t mode = ESP_ZB_ZCL_THERMOSTAT_SYSTEM_MODE_OFF;
         mode <= ESP_ZB_ZCL_THERMOSTAT_SYSTEM_MODE_SLEEP;
         mode = (esp_zb_zcl_thermostat_system_mode_t)(mode + 1)) {
      if (!strcmp(systemModes[mode], system_mode->valuestring)) {
        ESP_LOGI(TAG, "system_mode %s (%d)\n", system_mode->valuestring, mode);
        trv->setSystemMode(mode);
      }
    }
  }

  if (cJSON_IsNumber(current_heating_setpoint)) {
    ESP_LOGI(TAG, "current_heating_setpoint %f\n", current_heating_setpoint->valuedouble);
    trv->setHeatingSetpoint((float)current_heating_setpoint->valuedouble);
  }

  if (cJSON_IsNumber(local_temperature_calibration)) {
    ESP_LOGI(TAG, "local_temperature_calibration %f\n", local_temperature_calibration->valuedouble);
    trv->setTempCalibration((float)local_temperature_calibration->valuedouble);
  }

  // Free the root object
  cJSON_Delete(root);
}

/*
void processNetMessage(const char *json, Trv* trv) {
  jparse_ctx_t jctx;
  int ret = json_parse_start(&jctx, json, strlen(json));
  if (ret != OS_SUCCESS) {
    ESP_LOGW(TAG, "JSON parse failed: %s\n", json);
    return;
  }

  char str_val[64];
  float float_val;

  if (json_obj_get_string(&jctx, "system_mode", str_val, sizeof(str_val)) == OS_SUCCESS) {
    for (esp_zb_zcl_thermostat_system_mode_t mode = ESP_ZB_ZCL_THERMOSTAT_SYSTEM_MODE_OFF;
      mode <= ESP_ZB_ZCL_THERMOSTAT_SYSTEM_MODE_SLEEP;
      mode = (esp_zb_zcl_thermostat_system_mode_t)(mode+1)) {
      if (!strcmp(systemModes[mode], str_val)) {
        ESP_LOGI(TAG, "system_mode %s (%d)\n", str_val, mode);
        trv->setSystemMode(mode);
      }
    }
  }

  if (json_obj_get_float(&jctx, "current_heating_setpoint", &float_val) == OS_SUCCESS) {
    ESP_LOGI(TAG, "current_heating_setpoint %f\n", float_val);
    trv->setHeatingSetpoint(float_val);
  }

  if (json_obj_get_float(&jctx, "local_temperature_calibration", &float_val) == OS_SUCCESS) {
    ESP_LOGI(TAG, "local_temperature_calibration %f\n", float_val);
    trv->setTempCalibration(float_val);
  }
}
*/