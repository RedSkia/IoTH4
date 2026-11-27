#pragma once
// Anonymt enheds-id = de første 8 byte af SHA-256(salt + MAC).
// Bruges af ankeret (på hver wi-fi-pakke) og af gatewayen (når man skriver en MAC i "watch"),
// så begge laver præcis samme id. Den rå MAC sendes aldrig.

#include <string.h>
#include <mbedtls/sha256.h>
#include <mbedtls/platform_util.h>
#include "protocol.h"
#include "secrets.h"

inline void deviceIdFromMac(const uint8_t* mac, uint8_t* out) {
  uint8_t input[64];
  size_t saltLen = 0;
  while (saltLen < sizeof(input) - 6 && HASH_SALT[saltLen]) saltLen++;
  memcpy(input, HASH_SALT, saltLen);
  memcpy(input + saltLen, mac, 6);
  uint8_t digest[32];
  mbedtls_sha256(input, saltLen + 6, digest, 0);
  memcpy(out, digest, DEVICE_ID_BYTES);
  mbedtls_platform_zeroize(input, sizeof(input));    // den rå MAC slettes fra hukommelsen
  mbedtls_platform_zeroize(digest, sizeof(digest));
}
