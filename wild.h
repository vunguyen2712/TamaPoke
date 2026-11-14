#pragma once
#include <Arduino.h>

// Wild encounters and catching, with spawns that follow the time of day and a daily weather (feature 1+2)
//
// GAME LOGIC ONLY: no gfx, no SD, no touch -- this file links into the
// headless test binaries next to pet.cpp. The screen lives in ui_wild.ino.

// Called once from setup(), after pet/party/bag have loaded.
void wildBegin();
