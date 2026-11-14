// The weather on the main screen (feature 5), through the REAL sketch: the
// overlay is drawn by render() for today's weather (wild.cpp's, the same one
// the WILD screen shows), it stays inside its primitive budget, it changes
// real pixels -- only on the visible panel, never under the bottom panel, never
// on the header text -- and it draws nothing with no clock, on the egg, or
// for a sunny night.
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "pet.h"
#include "wild.h"
#include "rtcbat.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <vector>
#include <chrono>
#include <thread>
uint32_t g_seed = 4242; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX = 0, g_touchY = 0; volatile bool g_touchDown = false;
void FakeESP::restart() { exit(0); }
int FakeSerial::available() { return 0; }
String FakeSerial::readStringUntil(char) { return String(""); }
void setup(); void loop(); void render();
uint8_t uiCurrentScreen();
extern const char *const SCREEN_NAME[];
extern Pet pet;
extern Arduino_Canvas *gfx;
extern bool gNight;
void drawScene(uint8_t biome, uint32_t now, bool night);
void weatherSceneOverlay();
bool weatherOverlayShown();
uint16_t weatherLastPrims();
uint8_t weatherLastDrawn();

// The budget this test holds the overlay to: a few hundred primitives at most
// was the brief; the overlay is designed to stay well under 120 a frame.
#define BUDGET 120

static int bad = 0;
static void ck(bool ok, const char *w) { printf("%s  %s\n", ok ? "PASS" : "FAIL", w); if (!ok) bad++; }
static void pump(int n) {
  for (int i = 0; i < n; i++) { std::this_thread::sleep_for(std::chrono::milliseconds(30)); loop(); }
}
static void click(int x, int y) {
  g_touchX = x; g_touchY = y; g_touchDown = true; emuFireInterrupt(); pump(5);
  g_touchDown = false; emuFireInterrupt(); pump(3);
}
static void setDay(uint32_t day, int hour) {
  rtcSetEpoch(day * 86400u + hour * 3600u + 60u);
  pet.lastSeenEpoch = rtcEpoch();
}
static uint32_t dayWith(uint8_t wx) {
  for (uint32_t d = 20400; d < 20400 + 400; d++) if (wildWeatherOf(d) == wx) return d;
  return 0;
}
static const char *WXN[WX_COUNT] = { "CLEAR", "SUNNY", "RAIN", "SNOW", "FOG", "WIND" };

// Draw the scene, snapshot it, draw the overlay on top, and count what changed:
// anywhere, outside the round panel, under the bottom panel, on the header text.
struct Diff { int any, offPanel, underPanel, onHeader; };
static Diff overlayDiff() {
  uint32_t now = millis();
  drawScene(DEX_TBL[pet.speciesId].biome, now, gNight);
  std::vector<uint16_t> before(gfx->buffer(), gfx->buffer() + 466 * 466);
  weatherSceneOverlay();
  const uint16_t *after = gfx->buffer();
  Diff d = { 0, 0, 0, 0 };
  for (int y = 0; y < 466; y++)
    for (int x = 0; x < 466; x++) {
      if (before[y * 466 + x] == after[y * 466 + x]) continue;
      d.any++;
      long dx = x - 233, dy = y - 233;
      if (dx * dx + dy * dy >= 233L * 233L) d.offPanel++;
      if (y >= 312) d.underPanel++;
      if (y >= 52 && y < 52 + 24 && x >= 233 - 120 && x <= 233 + 120) d.onHeader++;   // the name line
    }
  return d;
}

