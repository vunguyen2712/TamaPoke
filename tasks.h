#pragma once
#include <Arduino.h>

// Daily and weekly tasks, fed by game_events.h (feature 3)
//
// GAME LOGIC ONLY: no gfx, no SD, no touch -- this file links into the
// headless test binaries next to pet.cpp. The screen lives in ui_tasks.ino.

// Called once from setup(), after pet/party/bag have loaded.
void tasksBegin();
