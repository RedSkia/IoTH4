#pragma once
// Ren matematik uden Arduino-afhængigheder, så den kan testes på computeren (se hosttest/).

struct Point {
  float x;
  float y;
};

// RSSI (dBm) → afstand (m) med log-distance-modellen:
// RSSI = rssiAt1m - 10 · n · log10(d)  ⇔  d = 10^((rssiAt1m - RSSI) / (10 · n))
float rssiToDistance(float rssi, float rssiAt1m, float pathLossExponent);

// Finder positionen ud fra 2 eller flere ankre:
//  - 3+ ankre, der ikke står på én linje: trilateration (x, y)
//  - 2 ankre, eller alle på én linje: positionen på linjen mellem de to nærmeste
bool locate(const Point* anchors, const float* distances, int count, Point& result);

// Trilateration med mindste kvadraters metode. Kræver 3+ ankre, der ikke står på én linje.
bool trilaterate(const Point* anchors, const float* distances, int count, Point& result);

// Position på linjen fra a til b, ud fra afstanden til hver af dem.
Point alongLine(Point a, Point b, float da, float db);

// Position for anker nummer 'number' (1..total), når ankrene står i en ring med uret
// (set oppefra, y nedad): A1 øverst til venstre, A2 til højre for A1, og videre rundt.
// 2 = linje, 3 = trekant, 4 = firkant, 5 = femkant, ... Afstanden mellem naboer er 'spacing'.
// Nederste venstre punkt af ringen ligger ved x = 0, øverste ved y = 0.
Point ringPosition(int number, int total, float spacing);

// Holder punktet inden for rektanglet min..max.
Point clampToBox(Point p, Point min, Point max);
