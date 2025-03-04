#include "../trv.h"

#include "ZigbeeTRV.h"
#include "zcl/esp_zigbee_zcl_thermostat.h"
#include <ha/esp_zigbee_ha_standard.h>

#if SOC_IEEE802154_SUPPORTED && CONFIG_ZB_ENABLED

// "valve_position" manufacturer-specifc attribute
#define MANU_THERMOSTATE_VALUE_POSITION_ID 0xFCCFU // Choose an ID in the manufacturer-specific range (0xFC00-0xFFFF)

static float zb_s16_to_temperature(int16_t value) {
  return 1.0 * value / 100;
}

#define SYSTEM_MODE_ENUM_NAME(name) case esp_zb_zcl_thermostat_system_mode_t::ESP_ZB_ZCL_THERMOSTAT_SYSTEM_MODE_##name: return #name
const char* systemModeToString(esp_zb_zcl_thermostat_system_mode_t mode) {
  switch (mode) {
    SYSTEM_MODE_ENUM_NAME(OFF);
    SYSTEM_MODE_ENUM_NAME(AUTO);
    SYSTEM_MODE_ENUM_NAME(COOL);
    SYSTEM_MODE_ENUM_NAME(HEAT);
    SYSTEM_MODE_ENUM_NAME(EMERGENCY_HEATING);
    SYSTEM_MODE_ENUM_NAME(PRECOOLING);
    SYSTEM_MODE_ENUM_NAME(FAN_ONLY);
    SYSTEM_MODE_ENUM_NAME(DRY);
    SYSTEM_MODE_ENUM_NAME(SLEEP);
    default: return "?";
  }
}

// Initialize the static instance of the class
ZigbeeTRV *ZigbeeTRV::_instance = nullptr;

