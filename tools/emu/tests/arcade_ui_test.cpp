// The ARCADE (ui_arcade.ino), driven through the REAL sketch: the hub row that
// opens it, the picker, and each of the four games -- BERRY CATCH (catch /
// gold / rock / drop / lives / drag), MEMORY (match / mismatch / combo / clear
// and re-deal), ECHO (playback, right pad, round, wrong pad, idle, lives) and
// POP-UP (critter / gold / bomb / duck / whiff / no early bombs / the cap).
// Then the shared rules: grades, rewards through Pet::friendVisit (joy, the
// daily bond cap), EV_PLAY, the once-a-day prize, bests surviving a reload,
// every frame flushing, leaving banking exactly once, and the guard that no
// game runs while the pet is an egg, asleep or in a ceremony.
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include "items.h"
#include "i18n.h"
#include "game_events.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <chrono>
#include <thread>
uint32_t g_seed = 4242; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX = 0, g_touchY = 0; volatile bool g_touchDown = false;
void FakeESP::restart() { exit(0); }
int FakeSerial::available() { return 0; }
String FakeSerial::readStringUntil(char) { return String(""); }
void setup(); void loop(); void render(); void onTap(int16_t, int16_t); void onSwipe(int); void onSwipeV(int);
extern Pet pet;
extern Arduino_Canvas *gfx;
extern const char *const SCREEN_NAME[];
uint8_t uiCurrentScreen();
extern bool hubOpen, arcadeOpen, trainOpen; extern uint8_t hubPage;
void featBackToHub();
// shared training helpers the arcade borrows
extern uint32_t trnGoAt; extern uint16_t trnCombo, trnBestCombo;
// arcade
extern uint8_t arcGame; extern bool arcOver; extern uint32_t arcUntil, arcOverAt;
extern uint16_t arcPts, arcBest[4], arcSessions; extern uint8_t arcGrade, arcJoyGot, arcBondGot, arcPrizeGot;
extern uint32_t arcPrizeDay; extern bool arcNewHi, arcAllSGot; extern uint8_t arcFirstGot, arcFirstS, arcPaid[4];
uint8_t arcPaidToday(uint8_t g); bool arcadePrizeToday();
void arcStart(uint8_t); void arcFinish(); void arcLeave(); void arcLoad(); void renderArcade();
void arcadeTap(int16_t, int16_t); void arcadeTouchPeek(bool, int16_t, int16_t);
uint8_t arcGradeOf(uint8_t, uint16_t); uint8_t arcJoyFor(uint8_t); uint8_t arcBondFor(uint8_t);
bool arcadeAllowed(); int arcRowAt(int16_t, int16_t);
// catch
extern uint8_t catK[6]; extern float catX[6], catY[6], catV[6], catPetX; extern int16_t catGoal;
extern uint8_t catLives; extern uint32_t catNext, catStunUntil; extern uint16_t catCaught, catDropped, catRocks;
void catSpawnAt(uint8_t, uint8_t, int16_t); uint8_t catStep(uint32_t);
// memory
extern uint8_t memType[16], memState[16]; extern int8_t memA, memB; extern uint32_t memHideAt, memDealAt;
extern uint16_t memPairs, memMisses, memBoards;
uint8_t memTap(int16_t, int16_t); void memDeal(); int memCardAt(int16_t, int16_t);
// echo
extern uint8_t echoSeq[32], echoLen, echoPos, echoState, echoLives; extern uint32_t echoT0; extern int8_t echoLit;
extern uint16_t echoRounds;
uint8_t echoTap(int16_t, int16_t); void echoStep(uint32_t); int echoPadAt(int16_t, int16_t);
int16_t echoPadX(uint8_t); int16_t echoPadY(uint8_t); uint16_t echoOnMs();
// pop
extern uint8_t popK[7]; extern uint32_t popBorn[7], popHitAt[7]; extern uint16_t popLife[7];
extern uint32_t popNext; extern uint16_t popHits, popGolds, popBombs, popMissed, popWhiffs;
uint8_t popTap(int16_t, int16_t); void popStep(uint32_t); void popSpawnAt(uint8_t, uint8_t);
int16_t popHoleX(uint8_t); int16_t popHoleY(uint8_t);

