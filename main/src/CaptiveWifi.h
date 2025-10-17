#ifndef CAPTIVE_WIFI_H
#define CAPTIVE_WIFI_H

#include "trv-state.h"
#include "../common/captiveportal/wifi-captiveportal.h"

class CaptivePortal: public HttpGetHandler {
 protected:
  Trv* trv;
  uint32_t timeout;

 public:
  CaptivePortal(Trv* trv, const char *name);
  virtual esp_err_t getHandler(httpd_req_t *req);
};

#endif
