#include "esp-now.hpp"

#include <string>
#include <sstream>

#include "../src/NetMsg.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_wifi.h"
#include "string.h"
#include "esp_wifi_types.h"
#include "../src/board.h"
#include "../common/encryption/encryption.h"

#define PAIR_DELIM "\x1D"
#define MACSTR "%02X:%02X:%02X:%02X:%02X:%02X"
#define MAC2STR(mac) mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]

typedef uint8_t MACAddr[6];

const MACAddr BROADCAST_ADDR = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
RTC_DATA_ATTR MACAddr hub = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
RTC_DATA_ATTR int wifiChannel = 0;
RTC_DATA_ATTR static signed int avgRssi = 0;

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
  auto json = trv->asJson(state, avgRssi);
  auto status = esp_now_send(hub, (uint8_t *)json.c_str(), json.length());
  ESP_LOGI(TAG, "Send state [%u] %s %s(%u)", json.length(), json.c_str(), status == ESP_OK ? "ok" : "failed", status);
}

void EspNet::data_receive_callback(const esp_now_recv_info_t *esp_now_info, const uint8_t *data, int data_len) {
  ESP_LOGI(TAG, "recv-now: src:" MACSTR " dst: " MACSTR ", ch %u+%u, rssi %d: %.*s",
    MAC2STR(esp_now_info->src_addr),
    MAC2STR(esp_now_info->des_addr),
    esp_now_info->rx_ctrl->channel,
    esp_now_info->rx_ctrl->second,
    esp_now_info->rx_ctrl->rssi,
    data_len, data);

  avgRssi = avgRssi ? (avgRssi + esp_now_info->rx_ctrl->rssi) / 2 : esp_now_info->rx_ctrl->rssi;

  if (data[0] == '{') {
    // We got some data
    processNetMessage((const char *)data, trv);
  } else if (memcmp(data, "PACK", 4) == 0) {
    if (nextPair == NULL) {
      ESP_LOGI(TAG, "Pairing finished!");
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
    if (wifiChannel > 0 && (wifiChannel == esp_now_info->rx_ctrl->channel || wifiChannel == esp_now_info->rx_ctrl->second)) {
      ESP_LOGW(TAG, "NACK? from hub " MACSTR " on channel %d+%d. Disconnecting", MAC2STR(esp_now_info->src_addr), esp_now_info->rx_ctrl->channel, esp_now_info->rx_ctrl->second);
      memcpy(hub, BROADCAST_ADDR, sizeof(hub));
      wifiChannel = 0;
    }
  }
}

void EspNet::data_send_callback(const uint8_t *mac_addr, esp_now_send_status_t status) {
  if (status != ESP_NOW_SEND_SUCCESS) {
    ESP_LOGW(TAG, "send-now: " MACSTR " %s", MAC2STR(mac_addr), "failed - diconnecting");
    memcpy(hub, BROADCAST_ADDR, sizeof(hub));
    wifiChannel = 0;
  } else {
    ESP_LOGI(TAG, "send-now: " MACSTR " %s", MAC2STR(mac_addr), "ok");
  }
}

EspNet *instance;
static void boundRx(const esp_now_recv_info_t *esp_now_info, const uint8_t *data, int data_len) {
  instance->data_receive_callback(esp_now_info, data, data_len);
}
static void boundTx(const esp_now_send_info_t *tx_info, esp_now_send_status_t status) {
  instance->data_send_callback(tx_info->des_addr, status);
}

EspNet::EspNet(Trv *trv) : trv(trv) {
  ESP_LOGI(TAG, "Init EspNet");
  instance = this;

  // 2. Configure WiFi
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(dev_wifi_init(&cfg));

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
  ESP_ERROR_CHECK(dev_wifi_deinit());
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

esp_err_t set_channel(uint8_t channel) {
  uint8_t current;
  wifi_second_chan_t secondary;
  esp_err_t e = esp_wifi_get_channel(&current, &secondary);
  ESP_LOGI(TAG, "Set wifi channel %d, current = %d", channel, current);
  if (e == ESP_OK && current == channel) {
    return ESP_OK;
  }
  new_channel = 0xFF;  // Reset volatile
  e = esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
  if (e == ESP_OK) {
    int elapsed = 0;
    while (new_channel != channel && elapsed < 500) {
      vTaskDelay(pdMS_TO_TICKS(20));
      elapsed += 20;
    }
    if (new_channel != channel) {
      ESP_LOGW(TAG, "Timed out waiting for channel %d change event", channel);
      e = ESP_ERR_TIMEOUT;
    } else {
      wifiChannel = channel;
    }
  } else {
    ESP_LOGE(TAG, "Failed to set channel %d", channel);
  }
  return e;
}

void pair_with_hub(uint8_t *out, size_t out_len) {
  esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_HOME_CHANNEL_CHANGE, channel_change_event, NULL);

  add_peer(BROADCAST_ADDR, 0);
  memset(pairInfo, 0, sizeof(pairInfo));
  nextPair = pairInfo;

  wifi_country_t country = {
      .cc = "GB",
      .schan = 1,
      .nchan = 13,
      .policy = WIFI_COUNTRY_POLICY_MANUAL};

  ESP_LOGI(TAG, "Wifi channel info %3s %d %d policy %d", country.cc, country.schan, country.nchan, country.policy);

  for (uint8_t ch = country.schan; ch < country.schan + country.nchan; ch++) {
    set_channel(ch);
    esp_now_send(BROADCAST_ADDR, out, out_len);
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
    set_channel(best->rx.channel);
    ESP_LOGI(TAG, "Paired with hub " MACSTR " on channel %d", MAC2STR(hub), wifiChannel);
  }
  esp_event_handler_unregister(WIFI_EVENT, WIFI_EVENT_HOME_CHANNEL_CHANGE, channel_change_event);
}

void EspNet::checkMessages() {
  uint8_t *out;
  size_t out_len;
  {
    //std::string pairName = "";
    std::stringstream pairName;
    pairName << Trv::deviceName()
    << PAIR_DELIM "FreeHouse" PAIR_DELIM
    "{"
    "\"model\":\"" FREEHOUSE_MODEL "\","
    "\"state_version\":" << Trv::stateVersion()<< ","
    "\"build\":\"" << versionDetail << "\","
    "\"writeable\":[";

    for (auto p = NetMsg::writeable; *p; p++) {
      if (p != NetMsg::writeable) pairName << ",";
      pairName << "\"" << *p << "\"";
    }
    pairName << "]}";

    ESP_LOGI(TAG, "Pairing as: %s", pairName.str().c_str());
    if (encrypt_bytes_with_passphrase(pairName.str().c_str(), 0, trv->getState(true).config.passKey, &out, &out_len)) {
      ESP_LOGW(TAG, "Failed to encrypt JOIN");
      return;
    }
  }

  {
    uint8_t *verbPhrase = (uint8_t *)malloc(out_len + 4);
    *((uint32_t *)verbPhrase) = *((const uint32_t *)"JOIN");
    memcpy(verbPhrase + 4, out, out_len);
    free(out);
    out = verbPhrase;
    out_len += 4;
  }

  if (wifiChannel == 0) {
    pair_with_hub(out, out_len);
  } else {
    ESP_LOGI(TAG, "Already paired with hub " MACSTR, MAC2STR(hub));
    esp_wifi_set_channel(wifiChannel, WIFI_SECOND_CHAN_NONE);
    // We send a PAIR here just to elicit any deferred messages
    add_peer(hub, wifiChannel);
    esp_now_send(hub, out, out_len);
    delay(50);  // Wait for responses
    // If we were disconnected from the hub, try again (once)
    if (wifiChannel == 0 || memcmp(hub, BROADCAST_ADDR, sizeof(hub)) == 0) {
      pair_with_hub(out, out_len);
    }
  }
  free(out);
}
