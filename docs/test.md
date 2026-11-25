# Test af knaplogik

Testet med simuleringen i `simtest/` (kører den rigtige `src/main.cpp` med simuleret tid).

**Regel:** Et tryk sender én besked. LED'en lyser i 1 s (`LED_MS`). **Så længe LED'en lyser, er alle knapper låst.** Et nyt tryk tæller først, når LED'en er slukket, og alle knapper har været sluppet i 50 ms.

| # | Scenarie | Forventet | Resultat |
|---|---|---|---|
| 1 | Opstart, alt koblet rigtigt | "Self test OK", status sendt, sover | OK |
| 2 | Opstart, knap 3 sidder fast (mangler 10k) | FAIL, intet sendt, LED'er blinker | OK |
| 3 | Timer-opvågning (hver time) | Kun status sendt, sover igen | OK |
| 4 | Vækket af blå knap | 1 besked (knap 1), sover igen | OK |
| 5 | Blå holdes nede i 15 s | 1 besked, sover først efter slip | OK |
| 6 | Vækket af rød, blå trykkes under opstart | Kun 1 besked (knap 4) | OK |
| 7 | Spam: blå 40 gange på 6 s | Maks 1 besked pr. sekund | OK |
| 8 | Blå, så rød efter 0,3 s (LED lyser) | 1 besked (rød ignoreres) | OK |
| 9 | Blå, så rød efter 0,7 s (LED lyser) | 1 besked (rød ignoreres) | OK |
| 10 | Blå, så rød efter 1,3 s (LED slukket) | 2 beskeder | OK |
| 11 | Blå, så rød efter 2,5 s (LED slukket) | 2 beskeder | OK |
| 12 | Blå og rød præcis samtidig | 1 besked | OK |
| 13 | Rød trykkes, mens blå holdes | 1 besked (rød ignoreres) | OK |
| 14 | Knappen preller ved tryk og slip | 1 besked | OK |
| 15 | Kun den trykkede LED lyser | Rød tændt, blå slukket | OK |
| 16 | LED lyser 1 s og slukker så | Tændt ved 0,9 s, slukket ved 1,1 s | OK |

## Fejl fundet med testen
| Fejl | Årsag | Rettelse |
|---|---|---|
| Rød kunne sendes, mens blå LED lyste | Lås og LED-tid var forskellige | Ét tal (`LED_MS`): LED tændt = låst |
| Tryk lige efter timer-opvågning blev ignoreret | Låsen startede ved opvågning, ikke ved tryk | Låsen starter kun ved et tryk (`lockedUntil`) |

Kontrol: Uden lås (`LED_MS = 0`) fejler 5 tests (7, 8, 9, 15, 16), så testen fanger spam.

## Begrænsning
Simuleringen tester logikken, ikke hardwaren. LED'er, knapper og wi-fi testes på boardet med selvtesten og Serial Monitor.
