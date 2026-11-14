#include "items.h"
#include "pet.h"
#include "battle.h"
#include "dex.h"
#include "noart.h"
#include "game_events.h"
#include <string.h>

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

// ======================================================================
// held items
// ======================================================================

bool itemConsumable(uint8_t item) {
  return item == ITEM_ORAN_BERRY || item == ITEM_SITRUS_BERRY ||
         item == ITEM_LUM_BERRY || item == ITEM_FOCUS_SASH;
}

// The record a holder number names, or nullptr. The live pet is not a
// PartyMon, so it is answered separately by the callers below.
static PartyMon *banked(Party &pt, uint8_t who) {
  if (who >= HOLDER_PARTY(0) && who < HOLDER_BOX(0)) return &pt.slots[who - HOLDER_PARTY(0)];
  if (who >= HOLDER_BOX(0) && who < HOLDER_COUNT) return &pt.box[who - HOLDER_BOX(0)];
  return nullptr;
}

bool holderExists(const Pet &p, const Party &pt, uint8_t who) {
  if (who == HOLDER_PET) return !p.isEgg();     // an egg holds nothing
  const PartyMon *m = banked(const_cast<Party &>(pt), who);
  return m && !m->empty();
}

uint8_t heldBy(const Pet &p, const Party &pt, uint8_t who) {
  if (!holderExists(p, pt, who)) return ITEM_NONE;
  if (who == HOLDER_PET) return p.held;
  return banked(const_cast<Party &>(pt), who)->held;
}

static void saveHolder(Pet &p, Party &pt, uint8_t who) {
  if (who == HOLDER_PET) p.saveNow();
  else if (who < HOLDER_BOX(0)) pt.save();
  else pt.boxSave();
}

static uint8_t *heldSlot(Pet &p, Party &pt, uint8_t who) {
  if (!holderExists(p, pt, who)) return nullptr;
  if (who == HOLDER_PET) return &p.held;
  return &banked(pt, who)->held;
}

bool heldGive(Pet &p, Party &pt, uint8_t who, uint8_t item) {
  uint8_t *h = heldSlot(p, pt, who);
  if (!h || !itemIsHeld(item) || !bag.has(item)) return false;
  if (*h == item) return true;                   // already holding one
  if (*h && bag.count(*h) >= BAG_MAX) return false;   // nowhere to put the old one
  bag.take(item);
  if (*h) bag.add(*h);
  *h = item;
  saveHolder(p, pt, who);
  return true;
}

bool heldTake(Pet &p, Party &pt, uint8_t who) {
  uint8_t *h = heldSlot(p, pt, who);
  if (!h || !*h) return false;
  if (!bag.add(*h)) return false;               // bag full of it: keep holding
  *h = ITEM_NONE;
  saveHolder(p, pt, who);
  return true;
}

void heldReturn(uint8_t &held) {
  if (itemIsHeld(held)) bag.add(held);
  held = ITEM_NONE;
}

void heldBattleSync(Pet &p, Party &pt, const Combatant &c) {
  if (!c.itemUsed || !itemConsumable(c.held)) return;
  if (c.holder < 0 || c.holder >= HOLDER_COUNT) return;   // a foe, or a link fight
  uint8_t *h = heldSlot(p, pt, (uint8_t)c.holder);
  // Only if it still holds THAT item: a second call after the first one
  // cleared it must not touch anything.
  if (!h || *h != c.held) return;
  *h = ITEM_NONE;
  saveHolder(p, pt, (uint8_t)c.holder);
}

// ======================================================================
// friendship evolution
// ======================================================================

// The species that evolve by friendship in the real games. dex.h gives each a
// LEVEL PROXY (evolveLevel) because DexEntry cannot say "friendship"; that
// level still applies, and the bond is required on top of it.
static const int16_t FRIEND_EVOS[] = {
  42,    // GOLBAT    -> CROBAT
  113,   // CHANSEY   -> BLISSEY
  172,   // PICHU     -> PIKACHU
  173,   // CLEFFA    -> CLEFAIRY
  174,   // IGGLYBUFF -> JIGGLYPUFF
  175,   // TOGEPI    -> TOGETIC
  298,   // AZURILL   -> MARILL
  406,   // BUDEW     -> ROSELIA
  427,   // BUNEARY   -> LOPUNNY
  433,   // CHINGLING -> CHIMECHO
  440,   // HAPPINY   -> CHANSEY
  446,   // MUNCHLAX  -> SNORLAX
  447,   // RIOLU     -> LUCARIO
  527,   // WOOBAT    -> SWOOBAT
  541,   // SWADLOON  -> LEAVANNY
  772,   // TYPE NULL -> SILVALLY
};
#define FRIEND_EVO_COUNT (sizeof(FRIEND_EVOS) / sizeof(FRIEND_EVOS[0]))

