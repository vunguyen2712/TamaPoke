#pragma once
#include <Arduino.h>
#include <Preferences.h>

// Wild encounters and catching, with spawns that follow the time of day and a
// daily weather (features 1 + 2). Design notes: docs/adventure-wild.md.
//
// GAME LOGIC ONLY: no gfx, no SD, no touch -- this file links into the
// headless test binaries next to pet.cpp. The screen lives in ui_wild.ino.
//
// The clock is the PET'S clock, never a second one: the day is Pet::today()
// and the hour comes from Pet::lastSeenEpoch, the same source isNightHour()
// reads. Every function that needs them takes them as arguments, so the tests
// can drive any day and hour without touching an RTC.

#define WILD_PER_DAY 3        // encounters a day, refilled when the day changes
#define WILD_THROWS 3         // balls you may throw before it flees
#define WILD_SHINY_ODDS 256   // 1 in this; the Shiny Charm doubles the chance
#define WILD_GIFT_BALLS 10    // POKE BALLs given once, the first time WILD opens
#define WILD_CATCH_CAP 90     // no ball is ever a sure thing (percent)
// Shiny chains (docs/adventure-wild-chain.md). Odds are N / WILD_SHINY_ODDS with
// N = 1 + chain length, at most WILD_CHAIN_SHINY_CAP (16/256 = 1/16), then x2
// with the Shiny Charm. They apply to the CHAINED species only.
#define WILD_CHAIN_SHINY_CAP 16
#define WILD_CHAIN_MAX 99     // the chain stops counting here (it never wraps)

// Weather of the day. Derived from the day number alone, so it is the same all
// day, the same after a reboot, and needs no storage.
enum WildWeather : uint8_t { WX_CLEAR = 0, WX_SUNNY, WX_RAIN, WX_SNOW, WX_FOG, WX_WIND, WX_COUNT };
// Window of the day, from the hour.
enum WildTime : uint8_t { WT_MORNING = 0, WT_DAY, WT_EVENING, WT_NIGHT, WT_COUNT };

uint8_t wildWeatherOf(uint32_t day);
uint8_t wildHourOf(uint32_t epoch);       // same formula as Pet::isNightHour(); 12 with no clock
uint8_t wildTimeOfHour(uint8_t hour);
inline uint8_t wildTimeOf(uint32_t epoch) { return wildTimeOfHour(wildHourOf(epoch)); }

// The ONE answer to "can this species appear in the wild at this level, in this
// region". Every roll goes through it; the tests roll thousands and ask it too.
//   - inside the region (REGION_ALL: anywhere), and that region's pack is on
//     the card (regionAvailable), and the species has art (speciesHasArt)
//   - never a legendary
//   - an evolution-only species only once the wild level has reached the level
//     its predecessor evolves at (and never one whose line starts legendary)
bool wildEligible(int16_t dex, uint8_t region, uint8_t level);
// GHOST types come out only at night -- or under fog, the one weather that
// brings them out by day.
bool wildTimeAllows(int16_t dex, uint8_t weather, uint8_t tod);
// Relative spawn weight (0 = cannot spawn now). Rarity sets the base; weather,
// the time window and the species' biome multiply it. See docs/adventure-wild.md.
uint16_t wildWeight(int16_t dex, uint8_t weather, uint8_t tod);
// The species whose evolution produces `dex`, or 0 for a base form.
int16_t wildPredecessor(int16_t dex);

// A deterministic pick: the same inputs always give the same creature.
int16_t wildRollSpecies(uint8_t region, uint8_t weather, uint8_t tod, uint8_t level, uint32_t seed);
// The same pick with a shiny chain running on `chainDex` (0 = none): when that
// species can spawn in this roll, its weight is raised to equal the sum of
// everything else's, so it is (at least) HALF of the roll. Never in wildWeight():
// that stays the plain conditions-only weight other features read.
int16_t wildRollSpeciesChained(uint8_t region, uint8_t weather, uint8_t tod, uint8_t level, uint32_t seed,
                               int16_t chainDex);
// Shiny chance numerator out of WILD_SHINY_ODDS: 1 + chainLen, capped at
// WILD_CHAIN_SHINY_CAP, doubled by the charm. chainLen 0 = the plain 1 (2).
uint16_t wildShinyNumer(uint8_t chainLen, bool charm);
// GHOST-style species that some weather/window combination keeps away: seeing
// one means today's conditions are what brought it out.
bool wildConditional(int16_t dex);
// Rare for the wild: a rare base form, or an evolved form of a rare line.
bool wildIsRare(int16_t dex);

// Why the next encounter is worth an alert (0 = it is not). ONE answer, read
// by the main-screen badge and the hub row alike.
enum WildAlertWhy : uint8_t { WA_RARE = 1, WA_SHINY = 2, WA_COND = 4, WA_NEW = 8 };
uint8_t wildAlertWhy(int16_t dex, bool shiny, bool registered);

