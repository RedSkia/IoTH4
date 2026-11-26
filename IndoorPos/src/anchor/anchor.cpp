// ===== Rolle: anker (målestation) =====
// Lytter på wi-fi-pakker (promiscuous mode), måler RSSI pr. enhed og sender
// et gennemsnit hvert WINDOW_MS til gatewayen med ESP-NOW.
// Gatewayen og dens kanal findes automatisk (ingen MAC eller kanal i config).

#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <ESP32_NOW.h>
#include <mbedtls/sha256.h>
#include <mbedtls/platform_util.h>
#include <math.h>
#include "config.h"
#include "protocol.h"
#include "secrets.h"
#include "roles.h"

namespace anchor {

// ---------- Ankerets nummer og position (gemt i flash af main.cpp) ----------
uint8_t anchorId = 0;
float   anchorX  = 0;
float   anchorY  = 0;

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

// ---------- ESP-NOW ----------
class BroadcastPeer : public ESP_NOW_Peer {          // til "hello" til alle
public:
  BroadcastPeer() : ESP_NOW_Peer(ESP_NOW.BROADCAST_ADDR, 0, WIFI_IF_STA, nullptr) {}
  bool begin() { return add(); }
  bool sendBytes(const void* data, int len) { return send((const uint8_t*)data, len) == (size_t)len; }
};

class GatewayPeer : public ESP_NOW_Peer {            // til målinger
public:
  GatewayPeer(const uint8_t* mac)
    : ESP_NOW_Peer(mac, 0, WIFI_IF_STA, ESPNOW_ENCRYPT ? ESPNOW_LMK : nullptr) {}
  bool begin() { return add(); }
  void onReceive(const uint8_t* data, size_t len, bool broadcast) override {
    lastHeard = millis();                            // gatewayens "hello" hver GATEWAY_HELLO_MS
  }
  bool sendMeasurement(const Measurement& m) {
    waitingForAck = true;
    bool ok = send((const uint8_t*)&m, sizeof(m)) == sizeof(m);
    if (!ok) waitingForAck = false;
    return ok;
  }
  void onSent(bool success) override {
    if (success) delivered = delivered + 1;
    else         failed    = failed + 1;
    waitingForAck = false;
  }
  volatile bool          waitingForAck = false;
  volatile uint32_t      delivered     = 0;          // kvitteret af gatewayen
  volatile uint32_t      failed        = 0;
  volatile unsigned long lastHeard     = 0;
};

BroadcastPeer broadcast;
GatewayPeer*  gateway = nullptr;
uint8_t       gatewayMac[6];
volatile bool gatewayFound = false;
uint32_t      seq = 0;
uint32_t      group = 0;                             // vores gruppe (hash af MQTT_USER)

// Beskeder fra ukendte afsendere: her opdager vi gatewayen
void onUnknownSender(const esp_now_recv_info_t* info, const uint8_t* data, int len, void* arg) {
  if (gatewayFound || len != sizeof(GatewayHello)) return;
  GatewayHello hello;
  memcpy(&hello, data, sizeof(hello));
  if (hello.version != PROTOCOL_VERSION || hello.type != MSG_GATEWAY_HELLO) return;
  if (hello.group != group) return;                  // en anden gruppes gateway
  memcpy(gatewayMac, info->src_addr, 6);
  gatewayFound = true;
}

// ---------- Hjælpefunktioner ----------
bool sameMac(const uint8_t* a, const uint8_t* b) { return memcmp(a, b, 6) == 0; }

// SHA-256(salt + MAC), de første 8 byte bruges som anonymt id
void hashMac(const uint8_t* mac, uint8_t* out) {
  uint8_t input[64];
  size_t saltLen = strnlen(HASH_SALT, sizeof(input) - 6);
  memcpy(input, HASH_SALT, saltLen);
  memcpy(input + saltLen, mac, 6);
  uint8_t digest[32];
  mbedtls_sha256(input, saltLen + 6, digest, 0);
  memcpy(out, digest, DEVICE_ID_BYTES);
  mbedtls_platform_zeroize(input, sizeof(input));
}

void printMac(const char* label, const uint8_t* mac) {
  Serial.printf("%s %02X:%02X:%02X:%02X:%02X:%02X\n", label, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

void blink(int times, int ms) {
  for (int i = 0; i < times; i++) {
    digitalWrite(LED_PIN, HIGH);
    delay(ms);
    digitalWrite(LED_PIN, LOW);
    delay(ms);
  }
}

// ---------- Sniffer (kører i wi-fi-tråden, skal være kort) ----------
void onPacket(void* buf, wifi_promiscuous_pkt_type_t type) {
  const wifi_promiscuous_pkt_t* pkt = (const wifi_promiscuous_pkt_t*)buf;
  int rssi = pkt->rx_ctrl.rssi;
  if (rssi < RSSI_MIN) return;
  if (pkt->rx_ctrl.sig_len < 24) return;             // for kort til en 802.11-header

  // Kun datapakker fra en enhed til routeren (ToDS = 1, FromDS = 0).
  // Det udelukker routere, beacons, ESP-NOW og "probe requests", hvor mobilen bruger en
  // tilfældig MAC, der skifter hele tiden og ellers ville give falske enheder.
  if (type != WIFI_PKT_DATA) return;
  const uint8_t* frame = pkt->payload;
  if ((frame[1] & 0x03) != 0x01) return;

  const uint8_t* mac = frame + 10;                   // addr2 = afsenderen
  if (sameMac(mac, gatewayMac)) return;              // vores egen gateway skal ikke spores

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
// Ankrene er ikke forbundet til routeren, men skal være på dens kanal, ellers kan gatewayen
// (som er forbundet) ikke modtage ESP-NOW. Ankeret prøver kanal 1-13, til det hører gatewayen.
// Det virker også, når der er flere routere med samme navn på forskellige kanaler.
void sayHello() {
  AnchorHello hello = {PROTOCOL_VERSION, MSG_ANCHOR_HELLO, group, anchorId, anchorX, anchorY};
  broadcast.sendBytes(&hello, sizeof(hello));
}

void findGateway() {
  Serial.println("Looking for gateway on channel 1-13 ...");
  int channel = 1;
  while (!gatewayFound) {                            // dobbeltblink = leder efter gatewayen
    esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
    unsigned long start = millis();
    while (!gatewayFound && millis() - start < 2 * GATEWAY_HELLO_MS) {
      blink(2, 60);
      delay(150);
      handleConsole();
    }
    if (!gatewayFound) channel = channel % 13 + 1;
  }
  Serial.printf("Gateway found on channel %d\n", channel);
  printMac("Gateway MAC:", gatewayMac);

  gateway = new GatewayPeer(gatewayMac);
  if (!gateway->begin()) {
    Serial.println("Could not add gateway - restarting");
    delay(1000);
    ESP.restart();
  }
  gateway->lastHeard = millis();
  sayHello();                                        // fortæl gatewayen nummer og position
}

void setup(uint8_t id, float x, float y) {
  anchorId = id;
  anchorX  = x;
  anchorY  = y;
  group    = groupId(MQTT_USER);
  memset(gatewayMac, 0, sizeof(gatewayMac));
  if (isnan(anchorX)) Serial.printf("Role: anchor A%d (position from config.h)\n", anchorId);
  else                Serial.printf("Role: anchor A%d at (%.2f, %.2f)\n", anchorId, anchorX, anchorY);

  if (!ESP_NOW.begin(ESPNOW_PMK) || !broadcast.begin()) {
    Serial.println("ESP-NOW error - restarting");
    delay(1000);
    ESP.restart();
  }
  ESP_NOW.onNewPeer(onUnknownSender, nullptr);
  findGateway();

  wifi_promiscuous_filter_t filter = {.filter_mask = WIFI_PROMIS_FILTER_MASK_DATA};
  esp_wifi_set_promiscuous_filter(&filter);
  esp_wifi_set_promiscuous_rx_cb(&onPacket);
  esp_wifi_set_promiscuous(true);
  Serial.println("Sniffing");
}

// ---------- Hvert vindue: send gennemsnit pr. enhed ----------
void loop() {
  unsigned long windowStart = millis();
  while (millis() - windowStart < WINDOW_MS) {       // kan stadig sættes op imens
    handleConsole();
    delay(20);
  }

  static unsigned long lastHello = 0;
  if (millis() - lastHello >= ANCHOR_HELLO_MS) {     // gatewayen kan være genstartet
    lastHello = millis();
    sayHello();
  }

  Seen batch[MAX_DEVICES];                           // kopiér og nulstil hurtigt
  portENTER_CRITICAL(&seenLock);
  int n = seenCount;
  memcpy(batch, seen, n * sizeof(Seen));
  memset(seen, 0, n * sizeof(Seen));                 // rå MAC'er slettes med det samme
  seenCount = 0;
  portEXIT_CRITICAL(&seenLock);

  uint32_t deliveredBefore = gateway->delivered;
  int tried = 0;
  for (int i = 0; i < n; i++) {
    if (batch[i].count < MIN_SAMPLES) continue;

    Measurement m = {};
    m.version  = PROTOCOL_VERSION;
    m.type     = MSG_MEASUREMENT;
    m.anchorId = anchorId;
    m.seq      = seq++;
    hashMac(batch[i].mac, m.deviceId);
    m.rssi     = (int8_t)lroundf((float)batch[i].rssiSum / batch[i].count);
    m.samples  = batch[i].count > 255 ? 255 : batch[i].count;

    digitalWrite(LED_PIN, HIGH);                     // kort blink = sender
    if (gateway->sendMeasurement(m)) {
      unsigned long start = millis();                // vent på kvittering før næste
      while (gateway->waitingForAck && millis() - start < 100) delay(1);
    }
    tried++;
    digitalWrite(LED_PIN, LOW);
  }

  mbedtls_platform_zeroize(batch, sizeof(batch));    // rå MAC'er slettes (compileren kan ikke fjerne det)
  int delivered = gateway->delivered - deliveredBefore;
  Serial.printf("Window: %d devices, %d delivered of %d, %lu failed total\n",
                n, delivered, tried, (unsigned long)gateway->failed);

  // Gatewayen er slukket eller har skiftet kanal (routeren kan skifte): start forfra og led igen
  if (millis() - gateway->lastHeard > LOST_GATEWAY_MS) {
    Serial.println("Lost the gateway - restarting to find it again");
    ESP.restart();
  }
}

}  // namespace anchor