bool isFriendEvo(int16_t dex) {
  for (size_t i = 0; i < FRIEND_EVO_COUNT; i++)
    if (FRIEND_EVOS[i] == dex) return true;
  return false;
}

// Something a creature may turn into: it must be drawable on THIS card.
// Evolution is one-way, so becoming a creature that can only ever draw as a
// dex number is permanent -- the reason eeveeOptions() filters the same way.
static bool targetOk(int16_t d) {
  return d >= 1 && d <= DEX_COUNT && speciesHasArt(d) && regionAvailable(regionOfDex(d));
}

bool friendIsNight(const Pet &p) {
  if (!p.lastSeenEpoch) return false;           // clock never set: it is day
  int h = (int)((p.lastSeenEpoch / 3600) % 24);
  if (FRIEND_NIGHT_START < FRIEND_NIGHT_END) return h >= FRIEND_NIGHT_START && h < FRIEND_NIGHT_END;
  return h >= FRIEND_NIGHT_START || h < FRIEND_NIGHT_END;
}

uint8_t eeveeLevelOptions(const Pet &p, int16_t *out) {
  int16_t all[EEVEE_EVO_COUNT];
  uint8_t n = p.eeveeOptions(all), m = 0;
  for (uint8_t i = 0; i < n; i++)
    if (all[i] != DEX_ESPEON && all[i] != DEX_UMBREON) out[m++] = all[i];
  if (m) return m;
  for (uint8_t i = 0; i < n; i++) out[i] = all[i];   // only those two on the card
  return n;
}

EvoCheck evoCheck(const Pet &p) {
  EvoCheck e;
  if (p.isEgg() || p.speciesId > DEX_COUNT) return e;          // EVO_FINAL
  const DexEntry &d = DEX_TBL[p.speciesId];
  if (d.evolvesTo == 0) return e;
  bool bonded = p.bond >= FRIEND_BOND;
  if (p.speciesId == DEX_EEVEE) {
    // A bonded Eevee takes the branch of the hour, if this card can draw it;
    // otherwise (or unbonded) evolve() picks one of the level branches.
    int16_t want = friendIsNight(p) ? DEX_UMBREON : DEX_ESPEON;
    if (bonded && targetOk(want)) { e.target = want; e.friendship = true; }
  } else if (isFriendEvo(p.speciesId)) {
    e.friendship = true;
    e.target = (int16_t)d.evolvesTo;
  } else {
    e.target = (int16_t)d.evolvesTo;     // every other species: exactly as before
  }
  // evoPen is the day owed for retiring the PREVIOUS creature early. It rides
  // on the same threshold careMistakes already moves, so there is one rule for
  // "this creature evolves later" rather than two that can disagree.
  e.needLevel = (uint16_t)(d.evolveLevel + p.careMistakes + p.evoPenalty());
  if (p.level() < e.needLevel) { e.block = EVO_LEVEL; return e; }
  if (e.friendship && !bonded) { e.block = EVO_BOND; return e; }
  if (e.friendship && !targetOk(e.target)) { e.block = EVO_NO_ART; return e; }
  if (p.lowestStat() < 40) { e.block = EVO_CARE; return e; }
  if (p.frozen || p.sleeping || p.ceremony != CER_NONE) { e.block = EVO_STATE; return e; }
  e.block = EVO_OK;
  return e;
}

// ======================================================================
// Pokedex rewards
// ======================================================================

static Preferences rwPrefs;
static bool rwOpen = false;
static uint8_t rwFlags = 0, rwGifts = 0;
static Pet *rwPet = nullptr;

// What the n-th gift is: a cycle through the battle items, berries first.
uint8_t dexGiftItem(uint8_t n, uint8_t *qty) {
  static const uint8_t GIFT[][2] = {
    { ITEM_ORAN_BERRY, 3 }, { ITEM_SITRUS_BERRY, 2 }, { ITEM_LUM_BERRY, 2 },
    { ITEM_QUICK_CLAW, 1 }, { ITEM_FOCUS_SASH, 1 },   { ITEM_LEFTOVERS, 1 },
  };
  const uint8_t k = sizeof(GIFT) / sizeof(GIFT[0]);
  if (qty) *qty = GIFT[n % k][1];
  return GIFT[n % k][0];
}

