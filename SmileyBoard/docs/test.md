# Test af knaplogik

Testet med simuleringen i `simtest/` (kører den rigtige `src/main.cpp` med simuleret tid). Testene følger `LED_MS` og `LOCK_WHILE_LED` og er kørt med `LED_MS` 100 og 1000 og begge værdier af `LOCK_WHILE_LED`.

**Regel:** Hvert tryk sender én besked, og LED'en lyser i 0,1 s (`LED_MS = 100`). Spam er tilladt (`LOCK_WHILE_LED = false`), som læreren ønsker. Et nyt tryk tæller, når alle knapper har været sluppet i 50 ms (debounce), så prel og holdte knapper kun giver én besked.

| # | Scenarie | Forventet | Resultat |
|---|---|---|---|
| 1 | Opstart, alt koblet rigtigt | "Self test OK", status sendt, sover | OK |
| 2 | Opstart, knap 3 sidder fast (mangler 10k) | FAIL, intet sendt, LED'er blinker | OK |
| 3 | Timer-opvågning (hver time) | Kun status sendt, sover igen | OK |
| 4 | Vækket af blå knap | 1 besked (knap 1), sover igen | OK |
| 5 | Blå holdes nede i 15 s | 1 besked, sover først efter slip | OK |
| 6 | Vækket af rød, blå trykkes under opstart | Kun 1 besked (knap 4) | OK |
| 7 | Spam: blå 40 gange på 6 s | 40 beskeder (spam tilladt) | OK |
| 8 | Rød trykkes, mens blå LED lyser | 2 beskeder (springes over når `LED_MS` < 300) | – |
| 9 | Rød trykkes, efter LED er slukket | 2 beskeder | OK |
| 10 | Blå og rød præcis samtidig | 1 besked | OK |
| 11 | Rød trykkes, mens blå holdes | 1 besked (rød ignoreres) | OK |
| 12 | Knappen preller ved tryk og slip | 1 besked | OK |
| 13 | LED skifter, når anden knap trykkes | Rød tændt, blå slukket (springes over når `LED_MS` < 300) | – |
| 14 | LED lyser i `LED_MS` | Tændt før, slukket efter | OK |
| 15 | Hver knap har sit humør | 1 = very_happy … 4 = very_unhappy | OK |
| 16 | Besked er gyldig JSON med dansk tid | `"time":"2026-11-25 12:04:06 CET"` og `ts` | OK |

## Beslutninger
| Valg | Begrundelse |
|---|---|
| Spam tilladt (`LOCK_WHILE_LED = false`) | Lærerens ønske. Sæt til `true`, hvis knapperne skal være låst, mens LED'en lyser |
| LED-tid og lås er adskilt | LED'en kan altid ses, og låsen slås til/fra med én bool |
| Debounce 50 ms | Knappen preller, så ét tryk ellers kan give flere beskeder |
| Tryk tæller først, når alle knapper er sluppet | En holdt knap giver kun én besked |
| Både `time` og `ts` i beskeden | `time` kan læses af mennesker, `ts` er nem for maskiner |

## Fejl fundet med testen
| Fejl | Årsag | Rettelse |
|---|---|---|
| Rød kunne sendes, mens blå LED lyste | Lås og LED-tid var blandet sammen | LED-tid og lås adskilt, lås som bool |
| Tryk lige efter timer-opvågning blev ignoreret | Låsen startede ved opvågning, ikke ved tryk | Låsen starter kun ved et tryk (`lockedUntil`) |

## Begrænsning
Simuleringen tester logikken, ikke hardwaren. LED'er, knapper og wi-fi testes på boardet med selvtesten og Serial Monitor.
