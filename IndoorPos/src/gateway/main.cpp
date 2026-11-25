// ===== Gateway =====
// Modtager RSSI fra ankrene (ESP-NOW), beregner hver enheds position (x, y)
// og sender id, tid og koordinat til MQTT med TLS og login.

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ESP32_NOW.h>
#include "config.h"
#include "protocol.h"
#include "positioning.h"
#include "secrets.h"
#include "ca_cert.h"

// ---------- Modtagelse fra ankrene ----------
struct Received {
  int         anchorIndex;                           // 0..ANCHOR_COUNT-1, ud fra afsenderens MAC
  Measurement m;
};

QueueHandle_t inbox;                                 // ESP-NOW-tråden lægger ind, loop() tager ud

class AnchorPeer : public ESP_NOW_Peer {
public:
  AnchorPeer(int index) : ESP_NOW_Peer(ANCHOR_MAC[index], 0, WIFI_IF_STA, ESPNOW_LMK), index(index) {}
  bool begin() { return add(); }
  void onReceive(const uint8_t* data, size_t len, bool broadcast) override {
    if (broadcast || len != sizeof(Measurement)) return;
    Received r;
    r.anchorIndex = index;
    memcpy(&r.m, data, sizeof(Measurement));
    if (r.m.version != PROTOCOL_VERSION) return;
    xQueueSend(inbox, &r, 0);                        // fuld kø: målingen droppes
  }
private:
  int index;
};

AnchorPeer* anchors[ANCHOR_COUNT];

// ---------- Enheder vi kender lige nu ----------
const int MAX_TRACKED = 64;

struct Tracked {
  bool          used;
  uint8_t       id[DEVICE_ID_BYTES];
  int8_t        rssi[ANCHOR_COUNT];
  unsigned long seenAt[ANCHOR_COUNT];                // 0 = aldrig set af det anker
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

// ---------- Beregning ----------
void handle(const Received& r) {
  Tracked* t = findOrAdd(r.m.deviceId);
  t->rssi[r.anchorIndex]   = r.m.rssi;
  t->seenAt[r.anchorIndex] = millis();
  t->lastSeen              = millis();

  if (PUBLISH_RAW && mqtt.connected()) {             // til kalibrering
    char json[96];
    snprintf(json, sizeof(json), "{\"anchor\":%d,\"id\":\"%s\",\"rssi\":%d,\"samples\":%d}",
             r.anchorIndex + 1, idToHex(r.m.deviceId).c_str(), r.m.rssi, r.m.samples);
    mqtt.publish(topicRaw.c_str(), json);
  }
}

void computePositions() {
  unsigned long now = millis();
  for (Tracked& t : tracked) {
    if (!t.used) continue;
    if (now - t.lastSeen > FORGET_MS) {              // dataminimering: glem gamle enheder
      t.used = false;
      continue;
    }

    Point pos[ANCHOR_COUNT];
    float dist[ANCHOR_COUNT];
    int   count = 0;
    for (int a = 0; a < ANCHOR_COUNT; a++) {
      if (t.seenAt[a] == 0 || now - t.seenAt[a] > STALE_MS) continue;
      pos[count]  = ANCHOR_POS[a];
      dist[count] = rssiToDistance(t.rssi[a], RSSI_AT_1M, PATH_LOSS_EXPONENT);
      count++;
    }
    if (count < MIN_ANCHORS) continue;

    Point p;
    if (!trilaterate(pos, dist, count, p)) continue;
    p = clampToArea(p, AREA_WIDTH, AREA_HEIGHT);

    time_t ts = time(nullptr);
    char clock[32];
    timeText(clock, sizeof(clock), ts);
    char json[160];
    snprintf(json, sizeof(json), "{\"id\":\"%s\",\"time\":\"%s\",\"ts\":%ld,\"x\":%.2f,\"y\":%.2f,\"anchors\":%d}",
             idToHex(t.id).c_str(), clock, (long)ts, p.x, p.y, count);
    if (mqtt.connected()) mqtt.publish(topicPosition.c_str(), json);
    Serial.println(json);
  }
}

// ---------- Program ----------
void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  inbox = xQueueCreate(64, sizeof(Received));

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.printf("\nGateway\nMy MAC: %s\n", WiFi.macAddress().c_str());   // skriv den ind i config.h (GATEWAY_MAC)
  while (WiFi.status() != WL_CONNECTED) delay(250);
  Serial.printf("WiFi OK, channel %d\n", WiFi.channel());

  configTzTime(TIMEZONE, "pool.ntp.org", WiFi.gatewayIP().toString().c_str());
  struct tm now;
  if (getLocalTime(&now, 10000)) Serial.println(&now, "Time OK: %Y-%m-%d %H:%M:%S");
  else                           Serial.println("Time error (no NTP server)");

  net.setCACert(CA_CERT);
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  topicPosition = String("elever/") + MQTT_USER + "/position";
  topicRaw      = String("elever/") + MQTT_USER + "/rssi";
  connectMqtt();

  if (!ESP_NOW.begin(ESPNOW_PMK)) Serial.println("ESP-NOW error");
  for (int a = 0; a < ANCHOR_COUNT; a++) {
    anchors[a] = new AnchorPeer(a);
    if (!anchors[a]->begin()) Serial.printf("Anchor A%d not added - check ANCHOR_MAC in config.h\n", a + 1);
  }
  Serial.println("Waiting for anchors");
}

void loop() {
  connectMqtt();
  mqtt.loop();

  Received r;
  while (xQueueReceive(inbox, &r, 0) == pdTRUE) handle(r);

  static unsigned long lastCompute = 0;
  if (millis() - lastCompute >= WINDOW_MS) {
    lastCompute = millis();
    computePositions();
  }
  delay(10);
}