// One bit per species: can this card's player actually register it?
// A fixpoint, because an evolution can point BACKWARDS in the dex (HAPPINY 440
// -> CHANSEY 113) and a chain can be several links long.
static void obtainableBits(uint8_t *bits) {
  memset(bits, 0, (DEX_COUNT + 8) / 8);
  auto set = [&](int16_t d) -> bool {
    if (!targetOk(d)) return false;
    uint8_t &b = bits[d >> 3];
    uint8_t m = (uint8_t)(1 << (d & 7));
    if (b & m) return false;
    b |= m;
    return true;
  };
  auto has = [&](int16_t d) { return (bits[d >> 3] >> (d & 7)) & 1; };
  for (int16_t d = 1; d <= DEX_COUNT; d++)
    if (DEX_TBL[d].rarity != R_EVO) set(d);   // hatches from an egg
  for (bool grew = true; grew;) {
    grew = false;
    for (int16_t d = 1; d <= DEX_COUNT; d++) {
      if (!has(d)) continue;
      if (DEX_TBL[d].evolvesTo) grew |= set((int16_t)DEX_TBL[d].evolvesTo);
      if (d == DEX_EEVEE)
        for (uint8_t i = 0; i < EEVEE_EVO_COUNT; i++) grew |= set(EEVEE_EVOS[i]);
    }
  }
}

bool dexObtainable(int16_t dex) {
  if (dex < 1 || dex > DEX_COUNT) return false;
  uint8_t bits[(DEX_COUNT + 8) / 8];
  obtainableBits(bits);
  return (bits[dex >> 3] >> (dex & 7)) & 1;
}

uint16_t regionObtainableCount(uint8_t r) {
  if (r >= REGION_COUNT || r == REGION_ALL) return 0;
  uint8_t bits[(DEX_COUNT + 8) / 8];
  obtainableBits(bits);
  uint16_t n = 0;
  for (int16_t d = REGIONS[r].lo; d <= (int16_t)REGIONS[r].hi && d <= DEX_COUNT; d++)
    if ((bits[d >> 3] >> (d & 7)) & 1) n++;
  return n;
}

static bool regionCompleteWith(const Pet &p, uint8_t r, const uint8_t *bits) {
  if (r >= REGION_COUNT || r == REGION_ALL) return false;
  uint16_t need = 0;
  for (int16_t d = REGIONS[r].lo; d <= (int16_t)REGIONS[r].hi && d <= DEX_COUNT; d++) {
    if (!((bits[d >> 3] >> (d & 7)) & 1)) continue;
    need++;
    if (!p.isRegistered(d)) return false;
  }
  return need > 0;     // nothing obtainable (pack missing) is never "complete"
}

bool regionDexComplete(const Pet &p, uint8_t r) {
  uint8_t bits[(DEX_COUNT + 8) / 8];
  obtainableBits(bits);
  return regionCompleteWith(p, r, bits);
}

uint8_t dexRewardFlags() { return rwFlags; }
uint8_t dexGiftsGranted() { return rwGifts; }

static void rwSave() {
  if (!rwOpen) return;
  rwPrefs.putUChar("dxrw", rwFlags);
  rwPrefs.putUChar("dxgf", rwGifts);
}

void dexRewardsCheck(const Pet &p) {
  bool changed = false;
  uint16_t n = p.registeredCount();
  // every DEX_GIFT_EVERY registrations; a save that is already past several
  // milestones is paid every one it is owed, once
  while ((uint16_t)(rwGifts + 1) * DEX_GIFT_EVERY <= n && rwGifts < 255) {
    uint8_t qty = 1;
    uint8_t it = dexGiftItem(rwGifts, &qty);
    bag.add(it, qty);
    rwGifts++;
    changed = true;
  }
  if (!(rwFlags & DXRW_OVAL) && n >= DEX_OVAL_AT) {
    bag.add(ITEM_OVAL_CHARM);
    rwFlags |= DXRW_OVAL;
    changed = true;
  }
  if (!(rwFlags & DXRW_SHINY)) {
    uint8_t bits[(DEX_COUNT + 8) / 8];
    obtainableBits(bits);
    for (uint8_t r = 0; r < REGION_COUNT; r++) {
      if (!regionCompleteWith(p, r, bits)) continue;
      bag.add(ITEM_SHINY_CHARM);
      rwFlags |= DXRW_SHINY;
      changed = true;
      break;
    }
  }
  if (changed) rwSave();
}

static void dexRewardSink(GameEv ev, int16_t) {
  if (ev == EV_DEX_REGISTER && rwPet) dexRewardsCheck(*rwPet);
}

void dexRewardsBegin(Pet *p) {
  rwPrefs.begin("tamapoke", false);
  rwOpen = true;
  rwFlags = rwPrefs.getUChar("dxrw", 0) & (DXRW_SHINY | DXRW_OVAL);
  rwGifts = rwPrefs.getUChar("dxgf", 0);
  rwPet = p;
  gameEventSubscribe(dexRewardSink);
  if (p) dexRewardsCheck(*p);     // an existing save may already have earned some
}

void dexRewardsReset() { rwFlags = 0; rwGifts = 0; rwPet = nullptr; rwOpen = false; }

// 1 in `base` becomes 1 in base/2 with the charm (the egg floor of 8 becomes 4).
int shinyCharmOdds(int base) {
  if (!bag.has(ITEM_SHINY_CHARM)) return base;
  base /= 2;
  return base < 4 ? 4 : base;
}
