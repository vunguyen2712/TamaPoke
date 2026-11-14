// Wild creatures must FIGHT BACK.
//
// Reported from a board: in a wild fight the opponent never attacked. The
// engine was fine -- the foe simply had nothing to attack WITH. A foe's moves
// come from its learnset at its level, and at low levels that is often nothing
// (a level 3 MAGIKARP) or only status moves (CATERPIE: TACKLE + STRING SHOT,
// and the easy AI picked STRING SHOT half the time). Measured before the fix:
// 80 of 359 species that can spawn at level 3 had no damaging move at all.
//
// Driven through the REAL sketch: startBattle() + taps, the same path the
// WILD screen's FIGHT uses, and checked on the narration the player reads.
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include "battle.h"
#include "moves.h"
#include "dex.h"
#include "wild.h"
#include <cstdio>
#include <cstring>
#include <chrono>
#include <thread>
uint32_t g_seed = 31337; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX = 0, g_touchY = 0; volatile bool g_touchDown = false;
void FakeESP::restart() { exit(0); }
int FakeSerial::available() { return 0; }
String FakeSerial::readStringUntil(char) { return String(""); }
void setup(); void loop();
extern Pet pet;
extern bool battleOpen, btlOver;
extern Combatant btlYou, btlFoe;
extern uint8_t btlMsgCount, btlKind;
extern char btlMsg[6][40];
void startBattle(int16_t dex, uint8_t lvl);
#define BK_WILD 3
#define BTL_CELL_X(i) (69 + ((i) % 2) * 168)
#define BTL_CELL_Y(i) (286 + ((i) / 2) * 52)

static int bad = 0;
static void ck(bool ok, const char *w) { printf("%s  %s\n", ok ? "PASS" : "FAIL", w); if (!ok) bad++; }
static void pump(int n) { for (int i = 0; i < n; i++) { std::this_thread::sleep_for(std::chrono::milliseconds(30)); loop(); } }
static void click(int x, int y) {
  g_touchX = x; g_touchY = y; g_touchDown = true; emuFireInterrupt(); pump(5);
  g_touchDown = false; emuFireInterrupt(); pump(3);
}
static bool canAttack(const Combatant &c) {
  for (int i = 0; i < MOVE_SLOTS; i++) if (moveDamages(c.moves[i])) return true;
  return false;
}

// Plays a wild fight by tapping the first move; true if the foe was narrated
// using a move at least once before the fight ended.
static bool foeActed(int16_t dex, uint8_t lvl) {
  startBattle(dex, lvl);
  btlKind = BK_WILD;
  char want[40];
  snprintf(want, sizeof(want), "%s used", DEX_TBL[dex].name);
  bool acted = false;
  for (int t = 0; t < 120 && battleOpen; t++) {
    if (btlMsgCount) {
      for (int i = 0; i < btlMsgCount; i++) if (strstr(btlMsg[i], want)) acted = true;
      click(233, 320);
      continue;
    }
    // The player keeps the foe alive long enough to answer: GROWL-style
    // chip from a capped creature is not needed -- the foe gets a turn on
    // the first round whenever it survives the first hit, and the player's
    // HP is topped up so the fight cannot end the other way first.
    btlYou.hp = btlYou.maxHp;
    int slot = -1;
    for (int i = 0; i < MOVE_SLOTS; i++) if (btlYou.moves[i]) { slot = i; break; }
    if (slot < 0) break;
    click(233, 274);
    click(BTL_CELL_X(slot) + 40, BTL_CELL_Y(slot) + 20);
  }
  battleOpen = false;
  return acted;
}

