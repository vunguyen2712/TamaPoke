#pragma once
#include <Arduino.h>
#include "trainers.h"

// The Battle Tower after the Elite 4, and harder gym rematches (features 4+6).
//
// GAME LOGIC ONLY: no gfx, no SD, no touch -- this file links into every
// headless test binary next to pet.cpp. The screen lives in ui_tower.ino, and
// the fight itself is the sketch's ordinary trainer battle: both features hand
// it a TrainerMon team and it never looks at TRAINER_SETS for replacements.
// Design notes: docs/adventure-tower.md.

class Pet;

// ---- the tower
#define TOWER_LEVEL 50          // both sides, for the fight only
#define TOWER_TEAM 3            // 3 vs 3
#define TOWER_REWARD_EVERY 7    // items into the bag at every 7th straight win
#define TOWER_SMART_FLOOR 7     // the AI reads the type chart from this floor on
#define TOWER_IV_FLOORS 14      // EASY_IV on floor 1, rising to HARD_IV on floor 15
#define TOWER_LEGEND_FLOOR 22   // no legendary before the third reward
// Opponents are drawn from a base-stat-total band that climbs with the floor:
// centre = START + STEP * (floor - 1), capped at TOP, +-HALF either side.
#define TOWER_BST_START 380
#define TOWER_BST_STEP 10
#define TOWER_BST_TOP 600
#define TOWER_BST_HALF 50
#define TOWER_STREAK_MAX 30000  // rides in an int16_t event arg

// ---- gym rematches
#define REMATCH_IV 24           // between EASY_IV and HARD_IV
// level' = level * 5/4 + 10, capped at MAX_TRAINER_LEVEL
uint8_t rematchLevel(uint8_t lvl);

// Which creature towerUnlocked()/rematchUnlocked() ask about. The sketch binds
// its live pet once (ui_tower.ino); tests bind their own. Unbound = locked.
// It is a pointer rather than `extern Pet pet` because pet lives in the
// sketch, and every test binary links this file whether or not it has one.
bool towerBindPet(const Pet *p);

// Called once from setup(), after pet/party/bag have loaded. Also re-reads
// NVS, which is how the tests prove the run is on flash and not just in RAM.
void towerBegin();
void towerClearAll();           // tests: forget the run and every rematch

// Open once ANY region's champion (index TRAINER_COUNT-1) has been beaten, on
// either difficulty.
bool towerUnlockedFor(const Pet &p);
bool towerUnlocked();

uint16_t towerStreak();         // wins in a row in the current run
uint16_t towerBest();           // best streak ever
inline uint16_t towerFloor() { return towerStreak() + 1; }   // the next fight
bool towerRunActive();          // a run with at least one win: CONTINUE
uint32_t towerSeed();
void towerStartRun(uint32_t seed);   // START: streak 0, a fresh set of floors

struct TowerTeam {
  TrainerMon mon[TOWER_TEAM];
  uint8_t count;
  uint8_t iv;
  bool smart;                   // hard-mode AI
};

// The species pool: drawable (art + its region's pack), and optionally no
// legendaries. A legendary is judged by the ROOT of its line, so Cosmoem
// counts even though only Cosmog carries the rarity.
bool towerDrawable(int16_t dex);
bool speciesLegendary(int16_t dex);
uint16_t speciesBst(int16_t dex);
// Follows the line while the level allows and the next stage is drawable.
int16_t evolveForLevel(int16_t dex, uint8_t lvl);

void towerBand(uint16_t floor, uint16_t &lo, uint16_t &hi);
uint8_t towerIvFor(uint16_t floor);
bool towerSmartFor(uint16_t floor);
const char *towerFoeName(uint16_t floor);
// The team on `floor` of the run seeded `seed`. Deterministic, so the screen can
// show who is next and the fight is exactly that team. False if the pool is
// too small to field anyone (no pack at all).
bool towerTeamFor(uint16_t floor, uint32_t seed, TowerTeam &out);

struct TowerReward {
  uint8_t item[3];
  uint8_t qty[3];
  uint8_t n;
};
// What reaching `streak` is worth. True only at a multiple of TOWER_REWARD_EVERY.
bool towerRewardFor(uint16_t streak, TowerReward &r);
// A floor cleared: streak +1, best kept, saved, the reward (if any) bagged
// once, EV_TOWER_WIN emitted. Returns the new streak.
uint16_t towerWin(TowerReward *got);
// A loss, or RUN: EV_TOWER_LOSS with the streak that ended, and the run is over.
void towerLoss();

// ---- rematches: open per region once its champion fell on EASY
bool rematchUnlockedFor(const Pet &p, uint8_t region);
bool rematchUnlocked(uint8_t region);
uint16_t rematchMask(uint8_t region);
bool rematchWon(uint8_t region, uint8_t idx);
// Sequential like the ladder: the first is open, each next once the previous fell.
bool rematchOpenFor(const Pet &p, uint8_t region, uint8_t idx);
// The stronger team: levels raised, members evolved where the level allows, and
// filled to six with drawable creatures of the leader's type. Returns the count.
uint8_t rematchTeam(uint8_t region, uint8_t idx, TrainerMon *out);
uint8_t rematchTop(uint8_t region, uint8_t idx, uint8_t *count = nullptr);
bool rematchRewardFor(uint8_t idx, bool first, TowerReward &r);
// Records the win in the rematch mask (never a badge), bags the reward,
// emits EV_REMATCH_WIN. Returns true if it was the first rematch win there.
bool rematchWin(uint8_t region, uint8_t idx, TowerReward *got);
