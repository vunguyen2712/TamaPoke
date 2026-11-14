#include "breed.h"
#include "pet.h"
#include "party.h"
#include "items.h"
#include "dex.h"
#include "noart.h"
#include "game_events.h"
#include <Preferences.h>

// See breed.h for the rules and docs/adventure-daycare.md for why.

// ---------------------------------------------------------------- the rule

// The species dex evolves FROM, or 0 for a first form. EEVEE's seven extra
// branches are not in evolvesTo (a single field) but in EEVEE_EVOS.
static int16_t prevOf(int16_t dex) {
  for (uint8_t i = 0; i < EEVEE_EVO_COUNT; i++)
    if (EEVEE_EVOS[i] == dex) return DEX_EEVEE;
  for (int16_t d = 1; d <= DEX_COUNT; d++)
    if (DEX_TBL[d].evolvesTo == (uint16_t)dex) return d;
  return 0;
}

int16_t breedBaseOf(int16_t dex) {
  if (dex < 1 || dex > DEX_COUNT) return 0;
  int16_t cur = dex;
  for (int guard = 0; guard < 6; guard++) {   // no line is longer than 3
    int16_t p = prevOf(cur);
    if (p < 1) break;
    cur = p;
  }
  return cur;
}

// Any member of the line, walked from its base: PHIONE is legendary while
// MANAPHY (its "evolution" in this table) is not, and either must refuse.
bool breedLineLegendary(int16_t dex) {
  int16_t cur = breedBaseOf(dex);
  for (int guard = 0; cur >= 1 && cur <= DEX_COUNT && guard < 6; guard++) {
    if (DEX_TBL[cur].rarity == R_LEGENDARIO) return true;
    cur = DEX_TBL[cur].evolvesTo;
  }
  return dex >= 1 && dex <= DEX_COUNT && DEX_TBL[dex].rarity == R_LEGENDARIO;
}

static bool shareType(int16_t a, int16_t b) {
  const DexEntry &x = DEX_TBL[a], &y = DEX_TBL[b];
  uint8_t tx[2] = { x.type1, x.type2 }, ty[2] = { y.type1, y.type2 };
  for (int i = 0; i < 2; i++)
    for (int j = 0; j < 2; j++)
      if (tx[i] != T_NONE && tx[i] == ty[j]) return true;
  return false;
}

// The same test pickEggSpecies() applies to its candidates, so a bred egg can
// never be something the ordinary egg pool would refuse: evolution-only forms
// (rarity R_EVO), legendaries, art-less species, a region without its pack.
static bool eggable(int16_t d) {
  if (d < 1 || d > DEX_COUNT) return false;
  uint8_t r = DEX_TBL[d].rarity;
  if (r != R_COMUN && r != R_RARO) return false;
  if (!speciesHasArt(d)) return false;
  return regionAvailable(regionOfDex(d));
}

static bool validMon(const PartyMon &m) { return m.dex >= 1 && m.dex <= DEX_COUNT; }

BreedVerdict breedCheck(const PartyMon &a, const PartyMon &b) {
  if (!validMon(a) || !validMon(b)) return BV_EMPTY;
  if (&a == &b) return BV_SAME;
  bool da = a.dex == BREED_DITTO, db = b.dex == BREED_DITTO;
  if (da && db) return BV_TWO_DITTO;
  if (breedLineLegendary(a.dex) || breedLineLegendary(b.dex)) return BV_LEGEND;
  if (!da && !db && !shareType(a.dex, b.dex)) return BV_NO_TYPE;
  int16_t egg = breedBaseOf(da ? b.dex : a.dex);
  if (!eggable(egg)) return BV_NO_EGG;
  return BV_OK;
}

int16_t breedEggSpecies(const PartyMon &a, const PartyMon &b) {
  if (breedCheck(a, b) != BV_OK) return 0;
  return breedBaseOf(a.dex == BREED_DITTO ? b.dex : a.dex);
}

