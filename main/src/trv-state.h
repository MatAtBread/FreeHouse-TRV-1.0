#ifndef TRV_STATE_H
#define TRV_STATE_H

#include "BatteryMonitor.h"
#include "DallasOneWire/DallasOneWire.h"
#include "MotorController.h"
#include "zcl/esp_zigbee_zcl_thermostat.h"

/* Common API to a TRV. The APIs can be actioned by Zigbee, the Cpative Portal, or internally by a sensor update */

typedef struct trv_mqtt_s {
  char wifi_ssid[32];
  char hostname[32]; // Use for DNS & MQTT topic
  char mqtt_server[64];
} trv_mqtt_t;

typedef struct trv_state_s
{
  // Read only
  float temperature;
  uint32_t batteryRaw;
  uint8_t batteryPercent;
  uint8_t isCharging;
  uint8_t valve_position;
  // Write (read is possible, but never modified by the system)
  float heatingSetpoint;
  float temperatureCalibration;
  esp_zb_zcl_thermostat_system_mode_t systemMode; // ESP_ZB_ZCL_THERMOSTAT_SYSTEM_MODE_OFF/AUTO/HEAT/SLEEP
  bool useMqtt; // If false, uses zigbee. If true, uses WiFi & MQTT
  trv_mqtt_t mqttConfig;
} trv_state_t;

class Trv
{
protected:
  DallasOneWire *tempSensor;
  MotorController *motor;
  BatteryMonitor *battery;

public:
  Trv(Heartbeat* heartbeat);
  virtual ~Trv();
  void resetValve();
  trv_state_t &getState();
  static trv_state_t &getLastState();
  void setHeatingSetpoint(float temp);
  void setSysteMode(esp_zb_zcl_thermostat_system_mode_t mode);
  void setTempCalibration(float temp);
  void checkAutoState();
  bool flatBattery();
};

#endif