int main() {
  setup(); pump(4);
  click(233, 138); pump(2);            // region: KANTO
  click(233, 145);                     // starter row 0
  ck(!pet.awaitingStarter(), "starter picked");
  setDay(20400, 13);

  // ---- the egg keeps a clean scene, even on a day with weather
  {
    uint32_t d = dayWith(WX_RAIN);
    setDay(d, 13);
    ck(pet.isEgg(), "starts on the egg");
    render();
    ck(weatherLastPrims() == 0 && !weatherOverlayShown(), "no weather over the egg");
  }
  pet.dbgHatchAs(25, false);
  pet.ageMinutes = 30UL * 60;
  while (pet.hasLearnOffer()) pet.declineLearn();
  ck(!strcmp(SCREEN_NAME[uiCurrentScreen()], "main"), "on the main screen");

  // ---- every weather, by day: the right one is drawn, inside the budget
  uint16_t worst = 0;
  for (uint8_t w = 0; w < WX_COUNT; w++) {
    uint32_t d = dayWith(w);
    char b[120];
    snprintf(b, sizeof(b), "a %s day exists in the first 400 days", WXN[w]); ck(d != 0, b);
    setDay(d, 13);
    gfx->frameReady = false;
    render();
    snprintf(b, sizeof(b), "%s: the main screen still flushes", WXN[w]); ck(gfx->frameReady, b);
    uint16_t mx = 0, mn = 0xFFFF;
    for (int f = 0; f < 30; f++) {             // 30 frames across ~1.5 s of animation
      render();
      uint16_t p = weatherLastPrims();
      if (p > mx) mx = p;
      if (p < mn) mn = p;
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    if (mx > worst) worst = mx;
    if (getenv("WX_SHOT")) {   // optional: dump the frame for a look (raw RGB565)
      char fn[64]; snprintf(fn, sizeof(fn), "wx_%s.raw", WXN[w]);
      FILE *fp = fopen(fn, "wb"); if (fp) { fwrite(gfx->buffer(), 2, 466 * 466, fp); fclose(fp); }
    }
    if (w == WX_CLEAR) {
      snprintf(b, sizeof(b), "CLEAR draws nothing (max %u primitives)", mx); ck(mx == 0, b);
      ck(!weatherOverlayShown(), "CLEAR: the overlay is off");
    } else {
      snprintf(b, sizeof(b), "%s draws its weather (%u..%u primitives a frame)", WXN[w], mn, mx);
      ck(weatherLastDrawn() == w && mn > 0, b);
      snprintf(b, sizeof(b), "%s stays inside the budget (%u <= %d)", WXN[w], mx, BUDGET);
      ck(mx <= BUDGET, b);
      Diff df = overlayDiff();
      snprintf(b, sizeof(b), "%s changes real pixels (%d)", WXN[w], df.any); ck(df.any > 50, b);
      snprintf(b, sizeof(b), "%s: none off the round panel (%d) or under the bottom panel (%d)",
               WXN[w], df.offPanel, df.underPanel);
      ck(df.offPanel == 0 && df.underPanel == 0, b);
      if (w != WX_SUNNY) {   // the sun's rays are around the scene's own sun, up there
        snprintf(b, sizeof(b), "%s keeps off the name line (%d px)", WXN[w], df.onHeader);
        ck(df.onHeader == 0, b);
      }
    }
  }
  { char b[80]; snprintf(b, sizeof(b), "worst frame over every weather: %u primitives", worst); ck(worst <= BUDGET, b); }

  // ---- animated: two moments of rain are not the same picture
  {
    setDay(dayWith(WX_RAIN), 13);
    drawScene(DEX_TBL[pet.speciesId].biome, 1000, false);
    std::vector<uint16_t> sky(gfx->buffer(), gfx->buffer() + 466 * 466);
    weatherSceneOverlay();
    std::vector<uint16_t> a(gfx->buffer(), gfx->buffer() + 466 * 466);
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    drawScene(DEX_TBL[pet.speciesId].biome, 1000, false);   // same scene...
    weatherSceneOverlay();                                  // ...a later instant of rain
    int diff = 0;
    for (size_t i = 0; i < a.size(); i++) if (a[i] != gfx->buffer()[i]) diff++;
    char b[80]; snprintf(b, sizeof(b), "the rain moves between frames (%d px)", diff); ck(diff > 50, b);
  }

  // ---- night-aware: a sunny NIGHT draws no sun rays; the same day by day does
  {
    uint32_t d = dayWith(WX_SUNNY);
    setDay(d, 23); render();
    ck(gNight && weatherLastPrims() == 0, "a sunny night draws no rays");
    setDay(d, 13); render();
    ck(!gNight && weatherLastPrims() > 0, "...and the same sunny day does");
    uint32_t r = dayWith(WX_RAIN);
    setDay(r, 23); render();
    ck(gNight && weatherLastPrims() > 0, "rain still falls at night");
  }

  // ---- no clock: no weather (and the companion: a clock brings it back)
  {
    pet.lastSeenEpoch = 0;
    render();
    ck(weatherLastPrims() == 0 && weatherLastDrawn() == 0xFF && !weatherOverlayShown(),
       "no clock: nothing is drawn");
    setDay(dayWith(WX_SNOW), 13);
    render();
    ck(weatherLastPrims() > 0, "...and setting the clock brings the weather back");
  }

  // ---- the same weather the WILD screen reports
  {
    uint32_t d = dayWith(WX_FOG);
    setDay(d, 10); render();
    ck(weatherLastDrawn() == wildWeatherOf(pet.currentDay()), "draws wildWeatherOf(pet.currentDay())");
  }

  printf("%s\n", bad ? "FAILURES" : "the weather overlay works");
  return bad ? 1 : 0;
}
