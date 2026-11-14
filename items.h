#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include "party.h"

// Items and the bag.
//
// Ids are stored RAW in NVS (bag counts, and the item a creature holds), so the
// list is APPEND-ONLY, exactly like MOVES: inserting one in the middle would
// silently turn every saved Leftovers into something else. New items go at the
// end, before ITEM_COUNT, and ITEM_COUNT must stay <= BAG_SLOTS.
//
// Names are proper nouns and stay English in every language, like species and
// move names. ASCII only -- the font has no accents.
enum ItemId : uint8_t {
  ITEM_NONE = 0,
  // --- balls: thrown at a wild creature after beating it
  ITEM_POKEBALL,
  ITEM_GREATBALL,
  ITEM_ULTRABALL,
  // --- held in battle
  ITEM_ORAN_BERRY,     // heals 10 HP once, at half HP or below
  ITEM_SITRUS_BERRY,   // heals 25% once, at half HP or below
  ITEM_LUM_BERRY,      // cures an ailment once
  ITEM_LEFTOVERS,      // heals 1/16 at the end of every turn
  ITEM_FOCUS_SASH,     // survives one hit from full HP with 1 HP
  ITEM_QUICK_CLAW,     // 20% chance to move first
  // --- key items: never consumed, never held
  ITEM_SHINY_CHARM,    // better shiny odds; reward for a complete regional dex
  ITEM_OVAL_CHARM,     // better daycare eggs
  ITEM_COUNT
};

enum ItemKind : uint8_t { IK_NONE = 0, IK_BALL, IK_HELD, IK_KEY };

struct ItemInfo {
  const char *name;    // ASCII, <= 12 chars so it fits a row at text size 2
  ItemKind kind;
  uint8_t power;       // ball: catch multiplier x10; berry: heal amount; else 0
};

extern const ItemInfo ITEM_TBL[ITEM_COUNT];

#define BAG_SLOTS 32      // stored size; room to append items without a migration
#define BAG_MAX 99        // per item

class Bag {
public:
  void begin();                         // load from NVS ("bag")
  uint8_t count(uint8_t item) const;
  bool has(uint8_t item) const { return count(item) > 0; }
  // Adds up to BAG_MAX; returns how many actually went in. Saves.
  uint8_t add(uint8_t item, uint8_t n = 1);
  // Takes one; false if there was none. Key items are never taken. Saves.
  bool take(uint8_t item);
  void clearAll();                      // tests / WIPE
  void save();

private:
  uint8_t counts[BAG_SLOTS] = { 0 };
  Preferences prefs;
  bool opened = false;                  // a Bag nobody begin()'d never writes
};

extern Bag bag;

bool itemValid(uint8_t item);
bool itemIsHeld(uint8_t item);
bool itemIsBall(uint8_t item);

// ======================================================================
// Held items (feature 8). Design notes: docs/adventure-items.md.
//
// A creature holds at most one IK_HELD item. The live pet keeps it in
// Pet::held (NVS "hitm"), a banked one in PartyMon::held, and it travels with
// the creature: banking, bringing back, party<->box swaps. A creature that
// leaves for good gives its item back to the bag rather than taking it along.
//
// CONSUMPTION RULE: berries and the FOCUS SASH are spent the moment they
// trigger in a battle -- gone from the creature that held it, saved at once,
// whether the fight is then won, lost or run from. LEFTOVERS and the QUICK
// CLAW are never spent. Link battles carry no items at all.
// ======================================================================
class Pet;
struct Combatant;

// Who holds an item. One number for every creature the player owns, so the
// bag screen, the battle and the tests all name a creature the same way.
#define HOLDER_PET 0
#define HOLDER_PARTY(i) (1 + (i))
#define HOLDER_BOX(i) (1 + PARTY_SLOTS + (i))
#define HOLDER_COUNT (1 + PARTY_SLOTS + BOX_SLOTS)
#define HOLDER_NONE (-1)

