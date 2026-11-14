#include "wild.h"
#include "pet.h"
#include "party.h"
#include "items.h"
#include "dex.h"
#include "noart.h"
#include "game_events.h"

Wild wild;

void wildBegin() { wild.begin(); }

// ---------- deterministic randomness ----------
// The daily encounters must be the same every time the same day is looked at
// (leaving and coming back must not re-roll them), so they come from a hash of
// the inputs, never from random(). The catch roll and the IVs DO use random():
// those are the luck of the moment, not the content of the day.
static uint32_t mix32(uint32_t x) {
  x ^= x >> 16; x *= 0x7FEB352Du;
  x ^= x >> 15; x *= 0x846CA68Bu;
  x ^= x >> 16;
  return x;
}

// ---------- weather and time ----------
// Weather weights out of 12: CLEAR 3, SUNNY 3, RAIN 2, SNOW 1, FOG 1, WIND 2.
static const uint8_t WX_WEIGHT[WX_COUNT] = { 3, 3, 2, 1, 1, 2 };

uint8_t wildWeatherOf(uint32_t day) {
  if (day == 0) return WX_CLEAR;            // no clock: no weather to speak of
  uint16_t total = 0;
  for (uint8_t i = 0; i < WX_COUNT; i++) total += WX_WEIGHT[i];
  uint16_t r = (uint16_t)(mix32(day * 2654435761u + 0x5EED) % total);
  for (uint8_t i = 0; i < WX_COUNT; i++) {
    if (r < WX_WEIGHT[i]) return i;
    r -= WX_WEIGHT[i];
  }
  return WX_CLEAR;
}

// Same formula as Pet::isNightHour(): the hour of lastSeenEpoch, UTC as the
// RTC keeps it. With no clock at all it is midday, the neutral window.
uint8_t wildHourOf(uint32_t epoch) {
  if (!epoch) return 12;
  return (uint8_t)((epoch / 3600) % 24);
}

// MORNING 06-10, DAY 11-16, EVENING 17-19, NIGHT 20-05.
uint8_t wildTimeOfHour(uint8_t h) {
  if (h >= 6 && h <= 10) return WT_MORNING;
  if (h >= 11 && h <= 16) return WT_DAY;
  if (h >= 17 && h <= 19) return WT_EVENING;
  return WT_NIGHT;
}

// ---------- who can spawn ----------
struct WildRule {
  uint8_t fav[4];      // types multiplied by `favMul`
  uint8_t dis[2];      // types halved
  uint8_t favMul;
  uint8_t biome;       // DexEntry::biome doubled (255 = none)
};
#define TN T_NONE
// Biome: 0 meadow 1 beach 2 forest 3 volcano 4 mountain 5 snow
static const WildRule WX_RULE[WX_COUNT] = {
  { { T_NORMAL, TN, TN, TN },               { TN, TN },          3, 0 },    // CLEAR
  { { T_FIRE, T_GRASS, T_GROUND, TN },      { T_ICE, T_WATER },  3, 3 },    // SUNNY
  { { T_WATER, T_ELECTRIC, T_BUG, TN },     { T_FIRE, T_ROCK },  3, 1 },    // RAIN
  { { T_ICE, T_STEEL, TN, TN },             { T_FIRE, T_GRASS }, 4, 5 },    // SNOW
  { { T_GHOST, T_PSYCHIC, T_POISON, TN },   { T_FLYING, TN },    3, 2 },    // FOG
  { { T_FLYING, T_DRAGON, TN, TN },         { T_BUG, TN },       3, 4 },    // WIND
};
static const WildRule WT_RULE[WT_COUNT] = {
  { { T_NORMAL, T_BUG, T_GRASS, T_FLYING }, { T_DARK, T_GHOST }, 2, 255 },  // MORNING
  { { T_FIRE, T_FIGHTING, T_GROUND, T_ROCK }, { TN, TN },        2, 255 },  // DAY
  { { T_PSYCHIC, T_FAIRY, T_NORMAL, TN },   { TN, TN },          2, 255 },  // EVENING
  { { T_GHOST, T_DARK, T_POISON, TN },      { T_GRASS, T_BUG },  3, 255 },  // NIGHT
};
#undef TN

static bool hasType(const DexEntry &e, uint8_t t) {
  return t != T_NONE && (e.type1 == t || e.type2 == t);
}
static bool hasAny(const DexEntry &e, const uint8_t *ts, uint8_t n) {
  for (uint8_t i = 0; i < n; i++) if (hasType(e, ts[i])) return true;
  return false;
}

