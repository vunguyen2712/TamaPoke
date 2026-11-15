// The main-screen "!" for a notable wild encounter, driven through the REAL
// sketch: render(), taps on the pet's screen, the hub, the WILD screen. The
// logic (peek == roll, the reasons) is chain_test; this proves the CALLERS ask
// it -- the badge announces exactly the creature the screen then shows, it hides
// when it must, and its hit area lands on nothing else on the main screen.
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include "battle.h"
#include "items.h"
#include "wild.h"
#include "rtcbat.h"
#include "species.h"
#include <cstdio>
#include <cstring>
#include <chrono>
#include <thread>
uint32_t g_seed = 4242; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX = 0, g_touchY = 0; volatile bool g_touchDown = false;
void FakeESP::restart() { exit(0); }
int FakeSerial::available() { return 0; }
String FakeSerial::readStringUntil(char) { return String(""); }
void setup(); void loop(); void render(); void onSwipe(int dir);
uint8_t uiCurrentScreen();
extern const char *const SCREEN_NAME[];
extern Pet pet;
extern Arduino_Canvas *gfx;
extern bool battleOpen, hubOpen, wildOpen, menuOpen;
void wildHubLabel(char *out, size_t n, const char *name);
uint8_t wildAlertNow();
void wildBadgeRect(int16_t *x0, int16_t *y0, int16_t *x1, int16_t *y1);
uint16_t wildMainOverlaps(int16_t x0, int16_t y0, int16_t x1, int16_t y1);
bool wildBadgeHitTest(int16_t x, int16_t y);
bool inPetZone(int16_t x, int16_t y);
void featCloseAll();
#define ROW(i) (88 + (i) * 50 + 22)
#define CXT 233

static int bad = 0;
static void ck(bool ok, const char *w) { printf("%s  %s\n", ok ? "PASS" : "FAIL", w); if (!ok) bad++; }
static void pump(int n) {
  for (int i = 0; i < n; i++) { std::this_thread::sleep_for(std::chrono::milliseconds(30)); loop(); }
}
static void click(int x, int y) {
  g_touchX = x; g_touchY = y; g_touchDown = true; emuFireInterrupt(); pump(5);
  g_touchDown = false; emuFireInterrupt(); pump(3);
}
static bool on(const char *scr) { return !strcmp(SCREEN_NAME[uiCurrentScreen()], scr); }
#define DAY0 20400u
static void setDay(uint32_t day, int hour) {
  rtcSetEpoch(day * 86400u + hour * 3600u + 60u);
  pet.lastSeenEpoch = rtcEpoch();
}
static void toMain() { featCloseAll(); hubOpen = false; menuOpen = false; battleOpen = false; pump(1); }

static int16_t bx, by;          // the badge's centre, from its hit rect
static uint16_t pxAt(int x, int y) { return gfx->buffer()[y * 466 + x]; }
// The badge's fill, 10 px right of its centre: inside the circle, clear of "!"
static bool badgeDrawn() {
  gfx->frameReady = false;
  render();
  uint16_t c = pxAt(bx + 10, by), c2 = pxAt(bx - 10, by + 3);
  return (c == UI_BAR_BAD || c == UI_BAR_WARN) && c2 == c;
}
static bool peekNow(WildNext &n) {
  return wild.peek(pet.currentDay(), pet.lastSeenEpoch, pet.region, pet.level(), bag.has(ITEM_SHINY_CHARM), n);
}

