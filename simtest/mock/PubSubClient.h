#pragma once
#include "WiFiClientSecure.h"
struct PubSubClient {
  PubSubClient(WiFiClientSecure&) {}
  void setServer(const char*, int) {}
  bool connect(const char*, const char*, const char*) { delay(800); return true; }  // TLS tager ca. 0,8 s
  int state() { return 0; }
  void loop() {}
  bool publish(const char*, const char* m) { sim.sent.push_back({sim.ms, m}); delay(50); return true; }
};
