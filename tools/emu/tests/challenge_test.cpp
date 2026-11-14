// The weekly gym challenge, as LOGIC: challenge.cpp against the real Pet, Party,
// Bag, rosters and dex. The screen, the picker and the fight are driven through
// the real sketch in challenge_ui_test; this one pins the draw and the rules.
#include "Arduino.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include "items.h"
#include "tasks.h"
#include "challenge.h"
#include "game_events.h"
#include <cstdio>
#include <cstring>
uint32_t g_seed = 11; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX = 0, g_touchY = 0; volatile bool g_touchDown = false; bool wasPressed = false;
static uint32_t g_ms = 0; uint32_t millis() { return g_ms; }
void FakeESP::restart() { exit(0); }
int FakeSerial::available() { return 0; } String FakeSerial::readStringUntil(char) { return String(""); }
void sfxPlay(uint8_t) {}

static int bad = 0;
static void ck(bool ok, const char *w) { printf("%s  %s\n", ok ? "PASS" : "FAIL", w); if (!ok) bad++; }

static int evWin = 0;
static int16_t evArg = -1;
static void sink(GameEv ev, int16_t arg) { if (ev == EV_CHALLENGE_WIN) { evWin++; evArg = arg; } }

static bool same(const ChalWeek &a, const ChalWeek &b) {
  return a.week == b.week && a.region == b.region && a.leader == b.leader && a.rule == b.rule && a.param == b.param;
}
static ChalWorld everything(uint8_t mons) {
  ChalWorld w; memset(&w, 0, sizeof(w));
  for (int r = 0; r < GYM_REGIONS; r++) w.leaders[r] = (uint8_t)((1u << TRAINER_GYMS) - 1);
  w.types = (1UL << TYPE_COUNT) - 1;
  w.mons = mons;
  return w;
}
static uint32_t mondayFrom(uint32_t d) { while ((d + 3) % 7) d++; return d; }

