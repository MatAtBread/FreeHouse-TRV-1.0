#ifndef CAPATIVE_WIFI_H
#define CAPATIVE_WIFI_H

#include "WiFi.h"
#include "DNSServer.h"
#include "WithTask.h"
#include "trv-state.h"

class CaptivePortal : public WithTask {
 protected:
   WiFiServer server;
   DNSServer dnsServer;
   Heartbeat* heartbeat;
   Trv* trv;

 public:
  CaptivePortal(Heartbeat* heartbeat, Trv* trv);
  void task();
};

#endif
