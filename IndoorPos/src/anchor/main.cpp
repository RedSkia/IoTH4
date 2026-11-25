// ===== Anker (målestation) =====
// Lytter på wi-fi-pakker (promiscuous mode), måler RSSI pr. enhed og sender
// et gennemsnit hvert WINDOW_MS til gatewayen med krypteret ESP-NOW.

#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <ESP32_NOW.h>
#include <mbedtls/sha256.h>
#include "config.h"
#include "protocol.h"
#include "secrets.h"

#ifndef ANCHOR_ID
#error "ANCHOR_ID mangler. Vælg et anchor-miljø i platformio.ini (fx env:anchor1)."
#endif

// ---------- Opsamling (deles mellem wi-fi-tråden og loop) ----------
const int MAX_DEVICES = 32;

struct Seen {
  uint8_t  mac[6];                                   // rå MAC, ligger kun i RAM i ét vindue
  int32_t  rssiSum;
  uint16_t count;
};

Seen         seen[MAX_DEVICES];
int          seenCount = 0;
portMUX_TYPE seenLock  = portMUX_INITIALIZER_UNLOCKED;

// ---------- ESP-NOW-forbindelse til gatewayen ----------
class GatewayPeer : public ESP_NOW_Peer {
public:
  GatewayPeer() : ESP_NOW_Peer(GATEWAY_MAC, 0, WIFI_IF_STA, ESPNOW_LMK) {}
  bool begin() { return add(); }
  bool sendMeasurement(const Measurement& m) {
    waitingForAck = true;
    bool ok = send((const uint8_t*)&m, sizeof(m)) == sizeof(m);
    if (!ok) waitingForAck = false;
    return ok;
  }
  void onSent(bool success) override {
    if (!success) failed++;
    waitingForAck = false;
  }
  volatile bool     waitingForAck = false;
  volatile uint32_t failed        = 0;
};

GatewayPeer gateway;
uint32_t    seq = 0;

// ---------- Hjælpefunktioner ----------
bool sameMac(const uint8_t* a, const uint8_t* b) { return memcmp(a, b, 6) == 0; }

bool isOwnSetup(const uint8_t* mac) {                // vores egne boards skal ikke spores
  if (sameMac(mac, GATEWAY_MAC)) return true;
  for (int i = 0; i < ANCHOR_COUNT; i++)
    if (sameMac(mac, ANCHOR_MAC[i])) return true;
  return false;
}

// SHA-256(salt + MAC), de første 8 byte bruges som anonymt id
void hashMac(const uint8_t* mac, uint8_t* out) {
  uint8_t input[64];
  size_t saltLen = strnlen(HASH_SALT, sizeof(input) - 6);
  memcpy(input, HASH_SALT, saltLen);
  memcpy(input + saltLen, mac, 6);
  uint8_t digest[32];
  mbedtls_sha256(input, saltLen + 6, digest, 0);
  memcpy(out, digest, DEVICE_ID_BYTES);
}

