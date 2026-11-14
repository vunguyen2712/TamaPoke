#include "challenge.h"
#include <Preferences.h>
#include "pet.h"
#include "party.h"
#include "items.h"
#include "game_events.h"
#include "tasks.h"
#include "tower.h"

// ---------- state ----------
// Player-wide, like the badges. One blob, "chal" (see save.cpp):
//   [0] version  [1] region  [2] leader  [3] rule  [4] param  [5] flags (bit0 won)
//   [6..9] week (LE)  [10..11] weeks cleared, ever (LE)
#define CHAL_SAVE_VER 1
#define CHAL_SAVE_LEN 12

static Preferences s_prefs;
static bool s_opened = false;   // nobody begin()'d us: never write
static bool s_have = false;     // a week is drawn
static ChalWeek s_cur;
static uint16_t s_wins = 0;

uint32_t challengeWeekOf(uint32_t day) { return tasksWeekOf(day); }   // ONE week rule

static void chalSave() {
  if (!s_opened) return;
  uint8_t b[CHAL_SAVE_LEN];
  b[0] = s_have ? CHAL_SAVE_VER : 0;
  b[1] = s_cur.region; b[2] = s_cur.leader; b[3] = s_cur.rule; b[4] = s_cur.param;
  b[5] = s_cur.won ? 1 : 0;
  for (int i = 0; i < 4; i++) b[6 + i] = (uint8_t)(s_cur.week >> (8 * i));
  b[10] = (uint8_t)s_wins; b[11] = (uint8_t)(s_wins >> 8);
  s_prefs.putBytes("chal", b, sizeof(b));
}

// Never trust a stored blob: wrong length or version, or a value out of range,
// and the week is dropped (the next refresh draws it again; the win count is
// kept whenever the length is right).
static void chalLoad() {
  s_have = false;
  s_cur = ChalWeek();
  s_wins = 0;
  uint8_t b[CHAL_SAVE_LEN];
  if (s_prefs.getBytesLength("chal") != sizeof(b)) return;
  s_prefs.getBytes("chal", b, sizeof(b));
  s_wins = (uint16_t)(b[10] | (b[11] << 8));
  if (b[0] != CHAL_SAVE_VER) return;
  if (b[1] >= GYM_REGIONS || b[2] >= TRAINER_GYMS || b[3] >= CR_COUNT) return;
  if (b[3] == CR_TYPE && b[4] >= TYPE_COUNT) return;
  if (b[3] == CR_LEVEL && (b[4] < 1 || b[4] > MAX_TRAINER_LEVEL)) return;
  s_cur.region = b[1]; s_cur.leader = b[2]; s_cur.rule = b[3]; s_cur.param = b[4];
  s_cur.won = b[5] & 1;
  s_cur.week = 0;
  for (int i = 0; i < 4; i++) s_cur.week |= (uint32_t)b[6 + i] << (8 * i);
  s_have = true;
}

void challengeBegin() {
  if (!s_opened) s_prefs.begin("tamapoke", false);
  s_opened = true;
  chalLoad();
}

void challengeClearAll() {
  s_have = false;
  s_cur = ChalWeek();
  s_wins = 0;
  chalSave();
}

bool challengeValid() { return s_have; }
const ChalWeek &challengeCurrent() { return s_cur; }
bool challengeDone() { return s_have && s_cur.won; }
uint16_t challengeWins() { return s_wins; }

bool challengeUnlockedFor(const Pet &p) {
  return p.badgeCount(false) + p.badgeCount(true) > 0;
}

// ---------- the leader's team ----------
static const Trainer &leaderOf(uint8_t region, uint8_t leader) {
  return TRAINER_SETS[region % GYM_REGIONS].list[leader % TRAINER_GYMS];
}
static uint8_t topLevel(const Trainer &t) {
  uint8_t top = 0;
  for (uint8_t k = 0; k < t.count; k++) if (t.team[k].level > top) top = t.team[k].level;
  return top;
}
static uint8_t levelCapFor(const Trainer &t) {
  uint8_t top = topLevel(t);
  int n = top - top / 4;
  return (uint8_t)(n < CHAL_LEVEL_MIN ? CHAL_LEVEL_MIN : n);
}

