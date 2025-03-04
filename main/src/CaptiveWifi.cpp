#include "../trv.h"

#include "help.h"
#include "CaptiveWifi.h"
#include "zcl/esp_zigbee_zcl_thermostat.h"
#include <WiFi.h>

const char* systemModeToString(esp_zb_zcl_thermostat_system_mode_t mode);

const char accessPointName[] = "FreeHouse-TRV";

void sendHtml(WiFiClient &cl, trv_state_t state) {
  static const char checked[] = "checked";
  cl.println(R"rawText(
<!DOCTYPE html>
<html>
<head>
 <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>FreeHouse-TRV</title>
  <style>button { display: block; margin: 0.5em; }</style>
</head>
<body>
  <h1>FreeHouse-TRV</h1>
)rawText");
cl.printf("<input onclick='window.location.href = \"/mode-on\"' type='radio' name='m' %s/>On", state.systemMode == ESP_ZB_ZCL_THERMOSTAT_SYSTEM_MODE_HEAT ? checked : "");
cl.printf("<input onclick='window.location.href = \"/mode-auto\"' type='radio' name='m' %s/>Auto", state.systemMode == ESP_ZB_ZCL_THERMOSTAT_SYSTEM_MODE_AUTO ? checked : "");
cl.printf("<input onclick='window.location.href = \"/mode-off\"' type='radio' name='m' %s/>Off", state.systemMode == ESP_ZB_ZCL_THERMOSTAT_SYSTEM_MODE_OFF ? checked : "");
cl.println("<table>");
cl.printf("<tr><td>syetm mode</td><td>%s</td></tr>", systemModeToString(state.systemMode));
cl.printf("<tr><td>temperature</td><td>%f</td></tr>", state.temperature);
cl.printf("<tr><td>battery (raw)</td><td>%lumV</td></tr>", state.batteryRaw);
cl.printf("<tr><td>battery %%</td><td>%d%%</td></tr>", state.batteryPercent);
cl.printf("<tr><td>power source</td><td>%s</td></tr>", state.isCharging ? "charging" : "battery power");
cl.printf("<tr><td>valve</td><td>%d</td></tr>", state.valve_position);
cl.printf("<tr><td>heating setpoint</td><td>");
cl.printf("<input type='range' min='10' max='30' value='%f' step='0.25' onchange='window.location.href = \"/temp-\"+this.value' />%f</td></tr>",
  state.heatingSetpoint, state.heatingSetpoint);
cl.printf("<tr><td>calibration</td><td>%f</td></tr>", state.temperatureCalibration);
cl.println("</table>"
  "<button onclick='window.location.href = \"/close\"'>Close</button>"
  "<button onclick='window.location.href = \"/power-off\"'>Power Off</button>"
  "</body></html>"
);
}

const uint32_t ttlSleep = 60000;

CaptivePortal::CaptivePortal(Heartbeat* heartbeat, Trv* trv) : heartbeat(heartbeat), server(80), trv(trv) {
  heartbeat->ping(ttlSleep);
  WiFi.mode(WIFI_AP);
  WiFi.softAP(accessPointName);

  while (WiFi.getMode() != WIFI_MODE_AP) {
    _log(".");
    delay(50);
  }
  ddelay(100);

  IPAddress apIP = WiFi.softAPIP();  //(192,168,168,168);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));

  dnsServer.start(53, "*", WiFi.softAPIP());
  dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
  dnsServer.setTTL(300);
  dnsServer.start(53, "*", apIP);

  server.begin();
  StartTask(CaptivePortal);

  _log("Started Captive Portal %s", accessPointName);
}

void CaptivePortal::task() {
  String header;
  // Current time
  unsigned long currentTime = millis();
  // Previous time
  unsigned long previousTime = 0;
  // Define timeout time in milliseconds (example: 2000ms = 2s)
  const long timeoutTime = 2000;
  bool close = false;
  while (1) {
    dnsServer.processNextRequest();

    WiFiClient client = server.accept();  // Listen for incoming clients
    if (client) {                            // If a new client connects,
      currentTime = millis();
      previousTime = currentTime;
      String currentLine = "";                                                   // make a String to hold incoming data from the client
      while (client.connected() && currentTime - previousTime <= timeoutTime) {  // loop while the client's connected
        currentTime = millis();
        if (client.available()) {  // if there's bytes to read from the client,
          char c = client.read();  // read a byte, then
          header += c;
          if (c == '\n') {  // if the byte is a newline character
            // if the current line is blank, you got two newline characters in a row.
            // that's the end of the client HTTP request, so send a response:
            if (currentLine.length() == 0) {
              // HTTP headers always start with a response code (e.g. HTTP/1.1 200 OK)
              // and a content-type so the client knows what's coming, then a blank line:
              client.println("HTTP/1.1 200 OK");
              client.println("Content-type:text/html");
              client.println("Connection: close");
              client.println();

              // Display the HTML web page
              if (header.startsWith("GET /mode-on")) {
                trv->setSysteMode(ESP_ZB_ZCL_THERMOSTAT_SYSTEM_MODE_HEAT);
              } else if (header.startsWith("GET /mode-auto")) {
                trv->setSysteMode(ESP_ZB_ZCL_THERMOSTAT_SYSTEM_MODE_AUTO);
              } else if (header.startsWith("GET /mode-off")) {
                trv->setSysteMode(ESP_ZB_ZCL_THERMOSTAT_SYSTEM_MODE_OFF);
              } else if (header.startsWith("GET /temp-")) {
                trv->setHeatingSetpoint(header.substring(10).toFloat());
              } else if (header.startsWith("GET /close")) {
                close = true;
              } else if (header.startsWith("GET /power-off")) {
                heartbeat->cardiacArrest(86400000U);
              }
              auto state = trv->getState();
              sendHtml(client, state);
              // The HTTP response ends with another blank line
              client.println();
              // Break out of the while loop
              break;
            } else {  // if you got a newline, then clear currentLine
              currentLine = "";
            }
          } else if (c != '\r') {  // if you got anything else but a carriage return character,
            currentLine += c;      // add it to the end of the currentLine
          }
        }
      }
      // Clear the header variable
      header = "";
      // Close the connection
      client.stop();
      if (close)
        heartbeat->cardiacArrest();
      else
        heartbeat->ping(ttlSleep);
    }
  }
}
