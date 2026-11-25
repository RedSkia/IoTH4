#pragma once
// Ren matematik uden Arduino-afhængigheder, så den kan testes på computeren (se hosttest/).

struct Point {
  float x;
  float y;
};

// RSSI (dBm) → afstand (m) med log-distance-modellen:
// RSSI = rssiAt1m - 10 · n · log10(d)  ⇔  d = 10^((rssiAt1m - RSSI) / (10 · n))
float rssiToDistance(float rssi, float rssiAt1m, float pathLossExponent);

// Trilateration med mindste kvadraters metode. Kræver mindst 3 ankre, der ikke ligger på én linje.
// Returnerer false, hvis positionen ikke kan beregnes.
bool trilaterate(const Point* anchors, const float* distances, int count, Point& result);

// Holder punktet inden for området (0..width, 0..height).
Point clampToArea(Point p, float width, float height);
