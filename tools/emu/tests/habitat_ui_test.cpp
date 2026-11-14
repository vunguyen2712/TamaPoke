// The Pokedex habitat view (feature 8), through the REAL sketch. The summary
// is asked of habitat.h, which asks wild.cpp; this checks the answers against
// the wild rules themselves (wildEligible / wildTimeAllows / wildPredecessor),
// then drives the screen: the HABITAT pill on the grid, the missing list read
// back by TAPPING every row (so it is the screen's list, not a copy of it),
// paging, back-out, region change, a locked region, the detail page.
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "pet.h"
#include "wild.h"
#include "habitat.h"
#include "rtcbat.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <vector>
#include <chrono>
#include <thread>
uint32_t g_seed = 909; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX = 0, g_touchY = 0; volatile bool g_touchDown = false;
void FakeESP::restart() { exit(0); }
int FakeSerial::available() { return 0; }
String FakeSerial::readStringUntil(char) { return String(""); }
void setup(); void loop(); void render(); void onTap(int16_t x, int16_t y);
void onSwipe(int dir); void onSwipeV(int dir);
uint8_t uiCurrentScreen();
extern const char *const SCREEN_NAME[];
extern Pet pet;
extern Arduino_Canvas *gfx;
extern bool galleryOpen, galleryPick, galleryDirty;
extern int galleryPage; extern int16_t galleryDetail; extern uint8_t galleryRegion;
extern bool habitatOpen; extern uint8_t habitatPage;
void habitatSummary(int16_t dex, char *out, size_t n);
uint8_t habitatMinLevel(int16_t dex);
uint8_t habitatPageCount();

// screen geometry (ui_habitat.ino): rows at 74 + 62 i, 56 tall; BACK at 392;
// the HABITAT pill on the grid at x 200..328, y 410..442
#define ROW_TAP(i) (74 + (i) * 62 + 28)
#define BTN_X 264
#define BTN_Y 426

static int bad = 0;
static void ck(bool ok, const char *w) { printf("%s  %s\n", ok ? "PASS" : "FAIL", w); if (!ok) bad++; }
static void pump(int n) {
  for (int i = 0; i < n; i++) { std::this_thread::sleep_for(std::chrono::milliseconds(30)); loop(); }
}
static void click(int x, int y) {
  g_touchX = x; g_touchY = y; g_touchDown = true; emuFireInterrupt(); pump(5);
  g_touchDown = false; emuFireInterrupt(); pump(3);
}
static bool on(const char *s) { return !strcmp(SCREEN_NAME[uiCurrentScreen()], s); }
static int shots = 0;
static bool flushes() {
  gfx->frameReady = false; render();
  if (getenv("HB_SHOT")) {   // optional: dump each checked frame for a look (raw RGB565)
    char fn[32]; snprintf(fn, sizeof(fn), "hb_%02d.raw", shots++);
    FILE *fp = fopen(fn, "wb"); if (fp) { fwrite(gfx->buffer(), 2, 466 * 466, fp); fclose(fp); }
  }
  return gfx->frameReady;
}
static void toGrid(uint8_t region) {
  galleryOpen = true; galleryPick = false; galleryDetail = 0; galleryPage = 0;
  galleryRegion = region; galleryDirty = true; habitatOpen = false;
}

