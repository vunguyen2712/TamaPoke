// The catch, as the player sees it: one ball is thrown at the creature, draws
// it in, drops, and shakes up to three times -- and within those shakes it is
// either caught (three shakes, a click and sparkles) or it bursts free and runs
// away. Driven through the REAL sketch: the WILD screen's ball rows, the
// sketch's own render(), and the framebuffer.
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include "items.h"
#include "wild.h"
#include <cstdio>
#include <cstring>
#include <chrono>
#include <thread>
uint32_t g_seed = 2024; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX = 0, g_touchY = 0; volatile bool g_touchDown = false;
void FakeESP::restart() { exit(0); }
int FakeSerial::available() { return 0; }
String FakeSerial::readStringUntil(char) { return String(""); }
void setup(); void loop(); void render();
extern Pet pet;
extern Arduino_Canvas *gfx;
extern bool wildOpen, hubOpen;
void wildOpened();
uint8_t uiCurrentScreen();
extern const char *const SCREEN_NAME[];
bool wildCatchAnimating();
uint8_t wildCatchStage(uint32_t *into);
uint8_t wildCatchShakesShown();
void onSwipe(int dir);
// stage ids, in ui_wild.ino's order
enum { S_OFF = 0, S_THROW, S_ABSORB, S_DROP, S_SHAKE, S_CLICK, S_BURST, S_FLEE, S_DONE };
#define WC_SHAKE 650
#define WC_SHAKE_MOVE 320
#define ROW(i) (88 + (i) * 50 + 22)
#define CXT 233

static int bad = 0;
static void ck(bool ok, const char *w) { printf("%s  %s\n", ok ? "PASS" : "FAIL", w); if (!ok) bad++; }
static void pump(int n) { for (int i = 0; i < n; i++) { std::this_thread::sleep_for(std::chrono::milliseconds(30)); loop(); } }
static void click(int x, int y) {
  g_touchX = x; g_touchY = y; g_touchDown = true; emuFireInterrupt(); pump(5);
  g_touchDown = false; emuFireInterrupt(); pump(3);
}
static uint16_t px(int x, int y) { return gfx->buffer()[y * 466 + x]; }

// A beaten creature waiting for a ball, on a fresh day if today is used up.
static bool toCatch() {
  for (int tries = 0; tries < 4; tries++) {
    wildOpen = true; wildOpened(); pump(1);
    if (wild.st.phase == WP_MEET) {
      wild.fightStarted();
      wild.battleEnded(true, false, 99, bag);
      if (wild.st.phase == WP_CATCH) return true;
    }
    if (wild.st.phase == WP_CATCH) return true;
    pet.lastSeenEpoch += 86400;               // the next day: three more
  }
  return false;
}

struct Seen { bool order = true; uint8_t shakes = 0; bool click = false, burst = false, flee = false, flushedAll = true; };

// Steps game time and records what the animation did. Also: no tap gets
// through while it plays.
static Seen watch() {
  Seen s;
  uint8_t last = S_OFF, lastShake = 0;
  emuSetTimeScale(4);
  for (int i = 0; i < 600 && wildCatchAnimating(); i++) {
    uint32_t into = 0;
    uint8_t st = wildCatchStage(&into);
    if (st < last) s.order = false;            // stages never go backwards
    if (st == S_SHAKE) { uint8_t k = (uint8_t)(into / WC_SHAKE) + 1; if (k > lastShake) lastShake = k; }
    if (st == S_CLICK) s.click = true;
    if (st == S_BURST) s.burst = true;
    if (st == S_FLEE) s.flee = true;
    last = st;
    gfx->frameReady = false; render();
    if (!gfx->frameReady && wildCatchAnimating()) s.flushedAll = false;
    pump(1);
  }
  emuSetTimeScale(1);
  s.shakes = lastShake;
  return s;
}

