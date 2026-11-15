// Move effects: every move has one, a turn plays as ONE timeline (the second
// move starts once the first has played), the target flinches and its HP bar
// drains when the blow LANDS, and the effect is really drawn. Driven through
// the REAL sketch: startBattle(), the FIGHT menu's taps, render().
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include "battle.h"
#include "moves.h"
#include <cstdio>
#include <cstring>
#include <chrono>
#include <thread>
uint32_t g_seed = 4242; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX = 0, g_touchY = 0; volatile bool g_touchDown = false;
void FakeESP::restart() { exit(0); }
int FakeSerial::available() { return 0; }
String FakeSerial::readStringUntil(char) { return String(""); }
void setup(); void loop(); void render(); void onTap(int16_t, int16_t);
extern Pet pet;
extern Arduino_Canvas *gfx;
extern bool battleOpen;
extern Combatant btlYou, btlFoe;
extern uint8_t btlMsgCount, btlMenu, btlKind;
extern uint32_t btlHitUntil[2], btlHitFrom[2], btlFaintUntil[2];
extern uint16_t btlHpShown[2];
void startBattle(int16_t dex, uint8_t lvl);
uint8_t moveFxShape(uint8_t mv);
uint32_t btlFxImpactOf(uint8_t from);
uint32_t btlFxEndOf(uint8_t from);
uint32_t btlFxEnd();
bool btlFxActive();
#define BTL_FAINT_MS 700

