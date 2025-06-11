#include "../trv.h"

#include <string.h>
#include <sstream>

#include "trv-state.h"
#include "mcu_temp.h"

#define MOTOR 17     // D7
#define NSLEEP 19    // D8
#define CHARGING 20  // D9
#define DTEMP 18     // D10
#define BATTERY 0    // A0

#define STATE_VERSION 4L

extern const char *systemModes[];

static RTC_DATA_ATTR trv_state_t globalState = {
  .version = STATE_VERSION,
  .sensors = {
    .local_temperature = 20.0,
    .sensor_temperature = 20.0,
    .battery_raw = 3500,
    .battery_percent = 50,
    .is_charging = 0,
    .position = 100,
  },
  .config = {
    .current_heating_setpoint = 21.5,
    .local_temperature_calibration = 0.0,
    .system_mode = ESP_ZB_ZCL_THERMOSTAT_SYSTEM_MODE_HEAT,
    .netMode = NET_MODE_ESP_NOW,
    .mqttConfig = {
        .wifi_ssid = "wifi-ssid",
        .wifi_pwd = "wifi-pwd",
        .device_name = "",
        .mqtt_server = "mqtt.local",
        .mqtt_port = 1883,
    },
    .passKey = {0},
    .sleep_time = 20
  }
};

const char *Trv::deviceName() { return globalState.config.mqttConfig.device_name; }

Trv::Trv() {
  fs = new TrvFS();

  // Try loading the config from the filesystem
  trv_state_t state;
  __SIZE_TYPE__ r = fs->read("/trv/state", &state, sizeof(state));
  ESP_LOGI(TAG, "Read state: %u bytes version %lu", r, state.version);
  if (r && r <= sizeof(state) && state.version < STATE_VERSION) {
    // Change a state v2 into a state v3
    if (state.version == 2 && r == sizeof(state) - (sizeof(state.config.passKey) + sizeof(state.config.sleep_time))) {
      state.version = 3;
      memset(state.config.passKey, 0, sizeof (state.config.passKey));
      r += sizeof(state.config.passKey);
    }
    ESP_LOGI(TAG, "Read state: %u bytes version %lu", r, state.version);
    // Change a state v3 into a state v4
    if (state.version == 3 && r == sizeof(state) - sizeof(state.config.sleep_time)) {
      state.version = STATE_VERSION;
      state.config.sleep_time = 20; // Default sleep time
      r += sizeof(state.config.sleep_time);
    }
    ESP_LOGI(TAG, "Read state: %u bytes version %lu", r, state.version);
  }

  if (r != sizeof (state) || state.version != STATE_VERSION) {
    ESP_LOGI(TAG, "Default state, version %lu != %lu", state.version, STATE_VERSION);
    // By default we stay in heat mode so that when assembling the hardware, the plunger stays in place
    globalState.config.system_mode = ESP_ZB_ZCL_THERMOSTAT_SYSTEM_MODE_HEAT;
    globalState.config.netMode = NET_MODE_ESP_NOW;
    globalState.config.mqttConfig.mqtt_port = 1883;
    globalState.config.local_temperature_calibration = 0.0;
    globalState.config.current_heating_setpoint = 21;
    globalState.config.sleep_time = 20;
  } else {
    globalState.config = state.config;
  }

  // Get the sensor values
  tempSensor = new DallasOneWire(DTEMP, globalState.sensors.sensor_temperature);
  battery = new BatteryMonitor(BATTERY, CHARGING);
  motor = new MotorController(MOTOR, NSLEEP, battery, globalState.sensors.position);
  getState(true); // Lazily update temp
  ESP_LOGI(TAG,"Initial state: '%s' mqtt://%s:%d, wifi %s >> %s",
    globalState.config.mqttConfig.device_name,
    globalState.config.mqttConfig.mqtt_server,
    globalState.config.mqttConfig.mqtt_port,
    globalState.config.mqttConfig.wifi_ssid,
    asJson(globalState).c_str());
}

Trv::~Trv() {
  delete battery;
  delete tempSensor;
  delete motor;
}

void Trv::setSleepTime(int seconds) {
  if (seconds < 0 || seconds > 300) {
    ESP_LOGW(TAG, "Invalid sleep time %d, must be between 0 and 300", seconds);
    return;
  }
  globalState.config.sleep_time = seconds;
  saveState();
  ESP_LOGI(TAG, "Set sleep time to %d seconds", seconds);
}

