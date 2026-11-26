// ===== Rolle: gateway =====
// Melder sig til ankrene, modtager RSSI (ESP-NOW), beregner hver enheds position
// og sender id, tid og koordinat til MQTT med TLS og login.
// Ankrene melder selv nummer og position, så gatewayen kender dem uden config.

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ESP32_NOW.h>
#include <math.h>
#include "config.h"
#include "protocol.h"
#include "positioning.h"
#include "secrets.h"
#include "ca_cert.h"
#include "roles.h"

namespace gateway {

// ---------- Modtagelse (ESP-NOW-tråden lægger ind, loop() tager ud) ----------
struct Packet {
  uint8_t mac[6];                                    // afsender
  bool    fromPeer;                                  // kom fra en kendt (evt. krypteret) forbindelse
  uint8_t len;
  uint8_t data[32];
};

QueueHandle_t inbox;

void enqueue(const uint8_t* mac, const uint8_t* data, int len, bool fromPeer) {
  if (len <= 0 || len > (int)sizeof(Packet::data)) return;
  Packet p;
  memcpy(p.mac, mac, 6);
  p.fromPeer = fromPeer;
  p.len      = len;
  memcpy(p.data, data, len);
  xQueueSend(inbox, &p, 0);                          // fuld kø: beskeden droppes
}

class AnchorPeer : public ESP_NOW_Peer {             // krypteret forbindelse til ét anker
public:
  AnchorPeer(const uint8_t* mac) : ESP_NOW_Peer(mac, 0, WIFI_IF_STA, ESPNOW_LMK) {}
  bool begin() { return add(); }
  bool end()   { return remove(); }
  void onReceive(const uint8_t* data, size_t len, bool broadcast) override {
    enqueue(addr(), data, len, !broadcast);
  }
};

class BroadcastPeer : public ESP_NOW_Peer {          // til "hello" til alle
public:
  BroadcastPeer() : ESP_NOW_Peer(ESP_NOW.BROADCAST_ADDR, 0, WIFI_IF_STA, nullptr) {}
  bool begin() { return add(); }
  bool sendBytes(const void* data, int len) { return send((const uint8_t*)data, len) == (size_t)len; }
};

void onUnknownSender(const esp_now_recv_info_t* info, const uint8_t* data, int len, void* arg) {
  enqueue(info->src_addr, data, len, false);
}

BroadcastPeer broadcast;
uint32_t      group = 0;                             // vores gruppe (hash af MQTT_USER)

// ---------- Enheder vi kender lige nu ----------
const int MAX_TRACKED = 64;

struct Tracked {
  bool          used;
  uint8_t       id[DEVICE_ID_BYTES];
  int8_t        rssi[MAX_ANCHORS];                   // indeks = plads i anchors[]
  unsigned long seenAt[MAX_ANCHORS];                 // 0 = aldrig set af det anker
  unsigned long lastSeen;
};

Tracked tracked[MAX_TRACKED];

Tracked* findOrAdd(const uint8_t* id) {
  Tracked* freeSlot = nullptr;
  Tracked* oldest   = &tracked[0];
  for (Tracked& t : tracked) {
    if (t.used && memcmp(t.id, id, DEVICE_ID_BYTES) == 0) return &t;
    if (!t.used && !freeSlot) freeSlot = &t;
    if (t.lastSeen < oldest->lastSeen) oldest = &t;
  }
  Tracked* t = freeSlot ? freeSlot : oldest;         // fuld tabel: erstat den ældste
  memset(t, 0, sizeof(Tracked));
  t->used = true;
  memcpy(t->id, id, DEVICE_ID_BYTES);
  return t;
}

// ---------- Ankre, der har meldt sig ----------
struct Anchor {
  bool        used;
  uint8_t     mac[6];
  uint8_t     id;
  Point       pos;
  AnchorPeer* peer;                                  // kun ved kryptering
};

Anchor anchors[MAX_ANCHORS];

Anchor* findAnchor(const uint8_t* mac) {
  for (Anchor& a : anchors)
    if (a.used && memcmp(a.mac, mac, 6) == 0) return &a;
  return nullptr;
}

void forgetAnchor(Anchor& a) {
  int slot = &a - anchors;
  for (Tracked& t : tracked) t.seenAt[slot] = 0;    // dens gamle målinger bruges ikke mere
  if (a.peer) {
    a.peer->end();
    delete a.peer;
  }
  memset(&a, 0, sizeof(Anchor));
}

void registerAnchor(const uint8_t* mac, const AnchorHello& hello) {
  // Et andet board med samme nummer? Så har det nye board taget over (fx et udskiftet anker)
  for (Anchor& old : anchors) {
    if (old.used && old.id == hello.anchorId && memcmp(old.mac, mac, 6) != 0) {
      Serial.printf("A%d moved to a new board (if this repeats, two boards have number %d)\n", old.id, old.id);
      forgetAnchor(old);
    }
  }

  Anchor* a = findAnchor(mac);
  bool isNew = !a;
  if (isNew) {
    for (Anchor& slot : anchors) if (!slot.used) { a = &slot; break; }
    if (!a) {
      Serial.println("Too many anchors - raise MAX_ANCHORS in config.h");
      return;
    }
    memset(a, 0, sizeof(Anchor));
    a->used = true;
    memcpy(a->mac, mac, 6);
  }
  Point pos = isnan(hello.x) ? ringPosition(hello.anchorId, ANCHORS, SPACING_M)   // kun nummer: brug opstillingen
                            : Point{hello.x, hello.y};                            // egen position
  bool moved = a->id != hello.anchorId || a->pos.x != pos.x || a->pos.y != pos.y;
  a->id  = hello.anchorId;
  a->pos = pos;
  if (isnan(hello.x) && hello.anchorId > ANCHORS)
    Serial.printf("Warning: A%d is higher than ANCHORS = %d in config.h\n", hello.anchorId, ANCHORS);

  if (ESPNOW_ENCRYPT && !a->peer) {
    a->peer = new AnchorPeer(mac);
    if (!a->peer->begin()) {
      Serial.printf("A%d: no room for more encrypted anchors - set ESPNOW_ENCRYPT = false\n", a->id);
      forgetAnchor(*a);
      return;
    }
  }

  if (isNew || moved) Serial.printf("Anchor A%d at (%.2f, %.2f) %s\n", a->id, a->pos.x, a->pos.y, isNew ? "joined" : "updated");
}

// ---------- Netværk ----------
WiFiClientSecure net;
PubSubClient     mqtt(net);
String           topicPosition;                      // elever/<bruger>/position
String           topicRaw;                           // elever/<bruger>/rssi
unsigned long    lastMqttTry = 0;

void connectMqtt() {
  if (mqtt.connected() || millis() - lastMqttTry < 5000) return;
  lastMqttTry = millis();
  String clientId = String(MQTT_USER) + "-gateway";
  if (mqtt.connect(clientId.c_str(), MQTT_USER, MQTT_PASSWORD)) Serial.println("MQTT connected");
  else                                                          Serial.printf("MQTT error %d\n", mqtt.state());
  digitalWrite(LED_PIN, mqtt.connected());           // LED tændt = klar
}

String idToHex(const uint8_t* id) {
  char hex[DEVICE_ID_BYTES * 2 + 1];
  for (int i = 0; i < DEVICE_ID_BYTES; i++) sprintf(hex + i * 2, "%02x", id[i]);
  return String(hex);
}

void timeText(char* out, size_t size, time_t now) {  // fx "2026-11-26 10:15:02 CET"
  struct tm local;
  localtime_r(&now, &local);
  strftime(out, size, "%Y-%m-%d %H:%M:%S %Z", &local);
}

// ---------- Behandling af beskeder ----------
void handleMeasurement(Anchor& anchor, const Measurement& m) {
  if (m.anchorId != anchor.id) return;               // passer ikke med ankerets hello

  int slot = &anchor - anchors;
  Tracked* t = findOrAdd(m.deviceId);
  t->rssi[slot]   = m.rssi;
  t->seenAt[slot] = millis();
  t->lastSeen     = millis();

  if (PUBLISH_RAW && mqtt.connected()) {             // til kalibrering
    char json[96];
    snprintf(json, sizeof(json), "{\"anchor\":%d,\"id\":\"%s\",\"rssi\":%d,\"samples\":%d}",
             anchor.id, idToHex(m.deviceId).c_str(), m.rssi, m.samples);
    mqtt.publish(topicRaw.c_str(), json);
  }
}

void handle(const Packet& p) {
  if (p.len < 2 || p.data[0] != PROTOCOL_VERSION) return;
  uint8_t type = p.data[1];

  if (type == MSG_ANCHOR_HELLO && p.len == sizeof(AnchorHello)) {
    AnchorHello hello;
    memcpy(&hello, p.data, sizeof(hello));
    if (hello.group != group || hello.anchorId == 0) return;   // en anden gruppes anker
    registerAnchor(p.mac, hello);
  }
  else if (type == MSG_MEASUREMENT && p.len == sizeof(Measurement)) {
    Anchor* anchor = findAnchor(p.mac);
    if (!anchor) return;                             // ankeret har ikke meldt sig endnu
    if (ESPNOW_ENCRYPT && !p.fromPeer) return;       // kryptering påkrævet: ukrypterede målinger afvises
    Measurement m;
    memcpy(&m, p.data, sizeof(m));
    handleMeasurement(*anchor, m);
  }
}

// ---------- Beregning ----------
void computePositions() {
  // Området = rektanglet omkring alle kendte ankre
  Point boxMin = {1e9f, 1e9f}, boxMax = {-1e9f, -1e9f};
  for (Anchor& a : anchors) {
    if (!a.used) continue;
    boxMin = {fminf(boxMin.x, a.pos.x), fminf(boxMin.y, a.pos.y)};
    boxMax = {fmaxf(boxMax.x, a.pos.x), fmaxf(boxMax.y, a.pos.y)};
  }

  unsigned long now = millis();
  for (Tracked& t : tracked) {
    if (!t.used) continue;
    if (now - t.lastSeen > FORGET_MS) {              // dataminimering: glem gamle enheder
      t.used = false;
      continue;
    }

    Point pos[MAX_ANCHORS];
    float dist[MAX_ANCHORS];
    int   count = 0;
    for (int s = 0; s < MAX_ANCHORS; s++) {
      if (!anchors[s].used || t.seenAt[s] == 0 || now - t.seenAt[s] > STALE_MS) continue;
      pos[count]  = anchors[s].pos;
      dist[count] = rssiToDistance(t.rssi[s], RSSI_AT_1M, PATH_LOSS_EXPONENT);
      count++;
    }
    if (count < MIN_ANCHORS) continue;

    Point p;
    if (!locate(pos, dist, count, p)) continue;
    p = clampToBox(p, boxMin, boxMax);

    time_t ts = time(nullptr);
    if (ts < 1700000000) {                           // tiden er ikke hentet (NTP) endnu: ingen 1970-data
      Serial.println("No valid time yet - position not published");
      return;
    }
    char clock[32];
    timeText(clock, sizeof(clock), ts);
    char json[160];
    snprintf(json, sizeof(json), "{\"id\":\"%s\",\"time\":\"%s\",\"ts\":%ld,\"x\":%.2f,\"y\":%.2f,\"anchors\":%d}",
             idToHex(t.id).c_str(), clock, (long)ts, p.x, p.y, count);
    if (mqtt.connected()) mqtt.publish(topicPosition.c_str(), json);
    Serial.println(json);
  }
}

void printAnchors() {
  int n = 0;
  Serial.print("Anchors:");
  for (Anchor& a : anchors)
    if (a.used) { Serial.printf(" A%d", a.id); n++; }
  Serial.printf("  (%d)\n", n);
  if (n < MIN_ANCHORS) Serial.printf("Need at least %d anchors for a position\n", MIN_ANCHORS);
}

// ---------- Program ----------
void setup() {
  Serial.println("Role: gateway");
  group = groupId(MQTT_USER);
  inbox = xQueueCreate(128, sizeof(Packet));          // plads til mange ankre og mobiler på én gang

  WiFi.setSleep(false);                              // radioen skal altid lytte, ellers tabes ESP-NOW-beskeder
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  unsigned long waitStart = millis();
  while (WiFi.status() != WL_CONNECTED) {            // rollen kan stadig skiftes, mens den venter
    handleConsole();
    if (millis() - waitStart > 15000) {
      Serial.printf("Still no WiFi - check WIFI_SSID/WIFI_PASSWORD in secrets.h (%s)\n", WIFI_SSID);
      waitStart = millis();
    }
    delay(250);
  }
  Serial.printf("WiFi OK, channel %d\n", (int)WiFi.channel());

  static String router = WiFi.gatewayIP().toString();   // static: NTP gemmer kun en pointer til navnet
  configTzTime(TIMEZONE, "pool.ntp.org", router.c_str());
  struct tm now;
  if (getLocalTime(&now, 10000)) Serial.println(&now, "Time OK: %Y-%m-%d %H:%M:%S");
  else                           Serial.println("Time error (no NTP server)");

  net.setCACert(CA_CERT);
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  topicPosition = String("elever/") + MQTT_USER + "/position";
  topicRaw      = String("elever/") + MQTT_USER + "/rssi";
  connectMqtt();

  if (!ESP_NOW.begin(ESPNOW_PMK) || !broadcast.begin()) {
    Serial.println("ESP-NOW error - restarting");
    delay(1000);
    ESP.restart();
  }
  ESP_NOW.onNewPeer(onUnknownSender, nullptr);
  Serial.println("Waiting for anchors");
}

void loop() {
  connectMqtt();
  mqtt.loop();

  Packet p;
  while (xQueueReceive(inbox, &p, 0) == pdTRUE) handle(p);

  static unsigned long lastHello = 0;
  if (millis() - lastHello >= GATEWAY_HELLO_MS) {    // "her er jeg" til ankre, der leder
    lastHello = millis();
    GatewayHello hello = {PROTOCOL_VERSION, MSG_GATEWAY_HELLO, group};
    broadcast.sendBytes(&hello, sizeof(hello));
  }

  static unsigned long lastCompute = 0;
  if (millis() - lastCompute >= WINDOW_MS) {
    lastCompute = millis();
    computePositions();
  }

  static unsigned long lastStatus = 0;
  if (millis() - lastStatus >= 10000) {
    lastStatus = millis();
    printAnchors();
  }
  delay(10);
}

}  // namespace gateway
