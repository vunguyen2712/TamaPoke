#include "game_events.h"

static GameEvSink gSinks[GAME_EV_SINKS] = { nullptr };

void gameEvent(GameEv ev, int16_t arg) {
  if (ev == EV_NONE || ev >= EV_COUNT) return;
  for (uint8_t i = 0; i < GAME_EV_SINKS; i++)
    if (gSinks[i]) gSinks[i](ev, arg);
}

bool gameEventSubscribe(GameEvSink sink) {
  if (!sink) return false;
  for (uint8_t i = 0; i < GAME_EV_SINKS; i++)
    if (gSinks[i] == sink) return true;
  for (uint8_t i = 0; i < GAME_EV_SINKS; i++)
    if (!gSinks[i]) { gSinks[i] = sink; return true; }
  return false;
}

void gameEventClearSinks() {
  for (uint8_t i = 0; i < GAME_EV_SINKS; i++) gSinks[i] = nullptr;
}