// Predecessors, built once. DexEntry stores only evolvesTo, so this inverts
// it; Eevee's other seven branches are not in the table and come from
// EEVEE_EVOS. A second branch of any OTHER split evolution (Bellossom,
// Politoed, Gallade...) has no predecessor here and so never spawns wild --
// there is no level to gate it on, and no rule is better than a made-up one.
static int16_t gPrev[DEX_COUNT + 1];
static bool gPrevBuilt = false;
static void buildPrev() {
  if (gPrevBuilt) return;
  for (int16_t d = 0; d <= DEX_COUNT; d++) gPrev[d] = 0;
  for (int16_t d = 1; d <= DEX_COUNT; d++) {
    uint16_t to = DEX_TBL[d].evolvesTo;
    if (to >= 1 && to <= DEX_COUNT && !gPrev[to]) gPrev[to] = d;
  }
  for (uint8_t i = 0; i < EEVEE_EVO_COUNT; i++) {
    int16_t b = EEVEE_EVOS[i];
    if (b >= 1 && b <= DEX_COUNT) gPrev[b] = DEX_EEVEE;
  }
  gPrevBuilt = true;
}

int16_t wildPredecessor(int16_t dex) {
  if (dex < 1 || dex > DEX_COUNT) return 0;
  buildPrev();
  return gPrev[dex];
}

// The rarity of the line's base form (what an egg would hold).
static uint8_t rootRarity(int16_t dex) {
  int16_t cur = dex;
  for (int guard = 0; guard < 6; guard++) {
    int16_t p = wildPredecessor(cur);
    if (!p) break;
    cur = p;
  }
  return DEX_TBL[cur].rarity;
}

bool wildEligible(int16_t dex, uint8_t region, uint8_t level) {
  if (dex < 1 || dex > DEX_COUNT) return false;
  if (region < REGION_COUNT && region != REGION_ALL && regionAvailable(region)) {
    if (dex < REGIONS[region].lo || dex > REGIONS[region].hi) return false;
  }
  // else: REGION_ALL, or the player's region lost its pack -- like the egg,
  // the choice is kept and the roll falls through to whatever is playable
  if (!regionAvailable(regionOfDex(dex))) return false;
  if (!speciesHasArt(dex)) return false;
  const DexEntry &e = DEX_TBL[dex];
  if (e.rarity == R_LEGENDARIO) return false;
  if (e.rarity == R_EVO) {
    int16_t p = wildPredecessor(dex);
    if (!p) return false;
    uint8_t need = DEX_TBL[p].evolveLevel;
    if (!need || level < need) return false;
    uint8_t root = rootRarity(dex);
    if (root != R_COMUN && root != R_RARO) return false;
  }
  return true;
}

bool wildTimeAllows(int16_t dex, uint8_t weather, uint8_t tod) {
  if (dex < 1 || dex > DEX_COUNT) return false;
  if (hasType(DEX_TBL[dex], T_GHOST)) return tod == WT_NIGHT || weather == WX_FOG;
  return true;
}

static uint16_t baseWeight(int16_t dex) {
  uint8_t r = DEX_TBL[dex].rarity;
  if (r == R_COMUN) return 12;
  if (r == R_RARO) return 3;
  if (r == R_EVO) return rootRarity(dex) == R_RARO ? 1 : 4;
  return 0;
}

static uint32_t applyRule(uint32_t w, const DexEntry &e, const WildRule &rl) {
  if (hasAny(e, rl.fav, 4)) w *= rl.favMul;
  if (hasAny(e, rl.dis, 2)) w /= 2;
  if (rl.biome != 255 && e.biome == rl.biome) w *= 2;
  return w;
}

uint16_t wildWeight(int16_t dex, uint8_t weather, uint8_t tod) {
  if (dex < 1 || dex > DEX_COUNT) return 0;
  if (!wildTimeAllows(dex, weather, tod)) return 0;
  const DexEntry &e = DEX_TBL[dex];
  uint32_t w = (uint32_t)baseWeight(dex) * 4;   // x4 so a halving never hits 0
  w = applyRule(w, e, WX_RULE[weather % WX_COUNT]);
  w = applyRule(w, e, WT_RULE[tod % WT_COUNT]);
  return w > 60000 ? 60000 : (uint16_t)w;
}

