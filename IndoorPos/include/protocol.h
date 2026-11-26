#pragma once
#include <stdint.h>

// Beskeder mellem ankre og gateway over ESP-NOW.
// Enheder identificeres kun med en hash af MAC-adressen. Den rå MAC sendes aldrig.

const uint8_t PROTOCOL_VERSION = 3;
const int     DEVICE_ID_BYTES  = 8;                  // 64 bit af SHA-256-hashen

enum MessageType : uint8_t {
  MSG_GATEWAY_HELLO = 1,                             // gateway → alle: "her er jeg"
  MSG_ANCHOR_HELLO  = 2,                             // anker → alle: mit nummer og min position
  MSG_MEASUREMENT   = 3,                             // anker → gateway: RSSI for én enhed
};

// Gruppens nummer: en hash af MQTT-brugernavnet. Boards parrer kun med deres egen gruppe,
// selvom flere hold kører samme program i samme lokale.
inline uint32_t groupId(const char* name) {
  uint32_t hash = 2166136261u;                       // FNV-1a
  while (*name) {
    hash ^= (uint8_t)*name++;
    hash *= 16777619u;
  }
  return hash;
}

struct __attribute__((packed)) GatewayHello {
  uint8_t  version;
  uint8_t  type;                                     // MSG_GATEWAY_HELLO
  uint32_t group;
};

struct __attribute__((packed)) AnchorHello {
  uint8_t  version;
  uint8_t  type;                                     // MSG_ANCHOR_HELLO
  uint32_t group;
  uint8_t  anchorId;                                 // 1..255
  float    x;                                        // position i meter, NAN = gatewayen regner den ud
  float    y;
};

struct __attribute__((packed)) Measurement {
  uint8_t  version;
  uint8_t  type;                                     // MSG_MEASUREMENT
  uint8_t  anchorId;
  uint32_t seq;                                      // løbenummer: afslører tabte beskeder
  uint8_t  deviceId[DEVICE_ID_BYTES];                // hash af enhedens MAC
  int8_t   rssi;                                     // gennemsnit i vinduet (dBm)
  uint8_t  samples;                                  // antal pakker bag gennemsnittet
};
