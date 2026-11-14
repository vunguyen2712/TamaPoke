#include "tasks.h"
#include "pet.h"
#include "party.h"
#include "items.h"
#include "dex.h"
#include "trainers.h"
#include "game_events.h"
#include "breed.h"

// ---------- the tables ----------
//
// APPEND-ONLY: the save stores a ROW INDEX into these, so reordering would turn
// today's FEED into yesterday's CLEAN for anyone mid-day. A row can be retuned
// (target, reward) freely; it just takes effect the next time it is rolled.
// One row per kind per table, which is what keeps the three dailies distinct.
// docs/adventure-tasks.md has the reasoning behind every number.
const TaskDef DAILY_TASKS[] = {
  { TK_FEED,        5,  ITEM_POKEBALL,     2 },
  { TK_FAV_BERRY,   2,  ITEM_ORAN_BERRY,   1 },
  { TK_CLEAN,       2,  ITEM_POKEBALL,     2 },
  { TK_PLAY,        2,  ITEM_ORAN_BERRY,   1 },
  { TK_TRAIN,       2,  ITEM_POKEBALL,     2 },
  { TK_WELL_KEPT,   60, ITEM_GREATBALL,    1 },   // minutes, straight
  { TK_HATCH,       1,  ITEM_GREATBALL,    2 },
  { TK_EVOLVE,      1,  ITEM_SITRUS_BERRY, 1 },
  { TK_GYM_WIN,     1,  ITEM_GREATBALL,    2 },
  { TK_REMATCH_WIN, 1,  ITEM_SITRUS_BERRY, 1 },
  { TK_WILD_SEEN,   3,  ITEM_POKEBALL,     3 },
  { TK_WILD_WIN,    2,  ITEM_ORAN_BERRY,   2 },
  { TK_WILD_CATCH,  1,  ITEM_POKEBALL,     3 },
  { TK_TOWER_WIN,   3,  ITEM_GREATBALL,    2 },
  { TK_ITEM_USED,   1,  ITEM_ORAN_BERRY,   2 },
};
const uint8_t DAILY_TASK_COUNT = sizeof(DAILY_TASKS) / sizeof(DAILY_TASKS[0]);

const TaskDef WEEKLY_TASKS[] = {
  { TK_CARE_DAY,    5,  ITEM_ULTRABALL,    2 },
  { TK_FEED,        30, ITEM_LEFTOVERS,    1 },
  { TK_PLAY,        10, ITEM_SITRUS_BERRY, 2 },
  { TK_TRAIN,       10, ITEM_FOCUS_SASH,   1 },
  { TK_WILD_WIN,    10, ITEM_QUICK_CLAW,   1 },
  { TK_WILD_CATCH,  5,  ITEM_ULTRABALL,    2 },
  { TK_GYM_WIN,     2,  ITEM_FOCUS_SASH,   1 },
  { TK_TOWER_WIN,   15, ITEM_LEFTOVERS,    1 },
  { TK_REMATCH_WIN, 3,  ITEM_LUM_BERRY,    2 },
  { TK_BREED,       1,  ITEM_GREATBALL,    5 },
  { TK_EVOLVE,      1,  ITEM_SITRUS_BERRY, 2 },
  { TK_DEX_NEW,     1,  ITEM_ULTRABALL,    1 },
  { TK_ITEM_USED,   5,  ITEM_LUM_BERRY,    2 },
};
const uint8_t WEEKLY_TASK_COUNT = sizeof(WEEKLY_TASKS) / sizeof(WEEKLY_TASKS[0]);

// Clearing all three dailies. Rotates by day so the bonus is not always a ball.
static const TaskDef DAILY_BONUS[] = {
  { TK_NONE, 0, ITEM_GREATBALL,    2 },
  { TK_NONE, 0, ITEM_ULTRABALL,    1 },
  { TK_NONE, 0, ITEM_SITRUS_BERRY, 1 },
  { TK_NONE, 0, ITEM_LUM_BERRY,    1 },
};
#define BONUS_COUNT (sizeof(DAILY_BONUS) / sizeof(DAILY_BONUS[0]))
#define BONUS_BIT (1 << TASK_SLOTS)