int main() {
  gameEventSubscribe(sink);
  Pet p; p.begin();
  if (p.awaitingStarter()) p.chooseStarter(4);
  if (p.isEgg()) p.dbgHatchAs(7, false);
  p.dbgHatchAs(7, false);              // SQUIRTLE: WATER
  bag.begin(); bag.clearAll();
  party.begin();
  for (int i = 0; i < PARTY_SLOTS; i++) party.slots[i] = PartyMon();
  challengeBegin(); challengeClearAll();

  // ---- weeks: the tasks' rule, Monday to Sunday
  uint32_t mon = mondayFrom(20400);
  ck(challengeWeekOf(mon) == tasksWeekOf(mon), "the week is tasksWeekOf(), not a second rule");
  bool sameWeek = true;
  for (int i = 0; i < 7; i++) sameWeek &= challengeWeekOf(mon + i) == challengeWeekOf(mon);
  ck(sameWeek, "Monday..Sunday are one week");
  ck(challengeWeekOf(mon - 1) + 1 == challengeWeekOf(mon) && challengeWeekOf(mon + 7) == challengeWeekOf(mon) + 1,
     "the week changes on Monday, not on Sunday");

  // ---- the draw: deterministic, and different from one week to the next
  ChalWorld all = everything(7);
  bool det = true, differs = true, rulesSeen[CR_COUNT] = { false };
  uint32_t w0 = challengeWeekOf(mon);
  for (uint32_t w = w0; w < w0 + 60; w++) {
    ChalWeek a, b, c;
    det &= challengePick(w, all, a) && challengePick(w, all, b) && same(a, b);
    challengePick(w + 1, all, c);
    differs &= !same(a, c) && a.rule != c.rule;
    rulesSeen[a.rule] = true;
  }
  ck(det, "the same week and world draw the same leader and rule");
  ck(differs, "consecutive weeks never share a rule (it rotates)");
  bool allRules = true;
  for (int r = 0; r < CR_COUNT; r++) allRules &= rulesSeen[r];
  ck(allRules, "every rule in the table comes up");

  // ---- only playable regions (packs) and reached leaders, from the REAL world
  for (int r = 0; r < GYM_REGIONS; r++) for (int i = 0; i < TRAINER_GYMS; i++) p.winBadge(r, i, false);
  gRegionArt = (1u << 0) | (1u << 2);            // KANTO + HOENN packs only
  ChalWorld w2 = challengeWorldOf(p, party);
  bool onlyPlayable = true, saw0 = false, saw2 = false;
  for (uint32_t w = w0; w < w0 + 200; w++) {
    ChalWeek c; challengePick(w, w2, c);
    onlyPlayable &= c.region == 0 || c.region == 2;
    saw0 |= c.region == 0; saw2 |= c.region == 2;
  }
  ck(onlyPlayable && saw0 && saw2, "with two packs, only those two regions are drawn (both are)");
  gRegionArt = 0xFFFF;
  ChalWorld w3 = challengeWorldOf(p, party);
  bool other = false;
  for (uint32_t w = w0; w < w0 + 200; w++) { ChalWeek c; challengePick(w, w3, c); other |= c.region != 0 && c.region != 2; }
  ck(other, "companion: with every pack, other regions are drawn too");
  Pet fresh; fresh.dbgHatchAs(7, false);         // never begin()'d: no badges, never saves
  ChalWorld w4 = challengeWorldOf(fresh, party);
  bool firstOnly = true;
  for (uint32_t w = w0; w < w0 + 100; w++) { ChalWeek c; challengePick(w, w4, c); firstOnly &= c.leader == 0; }
  ck(firstOnly, "a player with no badges is only ever drawn each ladder's first leader");
  bool deeper = false;
  for (uint32_t w = w0; w < w0 + 100; w++) { ChalWeek c; challengePick(w, w3, c); deeper |= c.leader > 0; }
  ck(deeper, "companion: badges open deeper leaders");
  ChalWorld none; memset(&none, 0, sizeof(none)); none.mons = 3; none.types = 1;
  ChalWeek nc;
  ck(!challengePick(w0, none, nc), "no playable leader at all: nothing is drawn");

  // ---- plausible rules: TRIO needs 3 creatures, TYPE X is a type you own
  ChalWorld two = everything(2), three = everything(3);
  bool noTrio = true, trio = false;
  for (uint32_t w = w0; w < w0 + 30; w++) {
    ChalWeek c; challengePick(w, two, c); noTrio &= c.rule != CR_TRIO;
    challengePick(w, three, c); trio |= c.rule == CR_TRIO;
  }
  ck(noTrio && trio, "TRIO is drawn only for a player with at least three creatures");
  ChalWorld water = everything(1); water.types = 1UL << T_WATER;
  bool typeOk = true, typeSeen = false;
  for (uint32_t w = w0; w < w0 + 30; w++) {
    ChalWeek c; challengePick(w, water, c);
    if (c.rule == CR_TYPE) { typeSeen = true; typeOk &= c.param == T_WATER; }
  }
  ck(typeSeen && typeOk, "TYPE ONLY names a type the player owns");

  // ---- the rule: one predicate for a creature, and the squad builds on it
  ChalWeek t; t.rule = CR_TYPE; t.param = T_WATER;
  int16_t sq1[2] = { 7, 4 }, sq2[2] = { 7, 9 };
  ck(challengeAllowsMon(t, 7) && !challengeAllowsMon(t, 4), "TYPE WATER: SQUIRTLE yes, CHARMANDER no");
  ck(challengeAllowsMon(t, 130), "a dual type counts by either type (GYARADOS)");
  ck(!challengeAllowsSquad(t, sq1, 2) && challengeAllowsSquad(t, sq2, 2), "a squad with one wrong type is refused");
  ChalWeek s; s.rule = CR_SOLO;
  ck(challengeAllowsSquad(s, sq2, 1) && !challengeAllowsSquad(s, sq2, 2), "SOLO: exactly one");
  ChalWeek tr; tr.rule = CR_TRIO;
  int16_t sq4[4] = { 7, 4, 1, 25 };
  ck(!challengeAllowsSquad(tr, sq4, 2) && challengeAllowsSquad(tr, sq4, 3) && !challengeAllowsSquad(tr, sq4, 4),
     "TRIO: exactly three");
  ck(!challengeAllowsSquad(s, sq2, 0), "nobody is never a squad");
  ChalWeek ni; ni.rule = CR_NOITEMS;
  ck(challengeItemsOff(ni) && !challengeItemsOff(t), "NO ITEMS switches items off, nothing else does");

  // ---- the leader's team, adjusted
  TrainerMon tm[TRAINER_TEAM_MAX];
  ChalWeek koga; koga.region = 0; koga.leader = 4;   // KOGA: 37,39,37,43
  koga.rule = CR_SOLO;
  ck(challengeTeam(koga, tm) == 1 && tm[0].dex == 110 && tm[0].level == 43, "SOLO: the leader's ace alone");
  koga.rule = CR_TRIO;
  uint8_t n = challengeTeam(koga, tm);
  ck(n == 3 && tm[0].dex == 89 && tm[2].dex == 110, "TRIO: the leader's last three, ace last");
  koga.rule = CR_NOITEMS;
  ck(challengeTeam(koga, tm) == 4 && challengeYourCap(koga) == 43, "otherwise the real team; you are capped at its best");
  ChalWorld kw = everything(3);
  ChalWeek lv; bool found = false;
  for (uint32_t w = w0; w < w0 + 20000 && !found; w++) { challengePick(w, kw, lv); found = lv.rule == CR_LEVEL && lv.region == 0 && lv.leader == 4; }
  ck(found && lv.param == 43 - 43 / 4, "LEVEL CAP for KOGA is 43 - 43/4 = 33");
  n = challengeTeam(lv, tm);
  bool allAt = n == 4;
  for (int i = 0; i < n; i++) allAt &= tm[i].level == lv.param;
  ck(allAt && challengeYourCap(lv) == lv.param, "LEVEL CAP: the leader's whole team at the cap, and so is yours");

  // ---- unlock: one badge anywhere, either difficulty
  ck(!challengeUnlockedFor(fresh), "no badge: locked");
  fresh.badgesHardX[3] = 1;                     // a SINNOH hard badge only
  ck(challengeUnlockedFor(fresh), "a single hard badge in another region unlocks it");

  // ---- rolling, the reward once a week, and reload
  challengeClearAll();
  uint32_t day = mon + 2;
  ck(challengeRollTo(day, all) && challengeValid(), "a week is drawn");
  ChalWeek cur = challengeCurrent();
  ChalWeek expect; challengePick(challengeWeekOf(day), all, expect);
  ck(same(cur, expect), "the stored week is exactly the pure draw");
  challengeRollTo(day + 3, everything(1));       // same week, other world: kept
  ck(same(challengeCurrent(), cur), "a later look in the same week keeps the draw");
  challengeBegin();
  ck(same(challengeCurrent(), cur) && !challengeDone(), "and it is on flash");
  uint8_t ub = bag.count(ITEM_ULTRABALL), hd = challengeRewardHeld(cur.week), hb = bag.count(hd);
  ck(challengeWin(), "a win pays");
  ck(bag.count(ITEM_ULTRABALL) == ub + CHAL_BALLS && bag.count(hd) == hb + 1, "3 ULTRA BALL + this week's held item");
  ck(evWin == 1 && evArg == (int16_t)(cur.week & 0xFFFF), "EV_CHALLENGE_WIN(week)");
  ck(!challengeWin() && bag.count(ITEM_ULTRABALL) == ub + CHAL_BALLS && evWin == 1, "a second win the same week pays nothing");
  challengeBegin();
  ck(challengeDone() && challengeWins() == 1, "DONE survives a reload");
  ck(!challengeWin() && bag.count(ITEM_ULTRABALL) == ub + CHAL_BALLS, "and still pays nothing after it");
  challengeRollTo(day + 4, all);                 // Saturday, same week
  ck(challengeDone(), "still done on Saturday");
  challengeRollTo(mon + 7, all);                 // next Monday
  ck(!challengeDone() && challengeCurrent().week == cur.week + 1, "Monday brings a new, unpaid challenge");
  ck(challengeWin() && bag.count(ITEM_ULTRABALL) == ub + 2 * CHAL_BALLS && challengeWins() == 2, "which pays again");
  ck(challengeRewardHeld(1) != challengeRewardHeld(2), "the held item rotates by week");

  // ---- a pack leaving the card re-draws the week, keeping DONE
  uint32_t nd = mon + 14;
  challengeRollTo(nd, all);
  ChalWeek before = challengeCurrent();
  challengeWin();
  gRegionArt = 0xFFFF & ~(1u << before.region);
  if (before.region == 0) gRegionArt &= ~(1u << 1);   // keep it honest either way
  challengeRefreshFor(p, party, nd);
  ck(challengeCurrent().region != before.region && challengeDone(), "the leader's pack gone: re-drawn elsewhere, still DONE");
  gRegionArt = 0xFFFF;

  // ---- a stored blob is never trusted
  {
    Preferences pr; pr.begin("tamapoke", false);
    uint8_t b[12] = { 1, 99, 0, 0, 0, 0, 0, 0, 0, 0, 5, 0 };   // region 99
    pr.putBytes("chal", b, sizeof(b));
  }
  challengeBegin();
  ck(!challengeValid() && challengeWins() == 5, "an out-of-range week is dropped (the count kept)");

  printf(bad ? "challenge_test: %d FAILED\n" : "challenge_test: all passed\n", bad);
  return bad ? 1 : 0;
}
