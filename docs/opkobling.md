# Opkobling – Smiley feedback panel

Board: ESP32 DevKit V1 (DOIT), USB opad.

## Strøm
| Fra | Til | Ledning |
|---|---|---|
| ESP32 3V3 | + skinne (rød streg) | rød |
| ESP32 GND | − skinne (blå streg) | sort |

## Knapper (højre side, over midterrillen)
Hver knap: øverste række = signal + 10k til −. Nederste række = rød ledning fra +.

| Knap (oppefra) | Pin | Ledning | Pulldown |
|---|---|---|---|
| 1 | D34 | blå | 10k → − |
| 2 | D35 | grøn | 10k → − |
| 3 | D32 | gul | 10k → − |
| 4 | D33 | orange | 10k → − |

D34/D35 har ingen indbygget pulldown, derfor er 10k påkrævet.
Alle fire er RTC-pins og kan vække ESP32 fra deep sleep (ext1, ANY_HIGH).

## LED'er (venstre side)
Pin → 220Ω → LED langt ben. Kort ben → − skinne.

| LED (oppefra) | Pin | Ledning |
|---|---|---|
| Blå | D23 | blå |
| Grøn | D22 | grøn |
| Gul | D21 | gul |
| Rød | D19 | orange |

Spring TX0 og RX0 over (bruges af USB).

## Modstande
| Værdi | Bruges til | 4 striber | 5 striber |
|---|---|---|---|
| 220Ω | LED'er | rød rød brun guld | rød rød sort sort brun |
| 10kΩ | knapper | brun sort orange guld | brun sort sort rød brun |
