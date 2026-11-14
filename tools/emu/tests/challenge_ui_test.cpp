// The weekly gym challenge through the REAL sketch: the hub row (locked and
// open), the CHALLENGE screen, the squad picker's PICK_CHALLENGE mode, the
// fight and what its end does. challenge_test owns the draw and the numbers;
// this one proves the CALLERS ask them (CLAUDE.md trap 1): the picker offers
// only what challengeAllowsMon() allows, a rule-breaking squad is refused at
// the start as well, caps and NO ITEMS touch only the copies, and the reward
// is paid once a week.
//
// Weeks are chosen by SEARCHING for a day whose draw has the rule a check
// needs (challengePick over the real world), then setting the RTC to it.
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include "battle.h"
#include "trainers.h"
#include "items.h"
#include "challenge.h"
#include "types.h"
#include "game_events.h"
#include <cstdio>
#include <cstring>
#include <chrono>
#include <thread>
uint32_t g_seed = 4404; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX = 0, g_touchY = 0; volatile bool g_touchDown = false;
void FakeESP::restart() { exit(0); }
int FakeSerial::available() { return 0; }
String FakeSerial::readStringUntil(char) { return String(""); }
void setup(); void loop(); void render();
void onSwipe(int dir);
void rtcSetEpoch(uint32_t e);
uint8_t uiCurrentScreen();
extern const char *const SCREEN_NAME[];
extern Arduino_Canvas *gfx;
extern Pet pet;
extern Party party;
extern bool battleOpen, btlWon, pickOpen, hubOpen, challengeOpen;
extern Combatant btlYou, btlFoe, btlSquad[];
extern uint8_t btlMsgCount, btlSquadN, btlKind, pickTrainer, hubPage;
extern int8_t btlTrainer;
extern uint32_t btlWinUntil;
extern TrainerMon btlFoeTeam[];
extern uint8_t btlFoeTeamN;
extern uint16_t squadMask;
uint8_t squadCap(uint8_t idx, bool hard);
uint8_t pickChosen();
uint8_t pickCandidates();
bool pickExists(uint8_t n);
void pickDefault(uint8_t cap);
void challengeHubLabel(char *out, size_t n, const char *name);
void challengePickFight();
void startTrainerBattle(uint8_t idx, bool hard);
#define BTL_CELL_X(i) (69 + ((i) % 2) * 168)
#define BTL_CELL_Y(i) (286 + ((i) / 2) * 52)
#define BK_GYM_ 1
#define BK_CHALLENGE_ 6    // the sketch's BK_CHALLENGE (an anonymous enum there)
#define PICK_CHALLENGE_ 0xFD

static int bad = 0;
static void ck(bool ok, const char *w) { printf("%s  %s\n", ok ? "PASS" : "FAIL", w); if (!ok) bad++; }

static int evWin = 0;
static int16_t evArg = -1;
static void sink(GameEv ev, int16_t a) { if (ev == EV_CHALLENGE_WIN) { evWin++; evArg = a; } }

static void pump(int n) {
  for (int i = 0; i < n; i++) { std::this_thread::sleep_for(std::chrono::milliseconds(30)); loop(); }
}
static void click(int x, int y) {
  g_touchX = x; g_touchY = y; g_touchDown = true; emuFireInterrupt(); pump(5);
  g_touchDown = false; emuFireInterrupt(); pump(3);
}
static bool on(const char *scr) { render(); return !strcmp(SCREEN_NAME[uiCurrentScreen()], scr); }
static bool flushes() { gfx->frameReady = false; render(); return gfx->frameReady; }
static void setDay(uint32_t day) {
  uint32_t e = day * 86400u + 12 * 3600u;
  rtcSetEpoch(e); pet.lastSeenEpoch = e;
}

static void tapMove() {
  int slot = -1;
  for (int i = 0; i < MOVE_SLOTS; i++) if (btlYou.moves[i]) { slot = i; break; }
  if (slot < 0) slot = 0;
  click(BTL_CELL_X(slot) + 40, BTL_CELL_Y(slot) + 20);
}
static void fightToWin() {
  for (int taps = 0; battleOpen && taps < 400; taps++) {
    if (btlWinUntil || btlMsgCount) { click(233, 320); continue; }
    btlFoe.hp = 1; btlYou.hp = btlYou.maxHp;
    tapMove();
  }
}
static void openChallengeFromHub() {
  hubOpen = true; challengeOpen = false; hubPage = 1; pump(2);
  click(233, 88 + 0 * 50 + 22);          // hub page 2, row 0: CHALLENGE
}
static void startRow() { click(233, 88 + 5 * 50 + 22); }       // START (feature row 5)
static void pickFight() { click(233 + 7 + 70, 350 + 22); }    // the picker's FIGHT
static void pickBack() { click(233 - 7 - 70, 350 + 22); }