// ---------- state ----------

static Pet *gPet = nullptr;
static Preferences prefs;
static bool opened = false;        // nobody begin()'d us: never write NVS
static bool have = false;          // a day has been rolled (or loaded)
static uint32_t curDay = 0, curWeek = 0;
static uint8_t rowIdx[TASK_SLOTS] = { 0 };
static uint8_t prog[TASK_SLOTS] = { 0 };
static uint8_t claimed = 0;        // bit per slot, plus BONUS_BIT
static uint8_t wellHeld = 0;       // TK_WELL_KEPT: minutes in a row, RAM only
static uint32_t noticeAt = 0;
static bool noticeSet = false;

// One blob, "tasks": version, claimed, rows[5], progress[5], day, week.
#define TASK_SAVE_VER 1
#define TASK_SAVE_LEN (2 + TASK_SLOTS * 2 + 8)

static const TaskDef &rowOf(uint8_t slot) {
  if (slot < TASK_DAILY) return DAILY_TASKS[rowIdx[slot] % DAILY_TASK_COUNT];
  return WEEKLY_TASKS[rowIdx[slot] % WEEKLY_TASK_COUNT];
}

static void put32(uint8_t *p, uint32_t v) { for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i)); }
static uint32_t get32(const uint8_t *p) { uint32_t v = 0; for (int i = 0; i < 4; i++) v |= (uint32_t)p[i] << (8 * i); return v; }

static void save() {
  if (!opened || !have) return;
  uint8_t b[TASK_SAVE_LEN];
  b[0] = TASK_SAVE_VER;
  b[1] = claimed;
  for (uint8_t i = 0; i < TASK_SLOTS; i++) { b[2 + i] = rowIdx[i]; b[2 + TASK_SLOTS + i] = prog[i]; }
  put32(b + 2 + TASK_SLOTS * 2, curDay);
  put32(b + 6 + TASK_SLOTS * 2, curWeek);
  prefs.putBytes("tasks", b, sizeof(b));
}

// Never trust a stored blob: wrong length, wrong version or an unknown row and
// it is dropped whole, so the next refresh simply rolls fresh tasks.
static void load() {
  have = false;
  claimed = 0;
  memset(rowIdx, 0, sizeof(rowIdx));
  memset(prog, 0, sizeof(prog));
  uint8_t b[TASK_SAVE_LEN];
  if (prefs.getBytesLength("tasks") != sizeof(b)) return;
  prefs.getBytes("tasks", b, sizeof(b));
  if (b[0] != TASK_SAVE_VER) return;
  for (uint8_t i = 0; i < TASK_SLOTS; i++) {
    uint8_t n = i < TASK_DAILY ? DAILY_TASK_COUNT : WEEKLY_TASK_COUNT;
    if (b[2 + i] >= n) return;
  }
  claimed = b[1] & (uint8_t)((1 << (TASK_SLOTS + 1)) - 1);
  for (uint8_t i = 0; i < TASK_SLOTS; i++) rowIdx[i] = b[2 + i];
  for (uint8_t i = 0; i < TASK_SLOTS; i++) {
    uint8_t t = rowOf(i).target;
    prog[i] = b[2 + TASK_SLOTS + i] > t ? t : b[2 + TASK_SLOTS + i];
  }
  curDay = get32(b + 2 + TASK_SLOTS * 2);
  curWeek = get32(b + 6 + TASK_SLOTS * 2);
  have = true;
}

// ---------- what this player can do ----------

// Weak: the tower feature defines the real one. Until then the tower opens
// with any region's champion beaten -- the last badge of a ladder.
__attribute__((weak)) bool towerUnlocked() {
  if (!gPet) return false;
  for (uint8_t r = 0; r < GYM_REGIONS; r++)
    if (gPet->hasBadge(r, TRAINER_COUNT - 1, false)) return true;
  return false;
}

