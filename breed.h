#pragma once
#include <Arduino.h>

// The daycare: breeding an egg from two banked creatures (feature 5)
//
// GAME LOGIC ONLY: no gfx, no SD, no touch -- this file links into the
// headless test binaries next to pet.cpp. The screen lives in ui_breed.ino.

// Called once from setup(), after pet/party/bag have loaded.
void breedBegin();
