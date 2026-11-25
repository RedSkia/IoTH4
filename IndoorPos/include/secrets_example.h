// Skabelon: kopiér til secrets.h og skriv de rigtige koder.
// secrets.h må IKKE committes (den står i .gitignore).
#pragma once
#include <stdint.h>

const char* const WIFI_SSID     = "IoT_H3/4";
const char* const WIFI_PASSWORD = "wifi_password";
const char* const MQTT_USER     = "username";
const char* const MQTT_PASSWORD = "mqtt_password";

// ESP-NOW-nøgler, præcis 16 tegn hver. Samme værdier på alle boards.
const uint8_t ESPNOW_PMK[16] = {'c','h','a','n','g','e','_','m','e','_','p','m','k','_','1','6'};
const uint8_t ESPNOW_LMK[16] = {'c','h','a','n','g','e','_','m','e','_','l','m','k','_','1','6'};

// Hemmeligt salt til hash af MAC-adresser. Skift det, så kan gamle id'er ikke kobles til nye.
const char* const HASH_SALT = "change-me-to-a-long-random-text";
