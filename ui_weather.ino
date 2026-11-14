// ---------- The day's weather drawn over the pet's scene (feature 5) ----------
// Called from featSceneOverlay() (ui_hub.ino) right after drawScene(), before
// the header, the pet and the buttons. The weather itself is wild.cpp's
// (wildWeatherOf / wildTimeOf) -- one weather per day, the same one the WILD
// screen shows. This tab only DRAWS it. Design notes: docs/adventure-habitat.md.
//
// Budget: the main screen redraws the full frame at ~10 fps into a PSRAM
// canvas, so the overlay is a handful of cheap primitives (lines, small
// rects), never a per-pixel pass. Every primitive goes through wxPrim() so the
// test can count them; WX_PRIM_BUDGET is the ceiling weather_ui_test enforces.
// Everything is a pure function of millis(): the same instant draws the same
// frame, nothing is stored between frames.

#define WX_PRIM_BUDGET 120   // primitives a frame, at most (weather_ui_test)
#define WX_BOTTOM 312        // the bottom panel is painted over from here down
#define WX_R2 (232L * 232L)  // the round panel: nothing outside radius ~233 shows

static uint16_t gWxPrims = 0;       // primitives drawn by the last overlay
static uint8_t gWxDrawn = 0xFF;     // the weather it drew, 0xFF = none

// For the tests: what the last frame's overlay did.
uint16_t weatherLastPrims() { return gWxPrims; }
uint8_t weatherLastDrawn() { return gWxDrawn; }

static inline bool wxOnPanel(int x, int y) {
  long dx = x - 233, dy = y - 233;
  return y >= 0 && y < WX_BOTTOM && dx * dx + dy * dy < WX_R2;
}
// The header's two text lines (name at y 52, status at y 90): particles are
// left out of this box so the weather never sits on the words.
static inline bool wxOnHeader(int x, int y) {
  return y >= 46 && y <= 110 && x >= 233 - 160 && x <= 233 + 160;
}
static inline uint32_t wxHash(uint32_t x) {
  x ^= x >> 16; x *= 0x7FEB352Du;
  x ^= x >> 15; x *= 0x846CA68Bu;
  x ^= x >> 16;
  return x;
}
static inline void wxPrim() { gWxPrims++; }

// THE rule for whether the overlay draws at all. No clock = no weather (day 0
// is CLEAR by design, and inventing one from millis() would be a second
// clock); the egg and the farewell/runaway ceremony keep a clean scene, since
// the egg screen's text and region pill and the ceremony's own effects matter
// more than the sky.
bool weatherOverlayShown() {
  if (!pet.lastSeenEpoch) return false;
  if (pet.isEgg() || pet.ceremony) return false;
  return wildWeatherOf(pet.currentDay()) != WX_CLEAR;
}

// RAIN: slanted streaks falling fast, a few splashes on the ground.
static void wxRain(uint32_t now, bool night) {
  uint16_t col = night ? C565(0x6a, 0x7e, 0xa0) : C565(0xa8, 0xc0, 0xdc);
  for (uint32_t i = 0; i < 96; i++) {
    uint32_t h = wxHash(i + 1);
    int span = WX_BOTTOM + 40;
    int y = (int)((h % span + now * (14 + (h >> 8) % 6) / 40) % span) - 20;
    int x = (int)((h >> 12) % 520) - 30 - y / 4;
    int len = 10 + (int)((h >> 20) % 8);
    if (!wxOnPanel(x, y) || wxOnHeader(x, y)) continue;
    if (!wxOnPanel(x - 3, y + len) || wxOnHeader(x - 3, y + len)) continue;   // both ends
    gfx->drawLine(x, y, x - 3, y + len, col); wxPrim();
  }
  uint16_t sp = night ? C565(0x8a, 0x9e, 0xc0) : C565(0xe0, 0xec, 0xf6);
  for (uint32_t i = 0; i < 8; i++) {
    uint32_t h = wxHash(i + 97 + now / 150);   // a new splash every 150 ms
    int x = 90 + (int)(h % 290), y = HORIZON + 12 + (int)((h >> 9) % 60);
    gfx->drawFastHLine(x - 3, y, 7, sp); wxPrim();
  }
}

// SNOW: flakes drifting down, swaying, two sizes for depth.
static void wxSnow(uint32_t now, bool night) {
  uint16_t col = night ? C565(0xc8, 0xd2, 0xe6) : C565(0xff, 0xff, 0xff);
  static const int8_t SWAY[16] = { 0, 2, 4, 5, 6, 5, 4, 2, 0, -2, -4, -5, -6, -5, -4, -2 };
  for (uint32_t i = 0; i < 64; i++) {
    uint32_t h = wxHash(i + 501);
    int span = WX_BOTTOM + 10;
    int sp = 1 + (int)(h >> 8) % 3;
    int y = (int)((h % span + now * sp / 40) % span) - 5;
    int x = (int)((h >> 12) % 466) + SWAY[(now / 140 + i) & 15];
    if (!wxOnPanel(x, y) || wxOnHeader(x, y)) continue;
    int s = sp >= 3 ? 3 : 2;
    if (!wxOnPanel(x + s - 1, y + s - 1) || wxOnHeader(x + s - 1, y + s - 1)) continue;
    gfx->fillRect(x, y, s, s, col); wxPrim();
  }
}

