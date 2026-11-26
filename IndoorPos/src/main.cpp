// ===== IndoorPos: samme program på alle boards =====
// Skriv i Serial Monitor og tryk Enter:
//   g              → boardet bliver gateway
//   3              → boardet bliver anker 3 (positionen regnes ud fra ANCHORS og SPACING_M i config.h)
//   3 1.5 2        → anker 3 i en selvvalgt position (x, y i meter), fx til en skæv opstilling
//   reset          → glem rollen
// Valget gemmes i boardets flash, så det kun skal gøres én gang.

#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include <math.h>
#include "config.h"
#include "roles.h"

enum Role : uint8_t { ROLE_NONE = 0, ROLE_GATEWAY = 1, ROLE_ANCHOR = 2 };

Preferences settings;
uint8_t     role = ROLE_NONE;
String      inputLine;

void printHelp() {
  Serial.println("Type a command and press Enter:");
  Serial.println("  g             = gateway");
  Serial.println("  <nr>          = anchor, e.g. 3");
  Serial.println("  <nr> <x> <y>  = anchor at your own position, e.g. 3 1.5 2");
  Serial.println("  reset         = forget role");
}

void saveAndRestart(const char* text) {
  Serial.println(text);
  Serial.println("Restarting...");
  delay(500);
  ESP.restart();
}

void runCommand(String line) {
  line.trim();
  line.toLowerCase();
  int   id;
  float x, y;

  if (line == "reset") {
    settings.clear();
    saveAndRestart("Role forgotten");
  }
  else if (line == "g" || line == "gateway") {
    settings.putUChar("role", ROLE_GATEWAY);
    saveAndRestart("Saved: gateway");
  }
  else {
    int parsed = sscanf(line.c_str(), "%d %f %f", &id, &x, &y);
    if ((parsed == 1 || parsed == 3) && id >= 1 && id <= 255) {
      if (parsed == 1) x = y = NAN;                  // NAN = gatewayen regner positionen ud
      settings.putUChar("role", ROLE_ANCHOR);
      settings.putUChar("id", id);
      settings.putFloat("x", x);
      settings.putFloat("y", y);
      if (parsed == 1) Serial.printf("Saved: anchor A%d (position from config.h)\n", id);
      else             Serial.printf("Saved: anchor A%d at (%.2f, %.2f)\n", id, x, y);
      saveAndRestart("");
    }
    Serial.println("Unknown command.");
    printHelp();
  }
}

void handleConsole() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\r' || c == '\n') {
      if (inputLine.length() > 0) {
        Serial.println();
        runCommand(inputLine);
        inputLine = "";
      }
    } else if (inputLine.length() < 40) {
      inputLine += c;
      Serial.print(c);                               // vis det, der skrives
    }
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  delay(500);

  WiFi.mode(WIFI_STA);
  while (!WiFi.STA.started()) delay(10);
  Serial.printf("\nIndoorPos  MAC %s\n", WiFi.macAddress().c_str());

  settings.begin("indoorpos", false);
  role = settings.getUChar("role", ROLE_NONE);
  if (role != ROLE_GATEWAY && role != ROLE_ANCHOR) role = ROLE_NONE;

  if (role == ROLE_NONE) {
    Serial.println("This board has no role yet.");
    printHelp();
    while (true) {                                   // langsomt blink = venter på rolle
      handleConsole();
      digitalWrite(LED_PIN, (millis() / 1000) % 2);
      delay(20);
    }
  }

  printHelp();
  if (role == ROLE_GATEWAY) gateway::setup();
  else anchor::setup(settings.getUChar("id", 1), settings.getFloat("x", NAN), settings.getFloat("y", NAN));
}

void loop() {
  handleConsole();                                   // rollen kan altid skiftes
  if (role == ROLE_GATEWAY) gateway::loop();
  else                      anchor::loop();
}
