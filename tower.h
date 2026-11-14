#pragma once
#include <Arduino.h>

// The Battle Tower after the Elite 4, and harder gym rematches (features 4+6)
//
// GAME LOGIC ONLY: no gfx, no SD, no touch -- this file links into the
// headless test binaries next to pet.cpp. The screen lives in ui_tower.ino.

// Called once from setup(), after pet/party/bag have loaded.
void towerBegin();
