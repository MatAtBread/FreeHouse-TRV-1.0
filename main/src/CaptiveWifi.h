#ifndef CAPATIVE_WIFI_H
#define CAPATIVE_WIFI_H

#include "DNSServer.h"
#include "WiFi.h"
#include "WithTask.h"
#include "trv-state.h"

class CaptivePortal : public WithTask {
 protected:
  Heartbeat* heartbeat;
  WiFiServer server;
  DNSServer dnsServer;
  Trv* trv;

 public:
  CaptivePortal(Heartbeat* heartbeat, Trv* trv);
  void task();
};

#endif
