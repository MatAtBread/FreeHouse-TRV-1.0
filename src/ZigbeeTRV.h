/* Class of Zigbee Temperature sensor endpoint inherited from common EP class */

#pragma once

#include "soc/soc_caps.h"
#include "sdkconfig.h"
#if SOC_IEEE802154_SUPPORTED && CONFIG_ZB_ENABLED

#include "ZigbeeEP.h"

class ZigbeeTRV : public ZigbeeEP {
public:
  ZigbeeTRV(uint8_t endpoint);
  ~ZigbeeTRV();

  void onTempRecieve(void (*callback)(float)) {
    _on_temp_recieve = callback;
  }
  void onConfigRecieve(void (*callback)(float, float, float)) {
    _on_config_recieve = callback;
  }

  void getTemperature();
  void getSensorSettings();
  void setTemperatureReporting(uint16_t min_interval, uint16_t max_interval, float delta);

private:
  // save instance of the class in order to use it in static functions
  static ZigbeeTRV *_instance;

  void (*_on_temp_recieve)(float);
  void (*_on_config_recieve)(float, float, float);
  float _min_temp;
  float _max_temp;
  float _tolerance;

  void findEndpoint(esp_zb_zdo_match_desc_req_param_t *cmd_req);
  static void bindCb(esp_zb_zdp_status_t zdo_status, void *user_ctx);
  static void findCb(esp_zb_zdp_status_t zdo_status, uint16_t addr, uint8_t endpoint, void *user_ctx);

  void zbAttributeRead(uint16_t cluster_id, const esp_zb_zcl_attribute_t *attribute) override;
  void zbAttributeSet(const esp_zb_zcl_set_attr_value_message_t *message) override;
};

#endif  //SOC_IEEE802154_SUPPORTED && CONFIG_ZB_ENABLED