int16_t wildRollSpecies(uint8_t region, uint8_t weather, uint8_t tod, uint8_t level, uint32_t seed) {
  return wildRollSpeciesChained(region, weather, tod, level, seed, 0);
}

int16_t wildRollSpeciesChained(uint8_t region, uint8_t weather, uint8_t tod, uint8_t level, uint32_t seed,
                               int16_t chainDex) {
  uint32_t others = 0, own = 0;
  for (int16_t d = 1; d <= DEX_COUNT; d++) {
    if (!wildEligible(d, region, level)) continue;
    uint16_t w = wildWeight(d, weather, tod);
    if (d == chainDex) own = w; else others += w;
  }
  // The chain boost: a chained species that can spawn now weighs as much as
  // everything else together -- half the roll. Without it a chain in a pool of
  // hundreds would be a lottery, not a hunt.
  uint32_t boosted = own;
  if (own && others > own) boosted = others;
  uint32_t total = others + boosted;
  if (!total) return 0;
  uint32_t r = mix32(seed) % total;
  for (int16_t d = 1; d <= DEX_COUNT; d++) {
    if (!wildEligible(d, region, level)) continue;
    uint32_t w = (d == chainDex) ? boosted : wildWeight(d, weather, tod);
    if (r < w) return d;
    r -= w;
  }
  return 0;
}

uint16_t wildShinyNumer(uint8_t chainLen, bool charm) {
  uint16_t n = 1u + chainLen;
  if (n > WILD_CHAIN_SHINY_CAP) n = WILD_CHAIN_SHINY_CAP;
  return charm ? n * 2 : n;
}

bool wildConditional(int16_t dex) {
  if (dex < 1 || dex > DEX_COUNT) return false;
  for (uint8_t wx = 0; wx < WX_COUNT; wx++)
    for (uint8_t t = 0; t < WT_COUNT; t++)
      if (!wildTimeAllows(dex, wx, t)) return true;
  return false;
}

bool wildIsRare(int16_t dex) {
  if (dex < 1 || dex > DEX_COUNT) return false;
  uint8_t r = DEX_TBL[dex].rarity;
  return r == R_RARO || (r == R_EVO && rootRarity(dex) == R_RARO);
}

uint8_t wildAlertWhy(int16_t dex, bool shiny, bool registered) {
  if (dex < 1 || dex > DEX_COUNT) return 0;
  uint8_t why = 0;
  if (wildIsRare(dex)) why |= WA_RARE;
  if (shiny) why |= WA_SHINY;
  if (wildConditional(dex)) why |= WA_COND;
  if (!registered) why |= WA_NEW;
  return why;
}

uint8_t wildLevelFor(uint8_t petLevel, uint32_t seed) {
  int l = (int)petLevel - 5 + (int)(mix32(seed ^ 0x1E7E1u) % 7);   // -5..+1
  if (l < 2) l = 2;
  if (l > MAX_LEVEL) l = MAX_LEVEL;
  return (uint8_t)l;
}

// Percent before the ball: common 45, rare 20; an evolved form of a common
// line 30, of a rare line 13. Times the ball's power/10, capped at 90.
uint8_t wildCatchChance(int16_t dex, uint8_t ball) {
  if (!itemIsBall(ball) || dex < 1 || dex > DEX_COUNT) return 0;
  uint8_t r = DEX_TBL[dex].rarity;
  uint32_t base;
  if (r == R_COMUN) base = 45;
  else if (r == R_RARO) base = 20;
  else if (r == R_EVO) base = rootRarity(dex) == R_RARO ? 13 : 30;
  else base = 5;
  uint32_t c = base * ITEM_TBL[ball].power / 10;
  return c > WILD_CATCH_CAP ? WILD_CATCH_CAP : (uint8_t)c;
}

