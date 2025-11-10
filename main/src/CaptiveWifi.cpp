#include "trv.h"
#include "CaptiveWifi.h"

#include "esp_sleep.h"
#include "../common/gpio/gpio.hpp"

#include <sstream>

#define PORTAL_TTL  60000
#define MULTILINE_STRING(...) #__VA_ARGS__

CaptivePortal::CaptivePortal(Trv* trv, const char *name) : trv(trv) {
  ESP_LOGI(TAG, "CaptivePortal::CaptivePortal");
  timeout = millis() + PORTAL_TTL;
  if (name == NULL || name[0] == 0)
    name = FREEHOUSE_MODEL;
  char ssid[33];
  snprintf(ssid, sizeof(ssid),"FreeHouse-%s",name);
  start_captive_portal(this, ssid);
  bool i = false;
  while (millis() < timeout) {
    delay(250);
    GPIO::digitalWrite(LED_BUILTIN, i);
    i = !i;
  }
  // Close the portal
  GPIO::digitalWrite(LED_BUILTIN, true);
  esp_restart();
};

static const char *netModes [] = {"esp-now", "wifi-mqtt", "ZigBee"};
const char *systemModes[] = {
    "off",
    "auto",
    "",
    "",
    "heat",
    "",
    "",
    "",
    "",
    "sleep"
};

const char accessPointName[] = "FreeHouse-TRV";

bool startsWith(const char *search, const char *match) {
    return strncmp(search, match, strlen(match))==0;
}

static uint8_t hexValue(const char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return 0;
}

static void unencode(char *buf, const char *src, int size) {
  while (*src && size) {
    if (*src == '%') {
      auto msn = hexValue(src[1]);
      auto lsn = hexValue(src[2]);
      src += 3;
      *buf++ = (char)(msn * 16 + lsn);
    } else {
      *buf++ = *src++;
      size--;
    }
  }
  *buf = 0;
}

