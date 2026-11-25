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
  if (fabsf(det) < 1e-6f) return false;              // ankrene ligger på én linje

  result.x = (a22 * b1 - a12 * b2) / det;
  result.y = (a11 * b2 - a12 * b1) / det;
  return true;
}

Point clampToArea(Point p, float width, float height) {
  if (p.x < 0) p.x = 0;
  if (p.y < 0) p.y = 0;
  if (p.x > width)  p.x = width;
  if (p.y > height) p.y = height;
  return p;
}
