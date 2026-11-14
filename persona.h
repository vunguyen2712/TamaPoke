#pragma once
#include <Arduino.h>

// Creature personalities (feature 6)
//
// GAME LOGIC ONLY: no gfx, no SD, no touch -- this file links into the
// headless test binaries next to pet.cpp.

// Called once from setup(), after pet/party/bag have loaded.
void personaBegin();
