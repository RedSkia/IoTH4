#pragma once
#include <stdint.h>

// Beskeden et anker sender til gatewayen med ESP-NOW (16 byte, under grænsen på 250 byte).
// Enheden identificeres kun med en hash af MAC-adressen. Den rå MAC sendes aldrig.

const uint8_t PROTOCOL_VERSION = 1;
const int     DEVICE_ID_BYTES  = 8;                  // 64 bit af SHA-256-hashen

struct __attribute__((packed)) Measurement {
  uint8_t  version;                                  // PROTOCOL_VERSION
  uint8_t  anchorId;                                 // 1..4
  uint32_t seq;                                      // løbenummer: afslører tabte beskeder
  uint8_t  deviceId[DEVICE_ID_BYTES];                // hash af enhedens MAC
  int8_t   rssi;                                     // gennemsnit i vinduet (dBm)
  uint8_t  samples;                                  // antal pakker bag gennemsnittet
};