// A day, from `from` on, whose week draws `rule` for this player.
static uint32_t dayWith(uint8_t rule, uint32_t from) {
  ChalWorld w = challengeWorldOf(pet, party);
  for (uint32_t d = from; d < from + 7 * 400; d += 7) {
    ChalWeek c;
    if (challengePick(challengeWeekOf(d), w, c) && c.rule == rule) return d;
  }
  return 0;
}
static int16_t candDex(uint8_t n) { return n == 0 ? pet.speciesId : party.slots[n - 1].dex; }
static bool candExistsRaw(uint8_t n) { return n == 0 ? !pet.isEgg() : !party.slots[n - 1].empty(); }
static bool hasType(int16_t d, uint8_t t) { return DEX_TBL[d].type1 == t || DEX_TBL[d].type2 == t; }

struct Snap { int16_t dex; uint16_t level; uint8_t held; };
static void snap(Snap *s) {
  for (int i = 0; i < PARTY_SLOTS; i++) { s[i].dex = party.slots[i].dex; s[i].level = party.slots[i].level; s[i].held = party.slots[i].held; }
}
static bool sameSnap(const Snap *a, const Snap *b) {
  for (int i = 0; i < PARTY_SLOTS; i++) if (a[i].dex != b[i].dex || a[i].level != b[i].level || a[i].held != b[i].held) return false;
  return true;
}

