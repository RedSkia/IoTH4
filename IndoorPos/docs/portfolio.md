# Portfolio

## Opgaven
Finde ud af, om en mobil er i et område, hvor den er, og hvor præcist, kun ud fra dens wi-fi-signal. Samtidig skal vi gemme så lidt om enhederne som muligt.

## Undersøgelser

### RSSI med sniffing
ESP32 kan i promiscuous mode se alle wi-fi-pakker og deres signalstyrke (RSSI). Den kan kun lytte på én kanal ad gangen, så ankrene skal være på samme kanal som mobilen sender på.
Kilde: Espressif ESP-NOW FAQ, https://docs.espressif.com/projects/esp-faq/en/latest/application-solution/esp-now.html

### RSSI → afstand
Log-distance-modellen: `RSSI = A − 10·n·log10(d)`. A er RSSI ved 1 m, og n er 2 i frit rum og op til ca. 4 indendørs. Begge skal måles i vores eget lokale.
Kilde: A Survey of Indoor Localization Systems and Technologies, https://arxiv.org/pdf/1709.01015

### Afstand → position
Trilateration: hver afstand er en cirkel om et anker, og positionen er der, hvor cirklerne krydser. Med støj krydser de ikke præcist, så vi bruger mindste kvadraters metode. Det kræver mindst 3 ankre, der ikke står på én linje.

### Kommunikation mellem ankrene
| Mulighed | Vurdering |
|---|---|
| Alle ankre på wi-fi + MQTT | Simpelt, men ankrene taler ikke sammen, og opgaven beder om det |
| **ESP-NOW til én gateway** | Lærerens anbefaling. Ingen router nødvendig for ankrene, kan krypteres |
| ESP-MESH / painlessMesh | Kun nødvendigt, hvis ankrene ikke kan nå gatewayen. painlessMesh er ikke afprøvet med Arduino-ESP32 3.x |

**Valg:** ESP-NOW. Vigtigt fund: ESP-NOW skal køre på samme kanal som routeren, som gatewayen er forbundet til, ellers kommer beskederne ikke frem, og der kommer ingen fejl. Derfor prøver ankrene kanal 1-13 ved opstart, til de hører gatewayen. Det virker også, når skolen har flere routere med samme navn på forskellige kanaler.
Kilde: https://s0ren.github.io/FAG_17682_IoT_II/Features/esp_now_mesh.html

## Beslutninger
| Beslutning | Begrundelse |
|---|---|
| 4 ankre i hjørnerne + 1 gateway | Standardopstillingen: den 4. giver bedre præcision og tåler, at ét anker ikke hører mobilen |
| Gennemsnit over 2 s | RSSI svinger meget fra pakke til pakke |
| Gatewayen beregner positionen | Én enhed har alle målinger, og ankrene forbliver simple |
| Matematikken i sit eget bibliotek | Kan testes på computeren og skiftes ud |
| Krypteret ESP-NOW (PMK + LMK) | Ellers kan alle på kanalen læse og forfalske målinger |
| MQTT med TLS og login | Samme sikkerhed som i smiley-projektet |
| Lyt kun på datapakker fra enheder til routeren | Routere, beacons og ESP-NOW kommer ikke med. Og mobiler, der leder efter netværk, bruger en tilfældig MAC, som ville give falske enheder |
| Gruppenummer i alle "hello"-beskeder | Flere hold kører samme program i samme lokale. Uden det kunne vores ankre parre med en anden gruppes gateway |
| Fra 2 til mange ankre | 2 ankre giver positionen på linjen mellem dem, 3+ giver (x, y). Står alle på én linje, bruges de to nærmeste |
| Ankrene finder selv gatewayen | Gatewayen siger "her er jeg", og ankrene svarer med nummer og position. Ingen MAC-adresser at skrive ind |
| Ét program til alle boards | Rollen skrives i Serial Monitor og gemmes i boardet. Kun én ting at uploade |
| Automatisk opstilling | Man skriver kun antal ankre og afstand i config. Ankrene får et nummer og stilles altid med uret rundt, så positionen regnes ud automatisk |
| Kryptering kan slås fra | ESP32 kan højst have ca. 7 krypterede forbindelser. Skal der bruges flere ankre, kan krypteringen slås fra |

## Persondata (GDPR)

### Hvad er personlige data?
En MAC-adresse identificerer en mobil og dermed ofte en person. Den er derfor en personoplysning. Den er ikke følsom i GDPR's forstand (som helbred eller religion), men kobles den med **tid og sted**, bliver den mere personlig: man kan se, hvornår en person er hvor.

### Er MAC-adressen altid den samme?
Nej. Moderne mobiler bruger tilfældige MAC-adresser i probe requests, netop for at undgå sporing. Når mobilen er forbundet til et net, bruger den typisk én fast (men tilfældig) adresse til det net. Det er de pakker, vi måler på.
Kilde: Martin m.fl., A Study of MAC Address Randomization in Mobile Devices, https://arxiv.org/pdf/1703.02874

### Hvad gør vi for at gemme mindst muligt?
| Tiltag | Effekt |
|---|---|
| MAC hashes med hemmeligt salt i ankeret | Den rå MAC forlader aldrig ankeret |
| Rå MAC'er ligger kun i RAM i ét vindue (2 s) | Intet gemmes permanent på ankrene |
| Gatewayen glemmer enheder efter 1 minut | Ingen lange historikker på enheden |
| Saltet kan skiftes | Gamle id'er kan ikke kobles til nye |
| Kun id, tid og (x, y) sendes | Ikke mere end formålet kræver |

Bemærk: en hash med salt er **pseudonymisering**, ikke anonymisering. Den, der kender saltet, kan stadig genkende en kendt MAC. Data skal derfor stadig behandles som persondata.

### Hvem kan få adgang, og kan nogen lytte med?
| Trin | Beskyttelse |
|---|---|
| Anker → gateway | ESP-NOW krypteret med PMK/LMK. Hello-beskederne er ukrypterede, men indeholder kun ankerets nummer og position |
| Gateway → server | TLS med lærerens CA-certifikat og login |
| Server | Kun brugere med login. ACL'en giver os kun adgang til `elever/<bruger>/` |
| Koder og nøgler | `secrets.h`, som ikke er i git |

### Hvor længe skal data gemmes?
På enhederne højst 1 minut. I databasen kun så længe, statistikken skal bruges, fx til projektets afslutning.

## Præcision
Forventning: et par meters nøjagtighed. Vægge, mennesker og mobilens retning påvirker RSSI meget. Resultater fra vores målinger skrives her:

| Position (sand) | Beregnet | Afvigelse |
|---|---|---|
| | | |
