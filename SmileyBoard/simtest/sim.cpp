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
bool has(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

int buttonMsgs() {
  int n = 0;
  for (auto& m : sim.sent) if (has(m.second, "\"button\"")) n++;
  return n;
}

bool firstIs(int button) {
  return !sim.sent.empty() && has(sim.sent[0].second, "\"button\":" + std::to_string(button));
}

bool hasLog(const std::string& text) {
  for (auto& line : sim.log) if (has(line, text)) return true;
  return false;
}

int fails = 0;
void check(const std::string& name, const std::string& expect, bool ok) {
  printf("| %-44s | %-40s | %-4s |\n", name.c_str(), expect.c_str(), ok ? "OK" : "FAIL");
  if (!ok) fails++;
}

void skip(const std::string& name, const std::string& why) {
  printf("| %-44s | %-40s | %-4s |\n", name.c_str(), why.c_str(), "-");
}

std::string ms(unsigned long t) { return std::to_string(t) + " ms"; }

// ---------- Tests ----------
int main() {
  printf("LED_MS = %lu, LOCK_WHILE_LED = %s\n\n", LED_MS, LOCK_WHILE_LED ? "true" : "false");
  printf("| %-44s | %-40s | %-4s |\n", "Test", "Expected", "");
  printf("|%s|%s|%s|\n", std::string(46, '-').c_str(), std::string(42, '-').c_str(), std::string(6, '-').c_str());

  // Opstart og selvtest
  run(FRESH, 0, {});
  check("Start: self test passes", "\"Self test OK\", status sent, sleeps",
        hasLog("Self test OK") && sim.sent.size() == 1 && sim.slept);

  run(FRESH, 0, {{YELLOW, 0, 999999}}, 5000);
  check("Start: button 3 stuck (missing 10k)", "FAIL, nothing sent, no sleep",
        hasLog("FAIL: button 3") && sim.sent.empty() && !sim.slept);

  run(TIMER, 0, {});
  check("Timer wake-up (every hour)", "only status sent, sleeps again",
        sim.sent.size() == 1 && buttonMsgs() == 0 && sim.slept);

  // Opvågning på knap
  run(EXT1, bit(BLUE), {{BLUE, 0, 300}});
  check("Woken by blue button", "1 message (button 1), sleeps again",
        buttonMsgs() == 1 && firstIs(1) && sim.slept);

  run(EXT1, bit(BLUE), {{BLUE, 0, 15000}});
  check("Blue held down for 15 s", "1 message, sleeps after release",
        buttonMsgs() == 1 && sim.slept && sim.ms >= 15000);

  run(EXT1, bit(RED), {{RED, 0, 200}, {BLUE, 300, 500}});
  check("Woken by red, blue pressed while starting", "only 1 message (button 4)",
        buttonMsgs() == 1 && firstIs(4));

  // Tryk mens den er vågen (vågen fra ca. 2 s)
  std::vector<Press> spam;
  for (unsigned long t = 3000; t < 9000; t += 150) spam.push_back({BLUE, t, t + 80});
  run(TIMER, 0, spam);
  if (LOCK_WHILE_LED) check("Spam: blue 40 times in 6 s", "max 1 message per LED time",
                            buttonMsgs() >= 2 && buttonMsgs() <= int(6000 / LED_MS) + 1);
  else                check("Spam: blue 40 times in 6 s", "40 messages (spam allowed)", buttonMsgs() == 40);

  // Rød trykkes, mens blå LED lyser (kræver at LED_MS er lang nok til at nå det)
  unsigned long redAt = 3000 + LED_MS / 2;
  if (LED_MS >= 300) {
    run(TIMER, 0, {{BLUE, 3000, 3100}, {RED, redAt, redAt + 50}});
    if (LOCK_WHILE_LED) check("Red after " + ms(LED_MS / 2) + " (LED on)", "1 message (locked)", buttonMsgs() == 1);
    else                check("Red after " + ms(LED_MS / 2) + " (LED on)", "2 messages", buttonMsgs() == 2);
  } else {
    skip("Red while blue LED is on", "skipped: LED_MS < 300");
  }

  unsigned long lateAt = 3000 + LED_MS + 500;
  run(TIMER, 0, {{BLUE, 3000, 3100}, {RED, lateAt, lateAt + 100}});
  check("Red after LED is off", "2 messages", buttonMsgs() == 2);

  run(TIMER, 0, {{BLUE, 3000, 3200}, {RED, 3000, 3200}});
  check("Blue and red at the same time", "1 message", buttonMsgs() == 1);

  run(TIMER, 0, {{BLUE, 3000, 9000}, {RED, 6000, 6200}});
  check("Red pressed while blue is held", "1 message (red ignored)", buttonMsgs() == 1);

  std::vector<Press> bounce;
  for (unsigned long t = 3000; t < 3020; t += 4) bounce.push_back({BLUE, t, t + 2});  // prel ved tryk
  bounce.push_back({BLUE, 3020, 3200});
  bounce.push_back({BLUE, 3202, 3204});                                                // prel ved slip
  run(TIMER, 0, bounce);
  check("Button bounces on press and release", "1 message", buttonMsgs() == 1);

  // LED'er
  if (LED_MS >= 300) {
    run(TIMER, 0, {{BLUE, 3000, 3100}, {RED, redAt, redAt + 50}}, redAt + 100);
    if (LOCK_WHILE_LED) check("LED when red pressed while blue on", "blue stays, red ignored",
                              sim.led[LED_BLUE] == 1 && sim.led[LED_RED] == 0);
    else                check("LED when red pressed while blue on", "LED switches: red on, blue off",
                              sim.led[LED_RED] == 1 && sim.led[LED_BLUE] == 0);
  } else {
    skip("LED switches on another press", "skipped: LED_MS < 300");
  }

  run(TIMER, 0, {{BLUE, 3000, 3100}}, 3000 + LED_MS * 8 / 10);
  bool onBefore = sim.led[LED_BLUE] == 1;
  run(TIMER, 0, {{BLUE, 3000, 3100}}, 3000 + LED_MS + 100);
  bool offAfter = sim.led[LED_BLUE] == 0;
  check("LED on for LED_MS (" + ms(LED_MS) + ")", "on before, off after", onBefore && offAfter);

  // Beskedens indhold (JSON)
  unsigned long gap = LED_MS + 500;                     // så låsen aldrig er i vejen
  run(TIMER, 0, {{BLUE, 3000, 3100}, {GREEN, 3000 + gap, 3100 + gap},
                 {YELLOW, 3000 + 2 * gap, 3100 + 2 * gap}, {RED, 3000 + 3 * gap, 3100 + 3 * gap}});
  bool moods = sim.sent.size() == 5 &&
               has(sim.sent[1].second, "\"button\":1,\"mood\":\"very_happy\"") &&
               has(sim.sent[2].second, "\"button\":2,\"mood\":\"happy\"") &&
               has(sim.sent[3].second, "\"button\":3,\"mood\":\"unhappy\"") &&
               has(sim.sent[4].second, "\"button\":4,\"mood\":\"very_unhappy\"");
  check("Each button has its mood", "1 very_happy ... 4 very_unhappy", moods);

  std::string msg = sim.sent.size() > 1 ? sim.sent[1].second : "";
  bool format = msg.front() == '{' && msg.back() == '}' && has(msg, "\"time\":\"20") &&
                (has(msg, " CET\"") || has(msg, " CEST\"")) && has(msg, "\"ts\":") && msg.size() < 256;
  check("Message is JSON with Danish time + ts", "{...,\"time\":\"... CET\",\"ts\":...}", format);
  if (!msg.empty()) printf("|   example: %-88s |\n", msg.c_str());

  printf("\n%s (%d failed)\n", fails ? "TESTS FAILED" : "ALL TESTS PASSED", fails);
  return fails ? 1 : 0;
}
