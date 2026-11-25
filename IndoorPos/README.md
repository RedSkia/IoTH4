# IndoorPos

Positionsbestemmelse af mobiler uden GPS – IoT II, TEC.
Fire ESP32-ankre i hjørnerne måler signalstyrken (RSSI) fra mobilers wi-fi. En gateway-ESP32 regner positionen ud og sender **id, tid og (x, y)** til MQTT.

![Opstilling](docs/setup.png)

## Arkitektur
| Del | Program | Opgave |
|---|---|---|
| A1–A4 (ankre) | `src/anchor/` | Sniffer wi-fi-pakker, beregner gennemsnitlig RSSI pr. enhed, sender til gateway med krypteret ESP-NOW |
| Gateway | `src/gateway/` | Modtager RSSI, beregner (x, y) med trilateration, sender til MQTT med TLS |
| Matematik | `lib/positioning/` | RSSI → afstand og trilateration. Uden Arduino, så den kan testes og genbruges |
| Beskedformat | `include/protocol.h` | Den 16-byte struct, ankrene sender |

**Dataflow:** mobil → (wi-fi-pakke) → anker → (ESP-NOW, krypteret) → gateway → (MQTT, TLS) → wilsons.local → database.

## Hardware
- 5 × ESP32 DevKit V1 (4 ankre + 1 gateway). Minimum 3 ankre.
- USB-strøm til hver (powerbank eller lader). **Ingen ledninger eller breadboard.**
- Den indbyggede blå LED (GPIO 2) viser status.

| LED | Anker | Gateway |
|---|---|---|
| Kort blink | Sender måling | – |
| Hurtigt blink | Finder ikke routeren | – |
| Tændt | – | Forbundet til MQTT |

## Opsætning
1. Kopiér `include/secrets_example.h` til `include/secrets.h`. Udfyld wi-fi, MQTT-login, to ESP-NOW-nøgler (16 tegn hver) og et hemmeligt salt. Brug de samme værdier på alle boards.
2. Upload `env:gateway` til ét board og `env:anchor1` … `env:anchor4` til de andre. Miljøet vælges nederst i VS Code.
3. Åbn Serial Monitor på hvert board, og skriv `My MAC:` ind i `include/config.h` (`GATEWAY_MAC` og `ANCHOR_MAC`).
4. Upload alle fem igen.
5. Mål området, og skriv bredde og højde i `AREA_WIDTH` og `AREA_HEIGHT`. Sæt A1 i (0, 0), A2 i (B, 0), A3 i (B, H) og A4 i (0, H), alle i samme højde.
6. Forbind testmobilen til `IoT_H3/4`. Ankrene lytter kun på routerens kanal.

## Kalibrering
Afstanden beregnes med `d = 10^((RSSI_AT_1M − RSSI) / (10 · n))`.
1. Sæt `PUBLISH_RAW = true`, og upload gatewayen. Rå målinger kommer nu på `elever/<bruger>/rssi`.
2. Hold mobilen 1 m fra A1. Gennemsnittet af RSSI er `RSSI_AT_1M`.
3. Gentag ved 2, 3 og 4 m. Regn `n = (RSSI_AT_1M − RSSI) / (10 · log10(d))` ud og tag gennemsnittet. Det er `PATH_LOSS_EXPONENT`.
4. Skriv tallene i `config.h`, og sæt `PUBLISH_RAW = false`.

## Data (MQTT)
Topic: `elever/<bruger>/position` på `wilsons.local:8883` (TLS + login).
```json
{"id":"3f2a9c1b7d4e8a60","time":"2026-11-26 10:15:02 CET","ts":1795680902,"x":1.85,"y":2.40,"anchors":4}
```
| Felt | Betyder |
|---|---|
| `id` | Anonymt id: hash af MAC-adressen med hemmeligt salt |
| `time` | Dansk tid, til mennesker |
| `ts` | Unix-tid, til beregninger (fx tryk pr. time) |
| `x`, `y` | Position i meter fra A1 |
| `anchors` | Antal ankre bag beregningen (3 eller 4) |

**Krav til datalagringen:** en MQTT-broker med TLS på port 8883, et login med skriveret til `elever/<bruger>/#` og en subscriber, der gemmer hver JSON-linje (fx lærerens Mosquitto + MongoDB). Felterne er flade, så der kan grupperes direkte på `ts` (pr. time) og `x`/`y` (heatmap).

## Indstillinger (`include/config.h`)
| Navn | Standard | Betyder |
|---|---|---|
| `AREA_WIDTH`, `AREA_HEIGHT` | 4, 3 | Områdets størrelse i meter |
| `RSSI_AT_1M` | −45 | Kalibreres |
| `PATH_LOSS_EXPONENT` | 2.5 | Kalibreres |
| `RSSI_MIN` | −90 | Svagere pakker ignoreres |
| `WINDOW_MS` | 2000 | Gennemsnit og beregning hvert 2. sekund |
| `STALE_MS` | 6000 | Ældre målinger bruges ikke |
| `FORGET_MS` | 60000 | Enheder glemmes efter 1 minut |
| `MIN_ANCHORS` | 3 | Ankre der skal til for en position |
| `PUBLISH_RAW` | false | Send rå RSSI (kun til kalibrering) |

## Udskiftning og udbygning
- **Andet signal end wi-fi** (fx BLE): skift kun snifferen i `src/anchor/`. Beskeden og gatewayen er uændrede.
- **Anden beregning** (fx vægtet centroid eller Kalman-filter): skift `lib/positioning/`. Testene viser, om den stadig virker.
- **Mere end 4 ankre:** sæt `ANCHOR_COUNT` op og tilføj positioner og MAC'er. Trilaterationen bruger alle.
- **ESP-MESH i stedet for ESP-NOW:** kun afsendelsen i ankeret og modtagelsen i gatewayen skal skiftes.
- **Anden server:** `connectMqtt()` og `publish` i gatewayen er det eneste, der kender MQTT.

## Test
Matematikken testes på computeren:
```
g++ -std=c++17 -I lib/positioning hosttest/test_positioning.cpp lib/positioning/positioning.cpp -o hosttest/run
./hosttest/run
```

## Persondata
Se `docs/portfolio.md`. Kort: den rå MAC-adresse forlader aldrig ankeret, den ligger kun i RAM i 2 sekunder, og gatewayen glemmer enheder efter 1 minut.