// mirrors of the constants under test (asserted against behaviour, not trusted)
enum { G_CATCH = 0, G_MEMORY, G_ECHO, G_POP };
#define NONE 0xFF
#define CAT_K_BERRY 1
#define CAT_K_GOLD 2
#define CAT_K_ROCK 3
#define CAT_CATCH_Y 352
#define MEM_T_NONE 0
#define MEM_T_FLIP 1
#define MEM_T_MATCH 2
#define MEM_T_MISS 3
#define MEM_T_CLEAR 4
#define ECHO_SHOW 0
#define ECHO_INPUT 1
#define ECHO_PAUSE 2
#define ECHO_T_NONE 0
#define ECHO_T_OK 1
#define ECHO_T_ROUND 2
#define ECHO_T_WRONG 3
#define POP_K_MOLE 1
#define POP_K_GOLD 2
#define POP_K_BOMB 3
#define POP_T_HIT 1
#define POP_T_GOLD 2
#define POP_T_BOMB 3
#define POP_T_WHIFF 4

static int bad = 0;
static void ck(bool ok, const char *w) { printf("%s  %s\n", ok ? "PASS" : "FAIL", w); if (!ok) bad++; }
static void slp(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
static bool flushes() { gfx->frameReady = false; render(); return gfx->frameReady; }
static bool onScreen(const char *n) { return !strcmp(SCREEN_NAME[uiCurrentScreen()], n); }
static void skipIntro() { trnGoAt = millis() - 1; }
static int evPlay = 0, evPlayArg = -1;
static void sink(GameEv ev, int16_t arg) { if (ev == EV_PLAY) { evPlay++; evPlayArg = arg; } }
static void freshPet() {
  pet.sleeping = false; pet.ceremony = CER_NONE; pet.energy = 100; pet.fullness = 100; pet.joy = 40;
}
// a card's centre on the memory board
static void memXY(int i, int &x, int &y) {
  int x0 = 233 - (4 * 62 + 3 * 8) / 2;
  x = x0 + (i % 4) * 70 + 31; y = 116 + (i / 4) * 70 + 31;
}
static int memPartner(int i) { for (int k = 0; k < 16; k++) if (k != i && memType[k] == memType[i]) return k; return -1; }
static int memOther(int i) { for (int k = 0; k < 16; k++) if (memType[k] != memType[i] && memState[k] == 0) return k; return -1; }
static void openArcade() { featBackToHub(); hubOpen = false; trainOpen = false; arcadeOpen = true; arcGame = NONE; arcOver = false; arcLoad(); }

int main() {
  setup(); for (int i = 0; i < 4; i++) loop();
  if (pet.awaitingStarter()) pet.chooseStarter(4);
  if (pet.isEgg()) pet.dbgHatchAs(6, false);
  while (pet.hasLearnOffer()) pet.declineLearn();
  pet.dbgSetSeen(1700000000UL);
  freshPet();
  gameEventSubscribe(sink);

  // ---- the strings: six languages, none empty, ASCII only (the tab itself)
  {
    std::string src = __FILE__;                 // <root>/tools/emu/tests/arcade_ui_test.cpp
    src = src.substr(0, src.rfind("/tools/emu/tests/")) + "/ui_arcade.ino";
    FILE *f = fopen(src.c_str(), "rb");
    bool ascii = f != nullptr;
    if (f) { int c; while ((c = fgetc(f)) != EOF) if (c > 126 || (c < 32 && c != '\n' && c != '\t' && c != '\r')) ascii = false; fclose(f); }
    ck(ascii, "ui_arcade.ino is pure ASCII (the font has no accents)");
  }

  // =================================================================== ENTRY
  printf("-- the TRAIN panel's row\n");
  hubOpen = true; hubPage = 1;
  ck(flushes(), "hub page 2 flushes");
  onTap(233, 88 + 2 * 50 + 22);          // where ARCADE used to be: page 2, third row
  ck(!arcadeOpen, "the Adventure hub no longer has an ARCADE row");
  hubOpen = false; hubPage = 0;
  trainOpen = true;
  ck(flushes(), "the training menu with its ARCADE row flushes");
  onTap(233, 126 + 3 * 64 + 28);         // TRAIN_ROW_Y(3): the fourth row
  ck(arcadeOpen && !trainOpen && onScreen("arcade"), "TRAIN's fourth row (ARCADE) opens the arcade");
  ck(arcGame == NONE && flushes(), "it opens on the picker, which flushes");
  ck(arcRowAt(233, 88 + 20) == 0 && arcRowAt(233, 88 + 3 * 66 + 20) == 3 && arcRowAt(233, 30) == -1, "picker rows map to the four games");
  onSwipe(1);
  ck(!arcadeOpen && trainOpen && !hubOpen, "a horizontal swipe on the picker goes back to the training menu");
  onTap(233, 126 + 3 * 64 + 28);
  onSwipeV(1);
  ck(!arcadeOpen && trainOpen, "a vertical swipe on the picker goes back to the training menu");
  onTap(233, 126 + 3 * 64 + 28);
  onTap(233, 394);                       // FEAT_BACK pill
  ck(!arcadeOpen && trainOpen, "the picker's BACK pill goes back to the training menu");
  trainOpen = false;
  // guards: no game while asleep / an egg / a ceremony -- the row AND the start
  pet.sleeping = true;
  trainOpen = true; render();
  onTap(233, 126 + 3 * 64 + 28);
  ck(!arcadeOpen, "asleep: the TRAIN row refuses to open the arcade");
  trainOpen = false;
  arcadeOpen = true; hubOpen = false; arcGame = NONE;
  arcStart(G_CATCH);
  ck(arcGame == NONE, "asleep: arcStart refuses too (the twin path)");
  pet.sleeping = false;
  int16_t keep = pet.speciesId; pet.speciesId = -1;
  arcStart(G_POP);
  ck(arcGame == NONE && !arcadeAllowed(), "an egg cannot play");
  pet.speciesId = keep;
  pet.ceremony = CER_FAREWELL; arcStart(G_ECHO);
  ck(arcGame == NONE, "nor during a ceremony");
  pet.ceremony = CER_NONE;
  // falling asleep mid-game banks and returns to the list
  arcStart(G_MEMORY); skipIntro();
  ck(arcGame == G_MEMORY, "awake, the game starts");
  uint16_t sBefore = arcSessions;
  pet.sleeping = true; render();
  ck(arcGame == NONE && arcSessions == sBefore + 1, "the pet falling asleep mid-game banks it and closes it");
  pet.sleeping = false;

  // the hint and countdown
  uint32_t t0 = millis();
  onTap(233, 88 + 20);                    // picker row 0 = BERRY CATCH
  ck(arcGame == G_CATCH && trnGoAt >= t0 + 1700 && trnGoAt <= t0 + 1900, "a picker row starts its game on a 1.8 s countdown");
  ck(arcUntil == trnGoAt + 45000, "BERRY CATCH: the 45 s clock starts after it");
  bool allFlush = flushes();
  arcadeTap(300, 300);
  ck(catGoal == 233, "a tap during the countdown does nothing");

  // =================================================================== CATCH
  printf("-- berry catch\n");
  skipIntro();
  for (int i = 0; i < 6; i++) catK[i] = 0;
  catNext = millis() + 100000;            // no random drops while we test
  catPetX = 233; catGoal = 233;
  catSpawnAt(0, CAT_K_BERRY, 233); catY[0] = CAT_CATCH_Y - 1;
  uint8_t ev = catStep(10);
  ck((ev & 1) && arcPts == 1 && catCaught == 1 && catK[0] == 0, "a berry into the basket: caught, +1");
  for (int k = 0; k < 4; k++) { catSpawnAt(0, CAT_K_BERRY, 233); catY[0] = CAT_CATCH_Y - 1; catStep(10); }
  ck(trnCombo == 5 && arcPts == 1 + 1 + 1 + 1 + 2, "the 5th in a row is worth 2 (combo x2)");
  catSpawnAt(0, CAT_K_GOLD, 240); catY[0] = CAT_CATCH_Y - 1;
  uint16_t p0 = arcPts; catStep(10);
  ck(arcPts == p0 + 5 + 2 - 1, "a golden berry is worth 5 + mult - 1");
  // an off-basket berry falls through, lands, breaks the combo
  catSpawnAt(1, CAT_K_BERRY, 104); catY[1] = CAT_CATCH_Y - 1;
  p0 = arcPts;
  catStep(10);
  ck(catK[1] == CAT_K_BERRY && arcPts == p0, "a berry beside the basket is not caught");
  for (int k = 0; k < 40 && catK[1]; k++) catStep(50);
  ck(catDropped == 1 && trnCombo == 0 && arcPts == p0, "it lands: dropped, combo broken, no points lost");
  // a rock on the ground beside the basket is harmless; in it costs a life
  catSpawnAt(2, CAT_K_ROCK, 104); catY[2] = CAT_CATCH_Y - 1;
  for (int k = 0; k < 40 && catK[2]; k++) catStep(50);
  ck(catLives == 3 && catRocks == 0, "a rock missing the basket costs nothing");
  catSpawnAt(2, CAT_K_ROCK, 233); catY[2] = CAT_CATCH_Y - 1;
  catStep(10);
  ck(catLives == 2 && catRocks == 1 && trnCombo == 0, "a rock in the basket: a life gone, combo broken");
  catGoal = 362; float px = catPetX; catStep(50);
  ck(catPetX == px, "and the basket is stunned (cannot move) for a moment");
  catStunUntil = millis() - 1; catStep(50);
  ck(catPetX > px && catPetX <= px + 0.9f * 50 + 0.5f, "after the stun it runs at 0.9 px/ms toward the finger");
  // steering: a held finger (handleTouch's peek) and a tap
  arcadeTouchPeek(true, 140, 300);
  ck(catGoal == 140, "a finger held down steers the basket");
  arcadeTouchPeek(true, 400, 30);
  ck(catGoal == 140, "...but not from the header strip");
  arcadeTouchPeek(true, 900, 300);
  ck(catGoal == 362, "...and clamped to the ground the panel shows");
  arcadeTap(200, 300);
  ck(catGoal == 200, "a tap sends the basket there");
  onSwipe(1);
  ck(arcGame == G_CATCH && !arcOver, "a horizontal swipe does NOT leave BERRY CATCH (a drag is the control)");
  for (int f = 0; f < 6; f++) { allFlush &= flushes(); slp(20); }
  // the last rocks end it
  catPetX = 233;
  catSpawnAt(2, CAT_K_ROCK, 233); catY[2] = CAT_CATCH_Y - 1; catStep(10);
  catStunUntil = 0;
  catSpawnAt(2, CAT_K_ROCK, 233); catY[2] = CAT_CATCH_Y - 1;
  uint16_t s0 = arcSessions;
  catStep(10);
  ck(catLives == 0 && arcOver && arcSessions == s0 + 1, "the third rock ends the game (banked once)");
  allFlush &= flushes();
  ck(arcBest[G_CATCH] == arcPts && arcNewHi, "the score is the new best");
  arcadeTap(233, 233);
  ck(arcGame == G_CATCH && arcOver, "a tap in the first 0.7 s does not dismiss the card");
  arcOverAt = millis() - 800;
  arcadeTap(233, 233);
  ck(arcGame == NONE, "after that a tap goes back to the picker");

  // =================================================================== MEMORY
  printf("-- memory\n");
  arcStart(G_MEMORY);
  ck(arcUntil == trnGoAt + 60000, "MEMORY: 60 s");
  {
    int cnt[18] = { 0 }; bool ok = true;
    for (int i = 0; i < 16; i++) { if (memType[i] >= 18) ok = false; else cnt[memType[i]]++; }
    int pairs = 0; for (int t = 0; t < 18; t++) { if (cnt[t] == 2) pairs++; else if (cnt[t]) ok = false; }
    ck(ok && pairs == 8, "the board is 8 pairs of distinct types");
  }
  allFlush &= flushes();
  skipIntro();
  int x, y, a = 0, b = memPartner(0), c, cx, cy;
  memXY(a, x, y);
  ck(memCardAt((int16_t)x, (int16_t)y) == a && memCardAt(233, 60) == -1, "card hit-test");
  ck(memTap((int16_t)x, (int16_t)y) == MEM_T_FLIP && memState[a] == 1, "the first card flips up");
  ck(memTap((int16_t)x, (int16_t)y) == MEM_T_NONE, "tapping it again does nothing");
  memXY(b, cx, cy);
  ck(memTap((int16_t)cx, (int16_t)cy) == MEM_T_MATCH && memState[a] == 2 && memState[b] == 2 && arcPts == 10,
     "its partner: a pair, both stay up, +10");
  // a mismatch shows, then turns back
  a = -1; for (int i = 0; i < 16 && a < 0; i++) if (!memState[i]) a = i;
  c = memOther(a);
  memXY(a, x, y); memTap((int16_t)x, (int16_t)y);
  memXY(c, cx, cy);
  ck(memTap((int16_t)cx, (int16_t)cy) == MEM_T_MISS && memMisses == 1 && trnCombo == 0 && arcPts == 10,
     "two different cards: a miss, combo broken, nothing lost");
  ck(memState[a] == 1 && memState[c] == 1, "both stay face up for a moment");
  memHideAt = millis() - 1; allFlush &= flushes();
  ck(memState[a] == 0 && memState[c] == 0 && memA < 0 && memB < 0, "then turn back down");
  // a tap during the peek turns them back at once and counts as a fresh flip
  memXY(a, x, y); memTap((int16_t)x, (int16_t)y);
  memXY(c, cx, cy); memTap((int16_t)cx, (int16_t)cy);
  int d = memPartner(a);
  memXY(d, x, y);
  ck(memTap((int16_t)x, (int16_t)y) == MEM_T_FLIP && memState[a] == 0 && memState[c] == 0 && memA == d,
     "tapping during the peek flips the pair back and starts a new turn");
  // clear the rest in a row: combo x2 from the 5th pair, then the board bonus
  memState[d] = 0; memA = -1;
  uint16_t before = arcPts; int matches = 0, lastP = 0; uint8_t r = 0;
  for (int i = 0; i < 16; i++) {
    if (memState[i]) continue;
    int j = memPartner(i);
    memXY(i, x, y); memTap((int16_t)x, (int16_t)y);
    memXY(j, cx, cy);
    uint16_t pb = arcPts;
    r = memTap((int16_t)cx, (int16_t)cy);
    matches++; lastP = arcPts - pb;
  }
  ck(matches == 7 && r == MEM_T_CLEAR && memBoards == 1, "the last pair clears the board");
  ck(lastP == 20 + 20, "with 5+ pairs in a row a pair is worth 20, and the clear adds 20");
  memState[0] = 0; memXY(0, x, y);       // even a face-down card cannot be flipped mid-deal
  ck(memDealAt != 0 && memTap((int16_t)x, (int16_t)y) == MEM_T_NONE && memState[0] == 0, "taps wait while the new board is dealt");
  memDealAt = millis() - 1; allFlush &= flushes();
  { bool down = true; for (int i = 0; i < 16; i++) if (memState[i]) down = false;
    ck(memDealAt == 0 && down, "a fresh face-down board is dealt"); }
  (void)before;
  // leave by a vertical swipe: banks
  s0 = arcSessions; uint16_t pts = arcPts; uint8_t joy0 = pet.joy;
  onSwipeV(1);
  ck(arcGame == NONE && arcSessions == s0 + 1 && arcBest[G_MEMORY] == pts, "a vertical swipe leaves, banking the score as the best");
  ck(pet.joy > joy0 && arcadeOpen && onScreen("arcade"), "it paid joy and went back to the picker (not the hub)");
  onSwipeV(1);
  ck(arcGame == NONE && arcSessions == s0 + 1, "a second swipe pays nothing more");
  ck(!arcadeOpen && trainOpen && !hubOpen, "...and backs out to the training menu");
  openArcade();

  // =================================================================== ECHO
  printf("-- echo\n");
  arcStart(G_ECHO);
  ck(echoLen == 1 && echoState == ECHO_SHOW && echoLives == 2, "ECHO starts with one pad to watch and 2 lives");
  for (uint8_t p = 0; p < 4; p++) ck(echoPadAt(echoPadX(p), echoPadY(p)) == p, "a pad's centre maps to that pad");
  ck(echoPadAt(233, 250) == -1 && echoPadAt(233, 30) == -1, "the hub and beyond the ring are no pad");
  skipIntro();
  echoT0 = millis();
  echoStep(millis() + 5);
  ck(echoLit == echoSeq[0], "the playback lights the sequence");
  uint32_t step = echoOnMs() + 140;
  ck(echoTap(echoPadX(echoSeq[0]), echoPadY(echoSeq[0])) == ECHO_T_NONE, "taps during the playback are ignored");
  echoStep(echoT0 + step * echoLen + 1);
  ck(echoState == ECHO_INPUT, "after it, your turn");
  ck(echoTap(echoPadX(echoSeq[0]), echoPadY(echoSeq[0])) == ECHO_T_ROUND && arcPts == 2 && echoRounds == 1,
     "the right pad completes round 1: 1 + its length 1");
  echoStep(echoT0 + 701);
  ck(echoState == ECHO_SHOW && echoLen == 2, "after the pause the sequence grows by one");
  echoT0 = millis(); echoStep(echoT0 + step * echoLen + 1);
  ck(echoState == ECHO_INPUT, "watched");
  ck(echoTap(echoPadX(echoSeq[0]), echoPadY(echoSeq[0])) == ECHO_T_OK && arcPts == 3, "a right pad mid-sequence: +1");
  uint8_t wrong = (uint8_t)((echoSeq[1] + 1) & 3);
  ck(echoTap(echoPadX(wrong), echoPadY(wrong)) == ECHO_T_WRONG && echoLives == 1 && echoState == ECHO_PAUSE, "a wrong pad costs a life");
  echoStep(echoT0 + 701);
  ck(echoLen == 2 && echoState == ECHO_SHOW, "and replays the same sequence (it does not grow)");
  ck(echoTap(233, 250) == ECHO_T_NONE, "the hub is not a pad");
  echoState = ECHO_INPUT; echoPos = 0; echoT0 = millis() - 5001;
  echoStep(millis());
  ck(echoLives == 0 && arcOver, "five idle seconds on your turn is a mistake; the last life ends it");
  allFlush &= flushes();
  ck(arcGradeOf(G_ECHO, 2 * 3) == 0 && arcGradeOf(G_ECHO, 3 * 4) == 1 && arcGradeOf(G_ECHO, 5 * 6) == 2 && arcGradeOf(G_ECHO, 7 * 8) == 3,
     "ECHO grades: B at 3 rounds cleared, A at 5, S at 7");
  arcGame = NONE; arcOver = false;

  // =================================================================== POP
  printf("-- pop-up\n");
  arcStart(G_POP);
  ck(arcUntil == trnGoAt + 45000, "POP-UP: 45 s");
  skipIntro();
  popNext = millis() + 100000;
  popSpawnAt(3, POP_K_MOLE);
  ck(popTap(popHoleX(3), popHoleY(3) - 30) == POP_T_HIT && arcPts == 1 && popHits == 1, "bop a critter: +1");
  ck(popTap(popHoleX(3), popHoleY(3) - 30) == POP_T_WHIFF, "a bonked one cannot be bopped twice");
  ck(trnCombo == 0, "and the whiff broke the combo");
  popSpawnAt(0, POP_K_GOLD);
  ck(popTap(popHoleX(0) + 40, popHoleY(0) - 30) == POP_T_GOLD && arcPts == 1 + 3 && popGolds == 1, "a golden one: +3 (generous hit area)");
  popSpawnAt(4, POP_K_BOMB);
  popSpawnAt(2, POP_K_MOLE);
  ck(popTap(popHoleX(4), popHoleY(4) - 30) == POP_T_BOMB && arcPts == 0 && trnCombo == 0 && popBombs == 1,
     "the bomb: -5 (floored at 0), combo gone");
  popBorn[2] = millis() - popLife[2] - 1;
  popStep(millis());
  ck(popK[2] == 0 && popMissed == 1, "a critter left alone ducks back: a miss");
  ck(popTap(popHoleX(6), popHoleY(6) - 30) == POP_T_WHIFF && popWhiffs == 2, "a tap on an empty hole is a whiff");
  // no bombs before 5 s, at most one critter up before 8 s
  {
    bool bomb = false, over = false;
    for (int k = 0; k < 400; k++) {
      trnGoAt = millis() - 1000;          // 1 s in
      for (int h = 0; h < 7; h++) if (popK[h] && (k % 3 == 0)) popK[h] = 0;
      popNext = 0;
      popStep(millis());
      int up = 0;
      for (int h = 0; h < 7; h++) { if (popK[h] == POP_K_BOMB) bomb = true; if (popK[h] && popK[h] != POP_K_BOMB && !popHitAt[h]) up++; }
      if (up > 1) over = true;
    }
    ck(!bomb && !over, "before 5 s no bomb, before 8 s never two critters at once");
    for (int h = 0; h < 7; h++) popK[h] = 0;
    bool sawBomb = false, sawTwo = false;
    for (int k = 0; k < 600; k++) {
      trnGoAt = millis() - 30000;         // 30 s in
      popNext = 0;
      popStep(millis());
      int up = 0;
      for (int h = 0; h < 7; h++) { if (popK[h] == POP_K_BOMB) sawBomb = true; if (popK[h] && popK[h] != POP_K_BOMB && !popHitAt[h]) up++; }
      if (up >= 2) sawTwo = true;
      if (up > 3) over = true;
      if (k % 5 == 0) for (int h = 0; h < 7; h++) popK[h] = 0;
    }
    ck(sawBomb && sawTwo && !over, "...and later bombs do appear, several critters at once, never more than 3");
  }
  skipIntro();
  for (int f = 0; f < 6; f++) { allFlush &= flushes(); slp(20); }
  // the header strip leaves, banking
  popSpawnAt(1, POP_K_MOLE); popTap(popHoleX(1), popHoleY(1) - 30);
  s0 = arcSessions;
  arcadeTap(233, 30);
  ck(arcGame == NONE && arcSessions == s0 + 1, "a tap on the header strip leaves, banking");

  // the clock ends a session and shows the card
  arcStart(G_POP); skipIntro(); arcPts = 7; arcUntil = millis() - 1;
  allFlush &= flushes();
  ck(arcOver && arcSessions == s0 + 2, "the clock running out ends the game and shows the card");
  { uint8_t j = pet.joy; arcFinish(); ck(arcSessions == s0 + 2 && pet.joy == j, "finishing again (a swipe on the card) pays nothing"); }
  for (int f = 0; f < 4; f++) { allFlush &= flushes(); slp(30); }
  arcOverAt = millis() - 7001;
  allFlush &= flushes();
  ck(arcGame == NONE && !arcOver, "the card goes back to the picker by itself after 7 s");
  ck(allFlush, "every frame of every game, intro, play and results, flushes");

  // =================================================================== REWARDS
  printf("-- rewards\n");
  ck(arcJoyFor(0) == 4 && arcJoyFor(3) == 16 && arcBondFor(1) == 1 && arcBondFor(2) == 2, "joy 4/8/12/16 by grade, bond 1 (C,B) or 2 (A,S)");
  ck(arcGradeOf(G_CATCH, 24) == 0 && arcGradeOf(G_CATCH, 25) == 1 && arcGradeOf(G_CATCH, 50) == 2 && arcGradeOf(G_CATCH, 80) == 3,
     "CATCH grades at 25 / 50 / 80");
  ck(arcGradeOf(G_MEMORY, 150) == 3 && arcGradeOf(G_MEMORY, 149) == 2 && arcGradeOf(G_POP, 65) == 3 && arcGradeOf(G_POP, 64) == 2,
     "MEMORY S at 150, POP-UP S at 65");
  // a session that scored nothing pays nothing and announces nothing
  evPlay = 0; freshPet();
  uint8_t joyB = pet.joy, bondB = pet.bond, eneB = pet.energy;
  arcStart(G_CATCH); arcLeave();
  ck(pet.joy == joyB && pet.bond == bondB && pet.energy == eneB && evPlay == 0, "a 0-point session pays nothing and emits nothing");
  // an S: joy, bond, energy, EV_PLAY, today's S prize and the game's first-S item
  arcPrizeDay = 0; arcFirstS = 0; memset(arcPaid, 0, 4);
  bag.clearAll();
  joyB = pet.joy; bondB = pet.bond;
  arcStart(G_POP); skipIntro(); arcPts = 70; arcFinish();
  ck(arcGrade == 3 && pet.joy == joyB + 16 && pet.energy == eneB - 3, "an S pays 16 joy and costs 3 energy");
  ck(pet.bond == bondB + arcBondGot && arcBondGot <= 2, "the bond it reports is the bond the pet actually gained");
  ck(evPlay == 1 && evPlayArg == 70, "EV_PLAY is emitted with the score (tasks count it)");
  ck(arcPrizeGot == ITEM_SITRUS_BERRY && bag.count(ITEM_SITRUS_BERRY) == 1 && arcPrizeDay == pet.currentDay() + 1,
     "today's S prize: POP-UP pays a SITRUS BERRY");
  ck(arcFirstGot == ITEM_LUM_BERRY && bag.count(ITEM_LUM_BERRY) == 1 && (arcFirstS & 8), "POP-UP's first S adds a LUM BERRY, once");
  ck(arcPaidToday(G_POP) == 3, "the list knows POP-UP paid its S today");
  allFlush = flushes();
  arcGame = NONE; arcOver = false;
  arcStart(G_POP); skipIntro(); arcPts = 70; arcFinish();
  ck(arcGrade == 3 && arcPrizeGot == 0 && arcFirstGot == 0 && bag.count(ITEM_SITRUS_BERRY) == 1 && bag.count(ITEM_LUM_BERRY) == 1,
     "a second S in the same game the same day: nothing more");
  arcGame = NONE; arcOver = false;
  arcStart(G_POP); skipIntro(); arcPts = 45; arcFinish();
  ck(arcGrade == 2 && arcPrizeGot == 0, "an A after today's S pays nothing more");
  arcGame = NONE; arcOver = false;
  // an A first, then the S upgrade, in another game
  arcStart(G_CATCH); skipIntro(); arcPts = 55; arcFinish();
  ck(arcGrade == 2 && arcPrizeGot == ITEM_ORAN_BERRY && arcFirstGot == 0 && arcPaidToday(G_CATCH) == 2, "an A pays its own prize (CATCH: ORAN BERRY)");
  arcGame = NONE; arcOver = false;
  arcStart(G_CATCH); skipIntro(); arcPts = 52; arcFinish();
  ck(arcPrizeGot == 0, "a second A the same day pays nothing");
  arcGame = NONE; arcOver = false;
  arcStart(G_CATCH); skipIntro(); arcPts = 90; arcFinish();
  ck(arcPrizeGot == ITEM_SITRUS_BERRY && arcFirstGot == ITEM_LEFTOVERS && arcPaidToday(G_CATCH) == 3,
     "an S after the A still pays the S prize and the first-S LEFTOVERS");
  ck(!arcadePrizeToday() && !arcAllSGot, "two games left: today's prizes are not all won");
  allFlush &= flushes();
  arcGame = NONE; arcOver = false;
  arcStart(G_MEMORY); skipIntro(); arcPts = 200; arcFinish();
  ck(arcPrizeGot == ITEM_GREATBALL && arcFirstGot == ITEM_QUICK_CLAW && !arcAllSGot, "MEMORY's S: GREAT BALL and QUICK CLAW");
  arcGame = NONE; arcOver = false;
  // the daily bond cap: Pet::addBond stops it, the arcade reports the truth
  {
    int got = 0; uint8_t b0 = pet.bond;
    for (int k = 0; k < 20; k++) {
      arcStart(G_ECHO); skipIntro(); arcPts = 60; arcFinish(); got += arcBondGot;
      if (k == 0) {
        ck(arcPrizeGot == ITEM_GREATBALL && arcFirstGot == ITEM_FOCUS_SASH && arcAllSGot && bag.count(ITEM_ULTRABALL) == 3,
           "the fourth game's first S also pays the all-four bonus: 3 ULTRA BALLs");
        allFlush &= flushes();
      }
      if (k == 1) ck(!arcAllSGot && bag.count(ITEM_ULTRABALL) == 3 && bag.count(ITEM_FOCUS_SASH) == 1, "the one-time bonuses pay once");
      arcGame = NONE; arcOver = false;
    }
    ck(pet.bond - b0 == got && got <= 20 && arcBondGot == 0, "twenty S sessions: the bond stops at the daily cap of 20");
  }
  ck(arcadePrizeToday(), "all four S prizes won: the list says so");
  // next day: the prizes again, but not the one-time bonuses
  pet.dbgSetSeen(1700000000UL + 86400UL);
  ck(arcPaidToday(G_ECHO) == 0 && !arcadePrizeToday(), "a new day clears today's prizes");
  uint8_t gb = bag.count(ITEM_GREATBALL);
  arcStart(G_ECHO); skipIntro(); arcPts = 60; arcFinish();
  ck(arcPrizeGot == ITEM_GREATBALL && bag.count(ITEM_GREATBALL) == gb + 1 && arcFirstGot == 0 && !arcAllSGot,
     "a new day: the S prize is back, the first-S item is not");
  arcGame = NONE; arcOver = false;
  // a lower score is not a new best
  uint16_t bestPop = arcBest[G_POP];
  arcStart(G_POP); skipIntro(); arcPts = 3; arcFinish();
  ck(!arcNewHi && arcBest[G_POP] == bestPop, "a lower score keeps the best");
  arcGame = NONE; arcOver = false;

  // =================================================================== PERSIST
  printf("-- persistence\n");
  uint16_t want[4]; memcpy(want, arcBest, sizeof(want)); uint32_t wantDay = arcPrizeDay;
  uint8_t wantPaid[4]; memcpy(wantPaid, arcPaid, 4); uint8_t wantFirst = arcFirstS;
  memset(arcBest, 0, sizeof(arcBest)); arcPrizeDay = 0; memset(arcPaid, 0, 4); arcFirstS = 0;
  arcLoad();
  ck(!memcmp(want, arcBest, sizeof(want)) && arcPrizeDay == wantDay && want[G_POP] >= 70 && want[G_MEMORY] >= 200,
     "bests and the prize day come back from NVS");
  ck(!memcmp(wantPaid, arcPaid, 4) && arcFirstS == wantFirst && wantFirst == 0x1F, "today's paid ranks and the one-time bonuses come back too");
  {
    Preferences p; p.begin("tamapoke", true);
    ck(p.isKey("arcHi") && p.getBytesLength("arcHi") == 8 && p.isKey("arcDay") && p.getBytesLength("arcRw") == 5,
       "stored under arcHi (8 B), arcDay and arcRw (5 B)");
  }
  // reopening the screen re-reads NVS (a WIPE or IMPORT would not leave stale bests)
  arcBest[G_CATCH] = 9999;
  arcadeOpen = false; trainOpen = true;
  onTap(233, 126 + 3 * 64 + 28);
  ck(arcadeOpen && arcBest[G_CATCH] == want[G_CATCH], "opening the arcade reloads the bests");
  ck(flushes(), "the picker with bests flushes");

  // leaving the screen from anywhere mid-game banks (featCloseAll)
  arcStart(G_POP); skipIntro(); arcPts = 4;
  s0 = arcSessions;
  featBackToHub();
  ck(!arcadeOpen && arcSessions == s0 + 1 && arcGame == NONE, "closing the arcade mid-game banks the session");

  printf("\n%s (%d failure%s)\n", bad ? "ARCADE TEST FAILED" : "arcade ok", bad, bad == 1 ? "" : "s");
  return bad ? 1 : 0;
}
