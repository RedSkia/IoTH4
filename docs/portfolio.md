# Portfolio

## Idé
Et smiley-panel som dem i banken, hvor et tryk sendes til en server. Det skal kunne køre på batteri, så det skal sove det meste af tiden.

## Hardware
Vi tegnede opkoblingen først og byggede den på et breadboard. Tegningen ligger i `blueprint.png`, og pins står i `wiring.md`.

## Valg af pins
Kun RTC-pins kan vække ESP32 fra deep sleep, så knapperne sidder på D32–D35. D34 og D35 har ingen indbygget pulldown, så vi satte 10k modstande på alle knapper.

## Netværk og sikkerhed
Vi bruger MQTT på port 8883 med TLS, lærerens certifikat og login. Koderne ligger i `secrets.h`, som ikke kommer på GitHub.

## Tid
Tiden hentes med NTP, før vi forbinder til MQTT, fordi TLS skal kende datoen. Vi bruger dansk tidszone, så den selv skifter til sommertid.

## Deep sleep
Den sover efter 10 sekunder uden tryk og vågner på et knaptryk eller hver time. LED'en tændes med det samme, så man ikke mærker ventetiden på wi-fi.

## Knaplogik
Først havde vi en lås på 1 sekund, men den passede ikke med LED-tiden, og læreren ville gerne kunne spamme. Nu er låsen en bool (`LOCK_WHILE_LED`), og debounce sørger for, at én knap kun giver ét tryk.

## Beskeden
Vi sender JSON med knap, humør, dansk tid og Unix-tid. Dansk tid er til mennesker, og Unix-tid er til programmer.

## Test
Selvtesten tjekker LED'er og knapper ved opstart. Simuleringstesten kører koden på computeren og fandt to fejl i knaplogikken, som vi rettede.
