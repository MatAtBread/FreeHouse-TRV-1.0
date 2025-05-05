#ifndef TRV_STATE_H
#define TRV_STATE_H

#include <string>

#include "BatteryMonitor.h"
#include "DallasOneWire/DallasOneWire.h"
#include "MotorController.h"
#include "fs.h"

//#include "../managed_components/espressif__esp-zigbee-lib/include/zcl/esp_zigbee_zcl_thermostat.h"
typedef enum {
  ESP_ZB_ZCL_THERMOSTAT_SYSTEM_MODE_OFF = 0x00,
  ESP_ZB_ZCL_THERMOSTAT_SYSTEM_MODE_AUTO = 0x01,
  ESP_ZB_ZCL_THERMOSTAT_SYSTEM_MODE_HEAT = 0x04,
  ESP_ZB_ZCL_THERMOSTAT_SYSTEM_MODE_SLEEP = 0x09,
} esp_zb_zcl_thermostat_system_mode_t;

/* Common API to a TRV. The APIs can be actioned by Zigbee, the Cpative Portal, or internally by a sensor update */

typedef struct trv_mqtt_s {
  uint8_t wifi_ssid[32];
  uint8_t wifi_pwd[64];
  char device_name[32]; // Use for DNS & MQTT topic
  char mqtt_server[32];
  uint16_t mqtt_port;
} trv_mqtt_t;

typedef enum {
  NET_MODE_ESP_NOW,
  NET_MODE_MQTT,
  NET_MODE_ZIGBEE
} net_mode_t;

typedef struct trv_state_s
{
  uint32_t version;
  struct {
    // Read only
    float local_temperature;
    float sensor_temperature;
    uint32_t battery_raw;
    uint8_t battery_percent;
    uint8_t is_charging;
    uint8_t position;
  } sensors;
  struct {
    // Write (read is possible, but never modified by the system)
    float current_heating_setpoint;
    float local_temperature_calibration;
    esp_zb_zcl_thermostat_system_mode_t system_mode; // ESP_ZB_ZCL_THERMOSTAT_SYSTEM_MODE_OFF/AUTO/HEAT/SLEEP
    net_mode_t netMode; // NET_MODE_ESP_NOW expects mqttConfig to contain WIFI settings, NET_MODE_MQTT expects WIFI & NET_MODE_MQTT settings, NET_MODE_ZIGBEE expects no config
    trv_mqtt_t mqttConfig;
  } config;
} trv_state_t;

class Trv
{
protected:
  DallasOneWire *tempSensor;
  MotorController *motor;
  BatteryMonitor *battery;
  TrvFS *fs;

public:
  Trv();
  virtual ~Trv();
  void saveState();
  void resetValve();
  const trv_state_t &getState(bool fast);
  void setHeatingSetpoint(float temp);
  void setSystemMode(esp_zb_zcl_thermostat_system_mode_t mode);
  void setTempCalibration(float temp);
  void checkAutoState();
  bool flatBattery();
  bool is_charging();
  void setNetMode(net_mode_t mode, trv_mqtt_t *mqtt = NULL);

  static const char* deviceName();
  static std::string asJson(const trv_state_t& state);
};

#endif