int main() {
  setup(); pump(4);
  click(233, 138); pump(2);
  click(233, 145);
  if (pet.isEgg()) pet.dbgHatchAs(9, false);
  pet.ageMinutes = 30 * MINUTES_PER_LEVEL;
  bag.add(ITEM_POKEBALL, 60); bag.add(ITEM_ULTRABALL, 60); bag.add(ITEM_GREATBALL, 10);
  hubOpen = false;

  // ---- one throw: it is SETTLED before the first frame, then it plays
  ck(toCatch(), "a beaten wild creature waits for a ball");
  int16_t dex = wild.st.dex;
  uint8_t pb = bag.count(ITEM_POKEBALL);
  int pc = party.count() + party.boxCount();
  click(CXT, ROW(2));                                  // POKE BALL
  ck(bag.count(ITEM_POKEBALL) == pb - 1, "the throw spends exactly one ball");
  ck(wildCatchAnimating(), "and the throw animation starts");
  ck(wild.st.phase != WP_CATCH, "the result is already decided and saved -- one throw, no second try");
  bool caught1 = party.count() + party.boxCount() == pc + 1;
  { Party p2; p2.begin(); int n = 0; for (int i = 0; i < PARTY_SLOTS; i++) if (!p2.slots[i].empty()) n++;
    for (int i = 0; i < BOX_SLOTS; i++) if (!p2.box[i].empty()) n++;
    ck(n == (caught1 ? pc + 1 : pc), "...and is on flash already, mid-animation"); }

  // taps do nothing while it plays
  click(CXT, ROW(5));                                  // where LET GO / RUN would be
  click(233, 412);                                     // BACK
  ck(wildCatchAnimating() && wildOpen, "taps are ignored while the ball is in the air");

  // ---- the ball is drawn: POKE BALL red on its top half, at rest in a shake
  emuSetTimeScale(4);
  bool sawBall = false;
  for (int i = 0; i < 400 && wildCatchAnimating(); i++) {
    uint32_t into = 0;
    if (wildCatchStage(&into) == S_SHAKE && (into % WC_SHAKE) >= WC_SHAKE_MOVE + 60) {
      render();
      sawBall = px(232, 193) == 0xE8A4 && px(232, 218) == 0xFFFF;   // red top, white bottom
      break;
    }
    if (wildCatchStage(nullptr) > S_SHAKE) break;
    pump(1);
  }
  emuSetTimeScale(1);
  ck(sawBall || wildCatchShakesShown() == 0, "a POKE BALL is drawn red over white where it shakes");
  Seen s1 = watch();
  ck(s1.order, "throw -> draw in -> drop -> shakes -> result, in order");
  ck(s1.flushedAll, "every frame of it flushes");
  ck(!wildCatchAnimating(), "and it ends");

  // ---- many throws: both endings, each exactly as specified
  int caughtN = 0, fledN = 0, badCatch = 0, badFlee = 0, shakeHist[4] = { 0 };
  bool ultraDrawn = false, greatDrawn = false;
  for (int enc = 0; enc < 30 && (caughtN < 3 || fledN < 3); enc++) {
    if (party.count() >= PARTY_SLOTS) for (int i = 1; i < PARTY_SLOTS; i++) party.releaseAt(i);
    if (!toCatch()) break;
    uint8_t ballRow = (enc % 3 == 0) ? 4 : (enc % 3 == 1) ? 2 : 3;   // ULTRA, POKE, GREAT
    int before = party.count() + party.boxCount();
    click(CXT, ROW(ballRow));
    if (!wildCatchAnimating()) continue;
    bool caught = party.count() + party.boxCount() == before + 1;
    // the ball's colour, once per kind, at rest in its first shake
    if (wildCatchShakesShown() > 0 && (ballRow != 2)) {
      emuSetTimeScale(4);
      for (int i = 0; i < 400 && wildCatchAnimating(); i++) {
        uint32_t into = 0;
        uint8_t st = wildCatchStage(&into);
        if (st == S_SHAKE && (into % WC_SHAKE) >= WC_SHAKE_MOVE + 60) {
          render();
          if (ballRow == 4 && px(232, 193) == 0x31A6) ultraDrawn = true;
          if (ballRow == 3 && px(232, 193) == 0x2B7F) greatDrawn = true;
          break;
        }
        if (st > S_SHAKE) break;
        pump(1);
      }
      emuSetTimeScale(1);
    }
    uint8_t want = wildCatchShakesShown();
    Seen s = watch();
    if (s.shakes <= 3) shakeHist[s.shakes]++;
    if (caught) {
      caughtN++;
      if (!(s.shakes == 3 && want == 3 && s.click && !s.burst && !s.flee && s.order)) badCatch++;
    } else {
      fledN++;
      if (!(s.shakes < 3 && s.shakes == want && s.burst && s.flee && !s.click && s.order)) badFlee++;
    }
  }
  printf("      caught %d, fled %d; shakes seen 0:%d 1:%d 2:%d 3:%d\n", caughtN, fledN,
         shakeHist[0], shakeHist[1], shakeHist[2], shakeHist[3]);
  ck(caughtN >= 3 && fledN >= 1, "both endings happened");
  ck(badCatch == 0, "every catch: exactly three shakes, then the click (no burst)");
  ck(badFlee == 0, "every escape: fewer than three shakes, it bursts out and runs away");
  ck(ultraDrawn, "an ULTRA BALL is drawn in its own colours");
  ck(greatDrawn, "a GREAT BALL is drawn in its own colours");

  // ---- the result is shown once it ends, and the screen is usable again
  ck(!wildCatchAnimating() && wild.st.phase != WP_CATCH, "after the throw the catch face is gone");
  gfx->frameReady = false; render();
  ck(gfx->frameReady, "the result face flushes");

  // ---- leaving mid-throw is safe: the result was saved, nothing replays
  if (party.count() >= PARTY_SLOTS) for (int i = 1; i < PARTY_SLOTS; i++) party.releaseAt(i);
  if (toCatch()) {
    int before = party.count() + party.boxCount();
    uint8_t u = bag.count(ITEM_ULTRABALL);
    click(CXT, ROW(4));
    ck(wildCatchAnimating(), "a throw is in the air");
    onSwipe(-1);                                       // swipe away mid-throw
    ck(!wildOpen && !wildCatchAnimating(), "swiping away drops the animation");
    wildOpen = true; wildOpened(); pump(1);
    ck(!wildCatchAnimating(), "reopening WILD does not replay it");
    ck(bag.count(ITEM_ULTRABALL) == u - 1, "and the ball stays spent exactly once");
    ck(party.count() + party.boxCount() >= before, "and a catch it made is still there");
  }

  // ---- no room: nothing is thrown, nothing animates
  for (int i = 0; i < PARTY_SLOTS; i++) if (party.slots[i].empty()) { PartyMon m; m.dex = 19; m.level = 5; party.add(m); }
  for (int i = 0; i < BOX_SLOTS; i++) if (party.box[i].empty()) { PartyMon m; m.dex = 19; m.level = 5; party.boxAdd(m); }
  if (toCatch()) {
    uint8_t p = bag.count(ITEM_POKEBALL);
    click(CXT, ROW(2));
    ck(!wildCatchAnimating() && bag.count(ITEM_POKEBALL) == p, "party and box full: no throw, no animation, ball kept");
  }

  printf(bad ? "catchanim_ui_test: %d FAILED\n" : "catchanim_ui_test: all passed\n", bad);
  return bad ? 1 : 0;
}