// What the next encounter is (or the one already out).
struct WildNext {
  int16_t dex = 0;
  uint8_t level = 0;
  uint8_t shiny = 0;
  uint8_t weather = 0;
  uint8_t tod = 0;
  bool out = false;         // already out (MEET/FIGHT/CATCH), not a fresh roll
};
// Wild level: the live pet's level -5..+1, at least 2, at most 100.
uint8_t wildLevelFor(uint8_t petLevel, uint32_t seed);
// Catch chance in percent for a species and a ball (ItemInfo.power = x10),
// capped at WILD_CATCH_CAP. 0 for something that is not a ball.
uint8_t wildCatchChance(int16_t dex, uint8_t ball);

// What a throw did.
enum WildThrow : uint8_t {
  WTH_NONE = 0,     // nothing to throw at, or not a ball
  WTH_NO_BALL,      // none of that ball in the bag
  WTH_NO_ROOM,      // party AND box full: nothing spent
  WTH_MISSED,       // broke free, throws remain
  WTH_FLED,         // broke free on the last throw
  WTH_PARTY,        // caught, into the party
  WTH_BOX,          // caught, into the box
};

enum WildPhase : uint8_t {
  WP_NONE = 0,      // nothing out: roll one (if any are left today)
  WP_MEET,          // shown, FIGHT or RUN
  WP_FIGHT,         // a battle is running; a reboot here counts as a loss
  WP_CATCH,         // beaten, throwing balls
};

class Party;
class Bag;
class Pet;

class Wild {
public:
  // The persisted state (NVS "wild"). Raw bytes, so append-only like PartyMon.
  struct State {
    uint32_t day = 0;       // the day `used` counts for
    uint8_t used = 0;       // encounters spent on `day`
    uint8_t phase = WP_NONE;
    int16_t dex = 0;        // the creature out now (0 = none)
    uint8_t level = 0;
    uint8_t shiny = 0;
    uint8_t throws = 0;     // balls thrown at it so far
    uint8_t weather = 0;    // conditions it was met in, for the screen
    uint8_t tod = 0;
    uint32_t encDay = 0;    // the day it was met (a carried-over catch is not today's)
  };
  State st;
  bool gift = false;        // NVS "wgift": the starter balls were given

  // The shiny chain (NVS "wchain", raw bytes, append-only). Beating `dex` again
  // adds a link; beating another species restarts it on that one; a whole day
  // with no wild win in between breaks it (alive while today <= day + 1).
  struct Chain {
    int16_t dex = 0;        // the chained species (0 = none)
    uint8_t len = 0;        // links, 1..WILD_CHAIN_MAX
    uint8_t best = 0;       // the longest chain ever reached
    uint32_t day = 0;       // the day of the last link
  };
  Chain ch;
  // The chain as it stands on `day`: 0 / 0 once it has broken.
  uint8_t chainLen(uint32_t day) const;
  int16_t chainDex(uint32_t day) const { return chainLen(day) ? ch.dex : 0; }

  void begin();             // load from NVS; a fight cut off by a reset is spent
  void save();

  // A new day refills the encounters. A creature merely MET yesterday leaves
  // with the day; one already beaten and waiting for a ball stays catchable.
  void newDay(uint32_t day);
  uint8_t left() const { return st.used >= WILD_PER_DAY ? 0 : WILD_PER_DAY - st.used; }

  // Puts today's next creature out if none is and any are left. Deterministic:
  // the day, which encounter of the day it is, the time window, the weather and
  // the region decide it. Emits EV_WILD_SEEN. Returns true if one is out.
  bool roll(uint32_t day, uint32_t epoch, uint8_t region, uint8_t petLevel, bool charm);
  // THE answer to "what is next": exactly what roll() would put out with the
  // same arguments, without writing or emitting anything. roll() is built on
  // it, so a peek (the main-screen alert) and the screen can never disagree.
  // False when nothing can come today.
  bool peek(uint32_t day, uint32_t epoch, uint8_t region, uint8_t petLevel, bool charm, WildNext &out) const;

  void fightStarted();      // MEET -> FIGHT (persisted before the battle starts)
  // From the battle: a win opens the catch (EV_WILD_WIN, maybe a berry -- the
  // item id is returned, 0 if none); a loss or a run spends the encounter.
  uint8_t battleEnded(bool won, bool ran, uint8_t dropRoll100, Bag &bg);
  void runAway();           // RUN from MEET, or LET GO during the catch: spent
  // Throw `ball`. roll100 is 0..99 (random(100) in the game, fixed in tests).
  // On a catch the creature goes to the party, else the box, is registered in
  // the Pokedex and emits EV_WILD_CATCH. If both are full NOTHING is spent.
  uint8_t throwBall(uint8_t ball, uint8_t roll100, Party &pt, Bag &bg, Pet &pp);
  // The once-only POKE BALL gift. True if it was given just now.
  bool giveGift(Bag &bg);

  void clearAll();          // tests / WIPE

private:
  void spend();
  void chainWin(int16_t dex, uint32_t day);
  void saveChain();
  Preferences prefs;
  bool opened = false;      // a Wild nobody begin()'d never writes
};

extern Wild wild;

// Called once from setup(), after pet/party/bag have loaded.
void wildBegin();
