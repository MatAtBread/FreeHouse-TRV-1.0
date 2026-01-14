#ifndef TRV_STATE_H
#define TRV_STATE_H

#include <string>

#include "trv.h"

#include "BatteryMonitor.h"
#include "DallasOneWire/DallasOneWire.h"
#include "MotorController.h"
#include "fs.h"
#include "../common/encryption/encryption.h"


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
    ENCRYPTION_KEY passKey;
    int sleep_time; // in seconds, 1 to 120
    uint8_t resolution; // 0=9-bit, 1=10-bit, 2=11-bit, 3=12-bit
    int _reserved;
    motor_params_t motor;
  } config;
} trv_state_t;

class Trv
{
protected:
  DallasOneWire *tempSensor;
  MotorController *motor;
  BatteryMonitor *battery;
  TrvFS *fs;
  bool configDirty;

  std::string otaUrl;
  std::string otaSsid;
  std::string otaPwd;
  void requestUpdate(const char *otaUrl, const char *otaSsid, const char *otaPwd);
  void doUnpair();
  void doUpdate();
  void checkAutoState();

public:
  Trv();
  virtual ~Trv();
  void saveState();
  void resetValve();
  const trv_state_t &getState(bool fast);
  void setHeatingSetpoint(float temp);
  void setSystemMode(esp_zb_zcl_thermostat_system_mode_t mode);
  void setTempCalibration(float temp);
  void setTempResolution(uint8_t res);
  bool flatBattery();
  bool is_charging();
  void setNetMode(net_mode_t mode, trv_mqtt_t *mqtt = NULL);
  void setPassKey(const uint8_t *key);
  void setSleepTime(int seconds);
  void setMotorParameters(const motor_params_t &params);
  void calibrate();
  void processNetMessage(const char *json);

  static const char* deviceName();
  static uint32_t stateVersion();
  static std::string asJson(const trv_state_t& state, signed int rssi = 0);
  static const char* writeable[];
};

#endif
