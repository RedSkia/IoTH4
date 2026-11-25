#include <Arduino.h>

// Knapper: 3V3 -> knap -> GPIO, og 10k fra GPIO til GND (SKAL vaere der, isaer paa D34/D35)
// Oeverst til nederst paa dit board: blaa=D34, groen=D35, gul=D32, orange=D33
const int BTN[4] = {34, 35, 32, 33};
// LED'er: GPIO -> 220 ohm -> LED langt ben, kort ben -> GND
const int LED[4] = {23, 22, 21, 19};

void setup() {
  Serial.begin(115200);
  for (int i = 0; i < 4; i++) {
    pinMode(BTN[i], INPUT);
    pinMode(LED[i], OUTPUT);
  }
  // Test 1: hver LED lyser kort, 1 til 4
  for (int i = 0; i < 4; i++) {
    Serial.printf("LED %d (GPIO %d) taendt\n", i + 1, LED[i]);
    digitalWrite(LED[i], HIGH);
    delay(600);
    digitalWrite(LED[i], LOW);
  }
  Serial.println("Test 2: tryk paa knapperne");
}

void loop() {
  // Test 2: knap taender sin egen LED
  for (int i = 0; i < 4; i++) {
    bool trykket = digitalRead(BTN[i]) == HIGH;
    digitalWrite(LED[i], trykket);
    if (trykket) Serial.printf("Knap %d (GPIO %d) trykket\n", i + 1, BTN[i]);
  }
  delay(50);
}
