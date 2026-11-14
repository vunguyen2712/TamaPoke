// The Battle Tower through the REAL sketch: hub row, tower screen, the squad
// picker's tower mode, the battle, and what the end of a fight does to the run.
// tower_test owns the numbers; this one proves the CALLERS ask them -- that the
// fight really is level 50 on both sides, really is the team the screen showed,
// and that a win, a loss and a RUN each land on the streak (CLAUDE.md trap 1:
// test the caller, not just the rule).
//
// Outcomes are steered by setting HP between taps (the foe to 1 to win; our
// side to 1 with only a status move to lose); every step of the flow itself --
// taps, resolution, replacements, the ending -- is the firmware's own.
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include "battle.h"
#include "trainers.h"
#include "items.h"
#include "tower.h"
#include "game_events.h"
#include <cstdio>
#include <cstring>
#include <chrono>
#include <thread>
uint32_t g_seed = 515; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX = 0, g_touchY = 0; volatile bool g_touchDown = false;
void FakeESP::restart() { exit(0); }
int FakeSerial::available() { return 0; }
String FakeSerial::readStringUntil(char) { return String(""); }
void setup(); void loop(); void render();
void onSwipe(int dir);
uint8_t uiCurrentScreen();
extern const char *const SCREEN_NAME[];
extern Arduino_Canvas *gfx;
extern Pet pet;
extern Party party;
extern bool battleOpen, btlWon, pickOpen, hubOpen, towerOpen, btlHard;
extern Combatant btlYou, btlFoe, btlSquad[];
extern uint8_t btlMsgCount, btlMenu, btlSquadN, btlKind, pickTrainer;
extern int8_t btlTrainer;
extern uint32_t btlWinUntil;
extern TrainerMon btlFoeTeam[];
extern uint8_t btlFoeTeamN;
extern uint16_t squadMask;
uint8_t squadCap(uint8_t idx, bool hard);
uint8_t pickChosen();
void towerHubLabel(char *out, size_t n, const char *name);
#define BTL_CELL_X(i) (69 + ((i) % 2) * 168)
#define BTL_CELL_Y(i) (286 + ((i) / 2) * 52)
#define BK_TOWER_ 4        // the sketch's BK_TOWER (an anonymous enum there)
#define PICK_TOWER_ 0xFE

static int bad = 0;
static void ck(bool ok, const char *w) { printf("%s  %s\n", ok ? "PASS" : "FAIL", w); if (!ok) bad++; }

static int evWin = 0, evLoss = 0;
static int16_t winArg = -1, lossArg = -1;
static void sink(GameEv ev, int16_t a) {
  if (ev == EV_TOWER_WIN) { evWin++; winArg = a; }
  if (ev == EV_TOWER_LOSS) { evLoss++; lossArg = a; }
}

static void pump(int n) {
  for (int i = 0; i < n; i++) { std::this_thread::sleep_for(std::chrono::milliseconds(30)); loop(); }
}
static void click(int x, int y) {
  g_touchX = x; g_touchY = y; g_touchDown = true; emuFireInterrupt(); pump(5);
  g_touchDown = false; emuFireInterrupt(); pump(3);
}
static bool on(const char *scr) { render(); return !strcmp(SCREEN_NAME[uiCurrentScreen()], scr); }

static void tapMove() {
  int slot = -1;
  for (int i = 0; i < MOVE_SLOTS; i++) if (btlYou.moves[i]) { slot = i; break; }
  if (slot < 0) slot = 0;
  click(BTL_CELL_X(slot) + 40, BTL_CELL_Y(slot) + 20);   // FIGHT, then the move
}
static void fightToWin() {
  for (int taps = 0; battleOpen && taps < 400; taps++) {
    if (btlWinUntil || btlMsgCount) { click(233, 320); continue; }
    btlFoe.hp = 1;                  // any hit ends it
    btlYou.hp = btlYou.maxHp;       // and nothing ends us first
    tapMove();
  }
}
static uint8_t statusMove() {
  for (int m = 1; m < MOVE_COUNT; m++) if (MOVE_TBL[m].cat == MC_STATUS) return (uint8_t)m;
  return 0;
}
static void fightToLose() {
  uint8_t st = statusMove();
  for (int i = 0; i < btlSquadN; i++) {
    btlSquad[i].hp = 1;
    for (int k = 0; k < MOVE_SLOTS; k++) btlSquad[i].moves[k] = k ? 0 : st;
  }
  for (int taps = 0; battleOpen && taps < 900; taps++) {
    if (btlWinUntil || btlMsgCount) { click(233, 320); continue; }
    btlYou.hp = 1;
    for (int k = 0; k < MOVE_SLOTS; k++) btlYou.moves[k] = k ? 0 : st;
    btlFoe.hp = btlFoe.maxHp;
    tapMove();
  }
}
static void openTowerFromHub() {
  hubOpen = true; towerOpen = false; pump(2);
  click(233, 88 + 2 * 50 + 22);          // hub row 2: TOWER
}
static void startFromTower() {
  click(233, 88 + 5 * 50 + 22);          // START / CONTINUE (feature row 5)
  click(233 + 7 + 70, 350 + 22);         // the picker's FIGHT
}

