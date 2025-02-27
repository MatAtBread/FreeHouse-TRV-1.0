#include "trv-state.h"

#include <HardwareSerial.h>

#define MOTOR D7 // (and D6 on the Dev board)
#define NSLEEP D8
#define CHARGING D9
#define DTEMP D10  // NYI: One wire bus for DS18B20L / MY18B20L
#define BATTERY A0

static RTC_DATA_ATTR trv_state_t globalState;

Trv::Trv(Heartbeat* heartbeat, bool wakeUp) {
  tempSensor = new DallasOneWire(DTEMP);
  battery = new BatteryMonitor(BATTERY, CHARGING);
  motor = new MotorController(MOTOR /* and D6 on dev board*/, NSLEEP, heartbeat, battery, globalState.valve_position);

  // Maybe implement this another time.
  globalState.temperatureCalibration = 0.0;
  // Initialize output values
  getState();
  // Initialize user values
  if (!wakeUp) {
    // TODO: Restore global valeus from storage
    // For now, just make up some values
    if (globalState.heatingSetpoint < 10 || globalState.heatingSetpoint > 30)
      globalState.heatingSetpoint = 21.5;
    if (globalState.systemMode < ESP_ZB_ZCL_THERMOSTAT_SYSTEM_MODE_OFF || globalState.systemMode > ESP_ZB_ZCL_THERMOSTAT_SYSTEM_MODE_HEAT)
      globalState.systemMode = ESP_ZB_ZCL_THERMOSTAT_SYSTEM_MODE_AUTO;

    Serial.println("Reset: open valve");
    // When restarting after a reset or power loss (eg new battery), force open the valve
    globalState.valve_position = 50; // We don't know what the valve position is after a hard reset
    motor->setValvePosition(100);
    Serial.println("Reset: wait until valve opened");
    motor->delayUntilIdle();
    // Once the valve is open, we can set the target position depending on the state
    Serial.println("Reset: valve opened, set system mode");
    setSysteMode(globalState.systemMode);
  }
}

Trv::~Trv(){}

trv_state_t& Trv::getState(){
  globalState.temperature = tempSensor->readTemp() + globalState.temperatureCalibration;
  globalState.isCharging = battery->isCharging();
  globalState.valve_position = motor->getValvePosition();
  // We only update battery values when the motor is off. If it's moving, it will drop due to the loading
  if (motor->getDirection() == 0) {
    globalState.batteryRaw = battery->getValue();
    globalState.batteryPercent = battery->getPercent(globalState.batteryRaw);
  }

  return globalState;
}

bool Trv::flatBattery() {
  return battery->getPercent() <= 1;
}

void Trv::setTempCalibration(float temp) {
  globalState.temperatureCalibration = temp;
  checkAutoState();
}

void Trv::setHeatingSetpoint(float temp){
  globalState.heatingSetpoint = temp;
  checkAutoState();
}

void Trv::setSysteMode(esp_zb_zcl_thermostat_system_mode_t mode){
  globalState.systemMode = mode;
  if (mode == ESP_ZB_ZCL_THERMOSTAT_SYSTEM_MODE_OFF) {
    motor->setValvePosition(0);
  } else if (mode == ESP_ZB_ZCL_THERMOSTAT_SYSTEM_MODE_HEAT) {
    motor->setValvePosition(100);
  } else if (mode == ESP_ZB_ZCL_THERMOSTAT_SYSTEM_MODE_AUTO) {
    checkAutoState();
  }
}

void Trv::checkAutoState() {
  auto state = getState();
  Serial.printf(F("checkAutoState: %f %f %d\n"), state.temperature, state.heatingSetpoint, state.systemMode);
  if (state.systemMode == ESP_ZB_ZCL_THERMOSTAT_SYSTEM_MODE_AUTO) {
    if (state.temperature > state.heatingSetpoint + 0.5) {
      motor->setValvePosition(0);
    } else if (state.temperature < state.heatingSetpoint) {
      motor->setValvePosition(100);
    }
  }
}
