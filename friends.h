#pragma once
#include <Arduino.h>

// Two-board extras: daily gift swap and pet visits (feature 7)
//
// GAME LOGIC ONLY: no gfx, no SD, no touch -- this file links into the
// headless test binaries next to pet.cpp.

// Called once from setup(), after pet/party/bag have loaded.
void friendsBegin();
