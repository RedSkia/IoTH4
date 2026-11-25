// Simuleringstest af src/main.cpp på computeren (ingen ESP32 nødvendig).
// Kør fra projektmappen:
//   g++ -std=c++17 -I simtest/mock simtest/sim.cpp -o sim && ./sim
#include "mock/Arduino.h"
Sim sim;
#include "../src/main.cpp"                               // det rigtige program

// Knapper og LED'er på boardet
const int BLUE = 34, GREEN = 35, YELLOW = 32, RED = 33;
const int LED_BLUE = 23, LED_RED = 19;

// Opvågningsårsag
const int FRESH = 0, EXT1 = 3, TIMER = 4;
uint64_t bit(int pin) { return 1ULL << pin; }

// Kør programmet fra opvågning til søvn, eller til tiden 'limit' (ms) er brugt
void run(int wakeCause, uint64_t ext1, std::vector<Press> presses, unsigned long limit = 40000) {
  sim = Sim();
  sim.wakeCause = wakeCause;
  sim.ext1 = ext1;
  sim.presses = presses;
  sim.limit = limit;
  lockedUntil = ledOffAt = sleepAt = lastHeld = 0;    // nulstilles ved hver opvågning (som på ESP32)
  try {
    setup();
    while (true) loop();
  } catch (SleepNow&) {
  } catch (TimeUp&) {}
}

// ---------- Hjælpefunktioner ----------
int buttonMsgs() {
  int n = 0;
  for (auto& m : sim.sent) if (m.second.find("button") != std::string::npos) n++;
  return n;
}

bool firstIs(int button) {
  std::string want = "\"button\":" + std::to_string(button);
  return !sim.sent.empty() && sim.sent[0].second.find(want) != std::string::npos;
}

bool hasLog(const std::string& text) {
  for (auto& line : sim.log) if (line.find(text) != std::string::npos) return true;
  return false;
}

int fails = 0;
void check(const char* name, const char* expect, bool ok) {
  printf("| %-44s | %-40s | %-4s |\n", name, expect, ok ? "OK" : "FEJL");
  if (!ok) fails++;
}

// ---------- Tests ----------
int main() {
  printf("LOCK_WHILE_LED = %s\n\n", LOCK_WHILE_LED ? "true" : "false");
  printf("| %-44s | %-40s | %-4s |\n", "Test", "Forventet", "");
  printf("|%s|%s|%s|\n", std::string(46, '-').c_str(), std::string(42, '-').c_str(), std::string(6, '-').c_str());

  // Opstart og selvtest
  run(FRESH, 0, {});
  check("Opstart: selvtest uden fejl", "\"Self test OK\", status sendt, sover",
        hasLog("Self test OK") && sim.sent.size() == 1 && sim.slept);

  run(FRESH, 0, {{YELLOW, 0, 999999}}, 5000);
  check("Opstart: knap 3 sidder fast (mangler 10k)", "FAIL, intet sendt, sover ikke",
        hasLog("FAIL: button 3") && sim.sent.empty() && !sim.slept);

  run(TIMER, 0, {});
  check("Timer-opvågning (hver time)", "kun status sendt, sover igen",
        sim.sent.size() == 1 && buttonMsgs() == 0 && sim.slept);

  // Opvågning på knap
  run(EXT1, bit(BLUE), {{BLUE, 0, 300}});
  check("Vækket af blå knap", "1 besked (knap 1), sover igen",
        buttonMsgs() == 1 && firstIs(1) && sim.slept);

  run(EXT1, bit(BLUE), {{BLUE, 0, 15000}});
  check("Blå holdes nede i 15 s", "1 besked, sover først efter slip",
        buttonMsgs() == 1 && sim.slept && sim.ms >= 15000);

  run(EXT1, bit(RED), {{RED, 0, 200}, {BLUE, 300, 500}});
  check("Vækket af rød, blå trykkes under opstart", "kun 1 besked (knap 4)",
        buttonMsgs() == 1 && firstIs(4));

  // Tryk mens den er vågen (vågen fra ca. 2 s)
  std::vector<Press> spam;
  for (unsigned long t = 3000; t < 9000; t += 150) spam.push_back({BLUE, t, t + 80});
  run(TIMER, 0, spam);
  if (LOCK_WHILE_LED) check("Spam: blå 40 gange på 6 s", "maks 1 besked pr. LED-tid", buttonMsgs() >= 2 && buttonMsgs() <= 7);
  else                check("Spam: blå 40 gange på 6 s", "40 beskeder (spam tilladt)", buttonMsgs() == 40);

  run(TIMER, 0, {{BLUE, 3000, 3100}, {RED, 3300, 3400}});
  if (LOCK_WHILE_LED) check("Blå, så rød efter 0,3 s", "1 besked (låst)", buttonMsgs() == 1);
  else                check("Blå, så rød efter 0,3 s", "2 beskeder", buttonMsgs() == 2);

  run(TIMER, 0, {{BLUE, 3000, 3100}, {RED, 5500, 5600}});
  check("Blå, så rød efter 2,5 s", "2 beskeder", buttonMsgs() == 2);

  run(TIMER, 0, {{BLUE, 3000, 3200}, {RED, 3000, 3200}});
  check("Blå og rød præcis samtidig", "1 besked", buttonMsgs() == 1);

  run(TIMER, 0, {{BLUE, 3000, 9000}, {RED, 6000, 6200}});
  check("Rød trykkes mens blå holdes", "1 besked (rød ignoreres)", buttonMsgs() == 1);

  std::vector<Press> bounce;
  for (unsigned long t = 3000; t < 3020; t += 4) bounce.push_back({BLUE, t, t + 2});  // prel ved tryk
  bounce.push_back({BLUE, 3020, 3200});
  bounce.push_back({BLUE, 3202, 3204});                                                // prel ved slip
  run(TIMER, 0, bounce);
  check("Knappen preller ved tryk og slip", "1 besked", buttonMsgs() == 1);

  // LED'er
  run(TIMER, 0, {{BLUE, 3000, 3100}, {RED, 3300, 3400}}, 3500);
  if (LOCK_WHILE_LED) check("Rød trykkes mens blå LED lyser", "blå bliver, rød ignoreres",
                            sim.led[LED_BLUE] == 1 && sim.led[LED_RED] == 0);
  else                check("Rød trykkes mens blå LED lyser", "LED skifter: rød tændt, blå slukket",
                            sim.led[LED_RED] == 1 && sim.led[LED_BLUE] == 0);

  run(TIMER, 0, {{BLUE, 3000, 3100}}, 3900);
  bool onAt900 = sim.led[LED_BLUE] == 1;
  run(TIMER, 0, {{BLUE, 3000, 3100}}, 4100);
  bool offAt1100 = sim.led[LED_BLUE] == 0;
  check("LED lyser 1 s og slukker så", "tændt ved 0,9 s, slukket ved 1,1 s", onAt900 && offAt1100);

  printf("\n%s (%d fejl)\n", fails ? "TEST FEJLEDE" : "ALLE TESTS OK", fails);
  return fails ? 1 : 0;
}
