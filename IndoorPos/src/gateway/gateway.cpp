// ===== Rolle: gateway =====
// Melder sig til ankrene, modtager RSSI (ESP-NOW), beregner hver enheds position
// og sender id, tid og koordinat til MQTT med TLS og login.
// Ankrene melder selv nummer og position, så gatewayen kender dem uden config.
// Antal ankre og afstand kan ændres i Serial Monitor og gemmes i flash (anchors, spacing, list).
// Ankre, der ikke er hørt i LOST_ANCHOR_MS, fjernes af sig selv.

#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ESP32_NOW.h>
#include <math.h>
#include "config.h"
#include "protocol.h"
#include "positioning.h"
#include "secrets.h"
#include "device_id.h"
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
  float         rssi[MAX_ANCHORS];                   // udglattet RSSI, indeks = plads i anchors[]
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

// ---------- Enheder set for nylig (kun anonyme id'er, ingen målinger) ----------
// Så "watch db8" også kan finde en enhed, der ikke følges endnu. Glemmes efter FORGET_MS.
const int MAX_RECENT = 32;

struct Recent {
  bool          used;
  uint8_t       id[DEVICE_ID_BYTES];
  unsigned long at;
};

Recent recent[MAX_RECENT];

void rememberSeen(const uint8_t* id) {
  Recent* slot = &recent[0];
  for (Recent& r : recent) {
    if (r.used && memcmp(r.id, id, DEVICE_ID_BYTES) == 0) { r.at = millis(); return; }
    if (!r.used || millis() - r.at > FORGET_MS) { slot = &r; break; }   // ledig eller gammel plads
    if (r.at < slot->at) slot = &r;                                      // ellers den ældste
  }
  slot->used = true;
  memcpy(slot->id, id, DEVICE_ID_BYTES);
  slot->at = millis();
}

bool recentlySeen(const Recent& r) { return r.used && millis() - r.at <= FORGET_MS; }

// ---------- Opstilling (gemt i flash, ændres i Serial Monitor) ----------
Preferences layoutStore;
int         ringSize = DEFAULT_ANCHORS;              // antal ankre i ringen
float       spacing  = DEFAULT_SPACING_M;            // meter mellem naboer

bool validRingSize(int n) {
  return n >= MIN_ANCHORS && n <= MAX_ANCHORS && (!ESPNOW_ENCRYPT || n <= MAX_ENCRYPTED_ANCHORS);
}
bool validSpacing(float m) { return m >= 0.1f && m <= 100.0f; }   // false for NAN

// Radiomodel (kalibrering), også gemt i flash: "calib <rssi ved 1 m> <n>"
float rssiAt1m   = RSSI_AT_1M;
float pathLossN  = PATH_LOSS_EXPONENT;
bool validRssiAt1m(float a) { return a >= -100.0f && a <= -10.0f; }
bool validPathLoss(float n) { return n >= 1.0f && n <= 6.0f; }

void loadFilter();                                   // længere nede

void loadLayout() {
  layoutStore.begin("gateway", false);
  int   n = layoutStore.getInt("anchors", DEFAULT_ANCHORS);
  float m = layoutStore.getFloat("spacing", DEFAULT_SPACING_M);
  float a = layoutStore.getFloat("rssi1m", RSSI_AT_1M);
  float e = layoutStore.getFloat("n", PATH_LOSS_EXPONENT);
  ringSize  = validRingSize(n) ? n : DEFAULT_ANCHORS;
  spacing   = validSpacing(m)  ? m : DEFAULT_SPACING_M;
  rssiAt1m  = validRssiAt1m(a) ? a : RSSI_AT_1M;
  pathLossN = validPathLoss(e) ? e : PATH_LOSS_EXPONENT;
  loadFilter();
}

// ---------- Watch-liste: følg kun udvalgte enheder (gemt i flash) ----------
// Tom liste  = alle enheder spores.
// Ellers     = KUN enhederne på listen spores, sendes til MQTT og vises med RSSI fra hvert anker.
//              Andre enheder smides væk med det samme (dataminimering).
const int MAX_WATCHED = 16;
uint8_t   watched[MAX_WATCHED][DEVICE_ID_BYTES];
int       watchedCount = 0;

void loadFilter() {
  watchedCount = 0;
  if (layoutStore.isKey("watch")) {
    size_t len = layoutStore.getBytesLength("watch");
    if (len % DEVICE_ID_BYTES == 0 && len <= sizeof(watched)) {
      layoutStore.getBytes("watch", watched, len);
      watchedCount = len / DEVICE_ID_BYTES;
    }
  }
}

