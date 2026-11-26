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
}

// Læser kommandoer fra Serial Monitor (g, <nummer> <x> <y>, reset). Kaldes ofte af begge roller.
void handleConsole();
