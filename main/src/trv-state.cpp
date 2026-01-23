#include "../trv.h"

#include <string.h>
#include <sstream>

#include "trv-state.h"
#include "mcu_temp.hpp"
#include "pins.h"
#include "helpers.h"
#include <net/esp-now.hpp>

#define STATE_VERSION 8L

#define STALL_MS_DEFAULT 100
#define BACKOFF_MS_DEFAULT 100

extern const char *systemModes[];
static RTC_DATA_ATTR trv_state_t globalState;
const trv_state_t defaultState = {
  .version = STATE_VERSION,
  .sensors = {
    .local_temperature = 20.0,
    .sensor_temperature = 20.0,
    .battery_raw = 3500,
    .battery_percent = 50,
    .is_charging = 0,
    .position = 50,
  },
  .config = {
    .current_heating_setpoint = 21.5,
    .local_temperature_calibration = 0.0,
    .system_mode = ESP_ZB_ZCL_THERMOSTAT_SYSTEM_MODE_SLEEP,
    .netMode = NET_MODE_ESP_NOW,
    .mqttConfig = {
        .wifi_ssid = "wifi-ssid",
        .wifi_pwd = "wifi-pwd",
        .device_name = "",
        .mqtt_server = "mqtt.local",
        .mqtt_port = 1883,
    },
    .passKey = {0},
    .sleep_time = 20,
    .resolution = 1,
    .debug_flags = 0,
    .motor = {
      .reversed = true,
      .backoff_ms = 259,
      .stall_ms = 1
    }
  }
};

uint32_t debugFlag(DebugFlags mask) {
  return globalState.config.debug_flags & (uint32_t)mask;
}

const char *Trv::deviceName() { return globalState.config.mqttConfig.device_name; }
const uint8_t *Trv::getPassKey() { return globalState.config.passKey; }
uint32_t Trv::stateVersion() { return globalState.version; }

#define UPDATE_STATE(number, default_expr) \
  if (state.version == number) { \
    state.version +=1; \
    default_expr; \
    configDirty = true; \
    ESP_LOGI(TAG, "Read state: %u bytes version %lu", r, state.version);\
  }

static McuTempSensor* mcuTempSensor = NULL;

// On construction, the state is guaranteed to be valid, and asynchronously the sensors, etc are initialized
Trv::Trv() {
  mustCalibrate = false;
  configDirty = false;
  fs = new TrvFS();

  // On timer wake (ESP-NOW check), trust the RTC state if version matches
  if (esp_reset_reason() == ESP_RST_DEEPSLEEP && globalState.version == STATE_VERSION) {
      // RTC state is valid, skip FS read
  } else {
    trv_state_t state;
    // Try loading the config from the filesystem
    __SIZE_TYPE__ r = fs->read("/trv/state", &state, sizeof(state));
    ESP_LOGI(TAG, "Read state: %u bytes version %lu", r, state.version);
    if (r && (r <= sizeof(state) || state.version < STATE_VERSION)) {
        UPDATE_STATE(7, state.config.debug_flags = 0; state.config.motor.backoff_ms = BACKOFF_MS_DEFAULT; state.config.motor.stall_ms = STALL_MS_DEFAULT; )
        r = sizeof(state);
    }

    if (r != sizeof (state) || state.version != STATE_VERSION) {
        ESP_LOGI(TAG, "Default state, version %lu != %lu", state.version, STATE_VERSION);
        globalState = defaultState;
    } else {
        globalState = state;
        globalState.sensors = defaultState.sensors;
    }
    mustCalibrate = true;
  }
  // We crerate the battery monitor here, as it's fast and not task based, which makes testing for a flat battery quick
  battery = new BatteryMonitor();
  StartTask(Trv);
}

void Trv::task() {
  // Get the sensor values
  tempSensor = new DallasOneWire(globalState.sensors.sensor_temperature, globalState.config.resolution);
  motor = new MotorController(battery, globalState.sensors.position, globalState.config.motor);
  if (mustCalibrate) {
      motor->calibrate();
  }
  if (!mcuTempSensor) mcuTempSensor = new McuTempSensor(); // Lazily get MCU temp

  globalState.sensors.is_charging = battery->is_charging();
  globalState.sensors.position = motor->getValvePosition();
  // We only update battery values when the motor is off. If it's moving, it will drop due to the loading
  if (motor->getDirection() == 0) {
    globalState.sensors.battery_raw = battery->getValue();
    globalState.sensors.battery_percent = battery->getPercent(globalState.sensors.battery_raw);
  }

  // We choose to keep a running average the temp to minimize the heating effect of operating
  // We don't read the temperature if we are charging, as the MCU is hot
  float compensation = 0.0;
  if (globalState.sensors.is_charging) {
    compensation = 1.0; // TODO: calibrate this value from the MCU temperature
  }
  auto local = tempSensor->readTemp() + globalState.config.local_temperature_calibration + compensation;
  globalState.sensors.local_temperature = (local + globalState.sensors.local_temperature) / 2;
}