int main() {
  setup(); pump(4);
  click(233, 138); pump(2);            // region: KANTO
  click(233, 145);                     // starter row 0
  if (pet.isEgg()) pet.dbgHatchAs(1, false);   // BULBASAUR, kept LOW so foes survive
  pet.ageMinutes = 4 * MINUTES_PER_LEVEL;      // level 5
  pet.relearnFromLevel();
  while (pet.hasLearnOffer()) pet.declineLearn();

  // ---- the reported case, through the sketch: a level 3 MAGIKARP knows
  // nothing by its learnset, and must still have something to fight with
  startBattle(129, 3);
  ck(battleOpen, "a wild MAGIKARP L3 fight starts");
  ck(canAttack(btlFoe), "a level 3 MAGIKARP is given a damaging move");
  battleOpen = false;
  ck(foeActed(129, 3), "and it actually attacks in the fight (narrated)");
  ck(foeActed(10, 4), "a CATERPIE L4 attacks too");
  ck(foeActed(16, 5), "a PIDGEY L5 attacks too");

  // ---- every creature that can spawn, at every level band, can attack.
  // Built exactly as the fight builds it: startBattle().
  int checked = 0, unable = 0;
  for (int lvl : { 2, 3, 8, 15, 30, 60, 96 }) {
    for (int16_t d = 1; d <= DEX_COUNT; d++) {
      if (!wildEligible(d, regionOfDex(d), (uint8_t)lvl)) continue;
      startBattle(d, (uint8_t)lvl);
      if (!battleOpen) continue;
      checked++;
      if (!canAttack(btlFoe)) { if (unable < 5) printf("      %s L%d cannot attack\n", DEX_TBL[d].name, lvl); unable++; }
      battleOpen = false;
    }
  }
  printf("      %d wild fights built\n", checked);
  ck(checked > 2000, "the sweep really built thousands of wild foes");
  ck(unable == 0, "every one of them has a damaging move");

  // ---- the guarantee never invents a move above the level gate: what it
  // adds is from the learnset at that level, or STRUGGLE
  startBattle(129, 3);
  bool onlyLegal = true;
  for (int i = 0; i < MOVE_SLOTS; i++) {
    uint8_t mv = btlFoe.moves[i];
    if (!mv) continue;
    if (mv == MV_STRUGGLE) continue;
    bool legal = false;
    for (uint8_t k = 0; k < learnCount(129); k++)
      if (learnMove(129, k) == mv && moveUnlockLevel(129, k) <= 3) legal = true;
    if (!legal) onlyLegal = false;
  }
  ck(onlyLegal, "MAGIKARP L3 gets STRUGGLE or a legal move, nothing above its level");
  battleOpen = false;

  // ---- a wild foe mostly attacks: TACKLE + GROWL picks TACKLE ~90%
  Combatant c;
  c.moves[0] = 1;                                     // TACKLE
  for (uint8_t m = 1; m < MOVE_COUNT; m++)
    if (!strcmp(MOVE_TBL[m].name, "GROWL")) { c.moves[1] = m; break; }
  ck(c.moves[1] != 0, "GROWL exists to test with");
  int tackle = 0;
  for (int i = 0; i < 4000; i++) if (aiChooseMoveWild(c, WILD_ATTACK_PCT) == 1) tackle++;
  printf("      TACKLE chosen %d / 4000\n", tackle);
  ck(tackle > 3400 && tackle < 3800, "a wild TACKLE+GROWL foe attacks about 90% of the time");
  int growl = 4000 - tackle;
  ck(growl > 200, "and still growls sometimes (it is wild, not a robot)");

  // ---- ...and the FIGHT really uses that choice: a wild foe holding TACKLE
  // + GROWL, both sides kept alive, narrated over many turns. With the old
  // uniform easy AI this lands near 50%.
  {
    startBattle(19, 5);                 // RATTATA
    btlKind = BK_WILD;
    btlFoe.moves[0] = 1; btlFoe.moves[1] = c.moves[1]; btlFoe.moves[2] = btlFoe.moves[3] = 0;
    int used = 0, tk = 0;
    for (int t = 0; t < 400 && battleOpen && used < 60; t++) {
      if (btlMsgCount) {
        for (int i = 0; i < btlMsgCount; i++) {
          if (strstr(btlMsg[i], "RATTATA used TACKLE")) { used++; tk++; }
          else if (strstr(btlMsg[i], "RATTATA used GROWL")) used++;
        }
        click(233, 320);
        continue;
      }
      btlYou.hp = btlYou.maxHp; btlFoe.hp = btlFoe.maxHp;   // nobody faints
      int slot = -1;
      for (int i = 0; i < MOVE_SLOTS; i++) if (btlYou.moves[i]) { slot = i; break; }
      if (slot < 0) break;
      click(233, 274);
      click(BTL_CELL_X(slot) + 40, BTL_CELL_Y(slot) + 20);
    }
    battleOpen = false;
    printf("      in a real wild fight: TACKLE %d of %d foe turns\n", tk, used);
    ck(used >= 40, "the foe took turns in the fight");
    ck(used && tk * 100 / used >= 75, "a wild foe in the sketch mostly attacks (not the uniform easy AI)");
  }

  // ---- the player's own side is never touched by the guarantee
  pet.dbgHatchAs(129, false);   // the player's own MAGIKARP L1: no moves
  pet.ageMinutes = 0; pet.relearnFromLevel();
  while (pet.hasLearnOffer()) pet.declineLearn();
  startBattle(16, 3);
  bool playerUntouched = !canAttack(btlYou) || pet.moves[0] == btlYou.moves[0];
  ck(playerUntouched, "the player's creature keeps exactly the moves it has");
  battleOpen = false;

  printf(bad ? "wildfight_ui_test: %d FAILED\n" : "wildfight_ui_test: all passed\n", bad);
  return bad ? 1 : 0;
}