// ---------- the persisted state ----------
void Wild::begin() {
  prefs.begin("tamapoke", false);
  opened = true;
  st = State();
  size_t have = prefs.getBytesLength("wild");
  // A shorter blob (an older build) reads into the front; State is append-only.
  if (have && have <= sizeof(st)) prefs.getBytes("wild", &st, have);
  gift = prefs.getBool("wgift", false);
  ch = Chain();
  size_t hc = prefs.getBytesLength("wchain");
  if (hc && hc <= sizeof(ch)) prefs.getBytes("wchain", &ch, hc);
  if (ch.dex < 1 || ch.dex > DEX_COUNT || !ch.len) ch = Chain{0, 0, ch.best, 0};
  if (ch.len > WILD_CHAIN_MAX) ch.len = WILD_CHAIN_MAX;
  if (ch.best > WILD_CHAIN_MAX) ch.best = WILD_CHAIN_MAX;
  // Never trust stored bytes to index anything.
  if (st.used > WILD_PER_DAY) st.used = WILD_PER_DAY;
  if (st.phase > WP_CATCH || st.dex < 1 || st.dex > DEX_COUNT) { st.phase = WP_NONE; st.dex = 0; }
  if (st.phase == WP_NONE) st.dex = 0;
  if (st.level < 1 || st.level > MAX_LEVEL) st.level = 2;
  st.weather %= WX_COUNT;
  st.tod %= WT_COUNT;
  // A fight the board was reset in the middle of is a fight lost: otherwise
  // pulling the power on a losing battle would be a free retry.
  if (st.phase == WP_FIGHT) spend();
}

void Wild::save() {
  if (!opened) return;
  prefs.putBytes("wild", &st, sizeof(st));
}

void Wild::newDay(uint32_t day) {
  if (day == st.day) return;
  st.day = day;
  st.used = 0;
  if (st.phase == WP_MEET) { st.phase = WP_NONE; st.dex = 0; }   // it left with the day
  save();
}

void Wild::spend() {
  if (st.encDay == st.day && st.used < WILD_PER_DAY) st.used++;
  st.phase = WP_NONE;
  st.dex = 0;
  st.throws = 0;
  save();
}

bool Wild::peek(uint32_t day, uint32_t epoch, uint8_t region, uint8_t petLevel, bool charm, WildNext &out) const {
  out = WildNext();
  // newDay(day), simulated: a new day is a full one, and a creature merely MET
  // on another day has left with it.
  bool fresh = day != st.day;
  uint8_t used = fresh ? 0 : st.used;
  uint8_t phase = (fresh && st.phase == WP_MEET) ? (uint8_t)WP_NONE : st.phase;
  if (phase != WP_NONE) {
    out.dex = st.dex; out.level = st.level; out.shiny = st.shiny;
    out.weather = st.weather; out.tod = st.tod; out.out = true;
    return st.dex >= 1;
  }
  if (used >= WILD_PER_DAY) return false;
  uint8_t wx = wildWeatherOf(day);
  uint8_t tod = wildTimeOf(epoch);
  uint32_t seed = mix32(day ^ mix32(used + 1u) ^ mix32((uint32_t)tod * 977u + (uint32_t)region * 31u + 7u));
  uint8_t lvl = wildLevelFor(petLevel, seed);
  int16_t cd = chainDex(day);
  int16_t d = wildRollSpeciesChained(region, wx, tod, lvl, seed, cd);
  if (!d) return false;
  // The chain's shiny bonus is for the chained species only.
  uint16_t n = wildShinyNumer(d == cd ? chainLen(day) : 0, charm);
  out.dex = d;
  out.level = lvl;
  out.shiny = (mix32(seed ^ 0x5417u) % WILD_SHINY_ODDS) < n;
  out.weather = wx;
  out.tod = tod;
  return true;
}

bool Wild::roll(uint32_t day, uint32_t epoch, uint8_t region, uint8_t petLevel, bool charm) {
  newDay(day);
  if (st.phase != WP_NONE) return true;
  WildNext n;
  if (!peek(day, epoch, region, petLevel, charm, n)) return false;
  st.dex = n.dex;
  st.level = n.level;
  st.shiny = n.shiny;
  st.throws = 0;
  st.weather = n.weather;
  st.tod = n.tod;
  st.encDay = day;
  st.phase = WP_MEET;
  save();
  gameEvent(EV_WILD_SEEN, n.dex);
  if (n.shiny) gameEvent(EV_SHINY_FOUND, n.dex);
  return true;
}

// ---------- the shiny chain ----------
uint8_t Wild::chainLen(uint32_t day) const {
  if (!ch.len || ch.dex < 1) return 0;
  // A whole day with no wild win breaks it: the last link on day D keeps it
  // alive through D + 1. (A clock going backwards does not break it.)
  if (day > ch.day + 1) return 0;
  return ch.len;
}

