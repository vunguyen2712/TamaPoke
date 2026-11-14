#include "items.h"

// APPEND-ONLY, in ItemId order. See items.h.
const ItemInfo ITEM_TBL[ITEM_COUNT] = {
  { "-",           IK_NONE, 0 },
  { "POKE BALL",   IK_BALL, 10 },
  { "GREAT BALL",  IK_BALL, 15 },
  { "ULTRA BALL",  IK_BALL, 20 },
  { "ORAN BERRY",  IK_HELD, 10 },
  { "SITRUS BERRY",IK_HELD, 25 },
  { "LUM BERRY",   IK_HELD, 0 },
  { "LEFTOVERS",   IK_HELD, 0 },
  { "FOCUS SASH",  IK_HELD, 0 },
  { "QUICK CLAW",  IK_HELD, 20 },
  { "SHINY CHARM", IK_KEY,  0 },
  { "OVAL CHARM",  IK_KEY,  0 },
};
static_assert(ITEM_COUNT <= BAG_SLOTS, "the bag blob has no room for this many items");

Bag bag;

bool itemValid(uint8_t item) { return item > ITEM_NONE && item < ITEM_COUNT; }
bool itemIsHeld(uint8_t item) { return itemValid(item) && ITEM_TBL[item].kind == IK_HELD; }
bool itemIsBall(uint8_t item) { return itemValid(item) && ITEM_TBL[item].kind == IK_BALL; }

void Bag::begin() {
  prefs.begin("tamapoke", false);
  opened = true;
  memset(counts, 0, sizeof(counts));
  size_t have = prefs.getBytesLength("bag");
  if (have && have <= sizeof(counts)) prefs.getBytes("bag", counts, sizeof(counts));
  // Never trust a stored count: unknown ids are dropped, the rest clamped.
  for (uint8_t i = 0; i < BAG_SLOTS; i++) {
    if (!itemValid(i)) counts[i] = 0;
    else if (counts[i] > BAG_MAX) counts[i] = BAG_MAX;
  }
}

uint8_t Bag::count(uint8_t item) const {
  return itemValid(item) ? counts[item] : 0;
}

uint8_t Bag::add(uint8_t item, uint8_t n) {
  if (!itemValid(item) || !n) return 0;
  uint8_t room = BAG_MAX - counts[item];
  if (ITEM_TBL[item].kind == IK_KEY) room = counts[item] ? 0 : 1;   // key items: one
  uint8_t got = n < room ? n : room;
  counts[item] += got;
  if (got) save();
  return got;
}

bool Bag::take(uint8_t item) {
  if (!itemValid(item) || !counts[item]) return false;
  if (ITEM_TBL[item].kind == IK_KEY) return true;   // owned, never spent
  counts[item]--;
  save();
  return true;
}

void Bag::clearAll() {
  memset(counts, 0, sizeof(counts));
  save();
}

void Bag::save() {
  if (!opened) return;
  prefs.putBytes("bag", counts, sizeof(counts));
}
