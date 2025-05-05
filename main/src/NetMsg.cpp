#include "esp_log.h"
#include "esp_https_ota.h"
#ifdef CONFIG_ESP_HTTPS_OTA_ALLOW_HTTP
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#endif
#include "wifi-sta.hpp"

#include "string.h"
#include "trv-state.h"
#include "trv.h"
#include "../common/gpio/gpio.hpp"
#include "NetMsg.h"
#include "cJSON.h"

extern const char *systemModes[];

static int64_t content_len = -1;
static int64_t content_read = 0;
static int lastPercent = -1;

typedef struct {
  esp_ota_handle_t handle;
  const esp_partition_t *partition;
} ota_data_t;

esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
  ota_data_t *update = (ota_data_t *)evt->user_data;

    switch(evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGI(TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGI(TAG, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
            // ESP_LOGI(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            if (!esp_http_client_is_chunked_response(evt->client)) {
                // If user_data buffer is configured, copy the response into the buffer
                if (content_len == -1) {
                    content_len = esp_http_client_get_content_length(evt->client);
                    ESP_LOGI(TAG, "content_len=%lld", content_len);
                }
                content_read += evt->data_len;
                int percent = (content_read * 100) / content_len;
                if (percent != lastPercent) {
                    ESP_LOGI(TAG, "Download progress: %d%% (%lld of %lld)", percent, content_read, content_len);
                    lastPercent = percent;
                }
                GPIO::digitalWrite(LED_BUILTIN, !GPIO::digitalRead(LED_BUILTIN));
                ESP_ERROR_CHECK_WITHOUT_ABORT(esp_ota_write(update->handle, evt->data, evt->data_len));
            }
            break;

        case HTTP_EVENT_ON_FINISH:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_FINISH");
            ESP_ERROR_CHECK_WITHOUT_ABORT(esp_ota_end(update->handle));
            ESP_ERROR_CHECK_WITHOUT_ABORT(esp_ota_set_boot_partition(update->partition));
            esp_restart();
            break;

            case HTTP_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED");
            break;
        case HTTP_EVENT_REDIRECT:
            ESP_LOGI(TAG, "HTTP_EVENT_REDIRECT");
            esp_http_client_set_header(evt->client, "From", "user@example.com");
            esp_http_client_set_header(evt->client, "Accept", "text/html");
            esp_http_client_set_redirection(evt->client);
            break;
    }
    return ESP_OK;
}

class SoftWatchDog: public WithTask {
  public:
  int seconds;
  bool cancel;
  SoftWatchDog(int seconds): seconds(seconds) {
    StartTask(SoftWatchDog);
  }
  ~SoftWatchDog() {
    cancel = true;
    wait();
  }
  void task() {
    while (seconds-- && !cancel) {
      ESP_LOGI(TAG, "SoftWatchDog %d %d", seconds, cancel);
      vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
    if (!cancel && seconds <= 0) {
      ESP_LOGW(TAG, "SoftWatchDog restart!");
      vTaskDelay(100 / portTICK_PERIOD_MS);
      esp_restart();
    }
  }
};

NetMsg::~NetMsg() {
  if (otaUrl[0] && otaSsid[0]) {
    std::string otaUrlStr = otaUrl;
    if (!otaUrlStr.ends_with("/")) {
      otaUrlStr += "/";
    }
    otaUrlStr += FREEHOUSE_MODEL;
    otaUrlStr += "/";
    otaUrlStr += CONFIG_IDF_TARGET;
    otaUrlStr += "/";
    otaUrlStr += "trv-1.bin";

    WiFiStation sta((const uint8_t *)otaSsid, (const uint8_t *)otaPwd, Trv::deviceName(), 1);
    sta.connect();

    ESP_LOGI(TAG, "OTA update URL: %s, Wifi %s", otaUrlStr.c_str(), otaSsid);
    esp_http_client_config_t config = {
        .url = otaUrlStr.c_str(),
        .cert_pem = NULL,
        .timeout_ms = 5000,
        .event_handler = _http_event_handler,
        //.transport_type = HTTP_TRANSPORT_OVER_TCP
    };

    if (otaUrlStr.starts_with("http://")) {
      // Get OTA partition
//      SoftWatchDog *woof = new SoftWatchDog(150); // Max 2.5 mins to update
      SoftWatchDog woof(150);
      const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
      esp_ota_handle_t update_handle = 0;
      ESP_ERROR_CHECK_WITHOUT_ABORT(esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &update_handle));
      ota_data_t od = {.handle = update_handle, .partition = update_partition};
      config.user_data = &od;

      esp_http_client_handle_t client = esp_http_client_init(&config);
      esp_err_t err = esp_http_client_perform(client);
      if (err == ESP_OK) {
          ESP_LOGI(TAG, "HTTP GET Status = %d, content_length = %" PRId64,
                  esp_http_client_get_status_code(client),
                  esp_http_client_get_content_length(client));
      } else {
          ESP_LOGE(TAG, "HTTP GET request failed: %s", esp_err_to_name(err));
      }
      esp_http_client_cleanup(client);

      ESP_LOGI(TAG, "Woof %d", woof.seconds);
    } else {
      esp_https_ota_config_t ota_config = {
          .http_config = &config,
      };
      esp_err_t ret = esp_https_ota(&ota_config);

      if (ret == ESP_OK) {
        esp_restart();
      }
    }
  }
}

