/** Wrappers to the underlying ESP-IDF peripheral libraries */
#ifndef WIFI_STA_H
#define WIFI_STA_H

// #include "esp_system.h"
// #include "freertos/FreeRTOS.h"
// #include "esp_timer.h"
// #include "../../../common/gpio/gpio.hpp"
#include "sys/_stdint.h"

class WiFiStation {
 protected:
  void wifi_init_sta(void);
  uint8_t ssid[32];
  uint8_t password[64];
  const char *device_name;
  int maxRetries;

  public:
  WiFiStation(const uint8_t *ssid, const uint8_t *password, const char *device_name = NULL, int maxRetries = 5);
  void connect();
  void disconnect();
  void close();
  virtual ~WiFiStation();
};

#endif // WIFI_STA_H