void saveFilter() {
  if (watchedCount > 0)              layoutStore.putBytes("watch", watched, watchedCount * DEVICE_ID_BYTES);
  else if (layoutStore.isKey("watch")) layoutStore.remove("watch");
}

int watchedIndex(const uint8_t* id) {
  for (int i = 0; i < watchedCount; i++)
    if (memcmp(watched[i], id, DEVICE_ID_BYTES) == 0) return i;
  return -1;
}

bool passesFilter(const uint8_t* id) { return watchedCount == 0 || watchedIndex(id) >= 0; }

void forgetSettings() {
  Preferences store;
  store.begin("gateway", false);
  store.clear();
  store.end();
}

// ---------- Ankre, der har meldt sig ----------
struct Anchor {
  bool          used;
  uint8_t       mac[6];
  uint8_t       id;
  bool          ownPos;                              // true = ankeret har selv fået en position (fx "3 1.5 2")
  Point         pos;
  unsigned long lastHeard;                           // sidste besked fra ankeret
  AnchorPeer*   peer;                                // kun ved kryptering
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
  bool  ownPos = !isnan(hello.x) && !isnan(hello.y);
  Point pos    = ownPos ? Point{hello.x, hello.y}                              // egen position
                        : ringPosition(hello.anchorId, ringSize, spacing);     // kun nummer: brug opstillingen
  bool moved = a->id != hello.anchorId || a->pos.x != pos.x || a->pos.y != pos.y;
  a->id        = hello.anchorId;
  a->ownPos    = ownPos;
  a->pos       = pos;
  a->lastHeard = millis();

  if (ESPNOW_ENCRYPT && !a->peer) {
    a->peer = new AnchorPeer(mac);
    if (!a->peer->begin()) {
      Serial.printf("A%d: no room for more encrypted anchors - set ESPNOW_ENCRYPT = false\n", a->id);
      forgetAnchor(*a);
      return;
    }
  }

  if (isNew || moved) {
    Serial.printf("Anchor A%d at (%.2f, %.2f) %s\n", a->id, a->pos.x, a->pos.y, isNew ? "joined" : "updated");
    if (!ownPos && a->id > ringSize)
      Serial.printf("Warning: A%d is higher than the number of anchors (%d). Type: anchors %d\n",
                    a->id, ringSize, a->id);
  }
}

// Ankre, der ikke er hørt længe (slukket, flyttet, løbet tør for strøm), fjernes
void dropSilentAnchors() {
  for (Anchor& a : anchors) {
    if (a.used && millis() - a.lastHeard > LOST_ANCHOR_MS) {
      Serial.printf("Anchor A%d left (nothing heard for %lu s)\n", a.id, LOST_ANCHOR_MS / 1000);
      forgetAnchor(a);
    }
  }
}

// Ny opstilling: regn ringpositionerne ud igen. Ankre med egen position røres ikke.
void applyLayout() {
  for (Anchor& a : anchors) {
    if (!a.used || a.ownPos) continue;
    a.pos = ringPosition(a.id, ringSize, spacing);
  }
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

  rememberSeen(m.deviceId);                          // kun id'et, så det kan vælges med watch
  if (!passesFilter(m.deviceId)) return;             // watch-liste: andre enheders målinger gemmes slet ikke

  int slot = &anchor - anchors;
  Tracked* t = findOrAdd(m.deviceId);
  bool fresh = t->seenAt[slot] != 0 && millis() - t->seenAt[slot] <= STALE_MS;
  t->rssi[slot]   = fresh ? t->rssi[slot] + RSSI_SMOOTHING * (m.rssi - t->rssi[slot])   // udglat støj
                          : m.rssi;                                                     // første måling
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
    if (m.anchorId == anchor->id) anchor->lastHeard = millis();
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
    if (now - t.lastSeen > FORGET_MS || !passesFilter(t.id)) {   // dataminimering: glem gamle/ikke-fulgte enheder
      t.used = false;
      continue;
    }

    Point pos[MAX_ANCHORS];
    float dist[MAX_ANCHORS];
    int   used[MAX_ANCHORS];                         // hvilke pladser i anchors[] der er med
    int   count = 0;
    for (int s = 0; s < MAX_ANCHORS; s++) {
      if (!anchors[s].used || t.seenAt[s] == 0 || now - t.seenAt[s] > STALE_MS) continue;
      pos[count]  = anchors[s].pos;
      dist[count] = rssiToDistance(t.rssi[s], rssiAt1m, pathLossN);
      used[count] = s;
      count++;
    }
    Point p;
    bool   located = count >= MIN_ANCHORS && estimate(pos, dist, count, boxMin, boxMax, p);
    String hex     = idToHex(t.id);

    // watch: RSSI og afstand fra hvert anker. Vises også med kun ét anker (bruges til kalibrering)
    if (watchedCount > 0 && count > 0 && !consoleBusy()) {
      Serial.printf("%s ", hex.c_str() + hex.length() - 4);
      for (int i = 0; i < count; i++)
        Serial.printf(" A%d %4.0f dBm %4.1f m |", anchors[used[i]].id, t.rssi[used[i]], dist[i]);
      if (located) Serial.printf("  -> (%.2f, %.2f)\n", p.x, p.y);
      else         Serial.printf("  (need %d anchors for a position)\n", MIN_ANCHORS);
    }
    if (!located) continue;

    time_t ts = time(nullptr);
    if (ts < 1700000000) {                           // tiden er ikke hentet (NTP) endnu: ingen 1970-data
      if (!consoleBusy()) Serial.println("No valid time yet - position not published");
      return;
    }
    char clock[32];
    timeText(clock, sizeof(clock), ts);
    char json[160];
    snprintf(json, sizeof(json), "{\"id\":\"%s\",\"time\":\"%s\",\"ts\":%ld,\"x\":%.2f,\"y\":%.2f,\"anchors\":%d}",
             hex.c_str(), clock, (long)ts, p.x, p.y, count);
    if (mqtt.connected()) mqtt.publish(topicPosition.c_str(), json);
    if (!consoleBusy() && watchedCount == 0) Serial.println(json);   // MQTT får den altid, skærmen kun når ingen skriver
  }
}

