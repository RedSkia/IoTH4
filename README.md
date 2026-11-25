# SmileyBoard

Smiley feedback panel (ESP32) – IoT II, TEC.
Et tryk på en af fire knapper sendes til `elever/marcus/smiley` på `wilsons.local:8883` (MQTT med TLS + login). Enheden sover mellem tryk (deep sleep).

## Besked (JSON)
```json
{"button":1,"mood":"very_happy","time":"2026-11-25 09:47:35 CET","ts":1795596455}
```
| Felt | Betyder |
|---|---|
| `button` | Knap 1–4 (oppefra) |
| `mood` | `very_happy`, `happy`, `unhappy`, `very_unhappy` |
| `time` | Dansk tid, læsbar (CET om vinteren, CEST om sommeren) |
| `ts` | Unix-tid i sekunder (til maskiner) |

Ved timer-opvågning sendes `{"status":"awake","time":"...","ts":...}`.

## Kom i gang
1. Åbn mappen i VS Code med PlatformIO.
2. Kopiér `include/secrets_example.h` til `include/secrets.h`, og skriv wi-fi og MQTT-login.
3. Upload (→) og åbn Serial Monitor (🔌).
4. Se beskeder: http://wilsons.local:8080/elever/marcus/smiley.jsonl

## Filer
| Fil | Indhold |
|---|---|
| `src/main.cpp` | Programmet |
| `include/secrets.h` | Koder, **må aldrig committes** |
| `include/secrets_example.h` | Skabelon til secrets.h |
| `include/ca_cert.h` | Lærerens CA-certifikat |
| `platformio.ini` | Board og bibliotek |
| `docs/logbook.md` | Logbog, én kort note pr. dag |
| `docs/portfolio.md` | Idéer og beslutninger, fra idé til løsning |
| `docs/presentation.md` | Præsentation af projektet |
| `docs/blueprint.png` / `.svg` | Tegning af boardet |
| `docs/wiring.md` | Hvilke pins der går hvorhen |
| `docs/test.md` | Testrapport med alle scenarier |
| `simtest/` | Simuleringstest af knaplogikken (se simtest/README.md) |

## Indstillinger (toppen af main.cpp)
| Navn | Værdi | Betyder |
|---|---|---|
| `MOOD` | very_happy … very_unhappy | hvad hver knap betyder |
| `LED_MS` | 100 | LED lyser 0,1 s efter tryk |
| `LOCK_WHILE_LED` | false | `true` = knapperne er låst, så længe LED'en lyser. `false` = spam tilladt |
| `AWAKE_MS` | 10000 | sover efter 10 s uden tryk |
| `SLEEP_S` | 3600 | vågner selv hver time |

## Selvtest
Ved upload, reset (EN) og når strømmen sættes til, kører `selfTest()` i `main.cpp`:
- LED'erne lyser én ad gangen (tjek med øjnene).
- Knapperne skal være LOW, når ingen trykker. Ellers skrives `FAIL` i Serial Monitor, og alle LED'er blinker hurtigt.

Selvtesten kører ikke, når enheden vågner fra deep sleep.
