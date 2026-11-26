// Unit-test af positionsmatematikken på computeren (ingen ESP32 nødvendig).
// Kør fra projektmappen:
//   g++ -std=c++17 -I lib/positioning hosttest/test_positioning.cpp lib/positioning/positioning.cpp -o hosttest/run && ./hosttest/run
#include <cmath>
#include <cstdio>
#include "positioning.h"

int fails = 0;
void check(const char* name, bool ok) {
  printf("%-54s %s\n", name, ok ? "OK" : "FAIL");
  if (!ok) fails++;
}
bool near(Point p, Point q, float tolerance) { return hypotf(p.x - q.x, p.y - q.y) <= tolerance; }
float dist(Point a, Point b) { return hypotf(a.x - b.x, a.y - b.y); }

// Forventet RSSI i afstanden d (omvendt model), bruges til at lave testdata
float rssiAt(float d, float a, float n) { return a - 10.0f * n * log10f(d); }

// Beregner position for 'truth' med præcise afstande fra ankrene
bool solve(const Point* anchors, int count, Point truth, Point& result) {
  float d[32];
  for (int i = 0; i < count; i++) d[i] = dist(anchors[i], truth);
  return locate(anchors, d, count, result);
}

int main() {
  Point p;

  // RSSI → afstand
  check("RSSI equal to RSSI_AT_1M gives 1 m", fabsf(rssiToDistance(-45, -45, 2.5f) - 1.0f) < 0.001f);
  check("n = 2, 20 dB weaker gives 10 m",     fabsf(rssiToDistance(-65, -45, 2.0f) - 10.0f) < 0.01f);
  check("Weaker signal gives longer distance", rssiToDistance(-70, -45, 2.5f) > rssiToDistance(-60, -45, 2.5f));

  // Forskellige opstillinger
  Point line2[2] = {{0, 0}, {3, 0}};
  check("2 anchors on a line",           solve(line2, 2, {1.0f, 0}, p) && near(p, {1.0f, 0}, 0.01f));

  Point line3[3] = {{0, 0}, {1, 0}, {2, 0}};
  check("3 anchors on one line",         solve(line3, 3, {1.5f, 0}, p) && near(p, {1.5f, 0}, 0.01f));

  Point triangle[3] = {{0, 0}, {2, 0}, {1, 2}};
  check("3 anchors in a triangle",       solve(triangle, 3, {1.0f, 0.8f}, p) && near(p, {1.0f, 0.8f}, 0.01f));

  Point box[4] = {{0, 0}, {2, 0}, {0, 2}, {2, 2}};
  check("4 anchors in a box",            solve(box, 4, {0.5f, 1.5f}, p) && near(p, {0.5f, 1.5f}, 0.01f));

  Point hexagon[6];
  for (int i = 0; i < 6; i++) hexagon[i] = {2 + 2 * cosf(i * 1.0472f), 2 + 2 * sinf(i * 1.0472f)};
  check("6 anchors in a hexagon",        solve(hexagon, 6, {2.5f, 1.5f}, p) && near(p, {2.5f, 1.5f}, 0.01f));

  Point grid[9];
  for (int i = 0; i < 9; i++) grid[i] = {(float)(i % 3) * 2, (float)(i / 3) * 2};
  check("9 anchors in a grid",           solve(grid, 9, {3.3f, 1.2f}, p) && near(p, {3.3f, 1.2f}, 0.01f));

  // Hele kæden med støj: RSSI → afstand → position
  Point truth = {0.7f, 1.3f};
  float d[4];
  float noise[4] = {3, -3, 2, -2};
  for (int i = 0; i < 4; i++) d[i] = rssiToDistance(rssiAt(dist(box[i], truth), -45, 2.5f) + noise[i], -45, 2.5f);
  bool ok = locate(box, d, 4, p);
  p = clampToBox(p, {0, 0}, {2, 2});
  check("RSSI noise +-3 dB stays within 1 m", ok && near(p, truth, 1.0f));

  // Fejltilfælde og grænser
  float one[1] = {1};
  check("Fewer than 2 anchors are rejected", !locate(line2, one, 1, p));
  check("2 anchors: position stays on the line", solve(line2, 2, {1.0f, 0}, p) && fabsf(p.y) < 0.001f);
  float far2[2] = {10, 1};
  check("2 anchors: never beyond an anchor", locate(line2, far2, 2, p) && near(p, {3, 0}, 0.01f));
  Point c = clampToBox({-1, 5}, {0, 0}, {4, 3});
  check("Position is clamped to the box", c.x == 0 && c.y == 3);

  // Automatisk opstilling: ankre i en ring med uret
  check("Ring: 2 anchors, A1 left, A2 right",   near(ringPosition(1, 2, 2), {0, 0}, 0.001f) && near(ringPosition(2, 2, 2), {2, 0}, 0.001f));
  check("Ring: 4 anchors, A1 top left",         near(ringPosition(1, 4, 2), {0, 0}, 0.001f));
  check("Ring: 4 anchors, A2 top right",        near(ringPosition(2, 4, 2), {2, 0}, 0.001f));
  check("Ring: 4 anchors, A3 bottom right",     near(ringPosition(3, 4, 2), {2, 2}, 0.001f));
  check("Ring: 4 anchors, A4 bottom left",      near(ringPosition(4, 4, 2), {0, 2}, 0.001f));
  check("Ring: 3 anchors, A3 below the middle", near(ringPosition(3, 3, 2), {1, 1.732f}, 0.001f));
  bool neighbours = true;
  for (int total = 2; total <= 12; total++)
    for (int i = 1; i <= total; i++) {
      float d = dist(ringPosition(i, total, 1.5f), ringPosition(i % total + 1, total, 1.5f));
      if (total > 2 && fabsf(d - 1.5f) > 0.002f) neighbours = false;
    }
  check("Ring: neighbours are SPACING apart (3-12 anchors)", neighbours);
  Point r[6];
  for (int i = 0; i < 6; i++) r[i] = ringPosition(i + 1, 6, 2);
  check("Ring of 6 can locate a phone", solve(r, 6, {1.8f, 1.5f}, p) && near(p, {1.8f, 1.5f}, 0.01f));
  Point r3[3];
  for (int i = 0; i < 3; i++) r3[i] = ringPosition(i + 1, 3, 2);
  check("Ring of 3 can locate a phone", solve(r3, 3, {1.0f, 0.6f}, p) && near(p, {1.0f, 0.6f}, 0.01f));

  printf("\n%s (%d failed)\n", fails ? "TESTS FAILED" : "ALL TESTS PASSED", fails);
  return fails ? 1 : 0;
}