void Wild::chainWin(int16_t dex, uint32_t day) {
  if (dex < 1 || dex > DEX_COUNT) return;
  uint8_t cur = chainLen(day);
  bool grew = true;
  if (cur && ch.dex == dex) {
    grew = cur < WILD_CHAIN_MAX;
    ch.len = grew ? cur + 1 : WILD_CHAIN_MAX;
  } else { ch.dex = dex; ch.len = 1; }
  ch.day = day;
  if (ch.len > ch.best) ch.best = ch.len;
  saveChain();
  if (grew) gameEvent(EV_CHAIN, ch.len);
}

void Wild::saveChain() {
  if (!opened) return;
  prefs.putBytes("wchain", &ch, sizeof(ch));
}

void Wild::fightStarted() {
  if (st.phase != WP_MEET) return;
  st.phase = WP_FIGHT;
  save();
}

uint8_t Wild::battleEnded(bool won, bool ran, uint8_t dropRoll100, Bag &bg) {
  (void)ran;
  if (st.phase != WP_FIGHT) return ITEM_NONE;
  if (!won) { spend(); return ITEM_NONE; }
  st.phase = WP_CATCH;
  st.throws = 0;
  save();
  gameEvent(EV_WILD_WIN, st.dex);
  chainWin(st.dex, st.day);
  // Berries: ORAN 20%, SITRUS 5%, LUM 3%.
  uint8_t drop = ITEM_NONE;
  if (dropRoll100 < 20) drop = ITEM_ORAN_BERRY;
  else if (dropRoll100 < 25) drop = ITEM_SITRUS_BERRY;
  else if (dropRoll100 < 28) drop = ITEM_LUM_BERRY;
  if (drop && !bg.add(drop)) drop = ITEM_NONE;   // a full stack drops nothing
  return drop;
}

void Wild::runAway() {
  if (st.phase != WP_MEET && st.phase != WP_CATCH) return;
  spend();
}

uint8_t Wild::throwBall(uint8_t ball, uint8_t roll100, Party &pt, Bag &bg, Pet &pp) {
  if (st.phase != WP_CATCH || !itemIsBall(ball)) return WTH_NONE;
  if (!bg.has(ball)) return WTH_NO_BALL;
  // Room first, and BEFORE the ball is taken: a catch with nowhere to go
  // would spend a ball on nothing.
  if (pt.firstFree() < 0 && pt.boxFirstFree() < 0) return WTH_NO_ROOM;
  bg.take(ball);
  st.throws++;
  if (roll100 < wildCatchChance(st.dex, ball)) {
    PartyMon m;
    m.dex = st.dex;
    m.level = st.level;
    m.shiny = st.shiny ? 1 : 0;
    uint8_t floor = st.shiny ? 20 : 4;   // a shiny's floor, as from an egg
    m.ivAtk = floor + random(32 - floor);
    m.ivDef = floor + random(32 - floor);
    m.ivSpe = floor + random(32 - floor);
    m.ivHp = floor + random(32 - floor);
    // Moves from the learnset at its level, through a scratch Pet exactly like
    // the battle's opponents (foeFromSpecies) -- never a second copy of the
    // learnset rules. Nobody begin()s it, so it cannot write NVS.
    Pet tmp;
    tmp.dbgHatchAs(st.dex, false);
    tmp.ageMinutes = (uint32_t)(st.level - 1) * MINUTES_PER_LEVEL;
    tmp.relearnFromLevel();
    for (uint8_t i = 0; i < MOVE_SLOTS; i++) m.moves[i] = tmp.moves[i];
    bool inParty = pt.add(m);
    if (!inParty) pt.boxAdd(m);
    int16_t d = st.dex;
    pp.registerCaught(d, m.shiny);
    spend();
    gameEvent(EV_WILD_CATCH, d);
    return inParty ? WTH_PARTY : WTH_BOX;
  }
  if (st.throws >= WILD_THROWS) { spend(); return WTH_FLED; }
  save();
  return WTH_MISSED;
}

bool Wild::giveGift(Bag &bg) {
  if (gift) return false;
  bg.add(ITEM_POKEBALL, WILD_GIFT_BALLS);
  gift = true;
  if (opened) prefs.putBool("wgift", true);
  return true;
}

void Wild::clearAll() {
  st = State();
  ch = Chain();
  gift = false;
  save();
  saveChain();
  if (opened) prefs.putBool("wgift", false);
}