void printAnchors() {
  int n = 0;
  Serial.print("Anchors:");
  for (Anchor& a : anchors)
    if (a.used) { Serial.printf(" A%d", a.id); n++; }
  Serial.printf("  (%d of %d)\n", n, ringSize);
  if (n < MIN_ANCHORS) Serial.printf("Need at least %d anchors for a position\n", MIN_ANCHORS);
}

void printLayout() {
  Serial.printf("Layout: %d anchors, %.2f m apart (saved on this board)\n", ringSize, spacing);
  Serial.printf("Calibration: RSSI at 1 m = %.1f dBm, n = %.2f\n", rssiAt1m, pathLossN);
  if (watchedCount == 0) Serial.println("Watching: all devices");
  else                   Serial.printf("Watching: %d devices (only these are tracked)\n", watchedCount);
}

// "devices": alle enheder lige nu, med RSSI fra hvert anker. Bruges til at finde sin egen mobil.
void printDevices() {
  unsigned long now = millis();
  int shown = 0;
  for (Tracked& t : tracked) {
    if (!t.used) continue;
    Serial.printf("  %s ", idToHex(t.id).c_str());
    for (int s = 0; s < MAX_ANCHORS; s++) {
      if (!anchors[s].used) continue;
      if (t.seenAt[s] == 0 || now - t.seenAt[s] > STALE_MS) Serial.printf(" A%d   -- |", anchors[s].id);
      else                                                 Serial.printf(" A%d %4.0f |", anchors[s].id, t.rssi[s]);
    }
    Serial.printf("  last seen %lu s ago\n", (now - t.lastSeen) / 1000);
    shown++;
  }
  // Med watch-liste: vis også de andre enheder, der er set (kun id, ingen målinger), så de kan vælges
  if (watchedCount) {
    for (Recent& r : recent) {
      if (!recentlySeen(r) || watchedIndex(r.id) >= 0) continue;
      Serial.printf("  %s  not watched (watch %s to add it)\n", idToHex(r.id).c_str(),
                    idToHex(r.id).c_str() + DEVICE_ID_BYTES * 2 - 4);
      shown++;
    }
  }
  if (!shown) Serial.println("  No devices right now. Is the phone on the WiFi with the screen on?");
}

void printWatched() {
  if (watchedCount == 0) {
    Serial.println("Watching: all devices");
    return;
  }
  Serial.printf("Watching only (%d of max %d):\n", watchedCount, MAX_WATCHED);
  for (int i = 0; i < watchedCount; i++) Serial.printf("  %s\n", idToHex(watched[i]).c_str());
}

bool isHex(const char* s) {
  for (; *s; s++) if (!isxdigit((unsigned char)*s)) return false;
  return true;
}

