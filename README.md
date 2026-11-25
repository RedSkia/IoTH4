# SmileyBoard

Smiley feedback panel (ESP32) – IoT II, TEC.
Knaptryk sendes som `{"button":N,"ts":...}` til `elever/marcus/smiley` på `wilsons.local:8883` (TLS + login). Enheden sover mellem tryk (deep sleep).

## Kom i gang
1. Åbn mappen i VS Code med PlatformIO.
2. `include/secrets.h`: skriv wi-fi navn og kode (MQTT-login er udfyldt).
3. `include/ca_cert.h`: indsæt indholdet af https://s0ren.github.io/FAG_17682_IoT_II/Features/ca.crt
4. Upload (→) og åbn Serial Monitor (🔌).
5. Se beskeder: http://wilsons.local:8080/elever/marcus/smiley.jsonl

## Filer
| Fil | Indhold |
|---|---|
| `src/main.cpp` | Programmet |
| `include/secrets.h` | Koder – **må aldrig committes** |
| `include/secrets_eksempel.h` | Skabelon til secrets.h |
| `include/ca_cert.h` | Lærerens CA-certifikat |
| `platformio.ini` | Board og bibliotek |
| `docs/blueprint.svg` | Tegning af boardet |
| `docs/opkobling.md` | Hvilke pins der går hvorhen |
| `tests/hardware_test.cpp` | Test af knapper og LED'er (kopier til src/main.cpp) |

## Indstillinger (toppen af main.cpp)
| Navn | Værdi | Betyder |
|---|---|---|
| `LED_MS` | 2000 | LED lyser 2 s efter tryk |
| `AWAKE_MS` | 10000 | sover efter 10 s uden tryk |
| `SLEEP_S` | 3600 | vågner selv hver time |
