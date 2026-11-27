// ===== IndoorPos: samme program på alle boards =====
// Skriv i Serial Monitor og tryk Enter:
//   g              → boardet bliver gateway
//   3              → boardet bliver anker 3 (gatewayen regner positionen ud fra opstillingen)
//   3 1.5 2        → anker 3 i en selvvalgt position (x, y i meter), fx til en skæv opstilling
//   reset          → glem rollen og opstillingen
// Kun på gatewayen:
//   anchors 4      → antal ankre i ringen
//   spacing 4      → meter mellem to naboankre
//   calib -55 3    → kalibrering: RSSI ved 1 m og n
//   list           → vis opstillingen og de ankre, der er med
//   devices        → enheder lige nu, med RSSI fra hvert anker
//   watch db8      → følg kun denne enhed (slutning af id eller MAC)
//   unwatch db8    → stop med at følge denne
//   unwatch        → stop med at følge alle (alle spores igen)
// Alt gemmes i boardets flash, så det kun skal gøres én gang.

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
  Serial.println("  reset         = forget role and settings");
  if (role == ROLE_GATEWAY) gateway::printHelp();
}

void saveAndRestart(const char* text) {
  Serial.println(text);
  Serial.println("Restarting...");
  delay(500);
  ESP.restart();
}

void runCommand(String line) {
  line.trim();
  String typed = line;                               // som skrevet, til fejlbeskeden
  line.toLowerCase();
  int   id;
  float x, y;

  if (line == "reset") {
    settings.clear();
    gateway::forgetSettings();
    saveAndRestart("Role and settings forgotten");
  }
  else if (line == "g" || line == "gateway") {
    settings.putUChar("role", ROLE_GATEWAY);
    saveAndRestart("Saved: gateway");
  }
  else if (role == ROLE_GATEWAY && gateway::command(line.c_str())) {
    // klaret af gatewayen
  }
  else {
    // Hele linjen skal passe (%n = antal tegn læst), ellers bliver fx "3x" til anker 3
    const char* s = line.c_str();
    int  used     = 0;
    int  parsed   = 0;
    if (sscanf(s, "%d %f %f%n", &id, &x, &y, &used) == 3 && s[used] == '\0') parsed = 3;
    else if (sscanf(s, "%d%n", &id, &used) == 1 && s[used] == '\0')        parsed = 1;
    if (parsed && id >= 1 && id <= 255 && (parsed == 1 || (isfinite(x) && isfinite(y)))) {
      if (parsed == 1) x = y = NAN;                  // NAN = gatewayen regner positionen ud
      settings.putUChar("role", ROLE_ANCHOR);
      settings.putUChar("id", id);
      settings.putFloat("x", x);
      settings.putFloat("y", y);
      if (parsed == 1) Serial.printf("Saved: anchor A%d (position from config.h)\n", id);
      else             Serial.printf("Saved: anchor A%d at (%.2f, %.2f)\n", id, x, y);
      saveAndRestart("");
    }
    Serial.printf("Unknown command: \"%s\"\n", typed.c_str());
    printHelp();
  }
}

unsigned long lastKeyAt = 0;

bool consoleBusy() {                                 // glemt halvskrevet linje: log kører igen efter 20 s
  return inputLine.length() > 0 && millis() - lastKeyAt < 20000;
}

void handleConsole() {
  static bool escape  = false;                       // midt i en piletast o.l. (ESC [ A)
  static bool tooLong = false;
  while (Serial.available()) {
    int c = Serial.read();
    lastKeyAt = millis();
    if (escape) {                                    // spring resten af tastekoden over
      if (c != '[' && !(c >= '0' && c <= '9') && c != ';') escape = false;
      continue;
    }
    if (c == 27) { escape = true; continue; }
    if (c == '\t') c = ' ';

    if (c == '\r' || c == '\n') {
      if (tooLong)                      Serial.println("\nLine too long - ignored");
      else if (inputLine.length() > 0) { Serial.println(); runCommand(inputLine); }
      inputLine = "";
      tooLong   = false;
    } else if (c == 8 || c == 127) {                 // backspace: slet sidste tegn
      if (inputLine.length() > 0) {
        inputLine.remove(inputLine.length() - 1);
        Serial.print("\b \b");
      }
    } else if (c >= 32 && c <= 126) {                // kun almindelige tegn, støj ignoreres
      if (inputLine.length() >= 40) { tooLong = true; continue; }
      inputLine += (char)c;
      Serial.print((char)c);                         // vis det, der skrives
    }
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  delay(500);
  // Smid støj fra opstarten væk. Sker før hjælpeteksten vises, så intet skrevet efter den går tabt.
  while (Serial.available()) Serial.read();

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
