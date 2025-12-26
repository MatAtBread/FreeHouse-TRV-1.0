#include "pins.h"
#include "trv.h"
#include "trv-state.h"

#include "esp_log.h"
#include "esp_sleep.h"
#include "helpers.h"
#include "wifi-sta.hpp"
#include "esp_https_ota.h"
#ifdef CONFIG_ESP_HTTPS_OTA_ALLOW_HTTP
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#endif
#include "../common/gpio/gpio.hpp"

static int64_t content_len = -1;
static int64_t content_read = 0;
static int lastPercent = -1;

typedef struct {
  esp_ota_handle_t handle;
  const esp_partition_t *partition;
} ota_data_t;

void rtc_ram_preserving_restart() {
  esp_sleep_enable_timer_wakeup(1000000ULL);
  esp_deep_sleep_start();
}

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
            ERR_BACKTRACE(esp_ota_write(update->handle, evt->data, evt->data_len));
        }
        break;
    case HTTP_EVENT_ON_FINISH:
        ESP_LOGI(TAG, "HTTP_EVENT_ON_FINISH");
        ERR_BACKTRACE(esp_ota_end(update->handle));
        ERR_BACKTRACE(esp_ota_set_boot_partition(update->partition));
        // rtc_ram_preserving_restart();
        esp_restart();
        break;
    case HTTP_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED");
        break;
    case HTTP_EVENT_REDIRECT:
        ESP_LOGI(TAG, "HTTP_EVENT_REDIRECT");
        //esp_http_client_set_header(evt->client, "From", "user@example.com");
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
  }
  void task() {
    while (seconds-- > 0 && !cancel) {
      ESP_LOGI(TAG, "SoftWatchDog %d %d", seconds, cancel);
      vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
    if (!cancel && seconds <= 0) {
      ESP_LOGW(TAG, "SoftWatchDog restart!");
      vTaskDelay(100 / portTICK_PERIOD_MS);
      rtc_ram_preserving_restart();
    }
  }
};

void Trv::requestUpdate(const char *otaUrl, const char *otaSsid, const char *otaPwd) {
  this->otaUrl = otaUrl;
  this->otaSsid = otaSsid;
  this->otaPwd = otaPwd;
}

void Trv::doUpdate() {
    std::string otaUrlStr = otaUrl;
    if (!otaUrlStr.ends_with("/")) {
      otaUrlStr += "/";
    }
    otaUrlStr += FREEHOUSE_MODEL;
    otaUrlStr += "/";
    otaUrlStr += CONFIG_IDF_TARGET;
    otaUrlStr += "/";
    otaUrlStr += "trv-1.bin";

    WiFiStation sta((const uint8_t *)otaSsid.c_str(), (const uint8_t *)otaPwd.c_str(), Trv::deviceName(), 1);
    sta.connect();

    ESP_LOGI(TAG, "OTA update URL: %s, Wifi %s", otaUrlStr.c_str(), otaSsid.c_str());
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
      ERR_BACKTRACE(esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &update_handle));
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
        // rtc_ram_preserving_restart();
        esp_restart();
      }
    }
  }