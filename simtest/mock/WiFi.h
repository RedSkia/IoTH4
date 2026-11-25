#pragma once
#include "Arduino.h"
#define WL_CONNECTED 3
struct IPAddress { String toString() { return String("192.168.0.50"); } };
struct WiFiT {
  void begin(const char*, const char*) { delay(1000); }  // wifi tager ca. 1 s
  int status() { return WL_CONNECTED; }
  IPAddress localIP() { return {}; }
  IPAddress gatewayIP() { return {}; }
};
inline WiFiT WiFi;
