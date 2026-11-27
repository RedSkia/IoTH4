#pragma once
#include <stdint.h>

// ===== Alle centrale indstillinger samlet ét sted =====
// Koder (wi-fi, MQTT, nøgler) ligger i secrets.h, som ikke er i git.
// Hvert anker får sit nummer i Serial Monitor (gemmes i ankeret). Positionen regnes ud fra opstillingen nedenfor.
// MAC-adresser og kanal skal ikke skrives nogen steder: ankrene finder selv gatewayen.

// ---------- Radiomodel (kalibrér, se README) ----------
const float RSSI_AT_1M         = -45.0f;             // målt RSSI 1 m fra et anker
const float PATH_LOSS_EXPONENT = 2.5f;               // 2 = frit rum, 3-4 = indendørs med forhindringer
const int   RSSI_MIN           = -90;                // svagere pakker ignoreres
const float RSSI_SMOOTHING     = 0.5f;               // 0-1: lavere = roligere position, men langsommere
// RSSI_AT_1M og PATH_LOSS_EXPONENT er startværdier. Skriv "calib -55 3" på gatewayen for at
// ændre dem uden upload (gemmes i flash).

// ---------- Opstilling (startværdier) ----------
// Antal ankre og afstand ændres i gatewayens Serial Monitor, uden at uploade igen:
//   anchors 5      spacing 2.5      list
// Det gemmes i gatewayens flash. Tallene her bruges kun, til der er skrevet noget.
// Ankrene stilles i en ring MED URET, set oppefra. A1 øverst til venstre, A2 til højre for den:
//   2 ankre:  A1 ── A2        4 ankre:  A1 ── A2        3 ankre:  A1 ── A2
//                                       │      │                    \  /
//                                       A4 ── A3                     A3
// 5, 6, 7, ... ankre: samme idé, en femkant, sekskant osv.
const int   DEFAULT_ANCHORS   = 4;                   // antal ankre (2, 3, 4, 5, ...)
const float DEFAULT_SPACING_M = 2.0f;                // meter mellem to naboankre i ringen

// ---------- Ankre ----------
const int  MIN_ANCHORS    = 2;                       // 2 = position på linjen mellem dem, 3+ = (x, y)
const int  MAX_ANCHORS    = 32;                      // så mange kan gatewayen holde styr på
const bool ESPNOW_ENCRYPT = true;                    // krypteret ESP-NOW: højst 7 ankre (ESP32-grænse)
                                                     // false: ubegrænset antal, men målingerne er ukrypterede
const int  MAX_ENCRYPTED_ANCHORS = 7;                // ESP32-grænsen for krypterede forbindelser

// ---------- Tider ----------
const unsigned long WINDOW_MS         = 2000;        // anker: gennemsnit over så lang tid
const unsigned long STALE_MS          = 6000;        // gateway: ældre målinger bruges ikke
const unsigned long FORGET_MS         = 60000;       // gateway: glem enheder, der ikke er set (dataminimering)
const unsigned long GATEWAY_HELLO_MS  = 500;         // gateway melder sig så ofte
const unsigned long ANCHOR_HELLO_MS   = 10000;       // anker melder sig så ofte (gatewayen kan være genstartet)
const unsigned long LOST_GATEWAY_MS   = 30000;       // anker: intet hørt fra gatewayen så længe = led igen
const unsigned long LOST_ANCHOR_MS    = 30000;       // gateway: intet hørt fra et anker så længe = fjern det
const int           MIN_SAMPLES       = 2;           // mindst 2 pakker i et vindue

// ---------- MQTT ----------
const char* const MQTT_HOST   = "wilsons.local";
const int         MQTT_PORT   = 8883;                // TLS + login
const bool        PUBLISH_RAW = true;                // true = send også rå RSSI (til kalibrering)

// ---------- Tid ----------
const char* const TIMEZONE = "CET-1CEST,M3.5.0,M10.5.0/3";   // dansk tid

// ---------- Status-LED ----------
const int LED_PIN = 2;                               // indbygget blå LED

// ---------- Tjek ved kompilering ----------
static_assert(DEFAULT_ANCHORS >= MIN_ANCHORS && DEFAULT_ANCHORS <= MAX_ANCHORS, "DEFAULT_ANCHORS must be between 2 and 32");
static_assert(!ESPNOW_ENCRYPT || DEFAULT_ANCHORS <= MAX_ENCRYPTED_ANCHORS,
              "Encrypted ESP-NOW allows max 7 anchors: set ESPNOW_ENCRYPT = false");
static_assert(LOST_ANCHOR_MS > 2 * ANCHOR_HELLO_MS, "LOST_ANCHOR_MS must allow at least two missed anchor hellos");
