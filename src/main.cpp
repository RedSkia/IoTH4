#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include "secrets.h"   // WIFI_SSID, WIFI_PASSWORD, MQTT_USER, MQTT_PASSWORD
#include "ca_cert.h"   // CA_CERT

// ---------- Indstillinger ----------
const char* MQTT_HOST = "wilsons.local";
const int   MQTT_PORT = 8883;                       // TLS + login
const char* TIMEZONE  = "CET-1CEST,M3.5.0,M10.5.0/3";  // dansk tid

const int BUTTON[4] = {34, 35, 32, 33};             // oeverst -> nederst
const int LED[4]    = {23, 22, 21, 19};

const unsigned long LED_MS   = 2000;                // LED lyser efter tryk
const unsigned long AWAKE_MS = 10000;               // sov efter 10 s uden tryk
const uint64_t      SLEEP_S  = 3600;                // vaagn selv hver time

WiFiClientSecure net;
PubSubClient     mqtt(net);
String           topic;                             // elever/<bruger>/smiley

unsigned long lastPress;
unsigned long ledOnAt[4];
bool          wasPressed[4];                        // knappens forrige tilstand

// ---------- Soevn ----------
void sleep() {
  Serial.println("Sleeping...");
  uint64_t mask = 0;
  for (int i = 0; i < 4; i++) mask |= 1ULL << BUTTON[i];
  esp_sleep_enable_ext1_wakeup(mask, ESP_EXT1_WAKEUP_ANY_HIGH);  // vaagn paa knap
  esp_sleep_enable_timer_wakeup(SLEEP_S * 1000000ULL);           // vaagn paa tid
  esp_deep_sleep_start();
}

int wakeButton() {                                  // -1 = ikke vaekket af knap
  uint64_t mask = esp_sleep_get_ext1_wakeup_status();
  for (int i = 0; i < 4; i++)
    if (mask & (1ULL << BUTTON[i])) return i;
  return -1;
}

// ---------- Netvaerk ----------
void connect() {
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  for (int i = 0; WiFi.status() != WL_CONNECTED; i++) {
    if (i > 30) sleep();                            // 15 s uden wifi: sov
    delay(500);
  }

  configTzTime(TIMEZONE, "pool.ntp.org");           // tiden skal kendes foer TLS
  struct tm now;
  if (!getLocalTime(&now, 10000)) sleep();

  net.setCACert(CA_CERT);
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  topic = String("elever/") + MQTT_USER + "/smiley";

  String clientId = String(MQTT_USER) + "-" + String((uint32_t)ESP.getEfuseMac(), HEX);
  if (!mqtt.connect(clientId.c_str(), MQTT_USER, MQTT_PASSWORD)) {
    Serial.printf("MQTT error %d\n", mqtt.state());  // 4/5 = login, -2 = forbindelse
    sleep();
  }
  Serial.println("Connected");
}

void publish(const char* message) {
  mqtt.publish(topic.c_str(), message);
  Serial.println(message);
}

void onPress(int i) {
  digitalWrite(LED[i], HIGH);
  ledOnAt[i] = millis();
  lastPress  = millis();

  char message[40];
  snprintf(message, sizeof(message), "{\"button\":%d,\"ts\":%ld}", i + 1, (long)time(nullptr));
  publish(message);
}

// ---------- Program ----------
void setup() {
  Serial.begin(115200);
  for (int i = 0; i < 4; i++) {
    pinMode(BUTTON[i], INPUT);                      // 10k pulldown sidder paa boardet
    pinMode(LED[i], OUTPUT);
  }

  int button = wakeButton();
  if (button >= 0) digitalWrite(LED[button], HIGH); // LED taendes med det samme

  connect();

  if (button >= 0) onPress(button);
  else             publish("{\"status\":\"awake\"}");

  for (int i = 0; i < 4; i++) wasPressed[i] = digitalRead(BUTTON[i]);
  lastPress = millis();
}

void loop() {
  mqtt.loop();

  for (int i = 0; i < 4; i++) {
    bool pressed = digitalRead(BUTTON[i]);
    if (pressed && !wasPressed[i]) onPress(i);      // nyt tryk
    wasPressed[i] = pressed;
    if (millis() - ledOnAt[i] > LED_MS) digitalWrite(LED[i], LOW);
  }

  if (millis() - lastPress > AWAKE_MS) sleep();
  delay(20);
}