void NetMsg::processNetMessage(const char *json, Trv *trv) {
  cJSON *root = cJSON_Parse(json);
  if (!root) {
    ESP_LOGW(TAG, "JSON parse failed: %s\n", json);
    return;
  }

  cJSON *system_mode = cJSON_GetObjectItem(root, "system_mode");
  cJSON *current_heating_setpoint = cJSON_GetObjectItem(root, "current_heating_setpoint");
  cJSON *local_temperature_calibration = cJSON_GetObjectItem(root, "local_temperature_calibration");

  if (cJSON_IsString(system_mode) && (system_mode->valuestring != NULL)) {
    for (esp_zb_zcl_thermostat_system_mode_t mode = ESP_ZB_ZCL_THERMOSTAT_SYSTEM_MODE_OFF;
         mode <= ESP_ZB_ZCL_THERMOSTAT_SYSTEM_MODE_SLEEP;
         mode = (esp_zb_zcl_thermostat_system_mode_t)(mode + 1)) {
      if (!strcasecmp(systemModes[mode], system_mode->valuestring)) {
        ESP_LOGI(TAG, "system_mode %s (%d)\n", system_mode->valuestring, mode);
        messageCount++;
        trv->setSystemMode(mode);
      }
    }
  }

  if (cJSON_IsNumber(current_heating_setpoint)) {
    ESP_LOGI(TAG, "current_heating_setpoint %f\n", current_heating_setpoint->valuedouble);
    messageCount++;
    trv->setHeatingSetpoint((float)current_heating_setpoint->valuedouble);
  }

  if (cJSON_IsNumber(local_temperature_calibration)) {
    ESP_LOGI(TAG, "local_temperature_calibration %f\n", local_temperature_calibration->valuedouble);
    messageCount++;
    trv->setTempCalibration((float)local_temperature_calibration->valuedouble);
  }

  cJSON *ota = cJSON_GetObjectItem(root, "ota");
  if (cJSON_IsObject(ota)) {
    cJSON *url = cJSON_GetObjectItem(ota, "url");
    cJSON *ssid = cJSON_GetObjectItem(ota, "ssid");
    cJSON *pwd = cJSON_GetObjectItem(ota, "pwd");
    if (cJSON_IsString(url) && (url->valuestring != NULL)
      && cJSON_IsString(ssid) && (ssid->valuestring != NULL)
      && cJSON_IsString(pwd) && (pwd->valuestring != NULL)) {
      strncpy(otaUrl, url->valuestring, sizeof(otaUrl) - 1);
      strncpy(otaSsid, ssid->valuestring, sizeof(otaSsid) - 1);
      strncpy(otaPwd, pwd->valuestring, sizeof(otaPwd) - 1);
    }
    ESP_LOGI(TAG, "OTA URL: %s, Wifi %s", otaUrl, otaSsid);
  }
  // Free the root object
  cJSON_Delete(root);
}
