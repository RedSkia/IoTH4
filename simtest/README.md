# Simuleringstest

Kører den rigtige `src/main.cpp` på computeren med en falsk ESP32 (mappen `mock/`).
Tiden og knaptryk er simuleret, så alle scenarier kan testes på få sekunder.

## Kør testen
Kræver en C++-compiler (g++). Fra projektmappen:

```
g++ -std=c++17 -I simtest/mock simtest/sim.cpp -o sim
./sim
```

Resultatet er en tabel med OK/FEJL for hvert scenarie. Testen læser `LOCK_WHILE_LED` fra `main.cpp` og forventer det rigtige for både `true` og `false`. Se `docs/test.md`.

## Filer
| Fil | Indhold |
|---|---|
| `sim.cpp` | Testscenarier |
| `mock/*.h` | Falsk Arduino, WiFi, MQTT og hemmeligheder (ingen rigtige koder) |