// Finder et fuldt id ud fra det, der er skrevet:
//   en MAC (aa:bb:cc:dd:ee:ff, aa-bb-.. eller aabbccddeeff) → samme hash som ankeret laver
//   et helt id (16 tegn)
//   slutningen af et id (3-11 eller 13-15 tegn, fx db8) → søges blandt enheder set for nylig
bool resolveDevice(const char* text, uint8_t* out) {
  unsigned int b[6];
  int used = 0;
  if ((sscanf(text, "%2x:%2x:%2x:%2x:%2x:%2x%n", &b[0], &b[1], &b[2], &b[3], &b[4], &b[5], &used) == 6 ||
       sscanf(text, "%2x-%2x-%2x-%2x-%2x-%2x%n", &b[0], &b[1], &b[2], &b[3], &b[4], &b[5], &used) == 6) &&
      text[used] == '\0') {
    uint8_t mac[6];
    for (int i = 0; i < 6; i++) mac[i] = b[i];
    deviceIdFromMac(mac, out);
    memset(mac, 0, sizeof(mac));
    return true;
  }

  size_t len = strlen(text);
  if (len == 12 && isHex(text)) {                    // MAC uden skilletegn (aabbccddeeff)
    uint8_t mac[6];
    for (int i = 0; i < 6; i++) {
      unsigned int v;
      sscanf(text + i * 2, "%2x", &v);
      mac[i] = v;
    }
    deviceIdFromMac(mac, out);
    memset(mac, 0, sizeof(mac));
    return true;
  }
  if (len < 3 || len > DEVICE_ID_BYTES * 2 || !isHex(text)) {
    Serial.println("Write a MAC (aa:bb:cc:dd:ee:ff) or the end of a device id (at least 3 characters, see: devices)");
    return false;
  }
  if (len == DEVICE_ID_BYTES * 2) {                  // helt id
    for (int i = 0; i < DEVICE_ID_BYTES; i++) {
      unsigned int v;
      sscanf(text + i * 2, "%2x", &v);
      out[i] = v;
    }
    return true;
  }

  // Slutning af et id: skal passe på præcis én enhed (på listen, fulgt nu eller set for nylig)
  const int MAX_MATCHES = 8;
  uint8_t   found[MAX_MATCHES][DEVICE_ID_BYTES];
  int       matches = 0;
  auto consider = [&](const uint8_t* id) {
    if (!idToHex(id).endsWith(text)) return;
    for (int i = 0; i < matches && i < MAX_MATCHES; i++)
      if (memcmp(found[i], id, DEVICE_ID_BYTES) == 0) return;          // samme enhed to steder
    if (matches < MAX_MATCHES) memcpy(found[matches], id, DEVICE_ID_BYTES);
    matches++;
  };
  for (int i = 0; i < watchedCount; i++) consider(watched[i]);
  for (Tracked& t : tracked) if (t.used) consider(t.id);
  for (Recent& r : recent) if (recentlySeen(r)) consider(r.id);

  if (matches == 1) {
    memcpy(out, found[0], DEVICE_ID_BYTES);
    return true;
  }
  if (matches == 0) Serial.printf("No device ends with %s right now (see: devices)\n", text);
  else              Serial.printf("%d devices end with %s - write more characters\n", matches, text);
  return false;
}

void printList() {
  printLayout();
  for (int id = 1; id <= ringSize; id++) {           // hvor hvert anker skal stå, og om det er med
    Point p = ringPosition(id, ringSize, spacing);
    Anchor* found = nullptr;
    for (Anchor& a : anchors) if (a.used && a.id == id) found = &a;
    if (!found)            Serial.printf("  A%d  (%.2f, %.2f)  missing\n", id, p.x, p.y);
    else if (found->ownPos) Serial.printf("  A%d  (%.2f, %.2f)  online, own position\n", id, found->pos.x, found->pos.y);
    else                   Serial.printf("  A%d  (%.2f, %.2f)  online\n", id, p.x, p.y);
  }
  for (Anchor& a : anchors)                          // ankre uden for ringen (højere nummer)
    if (a.used && a.id > ringSize)
      Serial.printf("  A%d  (%.2f, %.2f)  online, %s\n", a.id, a.pos.x, a.pos.y,
                    a.ownPos ? "own position" : "number higher than anchors!");
}

void printHelp() {
  Serial.println("Gateway:");
  Serial.println("  anchors 4     = number of anchors");
  Serial.println("  spacing 4     = meters between neighbour anchors");
  Serial.println("  calib -55 3   = RSSI at 1 m and n (calibration)");
  Serial.println("  list          = anchors and settings");
  Serial.println("  devices       = devices seen now");
  Serial.println("  watch         = show the watch list");
  Serial.println("  watch db8     = only track this device (part of id, full id or MAC)");
  Serial.println("  unwatch db8   = stop watching this device");
  Serial.println("  unwatch       = stop watching all (track everyone)");
}