int main() {
  setup(); pump(4);
  click(233, 138); pump(2);            // region: KANTO
  click(233, 145);                     // starter row 0
  ck(!pet.awaitingStarter(), "starter picked");
  pet.dbgHatchAs(25, false);
  while (pet.hasLearnOffer()) pet.declineLearn();
  gRegionArt = 0xFFFF;
  HabTotals tot;

  // ================= the summary, against the wild rules =================
  {
    // a GHOST: night or fog only (wildTimeAllows), best in one of those
    HabInfo h = habitatOf(92, tot);    // GASTLY
    ck(h.how == HB_WILD, "GASTLY can be met in the wild");
    ck(h.restricted && h.onlyTod == (1u << WT_NIGHT) && h.onlyWx == (1u << WX_FOG),
       "GASTLY is flagged NIGHT / FOG only");
    ck(h.bestWx == WX_FOG || h.bestTod == WT_NIGHT, "GASTLY is best in fog or at night");
    char s[48]; habitatSummary(92, s, sizeof(s));
    ck(!strcmp(s, "NIGHT/FOG ONLY"), "the GASTLY row reads NIGHT/FOG ONLY");
    printf("      GASTLY: %s\n", s);
    // and the flag really is the rule: ask wildTimeAllows the other way round
    ck(!wildTimeAllows(92, WX_CLEAR, WT_DAY) && wildTimeAllows(92, WX_FOG, WT_DAY) &&
       wildTimeAllows(92, WX_CLEAR, WT_NIGHT), "(wildTimeAllows agrees)");
    // a non-ghost is not flagged
    HabInfo p = habitatOf(16, tot);    // PIDGEY
    ck(p.how == HB_WILD && !p.restricted && p.minLevel == 1, "PIDGEY: any time, any level");
  }
  {
    // the legendaries: an egg, never the wild
    HabInfo h = habitatOf(150, tot);   // MEWTWO
    char s[48]; habitatSummary(150, s, sizeof(s));
    ck(h.how == HB_EGG && !strcmp(s, "EGG ONLY"), "MEWTWO says EGG ONLY");
    int legs = 0, wrong = 0;
    for (int16_t d = 1; d <= DEX_COUNT; d++)
      if (DEX_TBL[d].rarity == R_LEGENDARIO && speciesHasArt(d)) {
        legs++;
        if (habitatOf(d, tot).how != HB_EGG) wrong++;
      }
    char b[80]; snprintf(b, sizeof(b), "all %d legendaries say EGG (%d wrong)", legs, wrong);
    ck(legs > 0 && !wrong, b);
  }
  {
    // an evolved form: from its predecessor's evolveLevel, and not one below
    int16_t prev = wildPredecessor(2);
    uint8_t need = DEX_TBL[prev].evolveLevel;
    HabInfo h = habitatOf(2, tot);     // IVYSAUR
    char b[96]; snprintf(b, sizeof(b), "IVYSAUR shows its minimum wild level %u (BULBASAUR evolves at %u)",
                         habitatMinLevel(2), need);
    ck(h.how == HB_WILD && h.minLevel == need && habitatMinLevel(2) == need && need > 1, b);
    ck(!wildEligible(2, 0, need - 1) && wildEligible(2, 0, need), "(wildEligible agrees at the edge)");
    // EVERY wild evolved form, across the dex: its level is wildPredecessor's
    int checked = 0, wrongLv = 0;
    for (int16_t d = 1; d <= DEX_COUNT; d++) {
      if (DEX_TBL[d].rarity != R_EVO) continue;
      HabInfo e = habitatOf(d, tot);
      if (e.how != HB_WILD) continue;
      checked++;
      if (e.minLevel != DEX_TBL[wildPredecessor(d)].evolveLevel) wrongLv++;
    }
    snprintf(b, sizeof(b), "%d wild evolved forms all show the predecessor's level (%d wrong)", checked, wrongLv);
    ck(checked > 20 && !wrongLv, b);
    // and evolution-only species that never qualify say EVOLVE
    int evo = 0, wrongE = 0;
    for (int16_t d = 1; d <= DEX_COUNT; d++) {
      if (DEX_TBL[d].rarity != R_EVO || !speciesHasArt(d)) continue;
      if (wildEligible(d, regionOfDex(d), MAX_LEVEL)) continue;
      evo++;
      char s[48]; habitatSummary(d, s, sizeof(s));
      if (habitatOf(d, tot).how != HB_EVOLVE || strcmp(s, "EVOLVE ONLY")) wrongE++;
    }
    snprintf(b, sizeof(b), "%d never-wild evolutions say EVOLVE ONLY (%d wrong)", evo, wrongE);
    ck(evo > 0 && !wrongE, b);
  }
  {
    // the habitat's verdict and wildEligible never disagree, anywhere
    int wild = 0, wrong = 0;
    for (int16_t d = 1; d <= DEX_COUNT; d++) {
      bool w = habitatOf(d, tot).how == HB_WILD;
      if (w) wild++;
      if (w != wildEligible(d, regionOfDex(d), MAX_LEVEL)) wrong++;
    }
    char b[80]; snprintf(b, sizeof(b), "HB_WILD == wildEligible for every species (%d wild, %d wrong)", wild, wrong);
    ck(wild > 100 && !wrong, b);
  }
  {
    // best conditions follow the type: water in the rain, fire in the sun
    ck(habitatOf(7, tot).bestWx == WX_RAIN, "SQUIRTLE is best in the RAIN");
    ck(habitatOf(4, tot).bestWx == WX_SUNNY, "CHARMANDER is best in the SUN");
    char s[48]; habitatSummary(7, s, sizeof(s)); printf("      SQUIRTLE: %s\n", s);
  }
  {
    // "most likely" is the species' SHARE of each condition, not its raw
    // weight: somewhere the two must disagree, or the totals do nothing --
    // and where they do, the habitat's pick really has the larger share.
    int differ = 0, wrongPick = 0;
    for (uint8_t rg = 0; rg < REGION_COUNT; rg++) {
      if (rg == REGION_ALL) continue;
      HabTotals t; habitatTotals(rg, t);
      for (int16_t d = REGIONS[rg].lo; d <= REGIONS[rg].hi && d <= DEX_COUNT; d++) {
        HabInfo h = habitatOf(d, t);
        if (h.how != HB_WILD) continue;
        uint32_t raw[WX_COUNT] = { 0 }; double sh[WX_COUNT] = { 0 };
        for (uint8_t w = 0; w < WX_COUNT; w++)
          for (uint8_t tt = 0; tt < WT_COUNT; tt++) {
            raw[w] += wildWeight(d, w, tt);
            if (t.t[w][tt]) sh[w] += (double)wildWeight(d, w, tt) / t.t[w][tt];
          }
        uint8_t rb = 0;
        for (uint8_t w = 1; w < WX_COUNT; w++) if (raw[w] > raw[rb]) rb = w;
        if (rb != h.bestWx) {
          differ++;
          if (sh[h.bestWx] < sh[rb]) wrongPick++;
        }
      }
    }
    char b[96]; snprintf(b, sizeof(b), "share and raw weight disagree for %d species, the share wins every time (%d wrong)", differ, wrongPick);
    ck(differ > 0 && !wrongPick, b);
  }
  {
    // no art, or no pack: NOT ON THIS CARD
    char s[48]; habitatSummary(NO_ART[0], s, sizeof(s));
    ck(habitatOf(NO_ART[0], tot).how == HB_NOCARD && !strcmp(s, "NOT ON THIS CARD"), "a species with no art says NOT ON THIS CARD");
    gRegionArt = 0x1;                      // KANTO only
    ck(habitatOf(152, tot).how == HB_NOCARD, "a species whose pack is missing says NOT ON THIS CARD");
    gRegionArt = 0xFFFF;
    ck(habitatOf(152, tot).how == HB_WILD, "...and with the pack it is wild again");
  }

  // ================= the screen =================
  for (int16_t d = 1; d <= 30; d++) pet.registerCaught(d, false);   // part of KANTO is known
  pet.registerCaught(92, false);
  uint16_t want = 0;
  for (int16_t d = REGIONS[0].lo; d <= REGIONS[0].hi; d++) if (!pet.isRegistered(d)) want++;

  // ---- the pill on the grid
  toGrid(0);
  ck(on("gallery"), "on the Pokedex grid");
  ck(flushes(), "the grid (with its HABITAT pill) flushes");
  onTap(BTN_X, BTN_Y);
  ck(on("habitat") && galleryOpen, "the HABITAT pill opens the habitat list");
  ck(flushes(), "the habitat list flushes");

  // ---- read the whole list back by tapping every row on every page
  {
    std::vector<int16_t> seen;
    uint8_t pages = habitatPageCount();
    bool ok = true;
    for (uint8_t pg = 0; pg < pages; pg++) {
      if (habitatPage != pg || !on("habitat")) { ok = false; break; }
      for (int r = 0; r < 5; r++) {
        galleryDetail = 0;
        onTap(233, ROW_TAP(r));
        if (!galleryDetail) continue;           // past the end of the list
        seen.push_back(galleryDetail);
        if (!on("gallery")) ok = false;         // the species' detail page
        if (!flushes()) ok = false;             // ...with its habitat line, flushing
        onSwipe(1);                             // back out of the detail...
        if (!on("habitat") || habitatPage != pg) ok = false;   // ...lands on the same page
      }
      if (pg + 1 < pages) onSwipe(-1);          // next page
    }
    ck(ok, "every row opens its detail page and backs out to the same page");
    bool onlyMissing = true, inRegion = true, sorted = true;
    for (size_t i = 0; i < seen.size(); i++) {
      if (pet.isRegistered(seen[i])) onlyMissing = false;
      if (seen[i] < REGIONS[0].lo || seen[i] > REGIONS[0].hi) inRegion = false;
      if (i && seen[i] <= seen[i - 1]) sorted = false;
    }
    char b[96];
    snprintf(b, sizeof(b), "the list holds every missing KANTO species: %u of %u", (unsigned)seen.size(), want);
    ck(seen.size() == want, b);
    ck(onlyMissing, "...and none the player already has");
    ck(inRegion && sorted, "...all from KANTO, in dex order, none twice");
    ck(pages == (want + 4) / 5 && pages > 1, "five to a page");
    // past the last page: back to the grid, never closing the Pokedex
    onSwipe(-1);
    ck(on("gallery") && galleryOpen && !habitatOpen, "paging past the end backs out to the grid");
  }

  // ---- paging back off the front, and the BACK pill
  onTap(BTN_X, BTN_Y);
  onSwipe(-1);
  ck(on("habitat") && habitatPage == 1, "a swipe left pages forward (and stays open)");
  onSwipe(1);
  ck(on("habitat") && habitatPage == 0, "a swipe right pages back");
  onSwipe(1);
  ck(on("gallery") && galleryOpen, "paging back off the front returns to the grid");
  onTap(BTN_X, BTN_Y);
  onTap(233, 412);
  ck(on("gallery") && galleryOpen, "BACK returns to the grid");
  ck(galleryDirty, "...and the grid is redrawn");

  // ---- vertical: another region, still the habitat list
  onTap(BTN_X, BTN_Y);
  onSwipe(-1);
  onSwipeV(1);
  ck(on("habitat") && galleryRegion == 1 && habitatPage == 0, "a vertical swipe moves to JOHTO's list, page 1");
  {
    galleryDetail = 0;
    onTap(233, ROW_TAP(0));
    ck(galleryDetail == REGIONS[1].lo, "JOHTO's first row is its first species (none registered)");
    onSwipe(1);
  }

  // ---- a region without its pack: shown LOCKED, not hidden, nothing to tap
  {
    gRegionArt = 0x1;
    ck(on("habitat") && flushes(), "a locked region still shows its habitat page (and flushes)");
    ck(habitatPageCount() == 1, "...one page, saying the pack is missing");
    galleryDetail = 0;
    onTap(233, ROW_TAP(0));
    ck(galleryDetail == 0 && on("habitat"), "...and its rows cannot be opened");
    gRegionArt = 0xFFFF;
    onTap(233, ROW_TAP(0));
    ck(galleryDetail == REGIONS[1].lo, "(with the pack the same tap does open one)");
    onSwipe(1);
  }

  // ---- leaving the Pokedex from here never re-opens on the habitat list
  {
    galleryOpen = false; render();
    ck(on("main"), "closing the Pokedex goes back to the pet");
    galleryOpen = true; galleryPick = true; render();
    ck(on("dexpick"), "re-opening it lands on the region chooser");
    galleryPick = false; galleryRegion = 0; galleryDirty = true;
    ck(on("gallery") && !habitatOpen, "...then the grid, not a stale habitat page");
  }

  // ---- a complete region says so, still with its page
  {
    for (int16_t d = REGIONS[0].lo; d <= REGIONS[0].hi; d++) pet.registerCaught(d, false);
    toGrid(0);
    onTap(BTN_X, BTN_Y);
    ck(on("habitat") && habitatPageCount() == 1 && flushes(), "a complete region shows one page and flushes");
  }

  // ---- the detail page of a REGISTERED species carries its habitat line too
  {
    toGrid(0);
    galleryDetail = 92;
    ck(flushes(), "the detail page with its habitat lines flushes");
  }

  printf("%s\n", bad ? "FAILURES" : "the habitat view works");
  return bad ? 1 : 0;
}
