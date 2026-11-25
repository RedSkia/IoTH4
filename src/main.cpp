#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include "secrets.h"   // WIFI_SSID, WIFI_PASSWORD, MQTT_USER, MQTT_PASSWORD
#include "ca_cert.h"   // CA_CERT

// ---------- Indstillinger ----------
const char* MQTT_HOST = "wilsons.local";
const int   MQTT_PORT = 8883;                            // TLS + login
const char* TIMEZONE  = "CET-1CEST,M3.5.0,M10.5.0/3";    // dansk tid, skifter selv sommer/vinter

const int BUTTON[4] = {34, 35, 32, 33};                  // øverst → nederst
const int LED[4]    = {23, 22, 21, 19};

const unsigned long LED_MS      = 2000;                  // LED lyser, og alle knapper er låst (anti-spam)
const unsigned long DEBOUNCE_MS = 50;                    // alle knapper skal være sluppet så længe
const unsigned long AWAKE_MS    = 10000;                 // sov efter 10 s uden tryk
const uint64_t      SLEEP_S     = 3600;                  // vågn selv hver time

WiFiClientSecure net;
PubSubClient     mqtt(net);
String           topic;                                  // elever/<bruger>/smiley

unsigned long lockedUntil = 0;                           // knapperne er låst indtil da
unsigned long sleepAt;                                   // hvornår den går i søvn
unsigned long lastHeld;                                  // sidst en knap var nede

// ---------- Knapper og LED'er ----------
bool anyButtonDown() {
  for (int i = 0; i < 4; i++)
    if (digitalRead(BUTTON[i])) return true;
  return false;
}

void allLeds(bool on) {
  for (int i = 0; i < 4; i++) digitalWrite(LED[i], on);
}

// ---------- Selvtest ----------
// Kører ved upload, reset og strøm til. Ikke når den vågner fra dyb søvn.
bool selfTest() {
  Serial.println("Self test...");

  for (int i = 0; i < 4; i++) {                          // LED'erne lyser én ad gangen, tjek med øjnene
    digitalWrite(LED[i], HIGH);
    delay(300);
    digitalWrite(LED[i], LOW);
  }

  for (int n = 0; n < 20; n++) {                         // knapperne skal være LOW, når ingen trykker
    for (int i = 0; i < 4; i++) {
      if (digitalRead(BUTTON[i])) {
        Serial.printf("FAIL: button %d (GPIO %d) is HIGH - tjek 10k og ledning\n", i + 1, BUTTON[i]);
        return false;
      }
    }
    delay(5);
  }

  Serial.println("Self test OK");
  return true;
}

void showError() {                                       // alle LED'er blinker = hardwarefejl
  while (true) {
    allLeds(true);
    delay(150);
    allLeds(false);
    delay(150);
  }
}

// ---------- Søvn ----------
void goToSleep() {
  Serial.println("Sleeping...");
  Serial.flush();                                        // skriv teksten færdig før søvn
  uint64_t mask = 0;
  for (int i = 0; i < 4; i++) mask |= 1ULL << BUTTON[i];
  esp_sleep_enable_ext1_wakeup(mask, ESP_EXT1_WAKEUP_ANY_HIGH);   // vågn på knap
  esp_sleep_enable_timer_wakeup(SLEEP_S * 1000000ULL);            // vågn på tid
  esp_deep_sleep_start();
}

int wakeButton() {                                       // -1 = ikke vækket af en knap
  uint64_t mask = esp_sleep_get_ext1_wakeup_status();
  for (int i = 0; i < 4; i++)
    if (mask & (1ULL << BUTTON[i])) return i;
  return -1;
}

// ---------- Netværk ----------
void connectAll() {
  Serial.printf("WiFi: %s ...\n", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  for (int i = 0; WiFi.status() != WL_CONNECTED; i++) {
    if (i > 30) {                                        // 15 s uden wifi: sov
      Serial.println("WiFi error (forkert navn/kode?)");
      goToSleep();
    }
    delay(500);
  }
  Serial.printf("WiFi OK, IP %s\n", WiFi.localIP().toString().c_str());

  // Tiden skal kendes før TLS. Routeren prøves også, hvis nettet ikke har internet.
  static String router = WiFi.gatewayIP().toString();
  configTzTime(TIMEZONE, "pool.ntp.org", router.c_str(), "time.google.com");
  struct tm now;
  if (!getLocalTime(&now, 10000)) {
    Serial.println("Time error (ingen NTP-server)");
    goToSleep();
  }
  Serial.println(&now, "Time OK: %Y-%m-%d %H:%M:%S");

  net.setCACert(CA_CERT);
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  topic = String("elever/") + MQTT_USER + "/smiley";

  String clientId = String(MQTT_USER) + "-" + String((uint32_t)ESP.getEfuseMac(), HEX);
  if (!mqtt.connect(clientId.c_str(), MQTT_USER, MQTT_PASSWORD)) {
    Serial.printf("MQTT error %d\n", mqtt.state());      // 4/5 = login, -2 = forbindelse
    goToSleep();
  }
  Serial.println("Connected");
}

void publish(const char* message) {
  mqtt.publish(topic.c_str(), message);
  Serial.println(message);
}

void onPress(int i) {
  allLeds(false);                                        // kun den trykkede LED lyser
  digitalWrite(LED[i], HIGH);
  lockedUntil = millis() + LED_MS;
  sleepAt     = millis() + AWAKE_MS;

  char message[40];
  snprintf(message, sizeof(message), "{\"button\":%d,\"ts\":%ld}", i + 1, (long)time(nullptr));
  publish(message);
}

// ---------- Program ----------
void setup() {
  Serial.begin(115200);
  for (int i = 0; i < 4; i++) {
    pinMode(BUTTON[i], INPUT);                           // 10k pulldown sidder på boardet
    pinMode(LED[i], OUTPUT);
  }

  bool freshStart = esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_UNDEFINED;
  if (freshStart && !selfTest()) showError();            // stop her, hvis hardwaren fejler

  int button = wakeButton();
  if (button >= 0) digitalWrite(LED[button], HIGH);      // LED tændes med det samme

  connectAll();

  if (button >= 0) onPress(button);
  else             publish("{\"status\":\"awake\"}");

  sleepAt  = millis() + AWAKE_MS;
  lastHeld = millis();                                   // knappen der vækkede skal slippes først
}

void loop() {
  mqtt.loop();

  // Et nyt tryk tæller kun, når alle knapper har været sluppet, og låsen er udløbet.
  bool ready = millis() >= lockedUntil && millis() - lastHeld > DEBOUNCE_MS;
  for (int i = 0; i < 4; i++) {
    if (digitalRead(BUTTON[i])) {
      if (ready) onPress(i);
      lastHeld = millis();
      break;                                             // kun én knap ad gangen
    }
  }

  if (millis() >= lockedUntil) allLeds(false);           // låsen udløbet: sluk LED

  if (millis() >= sleepAt && !anyButtonDown()) goToSleep();  // sov ikke, mens en knap holdes
  delay(10);
}
