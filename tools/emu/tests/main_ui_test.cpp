// The v3.18 home screen (ui_home.ino), driven through the REAL sketch: render()
// and onTap(). It pins what a screenshot cannot:
//   - every main-screen state still flushes (egg, awake, asleep, night, low
//     needs, a ceremony), each in every biome
//   - the four arc gauges show THEIR stat: the right one, filled to the right
//     length, in the right colour -- read back as pixels off the canvas
//   - the four buttons are drawn where they are tapped, each tap does its
//     action, every target is >= UI_TAP_MIN, and no label touches another in
//     any of the six languages
//   - what sits on top of the HUD (the wild "!", the feed picker, the party
//     banner, the menu, the sleeping Z's) is still drawn over it
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include "battle.h"
#include "wild.h"
#include "rtcbat.h"
#include "species.h"
#include "dex.h"
#include "i18n.h"
#include <cstdio>
#include <cstring>
#include <chrono>
#include <thread>
uint32_t g_seed = 1818; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX = 0, g_touchY = 0; volatile bool g_touchDown = false;
void FakeESP::restart() { exit(0); }
int FakeSerial::available() { return 0; }
String FakeSerial::readStringUntil(char) { return String(""); }
void setup(); void loop(); void render(); void onTap(int16_t x, int16_t y);
uint8_t uiCurrentScreen();
extern const char *const SCREEN_NAME[];
extern Pet pet;
extern Arduino_Canvas *gfx;
extern bool menuOpen, trainOpen, gNight;
extern uint32_t feedMenuUntil, bathUntil, partyBannerUntil;
extern char partyBannerName[14];
void uiButtonAt(int i, int *cx, int *cy, int *half);
void uiButtonHitRect(int i, int *x0, int *y0, int *x1, int *y1);
bool uiButtonHit(int i, int x, int y);
bool uiButtonDisabled(int i);
const char *homeButtonLabel(int i);
void needGaugePoint(int i, int permille, int *x, int *y);
void needGaugeLabelRect(int i, int *x0, int *y0, int *x1, int *y1);
uint16_t needGaugeTrack();
uint16_t hudBandColor(int y);
uint8_t wildAlertNow();
void wildBadgeRect(int16_t *x0, int16_t *y0, int16_t *x1, int16_t *y1);
void featCloseAll();

static int bad = 0;
static void ck(bool ok, const char *w) { printf("%s  %s\n", ok ? "PASS" : "FAIL", w); if (!ok) bad++; }
static void pump(int n) { for (int i = 0; i < n; i++) { std::this_thread::sleep_for(std::chrono::milliseconds(20)); loop(); } }
static void click(int x, int y) {
  g_touchX = x; g_touchY = y; g_touchDown = true; emuFireInterrupt(); pump(5);
  g_touchDown = false; emuFireInterrupt(); pump(3);
}
static bool on(const char *scr) { return !strcmp(SCREEN_NAME[uiCurrentScreen()], scr); }
static uint16_t px(int x, int y) { return gfx->buffer()[y * 466 + x]; }
static bool flushes() { gfx->frameReady = false; render(); return gfx->frameReady; }
static void at(uint32_t day, int hour) { rtcSetEpoch(day * 86400u + hour * 3600u + 60u); pet.lastSeenEpoch = rtcEpoch(); }
static int16_t dexOfBiome(int b) { for (int16_t d = 1; d <= DEX_COUNT; d++) if (DEX_TBL[d].biome == b) return d; return 1; }
// labels in gauge order 0..3: JOY, FOOD, ENE, HYG
static void setNeeds(uint8_t joy, uint8_t food, uint8_t ene, uint8_t hyg) {
  pet.joy = joy; pet.fullness = food; pet.energy = ene; pet.hygiene = hyg;
}
static uint16_t expectColor(uint8_t v) {   // the rule, written out: 50+ green, 25+ amber, else red
  return v >= 50 ? UI_BAR_OK : (v >= 25 ? UI_BAR_WARN : UI_BAR_BAD);
}
#define DAY0 20400u

// Gauge i drawn for value v: filled with v's colour up to ~v%, track beyond.
static bool gaugeShows(int i, uint8_t v, bool verbose) {
  bool ok = true;
  for (int p = 50; p <= 950; p += 50) {
    int x, y;
    needGaugePoint(i, p, &x, &y);
    uint16_t c = px(x, y);
    int edge = v * 10;
    if (p < edge - 40) { if (c != expectColor(v)) { ok = false; if (verbose) printf("      gauge %d v=%u at %d/1000 (%d,%d): 0x%04X, want fill 0x%04X\n", i, v, p, x, y, c, expectColor(v)); } }
    else if (p > edge + 40) { if (c != needGaugeTrack()) { ok = false; if (verbose) printf("      gauge %d v=%u at %d/1000 (%d,%d): 0x%04X, want track 0x%04X\n", i, v, p, x, y, c, needGaugeTrack()); } }
  }
  return ok;
}

