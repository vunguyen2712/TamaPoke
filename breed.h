#pragma once
#include <Arduino.h>
#include "party.h"

// The daycare: breeding an egg from two banked creatures (feature 5)
//
// GAME LOGIC ONLY: no gfx, no SD, no touch -- this file links into the
// headless test binaries next to pet.cpp. The screen lives in ui_daycare.ino.
// Design notes: docs/adventure-daycare.md.
//
// THE RULES (the README's "Daycare" section documents the same numbers):
//
//   Compatible   -- two DIFFERENT banked creatures (party or box) that share a
//                   type, or where exactly one of them is DITTO. Two DITTOs
//                   never; any creature whose evolution line holds a legendary
//                   never. There are no egg groups in the data, so the type
//                   rule is the whole compatibility table.
//   The egg      -- the BASE form of the non-Ditto parent's line (parent A's if
//                   both qualify: the player chooses by which one is picked
//                   first). It must be something the egg pool itself could
//                   hatch: not evolution-only, drawable, its region's pack on
//                   the card. Otherwise the pair is refused with a reason.
//   IVs          -- 3 of the 4 are inherited, each from a random parent; the
//                   4th is rolled at hatch like any egg. With the OVAL CHARM all
//                   4 are inherited, each the BETTER of the two parents.
//   Shiny        -- both parents shiny: a 1 in BREED_SHINY_BONUS chance, rolled
//                   once when the egg is made, of the egg being shiny on top of
//                   its own normal roll.
//   Where it goes -- QUEUED (one at a time) and becomes the NEXT egg: newEgg()
//                   takes it instead of rolling. If the live creature is an
//                   uncracked egg it can also be swapped in at once.
//   Cooldown     -- one breed per game day (Pet::currentDay()).
//   Parents are NOT consumed.

#define BREED_BANK (PARTY_SLOTS + BOX_SLOTS)   // banked index: party 0..5, box 6..23
#define BREED_INHERIT 3          // IVs inherited without the Oval Charm (of 4)
#define BREED_IV_ROLL 0xFF       // an IV the parents did not decide: rolled at hatch
#define BREED_SHINY_BONUS 16     // both parents shiny: 1 in this many
#define BREED_DITTO 132        // DITTO: breeds with anything but itself

class Pet;

// Why a pair can or cannot breed. The screen shows the reason.
enum BreedVerdict : uint8_t {
  BV_OK = 0,
  BV_EMPTY,       // a slot is empty
  BV_SAME,        // the same creature twice
  BV_TWO_DITTO,
  BV_LEGEND,      // a legendary line
  BV_NO_TYPE,     // no shared type and no Ditto
  BV_NO_EGG,      // the egg would be undrawable, evolution-only or its pack is missing
};

// What making an egg did.
enum BreedResult : uint8_t { BR_OK = 0, BR_BAD_PAIR, BR_QUEUE_FULL, BR_COOLDOWN, BR_NOT_READY };

struct BredEgg {
  int16_t dex = 0;               // 0 = none
  uint8_t iv[4] = { 0, 0, 0, 0 };   // atk, def, spe, hp; BREED_IV_ROLL = rolled at hatch
  uint8_t shiny = 0;             // the both-shiny bonus roll came up
  int16_t pa = 0, pb = 0;        // the parents' species, for the screen
};

// ---- the rule (pure; no state)
int16_t breedBaseOf(int16_t dex);              // first form of dex's line
bool breedLineLegendary(int16_t dex);          // any member of the line legendary
BreedVerdict breedCheck(const PartyMon &a, const PartyMon &b);
int16_t breedEggSpecies(const PartyMon &a, const PartyMon &b);   // 0 unless BV_OK
// Fills out[4] (atk, def, spe, hp). Uses random().
void breedInherit(const PartyMon &a, const PartyMon &b, bool ovalCharm, uint8_t out[4]);

// ---- the daycare's state (NVS key "dcare", see save.cpp)
const PartyMon *breedBanked(uint8_t idx);      // nullptr if out of range
BreedVerdict breedCheckIdx(uint8_t ia, uint8_t ib);
bool breedOnCooldown(uint32_t day);            // true = already bred today
BreedResult breedMake(uint8_t ia, uint8_t ib, uint32_t day);   // emits EV_BREED
bool breedHasQueued();
// Could a breed happen at all right now: no egg already queued, and some
// compatible pair among the banked creatures. The tasks feature asks this
// before offering a BREED task, so it can never offer an impossible one.
bool breedPossible();
const BredEgg &breedQueued();
const BredEgg &breedLive();                    // the bred egg currently on screen, if any
bool breedQueuedHatchable();                   // the queued species can still hatch here
bool breedCanSwapIn(const Pet &p);
bool breedSwapIn(Pet &p);                      // the uncracked live egg becomes the queued one

// ---- hooks for pet.cpp (only ever called for the begin()'d player Pet)
// newEgg(): moves the queued egg to "live" and hands over its species. Any
// older live record is dropped first -- the egg it described is gone.
bool breedTakeQueued(int16_t &dex, bool &shiny);
// hatch(): the inherited IVs, if the egg cracking is the bred one. Consumes it.
bool breedTakeLive(int16_t dex, uint8_t out[4]);

// Called once from setup(), after pet/party/bag have loaded. Loads from NVS.
void breedBegin();
void breedClearAll();     // tests
