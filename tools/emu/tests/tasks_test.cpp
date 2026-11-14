// Daily and weekly tasks: the logic, against the REAL Pet, Bag, event bus and
// NVS stub -- not a restatement of the rules.
//
// What this pins, and why each matters to a player:
//  - the same day always shows the same tasks; a new day resets them
//  - the week resets on its Monday boundary and NOT a day before
//  - an event moves exactly the tasks that count it
//  - a reward is paid once, also across a reload (the claimed flag is on flash)
//  - an impossible task is never offered -- and, so that check cannot pass
//    vacuously, the gated tasks DO get offered once they become possible
//  - the state task needs its condition held for the whole window
//  - the tasks key is in the backup table and survives an export/import
#include "Arduino.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include "items.h"
#include "tasks.h"
#include "save.h"
#include "game_events.h"
#include <cstdio>
#include <set>
#include <string>
uint32_t g_seed = 11; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX = 0, g_touchY = 0; volatile bool g_touchDown = false; bool wasPressed = false;
uint32_t millis() { return 0; } void FakeESP::restart() { exit(0); }
int FakeSerial::available() { return 0; } String FakeSerial::readStringUntil(char) { return String(""); }
void sfxPlay(uint8_t) {}
static int bad = 0;
static void ck(bool ok, const char *w) { printf("%s  %s\n", ok ? "PASS" : "FAIL", w); if (!ok) bad++; }

#define DAY0 20400u   // 2025-11-08, a Saturday; Monday is DAY0 + 2

static void setDay(Pet &p, uint32_t day) { p.lastSeenEpoch = day * 86400u + 12 * 3600u; }

static int slotOfKind(uint8_t kind, bool weekly) {
  for (uint8_t s = weekly ? TASK_DAILY : 0; s < (weekly ? TASK_SLOTS : TASK_DAILY); s++)
    if (tasksDef(s).kind == kind) return s;
  return -1;
}
static GameEv evFor(uint8_t kind) {
  switch (kind) {
    case TK_FEED: case TK_FAV_BERRY: return EV_FEED;
    case TK_CLEAN: return EV_CLEAN;  case TK_PLAY: return EV_PLAY;
    case TK_TRAIN: return EV_TRAIN;  case TK_CARE_DAY: return EV_CARE_DAY;
    case TK_HATCH: return EV_HATCH;  case TK_EVOLVE: return EV_EVOLVE;
    case TK_GYM_WIN: return EV_GYM_WIN; case TK_REMATCH_WIN: return EV_REMATCH_WIN;
    case TK_WILD_SEEN: return EV_WILD_SEEN; case TK_WILD_WIN: return EV_WILD_WIN;
    case TK_WILD_CATCH: return EV_WILD_CATCH; case TK_TOWER_WIN: return EV_TOWER_WIN;
    case TK_BREED: return EV_BREED; case TK_ITEM_USED: return EV_ITEM_USED;
    case TK_DEX_NEW: return EV_DEX_REGISTER;
    default: return EV_NONE;
  }
}
static void rows(uint8_t *out) { for (uint8_t s = 0; s < TASK_SLOTS; s++) out[s] = tasksDef(s).kind; }
static void reload(Pet &p) { tasksForget(); tasksBegin(p); }