bool Trv::requiresNetworkControl() {
  return otaUrl.length() > 0;
}

Trv::~Trv() {
  wait();
  if (configDirty) saveState(); // Update NVS if necessary
  if (this->otaUrl.length()) {
    doUpdate();
  }
  if (mcuTempSensor) delete mcuTempSensor;
  delete motor;
  delete tempSensor;
  delete battery;
  delete fs;
}

void Trv::setSleepTime(int seconds) {
  if (seconds < 0 || seconds > 300) {
    ESP_LOGW(TAG, "Invalid sleep time %d, must be between 0 and 300", seconds);
    return;
  }
  if (globalState.config.sleep_time != seconds) {
    globalState.config.sleep_time = seconds;
    configDirty = true;
  }
  ESP_LOGI(TAG, "Set sleep time to %d seconds", seconds);
}

void Trv::setDebugFlags(uint32_t flags) {
  if (globalState.config.debug_flags != flags) {
    globalState.config.debug_flags = flags;
    configDirty = true;
  }
  ESP_LOGI(TAG, "Set debug flags to 0x%04X. dirty=%d", flags, configDirty);
}

std::string Trv::asJson(const trv_state_t& s, signed int rssi) {
  const auto mcuTemp = mcuTempSensor->read();
  std::stringstream json;
  json << "{";
  if (rssi) json << "\"rssi\":" << rssi << ",";
  json << "\"mcu_temperature\":" << mcuTemp << ","
    "\"local_temperature\":" << s.sensors.local_temperature << ","
    "\"sensor_temperature\":" << s.sensors.sensor_temperature << ","
    "\"battery_percent\":" << (int)s.sensors.battery_percent << ","
    "\"battery_mv\":" << (int)s.sensors.battery_raw << ","
    "\"is_charging\":" << (s.sensors.is_charging ? "true" : "false") << ","
    "\"position\":" << (int)s.sensors.position << ","
    "\"motor\":\"" << MotorController::lastStatus << "\","
    "\"current_heating_setpoint\":" << s.config.current_heating_setpoint << ","
    "\"local_temperature_calibration\":" << s.config.local_temperature_calibration << ","
    "\"system_mode\":\"" << systemModes[s.config.system_mode] << "\","
    "\"sleep_time\":" << s.config.sleep_time << ","
    "\"resolution\":" << (0.5 / (float)(1 << s.config.resolution)) << ","
    "\"backoff_ms\":" << s.config.motor.backoff_ms << ","
    "\"stall_ms\":" << s.config.motor.stall_ms << ","
    "\"motor_reversed\":" << (s.config.motor.reversed ? "true":"false") << ","
    "\"debug_flags\":" << s.config.debug_flags << ","
    "\"unpair\":false,"
    "\"calibrate\":false"
    "}";

  return json.str();
}

void Trv::doUnpair() {
  EspNet::unpair();
}

void Trv::saveState() {
  globalState.sensors.position = motor->getValvePosition(); // Should be benign as MotorController is passed a reference to this value
  auto saved = fs->write("/trv/state", &globalState, sizeof(globalState));
  configDirty = !saved;
  ESP_LOGI(TAG, "saveState: %d", saved);
}

void Trv::setMotorParameters(const motor_params_t& params) {
  if (params.reversed == true || params.reversed == false) {
    if (globalState.config.motor.reversed != params.reversed) {
      wait();
      globalState.config.motor.reversed = params.reversed;
      configDirty = true;
      const auto pos = motor->getValvePosition();

      globalState.sensors.position = 50; // Invalidate position
      motor->setValvePosition(pos ? 100 : 0);  // Re-apply current position to change direction if necessary
    }
  }
  if (params.stall_ms > 0) {
    if (globalState.config.motor.stall_ms != params.stall_ms) {
        globalState.config.motor.stall_ms = params.stall_ms;
        configDirty = true;
    }
  }
  if (params.backoff_ms >= 0) {
    if (globalState.config.motor.backoff_ms != params.backoff_ms) {
        globalState.config.motor.backoff_ms = params.backoff_ms;
        configDirty = true;
    }
  }
}

const trv_config_t &Trv::getConfig() {
  return globalState.config;
}

const trv_state_t& Trv::getState() {
  wait();
  return globalState;
}