// true, hvis linjen starter med ordet efterfulgt af et mellemrum (så "watchdb8" ikke tæller som "watch db8")
bool isCommand(const char* line, const char* word) {
  size_t len = strlen(word);
  return strncmp(line, word, len) == 0 && line[len] == ' ';
}

bool command(const char* line) {
  int   n;
  float m;
  int   used = 0;                                    // %n: hele linjen skal passe, fx ikke "anchors 5x"
  float e;
  if (strcmp(line, "list") == 0) {
    printList();
    return true;
  }
  if (strcmp(line, "devices") == 0) {
    printDevices();
    return true;
  }
  // ----- watch / unwatch -----
  char    arg[24];
  uint8_t id[DEVICE_ID_BYTES];
  if (strcmp(line, "watch") == 0) {
    printWatched();
    return true;
  }
  if (strcmp(line, "unwatch") == 0 || strcmp(line, "unwatch all") == 0) {   // uden navn = alle
    watchedCount = 0;
    saveFilter();
    printWatched();
    return true;
  }
  if (isCommand(line, "watch") && sscanf(line, "watch %23s%n", arg, &used) == 1 && line[used] == '\0') {
    if (!resolveDevice(arg, id)) return true;
    if (watchedIndex(id) >= 0) {
      Serial.printf("Already watching %s\n", idToHex(id).c_str());
      return true;
    }
    if (watchedCount >= MAX_WATCHED) {
      Serial.printf("Max %d devices. Unwatch one first\n", MAX_WATCHED);
      return true;
    }
    memcpy(watched[watchedCount++], id, DEVICE_ID_BYTES);
    for (Tracked& t : tracked) if (t.used && watchedIndex(t.id) < 0) t.used = false;   // glem de andre nu
    saveFilter();
    printWatched();
    return true;
  }
  if (isCommand(line, "unwatch") && sscanf(line, "unwatch %23s%n", arg, &used) == 1 && line[used] == '\0') {
    if (!resolveDevice(arg, id)) return true;
    int i = watchedIndex(id);
    if (i < 0) {
      Serial.printf("Not watching %s\n", idToHex(id).c_str());
      return true;
    }
    memmove(watched[i], watched[i + 1], (watchedCount - i - 1) * DEVICE_ID_BYTES);
    watchedCount--;
    if (watchedCount > 0)                            // ikke fulgt mere: glem dens målinger med det samme
      for (Tracked& t : tracked) if (t.used && memcmp(t.id, id, DEVICE_ID_BYTES) == 0) t.used = false;
    saveFilter();
    printWatched();
    return true;
  }
  if (isCommand(line, "calib") && sscanf(line, "calib %f %f%n", &m, &e, &used) == 2 && line[used] == '\0') {
    if (!validRssiAt1m(m) || !validPathLoss(e)) {
      Serial.println("Use: calib <RSSI at 1 m, -100 to -10> <n, 1 to 6>, e.g. calib -55 3");
      return true;
    }
    rssiAt1m  = m;
    pathLossN = e;
    layoutStore.putFloat("rssi1m", m);
    layoutStore.putFloat("n", e);
    Serial.println("Saved.");
    printLayout();
    return true;
  }
  if (isCommand(line, "anchors") && sscanf(line, "anchors %d%n", &n, &used) == 1 && line[used] == '\0') {
    if (!validRingSize(n)) {
      int maxN = ESPNOW_ENCRYPT ? MAX_ENCRYPTED_ANCHORS : MAX_ANCHORS;
      Serial.printf("Anchors must be %d-%d%s\n", MIN_ANCHORS, maxN,
                    ESPNOW_ENCRYPT ? " (encryption limit, see ESPNOW_ENCRYPT in config.h)" : "");
      return true;
    }
    ringSize = n;
    layoutStore.putInt("anchors", n);
  }
  else if (isCommand(line, "spacing") && sscanf(line, "spacing %f%n", &m, &used) == 1 && line[used] == '\0') {
    if (!validSpacing(m)) {
      Serial.println("Spacing must be 0.1-100 m");
      return true;
    }
    spacing = m;
    layoutStore.putFloat("spacing", m);
  }
  else return false;

  applyLayout();
  Serial.println("Saved.");
  printList();
  return true;
}

// ---------- Program ----------
void setup() {
  Serial.println("Role: gateway");
  loadLayout();
  printLayout();
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
    dropSilentAnchors();
    computePositions();
  }

  static unsigned long lastStatus = 0;
  if (millis() - lastStatus >= 10000) {
    lastStatus = millis();
    if (!consoleBusy()) printAnchors();
  }
  delay(10);
}

}  // namespace gateway