static int bad = 0;
static void ck(bool ok, const char *w) { printf("%s  %s\n", ok ? "PASS" : "FAIL", w); if (!ok) bad++; }
static void slp(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
static void waitUntil(uint32_t t) { while ((int32_t)(millis() - t) < 0) slp(2); }

// A fight where both sides hold exactly one move; you are faster.
static void setup1v1(uint8_t yours, uint8_t theirs, uint16_t foeHp) {
  startBattle(9, 50);
  btlYou.moves[0] = yours; btlYou.moves[1] = btlYou.moves[2] = btlYou.moves[3] = 0;
  btlFoe.moves[0] = theirs; btlFoe.moves[1] = btlFoe.moves[2] = btlFoe.moves[3] = 0;
  btlYou.base[SI_SPE] = 999; btlFoe.base[SI_SPE] = 1;
  btlYou.maxHp = btlYou.hp = 900; btlHpShown[0] = 900;
  btlFoe.maxHp = 900; btlFoe.hp = foeHp; btlHpShown[1] = foeHp;
  btlMsgCount = 0; btlMenu = 0;
  render();
}
static void fight() { onTap(233, 290); onTap(69 + 40, 274 + 20); }   // FIGHT, move 1

static int countCol(uint16_t col) {
  int n = 0; const uint16_t *b = gfx->buffer();
  for (int i = 0; i < 466 * 252; i++) if (b[i] == col) n++;
  return n;
}

int main() {
  setup(); for (int i = 0; i < 4; i++) loop();
  if (pet.awaitingStarter()) pet.chooseStarter(4);
  if (pet.isEgg()) pet.dbgHatchAs(6, false);
  pet.ageMinutes = 50UL * MINUTES_PER_LEVEL;
  pet.relearnFromLevel();
  while (pet.hasLearnOffer()) pet.declineLearn();

  // ---- every move has a look
  int none = 0;
  for (uint8_t m = 1; m < MOVE_COUNT; m++)
    if (!moveFxShape(m)) { printf("      %s has no effect\n", MOVE_TBL[m].name); none++; }
  ck(none == 0, "every move has an effect");
  ck(moveFxShape(MV_THUNDERBOLT) != moveFxShape(MV_TACKLE) &&
     moveFxShape(MV_HYPER_BEAM) != moveFxShape(MV_EMBER) &&
     moveFxShape(MV_SWORDS_DANCE) != moveFxShape(MV_GROWL),
     "and they differ: a bolt is not a tackle, a beam not an ember, a buff not a debuff");

  // ---- one turn, one timeline: yours lands, THEN theirs starts
  setup1v1(MV_TACKLE, MV_TACKLE, 900);
  uint32_t t0 = millis();
  fight();
  ck(battleOpen && btlYou.hp < 900 && btlFoe.hp < 900, "both sides hit this turn");
  uint32_t youImp = btlFxImpactOf(0), youEnd = btlFxEndOf(0);
  uint32_t foeImp = btlFxImpactOf(1);
  ck(youImp > t0 && youEnd > youImp, "your effect is on the timeline");
  ck(foeImp >= youEnd, "the foe's move starts only once yours has played");
  ck(btlHitFrom[1] == youImp, "the foe flinches when YOUR blow lands");
  ck(btlHitFrom[0] == foeImp, "and you flinch when ITS blow lands, later");
  // the bars hold until the blow, then drain
  render();
  ck(btlHpShown[1] == 900, "the foe's HP bar holds before your blow lands");
  ck(btlHpShown[0] == 900, "and yours holds through the whole of your move");
  waitUntil(youImp + 30); render(); render();
  ck(btlHpShown[1] < 900, "the foe's bar drains once it lands");
  ck(btlHpShown[0] == 900, "while yours still waits for its move");
  waitUntil(foeImp + 30); render(); render();
  ck(btlHpShown[0] < 900, "and yours drains when the foe's lands");
  waitUntil(btlFxEnd() + 10);
  ck(!btlFxActive(), "and it all ends");

  // ---- the effect is DRAWN, in the move's colours, and flushes every frame
  setup1v1(MV_FLAMETHROWER, MV_RECOVER, 900);
  int before = countCol(0xFC00);               // FIRE's main tone
  fight();
  uint32_t imp = btlFxImpactOf(0);
  int peak = 0; bool flushed = true;
  while (millis() < btlFxEndOf(0)) {
    gfx->frameReady = false; render();
    if (!gfx->frameReady) flushed = false;
    int n = countCol(0xFC00); if (n > peak) peak = n;
    slp(20);
  }
  printf("      fire pixels: %d before, %d at the peak\n", before, peak);
  ck(peak > before + 200, "FLAMETHROWER is drawn in fire colours");
  ck(flushed, "every frame of it flushes");
  ck(imp > 0, "and it had an impact");
  waitUntil(btlFxEnd() + 10); render();
  ck(countCol(0xFC00) <= before + 10, "and leaves nothing behind once it ends");

  setup1v1(MV_ICE_BEAM, MV_RECOVER, 900);
  before = countCol(0x869E);                   // ICE's main tone
  fight(); peak = 0;
  while (millis() < btlFxEndOf(0)) { render(); int n = countCol(0x869E); if (n > peak) peak = n; slp(20); }
  ck(peak > before + 200, "ICE BEAM draws an ice beam");

  // Water Gun is a solid jet now, not Flamethrower's tumbling particles: one
  // unbroken body of water from mouth to target (thousands of WATER pixels at
  // once, where the old drop stream peaked at a few hundred).
  ck(moveFxShape(MV_WATER_GUN) != moveFxShape(MV_FLAMETHROWER) &&
     moveFxShape(MV_WATER_GUN) == moveFxShape(MV_HYDRO_PUMP),
     "WATER GUN and HYDRO PUMP share their own jet look");
  setup1v1(MV_WATER_GUN, MV_RECOVER, 900);
  before = countCol(0x2C7F);                   // WATER's main tone
  fight(); peak = 0;
  while (millis() < btlFxEndOf(0)) { render(); int n = countCol(0x2C7F); if (n > peak) peak = n; slp(20); }
  printf("      water pixels: %d before, %d at the peak\n", before, peak);
  ck(peak > before + 1500, "WATER GUN pours one unbroken jet of water");
  waitUntil(btlFxEnd() + 10); render();
  ck(countCol(0x2C7F) <= before + 10, "and the water is all gone once it ends");

  // a status move on yourself shows too
  setup1v1(MV_SWORDS_DANCE, MV_RECOVER, 900);
  before = countCol(0xFC00);
  fight(); peak = 0;
  while (millis() < btlFxEndOf(0)) { render(); int n = countCol(0xFC00); if (n > peak) peak = n; slp(20); }
  ck(peak > before + 30, "SWORDS DANCE raises ATTACK arrows");

  // ---- a knockout waits for the blow: the foe sinks AFTER the effect
  setup1v1(MV_TACKLE, MV_TACKLE, 1);
  btlKind = 0;
  fight();
  ck(btlFoe.fainted(), "a one-HP foe goes down");
  ck(btlFaintUntil[1] >= btlFxEndOf(0) + BTL_FAINT_MS, "it only drops once the hit has played");
  battleOpen = false;

  printf(bad ? "movefx_ui_test: %d FAILED\n" : "movefx_ui_test: all passed\n", bad);
  return bad ? 1 : 0;
}
