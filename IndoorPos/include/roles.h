#pragma once
#include <stdint.h>

// Ét program, to roller. Rollen vælges i Serial Monitor og gemmes i boardets flash.

namespace anchor {
void setup(uint8_t id, float x, float y);
void loop();
}

namespace gateway {
void setup();
void loop();
bool command(const char* line);                      // anchors <n>, spacing <m>, list. false = ikke en gateway-kommando
void printHelp();
void forgetSettings();                               // slet opstillingen i flash (bruges af reset)
}

// Læser kommandoer fra Serial Monitor (g, <nummer> <x> <y>, reset). Kaldes ofte af begge roller.
void handleConsole();

// true, mens nogen er i gang med at skrive en kommando. Løbende log holder pause imens,
// så den ikke blandes ind i det, der skrives.
bool consoleBusy();
