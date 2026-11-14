#pragma once
#include <Arduino.h>
#include "trainers.h"

// The weekly gym challenge (feature 4)
//
// GAME LOGIC ONLY: no gfx, no SD, no touch -- this file links into the
// headless test binaries next to pet.cpp. The screen lives in ui_challenge.ino
// and the fight is the sketch's ordinary trainer battle (btlOpenTeamFight).
// Design notes: docs/adventure-challenge.md.
//
// Each week (weeks start on a Monday: tasksWeekOf(), the rule the weekly tasks
// use) one gym leader from a playable region is drawn, with one RULE. The draw
// is a pure function of the week number and a snapshot of what the player
// could field (ChalWorld), taken the first time the week is seen and then
// kept, like the weekly tasks. One win a week pays; a loss or RUN can be
// retried as often as you like that week.

class Pet;
class Party;

enum ChalRule : uint8_t {
  CR_TYPE = 0,    // every creature you bring has type `param` (one of its two)
  CR_LEVEL,       // everyone capped to `param`; the leader's whole team AT `param`
  CR_SOLO,        // exactly one of yours against the leader's ace alone
  CR_NOITEMS,     // held items do nothing this fight (and are never spent)
  CR_TRIO,        // exactly three of yours against the leader's last three
  CR_COUNT
};

#define CHAL_IV 20              // the leader's IVs: between EASY_IV (16) and REMATCH_IV (24)
#define CHAL_BALLS 3            // the reward: CHAL_BALLS x ULTRA BALL ...
#define CHAL_LEVEL_MIN 5        // ... a LEVEL CAP is top - top/4, never under this
#define CHAL_TRIO 3

// What the player could field when the week was drawn. Everything the draw
// depends on is in here, so the tests can hand it any world they like.
struct ChalWorld {
  uint8_t leaders[GYM_REGIONS];   // bit i: gym leader i of that region is drawable
                                  // and reached (i == 0 or leader i-1 beaten)
  uint32_t types;                 // bit t: one of your creatures has type t
  uint8_t mons;                   // creatures you could bring (a hatched pet + the party)
};

struct ChalWeek {
  uint32_t week = 0;
  uint8_t region = 0;     // gym region (index into TRAINER_SETS)
  uint8_t leader = 0;     // 0..TRAINER_GYMS-1
  uint8_t rule = CR_SOLO;
  uint8_t param = 0;      // CR_TYPE: the type; CR_LEVEL: the cap; else 0
  bool won = false;       // this week's reward is paid
};

// Weeks: THE rule the tasks use, not a second one.
uint32_t challengeWeekOf(uint32_t day);

// The snapshot for this player, now (packs on the card included).
ChalWorld challengeWorldOf(const Pet &p, const Party &pt);
// The pure draw. False if nothing can be drawn (no playable leader at all).
bool challengePick(uint32_t week, const ChalWorld &w, ChalWeek &out);
bool challengeRulePossible(uint8_t rule, const ChalWorld &w);

// Called once from setup(); re-reads NVS, which is how the tests prove the
// week is on flash and not just in RAM.
void challengeBegin();
void challengeClearAll();      // tests: forget the week (and the win count)

// Rolls to `day`'s week if it is not the stored one (drawing from `w`) and
// saves. Also re-draws (keeping `won`) if the stored leader can no longer be
// fielded, e.g. its pack left the card. Returns false if there is no challenge.
bool challengeRollTo(uint32_t day, const ChalWorld &w);
// Same, building the world only when a roll is actually needed.
bool challengeRefreshFor(const Pet &p, const Party &pt, uint32_t day);
bool challengeValid();               // a week is drawn
const ChalWeek &challengeCurrent();
bool challengeDone();                // this week's reward is already paid
uint16_t challengeWins();            // weeks cleared, ever

// Open once the player holds at least one badge, any region, either difficulty.
bool challengeUnlockedFor(const Pet &p);

// ---- the rule: ONE answer for the picker AND the start
// May this creature come at all? (the picker only offers these)
bool challengeAllowsMon(const ChalWeek &c, int16_t dex);
// Is this exact squad legal? Every member must pass challengeAllowsMon and
// the count must be within challengeSquadMin..Max. The picker's FIGHT and the
// fight's start both ask this.
bool challengeAllowsSquad(const ChalWeek &c, const int16_t *dex, uint8_t n);
uint8_t challengeSquadMin(const ChalWeek &c);
uint8_t challengeSquadMax(const ChalWeek &c);
bool challengeItemsOff(const ChalWeek &c);

// The leader's team adjusted to the rule. Returns the count (0 = none).
uint8_t challengeTeam(const ChalWeek &c, TrainerMon *out);
bool challengeTeamDrawable(const ChalWeek &c);
// Your level cap for the fight (applied to the copies only): the adjusted
// team's best level, or the LEVEL CAP itself.
uint8_t challengeYourCap(const ChalWeek &c);

// The reward: CHAL_BALLS ULTRA BALLs and one held item that rotates by week.
uint8_t challengeRewardHeld(uint32_t week);
// The fight was won. Pays ONCE a week: `won` is saved BEFORE the items go in
// (a reset between the two loses the reward rather than paying it twice), then
// EV_CHALLENGE_WIN(week & 0xFFFF). Returns true if it paid.
bool challengeWin();