int main() {
  setup(); pump(4);
  click(233, 138); pump(2);            // region: KANTO
  click(233, 145);                     // starter row 0
  ck(!pet.awaitingStarter() && pet.isEgg(), "starter picked, the egg is waiting");
  setDay(DAY0, 13);

  int16_t x0, y0, x1, y1;
  wildBadgeRect(&x0, &y0, &x1, &y1);
  bx = (x0 + x1) / 2; by = (y0 + y1) / 2;

  // ---------------- placement: it lands on nothing else ----------------
  printf("      badge hit area x %d..%d y %d..%d\n", x0, x1, y0, y1);
  uint16_t ov = wildMainOverlaps(x0, y0, x1, y1);
  if (ov) printf("      overlaps mask 0x%x\n", ov);
  ck(ov == 0, "the hit area misses the band, pet zone, TASK DONE, evolve/farewell, buttons, banners, bars");
  { int inPet = 0, inBand = 0;
    for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) {
      if (inPetZone(x, y)) inPet++;
      if (y >= 28 && y < 94) inBand++;
    }
    ck(inPet == 0 && inBand == 0, "every pixel of it checked against inPetZone() and the menu band"); }
  // the overlap check has teeth: each neighbour, measured with the sketch's own constants
  ck(wildMainOverlaps(129, 388, 139, 398) & 32, "...a rect on the FOOD button is caught (buttons[], BTN_HIT)");
  ck(wildMainOverlaps(228, 200, 238, 210) & 8, "...a rect on the evolve button is caught (EVO_BTN_*)");
  ck(wildMainOverlaps(35, 200, 45, 210) & 16, "...a rect at the farewell button's left end is caught (FAR_BTN_*)");
  ck(wildMainOverlaps(228, 115, 238, 125) & 4, "...the TASK DONE banner is caught");
  ck(wildMainOverlaps(20, 60, 30, 70) & 1, "...the menu band is caught");
  ck(wildMainOverlaps(200, 200, 210, 210) & 2, "...the pet zone is caught");
  ck(wildMainOverlaps(60, 350, 70, 360) & 64, "...the action band is caught (HUD_BAND_Y)");
  ck(wildMainOverlaps(2, 260, 12, 270) & 2048, "...off the round panel is caught");
  ck(wildBadgeHitTest(x0, y0) && wildBadgeHitTest(x1, y1) && !wildBadgeHitTest(x0 - 1, by) &&
     !wildBadgeHitTest(x1 + 1, by), "the hit test is exactly that rect");
  ck(bx - 17 >= x0 && bx + 17 <= x1 && by - 17 >= y0 && by + 17 <= y1, "the drawn badge (r <= 17) sits inside its hit area");

  // ---------------- hidden while the pet is an egg ----------------
  { WildNext n; ck(peekNow(n) && !pet.isRegistered(n.dex), "an egg: the next encounter IS notable (new)...");
    ck(wildAlertNow() == 0 && !badgeDrawn(), "...but the badge is hidden while the pet is an egg"); }

  // ---------------- hatch: it shows, and tapping it opens WILD on that creature
  pet.dbgHatchAs(9, false);
  pet.ageMinutes = 30 * MINUTES_PER_LEVEL;
  pet.relearnFromLevel();
  while (pet.hasLearnOffer()) pet.declineLearn();
  toMain();
  ck(on("main"), "on the main screen");
  ck(wildAlertNow() != 0, "hatched: the alert is up");
  // clock unset, checked while nearly everything is still NEW, with the
  // companion that the encounter waiting on day 0 WOULD be notable
  { uint32_t keep = pet.lastSeenEpoch;
    pet.lastSeenEpoch = 0;
    WildNext n0; bool h0 = peekNow(n0);
    ck(h0 && wildAlertWhy(n0.dex, n0.shiny, pet.isRegistered(n0.dex)) != 0,
       "clock unset: there IS a notable encounter on day 0...");
    ck(wildAlertNow() == 0, "...but with no clock the badge is hidden");
    pet.lastSeenEpoch = keep;
    ck(wildAlertNow() != 0, "and it is back with the clock"); }
  ck(badgeDrawn(), "and the badge is drawn");
  ck(gfx->frameReady, "the main screen with the badge flushes");
  { char lbl[32]; wildHubLabel(lbl, sizeof(lbl), "WILD");
    ck(strstr(lbl, " !") != nullptr, "the hub's WILD row carries the \"!\" too"); }

  // over several days, every encounter: what was announced is what is shown.
  // Kanto's common base forms go in the Pokedex first, so the days hold both
  // notable encounters (the badge) and plain ones (no badge: the hub).
  for (int16_t d = 1; d <= 151; d++) if (DEX_TBL[d].rarity == R_COMUN) pet.registerCaught(d, false);
  int tapped = 0, viaHub = 0, mismatch = 0, notable = 0;
  for (uint32_t d = DAY0; d < DAY0 + 6; d++) {
    setDay(d, (int)(7 + (d % 4) * 4));
    for (int e = 0; e < WILD_PER_DAY; e++) {
      toMain();
      WildNext n; bool have = peekNow(n);
      if (!have) { mismatch++; break; }
      bool alert = wildAlertNow() != 0;
      if (alert != (wildAlertWhy(n.dex, n.shiny, pet.isRegistered(n.dex)) != 0)) mismatch++;
      if (alert) { notable++; click(bx, by); tapped++; }
      else { click(bx, by); if (wildOpen) mismatch++;     // no alert: the spot is inert
             hubOpen = true; click(CXT, ROW(0)); viaHub++; }
      if (!wildOpen || !on("wild")) { mismatch++; break; }
      if (wild.st.phase != WP_MEET || wild.st.dex != n.dex || wild.st.level != n.level || wild.st.shiny != n.shiny)
        mismatch++;
      click(CXT, ROW(5));                                // RUN
    }
  }
  printf("      %d encounters opened from the badge, %d from the hub\n", tapped, viaHub);
  ck(mismatch == 0, "every encounter the screen showed is the one the alert peeked");
  ck(tapped > 0, "...and the badge really opened WILD");
  ck(viaHub > 0, "...and plain encounters had no badge");

  // ---------------- none left: hidden, and the spot does nothing ----------------
  ck(wild.left() == 0, "the day is spent");
  toMain();
  ck(wildAlertNow() == 0 && !badgeDrawn(), "no encounters left: no badge");
  click(bx, by);
  ck(!wildOpen && on("main"), "and tapping where it was does not open WILD");
  { char lbl[32]; wildHubLabel(lbl, sizeof(lbl), "WILD");
    ck(strstr(lbl, " !") == nullptr, "nor does the hub row say \"!\""); }

  // ---------------- a new day; clock and ceremony guards ----------------
  setDay(DAY0 + 40, 13);
  toMain();
  { int tries = 0;
    while (!wildAlertNow() && tries < 20) { setDay(DAY0 + 41 + tries, 13); tries++; }
    ck(wildAlertNow() != 0, "a later day with a notable encounter (the companion)"); }
  { pet.ceremony = CER_FAREWELL;
    ck(wildAlertNow() == 0, "mid-ceremony: hidden");
    pet.ceremony = CER_NONE; }
  ck(wildAlertNow() != 0, "and back once both are restored");

  // ---------------- the Pokedex changing clears a NEW-only alert at once --------
  { int found = 0;
    for (int t = 0; t < 60 && !found; t++) {
      setDay(DAY0 + 100 + t, 13);
      WildNext n;
      if (peekNow(n) && wildAlertWhy(n.dex, n.shiny, pet.isRegistered(n.dex)) == WA_NEW) {
        found = 1;
        ck(wildAlertNow() == WA_NEW, "a next encounter that is only NEW: the alert says NEW");
        pet.registerCaught(n.dex, false);
        ck(wildAlertNow() == 0, "registering it clears the alert (no stale cache)");
      }
    }
    ck(found, "found such a day"); }

  // ---------------- beaten and waiting for a ball: no alert ----------------
  { setDay(DAY0 + 300, 13);
    toMain();
    int tries = 0;
    while (!wildAlertNow() && tries < 30) { setDay(DAY0 + 301 + tries, 13); tries++; }
    WildNext n; peekNow(n);
    click(bx, by);
    ck(wildOpen && wild.st.dex == n.dex, "opened on the announced creature");
    wild.fightStarted(); wild.battleEnded(true, false, 99, bag);
    toMain();
    ck(wild.st.phase == WP_CATCH && wildAlertNow() == 0, "already beaten (CATCH): no alert, it is no news");
    wild.runAway(); }

  printf(bad ? "wildalert_ui_test: %d FAILED\n" : "wildalert_ui_test: all passed\n", bad);
  return bad ? 1 : 0;
}
