// The three training games (ui_train.ino), driven through the REAL sketch:
// startSack/sackTap/renderSack, startSpeedGame/spdTap/renderSpeed,
// startGame/gameTap/renderGame. For each: the 3-2-1 countdown holds the clock
// and refuses taps, the scoring rules (crit / jab / weak spot / fever, gold /
// bomb / whiff / expiry, gold ball / pickup / second ball / wind), the combo
// and its multiplier, that the stat gain is what the Pet trainer returns for
// the documented effective score and never more than 18, that the RECORD keeps
// counting the raw hits, that every frame flushes, and that leaving early
// still banks.
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include "i18n.h"
#include <cstdio>
#include <cstring>
#include <chrono>
#include <thread>
uint32_t g_seed = 31337; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX = 0, g_touchY = 0; volatile bool g_touchDown = false;
void FakeESP::restart() { exit(0); }
int FakeSerial::available() { return 0; }
String FakeSerial::readStringUntil(char) { return String(""); }
void setup(); void loop(); void render(); void onTap(int16_t, int16_t); void onSwipeV(int);
extern Pet pet;
extern Arduino_Canvas *gfx;
extern int g_emuRiseLast, g_emuRiseCount;
// shared
extern uint32_t trnGoAt; extern uint16_t trnCombo, trnBestCombo; extern uint8_t trnGrade;
uint8_t trnMult(uint16_t); uint8_t trnPotential(uint16_t, uint8_t); uint8_t trnGradeOf(uint8_t);
// bag
extern bool sackOpen; extern uint32_t sackUntil, sackOverUntil, sackMeterT0, sackLastTap, sackFeverUntil;
extern uint32_t sackSpotAt, sackSpotUntil; extern int16_t sackSpotDX, sackSpotDY;
extern uint16_t sackHits, sackPts, sackCrits, sackSpots; extern uint8_t sackGain;
void startSack(); uint8_t sackTap(int16_t, int16_t); void leaveSack(); void renderSack();
uint16_t sackEff(); uint16_t sackNeedle(uint32_t); bool sackInZone(uint32_t); uint16_t sackZone(); int sackBagX();
// speed
extern bool spdOpen; extern uint32_t spdUntil, spdOverUntil;
extern uint16_t spdHits, spdMisses, spdPts, spdGold, spdBombs, spdWhiffs; extern uint8_t spdGain;
extern uint8_t spdTK[2]; extern float spdTX[2], spdTY[2], spdTVX[2], spdTVY[2];
extern uint32_t spdTBorn[2]; extern uint16_t spdTLife[2];
void startSpeedGame(); uint8_t spdTap(int16_t, int16_t); void leaveSpeed(); void renderSpeed();
uint16_t spdEff(); void spdSpawnAt(uint8_t, uint8_t);
// ball
extern bool gameOpen; extern uint32_t gameUntil, gameOverUntil; extern uint8_t gameScore, gameMisses, gameDefGain;
extern uint16_t gamePts, gameGolds, gameItems;
extern float gBX[2], gBY[2], gBVX[2], gBVY[2]; extern bool gBOn[2], gBGold[2];
extern uint8_t gItKind; extern float gItX, gItY0; extern uint32_t gItNext;
extern int8_t gWindDir; extern uint32_t gWindFrom, gWindUntil, gWindNext;
void startGame(); void gameTap(int16_t, int16_t); void leaveGame(); void renderGame(); void stepGame();
uint16_t gameEff(); void ballSpawn(uint8_t);

// mirrors of the constants under test (asserted against behaviour, not trusted)
#define SACK_JAB 1
#define SACK_OFF 2
#define SACK_CRIT 3
#define SACK_FEVERHIT 4
#define SACK_SPOT 8
#define SPD_T_NONE 0
#define SPD_T_HIT 1
#define SPD_T_GOLD 2
#define SPD_T_BOMB 3
#define SPD_T_WHIFF 4

