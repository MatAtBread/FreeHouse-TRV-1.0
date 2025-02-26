#ifndef TRV_STATE_H
#define TRV_STATE_H

#include "BatteryMonitor.h"
#include "DallasOneWire/DallasOneWire.h"
#include "MotorController.h"
#include "zcl\esp_zigbee_zcl_thermostat.h"

/* Common API to a TRV. The APIs can be actioned by Zigbee, the Cpative Portal, or internally by a sensor update */

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
  esp_zb_zcl_thermostat_system_mode_t systemMode; // ESP_ZB_ZCL_THERMOSTAT_SYSTEM_MODE_OFF/AUTO/HEAT (SLEEP?)
} trv_state_t;

class Trv
{
protected:
  DallasOneWire *tempSensor;
  MotorController *motor;
  BatteryMonitor *battery;

public:
  Trv(Heartbeat* heartbeat, bool wakeUp);
  virtual ~Trv();
  trv_state_t &getState();
  void setHeatingSetpoint(float temp);
  void setSysteMode(esp_zb_zcl_thermostat_system_mode_t mode);
  void setTempCalibration(float temp);
  void checkAutoState();
  bool flatBattery();
};

#endif