struct Snap { int16_t dex; uint16_t level; uint8_t iv[4]; uint8_t mv[MOVE_SLOTS]; };
static void snap(Snap *s) {
  for (int i = 0; i < PARTY_SLOTS; i++) {
    const PartyMon &m = party.slots[i];
    s[i].dex = m.dex; s[i].level = m.level;
    s[i].iv[0] = m.ivAtk; s[i].iv[1] = m.ivDef; s[i].iv[2] = m.ivSpe; s[i].iv[3] = m.ivHp;
    memcpy(s[i].mv, m.moves, MOVE_SLOTS);
  }
}
static bool sameSnap(const Snap *a, const Snap *b) {
  for (int i = 0; i < PARTY_SLOTS; i++)
    if (a[i].dex != b[i].dex || a[i].level != b[i].level || memcmp(a[i].iv, b[i].iv, 4) ||
        memcmp(a[i].mv, b[i].mv, MOVE_SLOTS)) return false;
  return true;
}

int main() {
  setup(); pump(4);
  click(233, 138); pump(2);            // region: KANTO
  click(233, 145);                     // starter row 0
  if (pet.isEgg()) pet.dbgHatchAs(9, false);
  ck(!pet.isEgg() && !pet.awaitingStarter(), "a hatched creature to fight with");
  pet.ageMinutes = 99 * MINUTES_PER_LEVEL;   // L100, so the cap down to 50 is visible
  pet.relearnFromLevel();
  while (pet.hasLearnOffer()) pet.declineLearn();
  // banked: one well under 50 and one well over, so raising AND capping both show
  const int16_t dx[3] = { 6, 3, 65 };
  const uint8_t lv[3] = { 20, 90, 35 };
  for (int i = 0; i < 3; i++) {
    PartyMon m; m.dex = dx[i]; m.level = lv[i];
    m.ivAtk = m.ivDef = m.ivSpe = m.ivHp = 20; m.moves[0] = 1;   // TACKLE
    party.replaceAt(i, m);
  }
  gameEventSubscribe(sink);

  // ---- locked until a champion falls
  ck(!towerUnlocked(), "fresh save: the tower is locked");
  char lbl[40];
  towerHubLabel(lbl, sizeof(lbl), "TOWER");
  ck(strstr(lbl, "CHAMPION") != nullptr, "the locked hub row says why");
  openTowerFromHub();
  ck(!towerOpen && hubOpen, "a locked TOWER row cannot be entered");
  pet.winBadge(1, TRAINER_COUNT - 2, false);
  openTowerFromHub();
  ck(!towerOpen, "the last Elite 4 member is not enough");
  pet.winBadge(1, TRAINER_COUNT - 1, false);       // JOHTO's champion
  ck(towerUnlocked(), "a champion -- in Johto, not Kanto -- unlocks it");
  openTowerFromHub();
  ck(towerOpen && on("tower"), "the TOWER row opens the tower screen");
  gfx->frameReady = false; render();
  ck(gfx->frameReady, "the unlocked tower screen flushes");

  // ---- picker: tower mode, three at level 50
  Snap before[PARTY_SLOTS], after[PARTY_SLOTS];
  snap(before);
  uint32_t age0 = pet.ageMinutes;
  uint8_t lvl0 = pet.level();
  TowerTeam shown;
  towerTeamFor(towerFloor(), towerSeed(), shown);
  click(233, 88 + 5 * 50 + 22);                    // START
  ck(pickOpen && pickTrainer == PICK_TOWER_, "START opens the squad picker in tower mode");
  ck(squadCap(PICK_TOWER_, false) == TOWER_TEAM && pickChosen() == TOWER_TEAM, "it asks for exactly three");
  click(233 - 7 - 70, 350 + 22);                   // BACK
  ck(!pickOpen && on("tower"), "the picker's BACK returns to the tower, not the gyms");
  startFromTower();
  ck(battleOpen && btlKind == BK_TOWER_ && btlTrainer == -1, "FIGHT starts a tower battle");
  bool all50 = btlSquadN == TOWER_TEAM;
  for (int i = 0; i < btlSquadN; i++) all50 &= btlSquad[i].level == TOWER_LEVEL;
  ck(all50, "our three fight at exactly 50: the L100 pet and the L90 capped, the L20 raised");
  bool foe50 = btlFoe.level == TOWER_LEVEL && btlFoeTeamN == TOWER_TEAM;
  bool sameAsShown = btlFoeTeamN == shown.count;
  for (int i = 0; i < btlFoeTeamN; i++) {
    foe50 &= btlFoeTeam[i].level == TOWER_LEVEL;
    sameAsShown &= btlFoeTeam[i].dex == shown.mon[i].dex;
  }
  ck(foe50, "the foe's three are level 50 too");
  ck(sameAsShown && btlFoe.dex == (int16_t)shown.mon[0].dex, "the team fought is the team the screen showed");
  ck(!btlHard, "floor 1 has the easy AI");
  snap(after);
  ck(sameSnap(before, after) && pet.ageMinutes == age0 && pet.level() == lvl0,
     "and nothing stored was touched to do it");

  // ---- a win
  bool replacementsFromTeam = true;
  {
    int16_t seen = btlFoe.dex; int idx = 0;
    for (int taps = 0; battleOpen && taps < 400; taps++) {
      if (btlFoe.dex != seen) { seen = btlFoe.dex; idx++; if (idx >= TOWER_TEAM || btlFoe.dex != (int16_t)shown.mon[idx].dex) replacementsFromTeam = false; }
      if (btlWinUntil || btlMsgCount) { click(233, 320); continue; }
      btlFoe.hp = 1; btlYou.hp = btlYou.maxHp;
      tapMove();
    }
    ck(idx == TOWER_TEAM - 1, "all three of the foe's creatures came out");
  }
  ck(replacementsFromTeam, "every replacement came from the battle's own team, in order");
  ck(btlWon && !battleOpen, "the floor is won and the battle closes");
  ck(towerStreak() == 1 && towerBest() == 1, "streak 1, best 1");
  ck(evWin == 1 && winArg == 1, "EV_TOWER_WIN(1)");
  ck(on("tower"), "and we are back on the tower screen");
  snap(after);
  ck(sameSnap(before, after) && pet.ageMinutes == age0, "still nothing stored touched after the fight");
  towerBegin();
  ck(towerStreak() == 1 && towerRunActive(), "the streak is on flash");

  // ---- the 7th win pays, through the real flow
  for (int i = 0; i < 5; i++) towerWin(nullptr);   // streak 6: floor 7 is next
  uint8_t ub = bag.count(ITEM_ULTRABALL);
  startFromTower();
  ck(battleOpen && btlHard, "floor 7 has the smart AI");
  fightToWin();
  ck(towerStreak() == 7 && bag.count(ITEM_ULTRABALL) == ub + 3 && bag.count(ITEM_SITRUS_BERRY) >= 2,
     "clearing floor 7 bags the reward");
  gfx->frameReady = false; render();
  ck(gfx->frameReady && on("tower"), "the reward screen flushes");

  // ---- RUN ends the run
  startFromTower();
  ck(battleOpen, "floor 8 starts");
  click(BTL_CELL_X(3) + 40, BTL_CELL_Y(3) + 20);   // RUN
  ck(!battleOpen && towerStreak() == 0 && towerBest() == 7, "RUN ends the run; best 7 kept");
  ck(evLoss == 1 && lossArg == 7, "EV_TOWER_LOSS(7) on the RUN");
  ck(on("tower"), "and the tower screen is back");

  // ---- a real loss ends it too
  startFromTower(); fightToWin();
  ck(towerStreak() == 1, "a new run: 1");
  startFromTower();
  fightToLose();
  ck(!battleOpen && !btlWon, "the second floor is lost");
  ck(towerStreak() == 0 && towerBest() == 7 && evLoss == 2 && lossArg == 1, "a loss resets the streak, keeps the best");
  towerBegin();
  ck(towerStreak() == 0 && towerBest() == 7, "both on flash");

  // ---- not paged: a horizontal swipe backs out to the hub
  onSwipe(-1);
  ck(!towerOpen && hubOpen, "a horizontal swipe on the tower goes back to the hub");

  // ---- a gym fight never touches the run
  towerWin(nullptr);
  uint16_t s = towerStreak();
  hubOpen = false; pump(1);
  void startTrainerBattle(uint8_t idx, bool hard);
  startTrainerBattle(0, false);
  fightToWin();
  ck(towerStreak() == s, "a gym win leaves the tower streak alone");

  printf(bad ? "tower_ui_test: %d FAILED\n" : "tower_ui_test: all passed\n", bad);
  return bad ? 1 : 0;
}