// Levels until the next evolution, or -1 if this creature will never evolve.
// Same threshold canEvolveNow() uses -- careMistakes and the retire penalty
// both move it -- so the task can never promise what the card would not.
static int levelsToEvolve(const Pet &p) {
  if (p.frozen || p.isEgg() || p.speciesId > DEX_COUNT) return -1;
  const DexEntry &d = DEX_TBL[p.speciesId];
  if (d.evolvesTo == 0) return -1;
  int need = (int)d.evolveLevel + p.careMistakes + p.evoPenalty();
  if (need > MAX_LEVEL) return -1;
  int lv = p.level();
  return need > lv ? need - lv : 0;
}

TaskWorld taskWorldOf(const Pet &p) {
  TaskWorld w;
  memset(&w, 0, sizeof(w));
  w.eggToday = p.isEgg() || p.canFarewellNow();
  // A level is an hour; count a day as 12 waking ones, and what is left of the
  // week (weeks start on a Monday) the same way.
  uint32_t day = p.today();
  uint32_t daysLeft = 7 - ((day + 3) % 7);
  int lv = levelsToEvolve(p);
  w.evolveToday = lv >= 0 && lv <= 12;
  w.evolveWeek = lv >= 0 && (uint32_t)lv <= daysLeft * 12;
  w.dexNewWeek = w.evolveWeek && !p.isRegistered(DEX_TBL[p.speciesId].evolvesTo);
  for (uint8_t r = 0; r < GYM_REGIONS && !w.gymsLeft; r++) {
    if (!regionAvailable(r)) continue;
    for (uint8_t i = 0; i < TRAINER_COUNT; i++)
      if (!p.hasBadge(r, i, false)) { w.gymsLeft = true; break; }
  }
  w.towerOpen = towerUnlocked();
  for (uint8_t i = 1; i < ITEM_COUNT; i++) {
    if (!bag.count(i)) continue;
    if (itemIsBall(i)) w.hasBall = true;
    if (itemIsHeld(i)) w.hasHeld = true;
  }
  w.canBreed = breedPossible();   // the daycare's own rule: a compatible pair, no egg queued
  return w;
}

// THE single answer to "can this task be offered". The care tasks need nothing:
// an egg hatches within minutes (three taps, or by itself), so even an egg's
// owner can feed and clean today.
bool taskPossible(uint8_t kind, bool weekly, const TaskWorld &w) {
  switch (kind) {
    case TK_FEED: case TK_FAV_BERRY: case TK_CLEAN: case TK_PLAY:
    case TK_TRAIN: case TK_CARE_DAY: case TK_WELL_KEPT:
    case TK_WILD_SEEN: case TK_WILD_WIN:
      return true;
    case TK_HATCH:       return !weekly && w.eggToday;
    case TK_EVOLVE:      return weekly ? w.evolveWeek : w.evolveToday;
    case TK_DEX_NEW:     return w.dexNewWeek;
    case TK_GYM_WIN:     return w.gymsLeft;
    case TK_REMATCH_WIN: return w.towerOpen;
    case TK_TOWER_WIN:   return w.towerOpen;
    case TK_WILD_CATCH:  return w.hasBall;
    case TK_ITEM_USED:   return w.hasHeld;
    case TK_BREED:       return w.canBreed;
    default:             return false;
  }
}

uint32_t tasksWeekOf(uint32_t day) { return (day + 3) / 7; }   // epoch day 4 was a Monday

static uint32_t mix(uint32_t x) {
  x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16;
  return x;
}

