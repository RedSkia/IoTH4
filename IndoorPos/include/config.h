#pragma once
#include <stdint.h>
#include "positioning.h"

// ===== Alle centrale indstillinger samlet ét sted =====
// Koder (wi-fi, MQTT, nøgler) ligger i secrets.h, som ikke er i git.

// ---------- Området (meter) ----------
const float AREA_WIDTH  = 2.0f;                      // B: mål jeres bord/lokale
const float AREA_HEIGHT = 2.0f;                      // H

// ---------- Ankrene ----------
const int ANCHOR_COUNT = 4;
const Point ANCHOR_POS[ANCHOR_COUNT] = {             // A1..A4 i hjørnerne
  {0.0f,       0.0f},
  {AREA_WIDTH, 0.0f},
  {AREA_WIDTH, AREA_HEIGHT},
  {0.0f,       AREA_HEIGHT},
};

// MAC-adresser: hvert board skriver sin egen MAC i Serial Monitor ved opstart.
const uint8_t GATEWAY_MAC[6] = {0x0C, 0x8B, 0x95, 0x76, 0x20, 0x14};
const uint8_t ANCHOR_MAC[ANCHOR_COUNT][6] = {
  {0xB4, 0xBF, 0xE9, 0x04, 0xE1, 0x08},              // A1
  {0xEC, 0x64, 0xC9, 0x85, 0x4F, 0xC8},              // A2
  {0x40, 0x22, 0xD8, 0x07, 0x15, 0x10},              // A3
  {0xCC, 0xDB, 0xA7, 0x1C, 0xA8, 0x6C},              // A4
};

// ---------- Radiomodel (kalibrér, se README) ----------
const float RSSI_AT_1M         = -45.0f;             // målt RSSI 1 m fra et anker
const float PATH_LOSS_EXPONENT = 2.5f;               // 2 = frit rum, 3-4 = indendørs med forhindringer
const int   RSSI_MIN           = -90;                // svagere pakker ignoreres

// ---------- Tider ----------
const unsigned long WINDOW_MS  = 2000;               // anker: gennemsnit over så lang tid
const unsigned long STALE_MS   = 6000;               // gateway: ældre målinger bruges ikke
const unsigned long FORGET_MS  = 60000;              // gateway: glem enheder, der ikke er set (dataminimering)
const int           MIN_ANCHORS = 3;                 // mindst 3 ankre for at beregne (x, y)
const int           MIN_SAMPLES = 2;                 // mindst 2 pakker i et vindue

// ---------- MQTT ----------
const char* const MQTT_HOST   = "wilsons.local";
const int         MQTT_PORT   = 8883;                // TLS + login
const bool        PUBLISH_RAW = false;               // true = send også rå RSSI (til kalibrering)

// ---------- Tid ----------
const char* const TIMEZONE = "CET-1CEST,M3.5.0,M10.5.0/3";   // dansk tid

// ---------- Status-LED ----------
const int LED_PIN = 2;                               // indbygget blå LED
