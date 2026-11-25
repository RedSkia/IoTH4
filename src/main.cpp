#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include "secrets.h"   // WIFI_SSID, WIFI_PASSWORD, MQTT_USER, MQTT_PASSWORD
#include "ca_cert.h"   // CA_CERT

// ---------- Indstillinger ----------
const char* MQTT_HOST = "wilsons.local";
const int   MQTT_PORT = 8883;                       // TLS + login
const char* TIDSZONE  = "CET-1CEST,M3.5.0,M10.5.0/3";

const int KNAP[4] = {34, 35, 32, 33};               // oeverst -> nederst
const int LED[4]  = {23, 22, 21, 19};

const unsigned long LED_MS    = 2000;               // LED lyser efter tryk
const unsigned long VAAGEN_MS = 10000;              // sov efter 10 s uden tryk
const uint64_t      SOVE_S    = 3600;               // vaagn selv hver time

WiFiClientSecure net;
PubSubClient     mqtt(net);
String           topic;

unsigned long sidsteTryk;
unsigned long ledTid[4];
bool          var[4];                               // knappens forrige tilstand

// ---------- Soevn ----------
void sov() {
  Serial.println("Sover...");
  uint64_t maske = 0;
  for (int i = 0; i < 4; i++) maske |= 1ULL << KNAP[i];
  esp_sleep_enable_ext1_wakeup(maske, ESP_EXT1_WAKEUP_ANY_HIGH);
  esp_sleep_enable_timer_wakeup(SOVE_S * 1000000ULL);
  esp_deep_sleep_start();
}

int vaekkendeKnap() {                               // -1 = ikke vaekket af knap
  uint64_t maske = esp_sleep_get_ext1_wakeup_status();
  for (int i = 0; i < 4; i++)
    if (maske & (1ULL << KNAP[i])) return i;
  return -1;
}

// ---------- Netvaerk ----------
void forbind() {
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  for (int i = 0; WiFi.status() != WL_CONNECTED; i++) {
    if (i > 30) sov();                              // 15 s uden wifi
    delay(500);
  }

  configTzTime(TIDSZONE, "pool.ntp.org");           // tid skal kendes foer TLS
  struct tm t;
  if (!getLocalTime(&t, 10000)) sov();

  net.setCACert(CA_CERT);
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  topic = String("elever/") + MQTT_USER + "/smiley";

  String id = String(MQTT_USER) + "-" + String((uint32_t)ESP.getEfuseMac(), HEX);
  if (!mqtt.connect(id.c_str(), MQTT_USER, MQTT_PASSWORD)) {
    Serial.printf("MQTT fejl %d\n", mqtt.state());  // 4/5 = login, -2 = forbindelse
    sov();
  }
  Serial.println("Forbundet");
}

void send(const char* besked) {
  mqtt.publish(topic.c_str(), besked);
  Serial.println(besked);
}

void tryk(int i) {
  digitalWrite(LED[i], HIGH);
  ledTid[i]  = millis();
  sidsteTryk = millis();

  char besked[40];
  snprintf(besked, sizeof(besked), "{\"knap\":%d,\"ts\":%ld}", i + 1, (long)time(nullptr));
  send(besked);
}

// ---------- Program ----------
void setup() {
  Serial.begin(115200);
  for (int i = 0; i < 4; i++) {
    pinMode(KNAP[i], INPUT);
    pinMode(LED[i], OUTPUT);
  }

  int knap = vaekkendeKnap();
  if (knap >= 0) digitalWrite(LED[knap], HIGH);     // LED taendes med det samme

  forbind();

  if (knap >= 0) tryk(knap);
  else           send("{\"status\":\"vaagen\"}");

  for (int i = 0; i < 4; i++) var[i] = digitalRead(KNAP[i]);
  sidsteTryk = millis();
}

void loop() {
  mqtt.loop();

  for (int i = 0; i < 4; i++) {
    bool er = digitalRead(KNAP[i]);
    if (er && !var[i]) tryk(i);                     // nyt tryk
    var[i] = er;
    if (millis() - ledTid[i] > LED_MS) digitalWrite(LED[i], LOW);
  }

  if (millis() - sidsteTryk > VAAGEN_MS) sov();
  delay(20);
}
