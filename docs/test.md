# Test af knaplogik

Testet med simuleringen i `simtest/` (kører den rigtige `src/main.cpp` med simuleret tid).

**Regel:** Et tryk sender én besked. Derefter lyser LED'en i 2 s, og alle knapper er låst. Et nyt tryk tæller først, når alle knapper har været sluppet i 50 ms, og låsen er udløbet.

| # | Scenarie | Forventet | Resultat |
|---|---|---|---|
| 1 | Opstart, alt koblet rigtigt | "Self test OK", status sendt, sover | OK |
| 2 | Opstart, knap 3 sidder fast (mangler 10k) | FAIL, intet sendt, LED'er blinker | OK |
| 3 | Timer-opvågning (hver time) | Kun status sendt, sover igen | OK |
| 4 | Vækket af blå knap | 1 besked (knap 1), sover igen | OK |
| 5 | Blå holdes nede i 15 s | 1 besked, sover først efter slip | OK |
| 6 | Vækket af rød, blå trykkes under opstart | Kun 1 besked (knap 4) | OK |
| 7 | Spam: blå 40 gange på 6 s | Maks 1 besked pr. 2 s | OK |
| 8 | Blå, så rød efter 0,3 s | 1 besked (rød ignoreres) | OK |
| 9 | Blå, så rød efter 1,5 s (LED lyser stadig) | 1 besked (rød ignoreres) | OK |
| 10 | Blå, så rød efter 2,5 s (LED slukket) | 2 beskeder | OK |
| 11 | Blå og rød præcis samtidig | 1 besked | OK |
| 12 | Rød trykkes, mens blå holdes | 1 besked (rød ignoreres) | OK |
| 13 | Knappen preller ved tryk og slip | 1 besked | OK |
| 14 | Kun den trykkede LED lyser | Rød tændt, blå slukket | OK |

## Fejl fundet med testen
| Fejl | Årsag | Rettelse |
|---|---|---|
| Rød kunne sendes 1,5 s efter blå | Låsen (1 s) var kortere end LED-tiden (2 s) | Knapperne er låst, så længe LED'en lyser |
| Tryk lige efter timer-opvågning blev ignoreret | Låsen startede ved opvågning, ikke ved tryk | Låsen starter kun ved et tryk (`lockedUntil`) |

Kontrol: Med den gamle 1 s-lås fejler test 7 og 9, så testen fanger fejlen.

## Begrænsning
Simuleringen tester logikken, ikke hardwaren. LED'er, knapper og wi-fi testes på boardet med selvtesten og Serial Monitor.
