#ifndef CAPTIVE_WIFI_H
#define CAPTIVE_WIFI_H

#include "../common/captiveportal/wifi-captiveportal.h"
#include "trv-state.h"

typedef enum {
  NONE,
  TEST_MODE,
  TIME_OUT,
  CLOSED,
  CALIBRATE,
  POWER_OFF
} exit_status_t;

class CaptivePortal : public HttpGetHandler {
 protected:
  Trv* trv;
  uint32_t timeout;
  bool calibrating = false;
  void exitPortal(exit_status_t status);

 public:
  CaptivePortal(Trv* trv, const char* name);
  virtual esp_err_t getHandler(httpd_req_t* req);
  exit_status_t exitStatus;
};

#endif