bool itemConsumable(uint8_t item);      // berries and the sash
bool holderExists(const Pet &p, const Party &pt, uint8_t who);
uint8_t heldBy(const Pet &p, const Party &pt, uint8_t who);
// Takes one `item` out of the bag and gives it to `who`; whatever `who` held
// goes back into the bag. False (and nothing changes) if the bag has none, the
// item cannot be held, or there is no such creature. Saves both.
bool heldGive(Pet &p, Party &pt, uint8_t who, uint8_t item);
// Takes `who`'s item back into the bag. False if it held nothing or the bag
// has no room for one more of it. Saves both.
bool heldTake(Pet &p, Party &pt, uint8_t who);
// A creature leaving for good: its item goes back to the bag (silently dropped
// only if the bag is already at BAG_MAX of it). Clears `held`; the caller saves
// the record it belongs to.
void heldReturn(uint8_t &held);
// After a battle turn: if `c` spent a consumable, remove it from the creature
// it came from (Combatant::holder). Safe to call every turn.
void heldBattleSync(Pet &p, Party &pt, const Combatant &c);

// ======================================================================
// Friendship evolution. THE single answer to "can this creature evolve, into
// what, and if not why" -- Pet::canEvolveNow(), Pet::evolve() and the card's
// progress page all ask evoCheck(), so none can promise what another refuses.
// ======================================================================
#define FRIEND_BOND 50          // Pet::bond (0..100) a friendship evolution needs
#define DEX_ESPEON 196
#define DEX_UMBREON 197
// Umbreon's night, by the pet's RTC hour. Wider than the auto-sleep night
// (00-06): a creature asleep cannot evolve, so 00-06 alone would be unreachable.
#define FRIEND_NIGHT_START 18
#define FRIEND_NIGHT_END 6

enum : uint8_t {
  EVO_OK = 0,      // evolve() will do it now
  EVO_FINAL,       // final form (or an egg): nothing to evolve into
  EVO_LEVEL,       // not at the level yet (careMistakes and evoPen included)
  EVO_BOND,        // a friendship evolution, bond below FRIEND_BOND
  EVO_NO_ART,      // the target's pack is missing or it has no art
  EVO_CARE,        // a stat is below 40 right now
  EVO_STATE,       // asleep, frozen or mid-ceremony; otherwise ready
};

struct EvoCheck {
  uint8_t block = EVO_FINAL;
  int16_t target = 0;       // what it becomes; 0 = EEVEE's level branch, picked in evolve()
  uint16_t needLevel = 0;
  bool friendship = false;  // this evolution is gated on the bond
};

EvoCheck evoCheck(const Pet &p);
bool isFriendEvo(int16_t dex);           // species whose evolution needs the bond
bool friendIsNight(const Pet &p);        // Umbreon's half of the day
// EEVEE's branches WITHOUT a bond: eeveeOptions() minus Espeon/Umbreon, which
// only friendship reaches -- unless that leaves nothing, then all of them.
uint8_t eeveeLevelOptions(const Pet &p, int16_t *out);

// ======================================================================
// Pokedex rewards. Granted once each, remembered in NVS ("dxrw", "dxgf").
//   - a region's OBTAINABLE dex complete -> SHINY CHARM (once, ever)
//   - every DEX_GIFT_EVERY registrations  -> a small item gift
//   - DEX_OVAL_AT registrations           -> OVAL CHARM
// OBTAINABLE = can actually be registered on this card: has art, its region's
// pack is present, and it either hatches (rarity > 0) or is the evolution of
// something obtainable (evolvesTo, Eevee's branches). A region with nothing
// obtainable (pack missing) can never count as complete.
// ======================================================================
#define DEX_GIFT_EVERY 25
#define DEX_OVAL_AT 100
enum : uint8_t { DXRW_SHINY = 1 << 0, DXRW_OVAL = 1 << 1 };

void dexRewardsBegin(Pet *p);         // subscribe + catch up on an old save; once, from setup()
void dexRewardsCheck(const Pet &p);   // grants whatever is due; idempotent
bool dexObtainable(int16_t dex);
uint16_t regionObtainableCount(uint8_t r);
bool regionDexComplete(const Pet &p, uint8_t r);
uint8_t dexRewardFlags();
uint8_t dexGiftsGranted();
uint8_t dexGiftItem(uint8_t n, uint8_t *qty);   // the n-th gift (0-based)
void dexRewardsReset();               // tests: forget RAM state (NVS untouched)

// The shiny charm's effect on an egg's 1-in-`base` roll. Wild shinies apply
// it themselves (wild.cpp); eggs ask here.
int shinyCharmOdds(int base);
