// Simuleringstest af src/main.cpp på computeren (ingen ESP32 nødvendig).
// Kør:  g++ -std=c++17 -I mock sim.cpp ../src/main.cpp -o sim && ./sim
#include "mock/Arduino.h"
#include <functional>
#include <iostream>

Sim sim;
void setup();
void loop();
extern unsigned long lockedUntil, ledOffAt, sleepAt, lastHeld;     // nulstilles ved hver opvågning (som på ESP32)

// Knapper på boardet: blå=34 (1), grøn=35 (2), gul=32 (3), rød=33 (4)
const int BLUE = 34, GREEN = 35, YELLOW = 32, RED = 33;
const int LED_BLUE = 23, LED_RED = 19;

// Kør programmet fra opvågning til søvn (eller til tiden er brugt)
void run(int wakeCause, uint64_t ext1, std::vector<Press> presses, unsigned long limit = 40000) {
  sim = Sim();
  sim.wakeCause = wakeCause;
  sim.ext1 = ext1;
  sim.presses = presses;
  sim.limit = limit;
  lockedUntil = ledOffAt = sleepAt = lastHeld = 0;
  try {
    setup();
    while (true) loop();
  } catch (SleepNow&) {
  } catch (TimeUp&) {}
}

int buttonMsgs() {
  int n = 0;
  for (auto& m : sim.sent) if (m.second.find("button") != std::string::npos) n++;
  return n;
}
bool hasLog(const std::string& t) {
  for (auto& l : sim.log) if (l.find(t) != std::string::npos) return true;
  return false;
}
unsigned long minGap() {                                 // mindste tid mellem to knap-beskeder
  unsigned long gap = 999999, last = 0; bool first = true;
  for (auto& m : sim.sent) {
    if (m.second.find("button") == std::string::npos) continue;
    if (!first) gap = std::min(gap, m.first - last);
    last = m.first; first = false;
  }
  return gap;
}

const int FRESH = 0, EXT1 = 3, TIMER = 4;
uint64_t bit(int pin) { return 1ULL << pin; }
int fails = 0;

void check(const char* name, const char* expect, bool ok) {
  printf("| %-44s | %-40s | %-4s |\n", name, expect, ok ? "OK" : "FEJL");
  if (!ok) fails++;
}

int main() {
  printf("| %-44s | %-40s | %-4s |\n", "Test", "Forventet", "");
  printf("|%s|%s|%s|\n", std::string(46, '-').c_str(), std::string(42, '-').c_str(), std::string(6, '-').c_str());

  // Opstart og selvtest
  run(FRESH, 0, {});
  check("Opstart: selvtest uden fejl", "\"Self test OK\", status sendt, sover", hasLog("Self test OK") && sim.sent.size() == 1 && sim.slept);

  run(FRESH, 0, {{YELLOW, 0, 999999}}, 5000);
  check("Opstart: knap 3 sidder fast (mangler 10k)", "FAIL, intet sendt, sover ikke", hasLog("FAIL: button 3") && sim.sent.empty() && !sim.slept);

  run(TIMER, 0, {});
  check("Timer-opvågning (hver time)", "kun status sendt, sover igen", sim.sent.size() == 1 && buttonMsgs() == 0 && sim.slept);

  // Opvågning på knap
  run(EXT1, bit(BLUE), {{BLUE, 0, 300}});
  check("Vækket af blå knap", "1 besked (knap 1), sover igen", buttonMsgs() == 1 && sim.sent[0].second.find("\"button\":1") != std::string::npos && sim.slept);

  run(EXT1, bit(BLUE), {{BLUE, 0, 15000}});
  check("Blå holdes nede i 15 s", "1 besked, sover først efter slip", buttonMsgs() == 1 && sim.slept && sim.ms >= 15000);

  run(EXT1, bit(RED), {{RED, 0, 200}, {BLUE, 300, 500}});
  check("Vækket af rød, blå trykkes under opstart", "kun 1 besked (knap 4)", buttonMsgs() == 1 && sim.sent[0].second.find("\"button\":4") != std::string::npos);

  // Tryk mens den er vågen (vågen fra ca. 2,1 s)
  std::vector<Press> spam;
  for (unsigned long t = 3000; t < 9000; t += 150) spam.push_back({BLUE, t, t + 80});
  run(TIMER, 0, spam);
  check("Spam: blå 40 gange på 6 s", "maks 1 besked pr. 0,5 s", buttonMsgs() >= 2 && buttonMsgs() <= 13 && minGap() >= 500);

  run(TIMER, 0, {{BLUE, 3000, 3100}, {RED, 3300, 3400}});
  check("Blå, så rød efter 0,3 s (låst)", "1 besked (rød ignoreres)", buttonMsgs() == 1);

  run(TIMER, 0, {{BLUE, 3000, 3100}, {RED, 3700, 3800}});
  check("Blå, så rød efter 0,7 s (lås udløbet)", "2 beskeder", buttonMsgs() == 2);

  run(TIMER, 0, {{BLUE, 3000, 3100}, {RED, 5500, 5600}});
  check("Blå, så rød efter 2,5 s (LED slukket)", "2 beskeder", buttonMsgs() == 2);

  run(TIMER, 0, {{BLUE, 3000, 3200}, {RED, 3000, 3200}});
  check("Blå og rød præcis samtidig", "1 besked", buttonMsgs() == 1);

  run(TIMER, 0, {{BLUE, 3000, 9000}, {RED, 6000, 6200}});
  check("Rød trykkes mens blå holdes", "1 besked (rød ignoreres)", buttonMsgs() == 1);

  std::vector<Press> bounce;
  for (unsigned long t = 3000; t < 3020; t += 4) bounce.push_back({BLUE, t, t + 2});
  bounce.push_back({BLUE, 3020, 3200});
  bounce.push_back({BLUE, 3202, 3204});                  // prel ved slip
  run(TIMER, 0, bounce);
  check("Knappen preller ved tryk og slip", "1 besked", buttonMsgs() == 1);

  bool onlyBlue = false;
  { sim = Sim(); sim.wakeCause = TIMER; sim.presses = {{BLUE, 3000, 3100}, {RED, 6000, 6100}}; sim.limit = 6500;
    lockedUntil = ledOffAt = sleepAt = lastHeld = 0;
    try { setup(); while (true) loop(); } catch (...) {}
    onlyBlue = sim.led[LED_RED] == 1 && sim.led[LED_BLUE] == 0; }
  check("Kun den trykkede LED lyser", "rød tændt, blå slukket", onlyBlue);

  printf("\n%s (%d fejl)\n", fails ? "TEST FEJLEDE" : "ALLE TESTS OK", fails);
  return fails ? 1 : 0;
}
