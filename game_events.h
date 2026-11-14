#pragma once
#include <Arduino.h>

// A tiny publish/subscribe bus for "something happened in the game".
//
// It exists so that the daily/weekly TASKS can count things that happen all
// over the codebase (a hatch in pet.cpp, a catch in the wild screen, a tower
// win) without every one of those places having to know that tasks exist.
// The emitter calls gameEvent(); whoever cares subscribes once at boot.
//
// Pure C++ with no hardware dependency, so it links into every test binary
// alongside pet.cpp. With no subscriber an event is simply dropped.
//
// APPEND-ONLY: tasks store event ids in NVS, so an id must never change its
// meaning. New events go at the end, before EV_COUNT.
enum GameEv : uint8_t {
  EV_NONE = 0,
  EV_HATCH,          // arg = dex that hatched
  EV_EVOLVE,         // arg = dex it became
  EV_FEED,           // arg = 1 if it was the favourite berry
  EV_CLEAN,
  EV_PLAY,           // a minigame session ended; arg = score
  EV_TRAIN,          // arg = 0 atk, 1 def, 2 spe
  EV_CARE_DAY,       // first care of a new day; arg = current streak
  EV_GYM_WIN,        // arg = trainer index (8.. = Elite 4 / champion)
  EV_GYM_LOSS,
  EV_REMATCH_WIN,    // arg = trainer index
  EV_WILD_SEEN,      // arg = dex
  EV_WILD_WIN,       // arg = dex
  EV_WILD_CATCH,     // arg = dex
  EV_TOWER_WIN,      // arg = new streak
  EV_TOWER_LOSS,     // arg = streak that ended
  EV_BREED,          // arg = dex of the egg
  EV_ITEM_USED,      // arg = item id
  EV_DEX_REGISTER,   // arg = dex newly registered
  EV_CHALLENGE_WIN,  // weekly gym challenge cleared; arg = week number (low 16 bits)
  EV_RANK_UP,        // arg = new rank
  EV_SHINY_FOUND,    // a shiny wild creature appeared; arg = dex
  EV_CHAIN,          // a wild chain grew; arg = new chain length
  EV_GIFT_SENT,      // two-board gift; arg = item
  EV_GIFT_RECEIVED,  // arg = item
  EV_VISIT,          // two boards' pets visited; arg = peer's dex
  EV_COUNT
};

typedef void (*GameEvSink)(GameEv ev, int16_t arg);

void gameEvent(GameEv ev, int16_t arg = 0);
// Up to GAME_EV_SINKS subscribers; subscribing the same sink twice is a no-op.
#define GAME_EV_SINKS 10
bool gameEventSubscribe(GameEvSink sink);
void gameEventClearSinks();   // tests only
