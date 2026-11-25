# Test af knaplogik

Testet med simuleringen i `simtest/` (kører den rigtige `src/main.cpp` med simuleret tid).

**Regel:** Et tryk sender én besked. LED'en lyser i 2 s (`LED_MS`), og alle knapper er låst i 0,5 s (`LOCK_MS`). Et nyt tryk tæller først, når alle knapper har været sluppet i 50 ms, og låsen er udløbet.

| # | Scenarie | Forventet | Resultat |
|---|---|---|---|
| 1 | Opstart, alt koblet rigtigt | "Self test OK", status sendt, sover | OK |
| 2 | Opstart, knap 3 sidder fast (mangler 10k) | FAIL, intet sendt, LED'er blinker | OK |
| 3 | Timer-opvågning (hver time) | Kun status sendt, sover igen | OK |
| 4 | Vækket af blå knap | 1 besked (knap 1), sover igen | OK |
| 5 | Blå holdes nede i 15 s | 1 besked, sover først efter slip | OK |
| 6 | Vækket af rød, blå trykkes under opstart | Kun 1 besked (knap 4) | OK |
| 7 | Spam: blå 40 gange på 6 s | Maks 1 besked pr. 0,5 s | OK |
| 8 | Blå, så rød efter 0,3 s (låst) | 1 besked (rød ignoreres) | OK |
| 9 | Blå, så rød efter 0,7 s (lås udløbet) | 2 beskeder | OK |
| 10 | Blå, så rød efter 2,5 s (LED slukket) | 2 beskeder | OK |
| 11 | Blå og rød præcis samtidig | 1 besked | OK |
| 12 | Rød trykkes, mens blå holdes | 1 besked (rød ignoreres) | OK |
| 13 | Knappen preller ved tryk og slip | 1 besked | OK |
| 14 | Kun den trykkede LED lyser | Rød tændt, blå slukket | OK |

## Fejl fundet med testen
| Fejl | Årsag | Rettelse |
|---|---|---|
| Rød kunne sendes, mens blå LED lyste | Lås og LED-tid var blandet sammen | Lås (`LOCK_MS`, 0,5 s) og LED-tid (`LED_MS`, 2 s) er nu adskilt og testet hver for sig |
| Tryk lige efter timer-opvågning blev ignoreret | Låsen startede ved opvågning, ikke ved tryk | Låsen starter kun ved et tryk (`lockedUntil`) |

Kontrol: Uden lås (`LOCK_MS = 0`) fejler test 7 og 8, så testen fanger spam.

## Begrænsning
Simuleringen tester logikken, ikke hardwaren. LED'er, knapper og wi-fi testes på boardet med selvtesten og Serial Monitor.
