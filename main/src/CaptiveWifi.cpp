#include "pins.h"
#include "CaptiveWifi.h"

#include "../common/gpio/gpio.hpp"

#include <sstream>
#include <trv.h>

#define PORTAL_TTL  60000
#define MULTILINE_STRING(...) #__VA_ARGS__

CaptivePortal::CaptivePortal(Trv* trv, const char *name) : trv(trv) {
  ESP_LOGI(TAG, "CaptivePortal::CaptivePortal");
  exitStatus = NONE;
  timeout = millis() + PORTAL_TTL;
  if (name == NULL || name[0] == 0)
    name = FREEHOUSE_MODEL;
  char ssid[33];
  snprintf(ssid, sizeof(ssid),"FreeHouse-%s",name);
  start_captive_portal(this, ssid);
  while (millis() < timeout && exitStatus == NONE) {
    GPIO::digitalWrite(LED_BUILTIN, true);
    delay(125);
    GPIO::digitalWrite(LED_BUILTIN, false);
    delay(125);
  }
  if (exitStatus == NONE) {
    exitStatus = TIME_OUT;
  }
  // Close the portal
  GPIO::digitalWrite(LED_BUILTIN, true);
  delay(250);
  stop_captive_portal();
}

void CaptivePortal::exitPortal(exit_status_t status) {
  exitStatus = status;
}

static const char *root = "/";
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

bool startsWith(const char *search, const char *match) {
  return strncmp(search, match, strlen(match)) == 0;
}