int main() {
  Pet pet; pet.begin();
  pet.dbgHatchAs(4, false);                 // CHARMANDER: evolves at 16
  party.begin(); bag.begin(); bag.clearAll();
  setDay(pet, DAY0);
  tasksBegin(pet);
  tasksRefresh();

  // ---- the same day gives the same tasks; the next day re-rolls
  uint8_t a[TASK_SLOTS], b[TASK_SLOTS];
  rows(a);
  ck(tasksDay() == DAY0, "rolled to the pet's day");
  for (int i = 0; i < 5; i++) tasksRefresh();
  rows(b);
  ck(memcmp(a, b, sizeof(a)) == 0, "refreshing the same day changes nothing");
  reload(pet); tasksRefresh(); rows(b);
  ck(memcmp(a, b, sizeof(a)) == 0, "and a reload on the same day shows the same tasks");
  {
    TaskWorld w = taskWorldOf(pet);
    uint8_t x[TASK_DAILY], y[TASK_DAILY];
    bool alwaysSame = true, everDiffers = false;
    for (uint32_t d = 0; d < 300; d++) {
      tasksPick(DAY0 + d, false, w, x); tasksPick(DAY0 + d, false, w, y);
      if (memcmp(x, y, sizeof(x))) alwaysSame = false;
      uint8_t z[TASK_DAILY]; tasksPick(DAY0 + d + 1, false, w, z);
      if (memcmp(x, z, sizeof(x))) everDiffers = true;
      if (x[0] == x[1] || x[0] == x[2] || x[1] == x[2]) alwaysSame = false;
    }
    ck(alwaysSame, "tasksPick is deterministic and the three dailies are distinct (300 days)");
    ck(everDiffers, "and different days do get different tasks");
  }

  // ---- events move exactly the matching tasks
  int s0 = 0;
  const TaskDef &d0 = tasksDef(s0);
  uint8_t before[TASK_SLOTS];
  for (uint8_t s = 0; s < TASK_SLOTS; s++) before[s] = tasksProgress(s);
  GameEv ev = evFor(d0.kind);
  if (ev == EV_NONE) {   // the state task drew slot 0; use slot 1
    s0 = 1;
    ev = evFor(tasksDef(1).kind);
  }
  const TaskDef &dm = tasksDef(s0);
  gameEvent(ev, 1);
  bool onlyMatching = true;
  for (uint8_t s = 0; s < TASK_SLOTS; s++) {
    GameEv e2 = evFor(tasksDef(s).kind);
    // FAV_BERRY counts EV_FEED only with arg 1 -- which is what was sent
    bool should = (e2 == ev);
    int moved = tasksProgress(s) - before[s];
    if (moved != (should ? 1 : 0)) { onlyMatching = false; printf("      slot %u kind %u moved %d\n", s, tasksDef(s).kind, moved); }
  }
  ck(tasksProgress(s0) == 1, "the matching task moved by one");
  ck(onlyMatching, "and only tasks counting that event moved");
  // an unrelated event moves nothing that does not count it
  {
    uint8_t p1[TASK_SLOTS]; for (uint8_t s = 0; s < TASK_SLOTS; s++) p1[s] = tasksProgress(s);
    gameEvent(EV_GYM_LOSS, 0);   // no task counts a loss
    bool still = true;
    for (uint8_t s = 0; s < TASK_SLOTS; s++) if (tasksProgress(s) != p1[s]) still = false;
    ck(still, "an event no task counts moves nothing");
  }
  // favourite berry: only arg 1 counts
  {
    tasksForget();
    // force a known world: roll a day whose dailies include FAV_BERRY
    TaskWorld w = taskWorldOf(pet);
    uint32_t day = 0;
    for (uint32_t d = DAY0; d < DAY0 + 400 && !day; d++) {
      uint8_t x[TASK_DAILY]; tasksPick(d, false, w, x);
      for (int i = 0; i < TASK_DAILY; i++) if (DAILY_TASKS[x[i]].kind == TK_FAV_BERRY) day = d;
    }
    ck(day != 0, "some day offers FAV BERRY");
    tasksBegin(pet); setDay(pet, day); tasksRefresh();
    int sf = slotOfKind(TK_FAV_BERRY, false);
    int sfeed = slotOfKind(TK_FEED, false);
    uint8_t p0 = tasksProgress(sf);
    gameEvent(EV_FEED, 0);
    ck(tasksProgress(sf) == p0, "a plain berry does not count as the favourite");
    if (sfeed >= 0) ck(tasksProgress(sfeed) >= 1, "but it does count as FEED");
    gameEvent(EV_FEED, 1);
    ck(tasksProgress(sf) == p0 + 1, "the favourite does");
  }
  // real emitters: the minigame and training hooks in Pet reach the bus
  {
    uint32_t day = 0;
    TaskWorld w = taskWorldOf(pet);
    for (uint32_t d = DAY0 + 500; d < DAY0 + 900 && !day; d++) {
      uint8_t x[TASK_DAILY]; tasksPick(d, false, w, x);
      bool p = false, t = false;
      for (int i = 0; i < TASK_DAILY; i++) { p |= DAILY_TASKS[x[i]].kind == TK_PLAY; t |= DAILY_TASKS[x[i]].kind == TK_TRAIN; }
      if (p && t) day = d;
    }
    ck(day != 0, "some day offers PLAY and TRAIN together");
    setDay(pet, day); tasksRefresh();
    int sp = slotOfKind(TK_PLAY, false), st = slotOfKind(TK_TRAIN, false);
    pet.playResult(0);
    ck(tasksProgress(sp) == 0, "a ball game that scored nothing does not count as play");
    pet.playResult(7);
    ck(tasksProgress(sp) == 1, "Pet::playResult() emits EV_PLAY");
    pet.trainStrength(0);
    ck(tasksProgress(st) == 0, "a bag session with no hits does not count as training");
    pet.trainStrength(12);
    pet.trainSpeed(6);
    ck(tasksProgress(st) == 2, "trainStrength() and trainSpeed() each emit EV_TRAIN");
  }

  // ---- claim: once, items once, also across a reload; progress survives a reload
  {
    setDay(pet, DAY0 + 1000); tasksRefresh();
    int s = -1;
    for (uint8_t i = 0; i < TASK_DAILY; i++) if (evFor(tasksDef(i).kind) != EV_NONE) { s = i; break; }
    const TaskDef &d = tasksDef(s);
    GameEv e = evFor(d.kind);
    int16_t arg = d.kind == TK_PLAY ? 5 : 1;
    ck(!tasksClaim(s), "an unfinished task cannot be claimed");
    gameEvent(e, arg);
    reload(pet); tasksRefresh();
    ck(tasksProgress(s) >= 1, "progress survives a reload");
    for (int i = 0; i < 40 && !tasksDone(s); i++) gameEvent(e, arg);
    ck(tasksDone(s), "the task completes");
    ck(tasksProgress(s) == d.target, "progress stops at the target");
    gameEvent(e, arg);
    ck(tasksProgress(s) == d.target, "and stays there");
    ck(tasksClaimable() >= 1, "it is claimable");
    uint8_t had = bag.count(d.item);
    ck(tasksClaim(s), "claim succeeds");
    ck(bag.count(d.item) == had + d.qty, "and pays exactly its reward");
    ck(!tasksClaim(s), "a second claim is refused");
    ck(bag.count(d.item) == had + d.qty, "and pays nothing");
    reload(pet); tasksRefresh();
    ck(tasksClaimed(s), "claimed survives a reload");
    ck(!tasksClaim(s), "a claim after a reload is refused");
    Bag b2; b2.begin();
    ck(b2.count(d.item) == had + d.qty, "the bag on flash holds the reward once");
  }

  // ---- the bonus: only after all three dailies are claimed, once
  {
    setDay(pet, DAY0 + 1001); tasksRefresh();
    ck(!tasksBonusReady() && !tasksClaimBonus(), "no bonus with nothing claimed");
    for (uint8_t s = 0; s < TASK_DAILY; s++) {
      const TaskDef &d = tasksDef(s);
      if (d.kind == TK_WELL_KEPT) { for (int m = 0; m < d.target; m++) tasksTickAt(DAY0 + 1001, true); }
      else for (int i = 0; i < 40 && !tasksDone(s); i++) gameEvent(evFor(d.kind), d.kind == TK_PLAY ? 5 : 1);
      ck(tasksDone(s), "a daily completes");
      if (s < TASK_DAILY - 1) { tasksClaim(s); ck(!tasksBonusReady(), "no bonus until all three are claimed"); }
    }
    tasksClaim(TASK_DAILY - 1);
    ck(tasksBonusReady(), "all three claimed: the bonus is ready");
    uint8_t it = tasksBonusItem(), had = bag.count(it);
    ck(tasksClaimBonus(), "the bonus pays");
    ck(bag.count(it) == had + tasksBonusQty(), "its items, exactly");
    ck(!tasksClaimBonus(), "and only once");
    reload(pet); tasksRefresh();
    ck(tasksBonusClaimed() && !tasksClaimBonus(), "and not again after a reload");
    setDay(pet, DAY0 + 1002); tasksRefresh();
    ck(!tasksBonusClaimed() && !tasksClaimed(0), "a new day resets the claims and the bonus");
    ck(tasksProgress(0) == 0 || tasksDef(0).kind == TK_WELL_KEPT, "and the daily progress");
  }

  // ---- the week: resets on Monday, not before
  {
    uint32_t mon = DAY0 + 2 + 7 * 200;      // a Monday
    ck(tasksWeekOf(mon) == tasksWeekOf(mon + 6), "Monday and Sunday are one week");
    ck(tasksWeekOf(mon - 1) + 1 == tasksWeekOf(mon), "Sunday before is the week before");
    setDay(pet, mon); tasksRefresh();
    uint32_t wk = tasksWeek();
    int sw = -1;
    for (uint8_t s = TASK_DAILY; s < TASK_SLOTS; s++) if (evFor(tasksDef(s).kind) != EV_NONE) { sw = s; break; }
    uint8_t wkKinds[2] = { tasksDef(3).kind, tasksDef(4).kind };
    gameEvent(evFor(tasksDef(sw).kind), 5);
    uint8_t got = tasksProgress(sw);
    ck(got >= 1, "a weekly task moved");
    for (uint32_t d = mon + 1; d <= mon + 6; d++) {
      setDay(pet, d); tasksRefresh();
      if (tasksWeek() != wk || tasksProgress(sw) < got || tasksDef(3).kind != wkKinds[0] || tasksDef(4).kind != wkKinds[1]) {
        printf("      day %u (+%u) reset the week\n", d, d - mon); ck(false, "the week holds until Sunday"); break;
      }
    }
    ck(tasksProgress(sw) >= got, "the weekly task kept its progress through Sunday");
    setDay(pet, mon + 7); tasksRefresh();
    ck(tasksWeek() == wk + 1, "the next Monday is a new week");
    ck(tasksProgress(3) == 0 && tasksProgress(4) == 0, "and weekly progress reset");
  }

  // ---- the state task: the whole window, in a row
  {
    uint32_t day = 0;
    TaskWorld w = taskWorldOf(pet);
    for (uint32_t d = DAY0 + 3000; d < DAY0 + 3400 && !day; d++) {
      uint8_t x[TASK_DAILY]; tasksPick(d, false, w, x);
      for (int i = 0; i < TASK_DAILY; i++) if (DAILY_TASKS[x[i]].kind == TK_WELL_KEPT) day = d;
    }
    ck(day != 0, "some day offers KEEP IT WELL");
    setDay(pet, day); tasksRefresh();
    int s = slotOfKind(TK_WELL_KEPT, false);
    uint8_t tgt = tasksDef(s).target;
    for (int m = 0; m < tgt - 1; m++) tasksTickAt(day, true);
    ck(!tasksDone(s), "one minute short is not done");
    tasksTickAt(day, false);
    ck(tasksProgress(s) == 0, "a lapse starts the window over");
    for (int m = 0; m < tgt - 1; m++) tasksTickAt(day, true);
    ck(!tasksDone(s), "and the minutes before the lapse did not count");
    tasksTickAt(day, true);
    ck(tasksDone(s), "the full window, in a row, completes it");
    tasksTickAt(day, false);
    ck(tasksDone(s), "and a lapse afterwards does not undo it");
    // the condition itself, against the real Pet
    pet.fullness = pet.joy = pet.energy = pet.hygiene = 80;
    ck(tasksWellKept(pet), "every stat 80: well kept");
    pet.joy = TASK_WELL_MIN - 1;
    ck(!tasksWellKept(pet), "one stat under the floor: not");
    pet.joy = 80;
    // and tasksTick() reads it from the bound pet
    setDay(pet, day + 1); tasksTick();
    int s2 = slotOfKind(TK_WELL_KEPT, false);
    if (s2 >= 0) { tasksTick(); ck(tasksProgress(s2) == 2, "tasksTick() counts a minute from the live pet"); }
  }

  // ---- impossible tasks are never offered
  {
    // a fresh save: no badges, no items, a final form (CHARIZARD) that will
    // never evolve -- so no evolve or new-dex task is possible either
    bag.clearAll();
    Pet fresh; fresh.begin(); fresh.dbgHatchAs(6, false);
    fresh.ageMinutes = 1;
    setDay(fresh, DAY0);
    TaskWorld w = taskWorldOf(fresh);
    ck(!w.towerOpen && !w.hasBall && !w.hasHeld && !w.evolveToday && !w.evolveWeek && !w.dexNewWeek && !w.eggToday && !w.canBreed,
       "fresh world: tower shut, empty bag, nothing to hatch, evolve or breed");
    ck(w.gymsLeft, "fresh world: gyms to beat");
    int badOffer = 0;
    for (uint32_t d = 0; d < 2000; d++) {
      uint8_t x[TASK_DAILY], y[TASK_WEEKLY];
      tasksPick(DAY0 + d, false, w, x); tasksPick(DAY0 + d, true, w, y);
      for (int i = 0; i < TASK_DAILY; i++) { uint8_t k = DAILY_TASKS[x[i]].kind;
        if (k == TK_TOWER_WIN || k == TK_REMATCH_WIN || k == TK_WILD_CATCH || k == TK_ITEM_USED || k == TK_HATCH || k == TK_EVOLVE) badOffer++; }
      for (int i = 0; i < TASK_WEEKLY; i++) { uint8_t k = WEEKLY_TASKS[y[i]].kind;
        if (k == TK_TOWER_WIN || k == TK_REMATCH_WIN || k == TK_WILD_CATCH || k == TK_ITEM_USED || k == TK_BREED || k == TK_DEX_NEW || k == TK_EVOLVE) badOffer++; }
    }
    ck(badOffer == 0, "fresh save, 2000 days: no tower/rematch/catch/item/hatch/evolve/breed task");

    // every gym beaten (both difficulties are irrelevant: easy is what gates)
    for (uint8_t r = 0; r < GYM_REGIONS; r++) for (uint8_t i = 0; i < TRAINER_COUNT; i++) fresh.winBadge(r, i, false);
    TaskWorld w2 = taskWorldOf(fresh);
    ck(!w2.gymsLeft, "all badges: no gyms left");
    int gymOffer = 0;
    for (uint32_t d = 0; d < 2000; d++) {
      uint8_t x[TASK_DAILY], y[TASK_WEEKLY];
      tasksPick(DAY0 + d, false, w2, x); tasksPick(DAY0 + d, true, w2, y);
      for (int i = 0; i < TASK_DAILY; i++) if (DAILY_TASKS[x[i]].kind == TK_GYM_WIN) gymOffer++;
      for (int i = 0; i < TASK_WEEKLY; i++) if (WEEKLY_TASKS[y[i]].kind == TK_GYM_WIN) gymOffer++;
    }
    ck(gymOffer == 0, "no gyms left, 2000 days: no gym task");

    // ... and the companion: those gates DO open. Without this the checks above
    // would pass with every gated task deleted from the tables.
    TaskWorld all; memset(&all, 0xFF, sizeof(all));
    for (int i = 0; i < (int)sizeof(all); i++) ((uint8_t *)&all)[i] = 1;
    std::set<int> seenD, seenW;
    for (uint32_t d = 0; d < 2000; d++) {
      uint8_t x[TASK_DAILY], y[TASK_WEEKLY];
      tasksPick(DAY0 + d, false, all, x); tasksPick(DAY0 + d, true, all, y);
      for (int i = 0; i < TASK_DAILY; i++) seenD.insert(DAILY_TASKS[x[i]].kind);
      for (int i = 0; i < TASK_WEEKLY; i++) seenW.insert(WEEKLY_TASKS[y[i]].kind);
    }
    ck(seenD.size() == DAILY_TASK_COUNT, "everything possible: every daily row gets offered");
    ck(seenW.size() == WEEKLY_TASK_COUNT, "everything possible: every weekly row gets offered");
    ck(seenD.count(TK_TOWER_WIN) && seenD.count(TK_GYM_WIN) && seenD.count(TK_HATCH), "including tower, gym and hatch");

    // the REAL world flips too: the champion opens the tower, a ball allows a catch
    nvs().clear();   // fresh's badges are on the shared flash; start clean
    Pet champ; champ.begin(); champ.dbgHatchAs(4, false);
    tasksBegin(champ);
    ck(!taskWorldOf(champ).towerOpen, "no champion badge: tower shut");
    champ.winBadge(1, TRAINER_COUNT - 1, false);
    ck(taskWorldOf(champ).towerOpen, "a champion badge in ANY region opens the tower");
    bag.add(ITEM_POKEBALL, 1);
    ck(taskWorldOf(champ).hasBall, "a ball in the bag allows a catch task");
    champ.ageMinutes = (uint32_t)(DEX_TBL[4].evolveLevel - 3) * MINUTES_PER_LEVEL;
    ck(taskWorldOf(champ).evolveToday, "three levels from evolving: an evolve task is possible");
    champ.frozen = true;
    ck(!taskWorldOf(champ).evolveToday && !taskWorldOf(champ).evolveWeek, "a frozen companion never evolves");
    tasksBegin(pet);
  }

  // ---- the backup: the tasks key is in the table and round-trips
  {
    setDay(pet, DAY0 + 5000); tasksRefresh();
    gameEvent(EV_CLEAN, 0); gameEvent(EV_FEED, 1); gameEvent(EV_CARE_DAY, 1);
    uint8_t k1[TASK_SLOTS], p1[TASK_SLOTS];
    for (uint8_t s = 0; s < TASK_SLOTS; s++) { k1[s] = tasksDef(s).kind; p1[s] = tasksProgress(s); }
    std::set<std::string> inTable;
    for (uint16_t i = 0; i < SAVE_FIELD_COUNT; i++) inTable.insert(SAVE_FIELDS[i].key);
    ck(nvs().count("tasks") == 1, "the tasks blob is on flash");
    int missing = 0;
    for (auto &kv : nvs()) if (!inTable.count(kv.first)) { printf("      key '%s' is stored but NOT backed up\n", kv.first.c_str()); missing++; }
    ck(missing == 0, "every key written (tasks included) is in the backup table");
    static uint8_t buf[4096];
    size_t n = saveExport(buf, sizeof(buf));
    ck(n > 0, "the save exports");
    nvs().clear();
    ck(saveImport(buf, n), "and imports");
    reload(pet); tasksRefresh();
    bool same = true;
    for (uint8_t s = 0; s < TASK_SLOTS; s++) if (tasksDef(s).kind != k1[s] || tasksProgress(s) != p1[s]) same = false;
    ck(same, "tasks and progress survive an export/import");
  }

  // ---- a corrupt blob is dropped, not trusted
  {
    uint8_t junk[20]; memset(junk, 0xEE, sizeof(junk));
    Preferences p; p.begin("tamapoke"); p.putBytes("tasks", junk, sizeof(junk));
    reload(pet); tasksRefresh();
    bool sane = true;
    for (uint8_t s = 0; s < TASK_SLOTS; s++) if (tasksDef(s).kind == TK_NONE || tasksProgress(s) > tasksDef(s).target) sane = false;
    ck(sane && !tasksClaimed(0), "a garbage blob re-rolls clean tasks");
  }

  printf(bad ? "\n%d FAILED\n" : "\nall passed\n", bad);
  return bad ? 1 : 0;
}