esp_err_t CaptivePortal::getHandler(httpd_req_t* req) {
  ESP_LOGI(TAG, "Serve %s", req->uri);
  timeout = millis() + PORTAL_TTL;

  auto url = req->uri;

  bool isEspNow = startsWith(url, "/net-esp/");

  // Check the URLs to manage the TRV
  if (startsWith(url, "/mode-heat")) {
    trv->setSystemMode(ESP_ZB_ZCL_THERMOSTAT_SYSTEM_MODE_HEAT);
  } else if (startsWith(url, "/mode-auto")) {
    trv->setSystemMode(ESP_ZB_ZCL_THERMOSTAT_SYSTEM_MODE_AUTO);
  } else if (startsWith(url, "/mode-off")) {
    trv->setSystemMode(ESP_ZB_ZCL_THERMOSTAT_SYSTEM_MODE_OFF);
  } else if (startsWith(url, "/mode-sleep")) {
    trv->setSystemMode(ESP_ZB_ZCL_THERMOSTAT_SYSTEM_MODE_SLEEP);
  } else if (startsWith(url, "/temp-")) {
    trv->setHeatingSetpoint(atof(url + 6));
  } else if (startsWith(url, "/sleep_time-")) {
    trv->setSleepTime(atoi(url + 12));
  } else if (startsWith(url, "/close")) {
    timeout = 0;
  } else if (startsWith(url, "/power-off")) {
    esp_deep_sleep(60 * 60 * 1000000ULL);
  } else if (startsWith(url, "/set-passphrase/")) {
      char passphrase[64];
      unencode(passphrase, req->uri + 16, sizeof(passphrase));
      auto passKey = trv->getState(false).config.passKey;
      if (strlen(passphrase) && get_key_for_passphrase(passphrase,(uint8_t *)passKey) == 0) {
        trv->saveState();
        return ESP_OK;
      }
  } else if (isEspNow || startsWith(url, "/net-mqtt/")) {
    std::string mqConf = url + 9;
    std::string device = mqConf.substr(0,mqConf.find('-'));
    mqConf = mqConf.substr(device.length()+1);
    std::string ssid = mqConf.substr(0,mqConf.find('-'));
    mqConf = mqConf.substr(ssid.length()+1);
    std::string pwd = mqConf.substr(0,mqConf.find('-'));
    mqConf = mqConf.substr(pwd.length()+1);
    std::string mqtt = mqConf.substr(0, mqConf.find(' ')); // TODO: Malformed headers might make the index -1

    trv_mqtt_t mqttConfig;
    unencode(mqttConfig.device_name, device.c_str(), sizeof mqttConfig.device_name);
    unencode((char *)mqttConfig.wifi_ssid, ssid.c_str(), sizeof mqttConfig.wifi_ssid);
    unencode((char *)mqttConfig.wifi_pwd, pwd.c_str(), sizeof mqttConfig.wifi_pwd);
    unencode(mqttConfig.mqtt_server, mqtt.c_str(), sizeof mqttConfig.mqtt_server);
    mqttConfig.mqtt_port = 1883;
    std::string decoded_mqtt_host = std::string(mqttConfig.mqtt_server);
    auto colon = decoded_mqtt_host.find(':');
    if (colon > 0 && colon < (sizeof mqttConfig.mqtt_server)-1) {
      mqttConfig.mqtt_port = atoi(decoded_mqtt_host.substr(colon+1).c_str());
      mqttConfig.mqtt_server[colon] = 0;
    }

    trv->setNetMode(isEspNow ? NET_MODE_ESP_NOW : NET_MODE_MQTT, &mqttConfig);
    delay(200);
    esp_restart();
  } else if (startsWith(url, "/net-zigbee")) {
    trv->setNetMode(NET_MODE_ZIGBEE);
    delay(200);
    esp_restart();
  }

  // Send back the current status
  httpd_resp_set_type(req, "text/html");

  static const char checked[] = "checked";
  const auto state = trv->getState(false);
  std::stringstream html;

  html << "<!DOCTYPE html>\n"
    "<html>\n"
    "<head>\n"
    "<meta charset=\"UTF-8\">\n"
    "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
    "<title>FreeHouse-TRV</title>\n"
    "<style>* { font-family: sans-serif; } button { display: block; margin: 0.5em; }</style>\n"
    "<script>"
    MULTILINE_STRING(
      function ota_upload(elt) {
        elt.disabled = true;
        const input = document.getElementById('firmware');
        if (!input.files.length || !(input.files[0] instanceof Blob)) {
          alert('Please select a file.');
          return;
        }

        const file = input.files[0];
        const xhr = new XMLHttpRequest();

        // Optional: Show progress in the console; replace with UI update as needed
        xhr.upload.onprogress = function(event) {
          if (event.lengthComputable) {
            const percentComplete = (event.loaded / event.total) * 100;
            elt.textContent = (`${percentComplete.toFixed(2)}% complete`);
          } else {
            elt.textContent = (`Uploaded ${event.loaded} bytes`);
          }
        };

        xhr.onload = function() {
          if (xhr.status === 200) {
            alert('Upload successful!');
          } else {
            alert('Upload failed.');
          }
          elt.disabled = false;
        };

        xhr.onerror = function() {
          alert('Upload error\n\n' + xhr.statusText);
          elt.disabled = false;
        };

        xhr.open('POST', '/ota', true);
        xhr.send(file);
      }
      )
    "</script>"
    "</head>\n"
    "<body>\n"
    "<h1>FreeHouse-TRV</h1>\n"
    "<input onclick='window.location.href = \"/mode-heat\"' type='radio' name='m' " << (state.config.system_mode == ESP_ZB_ZCL_THERMOSTAT_SYSTEM_MODE_HEAT ? checked : "") << "/>Heat\n"
    "<input onclick='window.location.href = \"/mode-auto\"' type='radio' name='m' " << (state.config.system_mode == ESP_ZB_ZCL_THERMOSTAT_SYSTEM_MODE_AUTO ? checked : "") << "/>Auto\n"
    "<input onclick='window.location.href = \"/mode-off\"' type='radio' name='m' " << (state.config.system_mode == ESP_ZB_ZCL_THERMOSTAT_SYSTEM_MODE_OFF ? checked : "") << "/>Off\n"
    "<input onclick='window.location.href = \"/mode-sleep\"' type='radio' name='m' " << (state.config.system_mode == ESP_ZB_ZCL_THERMOSTAT_SYSTEM_MODE_SLEEP ? checked : "") << "/>Sleep\n"
    "<table>\n"
    "<tr><td>syetem mode</td><td>" << systemModes[state.config.system_mode] << '(' << state.config.system_mode << ")</td></tr>\n"
    "<tr><td>local_temperature</td><td>" << state.sensors.local_temperature << " °C</td></tr>\n"
    "<tr><td>sensor_temperature</td><td>" << state.sensors.sensor_temperature << " °C</td></tr>\n"
    "<tr><td>battery (raw)</td><td>" << state.sensors.battery_raw << "mV</td></tr>\n"
    "<tr><td>battery %</td><td>" << (int)state.sensors.battery_percent << "%</td></tr>\n"
    "<tr><td>power source</td><td>" << (state.sensors.is_charging ? "charging" : "battery power") << "</td></tr>\n"
    "<tr><td>valve</td><td>" << (int)state.sensors.position << "</td></tr>\n"
    "<tr><td>heating setpoint</td><td>\n"
    "<input type='range' min='10' max='30' value='" << state.config.current_heating_setpoint << "' step='0.25' oninput='this.nextElementSibling.textContent = this.value' onchange='window.location.href = \"/temp-\"+this.value' /><span>" << state.config.current_heating_setpoint << "°C</span></td></tr>\n"
    "<tr><td>calibration</td><td>" << state.config.local_temperature_calibration << "°C</td></tr>\n"
    "<tr><td>resolution</td><td>" << (0.5 / (float)((1 << state.config.resolution))) << "°C</td></tr>\n"
    "<tr><td>Message comms mode</td><td>" << netModes[state.config.netMode] << "</td></tr>\n"
    "<tr><td>Sleep time (secs)</td>"
      "<td><input style='width:4em;' id='sleep_time' maxLength=4 value='" << state.config.sleep_time << "'>"
        "<button onclick='window.location.href=\"/sleep_time-\"+Number(document.getElementById(\"sleep_time\")?.value || 20)'>Set</button>"
      "</td>"
    "</tr>\n"
    "<tr><td>MQTT/ESP-NOW device name</td><td><input id='device' value=\"" << state.config.mqttConfig.device_name << "\"></td></tr>\n"
    "<tr style='display:none;'><td>WiFi SSID</td><td><input id='ssid' value=\"" << state.config.mqttConfig.wifi_ssid << "\"></td></tr>\n"
    "<tr style='display:none;'><td>WiFi password</td><td><input id='pwd' value=\"" << state.config.mqttConfig.wifi_pwd << "\"></td></tr>\n"
    "<tr style='display:none;'><td>MQTT server</td><td><input id='mqtt' value=\"" << state.config.mqttConfig.mqtt_server << ':' << state.config.mqttConfig.mqtt_port << "\"></td></tr>\n"
    "</table>\n"
    "<button onclick='window.location.href = \"/net-esp/\"+encodeURIComponent(\"device,ssid,pwd,mqtt\".split(\",\").map(id => document.getElementById(id).value).join(\"-\"))'>Enable ESP-NOW</button>\n"
    // "<button onclick='window.location.href = \"/net-mqtt/\"+encodeURIComponent(\"device,ssid,pwd,mqtt\".split(\",\").map(id => document.getElementById(id).value).join(\"-\"))'>Enable MQTT</button>\n"
    // "<button onclick='window.location.href = \"/net-zigbee\"'>Disable MQTT</button>\n"

    "<button onclick=\"const n = prompt('Enter the new FreeHouse network passphrase'); if (n) fetch('/set-passphrase/'+encodeURIComponent(n));\">Set FreeHouse network name</button>"

    "<button onclick='window.location.href = \"/close\"'>Close</button>\n"
    // "<button onclick='window.location.href = \"/power-off\"'>Power Off</button>\n"

    "<h2>OTA Update</h2>"
    "<div>"
    "  <input type='file' id='firmware'>"
    "  <button onclick='ota_upload(this)'>Update</button>"
    "</div>"
    "<div>Current: " << versionDetail << "</div>"
    "</body></html>";

  httpd_resp_send(req, html.str().c_str(), HTTPD_RESP_USE_STRLEN);

  return ESP_OK;
}