static uint8_t hexValue(const char c) {
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
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

static const char* sepdef(char **a, const char *b, const char *c) {
  const auto p = strsep(a,b);
  return p ? p : c;
}
esp_err_t CaptivePortal::getHandler(httpd_req_t *req) {
  static char buffer[sizeof req->uri];

  ESP_LOGI(TAG, "Serve %s", req->uri);
  if (exitStatus != NONE) {
    std::stringstream html;
    httpd_resp_set_type(req, "text/html");
    html << "<html><body>Closing..." << exitStatus << "</body></html>";
    httpd_resp_send(req, html.str().c_str(), HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
  }

  timeout = millis() + PORTAL_TTL;

  auto url = req->uri;

  // Check the URLs to manage the TRV
  if (startsWith(url, "/process")) {
    const char *json = strchr(url, '?');
    if (json) {
      unencode(buffer, json + 1, sizeof buffer);
      trv->processNetMessage(buffer);
    }
  } else if (startsWith(url, "/close")) {
    exitPortal(CLOSED);
  } else if (startsWith(url, "/test-mode")) {
    exitPortal(TEST_MODE);
  } else if (startsWith(url, "/calibrate")) {
    exitPortal(CALIBRATE);
  } else if (startsWith(url, "/power-off")) {
    exitPortal(POWER_OFF);
  } else if (startsWith(url, "/set-passphrase/")) {
    char passphrase[64];
    unencode(passphrase, req->uri + 16, sizeof(passphrase));
    uint8_t passKey[32]; // ENCRYPTION_KEY is 32 bytes
    if (strlen(passphrase) && get_key_for_passphrase(passphrase, passKey) == 0) {
      trv->setPassKey(passKey);
    }
  // } else if (startsWith(url, "/net-zigbee")) {
  //   trv->setNetMode(NET_MODE_ZIGBEE);
  //   delay(200);
  //   esp_restart();
  } else if (startsWith(url, "/net-")) {
    unencode(buffer, strchr(url + 1, '/') + 1, sizeof buffer);
    ESP_LOGI(TAG, "Set networking %s", buffer);

    char *saveptr = buffer;
    const char *device = sepdef(&saveptr, "\x1D", "");
    ESP_LOGI(TAG, "Set networking device: %s", device);
    const char *ssid = sepdef(&saveptr, "\x1D", "");
    ESP_LOGI(TAG, "Set networking ssid: %s", ssid);
    const char *pwd = sepdef(&saveptr, "\x1D", "");
    ESP_LOGI(TAG, "Set networking pwd: %s", pwd);
    const char *mqtt = sepdef(&saveptr, ":", "");
    ESP_LOGI(TAG, "Set networking mqtt: %s", mqtt);
    const char *port = sepdef(&saveptr, ":", "");
    ESP_LOGI(TAG, "Set networking port: %s", port);

    trv_mqtt_t mqttConfig;
    strncpy((char *)mqttConfig.device_name, device, sizeof mqttConfig.device_name);
    strncpy((char *)mqttConfig.wifi_ssid, ssid, sizeof mqttConfig.wifi_ssid);
    strncpy((char *)mqttConfig.wifi_pwd, pwd, sizeof mqttConfig.wifi_pwd);
    strncpy(mqttConfig.mqtt_server, mqtt, sizeof mqttConfig.mqtt_server);
    mqttConfig.mqtt_port = 1883;
    if (port[0])
      mqttConfig.mqtt_port = atoi(port);
    trv->setNetMode(startsWith(url, "/net-esp") ? NET_MODE_ESP_NOW : NET_MODE_MQTT, &mqttConfig);
    delay(200);
    // Since at the moment we DON'T support anything other than NET_MODE_ESP_NOW, there's no
    // need to restart, which clears the RTC RAM and therefore causes an unnecessary valve calibration
    exitPortal(CLOSED);
    // esp_restart();
  }

  if (strcmp(url, root)) {
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/");
    const char *resp_str = "<html><body>Redirecting</body></html>";
    httpd_resp_send(req, resp_str, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
  } else {
    // Send back the current status
    httpd_resp_set_type(req, "text/html");

    std::stringstream html;
    /*if (calibrating) {
      html << MULTILINE_STRING(
        <!DOCTYPE html>
        <html>
        <head>
        <meta charset="UTF-8">
        <meta name="viewport" content="width=device-width, initial-scale=1">
        <title>FreeHouse-TRV</title>
        <style>* { font-family: sans-serif; } button { display: block; margin: 0.5em; }</style>
        <script>setTimeout(function(){ window.location.reload() },3000)</script>
        </head>
        <body>
        <h1>FreeHouse-TRV</h1>
        <h2>calibrating...</h2>
        </body>
        </html>
      );
    } else*/ {
      static const char checked[] = "checked";
      const auto state = trv->getState();

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

  function processMessage(e,k,v) {
    if (e instanceof HTMLElement) {
      if (k === undefined) k = e.name;
      if (v === undefined) v = Number(e.value);
    }
    window.location.href = "/process?"+JSON.stringify({[k]:v})
  }
  )
        "</script>"
        "</head>\n"
        "<body>\n"
        "<h1>FreeHouse-TRV</h1>\n"
        "<input name='system_mode' onclick='processMessage(this,undefined,\"" << systemModes[ESP_ZB_ZCL_THERMOSTAT_SYSTEM_MODE_HEAT] << "\")' type='radio' " << (state.config.system_mode == ESP_ZB_ZCL_THERMOSTAT_SYSTEM_MODE_HEAT ? checked : "") << "/>" << systemModes[ESP_ZB_ZCL_THERMOSTAT_SYSTEM_MODE_HEAT] << "\n"
        "<input name='system_mode' onclick='processMessage(this,undefined,\"" << systemModes[ESP_ZB_ZCL_THERMOSTAT_SYSTEM_MODE_AUTO] << "\")' type='radio' " << (state.config.system_mode == ESP_ZB_ZCL_THERMOSTAT_SYSTEM_MODE_AUTO ? checked : "") << "/>" << systemModes[ESP_ZB_ZCL_THERMOSTAT_SYSTEM_MODE_AUTO] << "\n"
        "<input name='system_mode' onclick='processMessage(this,undefined,\"" << systemModes[ESP_ZB_ZCL_THERMOSTAT_SYSTEM_MODE_OFF] << "\")' type='radio' " << (state.config.system_mode == ESP_ZB_ZCL_THERMOSTAT_SYSTEM_MODE_OFF ? checked : "") << "/>" << systemModes[ESP_ZB_ZCL_THERMOSTAT_SYSTEM_MODE_OFF] << "\n"
        "<input name='system_mode' onclick='processMessage(this,undefined,\"" << systemModes[ESP_ZB_ZCL_THERMOSTAT_SYSTEM_MODE_SLEEP] << "\")' type='radio' " << (state.config.system_mode == ESP_ZB_ZCL_THERMOSTAT_SYSTEM_MODE_SLEEP ? checked : "") << "/>" << systemModes[ESP_ZB_ZCL_THERMOSTAT_SYSTEM_MODE_SLEEP] << "\n"
        "<table>\n"
          "<tr><td>valve</td><td>" << (int)state.sensors.position << " (" << MotorController::lastStatus << ")</td></tr>\n"
          "<tr><td>syetem mode</td><td>" << systemModes[state.config.system_mode] << '(' << state.config.system_mode << ")</td></tr>\n"
          "<tr><td>local_temperature</td><td>" << state.sensors.local_temperature << " °C</td></tr>\n"
          "<tr><td>sensor_temperature</td><td>" << state.sensors.sensor_temperature << " °C</td></tr>\n"
          "<tr><td>battery (raw)</td><td>" << state.sensors.battery_raw << "mV</td></tr>\n"
          "<tr><td>battery %</td><td>" << (int)state.sensors.battery_percent << "%</td></tr>\n"
          "<tr><td>power source</td><td>" << (state.sensors.is_charging ? "charging" : "battery power") << "</td></tr>\n"
          "<tr><td>heating setpoint</td>"
            "<td><input style='width:6em;' type='number' value='" << (state.config.current_heating_setpoint) << "' name='current_heating_setpoint' onchange='processMessage(this)'>°C</td>"
          "</tr>\n"
          "<tr><td>temp. calibration</td>"
            "<td><input style='width:6em;' type='number' value='" << (state.config.local_temperature_calibration) << "' name='local_temperature_calibration' onchange='processMessage(this)'>°C</td>"
          "</tr>\n"
          "<tr><td>Temp. resoluation</td>"
            "<td><select style='width:6em;' type='number' value='" << (int)(state.config.resolution) << "' name='resolution' onchange='processMessage(this,undefined,Number(this.selectedOptions[0].value))'>"
              "<option value='0.5' " << (state.config.resolution==0 ? "selected":"") << ">0.5°C</option>"
              "<option value='0.25' " << (state.config.resolution==1 ? "selected":"") << ">0.25°C</option>"
              "<option value='0.125' " << (state.config.resolution==2 ? "selected":"") << ">0.125°C</option>"
              "<option value='0.0625' " << (state.config.resolution==3 ? "selected":"") << ">0.0625°C</option>"
            "</select></td>"
          "</tr>\n"
          "<tr><td>Sleep time</td>"
            "<td><input style='width:6em;' type='number' value='" << (state.config.sleep_time) << "' name='sleep_time' onchange='processMessage(this)'>s</td>"
          "</tr>\n"
          "<tr><td>Back-off burst</td>"
            "<td><input style='width:6em;' type='number' value='" << (state.config.motor.backoff_ms) << "' name='backoff_ms' onchange='processMessage(this)'>ms</td>"
          "</tr>\n"
          "<tr><td>Stall time</td>"
            "<td><input style='width:6em;' type='number' value='" << (state.config.motor.stall_ms) << "' name='stall_ms' onchange='processMessage(this)'>ms</td>"
          "</tr>\n"
          "<tr><td>Motor reversed</td>"
              "<td><input type=\"checkbox\" " << (state.config.motor.reversed ? "checked":"") << " name='motor_reversed' onchange='processMessage(this,undefined,this.checked)'></td>"
          "</tr>\n"
          "<tr><td>Debug flags</td>"
            "<td><input style='width:6em;' type='number' value='" << (state.config.debug_flags) << "' name='debug_flags' onchange='processMessage(this)'></td>"
          "</tr>\n"
          "</table>\n"

        "<h2>Networking</h2>"
        "<table>\n"
          "<tr><td>Message comms mode</td><td>" << netModes[state.config.netMode] << "</td></tr>\n"
          "<tr><td>MQTT/ESP-NOW device name</td><td><input id='device' value=\"" << state.config.mqttConfig.device_name << "\"></td></tr>\n"
        // "<tr style='display:none;'><td>WiFi SSID</td><td><input id='ssid' value=\"" << state.config.mqttConfig.wifi_ssid << "\"></td></tr>\n"
        // "<tr style='display:none;'><td>WiFi password</td><td><input id='pwd' value=\"" << state.config.mqttConfig.wifi_pwd << "\"></td></tr>\n"
        // "<tr style='display:none;'><td>MQTT server</td><td><input id='mqtt' value=\"" << state.config.mqttConfig.mqtt_server << ':' << state.config.mqttConfig.mqtt_port << "\"></td></tr>\n"
        "</table>\n"
        "<button onclick='window.location.href = \"/net-esp/\"+encodeURIComponent(document.getElementById(\"device\").value)'>Enable ESP-NOW</button>"
        // "<button onclick='window.location.href = \"/net-mqtt/\"+encodeURIComponent(\"device,ssid,pwd,mqtt\".split(\",\").map(id => document.getElementById(id).value).join(\"-\"))'>Enable MQTT</button>\n"
        // "<button onclick='window.location.href = \"/net-zigbee\"'>Disable MQTT</button>\n"
        "<button onclick=\"const n = prompt('Enter the new FreeHouse network passphrase'); if (n) fetch('/set-passphrase/'+encodeURIComponent(n));\">Set FreeHouse network name</button>"

        "<h2>Actions</h2>"
        "<button onclick='window.location.href = \"/close\"'>Close</button>\n"
        "<button onclick='window.location.href = \"/calibrate\"'>Calibrate valve</button>\n"
        "<button onclick='window.location.href = \"/test-mode\"'>Test mode</button>\n"
        "<button onclick='window.location.href = \"/power-off\"'>Power Off</button>\n"

        "<h2>OTA Update</h2>"
        "<input type='file' id='firmware'>"
        "<button onclick='ota_upload(this)'>Update</button>"
        "<div>Current: " << versionDetail << "</div>"
        "<div>" << debugNetworkInfo() << "</div>"
        "</body></html>";
    }
    httpd_resp_send(req, html.str().c_str(), HTTPD_RESP_USE_STRLEN);

    return ESP_OK;
  }
}