void Trv::setNetMode(net_mode_t mode, trv_mqtt_t *mqtt){
  bool changed = false;
  if (globalState.config.netMode != mode) {
      globalState.config.netMode = mode;
      changed = true;
  }
  if (mqtt) {
    if (memcmp(&globalState.config.mqttConfig, mqtt, sizeof(trv_mqtt_t)) != 0) {
        memcpy(&globalState.config.mqttConfig, mqtt, sizeof(trv_mqtt_t));
        changed = true;
        ESP_LOGI(TAG, "Enable mqtt %s, %s, %s, %s:%d", globalState.config.mqttConfig.device_name, globalState.config.mqttConfig.wifi_ssid, globalState.config.mqttConfig.wifi_pwd, globalState.config.mqttConfig.mqtt_server, globalState.config.mqttConfig.mqtt_port);
    }
  }
  if (changed) {
      configDirty = true;
      // We should probably do a restart to make sure there are no clashes with other operations
  }
}

void Trv::setPassKey(const uint8_t *key) {
  if (memcmp(globalState.config.passKey, key, sizeof(globalState.config.passKey)) != 0) {
      memcpy(globalState.config.passKey, key, sizeof(globalState.config.passKey));
      configDirty = true;
  }
}

bool Trv::flatBattery() {
  // No need to wait, battery created fast in constructor
  return battery->getPercent() <= 1;
}

bool Trv::is_charging() {
  // No need to wait, battery created fast in constructor
  return battery->is_charging();
}

void Trv::setTempResolution(uint8_t res) {
  res &= 0x03;
  if (globalState.config.resolution == res)
    return;

  globalState.config.resolution = res;
  configDirty = true;
  wait();
  tempSensor->setResolution(globalState.config.resolution);
}

void Trv::setTempCalibration(float temp) {
  if (globalState.config.local_temperature_calibration == temp) return;
  globalState.config.local_temperature_calibration = temp;
  configDirty = true;
  checkAutoState();
}

void Trv::setHeatingSetpoint(float temp) {
  if (globalState.config.current_heating_setpoint == temp) return;
  globalState.config.current_heating_setpoint = temp;
  configDirty = true;
  checkAutoState();
}

void Trv::setSystemMode(esp_zb_zcl_thermostat_system_mode_t mode) {
  if (globalState.config.system_mode != mode) {
      configDirty = true;
  }
  globalState.config.system_mode = mode;
  wait();
  if (mode == ESP_ZB_ZCL_THERMOSTAT_SYSTEM_MODE_OFF) {
    motor->setValvePosition(0);
  } else if (mode == ESP_ZB_ZCL_THERMOSTAT_SYSTEM_MODE_HEAT) {
    motor->setValvePosition(100);
  } else if (mode == ESP_ZB_ZCL_THERMOSTAT_SYSTEM_MODE_AUTO) {
    checkAutoState();
  } else if (mode == ESP_ZB_ZCL_THERMOSTAT_SYSTEM_MODE_SLEEP) {
    // In sleep mode we just don't move the plunger at all
    motor->setValvePosition(-1); // Just stops the motor where it is
  }
}

void Trv::checkAutoState() {
  auto state = getState(); // wait already called
  if (state.config.system_mode == ESP_ZB_ZCL_THERMOSTAT_SYSTEM_MODE_AUTO) {
    if (state.sensors.local_temperature > state.config.current_heating_setpoint) {
      motor->setValvePosition(0);
    } else if (state.sensors.local_temperature < state.config.current_heating_setpoint - 0.5) {
      motor->setValvePosition(100);
    }
  }
}

void Trv::calibrate() {
  wait();
  const auto mode = globalState.config.system_mode;
  globalState.config.system_mode = ESP_ZB_ZCL_THERMOSTAT_SYSTEM_MODE_SLEEP;
  motor->calibrate();
  globalState.sensors.position = motor->getValvePosition();
  setSystemMode(mode);
  ESP_LOGI(TAG,"Calibrated state: '%s' mqtt://%s:%d, wifi %s >> %s",
    globalState.config.mqttConfig.device_name,
    globalState.config.mqttConfig.mqtt_server,
    globalState.config.mqttConfig.mqtt_port,
    globalState.config.mqttConfig.wifi_ssid,
    asJson(globalState).c_str());
}

void Trv::testMode(TouchButton &touchButton) {
  ESP_LOGI(TAG, "Enter test mode");
  int count = 0;
  GPIO::digitalWrite(LED_BUILTIN, false);
  wait();
  while (true) {
    ESP_LOGI(TAG, "Test cycle %d", count++);
    const int target = count & 1 ? 100 : 0;
    motor->setValvePosition(target);
    motor->wait();
    if (motor->getValvePosition() != target) {
      ESP_LOGW(TAG, "Failed to reach target %d, got %d. Sleeping.",
               target, motor->getValvePosition());
    }
    delay(5000);
    touchButton.reset();
    if (touchButton.pressed()) {
      ESP_LOGI(TAG, "Exit test mode on touch");
      return;
    }
  }
}