ZigbeeTRV::ZigbeeTRV(uint8_t endpoint) : ZigbeeEP(endpoint) {
  _device_id = ESP_ZB_HA_THERMOSTAT_DEVICE_ID;
  _instance = this;  // Set the static pointer to this instance

  //use custom config to avoid narrowing error -> must be fixed in zigbee-sdk
  esp_zb_thermostat_cfg_t thermostat_cfg = ESP_ZB_DEFAULT_THERMOSTAT_CONFIG();
  thermostat_cfg.thermostat_cfg.occupied_cooling_setpoint = 0;  // We don't do cooling
  thermostat_cfg.thermostat_cfg.control_sequence_of_operation = 2; // Heating only

  //use custom cluster creating to accept reportings from temperature sensor
  _cluster_list = esp_zb_zcl_cluster_list_create();
  esp_zb_attribute_list_t *basic_cluster = esp_zb_basic_cluster_create(&(thermostat_cfg.basic_cfg));
  esp_zb_cluster_list_add_basic_cluster(_cluster_list, basic_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
  esp_zb_cluster_list_add_identify_cluster(_cluster_list, esp_zb_identify_cluster_create(&(thermostat_cfg.identify_cfg)), ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
  esp_zb_cluster_list_add_identify_cluster(_cluster_list, esp_zb_zcl_attr_list_create(ESP_ZB_ZCL_CLUSTER_ID_IDENTIFY), ESP_ZB_ZCL_CLUSTER_CLIENT_ROLE);

  auto thermostat_cluster = esp_zb_thermostat_cluster_create(&(thermostat_cfg.thermostat_cfg));
  int8_t local_temperature_calibration = 0;
  esp_zb_thermostat_cluster_add_attr(thermostat_cluster, ESP_ZB_ZCL_ATTR_THERMOSTAT_LOCAL_TEMPERATURE_CALIBRATION_ID, &local_temperature_calibration);

  // "valve_position" manufacturer-specifc attribute
  uint8_t MANU_THERMOSTATE_VALUE_POSITION_DEFAULT_VALUE = 50U;
  esp_zb_custom_cluster_add_custom_attr(thermostat_cluster, MANU_THERMOSTATE_VALUE_POSITION_ID, ESP_ZB_ZCL_ATTR_TYPE_U8, ESP_ZB_ZCL_ATTR_ACCESS_READ_WRITE, &MANU_THERMOSTATE_VALUE_POSITION_DEFAULT_VALUE);

  esp_zb_cluster_list_add_thermostat_cluster(_cluster_list, thermostat_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

  /* Add temperature measurement cluster for attribute reporting */
  esp_zb_cluster_list_add_temperature_meas_cluster(_cluster_list, esp_zb_temperature_meas_cluster_create(NULL), ESP_ZB_ZCL_CLUSTER_CLIENT_ROLE);

  _ep_config = {.endpoint = _endpoint, .app_profile_id = ESP_ZB_AF_HA_PROFILE_ID, .app_device_id = ESP_ZB_HA_THERMOSTAT_DEVICE_ID, .app_device_version = 0};
}

void ZigbeeTRV::bindCb(esp_zb_zdp_status_t zdo_status, void *user_ctx) {
  if (zdo_status == ESP_ZB_ZDP_STATUS_SUCCESS) {
    if (user_ctx) {
      zb_device_params_t *sensor = (zb_device_params_t *)user_ctx;
      log_i("The temperature sensor originating from address(0x%x) on endpoint(%d)", sensor->short_addr, sensor->endpoint);
      _instance->_bound_devices.push_back(sensor);
    } else {
      log_v("Local binding success");
    }
    _is_bound = true;
  } else {
    log_e("Binding failed!");
  }
}

void ZigbeeTRV::findCb(esp_zb_zdp_status_t zdo_status, uint16_t addr, uint8_t endpoint, void *user_ctx) {
  if (zdo_status == ESP_ZB_ZDP_STATUS_SUCCESS) {
    log_i("Found temperature sensor");
    esp_zb_zdo_bind_req_param_t bind_req;
    /* Store the information of the remote device */
    zb_device_params_t *sensor = (zb_device_params_t *)malloc(sizeof(zb_device_params_t));
    sensor->endpoint = endpoint;
    sensor->short_addr = addr;
    esp_zb_ieee_address_by_short(sensor->short_addr, sensor->ieee_addr);
    log_d("Temperature sensor found: short address(0x%x), endpoint(%d)", sensor->short_addr, sensor->endpoint);

    /* 1. Send binding request to the sensor */
    bind_req.req_dst_addr = addr;
    log_d("Request temperature sensor to bind us");

    /* populate the src information of the binding */
    memcpy(bind_req.src_address, sensor->ieee_addr, sizeof(esp_zb_ieee_addr_t));
    bind_req.src_endp = endpoint;
    bind_req.cluster_id = ESP_ZB_ZCL_CLUSTER_ID_TEMP_MEASUREMENT;
    log_d("Bind temperature sensor");

    /* populate the dst information of the binding */
    bind_req.dst_addr_mode = ESP_ZB_ZDO_BIND_DST_ADDR_MODE_64_BIT_EXTENDED;
    esp_zb_get_long_address(bind_req.dst_address_u.addr_long);
    bind_req.dst_endp = *((uint8_t *)user_ctx);  //_endpoint;

    log_i("Request temperature sensor to bind us");
    esp_zb_zdo_device_bind_req(&bind_req, bindCb, NULL);

    /* 2. Send binding request to self */
    bind_req.req_dst_addr = esp_zb_get_short_address();

    /* populate the src information of the binding */
    esp_zb_get_long_address(bind_req.src_address);
    bind_req.src_endp = *((uint8_t *)user_ctx);  //_endpoint;
    bind_req.cluster_id = ESP_ZB_ZCL_CLUSTER_ID_TEMP_MEASUREMENT;

    /* populate the dst information of the binding */
    bind_req.dst_addr_mode = ESP_ZB_ZDO_BIND_DST_ADDR_MODE_64_BIT_EXTENDED;
    memcpy(bind_req.dst_address_u.addr_long, sensor->ieee_addr, sizeof(esp_zb_ieee_addr_t));
    bind_req.dst_endp = endpoint;

    log_i("Bind temperature sensor");
    esp_zb_zdo_device_bind_req(&bind_req, bindCb, (void *)sensor);
  }
}

void ZigbeeTRV::findEndpoint(esp_zb_zdo_match_desc_req_param_t *param) {
  uint16_t cluster_list[] = {ESP_ZB_ZCL_CLUSTER_ID_TEMP_MEASUREMENT};
  param->profile_id = ESP_ZB_AF_HA_PROFILE_ID;
  param->num_in_clusters = 1;
  param->num_out_clusters = 0;
  param->cluster_list = cluster_list;
  esp_zb_zdo_match_cluster(param, findCb, &_endpoint);
}

void ZigbeeTRV::zbAttributeRead(uint16_t cluster_id, const esp_zb_zcl_attribute_t *attribute) {
  _log("Attribute read: cluster %04X, attr %04X, value %d", cluster_id, attribute->id, *(uint8_t *)attribute->data.value);
  static uint8_t read_config = 0;
  if (cluster_id == ESP_ZB_ZCL_CLUSTER_ID_TEMP_MEASUREMENT) {
    if (attribute->id == ESP_ZB_ZCL_ATTR_TEMP_MEASUREMENT_VALUE_ID && attribute->data.type == ESP_ZB_ZCL_ATTR_TYPE_S16) {
      int16_t value = attribute->data.value ? *(int16_t *)attribute->data.value : 0;
      if (_on_temp_recieve) {
        _on_temp_recieve(zb_s16_to_temperature(value));
      }
    }
    if (attribute->id == ESP_ZB_ZCL_ATTR_TEMP_MEASUREMENT_MIN_VALUE_ID && attribute->data.type == ESP_ZB_ZCL_ATTR_TYPE_S16) {
      int16_t min_value = attribute->data.value ? *(int16_t *)attribute->data.value : 0;
      _min_temp = zb_s16_to_temperature(min_value);
      read_config++;
    }
    if (attribute->id == ESP_ZB_ZCL_ATTR_TEMP_MEASUREMENT_MAX_VALUE_ID && attribute->data.type == ESP_ZB_ZCL_ATTR_TYPE_S16) {
      int16_t max_value = attribute->data.value ? *(int16_t *)attribute->data.value : 0;
      _max_temp = zb_s16_to_temperature(max_value);
      read_config++;
    }
    if (attribute->id == ESP_ZB_ZCL_ATTR_TEMP_MEASUREMENT_TOLERANCE_ID && attribute->data.type == ESP_ZB_ZCL_ATTR_TYPE_U16) {
      uint16_t tolerance = attribute->data.value ? *(uint16_t *)attribute->data.value : 0;
      _tolerance = 1.0 * tolerance / 100;
      read_config++;
    }
    if (read_config == 3) {
      read_config = 0;
      xSemaphoreGive(lock);
    }
  }
}

void ZigbeeTRV::zbAttributeSet(const esp_zb_zcl_set_attr_value_message_t *message) {
  // Parse out the data
  if (message->info.cluster == ESP_ZB_ZCL_CLUSTER_ID_THERMOSTAT) {
    switch (message->attribute.id) {
      case ESP_ZB_ZCL_ATTR_THERMOSTAT_OCCUPIED_COOLING_SETPOINT_ID:
        // We don't do cooling
        return;
      case ESP_ZB_ZCL_ATTR_THERMOSTAT_OCCUPIED_HEATING_SETPOINT_ID:
        if (message->attribute.data.type == ESP_ZB_ZCL_ATTR_TYPE_S16) {
          int16_t value = message->attribute.data.value ? *(int16_t *)message->attribute.data.value : 0;
          _log("Set heating setpoint: %f", zb_s16_to_temperature(value));
          return;
        }
        break;
      case ESP_ZB_ZCL_ATTR_THERMOSTAT_SYSTEM_MODE_ID:
        if (message->attribute.data.type == ESP_ZB_ZCL_ATTR_TYPE_8BIT_ENUM) {
          _log("Set system mode: %s", systemModeToString((esp_zb_zcl_thermostat_system_mode_t)*(uint8_t *)message->attribute.data.value));
          return;
        }
        break;
      case ESP_ZB_ZCL_ATTR_THERMOSTAT_LOCAL_TEMPERATURE_CALIBRATION_ID:
        if (message->attribute.data.type == ESP_ZB_ZCL_ATTR_TYPE_S8) {
          int8_t value = message->attribute.data.value ? *(int8_t *)message->attribute.data.value : 0;
          _log("Set temp calibration: %f", value/10.0);
          return;
        }
        break;
      case MANU_THERMOSTATE_VALUE_POSITION_ID:
        if (message->attribute.data.type == ESP_ZB_ZCL_ATTR_TYPE_U8) {
          uint8_t value = message->attribute.data.value ? *(uint8_t *)message->attribute.data.value : 0;
          _log("Set valve position: %d", value);
          return;
        }
        break;
    }
    _log("Unknown attribute set: cluster %04X, attr %04X, value %p", message->info.cluster, message->attribute.id, message->attribute.data.value);
    return;
  }
}

void ZigbeeTRV::getTemperature() {
  /* Send "read attributes" command to the bound sensor */
  esp_zb_zcl_read_attr_cmd_t read_req;
  read_req.address_mode = ESP_ZB_APS_ADDR_MODE_DST_ADDR_ENDP_NOT_PRESENT;
  read_req.zcl_basic_cmd.src_endpoint = _endpoint;
  read_req.clusterID = ESP_ZB_ZCL_CLUSTER_ID_TEMP_MEASUREMENT;

  uint16_t attributes[] = {ESP_ZB_ZCL_ATTR_TEMP_MEASUREMENT_VALUE_ID};
  read_req.attr_number = ZB_ARRAY_LENTH(attributes);
  read_req.attr_field = attributes;

  log_i("Sending 'read temperature' command");
  esp_zb_lock_acquire(portMAX_DELAY);
  esp_zb_zcl_read_attr_cmd_req(&read_req);
  esp_zb_lock_release();
}

void ZigbeeTRV::getSensorSettings() {
  /* Send "read attributes" command to the bound sensor */
  esp_zb_zcl_read_attr_cmd_t read_req;
  read_req.address_mode = ESP_ZB_APS_ADDR_MODE_DST_ADDR_ENDP_NOT_PRESENT;
  read_req.zcl_basic_cmd.src_endpoint = _endpoint;
  read_req.clusterID = ESP_ZB_ZCL_CLUSTER_ID_TEMP_MEASUREMENT;

  uint16_t attributes[] = {
    ESP_ZB_ZCL_ATTR_TEMP_MEASUREMENT_MIN_VALUE_ID, ESP_ZB_ZCL_ATTR_TEMP_MEASUREMENT_MAX_VALUE_ID, ESP_ZB_ZCL_ATTR_TEMP_MEASUREMENT_TOLERANCE_ID
  };
  read_req.attr_number = ZB_ARRAY_LENTH(attributes);
  read_req.attr_field = attributes;

  log_i("Sending 'read temperature' command");
  esp_zb_lock_acquire(portMAX_DELAY);
  esp_zb_zcl_read_attr_cmd_req(&read_req);
  esp_zb_lock_release();

  //Take semaphore to wait for response of all attributes
  if (xSemaphoreTake(lock, ZB_CMD_TIMEOUT) != pdTRUE) {
    log_e("Error while reading attributes");
    return;
  } else {
    //Call the callback function when all attributes are read
    _on_config_recieve(_min_temp, _max_temp, _tolerance);
  }
}

void ZigbeeTRV::setTemperatureReporting(uint16_t min_interval, uint16_t max_interval, float delta) {
  /* Send "configure report attribute" command to the bound sensor */
  esp_zb_zcl_config_report_cmd_t report_cmd;
  report_cmd.address_mode = ESP_ZB_APS_ADDR_MODE_DST_ADDR_ENDP_NOT_PRESENT;
  report_cmd.zcl_basic_cmd.src_endpoint = _endpoint;
  report_cmd.clusterID = ESP_ZB_ZCL_CLUSTER_ID_TEMP_MEASUREMENT;

  int16_t report_change = (int16_t)delta * 100;
  esp_zb_zcl_config_report_record_t records[] = {
    {
      .direction = ESP_ZB_ZCL_REPORT_DIRECTION_SEND,
      .attributeID = ESP_ZB_ZCL_ATTR_TEMP_MEASUREMENT_VALUE_ID,
      .attrType = ESP_ZB_ZCL_ATTR_TYPE_S16,
      .min_interval = min_interval,
      .max_interval = max_interval,
      .reportable_change = &report_change,
    },
  };
  report_cmd.record_number = ZB_ARRAY_LENTH(records);
  report_cmd.record_field = records;

  log_i("Sending 'configure reporting' command");
  esp_zb_lock_acquire(portMAX_DELAY);
  esp_zb_zcl_config_report_cmd_req(&report_cmd);
  esp_zb_lock_release();
}

#endif  //SOC_IEEE802154_SUPPORTED && CONFIG_ZB_ENABLED
