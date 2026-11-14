#pragma once
#include <Arduino.h>

// Creature personalities (feature 6)
//
// GAME LOGIC ONLY: no gfx, no SD, no touch -- this file links into the
// headless test binaries next to pet.cpp.
//
// Every creature rolls ONE trait when it hatches. The trait changes which care
// it enjoys (extra joy/bond and a visible cheer when it gets its favourite) and
// nudges its awake needs a little. Two functions are the whole rule, and every
// care path in pet.cpp asks them rather than keeping its own copy:
//
//   personaCare(trait, care, baseJoy)  -- what a trait does to one act of care
//   personaDrain(trait, minute, poops, asleep) -- what it does to a minute's drain
//
// Overnight safety (CLAUDE.md "The three drain paths"): personaDrain() changes
// NOTHING while asleep except a faster energy refill for SLEEPY, and the offline
// catch-up (syncClock) uses the same answer. So no trait can make a night --
// screen off and asleep, or the board switched off -- any harder to survive.
//
// Storage: the live pet under NVS "prsn" (Pet::save/load); a banked creature in
// PartyMon::spare. 0 is "no trait": an egg, or a creature banked before this
// feature existed.

enum : uint8_t {
  PT_NONE = 0,
  PT_GLUTTON,     // loves food
  PT_SLEEPY,      // loves being put to bed
  PT_PLAYFUL,     // loves the ball game
  PT_TIDY,        // loves a bath, hates poop
  PT_CUDDLY,      // loves being petted
  PT_ENERGETIC,   // loves training
  PT_PICKY,       // loves its favourite berry, sniffs at the others
  PT_COUNT
};

// The acts of care a trait can react to.
enum : uint8_t {
  PC_BERRY = 0,   // a berry it does NOT love
  PC_FAV_BERRY,   // its favourite flavour
  PC_CANDY,
  PC_PLAY,        // the ball game (playResult) and play()
  PC_TRAIN,       // the training bag and the reaction test
  PC_BATH,
  PC_CARESS,
  PC_LIGHTS_OFF,  // put to bed with the light button
  PC_COUNT
};

struct PersonaEffect {
  int8_t joy = 0;     // added to the action's own joy
  int8_t food = 0;    // added to the action's own fullness
  uint8_t bond = 0;   // added bond (still under addBond()'s daily cap)
  bool fav = false;   // its favourite: the creature cheers
};

// Extra drain for ONE minute, on top of the normal one. Positive drains more,
// negative drains less (or, for energy while asleep, refills more).
struct PersonaDrain {
  int8_t food = 0;
  int8_t joy = 0;
  int8_t energy = 0;
};

// The extra drain lands on every PERSONA_NUDGE_EVERY-th minute: 1/4 point a
// minute, which is the "a little" in "nudges its needs a little".
#define PERSONA_NUDGE_EVERY 4

// THE answers. `baseJoy` is the joy the action itself gives, so PLAYFUL can
// double it whatever the session was worth.
PersonaEffect personaCare(uint8_t trait, uint8_t care, uint8_t baseJoy);
PersonaDrain personaDrain(uint8_t trait, uint32_t minute, uint8_t poops, bool asleep);

bool personaValid(uint8_t trait);        // 1..PT_COUNT-1
uint8_t personaRoll();                   // a fresh trait, uniform over the seven
const char *personaName(uint8_t trait);  // in the current language ("" for none)
const char *personaLoves(uint8_t trait); // what it loves, short ("" for none)

// The cheer: set when a creature gets its favourite care, read by the main
// screen. Only the player's own Pet sets it (pet.cpp checks `opened`).
#define PERSONA_CHEER_MS 2500UL
void personaCheer(uint8_t trait);
bool personaCheering();
uint8_t personaCheerTrait();

// Called once from setup(), after pet/party/bag have loaded.
void personaBegin();
