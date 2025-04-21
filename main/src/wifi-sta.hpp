/** Wrappers to the underlying ESP-IDF peripheral libraries */
#ifndef WIFI_STA_H
#define WIFI_STA_H

#include "esp_netif.h"

class WiFiStation {
  friend void _event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);

 protected:
  void wifi_init_sta(void);
  void event_handler(esp_event_base_t event_base, int32_t event_id, void *event_data);
  uint8_t ssid[32];
  uint8_t password[64];
  const char *device_name;
  int maxRetries;
  int s_retry_num;

  esp_event_handler_instance_t instance_any_id;
  esp_event_handler_instance_t instance_got_ip;
  esp_netif_t *sta_netif;
  EventGroupHandle_t s_wifi_event_group;

 public:
  WiFiStation(const uint8_t *ssid, const uint8_t *password, const char *device_name = NULL, int maxRetries = 5);
  void connect();
  void disconnect();
  void close();
  virtual ~WiFiStation();
};

#endif  // WIFI_STA_H