int main() {
  setup(); pump(4);
  click(233, 138); pump(2);            // region: KANTO
  click(233, 145);                     // starter row 0
  pet.dbgHatchAs(9, false);            // BLASTOISE: WATER
  pet.ageMinutes = 59 * MINUTES_PER_LEVEL;   // L60
  pet.relearnFromLevel();
  while (pet.hasLearnOffer()) pet.declineLearn();
  const int16_t dx[4] = { 6, 3, 26, 18 };    // CHARIZARD, VENUSAUR, RAICHU, PIDGEOT
  const uint8_t lv[4] = { 70, 55, 40, 45 };
  for (int i = 0; i < 4; i++) {
    PartyMon m; m.dex = dx[i]; m.level = lv[i];
    m.ivAtk = m.ivDef = m.ivSpe = m.ivHp = 20; m.moves[0] = 1;   // TACKLE
    party.replaceAt(i, m);
  }
  bag.add(ITEM_LEFTOVERS, 1);
  heldGive(pet, party, HOLDER_PET, ITEM_LEFTOVERS);
  ck(pet.held == ITEM_LEFTOVERS, "the live pet holds LEFTOVERS");
  gameEventSubscribe(sink);
  uint32_t day = 20450;
  setDay(day);

  // ---- locked until a badge
  char lbl[48];
  challengeHubLabel(lbl, sizeof(lbl), "CHALLENGE");
  ck(!challengeUnlockedFor(pet) && strstr(lbl, "BADGE"), "no badge: the hub row is greyed and says why");
  openChallengeFromHub();
  ck(!challengeOpen && hubOpen, "a locked CHALLENGE row cannot be entered");
  for (int i = 0; i < TRAINER_GYMS; i++) pet.winBadge(0, i, false);   // all of Kanto's gyms
  uint16_t badges0 = pet.badges, badgesH0 = pet.badgesHard;

  // ---- a TYPE ONLY week: the picker offers only that type
  uint32_t dT = dayWith(CR_TYPE, day);
  ck(dT != 0, "a TYPE ONLY week exists for this player");
  setDay(dT);
  openChallengeFromHub();
  ck(challengeOpen && on("challenge"), "the CHALLENGE row opens the screen");
  ck(flushes(), "the challenge screen flushes");
  ChalWeek cw = challengeCurrent();
  ck(cw.rule == CR_TYPE && cw.week == challengeWeekOf(dT), "it shows this week's draw");
  uint8_t elig = 0, total = 0;
  for (uint8_t n = 0; n <= PARTY_SLOTS; n++)
    if (candExistsRaw(n)) { total++; elig += hasType(candDex(n), cw.param); }
  printf("      TYPE %s: %u of %u eligible\n", typeName(cw.param), elig, total);
  startRow();
  ck(pickOpen && pickTrainer == PICK_CHALLENGE_, "START opens the picker in challenge mode");
  ck(flushes(), "the picker flushes in challenge mode");
  ck(pickCandidates() == elig && elig < total, "the picker offers exactly the creatures of that type");
  bool onlyType = true;
  for (uint8_t n = 0; n <= PARTY_SLOTS; n++)
    if (pickExists(n)) onlyType &= hasType(candDex(n), cw.param);
  ck(onlyType, "and none of another type");
  pickBack();
  ck(!pickOpen && challengeOpen && on("challenge"), "the picker's BACK returns to the challenge, not the gyms");

  // a rule-breaking squad, ticked behind the picker's back, is refused at start
  startRow();
  uint8_t wrong = 0xFF;
  for (uint8_t n = 0; n <= PARTY_SLOTS; n++) if (candExistsRaw(n) && !hasType(candDex(n), cw.param)) { wrong = n; break; }
  squadMask |= (uint16_t)(1 << wrong);
  pickFight();
  ck(!battleOpen && pickOpen, "FIGHT with a wrong-type creature ticked does nothing");
  pickOpen = false;
  challengePickFight();
  ck(!battleOpen && btlSquadN == 0, "and the start itself refuses it too");

  // the legal squad: fight, RUN, retry, win
  Snap before[PARTY_SLOTS], after[PARTY_SLOTS];
  snap(before);
  uint32_t age0 = pet.ageMinutes;
  startRow();
  pickFight();
  ck(battleOpen && btlKind == BK_CHALLENGE_ && btlTrainer == -1, "FIGHT starts a challenge battle");
  bool squadType = btlSquadN == elig;
  for (int i = 0; i < btlSquadN; i++) squadType &= hasType(btlSquad[i].dex, cw.param);
  ck(squadType, "everyone fighting is of that type");
  TrainerMon tm[TRAINER_TEAM_MAX];
  uint8_t tn = challengeTeam(cw, tm);
  bool sameTeam = btlFoeTeamN == tn;
  for (int i = 0; i < tn && sameTeam; i++) sameTeam &= btlFoeTeam[i].dex == tm[i].dex && btlFoeTeam[i].level == tm[i].level;
  ck(sameTeam, "the leader fights with the team the rule gives it");
  uint8_t cap = challengeYourCap(cw);
  bool capped = true;
  for (int i = 0; i < btlSquadN; i++) capped &= btlSquad[i].level <= cap;
  ck(capped, "your levels are capped to the leader's best");
  click(BTL_CELL_X(3) + 40, BTL_CELL_Y(3) + 20);   // RUN
  ck(!battleOpen && !challengeDone() && evWin == 0, "RUN: nothing paid, nothing done");
  ck(on("challenge"), "and the challenge screen is back");
  uint8_t ub = bag.count(ITEM_ULTRABALL), hd = challengeRewardHeld(cw.week), hb = bag.count(hd);
  startRow(); pickFight();
  ck(battleOpen, "a RUN can be retried the same week");
  fightToWin();
  ck(btlWon && !battleOpen && challengeDone(), "the win clears the week");
  ck(bag.count(ITEM_ULTRABALL) == ub + CHAL_BALLS && bag.count(hd) == hb + 1,
     "3 ULTRA BALL + the week's held item are bagged");
  ck(evWin == 1 && evArg == (int16_t)(cw.week & 0xFFFF), "EV_CHALLENGE_WIN(week)");
  ck(pet.badges == badges0 && pet.badgesHard == badgesH0, "no badge was touched");
  snap(after);
  ck(sameSnap(before, after) && pet.ageMinutes == age0, "nothing stored was touched");
  ck(on("challenge") && flushes(), "back on the challenge screen, which flushes (DONE)");
  challengeHubLabel(lbl, sizeof(lbl), "CHALLENGE");
  ck(strstr(lbl, "DONE") != nullptr, "the hub row says DONE");
  startRow();
  ck(!pickOpen && !battleOpen, "START is refused once the week is done");
  challengeBegin();
  ck(challengeDone() && !challengeWin() && bag.count(ITEM_ULTRABALL) == ub + CHAL_BALLS,
     "after a reload: still done, and a second payout is impossible");

  // ---- NO ITEMS: the copy holds nothing, the stored pet keeps its item.
  // Companion first: a SOLO week, where the pet (ticked first) brings LEFTOVERS.
  uint32_t dS = dayWith(CR_SOLO, dT + 7);
  setDay(dS);
  openChallengeFromHub();
  ck(challengeCurrent().rule == CR_SOLO, "a SOLO week");
  startRow();
  ck(pickOpen && squadCap(PICK_CHALLENGE_, false) == 1 && pickChosen() == 1, "SOLO: the picker asks for one");
  squadMask = 0x3;                       // two ticked
  pickFight();
  ck(!battleOpen, "SOLO: two creatures are refused");
  pickOpen = false; btlSquadN = 0;
  challengePickFight();                  // buildSquad would quietly trim two to one
  ck(!battleOpen && btlSquadN == 0, "SOLO: the start refuses two as well, rather than trimming");
  pickOpen = true;
  pickDefault(1);
  pickFight();
  ck(battleOpen && btlSquadN == 1 && btlFoeTeamN == 1, "SOLO: one against the leader's ace");
  ck(btlSquad[0].held == ITEM_LEFTOVERS, "companion: outside NO ITEMS the item comes along");
  click(BTL_CELL_X(3) + 40, BTL_CELL_Y(3) + 20);   // RUN
  uint32_t dN = dayWith(CR_NOITEMS, dS + 7);
  setDay(dN);
  openChallengeFromHub();
  ck(challengeCurrent().rule == CR_NOITEMS, "a NO ITEMS week");
  startRow(); pickFight();
  ck(battleOpen && btlSquad[0].dex == pet.speciesId, "the pet leads");
  ck(btlSquad[0].held == ITEM_NONE && btlYou.held == ITEM_NONE, "NO ITEMS: the fighting copy holds nothing");
  ck(pet.held == ITEM_LEFTOVERS, "while the stored pet keeps its LEFTOVERS");
  click(BTL_CELL_X(3) + 40, BTL_CELL_Y(3) + 20);   // RUN
  ck(pet.held == ITEM_LEFTOVERS, "and still has them after the fight");

  // ---- LEVEL CAP: yours capped, the leader's at the cap, stored levels untouched
  uint32_t dL = dayWith(CR_LEVEL, dN + 7);
  setDay(dL);
  openChallengeFromHub();
  ChalWeek lw = challengeCurrent();
  ck(lw.rule == CR_LEVEL, "a LEVEL CAP week");
  uint8_t lvl0 = pet.level();
  snap(before);
  startRow(); pickFight();
  bool under = battleOpen;
  for (int i = 0; i < btlSquadN; i++) under &= btlSquad[i].level <= lw.param;
  bool foeAt = btlFoeTeamN > 0;
  for (int i = 0; i < btlFoeTeamN; i++) foeAt &= btlFoeTeam[i].level == lw.param;
  ck(under && lw.param < lvl0, "everyone of yours fights at most at the cap (below the pet's own level)");
  ck(foeAt, "the leader's whole team is at the cap");
  click(BTL_CELL_X(3) + 40, BTL_CELL_Y(3) + 20);   // RUN
  snap(after);
  ck(pet.level() == lvl0 && sameSnap(before, after), "stored levels untouched");

  // ---- TRIO: exactly three
  uint32_t dR = dayWith(CR_TRIO, dL + 7);
  setDay(dR);
  openChallengeFromHub();
  ck(challengeCurrent().rule == CR_TRIO, "a TRIO week");
  startRow();
  ck(squadCap(PICK_CHALLENGE_, false) == 3 && pickChosen() == 3, "TRIO: three ticked by default");
  squadMask = 0x3;
  pickFight();
  ck(!battleOpen && pickOpen, "TRIO: two are refused");
  pickDefault(3);
  pickFight();
  ck(battleOpen && btlSquadN == 3, "TRIO: three fight");
  click(BTL_CELL_X(3) + 40, BTL_CELL_Y(3) + 20);   // RUN

  // ---- not paged: a horizontal swipe backs out to the hub
  ck(on("challenge"), "on the challenge screen");
  onSwipe(-1);
  ck(!challengeOpen && hubOpen, "a horizontal swipe goes back to the hub");

  // ---- a regular gym win is not a challenge win
  hubOpen = false; pump(1);
  uint16_t wins = challengeWins(); bool done = challengeDone();
  int ev0 = evWin;
  pet.badges &= ~(1u << 0);              // Brock again, as a new badge
  squadMask = 0x1;
  startTrainerBattle(0, false);
  ck(battleOpen && btlKind == BK_GYM_, "a gym battle is BK_GYM");
  fightToWin();
  ck(pet.hasBadge(0, 0, false), "the gym win still awards its badge");
  ck(challengeWins() == wins && challengeDone() == done && evWin == ev0, "and leaves the challenge alone");

  printf(bad ? "challenge_ui_test: %d FAILED\n" : "challenge_ui_test: all passed\n", bad);
  return bad ? 1 : 0;
}