static int bad = 0;
static void ck(bool ok, const char *w) { printf("%s  %s\n", ok ? "PASS" : "FAIL", w); if (!ok) bad++; }
static void slp(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
static bool flushes(void (*r)()) { gfx->frameReady = false; r(); return gfx->frameReady; }

static void freshPet() {
  pet.sleeping = false; pet.energy = 100; pet.fullness = 100; pet.joy = 50;
  pet.ivAtk = pet.ivDef = pet.ivSpe = pet.ivHp = 31;
  pet.trAtk = pet.trDef = pet.trSpe = 0;
  pet.strHi = pet.spdHi = pet.gameHi = 0;
}
// put the bag's needle dead centre (in the zone) or at the far end (out of it)
static void needleIn()  { sackMeterT0 = millis() - 325; }
static void needleOut() { sackMeterT0 = millis(); }
static void wound() { sackLastTap = millis() - 1000; }       // the punch is wound up
static void skipIntro() { trnGoAt = millis() - 1; }

int main() {
  setup(); for (int i = 0; i < 4; i++) loop();
  if (pet.awaitingStarter()) pet.chooseStarter(4);
  if (pet.isEgg()) pet.dbgHatchAs(6, false);
  while (pet.hasLearnOffer()) pet.declineLearn();
  freshPet();

  // ---- shared rules
  ck(trnMult(0) == 1 && trnMult(4) == 1 && trnMult(5) == 2 && trnMult(9) == 2 && trnMult(10) == 3 && trnMult(40) == 3,
     "combo multiplier: x1 to 4, x2 from 5, x3 from 10");
  ck(trnGradeOf(18) == 3 && trnGradeOf(12) == 2 && trnGradeOf(11) == 1 && trnGradeOf(6) == 1 && trnGradeOf(5) == 0,
     "grade: S at the 18 ceiling, A from 12, B from 6, else C");
  ck(trnPotential(1000, 4) == 18 && trnPotential(40, 4) == 10, "the potential gain is capped at 18");
  {
    const char *const *rows[] = { S_TRN_GO, S_TRN_CRIT, S_TRN_WEAK, S_TRN_FEVER, S_TRN_COMBO, S_TRN_MISS,
                                  S_TRN_2BALL, S_TRN_WIND, S_TRN_HBAG, S_TRN_HSPD, S_TRN_HBALL, S_TRN_PTS,
                                  S_TRN_DEF, S_TRN_MAXED };
    bool ok = true;
    for (auto r : rows) for (int l = 0; l < LANG_COUNT; l++) {
      if (!r[l] || !r[l][0]) ok = false;
      else for (const char *c = r[l]; *c; c++) if ((unsigned char)*c > 126) ok = false;
    }
    ck(ok, "the training strings: six languages, none empty, ASCII only");
  }

  // =================================================================== BAG
  printf("-- bag (ATK)\n");
  uint32_t t0 = millis();
  startSack();
  ck(sackOpen && trnGoAt >= t0 + 1700 && trnGoAt <= t0 + 1900, "the bag opens on a 1.8 s countdown");
  ck(sackUntil == trnGoAt + 10000, "and its 10 s clock starts only after it");
  bool allFlush = flushes(renderSack);
  wound(); needleIn();
  ck(sackTap(300, 200) == 0 && sackHits == 0 && sackPts == 0, "a punch during the countdown does not count");
  // wait the countdown out for real, once
  while ((int32_t)(millis() - trnGoAt) < 0) { allFlush &= flushes(renderSack); slp(40); }
  allFlush &= flushes(renderSack);
  sackLastTap = 0;
  wound(); needleIn();
  uint16_t p0 = sackPts;
  int riseBefore = g_emuRiseCount;
  ck(sackTap(300, 200) == SACK_CRIT && sackPts == p0 + 3 && trnCombo == 1, "after GO a wound-up punch in the zone is a CRITICAL worth 3");
  ck(g_emuRiseCount == riseBefore + 1 && g_emuRiseLast == 0, "and plays the first rising blip");
  p0 = sackPts;
  needleIn();                        // in the zone, but thrown straight after the last
  ck(sackTap(300, 200) == SACK_JAB && sackPts == p0 + 1 && trnCombo == 1,
     "a punch inside the wind-up is a JAB: 1 point, no crit, the chain kept");
  for (int i = 0; i < 3; i++) { wound(); needleIn(); sackTap(300, 200); }
  ck(trnCombo == 4 && g_emuRiseLast == 3, "four crits in a row: combo 4, the blip has climbed to step 3");
  ck(sackZone() < 100, "and the gold zone narrows as the chain grows");
  p0 = sackPts;
  wound(); needleIn();
  sackTap(300, 200);
  ck(trnCombo == 5 && sackPts == p0 + 4, "the 5th crit runs at x2: worth 2 + 2");
  p0 = sackPts;
  wound(); needleOut();
  ck(!sackInZone(millis()) && sackTap(300, 200) == SACK_OFF && trnCombo == 0 && sackPts == p0 + 1,
     "a wound-up punch outside the zone is 1 point and breaks the chain");
  ck(trnBestCombo == 5, "the best chain is remembered for the card");
  // FEVER after six crits in a row
  for (int i = 0; i < 6; i++) { wound(); needleIn(); sackTap(300, 200); }
  ck((int32_t)(sackFeverUntil - millis()) > 2000 && trnCombo == 0, "six crits in a row light FEVER (2.5 s) and reset the chain");
  p0 = sackPts;
  needleOut();
  ck(sackTap(250, 200) == SACK_FEVERHIT && sackPts == p0 + 2, "in FEVER every punch, even an untimed jab, is worth 2");
  allFlush &= flushes(renderSack);
  sackFeverUntil = millis();
  // the weak spot
  sackSpotAt = millis(); sackSpotUntil = millis() + 1100; sackSpotDX = 10; sackSpotDY = 60;
  p0 = sackPts;
  slp(1);
  sackLastTap = millis();                       // a jab, so only the spot is being measured
  uint8_t k = sackTap(sackBagX() + 10 + 200, 116 + 60);
  ck(!(k & SACK_SPOT) && sackPts == p0 + 1, "a punch far from the weak spot does not find it");
  p0 = sackPts;
  sackLastTap = millis();
  k = sackTap(sackBagX() + 10 + 20, 116 + 60 + 10);
  ck((k & SACK_SPOT) && sackPts == p0 + 1 + 3 && sackSpots == 1, "a punch on the WEAK SPOT adds 3");
  sackLastTap = millis();
  k = sackTap(sackBagX() + 10, 116 + 60);
  ck(!(k & SACK_SPOT), "and the spot is gone once found");
  allFlush &= flushes(renderSack);
  // the end: train from the effective score, record the raw hits
  uint16_t hits = sackHits, pts = sackPts;
  ck(pts > hits, "timing has earned more points than punches");
  sackUntil = millis();
  allFlush &= flushes(renderSack);
  uint8_t want = (uint8_t)((pts / 4) > 18 ? 18 : pts / 4);
  printf("      %u punches, %u points -> STR +%u (want %u)\n", hits, pts, sackGain, want);
  ck(sackOverUntil && sackGain == want && pet.trAtk == want, "STR gain = min(18, points / 4)");
  ck(pet.strHi == hits, "the record still counts PUNCHES, not points");
  ck(trnGrade == trnGradeOf(want), "the grade is the gain's grade");
  slp(800); allFlush &= flushes(renderSack);
  uint8_t trBefore = pet.trAtk;
  leaveSack();
  ck(pet.trAtk == trBefore, "leaving from the results card does not pay twice");
  ck(allFlush, "every bag frame flushed: countdown, play, fever, results");

  // a pure masher trains exactly as before: points == punches, gain == hits/4
  freshPet();
  startSack(); skipIntro();
  for (int i = 0; i < 61; i++) { sackLastTap = millis(); needleIn(); sackTap(300, 220); }
  ck(sackPts == sackHits && sackHits == 61, "mashing: every punch a jab, points == punches");
  sackUntil = millis(); renderSack();
  ck(sackGain == 61 / 4, "a masher's gain is the old hits/4 (61 -> 15)");
  sackOpen = false;
  // the 18 ceiling
  freshPet();
  startSack(); skipIntro();
  for (int i = 0; i < 40; i++) { wound(); needleIn(); sackTap(300, 200); }
  sackUntil = millis(); renderSack();
  printf("      40 crits: %u points -> +%u\n", sackPts, sackGain);
  ck(sackPts / 4 > 18 && sackGain == 18 && trnGrade == 3, "however many points, a session gains at most 18 (grade S)");
  sackOpen = false;
  // leaving early banks the effective score
  freshPet();
  startSack(); skipIntro();
  for (int i = 0; i < 8; i++) { wound(); needleIn(); sackTap(300, 200); }
  uint16_t e = sackEff();
  leaveSack();
  ck(!sackOpen && pet.trAtk == e / 4 && e / 4 > 8 / 4, "leaving early banks min(18, points/4), crits included");
  ck(pet.strHi == 8, "and records the 8 punches");
  // the header strip still leaves (handled at press time in handleTouch)
  freshPet();
  startSack();
  leaveSack();
  ck(!sackOpen && pet.trAtk == 0, "leaving during the countdown banks nothing");

  // ================================================================= SPEED
  printf("-- reaction test (SPE)\n");
  freshPet();
  t0 = millis();
  startSpeedGame();
  ck(spdOpen && spdUntil == trnGoAt + 15000 && trnGoAt >= t0 + 1700, "the reaction test counts down, then runs 15 s");
  allFlush = flushes(renderSpeed);
  ck(!spdTK[0] && !spdTK[1], "no target during the countdown");
  ck(spdTap(233, 233) == SPD_T_NONE && spdWhiffs == 0, "and a tap then is neither a hit nor a whiff");
  skipIntro();
  allFlush &= flushes(renderSpeed);
  ck(spdTK[0] == 1 || spdTK[0] == 2 || spdTK[1] == 1 || spdTK[1] == 2, "GO: a real target is up");
  // a red one
  spdTK[1] = 0;
  spdSpawnAt(0, 1); spdTX[0] = 200; spdTY[0] = 200;
  uint16_t sp0 = spdPts;
  ck(spdTap(200 + 50, 200) == SPD_T_HIT && spdHits == 1 && spdPts == sp0 + 1 && trnCombo == 1,
     "a red target (tapped within its radius + slop) is 1 point");
  ck(spdTK[0] || spdTK[1], "and another takes its place");
  // a whiff
  spdTK[0] = spdTK[1] = 0;
  spdSpawnAt(0, 1); spdTX[0] = 150; spdTY[0] = 150;
  sp0 = spdPts;
  ck(spdTap(330, 330) == SPD_T_WHIFF && trnCombo == 0 && spdPts == sp0 && spdWhiffs == 1,
     "a tap on empty panel is a whiff: the chain breaks, no points lost");
  // chain to x2, then a gold one
  for (int i = 0; i < 5; i++) {
    spdTK[0] = spdTK[1] = 0; spdSpawnAt(0, 1); spdTX[0] = 233; spdTY[0] = 233; spdTap(233, 233);
  }
  ck(trnCombo == 5, "five hits in a row: combo 5");
  spdTK[0] = spdTK[1] = 0; spdSpawnAt(0, 2); spdTX[0] = 233; spdTY[0] = 233;
  sp0 = spdPts;
  ck(spdTap(233, 233) == SPD_T_GOLD && spdPts == sp0 + 3 + 1 && spdGold == 1, "a GOLD target at x2 is 3 + 1");
  // a bomb
  spdTK[0] = spdTK[1] = 0; spdSpawnAt(0, 1); spdTX[0] = 120; spdTY[0] = 233;
  spdSpawnAt(1, 3); spdTX[1] = 330; spdTY[1] = 233;
  sp0 = spdPts; uint16_t h0 = spdHits;
  ck(spdTap(330, 233) == SPD_T_BOMB && spdPts == sp0 - 3 && trnCombo == 0 && spdHits == h0 && spdBombs == 1,
     "a BOMB costs 3 points and the chain, and is not a hit");
  spdPts = 1;
  spdTK[0] = spdTK[1] = 0; spdSpawnAt(1, 3); spdTX[1] = 330; spdTY[1] = 233;
  spdSpawnAt(0, 1); spdTX[0] = 120; spdTY[0] = 233;
  spdTap(330, 233);
  ck(spdPts == 0, "and never takes the points below zero");
  // expiry
  spdTK[0] = spdTK[1] = 0; spdSpawnAt(0, 1); spdTX[0] = 233; spdTY[0] = 233;
  spdTap(233, 233);
  spdTK[0] = spdTK[1] = 0; spdSpawnAt(0, 1); spdTBorn[0] = millis() - 5000;
  uint16_t m0 = spdMisses;
  allFlush &= flushes(renderSpeed);
  ck(spdMisses == m0 + 1 && trnCombo == 0, "a real target that expires is a miss and breaks the chain");
  ck(spdTK[0] == 1 || spdTK[0] == 2 || spdTK[1] == 1 || spdTK[1] == 2, "and a new one appears");
  spdTK[0] = spdTK[1] = 0; spdSpawnAt(1, 3); spdTBorn[1] = millis() - 5000;
  spdSpawnAt(0, 1);
  m0 = spdMisses;
  renderSpeed();
  ck(spdMisses == m0, "a bomb left alone just fizzles: not a miss");
  // what the session spawns, and when: 400 refills at each stage
  {
    auto census = [&](uint32_t elapsed, int &bombs, int &doubles, int &movers, int &twoBombs, int &noGood) {
      bombs = doubles = movers = twoBombs = noGood = 0;
      for (int n = 0; n < 400; n++) {
        trnGoAt = millis() - elapsed;
        spdTK[0] = spdTK[1] = 0;
        renderSpeed();                                     // spawns through the real path
        // and a hit, so the refill path rolls too
        int g = (spdTK[0] == 1 || spdTK[0] == 2) ? 0 : 1;
        spdTap((int16_t)spdTX[g], (int16_t)spdTY[g]);
        if (spdTK[0] == 3 || spdTK[1] == 3) bombs++;
        if (spdTK[0] == 3 && spdTK[1] == 3) twoBombs++;
        bool g0 = spdTK[0] == 1 || spdTK[0] == 2, g1 = spdTK[1] == 1 || spdTK[1] == 2;
        if (g0 && g1) doubles++;
        if (!g0 && !g1) noGood++;
        for (int i = 0; i < 2; i++) if (spdTK[i] && (spdTVX[i] != 0 || spdTVY[i] != 0)) movers++;
      }
    };
    int b, d, m, bb, ng;
    spdUntil = millis() + 60000;
    census(1000, b, d, m, bb, ng);
    printf("      at 1 s: bombs %d doubles %d movers %d\n", b, d, m);
    ck(b == 0 && d == 0 && m == 0, "first 3 s: no bombs, no second target, nothing moves");
    census(4000, b, d, m, bb, ng);
    printf("      at 4 s: bombs %d doubles %d movers %d\n", b, d, m);
    ck(b > 40 && d == 0 && m == 0, "from 3 s bombs appear beside a real target (but not yet two real ones)");
    census(6000, b, d, m, bb, ng);
    printf("      at 6 s: bombs %d doubles %d movers %d\n", b, d, m);
    ck(d > 40 && m == 0, "from 5 s two real targets at once");
    census(9000, b, d, m, bb, ng);
    printf("      at 9 s: bombs %d doubles %d movers %d two-bombs %d no-real %d\n", b, d, m, bb, ng);
    ck(m > 100, "from 8 s targets move");
    ck(bb == 0 && ng == 0, "never two bombs, and never a field without a real target");
  }
  // end of session
  freshPet();
  startSpeedGame(); skipIntro();
  renderSpeed();
  for (int i = 0; i < 30; i++) {
    spdTK[0] = spdTK[1] = 0; spdSpawnAt(0, i % 4 == 0 ? 2 : 1); spdTX[0] = 233; spdTY[0] = 233; spdTap(233, 233);
  }
  uint16_t shits = spdHits, spts = spdPts;
  spdUntil = millis();
  allFlush &= flushes(renderSpeed);
  uint16_t seff = (uint16_t)(spts * 2 / 3);
  want = (uint8_t)(seff / 2 > 18 ? 18 : seff / 2);
  printf("      %u targets, %u points, eff %u -> SPE +%u (want %u)\n", shits, spts, seff, spdGain, want);
  ck(spdOverUntil && spdEff() == seff && spdGain == want && pet.trSpe == want, "SPE gain = min(18, (points*2/3) / 2)");
  ck(pet.spdHi == shits, "the record counts TARGETS hit, not points");
  slp(600); allFlush &= flushes(renderSpeed);
  ck(allFlush, "every reaction-test frame flushed");
  trBefore = pet.trSpe; leaveSpeed();
  ck(pet.trSpe == trBefore, "leaving from its card does not pay twice");
  // leave early banks
  freshPet();
  startSpeedGame(); skipIntro(); renderSpeed();
  for (int i = 0; i < 9; i++) { spdTK[0] = spdTK[1] = 0; spdSpawnAt(0, 2); spdTX[0] = 233; spdTY[0] = 233; spdTap(233, 233); }
  seff = spdEff();
  onSwipeV(1);                                         // the swipe exit
  ck(!spdOpen && pet.trSpe == seff / 2 && seff / 2 > 9 / 2, "swiping out early banks the effective score");

  // ================================================================== BALL
  printf("-- ball game (DEF)\n");
  freshPet();
  t0 = millis();
  startGame();
  ck(gameOpen && gameUntil == trnGoAt + 20000 && trnGoAt >= t0 + 1700, "the ball game counts down, then runs 20 s");
  allFlush = flushes(renderGame);
  float by0 = gBY[0];
  renderGame(); renderGame();
  ck(gBY[0] == by0, "the ball hangs still during the countdown");
  gameTap((int16_t)gBX[0], (int16_t)gBY[0]);
  ck(gameScore == 0, "and cannot be hit before GO");
  skipIntro();
  gBX[0] = 233; gBY[0] = 250; gBGold[0] = false;
  gameTap(233, 250);
  ck(gameScore == 1 && gamePts == 1 && trnCombo == 1, "after GO a hit is 1 point");
  for (int i = 0; i < 4; i++) { gBX[0] = 233; gBY[0] = 250; gameTap(233, 250); }
  ck(trnCombo == 5 && gamePts == 1 + 1 + 1 + 1 + 2, "the 5th hit in a row runs at x2");
  uint16_t gp0 = gamePts;
  gBX[0] = 233; gBY[0] = 250; gBGold[0] = true; gameTap(233, 250);
  ck(gamePts == gp0 + 2 + 2 && gameGolds == 1, "a GOLDEN ball adds 2 more per hit");
  // the header strip: a ball up there is hit, empty header leaves -- tested below
  gBX[0] = 233; gBY[0] = 40; gBGold[0] = false;
  uint8_t s0 = gameScore;
  gameTap(233, 40);
  ck(gameOpen && gameScore == s0 + 1, "a ball up in the header strip is hit, not a quit");
  // a pickup: points, but not a hit and the chain untouched
  gp0 = gamePts; s0 = gameScore; uint16_t c0 = trnCombo;
  gItKind = 2; gItX = 120; gItY0 = 200;
  gBX[0] = 350; gBY[0] = 300;
  gameTap(120, 200);
  ck(gamePts == gp0 + 3 && gameScore == s0 && trnCombo == c0 && gameItems == 1 && gItKind == 0,
     "a floating star is 3 points, not a hit, chain untouched");
  // a floor touch breaks the chain and costs a life
  gBX[0] = 233; gBY[0] = 383; gBVY[0] = 6;
  uint8_t mi0 = gameMisses;
  stepGame();
  ck(gameMisses == mi0 + 1 && trnCombo == 0, "a ball on the floor costs a life and breaks the chain");
  // the second ball
  ck(!gBOn[1], "one ball until 12 hits");
  while (gameScore < 12) { gBX[0] = 233; gBY[0] = 250; gameTap(233, 250); }
  ck(gBOn[1], "the 12th hit drops in a second ball");
  gBX[1] = 140; gBY[1] = 200; gBX[0] = 330; gBY[0] = 300;
  s0 = gameScore;
  gameTap(140, 200);
  ck(gameScore == s0 + 1 && gBVY[1] < 0, "and it is hit like the first");
  gBY[1] = 383; gBVY[1] = 6; gBY[0] = 200; gBVY[0] = -3;
  mi0 = gameMisses;
  stepGame();
  ck(gameMisses == mi0 + 1, "either ball on the floor costs a life");
  // wind
  gBY[0] = gBY[1] = 150; gBVY[0] = gBVY[1] = -2;
  trnGoAt = millis() - 11000; gWindNext = millis(); gWindUntil = 0;
  stepGame();
  ck(gWindUntil == 0 || (int32_t)(gWindUntil - millis()) <= 0, "no wind before 12 s");
  trnGoAt = millis() - 12500;
  float vx0 = gBVX[0];
  stepGame();
  ck((int32_t)(gWindUntil - millis()) > 0 && gWindDir != 0, "from 12 s a gust blows");
  ck((gBVX[0] - vx0) * gWindDir > 0.1f, "and pushes the ball its way");
  allFlush &= flushes(renderGame);
  // the end
  gBY[0] = gBY[1] = 150;
  uint8_t gs = gameScore; uint16_t gpts = gamePts;
  gameUntil = millis();
  allFlush &= flushes(renderGame);
  uint16_t geff = (uint16_t)(gpts * 2 / 3);
  want = (uint8_t)(geff / 2 > 18 ? 18 : geff / 2);
  printf("      %u hits, %u points, eff %u -> DEF +%u (want %u)\n", gs, gpts, geff, gameDefGain, want);
  ck(gameOverUntil && gameEff() == geff && gameDefGain == want && pet.trDef == want, "DEF gain = min(18, (points*2/3) / 2)");
  ck(pet.gameHi == gs, "the record counts HITS, not points");
  slp(600); allFlush &= flushes(renderGame);
  ck(allFlush, "every ball-game frame flushed");
  trBefore = pet.trDef; leaveGame();
  ck(pet.trDef == trBefore && !gameOpen, "leaving from its card does not pay twice");
  // leave early through the header banks
  freshPet();
  startGame(); skipIntro();
  for (int i = 0; i < 10; i++) { gBX[0] = 233; gBY[0] = 250; gBGold[0] = true; gameTap(233, 250); }
  geff = gameEff();
  gBX[0] = 233; gBY[0] = 300;
  gameTap(60, 40);                                 // empty header strip
  ck(!gameOpen && pet.trDef == geff / 2 && geff / 2 > 10 / 2, "tapping the empty header leaves and banks the effective score");
  ck(pet.gameHi == 10, "and records the 10 hits");
  // the ceiling
  freshPet();
  startGame(); skipIntro();
  for (int i = 0; i < 80; i++) { gBX[0] = 233; gBY[0] = 250; gBGold[0] = true; gameTap(233, 250); gBOn[1] = false; }
  gameUntil = millis(); renderGame();
  ck(gameEff() / 2 > 18 && gameDefGain == 18 && trnGrade == 3, "the ball game gains at most 18 too");
  gameOpen = false;

  printf("%s\n", bad ? "FAILURES" : "all good");
  return bad ? 1 : 0;
}