void breedInherit(const PartyMon &a, const PartyMon &b, bool ovalCharm, uint8_t out[4]) {
  const uint8_t pa[4] = { a.ivAtk, a.ivDef, a.ivSpe, a.ivHp };
  const uint8_t pb[4] = { b.ivAtk, b.ivDef, b.ivSpe, b.ivHp };
  if (ovalCharm) {   // all four, the better parent each time
    for (int i = 0; i < 4; i++) out[i] = pa[i] > pb[i] ? pa[i] : pb[i];
  } else {           // BREED_INHERIT of the four, each from a random parent
    uint8_t order[4] = { 0, 1, 2, 3 };
    for (int k = 3; k > 0; k--) {
      int j = random(k + 1);
      uint8_t t = order[k]; order[k] = order[j]; order[j] = t;
    }
    for (int i = 0; i < 4; i++) out[i] = BREED_IV_ROLL;
    for (int k = 0; k < BREED_INHERIT; k++) {
      uint8_t s = order[k];
      out[s] = random(2) ? pa[s] : pb[s];
    }
  }
  // A parent's IV is a stored byte: never trust it past the range a hatch gives.
  for (int i = 0; i < 4; i++)
    if (out[i] != BREED_IV_ROLL && out[i] > 31) out[i] = 31;
}

// ---------------------------------------------------------------- the state

static BredEgg gQueued;           // made at the daycare, waiting for newEgg()
static BredEgg gLive;             // taken by newEgg(): the egg on screen now
static uint32_t gLastDay = 0;
static bool gBredEver = false;    // day 0 (no clock) still allows a first breed
static bool gOpened = false;      // never write NVS unless begun
static Preferences gPrefs;

// "dcare" layout, little-endian, APPEND-ONLY:
//  [0] version 1
//  [1..2] queued dex  [3..6] queued IVs  [7] queued shiny  [8..9] pa  [10..11] pb
//  [12..13] live dex  [14..17] live IVs
//  [18..21] last breed day  [22] bred ever
#define DC_LEN 23

static void put16(uint8_t *p, int16_t v) { p[0] = (uint8_t)(v & 0xFF); p[1] = (uint8_t)((uint16_t)v >> 8); }
static int16_t get16(const uint8_t *p) { return (int16_t)(p[0] | (p[1] << 8)); }

static void saveState() {
  if (!gOpened) return;
  uint8_t b[DC_LEN] = { 0 };
  b[0] = 1;
  put16(b + 1, gQueued.dex);
  memcpy(b + 3, gQueued.iv, 4);
  b[7] = gQueued.shiny;
  put16(b + 8, gQueued.pa);
  put16(b + 10, gQueued.pb);
  put16(b + 12, gLive.dex);
  memcpy(b + 14, gLive.iv, 4);
  for (int i = 0; i < 4; i++) b[18 + i] = (uint8_t)(gLastDay >> (8 * i));
  b[22] = gBredEver ? 1 : 0;
  gPrefs.putBytes("dcare", b, sizeof(b));
}

static bool sane(BredEgg &e) {
  if (e.dex < 1 || e.dex > DEX_COUNT) { e = BredEgg(); return false; }
  for (int i = 0; i < 4; i++)
    if (e.iv[i] != BREED_IV_ROLL && e.iv[i] > 31) e.iv[i] = 31;
  if (e.pa < 0 || e.pa > DEX_COUNT) e.pa = 0;
  if (e.pb < 0 || e.pb > DEX_COUNT) e.pb = 0;
  e.shiny = e.shiny ? 1 : 0;
  return true;
}

void breedBegin() {
  gPrefs.begin("tamapoke", false);
  gOpened = true;
  gQueued = BredEgg();
  gLive = BredEgg();
  gLastDay = 0;
  gBredEver = false;
  uint8_t b[DC_LEN] = { 0 };
  size_t have = gPrefs.getBytesLength("dcare");
  if (!have) return;
  if (have <= sizeof(b)) gPrefs.getBytes("dcare", b, sizeof(b));
  else {   // a longer, newer layout: keep the prefix we understand
    uint8_t *tmp = (uint8_t *)malloc(have);
    if (!tmp) return;
    if (gPrefs.getBytes("dcare", tmp, have) == have) memcpy(b, tmp, sizeof(b));
    free(tmp);
  }
  if (b[0] < 1) return;
  gQueued.dex = get16(b + 1);
  memcpy(gQueued.iv, b + 3, 4);
  gQueued.shiny = b[7];
  gQueued.pa = get16(b + 8);
  gQueued.pb = get16(b + 10);
  sane(gQueued);
  gLive.dex = get16(b + 12);
  memcpy(gLive.iv, b + 14, 4);
  sane(gLive);
  gLastDay = (uint32_t)b[18] | ((uint32_t)b[19] << 8) | ((uint32_t)b[20] << 16) | ((uint32_t)b[21] << 24);
  gBredEver = b[22] != 0;
}

