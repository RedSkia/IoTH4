# Logbog

## 25. november 2026
Vi læste opgaven og undersøgte sniffing, ESP-NOW og trilateration.
Vi valgte 4 ankre og en gateway og lavede projektets struktur og tegning.
Vi skrev koden til ankre og gateway og testede matematikken på computeren.

## 26. november 2026
Vi byggede opstillingen med 5 ESP32'er og satte koder og MAC-adresser op.
Vi testede systemet, og positionerne kom frem på MQTT.
Vi fik ankrene til at genstarte selv, hvis gatewayen ikke svarer.
Gatewayen sender ikke data, før tiden er hentet med NTP.
Vi gjorde systemet fleksibelt, så det virker med 2 til mange ankre.
Ankrene finder nu selv gatewayen, så MAC-adresser ikke skal skrives ind.
Ankrene stilles altid med uret, så hvert anker kun skal have et nummer.
Alle boards bruger nu det samme program, og rollen vælges i Serial Monitor.
Ankrene finder selv gatewayens kanal, og kun mobiler på wi-fi spores.
