#include "esp-now.hpp"

#include <string>

#include "../../common/bind/bind.hpp"
#include "../src/NetMsg.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_sleep.h"
#include "esp_wifi.h"
#include "string.h"

#define MACSTR "%02X:%02X:%02X:%02X:%02X:%02X"
#define MAC2STR(mac) mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]

const char *topicAndInfo = "FreeHouse:{\"model\":\"TRV1\"}";

typedef uint8_t MACAddr[6];

const MACAddr BROADCAST_ADDR = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
RTC_DATA_ATTR MACAddr hub = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
RTC_DATA_ATTR int wifiChannel = 0;

void add_peer(const uint8_t *mac, uint8_t channel) {
  if (!esp_now_is_peer_exist(mac)) {
    esp_now_peer_info_t peer = {};
    peer.channel = channel;
    peer.ifidx = WIFI_IF_STA;
    memcpy(peer.peer_addr, mac, sizeof(peer.peer_addr));
    ESP_LOGI(TAG, "Add peer " MACSTR " channel %d", MAC2STR(mac), peer.channel);
    ESP_ERROR_CHECK(esp_now_add_peer(&peer));
  }
}

void EspNet::sendStateToHub(const trv_state_t &state) {
  if (wifiChannel == 0 || memcmp(hub, BROADCAST_ADDR, sizeof(hub)) == 0) {
    ESP_LOGW(TAG, "Not paired with hub, not sending state");
    return;
  }

  add_peer(hub, wifiChannel);

  // TODO: Check if the state has changed since the last update
  auto json = trv->asJson(state);
  ESP_LOGI(TAG, "Send state %s", json.c_str());
  esp_now_send(hub, (uint8_t *)json.c_str(), json.length());
}

void EspNet::data_receive_callback(const esp_now_recv_info_t *esp_now_info, const uint8_t *data, int data_len) {
  ESP_LOGI(TAG, "recv-now: src:" MACSTR " dst: " MACSTR " %.*s", MAC2STR(esp_now_info->src_addr), MAC2STR(esp_now_info->des_addr), data_len, data);
  if (data[0] == '{') {
    // We got some data
    processNetMessage((const char *)data, trv);
  } else if (memcmp(data, "PACK", 4) == 0) {
    memcpy(hub, esp_now_info->src_addr, sizeof(hub));
    ESP_LOGI(TAG, "Paired with hub " MACSTR " on channel %d", MAC2STR(hub), esp_now_info->rx_ctrl->channel);
    wifiChannel = esp_now_info->rx_ctrl->channel;
  } else {
    memcpy(hub, BROADCAST_ADDR, sizeof(hub));
    wifiChannel = 0;
  }
}

void EspNet::data_send_callback(const uint8_t *mac_addr, esp_now_send_status_t status) {
  if (status != ESP_NOW_SEND_SUCCESS) {
    ESP_LOGW(TAG, "send-now: " MACSTR " %s", MAC2STR(mac_addr), "Failed");
    memcpy(hub, BROADCAST_ADDR, sizeof(hub));
    wifiChannel = 0;
  } else {
    ESP_LOGI(TAG, "send-now: " MACSTR " %s", MAC2STR(mac_addr), "Sent");
  }
}

EspNet *instance;
static void boundRx(const esp_now_recv_info_t *esp_now_info, const uint8_t *data, int data_len) {
  instance->data_receive_callback(esp_now_info, data, data_len);
}
static void boundTx(const uint8_t *mac_addr, esp_now_send_status_t status) {
  instance->data_send_callback(mac_addr, status);
}

EspNet::EspNet(Trv *trv) : trv(trv) {
  instance = this;

  // 2. Configure WiFi
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));

  // 3. Avoid NVS usage for faster startup
  ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));

  // 4. Set minimal WiFi mode (STA or AP)
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_start());

  // 5. Initialize ESP-NOW
  ESP_ERROR_CHECK(esp_now_init());

  // 7. Register callbacks
  esp_now_register_recv_cb(boundRx);
  esp_now_register_send_cb(boundTx);
}

EspNet::~EspNet() {
  ESP_LOGI(TAG, "De-init radio");
  ESP_ERROR_CHECK(esp_now_deinit());
  ESP_ERROR_CHECK(esp_wifi_stop());
  ESP_ERROR_CHECK(esp_wifi_deinit());
}

void EspNet::checkMessages() {
  std::string pairName = "PAIR";
  pairName += Trv::deviceName();
  pairName += ':';
  pairName += topicAndInfo;
  if (wifiChannel == 0) {
    add_peer(BROADCAST_ADDR, 0);
    for (int ch = 1; ch <= 13; ch++) {
      ESP_ERROR_CHECK(esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE));
      esp_now_send(BROADCAST_ADDR, (const uint8_t *)pairName.c_str(), pairName.length());
      delay(50);  // Wait for responses
      if (wifiChannel) {
        ESP_ERROR_CHECK(esp_wifi_set_channel(wifiChannel, WIFI_SECOND_CHAN_NONE));
        break;
      }
    }
    if (!wifiChannel) {
      ESP_LOGW(TAG, "Failed to pair with hub");
    }
  } else {
    ESP_LOGI(TAG, "Already paired with hub " MACSTR, MAC2STR(hub));
    ESP_ERROR_CHECK(esp_wifi_set_channel(wifiChannel, WIFI_SECOND_CHAN_NONE));
    // We send a PAIR here just to elicit any deferred messages
    add_peer(hub, wifiChannel);
    esp_now_send(hub, (const uint8_t *)pairName.c_str(), pairName.length());
    delay(50);  // Wait for responses
  }
}