void printMac(const char* label, const uint8_t* mac) {
  Serial.printf("%s %02X:%02X:%02X:%02X:%02X:%02X\n", label, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

// ---------- Sniffer (kører i wi-fi-tråden, skal være kort) ----------
void onPacket(void* buf, wifi_promiscuous_pkt_type_t type) {
  const wifi_promiscuous_pkt_t* pkt = (const wifi_promiscuous_pkt_t*)buf;
  int rssi = pkt->rx_ctrl.rssi;
  if (rssi < RSSI_MIN) return;
  if (pkt->rx_ctrl.sig_len < 24) return;             // for kort til en 802.11-header

  const uint8_t* frame = pkt->payload;
  uint8_t subtype = (frame[0] >> 4) & 0x0F;
  bool    fromAp  = frame[1] & 0x02;                 // FromDS: sendt af routeren
  if (type == WIFI_PKT_MGMT && (subtype == 8 || subtype == 13)) return;  // beacon og action (ESP-NOW)
  if (type == WIFI_PKT_DATA && fromAp) return;

  const uint8_t* mac = frame + 10;                   // addr2 = afsenderen
  if (sameMac(mac, frame + 16)) return;              // afsender = BSSID (addr3): en router, ikke en mobil
  if (isOwnSetup(mac)) return;

  portENTER_CRITICAL(&seenLock);
  int i = 0;
  while (i < seenCount && !sameMac(seen[i].mac, mac)) i++;
  if (i == seenCount && seenCount < MAX_DEVICES) {
    memcpy(seen[i].mac, mac, 6);
    seen[i].rssiSum = 0;
    seen[i].count   = 0;
    seenCount++;
  }
  if (i < seenCount) {
    seen[i].rssiSum += rssi;
    seen[i].count++;
  }
  portEXIT_CRITICAL(&seenLock);
}

// ---------- Opstart ----------
// Ankrene er ikke forbundet til routeren, men skal være på dens kanal,
// ellers kan gatewayen (som er forbundet) ikke modtage ESP-NOW.
int findRouterChannel() {
  while (true) {
    Serial.printf("Searching for %s ...\n", WIFI_SSID);
    int found = WiFi.scanNetworks();
    for (int i = 0; i < found; i++) {
      if (WiFi.SSID(i) == WIFI_SSID) {
        int channel = WiFi.channel(i);
        WiFi.scanDelete();
        return channel;
      }
    }
    WiFi.scanDelete();
    for (int n = 0; n < 10; n++) {                   // hurtigt blink = router ikke fundet
      digitalWrite(LED_PIN, n % 2);
      delay(250);
    }
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);

  WiFi.mode(WIFI_STA);
  while (!WiFi.STA.started()) delay(10);
  uint8_t ownMac[6];
  WiFi.macAddress(ownMac);
  Serial.printf("\nAnchor A%d\n", ANCHOR_ID);
  printMac("My MAC:", ownMac);                       // skriv den ind i config.h (ANCHOR_MAC)

  int channel = findRouterChannel();
  WiFi.setChannel(channel);
  Serial.printf("Channel %d\n", channel);

  if (!ESP_NOW.begin(ESPNOW_PMK) || !gateway.begin()) {
    Serial.println("ESP-NOW error - check GATEWAY_MAC in config.h");
    while (true) delay(1000);
  }

  wifi_promiscuous_filter_t filter = {.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA};
  esp_wifi_set_promiscuous_filter(&filter);
  esp_wifi_set_promiscuous_rx_cb(&onPacket);
  esp_wifi_set_promiscuous(true);
  Serial.println("Sniffing");
}

// ---------- Hvert vindue: send gennemsnit pr. enhed ----------
void loop() {
  delay(WINDOW_MS);

  Seen batch[MAX_DEVICES];                           // kopiér og nulstil hurtigt
  portENTER_CRITICAL(&seenLock);
  int n = seenCount;
  memcpy(batch, seen, n * sizeof(Seen));
  seenCount = 0;
  portEXIT_CRITICAL(&seenLock);

  int sent = 0;
  for (int i = 0; i < n; i++) {
    if (batch[i].count < MIN_SAMPLES) continue;

    Measurement m = {};
    m.version  = PROTOCOL_VERSION;
    m.anchorId = ANCHOR_ID;
    m.seq      = seq++;
    hashMac(batch[i].mac, m.deviceId);
    m.rssi     = batch[i].rssiSum / batch[i].count;
    m.samples  = batch[i].count > 255 ? 255 : batch[i].count;

    digitalWrite(LED_PIN, HIGH);                     // kort blink = sender
    if (gateway.sendMeasurement(m)) {
      unsigned long start = millis();                // vent på kvittering før næste
      while (gateway.waitingForAck && millis() - start < 100) delay(1);
      sent++;
    }
    digitalWrite(LED_PIN, LOW);
  }

  memset(batch, 0, sizeof(batch));                   // rå MAC'er slettes efter brug
  Serial.printf("Window: %d devices, %d sent, %lu failed\n", n, sent, (unsigned long)gateway.failed);
}
