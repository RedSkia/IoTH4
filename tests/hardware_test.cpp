#include <Arduino.h>

// Test af knapper og LED'er. Kopier til src/main.cpp for at koere den.
const int BUTTON[4] = {34, 35, 32, 33};   // oeverst -> nederst
const int LED[4]    = {23, 22, 21, 19};

void setup() {
  Serial.begin(115200);
  for (int i = 0; i < 4; i++) {
    pinMode(BUTTON[i], INPUT);            // 10k pulldown sidder paa boardet
    pinMode(LED[i], OUTPUT);
  }

  // Test 1: hver LED blinker en gang
  for (int i = 0; i < 4; i++) {
    Serial.printf("LED %d (GPIO %d) on\n", i + 1, LED[i]);
    digitalWrite(LED[i], HIGH);
    delay(600);
    digitalWrite(LED[i], LOW);
  }
  Serial.println("Test 2: press the buttons");
}

void loop() {
  // Test 2: knappen taender sin egen LED
  for (int i = 0; i < 4; i++) {
    bool pressed = digitalRead(BUTTON[i]) == HIGH;
    digitalWrite(LED[i], pressed);
    if (pressed) Serial.printf("Button %d (GPIO %d) pressed\n", i + 1, BUTTON[i]);
  }
  delay(50);
}