int main() {
  setup(); pump(4);
  click(233, 138); pump(2);            // region: KANTO
  click(233, 145);                     // starter row 0
  ck(!pet.awaitingStarter() && pet.isEgg(), "starter picked, the egg is waiting");
  at(DAY0, 13);
  ck(on("main"), "on the main screen");

  // ---------------- every state flushes ----------------
  ck(flushes(), "the egg flushes");
  { int nx = 0, ny = 0, x0, y0, x1, y1;   // the egg has no gauges: the pet's needs are not its yet
    needGaugePoint(1, 0, &nx, &ny);
    ck(px(nx, ny) != UI_BAR_OK && px(nx, ny) != needGaugeTrack(), "...and draws no gauges");
    needGaugeLabelRect(0, &x0, &y0, &x1, &y1); (void)x1; (void)y1; }
  pet.dbgHatchAs(dexOfBiome(0), false);
  while (pet.hasLearnOffer()) pet.declineLearn();
  ck(!pet.isEgg() && on("main"), "hatched, back on main");
  for (int b = 0; b < 6; b++) {
    pet.speciesId = dexOfBiome(b);
    const int hours[4] = { 13, 7, 19, 23 };
    for (int h : hours) {
      at(DAY0, h);
      char w[64]; snprintf(w, sizeof(w), "biome %d at %02d:00 flushes", b, h);
      if (!flushes()) ck(false, w);
    }
  }
  ck(true, "every biome at day, dawn, dusk and night flushes");
  at(DAY0, 23); ck(flushes() && gNight, "night flushes (and is night)");
  at(DAY0, 13); setNeeds(5, 3, 8, 1); ck(flushes(), "low needs flush");
  setNeeds(80, 80, 80, 80);
  pet.toggleLight(); ck(pet.sleeping && flushes(), "asleep flushes");

  // ---------------- asleep: only the LIGHT is live, its label says so ----------------
  { int lx, ly; uiButtonAt(1, &lx, &ly, nullptr);
    ck(uiButtonDisabled(0) && !uiButtonDisabled(1) && uiButtonDisabled(2) && uiButtonDisabled(3),
       "asleep: FOOD, BATH and TRAIN greyed, LIGHT live");
    ck(!strcmp(homeButtonLabel(1), "WAKE"), "the light button reads WAKE while asleep");
    render();
    // a greyed tile has no icon: its centre is the tile's flat fill; the live one has its moon
    int fx, fy; uiButtonAt(0, &fx, &fy, nullptr);
    ck(px(fx, fy) == px(fx - 18, fy + 18), "the greyed FOOD tile draws no icon");
    // the rising Z's: something light-inked appears right of the pet between frames
    int zs = 0;
    for (int y = 112; y < 200; y++) for (int x = 296; x < 370; x++) if (px(x, y) == UI_INK_NIGHT) zs++;
    ck(zs > 20, "the sleeping Z's are drawn");
    click(lx, ly);
    ck(!pet.sleeping, "tapping the LIGHT wakes it");
    ck(!strcmp(homeButtonLabel(1), "SLEEP"), "...and the label reads SLEEP again");
  }
  ck(on("main"), "(main)");

  // ---------------- the gauges read their own stat ----------------
  at(DAY0, 13);
  {
    // four different values, so a gauge wired to the wrong stat shows the wrong length/colour
    const uint8_t V[4] = { 80, 40, 10, 62 };   // JOY, FOOD, ENE, HYG
    setNeeds(V[0], V[1], V[2], V[3]);
    render();
    bool all = true;
    for (int i = 0; i < 4; i++) all &= gaugeShows(i, V[i], true);
    ck(all, "each gauge shows its own stat: JOY 80 green, FOOD 40 amber, ENE 10 red, HYG 62 green");
    // and every value along the way, on every gauge
    const uint8_t STEPS[] = { 0, 15, 24, 25, 49, 50, 75, 100 };
    bool sweep = true;
    for (uint8_t v : STEPS) {
      setNeeds(v, v, v, v);
      render();
      for (int i = 0; i < 4; i++) if (!gaugeShows(i, v, true)) sweep = false;
    }
    ck(sweep, "0, 15, 24, 25, 49, 50, 75, 100: length and green/amber/red follow the value");
    // the same at night: the track darkens, the rule does not change
    at(DAY0, 23); setNeeds(V[0], V[1], V[2], V[3]); render();
    bool night = true;
    for (int i = 0; i < 4; i++) night &= gaugeShows(i, V[i], true);
    ck(night && gNight, "...and at night");
    at(DAY0, 13);
    // the label: inked, inside the panel, clear of the gauge and of each other
    setNeeds(80, 80, 80, 80); render();
    bool lab = true;
    for (int i = 0; i < 4; i++) {
      int x0, y0, x1, y1, ink = 0;
      needGaugeLabelRect(i, &x0, &y0, &x1, &y1);
      for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) if (px(x, y) == UI_INK) ink++;
      if (ink < 30) { lab = false; printf("      gauge %d label: %d ink px\n", i, ink); }
      int cx0 = (x0 < 233 ? x0 : x1) - 233, cy0 = (y0 < 233 ? y0 : y1) - 233;
      if (cx0 * cx0 + cy0 * cy0 > 205 * 205) {   // the arc's rim starts at radius 206
        lab = false; printf("      gauge %d label runs onto its arc\n", i);
      }
    }
    ck(lab, "each gauge has its label drawn inside its arc");
  }

  // ---------------- buttons: drawn where they are tapped ----------------
  {
    render();
    bool drawn = true, panel = true, inside = true, size = true;
    for (int i = 0; i < 4; i++) {
      int cx, cy, h, x0, y0, x1, y1;
      uiButtonAt(i, &cx, &cy, &h);
      uiButtonHitRect(i, &x0, &y0, &x1, &y1);
      // the tile is there: its flat fill (not the band) a few px inside each corner...
      uint16_t band = hudBandColor(cy + h + 6);
      int in[4][2] = { { cx - h + 5, cy - h + 12 }, { cx + h - 5, cy - h + 12 }, { cx - h + 5, cy + h - 8 }, { cx + h - 5, cy + h - 8 } };
      for (auto &p : in) if (px(p[0], p[1]) == band || px(p[0], p[1]) == hudBandColor(p[1])) drawn = false;
      // ...and the band, not a tile, just past its shadow
      if (px(cx, cy + h + 6) != band) { drawn = false; printf("      button %d: no band under it\n", i); }
      // the whole tile (rounded corners r=14) is on the round panel
      int kx = (cx < 233 ? cx - h : cx + h - 1), ky = cy + h + 3 - 1;
      int ccx = kx + (cx < 233 ? 14 : -14), ccy = ky - 14;
      double d = sqrt((double)(ccx - 233) * (ccx - 233) + (double)(ccy - 233) * (ccy - 233)) + 14;
      if (d > 232.5) { panel = false; printf("      button %d corner at radius %.1f\n", i, d); }
      // the hit column holds the whole tile and its label
      if (!(x0 <= cx - h && x1 >= cx + h - 1 && y0 <= cy - h - 19 && y1 >= cy + h)) inside = false;
      if (x1 - x0 + 1 < 44 || y1 - y0 + 1 < 44) size = false;
    }
    ck(drawn, "every button's tile is drawn at uiButtonAt(), over the band");
    ck(panel, "every tile, shadow and corner included, is on the round panel");
    ck(inside, "each hit column covers its tile AND its label");
    ck(size, "every hit column is at least UI_TAP_MIN (44 px) each way");
    // the columns tile the row: no pixel belongs to two buttons, no gap between them
    bool tiled = true;
    for (int i = 0; i + 1 < 4; i++) {
      int a0, ay0, a1, ay1, b0, by0, b1, by1;
      uiButtonHitRect(i, &a0, &ay0, &a1, &ay1);
      uiButtonHitRect(i + 1, &b0, &by0, &b1, &by1);
      if (b0 != a1 + 1) tiled = false;
    }
    ck(tiled, "the four hit columns meet edge to edge with no overlap");
  }
  // every tap point inside a tile or on its label does that button's job
  {
    struct Off { int dx, dy; } offs[] = { { 0, 0 }, { -24, -24 }, { 24, 24 }, { -24, 22 }, { 0, -40 } };   // last: the label
    bool food = true, light = true, bath = true, train = true;
    for (auto &o : offs) {
      int cx, cy;
      uiButtonAt(0, &cx, &cy, nullptr);
      feedMenuUntil = 0; click(cx + o.dx, cy + o.dy);
      if (!feedMenuUntil) food = false;
      feedMenuUntil = 0; pump(1);
      uiButtonAt(1, &cx, &cy, nullptr);
      bool was = pet.sleeping; click(cx + o.dx, cy + o.dy);
      if (pet.sleeping == was) light = false;
      if (pet.sleeping) pet.toggleLight();
      uiButtonAt(2, &cx, &cy, nullptr);
      bathUntil = 0; pet.hygiene = 20; click(cx + o.dx, cy + o.dy);
      if (!bathUntil) bath = false;
      bathUntil = 0;
      uiButtonAt(3, &cx, &cy, nullptr);
      trainOpen = false; click(cx + o.dx, cy + o.dy);
      if (!trainOpen) train = false;
      trainOpen = false; pump(1);
    }
    ck(food, "FOOD: centre, corners and label all open the food picker");
    ck(light, "LIGHT: centre, corners and label all toggle sleep");
    ck(bath, "BATH: centre, corners and label all start a bath");
    ck(train, "TRAIN: centre, corners and label all open training");
    ck(on("main"), "(main)");
  }
  // no label touches its neighbour, in any language
  {
    bool clear = true;
    for (int l = 0; l < LANG_COUNT; l++) {
      setLang((Lang)l);
      for (int s = 0; s < 2; s++) {
        if ((s == 1) != pet.sleeping) pet.toggleLight();
        for (int i = 0; i + 1 < 4; i++) {
          int ax, ay, bx, by, h;
          uiButtonAt(i, &ax, &ay, &h); uiButtonAt(i + 1, &bx, &by, nullptr);
          const char *a = homeButtonLabel(i), *b = homeButtonLabel(i + 1);
          int aR = ax + (int)strlen(a) * 6, bL = bx - (int)strlen(b) * 6;
          bool vOverlap = (ay - h - 19) < (by - h - 19) + 16 && (by - h - 19) < (ay - h - 19) + 16;
          if (vOverlap && bL - aR < 4) { clear = false; printf("      lang %d: '%s' and '%s' %d px apart\n", l, a, b, bL - aR); }
          if (strlen(a) == 0) clear = false;
        }
      }
      if (pet.sleeping) pet.toggleLight();
    }
    setLang(LANG_EN);
    ck(clear, "the button labels stay 4+ px apart in all six languages, awake and asleep");
  }

  // ---------------- what sits on top of the HUD is still drawn ----------------
  {
    // the feed picker over the band and the labels
    feedMenuUntil = millis() + 4000; render();
    ck(px(110, 340) == UI_WHITE, "the food picker is drawn over the HUD");
    feedMenuUntil = 0;
    // the party banner
    snprintf(partyBannerName, sizeof(partyBannerName), "%s", "TEST");
    partyBannerUntil = millis() + 3000; render();
    ck(px(60, 213) == UI_BAR_OK, "the party banner is drawn over the scene");
    partyBannerUntil = 0;
    // the menu dims the whole screen, gauges and band included
    int gx, gy; needGaugePoint(1, 500, &gx, &gy);
    render(); uint16_t b0 = px(gx, gy), b1 = px(gx, gy + 1);
    menuOpen = true; render();   // the dimming is every other row
    ck(px(gx, gy) != b0 || px(gx, gy + 1) != b1, "the menu is drawn over the gauges");
    menuOpen = false;
    // the wild "!" on a day that has one
    uint32_t day = DAY0;
    for (; day < DAY0 + 400; day++) { at(day, 13); if (wildAlertNow()) break; }
    if (wildAlertNow()) {
      int16_t x0, y0, x1, y1;
      wildBadgeRect(&x0, &y0, &x1, &y1);
      int bx = (x0 + x1) / 2, by = (y0 + y1) / 2;
      render();
      uint16_t c = px(bx + 10, by);
      ck(c == UI_BAR_BAD || c == UI_BAR_WARN, "the wild '!' badge is drawn");
      // and it is clear of every gauge label and of the gauges themselves
      bool clear = true;
      for (int i = 0; i < 4; i++) {
        int lx0, ly0, lx1, ly1;
        needGaugeLabelRect(i, &lx0, &ly0, &lx1, &ly1);
        if (lx0 <= x1 && x0 <= lx1 && ly0 <= y1 && y0 <= ly1) clear = false;
        for (int p = 0; p <= 1000; p += 25) {
          int gx2, gy2; needGaugePoint(i, p, &gx2, &gy2);
          if (gx2 + 9 >= x0 && gx2 - 9 <= x1 && gy2 + 9 >= y0 && gy2 - 9 <= y1) clear = false;
        }
      }
      ck(clear, "...clear of the gauges and their labels");
    } else {
      ck(false, "found a day with a wild alert to draw");
    }
  }

  // ---------------- a ceremony still flushes over the new scene ----------------
  at(DAY0, 13);
  pet.ceremony = CER_FAREWELL;
  ck(flushes(), "a farewell ceremony flushes");
  pet.ceremony = CER_NONE;

  printf("%s\n", bad ? "FAILURES" : "all good");
  return bad ? 1 : 0;
}
