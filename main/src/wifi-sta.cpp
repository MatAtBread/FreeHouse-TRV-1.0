#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "nvs_flash.h"
#include "board.h"

#include "../trv.h"
#include "wifi-sta.hpp"

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

void _event_handler(void* wifi, esp_event_base_t event_base,
  int32_t event_id, void* event_data) {
    ((WiFiStation *)wifi)->event_handler(event_base, event_id, event_data);
}

void WiFiStation::event_handler(esp_event_base_t event_base, int32_t event_id, void* event_data) {
  ESP_LOGI(TAG, "Wifi event %s %ld", event_base, event_id);
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
    esp_wifi_connect();
  } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
    if (s_retry_num < maxRetries) {
      esp_wifi_connect();
      s_retry_num++;
      ESP_LOGI(TAG, "retry to connect to the AP");
    } else {
      xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
    }
    ESP_LOGI(TAG, "connect to the AP fail");
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    ip_event_got_ip_t* event = (ip_event_got_ip_t*)event_data;
    ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
    s_retry_num = 0;
    xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
  }
}

void WiFiStation::wifi_init_sta(void) {
  // ESP_ERROR_CHECK(esp_event_loop_create_default());
  s_retry_num = 0;
  sta_netif = esp_netif_create_default_wifi_sta();
  esp_netif_set_hostname(sta_netif, device_name);

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(dev_wifi_init(&cfg));

  ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                      ESP_EVENT_ANY_ID,
                                                      &_event_handler,
                                                      this,
                                                      &instance_any_id));
  ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                      IP_EVENT_STA_GOT_IP,
                                                      &_event_handler,
                                                      this,
                                                      &instance_got_ip));

  wifi_config_t wifi_config = {
      .sta = {
          /* Authmode threshold resets to WPA2 as default if password matches WPA2 standards (password len => 8).
           * If you want to connect the device to deprecated WEP/WPA networks, Please set the threshold value
           * to WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK and set the password with length and format matching to
           * WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK standards.
           */
          .threshold = {.authmode = WIFI_AUTH_OPEN},
          .sae_pwe_h2e = WPA3_SAE_PWE_UNSPECIFIED,
          .sae_h2e_identifier = ""},
  };
  memcpy(wifi_config.sta.ssid, ssid, sizeof wifi_config.sta.ssid);
  memcpy(wifi_config.sta.password, password, sizeof wifi_config.sta.password);

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
  ESP_ERROR_CHECK(esp_wifi_start());

  /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
   * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
  EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                         WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                         pdFALSE,
                                         pdFALSE,
                                         portTICK_PERIOD_MS * 10000); // Max 10 seconds

  /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
   * happened. */
  if (bits & WIFI_CONNECTED_BIT) {
    ESP_LOGI(TAG, "connected to ap SSID:%s password:%s", ssid, password);
  } else if (bits & WIFI_FAIL_BIT) {
    ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s", ssid, password);
  } else {
    ESP_LOGE(TAG, "UNEXPECTED EVENT");
  }
  ESP_LOGI(TAG, "wifi_init_sta finished.");
}

WiFiStation::WiFiStation(const uint8_t* ssid, const uint8_t* password, const char *device_name, int maxRetries) {
  this->maxRetries = maxRetries;
  this->device_name = device_name;
  memcpy(this->ssid, ssid, sizeof this->ssid);
  memcpy(this->password, password, sizeof this->password);
  s_wifi_event_group = xEventGroupCreate();
}


WiFiStation::~WiFiStation(){
  esp_wifi_stop();
  esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, &instance_any_id);
  esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, &instance_got_ip);
  dev_wifi_deinit();
  esp_netif_destroy_default_wifi(sta_netif);
  vEventGroupDelete(s_wifi_event_group);
  s_wifi_event_group = NULL;
}

void WiFiStation::connect() {
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");
  wifi_init_sta();
}

void WiFiStation::disconnect() {
  // TODO
}

void WiFiStation::close() {
  disconnect();
  // TODO
}