// FOG: five drifting bands, each a few spaced lines so the scene shows through.
// Kept below the header so the name and status stay sharp.
static void wxFog(uint32_t now, bool night) {
  uint16_t col = night ? C565(0x4c, 0x56, 0x6c) : C565(0xe6, 0xea, 0xee);
  for (int b = 0; b < 5; b++) {
    int y0 = 124 + b * 38;
    int w = 300 + (b & 1) * 90;
    int x0 = (int)((now / (24 + b * 6) + b * 173) % (466 + w)) - w;
    if (b & 1) x0 = 466 - w - x0;   // alternate bands drift the other way
    for (int k = 0; k < 10; k++) {
      int y = y0 + k * 2;
      bool edge = (k < 2 || k > 7);   // thinner at the top and bottom of a band
      int x = x0 + ((k & 1) ? 12 : 0) + (edge ? 40 : 0), ww = w - (edge ? 80 : 0);
      // clip to the panel's chord at this height, so no line is wasted off it
      long dy = y - 233;
      long half = 0;
      while ((half + 8) * (half + 8) + dy * dy < WX_R2) half += 8;
      int lo = 233 - (int)half, hi = 233 + (int)half;
      if (x < lo) { ww -= lo - x; x = lo; }
      if (x + ww > hi) ww = hi - x;
      if (ww <= 0) continue;
      gfx->drawFastHLine(x, y, ww, col); wxPrim();
    }
  }
}

// WIND: fast horizontal streaks and a few tumbling leaves.
static void wxWind(uint32_t now, bool night) {
  uint16_t col = night ? C565(0x5a, 0x68, 0x88) : C565(0xf4, 0xf8, 0xff);
  for (uint32_t i = 0; i < 28; i++) {
    uint32_t h = wxHash(i + 901);
    int len = 26 + (int)(h % 44);
    int y = 30 + (int)((h >> 8) % (WX_BOTTOM - 40));
    int x = (int)(((h >> 4) % 700 + now * (18 + (h >> 20) % 10) / 40) % 700) - 120;
    if (!wxOnPanel(x, y) || !wxOnPanel(x + len, y) || wxOnHeader(x + len / 2, y)) continue;
    gfx->drawFastHLine(x, y, len, col); wxPrim();
  }
  static const int8_t BOB[8] = { 0, 3, 5, 3, 0, -3, -5, -3 };
  for (uint32_t i = 0; i < 6; i++) {
    uint32_t h = wxHash(i + 1301);
    int y = 130 + (int)((h >> 8) % 160) + BOB[(now / 90 + i) & 7];
    int x = (int)(((h >> 4) % 600 + now * (10 + i) / 40) % 600) - 60;
    if (!wxOnPanel(x, y)) continue;
    uint16_t lc = (i & 1) ? C565(0xb0, 0x7a, 0x3c) : C565(0x6a, 0xa8, 0x4c);
    if (night) lc = lerp565(lc, C565(0x10, 0x14, 0x24), 9, 16);
    bool flat = ((now / 120 + i) & 1);
    gfx->fillRect(x, y, flat ? 5 : 3, flat ? 3 : 5, lc); wxPrim();
  }
}

// SUNNY: slowly turning rays and a halo around the sun drawScene() already
// drew, plus a lens-flare trail. By day only: a sunny NIGHT is a clear sky.
static void wxSun(uint32_t now) {
  int h = sceneHour();
  // the scene's own sun: high until 18:00, then setting on the horizon
  int sx = h < 18 ? 360 : 233, sy = h < 18 ? 84 : HORIZON - 6, sr = h < 18 ? 26 : 34;
  uint16_t ray = C565(0xff, 0xe8, 0x9a), halo = C565(0xff, 0xf4, 0xc8);
  gfx->drawCircle(sx, sy, sr + 4, halo); wxPrim();
  gfx->drawCircle(sx, sy, sr + 5, halo); wxPrim();
  float a0 = (float)(now % 24000) / 24000.0f * 6.2831853f;
  for (int i = 0; i < 12; i++) {
    float a = a0 + i * 0.5235988f;
    float c = cosf(a), s = sinf(a);
    int r0 = sr + 9, r1 = sr + 22 + ((i & 1) ? 0 : 10) + (int)((now / 200 + i) % 4);
    int x0 = sx + (int)(c * r0), y0 = sy + (int)(s * r0);
    int x1 = sx + (int)(c * r1), y1 = sy + (int)(s * r1);
    if (!wxOnPanel(x1, y1) || y1 >= HORIZON) continue;
    gfx->drawLine(x0, y0, x1, y1, ray); wxPrim();
    gfx->drawLine(x0 + 1, y0, x1 + 1, y1, ray); wxPrim();
  }
  if (h < 18) {   // flare: three rings on the line from the sun to the centre
    static const uint8_t FR[3] = { 9, 5, 13 };
    static const uint8_t FT[3] = { 40, 62, 88 };   // percent of the way
    for (int k = 0; k < 3; k++) {
      int fx = sx + (233 - sx) * FT[k] / 100, fy = sy + (200 - sy) * FT[k] / 100;
      gfx->drawCircle(fx, fy, FR[k] + (int)((now / 300 + k) % 2), halo); wxPrim();
    }
  }
}

void weatherSceneOverlay() {
  gWxPrims = 0;
  gWxDrawn = 0xFF;
  if (!weatherOverlayShown()) return;
  uint8_t wx = wildWeatherOf(pet.currentDay());
  uint32_t now = millis();
  switch (wx) {
    case WX_RAIN: wxRain(now, gNight); break;
    case WX_SNOW: wxSnow(now, gNight); break;
    case WX_FOG: wxFog(now, gNight); break;
    case WX_WIND: wxWind(now, gNight); break;
    case WX_SUNNY: if (!gNight) wxSun(now); break;
    default: break;   // CLEAR: the scene's own clouds are the weather
  }
  gWxDrawn = wx;
}