// A seeded partial shuffle of the possible rows. Never random(): it must be the
// same answer every time it is asked, and must not disturb the game's RNG.
void tasksPick(uint32_t seed, bool weekly, const TaskWorld &w, uint8_t *out) {
  const TaskDef *tbl = weekly ? WEEKLY_TASKS : DAILY_TASKS;
  uint8_t n = weekly ? WEEKLY_TASK_COUNT : DAILY_TASK_COUNT;
  uint8_t want = weekly ? TASK_WEEKLY : TASK_DAILY;
  uint8_t cand[32], c = 0;
  for (uint8_t i = 0; i < n && c < sizeof(cand); i++)
    if (taskPossible(tbl[i].kind, weekly, w)) cand[c++] = i;
  uint32_t s = mix(seed * 2 + (weekly ? 1 : 0) + 0x7A5C);
  for (uint8_t k = 0; k < want; k++) {
    if (k >= c) { out[k] = cand[0]; continue; }   // cannot happen: >= 5 always possible
    uint8_t j = k + (uint8_t)(s % (c - k));
    uint8_t t = cand[k]; cand[k] = cand[j]; cand[j] = t;
    out[k] = cand[k];
    s = mix(s + 0x9E3779B9U);
  }
}

// ---------- rolling ----------

void tasksRollTo(uint32_t day) {
  uint32_t week = tasksWeekOf(day);
  bool newDay = !have || day != curDay;
  bool newWeek = !have || week != curWeek;
  if (!newDay && !newWeek) return;
  TaskWorld w;
  if (gPet) w = taskWorldOf(*gPet);
  else memset(&w, 0, sizeof(w));
  // A changed day re-rolls whichever way the clock moved: a corrected RTC must
  // never leave the tasks stuck on a day that will not come again.
  if (newDay) {
    tasksPick(day, false, w, rowIdx);
    for (uint8_t i = 0; i < TASK_DAILY; i++) { prog[i] = 0; claimed &= ~(1 << i); }
    claimed &= ~BONUS_BIT;
    wellHeld = 0;
    curDay = day;
  }
  if (newWeek) {
    tasksPick(week, true, w, rowIdx + TASK_DAILY);
    for (uint8_t i = TASK_DAILY; i < TASK_SLOTS; i++) { prog[i] = 0; claimed &= ~(1 << i); }
    curWeek = week;
  }
  have = true;
  save();
}

void tasksRefresh() {
  tasksRollTo(gPet ? gPet->today() : 0);
}

static void completed() { noticeAt = millis(); noticeSet = true; }

static bool matches(uint8_t kind, GameEv ev, int16_t arg) {
  switch (kind) {
    case TK_FEED:        return ev == EV_FEED;
    case TK_FAV_BERRY:   return ev == EV_FEED && arg == 1;
    case TK_CLEAN:       return ev == EV_CLEAN;
    case TK_PLAY:        return ev == EV_PLAY && arg > 0;   // a session that scored
    case TK_TRAIN:       return ev == EV_TRAIN;
    case TK_CARE_DAY:    return ev == EV_CARE_DAY;
    case TK_HATCH:       return ev == EV_HATCH;
    case TK_EVOLVE:      return ev == EV_EVOLVE;
    case TK_GYM_WIN:     return ev == EV_GYM_WIN;
    case TK_REMATCH_WIN: return ev == EV_REMATCH_WIN;
    case TK_WILD_SEEN:   return ev == EV_WILD_SEEN;
    case TK_WILD_WIN:    return ev == EV_WILD_WIN;
    case TK_WILD_CATCH:  return ev == EV_WILD_CATCH;
    case TK_TOWER_WIN:   return ev == EV_TOWER_WIN;
    case TK_BREED:       return ev == EV_BREED;
    case TK_ITEM_USED:   return ev == EV_ITEM_USED;
    case TK_DEX_NEW:     return ev == EV_DEX_REGISTER;
    default:             return false;   // TK_WELL_KEPT is a state, not an event
  }
}

void tasksOnEvent(GameEv ev, int16_t arg) {
  if (!opened) return;
  tasksRefresh();          // an event just after midnight counts for the new day
  bool changed = false;
  for (uint8_t i = 0; i < TASK_SLOTS; i++) {
    const TaskDef &d = rowOf(i);
    if (prog[i] >= d.target || !matches(d.kind, ev, arg)) continue;
    prog[i]++;
    changed = true;
    if (prog[i] >= d.target) completed();
  }
  if (changed) save();
}

bool tasksWellKept(const Pet &p) {
  return !p.isEgg() && p.ceremony == CER_NONE && p.lowestStat() >= TASK_WELL_MIN;
}

