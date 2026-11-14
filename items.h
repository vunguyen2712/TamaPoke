#pragma once
#include <Arduino.h>
#include <Preferences.h>

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
