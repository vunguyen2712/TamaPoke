#pragma once
#include <Arduino.h>
#include "game_events.h"

// Daily and weekly tasks, fed by game_events.h (feature 3)
//
// GAME LOGIC ONLY: no gfx, no SD, no touch -- this file links into the
// headless test binaries next to pet.cpp. The screen lives in ui_tasks.ino.
//
// Three daily tasks and two weekly ones. Which tasks is decided by the DAY
// NUMBER (Pet::today(), the same RTC day the streak counts in) and the week
// number derived from it, together with what this player can actually do --
// a tower task is never offered before the tower opens. The chosen table rows
// are stored when the day rolls, so a task cannot change under the player's
// feet if, say, they beat the last gym at noon. See docs/adventure-tasks.md.

class Pet;

#define TASK_DAILY 3
#define TASK_WEEKLY 2
#define TASK_SLOTS (TASK_DAILY + TASK_WEEKLY)   // slots 0..2 daily, 3..4 weekly
#define TASK_NOTICE_MS 3000UL                    // the "TASK DONE" banner

// What a task counts. APPEND-ONLY: the screen's label table is indexed by it.
enum TaskKind : uint8_t {
  TK_NONE = 0,
  TK_FEED,          // EV_FEED, any berry
  TK_FAV_BERRY,     // EV_FEED with arg 1 (the favourite)
  TK_CLEAN,         // EV_CLEAN
  TK_PLAY,          // EV_PLAY with a score above 0
  TK_TRAIN,         // EV_TRAIN (bag or reaction test)
  TK_CARE_DAY,      // EV_CARE_DAY
  TK_HATCH,         // EV_HATCH
  TK_EVOLVE,        // EV_EVOLVE
  TK_GYM_WIN,       // EV_GYM_WIN
  TK_REMATCH_WIN,   // EV_REMATCH_WIN
  TK_WILD_SEEN,     // EV_WILD_SEEN
  TK_WILD_WIN,      // EV_WILD_WIN
  TK_WILD_CATCH,    // EV_WILD_CATCH
  TK_TOWER_WIN,     // EV_TOWER_WIN
  TK_BREED,         // EV_BREED
  TK_ITEM_USED,     // EV_ITEM_USED
  TK_DEX_NEW,       // EV_DEX_REGISTER
  TK_WELL_KEPT,     // STATE: every stat >= TASK_WELL_MIN for `target` minutes straight
  TK_COUNT
};
#define TASK_WELL_MIN 50   // the stat floor TK_WELL_KEPT has to hold

// One row of a task table: what to count, how many, and what it pays.
struct TaskDef {
  uint8_t kind;      // TaskKind
  uint8_t target;    // count (minutes for TK_WELL_KEPT)
  uint8_t item;      // ItemId of the reward
  uint8_t qty;
};
// APPEND-ONLY: the stored task is a row index into these.
extern const TaskDef DAILY_TASKS[];
extern const uint8_t DAILY_TASK_COUNT;
extern const TaskDef WEEKLY_TASKS[];
extern const uint8_t WEEKLY_TASK_COUNT;

// What this player can do, sampled when a day or week rolls. Every field is a
// "this can happen within the period" answer; a task needing a false one is
// never offered. Plain struct so tests can build any world they like.
struct TaskWorld {
  bool eggToday;      // the live pet is an egg, or a farewell (-> a new egg) is open
  bool evolveToday;   // its next evolution is within reach today
  bool evolveWeek;    // ... or within what is left of the week
  bool dexNewWeek;    // that evolution would register a new species
  bool gymsLeft;      // some available ladder still has an unearned (easy) badge
  bool towerOpen;     // towerUnlockedFor(): tower floors
  bool rematchOpen;   // rematchUnlockedFor() in some playable region
  bool hasBall;       // a ball in the bag (a catch needs one)
  bool hasHeld;       // a held item in the bag (an item can be used up)
  bool canBreed;      // two banked creatures for the daycare
};
TaskWorld taskWorldOf(const Pet &p);
bool taskPossible(uint8_t kind, bool weekly, const TaskWorld &w);
// Deterministic: the same seed and world always give the same rows. Writes
// TASK_DAILY (or TASK_WEEKLY) distinct row indices into `out`.
void tasksPick(uint32_t seed, bool weekly, const TaskWorld &w, uint8_t *out);
uint32_t tasksWeekOf(uint32_t day);   // weeks start on a Monday

// Weak by default: unlocked once any region's champion (badge TRAINER_COUNT-1)
// has been beaten. The tower feature overrides it with its own answer.
bool towerUnlocked();

// Once from setup(), after pet/party/bag have loaded. Loads the save and
// subscribes to the event bus; it does NOT roll the day -- at boot the pet's
// clock is still yesterday's until syncClock() runs.
void tasksBegin(Pet &p);
// Bring the tasks up to the pet's current day (a no-op if nothing changed).
void tasksRefresh();
// The same, for an explicit day number -- the testable entry point.
void tasksRollTo(uint32_t day);
// Once a minute from loop(): rolls the day and checks the state task.
void tasksTick();
void tasksTickAt(uint32_t day, bool wellKept);
bool tasksWellKept(const Pet &p);   // the TK_WELL_KEPT condition, right now
void tasksOnEvent(GameEv ev, int16_t arg);   // the bus sink

// For the screen. `slot` is 0..TASK_SLOTS-1.
const TaskDef &tasksDef(uint8_t slot);
uint8_t tasksProgress(uint8_t slot);    // 0..target
bool tasksDone(uint8_t slot);
bool tasksClaimed(uint8_t slot);
bool tasksClaim(uint8_t slot);          // pays out once; false if not done or already paid
bool tasksBonusReady();                 // all three dailies claimed, bonus not yet
bool tasksBonusClaimed();
bool tasksClaimBonus();
uint8_t tasksBonusItem();
uint8_t tasksBonusQty();
uint8_t tasksClaimable();               // tasks + bonus waiting for a tap
uint32_t tasksDay();
uint32_t tasksWeek();
bool tasksNoticeActive();               // a task completed in the last TASK_NOTICE_MS
void tasksForget();                     // tests: drop RAM state (as if rebooted)