uint8_t challengeTeam(const ChalWeek &c, TrainerMon *out) {
  if (c.region >= GYM_REGIONS || c.leader >= TRAINER_GYMS) return 0;
  const Trainer &t = leaderOf(c.region, c.leader);
  uint8_t n = 0;
  switch (c.rule) {
    case CR_SOLO: {                  // the ace alone: highest level, the last on a tie
      uint8_t ace = 0;
      for (uint8_t k = 0; k < t.count; k++) if (t.team[k].level >= t.team[ace].level) ace = k;
      out[n++] = t.team[ace];
      break;
    }
    case CR_TRIO: {                  // the last three (the ace closes, as in the games)
      uint8_t from = t.count > CHAL_TRIO ? t.count - CHAL_TRIO : 0;
      for (uint8_t k = from; k < t.count; k++) out[n++] = t.team[k];
      break;
    }
    default:
      for (uint8_t k = 0; k < t.count; k++) out[n++] = t.team[k];
      break;
  }
  if (c.rule == CR_LEVEL)            // the whole team AT the cap
    for (uint8_t k = 0; k < n; k++) out[k].level = c.param;
  return n;
}

static bool leaderDrawable(uint8_t region, uint8_t leader) {
  if (!regionAvailable(region)) return false;
  const Trainer &t = leaderOf(region, leader);
  if (!t.count) return false;
  for (uint8_t k = 0; k < t.count; k++)
    if (!towerDrawable((int16_t)t.team[k].dex)) return false;
  return true;
}
bool challengeTeamDrawable(const ChalWeek &c) {
  return c.region < GYM_REGIONS && c.leader < TRAINER_GYMS && leaderDrawable(c.region, c.leader);
}

uint8_t challengeYourCap(const ChalWeek &c) {
  if (c.rule == CR_LEVEL) return c.param;
  TrainerMon tm[TRAINER_TEAM_MAX];
  uint8_t n = challengeTeam(c, tm), top = 0;
  for (uint8_t k = 0; k < n; k++) if (tm[k].level > top) top = tm[k].level;
  return top;
}

// ---------- the world and the draw ----------
static void addTypes(uint32_t &types, int16_t dex) {
  if (dex < 1 || dex > DEX_COUNT) return;
  const DexEntry &d = DEX_TBL[dex];
  if (d.type1 < TYPE_COUNT) types |= 1UL << d.type1;
  if (d.type2 < TYPE_COUNT) types |= 1UL << d.type2;
}

ChalWorld challengeWorldOf(const Pet &p, const Party &pt) {
  ChalWorld w;
  memset(&w, 0, sizeof(w));
  for (uint8_t r = 0; r < GYM_REGIONS; r++)
    for (uint8_t i = 0; i < TRAINER_GYMS; i++) {
      bool reached = i == 0 || p.hasBadge(r, i - 1, false) || p.hasBadge(r, i - 1, true);
      if (reached && leaderDrawable(r, i)) w.leaders[r] |= (uint8_t)(1 << i);
    }
  if (!p.isEgg()) { addTypes(w.types, p.speciesId); w.mons++; }
  for (uint8_t i = 0; i < PARTY_SLOTS; i++) {
    if (pt.slots[i].empty()) continue;
    addTypes(w.types, pt.slots[i].dex);
    w.mons++;
  }
  return w;
}

bool challengeRulePossible(uint8_t rule, const ChalWorld &w) {
  switch (rule) {
    case CR_TYPE: return w.types != 0;          // X is always a type you own
    case CR_TRIO: return w.mons >= CHAL_TRIO;
    case CR_LEVEL: case CR_SOLO: case CR_NOITEMS: return w.mons >= 1;
    default: return false;
  }
}

static uint32_t mix(uint32_t x) {   // the draw must not depend on random()
  x ^= x >> 16; x *= 0x7FEB352DU; x ^= x >> 15; x *= 0x846CA68BU; x ^= x >> 16;
  return x;
}

