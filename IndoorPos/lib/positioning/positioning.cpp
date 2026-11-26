#include "positioning.h"
#include <math.h>

float rssiToDistance(float rssi, float rssiAt1m, float pathLossExponent) {
  return powf(10.0f, (rssiAt1m - rssi) / (10.0f * pathLossExponent));
}

bool trilaterate(const Point* anchors, const float* distances, int count, Point& result) {
  if (count < 3) return false;

  // Cirklernes ligninger trækkes fra den sidste, så de bliver lineære:
  // 2(xk - xi)·x + 2(yk - yi)·y = di² - dk² - xi² + xk² - yi² + yk²
  // Løses som normalligninger (AᵀA)·p = Aᵀb for et 2×2-system.
  const Point& k = anchors[count - 1];
  const float dk = distances[count - 1];
  float a11 = 0, a12 = 0, a22 = 0, b1 = 0, b2 = 0;

  for (int i = 0; i < count - 1; i++) {
    const Point& p = anchors[i];
    float ax = 2.0f * (k.x - p.x);
    float ay = 2.0f * (k.y - p.y);
    float b  = distances[i] * distances[i] - dk * dk
             - p.x * p.x + k.x * k.x - p.y * p.y + k.y * k.y;
    a11 += ax * ax;
    a12 += ax * ay;
    a22 += ay * ay;
    b1  += ax * b;
    b2  += ay * b;
  }

  float det = a11 * a22 - a12 * a12;
  if (fabsf(det) <= 1e-6f * (a11 * a22 + 1e-9f)) return false;   // ankrene står på én linje

  result.x = (a22 * b1 - a12 * b2) / det;
  result.y = (a11 * b2 - a12 * b1) / det;
  return true;
}

Point alongLine(Point a, Point b, float da, float db) {
  float dx = b.x - a.x;
  float dy = b.y - a.y;
  float length = sqrtf(dx * dx + dy * dy);
  if (length < 1e-6f) return a;

  // Afstand fra a langs linjen (samme formel som skæringen mellem to cirkler)
  float t = (da * da - db * db + length * length) / (2.0f * length);
  if (t < 0) t = 0;
  if (t > length) t = length;
  return {a.x + dx * t / length, a.y + dy * t / length};
}

bool locate(const Point* anchors, const float* distances, int count, Point& result) {
  if (count < 2) return false;
  if (count >= 3 && trilaterate(anchors, distances, count, result)) return true;

  // 2 ankre, eller alle på én linje: brug de to nærmeste (mindst støj)
  int first = 0, second = 1;
  if (distances[second] < distances[first]) { first = 1; second = 0; }
  for (int i = 2; i < count; i++) {
    if (distances[i] < distances[first])       { second = first; first = i; }
    else if (distances[i] < distances[second]) { second = i; }
  }
  result = alongLine(anchors[first], anchors[second], distances[first], distances[second]);
  return true;
}

Point clampToBox(Point p, Point min, Point max) {
  if (p.x < min.x) p.x = min.x;
  if (p.y < min.y) p.y = min.y;
  if (p.x > max.x) p.x = max.x;
  if (p.y > max.y) p.y = max.y;
  return p;
}

static Point ringCorner(int index, int total, float radius) {
  // Hjørnerne ligger på en cirkel. A1 og A2 danner den øverste kant, så A1 er øverst til venstre.
  // Med y nedad går voksende vinkel med uret.
  const float pi    = 3.14159265f;
  float       angle = -pi / 2 - pi / total + index * 2 * pi / total;
  return {radius * cosf(angle), radius * sinf(angle)};
}

Point ringPosition(int number, int total, float spacing) {
  if (total < 2) return {0, 0};
  const float pi     = 3.14159265f;
  float       radius = spacing / (2 * sinf(pi / total));   // så naboer står 'spacing' fra hinanden

  Point minCorner = {1e9f, 1e9f};                    // flyt ringen, så den starter ved (0, 0)
  for (int i = 0; i < total; i++) {
    Point c = ringCorner(i, total, radius);
    if (c.x < minCorner.x) minCorner.x = c.x;
    if (c.y < minCorner.y) minCorner.y = c.y;
  }
  Point p = ringCorner(number - 1, total, radius);
  float x = roundf((p.x - minCorner.x) * 1000) / 1000;   // millimeter er nok
  float y = roundf((p.y - minCorner.y) * 1000) / 1000;
  return {x, y};
}
