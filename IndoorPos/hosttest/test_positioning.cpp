// Unit-test af positionsmatematikken på computeren (ingen ESP32 nødvendig).
// Kør fra projektmappen:
//   g++ -std=c++17 -I lib/positioning hosttest/test_positioning.cpp lib/positioning/positioning.cpp -o hosttest/run && ./hosttest/run
#include <cmath>
#include <cstdio>
#include "positioning.h"

int fails = 0;
void check(const char* name, bool ok) {
  printf("%-52s %s\n", name, ok ? "OK" : "FAIL");
  if (!ok) fails++;
}
bool near(float a, float b, float tolerance) { return fabsf(a - b) <= tolerance; }

// Forventet RSSI i afstanden d (omvendt model), bruges til at lave testdata
float rssiAt(float d, float a, float n) { return a - 10.0f * n * log10f(d); }

int main() {
  // RSSI → afstand
  check("RSSI equal to RSSI_AT_1M gives 1 m", near(rssiToDistance(-45, -45, 2.5f), 1.0f, 0.001f));
  check("n = 2, 20 dB weaker gives 10 m",      near(rssiToDistance(-65, -45, 2.0f), 10.0f, 0.01f));
  check("Weaker signal gives longer distance",  rssiToDistance(-70, -45, 2.5f) > rssiToDistance(-60, -45, 2.5f));

  // Trilateration med præcise afstande
  Point corners[4] = {{0, 0}, {4, 0}, {4, 3}, {0, 3}};
  Point truth = {1.5f, 2.0f};
  float d[4];
  for (int i = 0; i < 4; i++) d[i] = hypotf(truth.x - corners[i].x, truth.y - corners[i].y);
  Point p;
  check("4 anchors, exact distances",  trilaterate(corners, d, 4, p) && near(p.x, 1.5f, 0.01f) && near(p.y, 2.0f, 0.01f));
  check("3 anchors, exact distances",  trilaterate(corners, d, 3, p) && near(p.x, 1.5f, 0.01f) && near(p.y, 2.0f, 0.01f));

  // Hele kæden: RSSI → afstand → position
  float rssi[4];
  for (int i = 0; i < 4; i++) rssi[i] = rssiAt(d[i], -45, 2.5f);
  for (int i = 0; i < 4; i++) d[i] = rssiToDistance(rssi[i], -45, 2.5f);
  check("RSSI -> distance -> position", trilaterate(corners, d, 4, p) && near(p.x, 1.5f, 0.01f) && near(p.y, 2.0f, 0.01f));

  // Støj: ±3 dB giver stadig et punkt i nærheden
  float noisy[4] = {rssi[0] + 3, rssi[1] - 3, rssi[2] + 2, rssi[3] - 2};
  for (int i = 0; i < 4; i++) d[i] = rssiToDistance(noisy[i], -45, 2.5f);
  bool ok = trilaterate(corners, d, 4, p);
  p = clampToArea(p, 4, 3);
  check("RSSI noise +-3 dB stays within 1.5 m", ok && hypotf(p.x - 1.5f, p.y - 2.0f) < 1.5f);

  // Fejltilfælde
  Point line[3] = {{0, 0}, {1, 0}, {2, 0}};
  float dl[3] = {1, 1, 1};
  check("Anchors on one line are rejected", !trilaterate(line, dl, 3, p));
  check("Fewer than 3 anchors are rejected", !trilaterate(corners, dl, 2, p));

  // Begrænsning til området
  Point c = clampToArea({-1, 5}, 4, 3);
  check("Position is clamped to the area", c.x == 0 && c.y == 3);

  printf("\n%s (%d failed)\n", fails ? "TESTS FAILED" : "ALL TESTS PASSED", fails);
  return fails ? 1 : 0;
}
