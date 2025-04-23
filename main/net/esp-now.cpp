#include "esp-now.hpp"

#include <string>

#include "../src/NetMsg.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_wifi.h"
#include "string.h"
#include "esp_wifi_types.h"

#define MACSTR "%02X:%02X:%02X:%02X:%02X:%02X"
#define MAC2STR(mac) mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]

const char *topicAndInfo = "FreeHouse:{\"model\":\"" FREEHOUSE_MODEL "\"}";

typedef uint8_t MACAddr[6];

const MACAddr BROADCAST_ADDR = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
RTC_DATA_ATTR MACAddr hub = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
RTC_DATA_ATTR int wifiChannel = 0;

typedef struct {
  MACAddr mac;
  esp_wifi_rxctrl_t rx;
} pairing_info_t;

static pairing_info_t pairInfo[20] = {0};
static pairing_info_t *nextPair = NULL;

esp_err_t add_peer(const uint8_t *mac, uint8_t channel) {
  esp_now_peer_info_t peer;
  memset(&peer, 0, sizeof(peer));
  memcpy(peer.peer_addr, mac, sizeof(peer.peer_addr));
  peer.channel = channel;
  peer.encrypt = false;

  return (esp_now_is_peer_exist(mac) ? esp_now_mod_peer : esp_now_add_peer)(&peer);
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
  ESP_LOGI(TAG, "recv-now: src:" MACSTR " dst: " MACSTR ", ch %u+%u, rssi %d: %.*s",
    MAC2STR(esp_now_info->src_addr),
    MAC2STR(esp_now_info->des_addr),
    esp_now_info->rx_ctrl->channel,
    esp_now_info->rx_ctrl->second,
    esp_now_info->rx_ctrl->rssi,
    data_len, data);

  if (data[0] == '{') {
    // We got some data
    processNetMessage((const char *)data, trv);
  } else if (memcmp(data, "PACK", 4) == 0) {
    if (nextPair == NULL) {
      ESP_LOGW(TAG, "Pairing finished!");
      return;
    }
    if (nextPair - pairInfo >= sizeof(pairInfo) / sizeof(pairing_info_t)) {
      nextPair = NULL;
      ESP_LOGW(TAG, "Pairing table full");
      return;
    }
    // Avoid duplicate macs
    for (auto p = pairInfo; p < nextPair; p++) {
      if (memcmp(p->mac, esp_now_info->src_addr, sizeof(MACAddr)) == 0) {
        if (p->rx.rssi < esp_now_info->rx_ctrl->rssi) {
          p->rx = *esp_now_info->rx_ctrl;
        }
        return;
      }
    }
    auto p = nextPair++;
    p->rx = *esp_now_info->rx_ctrl;
    memcpy(p->mac, esp_now_info->src_addr, sizeof(MACAddr));
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
  ESP_LOGI(TAG, "Init EspNet");
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
  ESP_LOGI(TAG, "EspNet created");
}

EspNet::~EspNet() {
  ESP_LOGI(TAG, "De-init radio");
  ESP_ERROR_CHECK(esp_now_deinit());
  ESP_ERROR_CHECK(esp_wifi_stop());
  ESP_ERROR_CHECK(esp_wifi_deinit());
}

static volatile uint8_t new_channel = 0xFF;  // Invalid channel
static void channel_change_event(void *event_handler_arg,
                                 esp_event_base_t event_base,
                                 int32_t event_id,
                                 void *event_data) {
  // ESP_LOGI(TAG, "Channel change event %s %ld", event_base, event_id);
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_HOME_CHANNEL_CHANGE) {
    wifi_event_home_channel_change_t *event = (wifi_event_home_channel_change_t *)event_data;
    // ESP_LOGI(TAG, "Channel change %u+%u was %u+%u", event->new_chan, event->new_snd, event->old_chan, event->old_snd);
    new_channel = event->new_chan;
  }
}

esp_err_t set_channel(int channel) {
  esp_err_t e = esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
  if (e == ESP_OK) {
    int elapsed = 0;
    while (new_channel != channel && elapsed < 500) {
      vTaskDelay(pdMS_TO_TICKS(20));
      elapsed += 20;
    }
    if (new_channel != channel) {
      ESP_LOGW(TAG, "Timed out waiting for channel change event");
      e = ESP_ERR_TIMEOUT;
    }
  } else {
    ESP_LOGE(TAG, "Failed to set channel %d", channel);
  }
  return e;
}

void EspNet::checkMessages() {
  std::string pairName = "PAIR";
  pairName += Trv::deviceName();
  pairName += ':';
  pairName += topicAndInfo;
  if (wifiChannel == 0) {
    esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_HOME_CHANNEL_CHANGE, channel_change_event, NULL);

    add_peer(BROADCAST_ADDR, 0);
    memset(pairInfo, 0, sizeof(pairInfo));
    nextPair = pairInfo;
    for (int ch = 1; ch <= 13; ch++) {
      set_channel(ch);
      esp_now_send(BROADCAST_ADDR, (const uint8_t *)pairName.c_str(), pairName.length());
      delay(50);  // Wait for responses
    }
    pairing_info_t *lastPair = nextPair;
    nextPair = NULL;

    pairing_info_t *best = NULL;
    for (auto p = pairInfo; p < lastPair; p++) {
      add_peer(p->mac, p->rx.channel);
      if (!best || p->rx.rssi > best->rx.rssi)
        best = p;
    }

    if (best == NULL) {
      ESP_LOGW(TAG, "Failed to find hub");
    } else {
      ESP_LOGI(TAG, "Best hub " MACSTR " channel %d+%d, rssi %d", MAC2STR(best->mac), best->rx.channel, best->rx.second, best->rx.rssi);
      for (auto p = pairInfo; p < lastPair; p++) {
        if (p != best && memcmp(p->mac, best->mac, sizeof(MACAddr))) {
          add_peer(p->mac, p->rx.channel);
          set_channel(p->rx.channel);
          ESP_ERROR_CHECK_WITHOUT_ABORT(esp_now_send(p->mac, (const uint8_t *)"NACK", 5));
          ESP_LOGI(TAG, "Nack'd hub " MACSTR " channel %d+%d, rssi %d", MAC2STR(p->mac), p->rx.channel, p->rx.second, p->rx.rssi);
        }
      }

      memcpy(hub, best->mac, sizeof(MACAddr));
      wifiChannel = best->rx.channel;
      ESP_LOGI(TAG, "Paired with hub " MACSTR " on channel %d", MAC2STR(hub), wifiChannel);
      set_channel(wifiChannel);
    }
    esp_event_handler_unregister(WIFI_EVENT, WIFI_EVENT_HOME_CHANNEL_CHANGE, channel_change_event);
  } else {
    ESP_LOGI(TAG, "Already paired with hub " MACSTR, MAC2STR(hub));
    esp_wifi_set_channel(wifiChannel, WIFI_SECOND_CHAN_NONE);
    // We send a PAIR here just to elicit any deferred messages
    add_peer(hub, wifiChannel);
    esp_now_send(hub, (const uint8_t *)pairName.c_str(), pairName.length());
    delay(50);  // Wait for responses
  }
}