// Minutes in a row, counted only while the board is running and the condition
// holds. A lapse -- or a reboot, since the count lives in RAM -- starts over:
// writing NVS every minute would cost a ~1 s flash stall each time.
void tasksTickAt(uint32_t day, bool wellKept) {
  tasksRollTo(day);
  for (uint8_t i = 0; i < TASK_SLOTS; i++) {
    const TaskDef &d = rowOf(i);
    if (d.kind != TK_WELL_KEPT || prog[i] >= d.target) continue;
    if (!wellKept) { wellHeld = 0; continue; }
    if (wellHeld < 255) wellHeld++;
    if (wellHeld >= d.target) { prog[i] = d.target; completed(); save(); }
  }
}

void tasksTick() {
  if (!gPet) return;
  tasksTickAt(gPet->today(), tasksWellKept(*gPet));
}

void tasksBegin(Pet &p) {
  gPet = &p;
  prefs.begin("tamapoke", false);
  opened = true;
  load();
  wellHeld = 0;
  noticeSet = false;
  gameEventSubscribe(tasksOnEvent);
}

void tasksForget() {
  have = false;
  wellHeld = 0;
  noticeSet = false;
  claimed = 0;
  memset(prog, 0, sizeof(prog));
}

// ---------- queries and claims ----------

const TaskDef &tasksDef(uint8_t slot) {
  static const TaskDef none = { TK_NONE, 1, ITEM_NONE, 0 };
  return slot < TASK_SLOTS ? rowOf(slot) : none;
}

uint8_t tasksProgress(uint8_t slot) {
  if (slot >= TASK_SLOTS) return 0;
  const TaskDef &d = rowOf(slot);
  if (d.kind == TK_WELL_KEPT && prog[slot] < d.target) return wellHeld < d.target ? wellHeld : d.target;
  return prog[slot];
}

bool tasksDone(uint8_t slot) { return slot < TASK_SLOTS && prog[slot] >= rowOf(slot).target; }
bool tasksClaimed(uint8_t slot) { return slot < TASK_SLOTS && (claimed & (1 << slot)); }

// The flag is saved BEFORE the items go in: a power cut between the two loses a
// reward rather than paying it twice.
bool tasksClaim(uint8_t slot) {
  if (!opened) return false;
  tasksRefresh();
  if (slot >= TASK_SLOTS || !tasksDone(slot) || tasksClaimed(slot)) return false;
  claimed |= (uint8_t)(1 << slot);
  save();
  const TaskDef &d = rowOf(slot);
  bag.add(d.item, d.qty);
  return true;
}

static bool dailiesClaimed() {
  for (uint8_t i = 0; i < TASK_DAILY; i++) if (!(claimed & (1 << i))) return false;
  return true;
}
bool tasksBonusClaimed() { return claimed & BONUS_BIT; }
bool tasksBonusReady() { return have && dailiesClaimed() && !tasksBonusClaimed(); }
uint8_t tasksBonusItem() { return DAILY_BONUS[curDay % BONUS_COUNT].item; }
uint8_t tasksBonusQty() { return DAILY_BONUS[curDay % BONUS_COUNT].qty; }

bool tasksClaimBonus() {
  if (!opened) return false;
  tasksRefresh();
  if (!tasksBonusReady()) return false;
  claimed |= BONUS_BIT;
  save();
  bag.add(tasksBonusItem(), tasksBonusQty());
  return true;
}

uint8_t tasksClaimable() {
  if (!have) return 0;
  uint8_t n = 0;
  for (uint8_t i = 0; i < TASK_SLOTS; i++) if (tasksDone(i) && !tasksClaimed(i)) n++;
  if (tasksBonusReady()) n++;
  return n;
}

uint32_t tasksDay() { return curDay; }
uint32_t tasksWeek() { return curWeek; }

bool tasksNoticeActive() {
  if (!noticeSet) return false;
  if (millis() - noticeAt < TASK_NOTICE_MS) return true;
  noticeSet = false;
  return false;
}