// The hooks are called from pet.cpp for the player's own Pet, which may run
// before setup() reaches breedBegin() (a first boot's newEgg()). Load on demand.
static void ensureOpen() { if (!gOpened) breedBegin(); }

void breedClearAll() {
  ensureOpen();
  gQueued = BredEgg();
  gLive = BredEgg();
  gLastDay = 0;
  gBredEver = false;
  saveState();
}

const PartyMon *breedBanked(uint8_t idx) {
  if (idx < PARTY_SLOTS) return &party.slots[idx];
  if (idx < BREED_BANK) return &party.box[idx - PARTY_SLOTS];
  return nullptr;
}

BreedVerdict breedCheckIdx(uint8_t ia, uint8_t ib) {
  const PartyMon *a = breedBanked(ia), *b = breedBanked(ib);
  if (!a || !b) return BV_EMPTY;
  return breedCheck(*a, *b);
}

bool breedOnCooldown(uint32_t day) { return gBredEver && day == gLastDay; }

bool breedHasQueued() { return gQueued.dex >= 1; }
const BredEgg &breedQueued() { return gQueued; }
const BredEgg &breedLive() { return gLive; }
bool breedQueuedHatchable() { return breedHasQueued() && eggable(gQueued.dex); }

BreedResult breedMake(uint8_t ia, uint8_t ib, uint32_t day) {
  if (!gOpened) return BR_NOT_READY;
  if (breedCheckIdx(ia, ib) != BV_OK) return BR_BAD_PAIR;
  if (breedHasQueued()) return BR_QUEUE_FULL;
  if (breedOnCooldown(day)) return BR_COOLDOWN;
  const PartyMon &a = *breedBanked(ia), &b = *breedBanked(ib);
  BredEgg e;
  e.dex = breedEggSpecies(a, b);
  breedInherit(a, b, bag.has(ITEM_OVAL_CHARM), e.iv);
  e.shiny = (a.shiny && b.shiny && random(BREED_SHINY_BONUS) == 0) ? 1 : 0;
  e.pa = a.dex;
  e.pb = b.dex;
  gQueued = e;
  gLastDay = day;
  gBredEver = true;
  saveState();
  gameEvent(EV_BREED, e.dex);
  return BR_OK;
}

bool breedCanSwapIn(const Pet &p) {
  return breedQueuedHatchable() && p.isEgg() && p.eggCracks() == 0 &&
         p.ceremony == CER_NONE && !p.awaitingStarter();
}

bool breedSwapIn(Pet &p) {
  if (!breedCanSwapIn(p)) return false;
  BredEgg e = gQueued;
  if (!p.eggSwapTo(e.dex, e.shiny != 0)) return false;
  gLive = e;
  gQueued = BredEgg();
  saveState();
  return true;
}

bool breedTakeQueued(int16_t &dex, bool &shiny) {
  ensureOpen();
  bool dirty = gLive.dex != 0;
  gLive = BredEgg();               // whatever egg that described is gone now
  // A species whose pack has left the card waits in the queue instead of
  // hatching as a number; the daycare screen says why.
  if (!breedQueuedHatchable()) { if (dirty) saveState(); return false; }
  gLive = gQueued;
  gQueued = BredEgg();
  saveState();
  dex = gLive.dex;
  shiny = gLive.shiny != 0;
  return true;
}

bool breedTakeLive(int16_t dex, uint8_t out[4]) {
  ensureOpen();
  if (gLive.dex < 1) return false;
  bool match = gLive.dex == dex;   // the console's EGG <dex> can replace it
  if (match) memcpy(out, gLive.iv, 4);
  gLive = BredEgg();
  saveState();
  return match;
}