bool challengePick(uint32_t week, const ChalWorld &w, ChalWeek &out) {
  // the leader: one of every (playable region, reached leader), in a fixed order
  uint8_t n = 0;
  for (uint8_t r = 0; r < GYM_REGIONS; r++)
    for (uint8_t i = 0; i < TRAINER_GYMS; i++) if ((w.leaders[r] >> i) & 1) n++;
  if (!n) return false;
  uint32_t pick = mix(week * 2654435761U + 0xC4A1) % n;
  out = ChalWeek();
  out.week = week;
  for (uint8_t r = 0; r < GYM_REGIONS; r++)
    for (uint8_t i = 0; i < TRAINER_GYMS; i++)
      if (((w.leaders[r] >> i) & 1) && pick-- == 0) { out.region = r; out.leader = i; }
  // the rule ROTATES with the week, so two weeks in a row differ whenever every
  // rule is possible; an impossible one passes its turn to the next
  out.rule = CR_COUNT;
  for (uint8_t k = 0; k < CR_COUNT; k++) {
    uint8_t r = (uint8_t)((week + k) % CR_COUNT);
    if (challengeRulePossible(r, w)) { out.rule = r; break; }
  }
  if (out.rule >= CR_COUNT) return false;
  if (out.rule == CR_TYPE) {
    uint8_t tn = 0, owned[TYPE_COUNT];
    for (uint8_t t = 0; t < TYPE_COUNT; t++) if ((w.types >> t) & 1) owned[tn++] = t;
    out.param = owned[mix(week ^ 0x51ED27u) % tn];
  } else if (out.rule == CR_LEVEL) {
    out.param = levelCapFor(leaderOf(out.region, out.leader));
  }
  return true;
}

bool challengeRollTo(uint32_t day, const ChalWorld &w) {
  uint32_t week = challengeWeekOf(day);
  if (s_have && s_cur.week == week && challengeTeamDrawable(s_cur)) return true;
  bool won = s_have && s_cur.week == week && s_cur.won;   // a re-draw keeps the win
  ChalWeek c;
  if (!challengePick(week, w, c)) {
    if (s_have && s_cur.week == week) return true;   // keep it; START refuses it
    s_have = false;
    return false;
  }
  c.won = won;
  s_cur = c;
  s_have = true;
  chalSave();
  return true;
}

bool challengeRefreshFor(const Pet &p, const Party &pt, uint32_t day) {
  if (s_have && s_cur.week == challengeWeekOf(day) && challengeTeamDrawable(s_cur)) return true;
  return challengeRollTo(day, challengeWorldOf(p, pt));
}

// ---------- the rule ----------
bool challengeAllowsMon(const ChalWeek &c, int16_t dex) {
  if (dex < 1 || dex > DEX_COUNT) return false;
  if (c.rule == CR_TYPE) return DEX_TBL[dex].type1 == c.param || DEX_TBL[dex].type2 == c.param;
  return true;
}
uint8_t challengeSquadMin(const ChalWeek &c) {
  return c.rule == CR_TRIO ? CHAL_TRIO : 1;
}
uint8_t challengeSquadMax(const ChalWeek &c) {
  if (c.rule == CR_SOLO) return 1;
  if (c.rule == CR_TRIO) return CHAL_TRIO;
  return TRAINER_TEAM_MAX;
}
bool challengeAllowsSquad(const ChalWeek &c, const int16_t *dex, uint8_t n) {
  if (n < challengeSquadMin(c) || n > challengeSquadMax(c)) return false;
  for (uint8_t i = 0; i < n; i++) if (!challengeAllowsMon(c, dex[i])) return false;
  return true;
}
bool challengeItemsOff(const ChalWeek &c) { return c.rule == CR_NOITEMS; }

// ---------- the reward ----------
uint8_t challengeRewardHeld(uint32_t week) {
  static const uint8_t HELD[] = { ITEM_LEFTOVERS, ITEM_FOCUS_SASH, ITEM_QUICK_CLAW,
                                  ITEM_SITRUS_BERRY, ITEM_LUM_BERRY };
  return HELD[week % (sizeof(HELD) / sizeof(HELD[0]))];
}

bool challengeWin() {
  if (!s_have || s_cur.won) return false;
  s_cur.won = true;
  if (s_wins < 0xFFFF) s_wins++;
  // Saved BEFORE the items go in: a reset between the two loses the reward
  // rather than paying it twice.
  chalSave();
  bag.add(ITEM_ULTRABALL, CHAL_BALLS);
  bag.add(challengeRewardHeld(s_cur.week), 1);
  gameEvent(EV_CHALLENGE_WIN, (int16_t)(s_cur.week & 0xFFFF));
  return true;
}