std::string Trv::asJson(const trv_state_t& s, signed int rssi) {
  mcu_temp_init();
  auto mcu_temp = mcu_temp_read();
  mcu_temp_deinit();
  ESP_LOGI(TAG, "MCU temp: %f", mcu_temp);

  std::stringstream json;
  json << "{";
  if (rssi) json << "\"rssi\":" << rssi << ",";
  json << "\"mcu_temperature\":" << mcu_temp << ","
    "\"local_temperature\":" << s.sensors.local_temperature << ","
    "\"sensor_temperature\":" << s.sensors.sensor_temperature << ","
    "\"battery_percent\":" << (int)s.sensors.battery_percent << ","
    "\"battery_mv\":" << (int)s.sensors.battery_raw << ","
    "\"is_charging\":" << (s.sensors.is_charging ? "true" : "false") << ","
    "\"position\":" << (int)s.sensors.position << ","
    "\"current_heating_setpoint\":" << s.config.current_heating_setpoint << ","
    "\"local_temperature_calibration\":" << s.config.local_temperature_calibration << ","
    "\"system_mode\":\"" << systemModes[s.config.system_mode] << "\","
    "\"sleep_time\":" << s.config.sleep_time <<
    "}";

  return json.str();
}

void Trv::saveState() {
  auto saved = fs->write("/trv/state", &globalState, sizeof(globalState));
  ESP_LOGI(TAG, "saveState: %d", saved);
}

void Trv::resetValve() {
  ESP_LOGI(TAG, "Reset: open valve");
  // When restarting after a reset or power loss (eg new battery), force open the valve
  globalState.sensors.position = 50;  // We don't know what the valve position is after a hard reset
  motor->setValvePosition(100);
  ESP_LOGI(TAG, "Reset: wait until valve opened");
  motor->wait();

  // Once the valve is open, we can set the target position depending on the state
  ESP_LOGI(TAG, "Reset: valve opened, set system mode");
  setSystemMode(globalState.config.system_mode);
}

const trv_state_t& Trv::getState(bool fast) {
  globalState.sensors.is_charging = battery->is_charging();
  // We don't read the temperature if we are charging, as the MCU is hot
  if (!fast) {
    float compensation = 0.0;
    if (globalState.sensors.is_charging) {
      compensation = 1.0; // TODO: calibrate this value from the MCU temperature
    }

    // We choose to keep a running average the temp to minimize the heating effect of operating
    auto local = tempSensor->readTemp() + globalState.config.local_temperature_calibration + compensation;
    globalState.sensors.local_temperature = (local + globalState.sensors.local_temperature) / 2;
  }

  globalState.sensors.position = motor->getValvePosition();
  // We only update battery values when the motor is off. If it's moving, it will drop due to the loading
  if (motor->getDirection() == 0) {
    globalState.sensors.battery_raw = battery->getValue();
    globalState.sensors.battery_percent = battery->getPercent(globalState.sensors.battery_raw);
  }

  return globalState;
}

void Trv::setNetMode(net_mode_t mode, trv_mqtt_t *mqtt){
  globalState.config.netMode = mode;
  if (mqtt) {
    memcpy(&globalState.config.mqttConfig, mqtt, sizeof(trv_mqtt_t));
    ESP_LOGI(TAG, "Enable mqtt %s, %s, %s, %s:%d", globalState.config.mqttConfig.device_name, globalState.config.mqttConfig.wifi_ssid, globalState.config.mqttConfig.wifi_pwd, globalState.config.mqttConfig.mqtt_server, globalState.config.mqttConfig.mqtt_port);
  }
  saveState();
  // We should probably do a restart to make sure there are no clashes with other operations
}

bool Trv::flatBattery() {
  return battery->getPercent() <= 1;
}

bool Trv::is_charging() {
  return battery->is_charging();
}

void Trv::setTempCalibration(float temp) {
  globalState.config.local_temperature_calibration = temp;
  saveState();
  checkAutoState();
}

void Trv::setHeatingSetpoint(float temp) {
  globalState.config.current_heating_setpoint = temp;
  saveState();
  checkAutoState();
}

void Trv::setSystemMode(esp_zb_zcl_thermostat_system_mode_t mode) {
  if (mode == ESP_ZB_ZCL_THERMOSTAT_SYSTEM_MODE_OFF) {
    globalState.config.system_mode = mode;
    saveState();
    motor->setValvePosition(0);
  } else if (mode == ESP_ZB_ZCL_THERMOSTAT_SYSTEM_MODE_HEAT) {
    globalState.config.system_mode = mode;
    saveState();
    motor->setValvePosition(100);
  } else if (mode == ESP_ZB_ZCL_THERMOSTAT_SYSTEM_MODE_AUTO) {
    globalState.config.system_mode = mode;
    saveState();
    checkAutoState();
  } else if (mode == ESP_ZB_ZCL_THERMOSTAT_SYSTEM_MODE_SLEEP) {
    globalState.config.system_mode = mode;
    motor->setValvePosition(-1); // Just stops the motor where it is
    saveState();
    // In sleep mode we just don't move the plunger at all
  }
}

void Trv::checkAutoState() {
  auto state = getState(true);
  if (state.config.system_mode == ESP_ZB_ZCL_THERMOSTAT_SYSTEM_MODE_AUTO) {
    if (state.sensors.local_temperature > state.config.current_heating_setpoint) {
      motor->setValvePosition(0);
    } else if (state.sensors.local_temperature < state.config.current_heating_setpoint - 0.5) {
      motor->setValvePosition(100);
    }
  }
}
