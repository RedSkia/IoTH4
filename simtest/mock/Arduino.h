// Falsk Arduino/ESP32 til test på computeren. Tiden er simuleret.
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <ctime>
#include <string>
#include <vector>

#define HIGH 1
#define LOW 0
#define INPUT 0
#define OUTPUT 1
#define HEX 16

// ---- simuleret tid og hardware ----
struct Press { int pin; unsigned long from, to; };       // knap nede fra..til (ms)
struct Sim {
  unsigned long ms = 0;
  unsigned long limit = 60000;                           // stop simuleringen her
  std::vector<Press> presses;
  int led[40] = {0};
  std::vector<std::pair<unsigned long, std::string>> sent;
  std::vector<std::string> log;
  int wakeCause = 0;                                     // 0 = frisk start
  uint64_t ext1 = 0;                                     // knap der vækkede
  bool slept = false;
};
extern Sim sim;
struct SleepNow {};                                      // kastes ved deep sleep
struct TimeUp {};                                        // kastes når tiden er brugt

inline unsigned long millis() { return sim.ms; }
inline void delay(unsigned long d) { sim.ms += d; if (sim.ms > sim.limit) throw TimeUp(); }
inline void pinMode(int, int) {}
inline int digitalRead(int pin) {
  for (auto& p : sim.presses) if (p.pin == pin && sim.ms >= p.from && sim.ms < p.to) return 1;
  return 0;
}
inline void digitalWrite(int pin, int v) { sim.led[pin] = v; }

struct String {
  std::string s;
  String() {}
  String(const char* c) : s(c) {}
  String(const std::string& c) : s(c) {}
  String(uint32_t v, int) { char b[16]; snprintf(b, 16, "%x", v); s = b; }
  String operator+(const String& o) const { return String(s + o.s); }
  String operator+(const char* o) const { return String(s + o); }
  const char* c_str() const { return s.c_str(); }
};

struct SerialT {
  void begin(int) {}
  void println(const char* t) { sim.log.push_back(t); }
  void println(const tm*, const char*) { sim.log.push_back("Time OK"); }
  void printf(const char* f, ...) { char b[200]; va_list a; va_start(a, f); vsnprintf(b, 200, f, a); va_end(a); sim.log.push_back(b); }
  void flush() {}
};
inline SerialT Serial;
struct EspT { uint64_t getEfuseMac() { return 0x4cba76958b0c; } };
inline EspT ESP;

inline void configTzTime(const char*, const char*, const char* = 0, const char* = 0) {}
inline bool getLocalTime(tm*, uint32_t) { delay(200); return true; }

enum { ESP_SLEEP_WAKEUP_UNDEFINED = 0, ESP_SLEEP_WAKEUP_EXT1 = 3, ESP_SLEEP_WAKEUP_TIMER = 4, ESP_EXT1_WAKEUP_ANY_HIGH = 1 };
inline int esp_sleep_get_wakeup_cause() { return sim.wakeCause; }
inline uint64_t esp_sleep_get_ext1_wakeup_status() { return sim.ext1; }
inline void esp_sleep_enable_ext1_wakeup(uint64_t, int) {}
inline void esp_sleep_enable_timer_wakeup(uint64_t) {}
inline void esp_deep_sleep_start() { sim.slept = true; throw SleepNow(); }
