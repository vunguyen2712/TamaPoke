// ---------- The home screen: its scene and its HUD (v3.18) ----------
//
// render() (TamaPoke.ino) still decides WHAT is on the pet's screen and in which
// order; this tab only draws the two layers that frame it:
//
//   drawScene()       the illustrated background: a layered sky with drifting
//                     clouds, a rayed sun / moon and twinkling stars, a distant
//                     layer and a textured ground per biome, a little ambient
//                     life (a gull, a sailboat, smoke, fireflies, snow).
//   the HUD           battery + clock, the name on a cloud label, the status in
//                     a speech bubble, the four needs as arc gauges hugging the
//                     bezel, a bond/streak line, and the pastel action band with
//                     its four labelled buttons.
//
// Budget: the main screen redraws the whole 466x466 canvas every frame (100 ms),
// so everything here is a few hundred fillRect/hline-sized primitives and pure
// functions of millis() -- nothing is stored between frames, no bitmaps.
// Layout numbers the tap path shares (buttons[], BTN_*, HUD_BAND_Y) live in
// TamaPoke.ino; main_ui_test holds them to what is drawn.

// ---- strings (six languages, ES EN FR DE IT PT, ASCII only) ----
// Short: at 12 px a character and 72 px between buttons, two neighbours may
// hold 11 characters between them. main_ui_test checks every language.
static const char *const S_HM_FOOD[LANG_COUNT]  = { "COMER", "FOOD", "MANGE", "ESSEN", "CIBO", "COMER" };
static const char *const S_HM_SLEEP[LANG_COUNT] = { "SUENO", "SLEEP", "DODO", "RUHE", "NANNA", "SONO" };
static const char *const S_HM_WAKE[LANG_COUNT]  = { "LUZ", "WAKE", "REVEIL", "WACH", "LUCE", "ACORDA" };
static const char *const S_HM_BATH[LANG_COUNT]  = { "BANO", "BATH", "BAIN", "BAD", "BAGNO", "BANHO" };
static const char *const S_HM_TRAIN[LANG_COUNT] = { "PESAS", "TRAIN", "SPORT", "SPORT", "SPORT", "FORCA" };

const char *homeButtonLabel(int i) {
  switch (i) {
    case BTN_FOOD:  return TL(S_HM_FOOD);
    case BTN_LIGHT: return pet.sleeping ? TL(S_HM_WAKE) : TL(S_HM_SLEEP);
    case BTN_BATH:  return TL(S_HM_BATH);
    default:        return TL(S_HM_TRAIN);
  }
}

// ---- small helpers ----
static inline uint32_t hmHash(uint32_t x) {
  x ^= x >> 16; x *= 0x7FEB352Du;
  x ^= x >> 15; x *= 0x846CA68Bu;
  x ^= x >> 16;
  return x;
}

// time of day for the scene: 0 day, 1 dawn, 2 dusk, 3 night
static uint8_t hmPhase = 0;
// Every scenery colour goes through this, so one palette serves all four times
// of day: night pulls it toward deep blue, dawn toward rose, dusk toward amber.
static uint16_t hmT(uint16_t c) {
  switch (hmPhase) {
    case 1: return lerp565(c, C565(0xf2, 0x9a, 0xa8), 4, 16);
    case 2: return lerp565(c, C565(0xf0, 0x86, 0x52), 5, 16);
    case 3: return lerp565(c, C565(0x12, 0x18, 0x32), 11, 16);
    default: return c;
  }
}

static void hmHLine(int x0, int x1, int y, uint16_t c) {
  if (x1 < x0) { int t = x0; x0 = x1; x1 = t; }
  if (y < 0 || y >= 466) return;
  if (x0 < 0) x0 = 0;
  if (x1 > 465) x1 = 465;
  if (x1 >= x0) gfx->fillRect(x0, y, x1 - x0 + 1, 1, c);
}

// The top of a circle of radius r whose highest point is at (x, top), clipped
// at ymax. The hills are huge circles; drawing only what shows above the ground
// keeps a 120 px hill to ~40 rows instead of 240.
static void hmCap(int x, int top, int r, int ymax, uint16_t c) {
  int cy = top + r;
  for (int y = top; y <= ymax && y <= cy + r; y++) {
    int dy = y - cy;
    int w = (int)sqrtf((float)(r * r - dy * dy));
    hmHLine(x - w, x + w, y, c);
  }
}

static void hmEllipse(int cx, int cy, int rx, int ry, uint16_t c) {
  for (int dy = -ry; dy <= ry; dy++) {
    int w = (int)(rx * sqrtf(1.0f - (float)(dy * dy) / (float)(ry * ry)));
    hmHLine(cx - w, cx + w, cy + dy, c);
  }
}

// A puffy cloud: a flat base and three domes, with a shaded underside.
static void hmCloud(int cx, int cy, int w, uint16_t col, uint16_t shade) {
  for (int pass = 0; pass < 2; pass++) {
    uint16_t c = pass ? col : shade;
    int o = pass ? 0 : 4;
    gfx->fillRoundRect(cx - w / 2, cy + o, w, w / 4, w / 8, c);
    gfx->fillCircle(cx - w / 4, cy + 3 + o, w / 5, c);
    gfx->fillCircle(cx + w / 20, cy - w / 12 + o, w / 4, c);
    gfx->fillCircle(cx + w / 4 + 2, cy + 4 + o, w / 6, c);
  }
}

// A conifer: two stacked triangles (and a snow cap if asked).
static void hmPine(int x, int base, int h, uint16_t c, uint16_t snow) {
  int w = h * 2 / 5;
  gfx->fillTriangle(x, base - h, x - w, base - h / 3, x + w, base - h / 3, c);
  gfx->fillTriangle(x, base - h * 2 / 3, x - w - w / 3, base, x + w + w / 3, base, c);
  if (snow) {
    gfx->fillTriangle(x, base - h, x - w / 3, base - h + h / 5, x + w / 3, base - h + h / 5, snow);
    hmHLine(x - w - w / 3 + 3, x + w + w / 3 - 3, base - 1, snow);
  }
}

// A gull/eagle: two wing strokes, flapping.
static void hmBird(int x, int y, bool up, uint16_t c) {
  int wy = up ? -5 : 2;
  for (int t = 0; t < 2; t++) {
    gfx->drawLine(x - 9, y + wy + t, x, y + t, c);
    gfx->drawLine(x, y + t, x + 9, y + wy + t, c);
  }
}

// Fireflies at night over grass: slow drifting dots that blink.
static void hmFireflies(uint32_t now, int n) {
  for (int k = 0; k < n; k++) {
    uint32_t h = hmHash(k * 31 + 7);
    if (((now / 220) + h) % 7 < 2) continue;
    int x = 40 + (int)(h % 386) + (int)(10 * sinf(now * 0.0011f + k));
    int y = HORIZON + 6 + (int)((h >> 9) % 80) + (int)(6 * sinf(now * 0.0017f + k * 2));
    gfx->fillRect(x, y, 3, 3, C565(0xf6, 0xf0, 0x7a));
    gfx->fillRect(x - 1, y + 1, 5, 1, C565(0xa8, 0xc0, 0x50));
  }
}

// Ground texture: deterministic specks, lighter and darker than the soil.
static void hmSpecks(uint16_t soil, int n, uint32_t seed) {
  uint16_t dk = lerp565(soil, C565(0x20, 0x20, 0x20), 4, 16);
  uint16_t lt = lerp565(soil, C565(0xff, 0xff, 0xff), 4, 16);
  for (int k = 0; k < n; k++) {
    uint32_t h = hmHash(k * 97 + seed);
    int x = 20 + (int)(h % 426), y = HORIZON + 10 + (int)((h >> 10) % (HUD_BAND_Y - HORIZON - 12));
    gfx->fillRect(x, y, 2 + (h >> 20) % 3, 2, (h >> 24) & 1 ? dk : lt);
  }
}

// ---------- sky ----------
static uint16_t hmSkyTop, hmSkyBot;
static uint16_t hmSkyAt(int y) { return lerp565(hmSkyTop, hmSkyBot, y, HORIZON); }

static void hmSun(uint32_t now) {
  const int sx = 360, sy = 84;    // ui_weather.ino's SUNNY rays are centred here too
  uint16_t sky = hmSkyAt(sy);
  uint16_t core = hmPhase == 1 ? C565(0xff, 0xc4, 0x7e) : C565(0xff, 0xe2, 0x5c);
  uint16_t ray = hmPhase == 1 ? C565(0xff, 0xd2, 0x9a) : C565(0xff, 0xec, 0x8a);
  gfx->fillCircle(sx, sy, 36, lerp565(sky, core, 4, 16));
  gfx->fillCircle(sx, sy, 30, lerp565(sky, core, 8, 16));
  float rot = (now % 24000) * (6.2832f / 24000.0f);
  for (int k = 0; k < 12; k++) {
    float a = rot + k * 0.5236f, w = 0.075f;
    int r0 = 33, r1 = (k & 1) ? 44 : 50;
    gfx->fillTriangle(sx + (int)(r0 * cosf(a - w)), sy + (int)(r0 * sinf(a - w)),
                      sx + (int)(r0 * cosf(a + w)), sy + (int)(r0 * sinf(a + w)),
                      sx + (int)(r1 * cosf(a)), sy + (int)(r1 * sinf(a)), ray);
  }
  gfx->fillCircle(sx, sy, 23, core);
  gfx->fillCircle(sx - 7, sy - 7, 8, lerp565(core, UI_WHITE, 9, 16));
}

// The evening sun sits low behind the far layer, cut by retro stripes.
// Where it sits (ui_weather.ino's SUNNY halo follows it): left of the pet, so
// the creature never hides it, low enough to sink behind the far layer.
#define DUSK_SUN_X 128
#define DUSK_SUN_Y (HORIZON - 52)
#define DUSK_SUN_R 34
static void hmDuskSun() {
  const int sx = DUSK_SUN_X, sy = DUSK_SUN_Y, r = DUSK_SUN_R;
  uint16_t core = C565(0xff, 0xd8, 0x8a);
  gfx->fillCircle(sx, sy, r + 8, lerp565(hmSkyAt(sy), core, 5, 16));
  gfx->fillCircle(sx, sy, r, core);
  for (int k = 0; k < 4; k++) {
    int y = sy + 4 + k * 8;
    hmHLine(sx - r, sx + r, y, hmSkyAt(y));
    if (k > 1) hmHLine(sx - r, sx + r, y + 1, hmSkyAt(y));
  }
}

static void hmMoonStars(uint32_t now) {
  for (int k = 0; k < 30; k++) {
    uint32_t h = hmHash(k * 13 + 3);
    int x = 30 + (int)(h % 406), y = 14 + (int)((h >> 10) % (HORIZON - 70));
    int ph = (int)((now / 260 + (h >> 20)) % 12);
    uint16_t c = ph < 2 ? C565(0x80, 0x88, 0xb0) : C565(0xf4, 0xf2, 0xe0);
    if (k % 6 == 0 && ph >= 2) {   // a few bigger four-point stars
      gfx->fillRect(x - 3, y, 7, 1, c);
      gfx->fillRect(x, y - 3, 1, 7, c);
      gfx->fillRect(x - 1, y - 1, 3, 3, c);
    } else {
      gfx->fillRect(x, y, 2, 2, c);
    }
  }
  const int mx = 360, my = 78;
  uint16_t glow = lerp565(hmSkyAt(my), C565(0xd8, 0xe0, 0xff), 3, 16);
  gfx->fillCircle(mx, my, 32, glow);
  gfx->fillCircle(mx, my, 22, C565(0xf6, 0xf0, 0xd6));
  gfx->fillCircle(mx - 6, my + 7, 4, C565(0xe0, 0xd8, 0xb8));   // a crater
  gfx->fillCircle(mx + 10, my - 6, 20, glow);                    // the crescent's bite
}

static void hmClouds(uint32_t now) {
  uint16_t col, shade;
  switch (hmPhase) {
    case 1: col = C565(0xff, 0xee, 0xf0); shade = C565(0xf0, 0xc4, 0xcc); break;
    case 2: col = C565(0xff, 0xe4, 0xc8); shade = C565(0xe8, 0xa8, 0x8c); break;
    case 3: col = C565(0x3a, 0x44, 0x6c); shade = C565(0x2a, 0x32, 0x56); break;
    default: col = UI_WHITE; shade = C565(0xd2, 0xe2, 0xf2); break;
  }
  // far: small and slow; near: big and twice as fast. Both wrap off the edges.
  for (int k = 0; k < 3; k++) {
    int x = (int)((now / 140 + k * 190) % 640) - 90;
    hmCloud(x, 136 + (k % 2) * 16, 54, lerp565(col, hmSkyAt(140), 5, 16), lerp565(shade, hmSkyAt(140), 5, 16));
  }
  if (hmPhase == 3) return;
  for (int k = 0; k < 2; k++) {
    int x = (int)((now / 70 + k * 330) % 700) - 120;
    hmCloud(x, 112 + k * 18, 96, col, shade);
  }
}

// ---------- the six biomes: a far layer, then the ground's details ----------
// 0 meadow, 1 beach, 2 forest, 3 volcano, 4 mountain, 5 snow (DexEntry::biome)

static void hmMeadowFar() {
  static const int16_t FAR[][3] = { { 40, 184, 90 }, { 175, 196, 110 }, { 300, 180, 100 }, { 440, 190, 90 } };
  static const int16_t NEAR[][3] = { { -10, 206, 120 }, { 233, 214, 170 }, { 476, 202, 120 } };
  uint16_t far = hmT(C565(0xa6, 0xd2, 0xb4)), near = hmT(C565(0x8c, 0xc8, 0x88));
  for (auto &h : FAR) hmCap(h[0], h[1], h[2], HORIZON, far);
  uint16_t tree = hmT(C565(0x6a, 0xa8, 0x7a)), trunk = hmT(C565(0x7a, 0x5a, 0x48));
  static const int16_t TREES[][2] = { { 118, 200 }, { 136, 204 }, { 318, 186 }, { 336, 190 }, { 420, 194 } };
  for (auto &t : TREES) {
    gfx->fillRect(t[0] - 1, t[1], 3, 7, trunk);
    gfx->fillCircle(t[0], t[1] - 3, 7, tree);
  }
  for (auto &h : NEAR) hmCap(h[0], h[1], h[2], HORIZON, near);
}

static void hmMeadowGround(uint16_t soil, uint32_t now) {
  uint16_t blade = hmT(C565(0x4e, 0x96, 0x58));
  static const uint16_t PETAL[4] = { C565(0xf0, 0x5a, 0x5a), UI_WHITE, C565(0xff, 0xd8, 0x4a), C565(0xf4, 0x9a, 0xc8) };
  for (int k = 0; k < 16; k++) {
    uint32_t h = hmHash(k * 41 + 11);
    int x = 30 + (int)(h % 406), y = HORIZON + 14 + (int)((h >> 9) % (HUD_BAND_Y - HORIZON - 24));
    gfx->fillRect(x, y + 2, 1, 4, blade);
    uint16_t pc = hmT(PETAL[(h >> 20) & 3]);
    gfx->fillRect(x - 2, y - 1, 5, 3, pc);
    gfx->fillRect(x - 1, y - 2, 3, 5, pc);
    gfx->fillRect(x, y, 1, 1, hmT(C565(0xff, 0xc8, 0x30)));
  }
  for (int k = 0; k < 12; k++) {
    uint32_t h = hmHash(k * 59 + 5);
    int x = 24 + (int)(h % 418), y = HORIZON + 10 + (int)((h >> 9) % (HUD_BAND_Y - HORIZON - 16));
    int sway = (int)(2 * sinf(now * 0.002f + k));
    gfx->drawLine(x, y + 8, x - 3 + sway, y, blade);
    gfx->drawLine(x + 2, y + 8, x + 2 + sway, y - 2, blade);
    gfx->drawLine(x + 4, y + 8, x + 7 + sway, y + 1, blade);
  }
  (void)soil;
}

static void hmBeachFar(uint32_t now) {
  const int seaTop = 176;
  uint16_t farSea = hmT(C565(0x7a, 0xc2, 0xe8)), nearSea = hmT(C565(0x2e, 0x86, 0xc8));
  for (int y = seaTop; y < HORIZON + 8; y += 4)
    gfx->fillRect(0, y, 466, 4, lerp565(farSea, nearSea, y - seaTop, HORIZON + 8 - seaTop));
  hmHLine(0, 465, seaTop, hmT(C565(0xc8, 0xec, 0xfa)));
  // an island with a palm, far right
  hmCap(404, 160, 46, seaTop, hmT(C565(0x5a, 0xa4, 0x6e)));
  hmHLine(366, 442, seaTop - 1, hmT(C565(0xe8, 0xd8, 0xa4)));
  uint16_t trunk = hmT(C565(0x8a, 0x64, 0x44)), frond = hmT(C565(0x3a, 0x8a, 0x52));
  gfx->drawLine(400, 162, 405, 142, trunk);
  gfx->drawLine(401, 162, 406, 142, trunk);
  gfx->fillTriangle(405, 142, 388, 148, 396, 140, frond);
  gfx->fillTriangle(405, 142, 422, 149, 414, 139, frond);
  gfx->fillTriangle(405, 142, 398, 134, 412, 134, frond);
  // a sailboat drifting along the horizon, right to left and round again
  int bx = 330 - (int)((now / 90) % 420);
  if (bx > -30) {
    uint16_t sail = hmT(UI_WHITE), hull = hmT(C565(0xc8, 0x4a, 0x3a));
    gfx->fillTriangle(bx - 12, seaTop + 1, bx + 12, seaTop + 1, bx + 8, seaTop + 6, hull);
    gfx->fillTriangle(bx - 12, seaTop + 1, bx - 8, seaTop + 6, bx + 8, seaTop + 6, hull);
    gfx->fillRect(bx, seaTop - 20, 1, 21, hmT(C565(0x5a, 0x4a, 0x3a)));
    gfx->fillTriangle(bx + 1, seaTop - 19, bx + 1, seaTop - 2, bx + 13, seaTop - 2, sail);
    gfx->fillTriangle(bx - 1, seaTop - 14, bx - 1, seaTop - 2, bx - 9, seaTop - 2, sail);
  }
  // glints on the water: short dashes that shimmer
  uint16_t glint = hmT(C565(0xd8, 0xf2, 0xff));
  for (int k = 0; k < 20; k++) {
    uint32_t h = hmHash(k * 23 + 1);
    if ((now / 400 + h) % 5 == 0) continue;
    int y = seaTop + 6 + (int)((h >> 8) % (HORIZON - seaTop - 4));
    int x = (int)(h % 466) + (int)(6 * sinf(now * 0.0015f + k));
    int w = 6 + (int)((h >> 16) % 10) * (y - seaTop) / 50;
    gfx->fillRect(x, y, w, 2, glint);
  }
}

static void hmBeachGround(uint16_t soil, uint32_t now) {
  // wet sand and a foam line that breathes in and out
  uint16_t wet = lerp565(soil, hmT(C565(0x9a, 0x86, 0x60)), 6, 16);
  gfx->fillRect(0, HORIZON + 8, 466, 10, wet);
  uint16_t foam = hmT(C565(0xf4, 0xfa, 0xff));
  int surge = (int)(4 * sinf(now * 0.0016f));
  for (int x = 0; x < 466; x += 6) {
    int y = HORIZON + 8 + surge + (int)(3 * sinf(x * 0.045f + now * 0.002f));
    gfx->fillRect(x, y, 6, 3, foam);
  }
  // shells and a starfish
  uint16_t shell = hmT(C565(0xf6, 0xc8, 0xc0)), shellD = hmT(C565(0xd8, 0x94, 0x8a));
  static const int16_t SH[][2] = { { 74, 300 }, { 352, 276 }, { 120, 262 }, { 300, 322 } };
  for (auto &s : SH) {
    hmCap(s[0], s[1], 5, s[1] + 4, shell);
    gfx->fillRect(s[0] - 1, s[1] + 1, 1, 3, shellD);
    gfx->fillRect(s[0] + 2, s[1] + 1, 1, 3, shellD);
  }
  uint16_t star = hmT(C565(0xf0, 0x80, 0x48));
  const int fx = 392, fy = 290;
  for (int k = 0; k < 5; k++) {
    float a = -1.5708f + k * 1.2566f;
    gfx->fillTriangle(fx + (int)(3 * cosf(a + 1.2f)), fy + (int)(3 * sinf(a + 1.2f)),
                      fx + (int)(3 * cosf(a - 1.2f)), fy + (int)(3 * sinf(a - 1.2f)),
                      fx + (int)(10 * cosf(a)), fy + (int)(10 * sinf(a)), star);
  }
  gfx->fillCircle(fx, fy, 3, star);
}

static void hmForestFar() {
  uint16_t far = hmT(C565(0x74, 0xa8, 0x94)), mid = hmT(C565(0x3e, 0x7e, 0x56));
  hmCap(233, 200, 300, HORIZON, hmT(C565(0x8e, 0xbc, 0xa0)));
  for (int k = 0; k < 15; k++) {
    uint32_t h = hmHash(k * 17 + 9);
    hmPine(k * 33 + 6, HORIZON - 12, 26 + (int)(h % 18), far, 0);
  }
  static const int16_t MID[][2] = { { 22, 74 }, { 70, 58 }, { 118, 46 }, { 350, 46 }, { 396, 60 }, { 446, 76 } };
  for (auto &t : MID) hmPine(t[0], HORIZON + 4, t[1], mid, 0);
}

static void hmForestGround(uint16_t soil) {
  // a lighter clearing for the pet to stand in
  hmEllipse(CX, PET_GROUND + 4, 150, 22, lerp565(soil, hmT(C565(0xb8, 0xa8, 0x6a)), 5, 16));
  uint16_t cap = hmT(C565(0xe0, 0x46, 0x3e)), dot = hmT(UI_WHITE), stem = hmT(C565(0xf0, 0xe4, 0xc8));
  static const int16_t MU[][2] = { { 70, 296 }, { 392, 282 }, { 104, 256 }, { 360, 318 } };
  for (auto &m : MU) {
    gfx->fillRect(m[0] - 2, m[1], 5, 7, stem);
    hmCap(m[0], m[1] - 6, 8, m[1], cap);
    gfx->fillRect(m[0] - 4, m[1] - 4, 2, 2, dot);
    gfx->fillRect(m[0] + 2, m[1] - 3, 2, 2, dot);
  }
  uint16_t fern = hmT(C565(0x2e, 0x6a, 0x40));
  static const int16_t FE[][2] = { { 40, 250 }, { 430, 262 }, { 150, 330 }, { 300, 250 } };
  for (auto &f : FE) {
    gfx->fillTriangle(f[0], f[1], f[0] - 12, f[1] + 10, f[0] - 2, f[1] + 10, fern);
    gfx->fillTriangle(f[0], f[1] - 4, f[0] - 3, f[1] + 10, f[0] + 3, f[1] + 10, fern);
    gfx->fillTriangle(f[0], f[1], f[0] + 12, f[1] + 10, f[0] + 2, f[1] + 10, fern);
  }
}

static void hmVolcanoFar(uint32_t now) {
  uint16_t ridge = hmT(C565(0x7a, 0x58, 0x5a)), cone = hmT(C565(0x5c, 0x40, 0x44));
  hmCap(390, 176, 120, HORIZON, ridge);
  // the volcano: a cone with a flat, glowing top
  gfx->fillTriangle(30, HORIZON, 104, 142, 104, HORIZON, cone);
  gfx->fillTriangle(220, HORIZON, 146, 142, 146, HORIZON, cone);
  gfx->fillRect(104, 142, 43, HORIZON - 142, cone);
  float pulse = 0.5f + 0.5f * sinf(now * 0.004f);
  uint16_t lava = lerp565(C565(0xd8, 0x3a, 0x22), C565(0xff, 0xb0, 0x3a), (int)(pulse * 16), 16);
  hmEllipse(125, 143, 21, 3, lava);
  for (int y = 146; y < 200; y += 3) {        // a lava trickle down the slope
    int x = 118 - (y - 146) / 3 + (int)(2 * sinf(y * 0.3f));
    gfx->fillRect(x, y, 3, 3, lava);
  }
  // smoke puffs rising from the crater, growing and fading into the sky
  for (int k = 0; k < 5; k++) {
    int age = (int)((now / 45 + k * 30) % 150);   // gone before it reaches the header
    int y = 132 - age * 2 / 3, x = 125 + age / 4 + (int)(4 * sinf(now * 0.002f + k));
    uint16_t c = lerp565(hmT(C565(0x9a, 0x90, 0x94)), hmSkyAt(y), age, 160);
    gfx->fillCircle(x, y, 6 + age / 18, c);
  }
}

static void hmVolcanoGround(uint16_t soil, uint32_t now) {
  float pulse = 0.5f + 0.5f * sinf(now * 0.003f);
  uint16_t crack = lerp565(C565(0x8a, 0x2a, 0x1e), C565(0xff, 0x8a, 0x2a), (int)(pulse * 16), 16);
  static const int16_t CR[][4] = { { 40, 262, 92, 270 }, { 92, 270, 110, 290 }, { 340, 256, 384, 262 },
                                   { 384, 262, 420, 252 }, { 160, 324, 214, 318 }, { 282, 322, 330, 330 } };
  for (auto &c : CR) {
    gfx->drawLine(c[0], c[1], c[2], c[3], crack);
    gfx->drawLine(c[0], c[1] + 1, c[2], c[3] + 1, crack);
  }
  uint16_t rock = lerp565(soil, C565(0x20, 0x14, 0x14), 7, 16);
  gfx->fillTriangle(56, HORIZON + 30, 30, HORIZON + 52, 84, HORIZON + 52, rock);
  gfx->fillTriangle(404, HORIZON + 26, 380, HORIZON + 50, 432, HORIZON + 50, rock);
  gfx->fillRoundRect(130, 300, 22, 10, 4, rock);
  // embers drifting up off the ground
  for (int k = 0; k < 8; k++) {
    uint32_t h = hmHash(k * 71 + 2);
    int age = (int)((now / 30 + h) % 120);
    int x = 40 + (int)(h % 386) + (int)(5 * sinf(now * 0.003f + k));
    int y = HUD_BAND_Y - 10 - age * 2;
    gfx->fillRect(x, y, 3, 3, age < 80 ? C565(0xff, 0xa0, 0x3a) : C565(0xc0, 0x4a, 0x2a));
  }
}

static void hmMountainFar() {
  uint16_t far = hmT(C565(0xa4, 0xb6, 0xd2)), near = hmT(C565(0x74, 0x86, 0x9c));
  uint16_t snow = hmT(C565(0xf4, 0xf8, 0xff)), pine = hmT(C565(0x3e, 0x6a, 0x52));
  static const int16_t PK[][3] = { { 74, 146, 110 }, { 222, 124, 130 }, { 380, 140, 116 } };  // x, top, half-base
  for (auto &p : PK) {
    gfx->fillTriangle(p[0], p[1], p[0] - p[2], HORIZON, p[0] + p[2], HORIZON, far);
    int s = (HORIZON - p[1]) / 4, w = p[2] * s / (HORIZON - p[1]);
    gfx->fillTriangle(p[0], p[1], p[0] - w, p[1] + s, p[0] + w, p[1] + s, snow);
    gfx->fillTriangle(p[0] - w, p[1] + s, p[0] - w / 2, p[1] + s + 6, p[0], p[1] + s, snow);
  }
  gfx->fillTriangle(150, 176, 40, HORIZON, 260, HORIZON, near);
  gfx->fillTriangle(330, 168, 220, HORIZON, 450, HORIZON, near);
  for (int k = 0; k < 16; k++) hmPine(k * 30 + 4, HORIZON + 2, 18 + (int)(hmHash(k) % 10), pine, 0);
}

static void hmMountainGround(uint16_t soil) {
  uint16_t stone = hmT(C565(0x9a, 0x9c, 0xa4)), stoneD = hmT(C565(0x6e, 0x70, 0x7a));
  static const int16_t ST[][3] = { { 60, 296, 12 }, { 404, 284, 14 }, { 112, 262, 7 }, { 360, 320, 8 }, { 300, 258, 6 } };
  for (auto &s : ST) {
    hmEllipse(s[0], s[1] + 2, s[2], s[2] / 2 + 1, stoneD);
    hmEllipse(s[0] - 1, s[1], s[2] - 1, s[2] / 2, stone);
  }
  // a trodden path winding to the pet
  hmEllipse(CX, PET_GROUND + 4, 130, 18, lerp565(soil, hmT(C565(0xe0, 0xcc, 0x9e)), 6, 16));
}

static void hmSnowFar() {
  uint16_t far = hmT(C565(0xd2, 0xde, 0xee)), near = hmT(C565(0xe8, 0xf0, 0xf8));
  uint16_t pine = hmT(C565(0x3a, 0x66, 0x5a)), snow = hmT(UI_WHITE);
  hmCap(90, 180, 140, HORIZON, far);
  hmCap(390, 172, 130, HORIZON, far);
  static const int16_t SPINE[][2] = { { 44, 70 }, { 84, 52 }, { 384, 56 }, { 426, 74 } };
  for (auto &p : SPINE) hmPine(p[0], HORIZON + 6, p[1], pine, snow);
  hmCap(233, 206, 240, HORIZON, near);
}

static void hmSnowGround(uint16_t soil, uint32_t now) {
  uint16_t drift = lerp565(soil, hmT(C565(0xb4, 0xc8, 0xe4)), 7, 16);
  hmEllipse(80, 280, 60, 8, drift);
  hmEllipse(380, 316, 70, 8, drift);
  hmEllipse(250, 262, 40, 5, drift);
  // a little snowman, right of the pet
  uint16_t body = hmT(UI_WHITE), shade = hmT(C565(0xc8, 0xd6, 0xea));
  gfx->fillCircle(386, 282, 14, shade);
  gfx->fillCircle(385, 281, 13, body);
  gfx->fillCircle(385, 261, 10, body);
  gfx->fillCircle(385, 246, 8, body);
  gfx->fillRect(382, 244, 2, 2, hmT(UI_INK));
  gfx->fillRect(387, 244, 2, 2, hmT(UI_INK));
  gfx->fillTriangle(385, 247, 385, 250, 393, 248, hmT(C565(0xf0, 0x8a, 0x3a)));
  gfx->fillRect(376, 236, 18, 3, hmT(C565(0x3a, 0x3a, 0x4a)));
  gfx->fillRect(379, 226, 12, 10, hmT(C565(0x3a, 0x3a, 0x4a)));
  // sparkles in the snow
  for (int k = 0; k < 10; k++) {
    uint32_t h = hmHash(k * 29 + 4);
    if ((now / 300 + h) % 6 > 1) continue;
    int x = 20 + (int)(h % 426), y = HORIZON + 12 + (int)((h >> 9) % 90);
    gfx->fillRect(x - 2, y, 5, 1, UI_WHITE);
    gfx->fillRect(x, y - 2, 1, 5, UI_WHITE);
  }
}

static void hmSnowfall(uint32_t now) {
  for (int k = 0; k < 22; k++) {
    uint32_t h = hmHash(k * 53 + 8);
    int sp = 14 + (int)(h % 12);
    int y = (int)((h % 400 + now / sp) % 340);
    int x = (int)((h >> 10) % 466) + (int)(8 * sinf(now * 0.001f + k));
    int s = 2 + (int)((h >> 20) & 1);
    gfx->fillRect(x, y, s, s, UI_WHITE);
  }
}

// ---------- drawScene: the whole background, every frame ----------
void drawScene(uint8_t biome, uint32_t now, bool night) {
  night = rankSceneNight(night);   // the player's scene theme (ui_rank.ino)
  if (biome > 5) biome = 0;
  int h = sceneHour();
  hmPhase = night ? 3 : (h < 8 ? 1 : (h < 18 ? 0 : 2));
  uint16_t top, bot;
  if (night)            { top = C565(0x0a, 0x10, 0x26); bot = C565(0x24, 0x2c, 0x56); }
  else if (h < 8)       { top = C565(0xc8, 0x6a, 0x92); bot = C565(0xfa, 0xc2, 0x86); }  // dawn
  else if (h < 18)      { top = C565(0x6c, 0xb8, 0xec); bot = C565(0xd4, 0xee, 0xf4); }  // day
  else                  { top = C565(0x8a, 0x4a, 0x7a); bot = C565(0xf6, 0xa8, 0x5e); }  // dusk
  if (!night && biome == 3) { top = lerp565(top, C565(0xb0, 0x60, 0x58), 4, 16); bot = lerp565(bot, C565(0xf0, 0xb0, 0x90), 4, 16); }
  if (!night && biome == 5) { top = lerp565(top, C565(0xc4, 0xd8, 0xee), 5, 16); bot = lerp565(bot, UI_WHITE, 5, 16); }
  rankSceneSky(top, bot, night);
  hmSkyTop = top; hmSkyBot = bot;

  for (int y = 0; y < HORIZON; y += 6)
    gfx->fillRect(0, y, 466, 6, lerp565(top, bot, y, HORIZON));

  if (night) hmMoonStars(now);
  else if (hmPhase == 2) hmDuskSun();
  else hmSun(now);
  hmClouds(now);

  // a gull crossing the sky every ~36 s (not at night)
  if (!night) {
    int bx = (int)((now / 40) % 900) - 120;
    if (bx < 520) {
      int by = 158 + (int)(10 * sinf(now * 0.0012f));
      hmBird(bx, by, (now / 180) % 2, hmT(C565(0x44, 0x4c, 0x60)));
      hmBird(bx - 26, by + 12, (now / 180 + 1) % 2, hmT(C565(0x44, 0x4c, 0x60)));
    }
  }

  switch (biome) {
    case 1: hmBeachFar(now); break;
    case 2: hmForestFar(); break;
    case 3: hmVolcanoFar(now); break;
    case 4: hmMountainFar(); break;
    case 5: hmSnowFar(); break;
    default: hmMeadowFar(); break;
  }

  // the ground: a lighter lip at the horizon, the soil, a darker foreground
  uint16_t soil = hmT(BIOME_SOIL[biome]);
  uint16_t lip = lerp565(soil, UI_WHITE, 2, 16), fore = lerp565(soil, C565(0x10, 0x14, 0x20), 3, 16);
  gfx->fillRect(0, HORIZON, 466, 4, lip);
  gfx->fillRect(0, HORIZON + 4, 466, 74, soil);
  for (int y = HORIZON + 78; y < 466; y += 16)
    gfx->fillRect(0, y, 466, 16, lerp565(soil, fore, y - HORIZON - 78, 466 - HORIZON - 78));
  hmSpecks(soil, 36, biome * 101 + 17);

  switch (biome) {
    case 1: hmBeachGround(soil, now); break;
    case 2: hmForestGround(soil); break;
    case 3: hmVolcanoGround(soil, now); break;
    case 4: hmMountainGround(soil); break;
    case 5: hmSnowGround(soil, now); break;
    default: hmMeadowGround(soil, now); break;
  }
  if (biome == 5) hmSnowfall(now);
  if (night && (biome == 0 || biome == 2)) hmFireflies(now, 9);
  rankSceneExtras(now, night);
}

// ---------- HUD ----------
static uint16_t hudInk() { return gNight ? UI_INK_NIGHT : UI_INK; }
static uint16_t hudHalo() { return gNight ? C565(0x10, 0x14, 0x2a) : UI_WHITE; }

// Size-2 text with a 1 px halo, so it reads on any sky or soil.
static void hudTextAt(const char *s, int x, int y, uint16_t col, uint8_t size) {
  gfx->setTextSize(size);
  gfx->setTextColor(hudHalo());
  static const int8_t O[4][2] = { { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 } };
  for (auto &o : O) { gfx->setCursor(x + o[0], y + o[1]); gfx->print(s); }
  gfx->setTextColor(col);
  gfx->setCursor(x, y);
  gfx->print(s);
}
void hudText(const char *s, int cx, int y, uint16_t col) {
  hudTextAt(s, cx - (int)strlen(s) * 6, y, col, 2);
}

void drawBattery() {
  int pc = batPercent();
  if (pc < 0) return;  // no battery connected
  int x = CX - 14, y = 3, w = 24, h = 11;
  bool charging = batCharging();
  uint16_t col = charging ? UI_BAR_OK
                 : (pc >= 40) ? hudInk()
                 : (pc >= 15) ? UI_BAR_WARN
                              : UI_BAR_BAD;
  gfx->fillRoundRect(x - 1, y - 1, w + 2, h + 2, 3, hudHalo());
  gfx->drawRoundRect(x, y, w, h, 2, col);
  gfx->fillRect(x + w, y + 3, 3, 5, col);  // terminal
  if (charging) {
    uint16_t bolt = C565(0xff, 0xd9, 0x4a);
    int bx = x + w / 2;
    gfx->fillTriangle(bx + 3, y + 1, bx - 4, y + 6, bx + 1, y + 6, bolt);
    gfx->fillTriangle(bx - 1, y + 5, bx + 4, y + 5, bx - 3, y + 10, bolt);
  } else {
    int fw = (w - 4) * pc / 100;
    if (fw > 0) gfx->fillRect(x + 2, y + 2, fw, h - 4, col);
  }
}

// HH:MM under the battery. pet.lastSeenEpoch is only refreshed every 30 s, so
// the minutes are carried forward on millis() from the moment it last changed.
static uint32_t hmSeenE = 0, hmSeenMs = 0;
static void drawClock() {
  uint32_t e = pet.lastSeenEpoch;
  if (!e) return;   // no clock set: nothing to show
  if (e != hmSeenE) { hmSeenE = e; hmSeenMs = millis(); }
  uint32_t t = hmSeenE + (millis() - hmSeenMs) / 1000;
  char b[8];
  snprintf(b, sizeof(b), "%02u:%02u", (unsigned)((t / 3600) % 24), (unsigned)((t / 60) % 60));
  hudText(b, CX, 17, hudInk());
}

// The name on a cloud-shaped label, the status in a speech bubble under it.
#define HDR_CLOUD_Y 44
#define HDR_CLOUD_H 34
#define HDR_BUBBLE_Y 84
#define HDR_BUBBLE_H 24
void drawHeader(const char *name, uint16_t nameColor, const char *msg) {
  drawBattery();
  drawClock();
  uint16_t fill = gNight ? C565(0x2a, 0x32, 0x58) : UI_WHITE;
  uint16_t edge = gNight ? C565(0x5c, 0x6a, 0xa4) : C565(0xa8, 0xc0, 0xdc);

  int w = (int)strlen(name) * 18 + 44;
  if (w > 420) w = 420;
  int x0 = CX - w / 2, y0 = HDR_CLOUD_Y;
  // The bumps stay clear of the clock (x 203..263, y 17..32): the inner pair
  // only on a label wide enough to put them past it.
  for (int pass = 0; pass < 2; pass++) {
    uint16_t c = pass ? fill : edge;
    int g = pass ? 0 : 2;
    gfx->fillRoundRect(x0 - g, y0 - g, w + 2 * g, HDR_CLOUD_H + 2 * g, HDR_CLOUD_H / 2 + g, c);
    gfx->fillCircle(x0 + 20, y0 + 5, 12 + g, c);
    gfx->fillCircle(x0 + w - 20, y0 + 6, 11 + g, c);
    if (w >= 220) {
      gfx->fillCircle(x0 + 50, y0 + 1, 15 + g, c);
      gfx->fillCircle(x0 + w - 52, y0 + 2, 14 + g, c);
    }
  }
  gfx->setTextSize(3);
  gfx->setTextColor(gNight ? UI_INK_NIGHT : lerp565(nameColor, UI_INK, 3, 8));
  gfx->setCursor(CX - (int)strlen(name) * 9, y0 + 6);
  gfx->print(name);

  int bw = (int)strlen(msg) * 12 + 40;
  if (bw > 420) bw = 420;
  int bx = CX - bw / 2, by = HDR_BUBBLE_Y;
  gfx->fillRoundRect(bx - 2, by - 2, bw + 4, HDR_BUBBLE_H + 4, HDR_BUBBLE_H / 2 + 2, edge);
  gfx->fillRoundRect(bx, by, bw, HDR_BUBBLE_H, HDR_BUBBLE_H / 2, fill);
  gfx->fillCircle(bx + 14, by + HDR_BUBBLE_H / 2, 4, gNight ? C565(0x8a, 0xb4, 0xff) : C565(0x4a, 0x90, 0xe0));
  gfx->setTextSize(2);
  gfx->setTextColor(hudInk());
  gfx->setCursor(bx + 26, by + 5);
  gfx->print(msg);
}

// ---- the four needs as arcs on the bezel ----
// Gauge i: 0 JOY upper-left, 1 FOOD lower-left, 2 ENE upper-right, 3 HYG
// lower-right. Each spans GAUGE_A0..GAUGE_A1 degrees off the horizontal at
// radius GAUGE_RM, and fills from its LOWER end upward as the need is met.
#define GAUGE_RM 214
#define GAUGE_HW 6         // half the track's thickness
#define GAUGE_A0 5.0f
#define GAUGE_A1 27.5f     // the lower pair's bottom cap stays above HUD_BAND_Y
#define GAUGE_LABEL_R 176

static void gaugeGeom(int i, int *side, int *up) {
  *side = (i < 2) ? -1 : 1;
  *up = (i % 2 == 0) ? 1 : -1;
}
static uint8_t gaugeValue(int i) {
  switch (i) {
    case 0: return pet.joy;
    case 1: return pet.fullness;
    case 2: return pet.energy;
    default: return pet.hygiene;
  }
}
static const char *gaugeLabel(int i) {
  switch (i) {
    case 0: return T(S_BAR_JOY);
    case 1: return T(S_BAR_FOOD);
    case 2: return T(S_BAR_ENE);
    default: return T(S_BAR_HYG);
  }
}
// The one answer to "what colour is a need at this value" (same thresholds as
// the status line: under 25 is when the pet complains).
uint16_t needGaugeColor(uint8_t v) {
  return v >= 50 ? UI_BAR_OK : (v >= 25 ? UI_BAR_WARN : UI_BAR_BAD);
}
uint16_t needGaugeTrack() { return gNight ? C565(0x2c, 0x32, 0x52) : C565(0xf4, 0xf0, 0xe8); }

static void gaugePt(int side, int up, float aDeg, int r, int *x, int *y) {
  float a = aDeg * 0.0174533f;
  *x = CX + side * (int)lroundf(r * cosf(a));
  *y = CY - up * (int)lroundf(r * sinf(a));
}

// The point on gauge i's centre line, permille of the way from where its fill
// starts (0) to its far end (1000). For the tests and the fill itself.
// The gauge as SEEN runs cap tip to cap tip: GAUGE_A0 - GAUGE_CAP to
// GAUGE_A1 + GAUGE_CAP. The fill is measured on that same length, so a need at
// 50% covers half of what the eye reads as the gauge.
#define GAUGE_CAP (GAUGE_HW * 57.2958f / GAUGE_RM)   // a cap's radius, in degrees
static float gaugeVisAngle(int up, int permille) {
  float lo = GAUGE_A0 - GAUGE_CAP, hi = GAUGE_A1 + GAUGE_CAP;
  float d = (hi - lo) * permille / 1000.0f;
  return up > 0 ? lo + d : hi - d;
}
void needGaugePoint(int i, int permille, int *x, int *y) {
  int side, up;
  gaugeGeom(i, &side, &up);
  gaugePt(side, up, gaugeVisAngle(up, permille), GAUGE_RM, x, y);
}

// An arc band between angles a0 < a1, drawn as horizontal spans of the annulus
// [r-hw, r+hw] plus a round cap at each end. Within +-35 degrees of the
// horizontal every row crosses one side of the annulus once, so this is ~100
// hlines rather than a per-pixel pass.
static void gaugeSeg(int side, int up, float a0, float a1, int r, int hw, uint16_t c) {
  int xa, ya, xb, yb;
  gaugePt(side, up, a0, r, &xa, &ya);
  gaugePt(side, up, a1, r, &xb, &yb);
  int y0 = ya < yb ? ya : yb, y1 = ya < yb ? yb : ya;
  int ro = r + hw, ri = r - hw;
  for (int y = y0; y <= y1; y++) {
    int dy = y - CY;
    int xo = (int)sqrtf((float)(ro * ro - dy * dy));
    int xi2 = ri * ri - dy * dy;
    int xi = xi2 > 0 ? (int)sqrtf((float)xi2) : 0;
    hmHLine(CX + side * xi, CX + side * xo, y, c);
  }
  gfx->fillCircle(xa, ya, hw, c);
  gfx->fillCircle(xb, yb, hw, c);
}

// The label sits inside the arc near its outer end.
void needGaugeLabelRect(int i, int *x0, int *y0, int *x1, int *y1) {
  int side, up, lx, ly;
  gaugeGeom(i, &side, &up);
  gaugePt(side, up, GAUGE_A1 - 2.0f, GAUGE_LABEL_R, &lx, &ly);
  int w = (int)strlen(gaugeLabel(i)) * 12;
  *x0 = lx - w / 2; *x1 = lx + w / 2;
  *y0 = ly - 8; *y1 = ly + 8;
}

void drawNeedGauges() {
  uint16_t edge = gNight ? C565(0x70, 0x7e, 0xb4) : C565(0x56, 0x66, 0x84);
  uint16_t track = needGaugeTrack();
  bool blink = (millis() / 450) % 2;
  for (int i = 0; i < 4; i++) {
    int side, up;
    gaugeGeom(i, &side, &up);
    uint8_t v = gaugeValue(i);
    if (v > 100) v = 100;
    uint16_t fill = needGaugeColor(v);
    // a need in the red pulses its rim, so it is noticed from across a room
    gaugeSeg(side, up, GAUGE_A0, GAUGE_A1, GAUGE_RM, GAUGE_HW + 2, (v < 25 && blink) ? UI_BAR_BAD : edge);
    gaugeSeg(side, up, GAUGE_A0, GAUGE_A1, GAUGE_RM, GAUGE_HW, track);
    if (v > 0) {
      // cap centres: the fixed start, and the visible end pulled back by a cap
      // (any need above zero shows at least the start cap)
      float start = up > 0 ? GAUGE_A0 : GAUGE_A1;
      float end = gaugeVisAngle(up, v * 10) - up * GAUGE_CAP;
      if ((end - start) * up < 0) end = start;
      float a0 = start < end ? start : end, a1 = start < end ? end : start;
      gaugeSeg(side, up, a0, a1, GAUGE_RM, GAUGE_HW, fill);
      if (a1 - a0 > 3.0f) gaugeSeg(side, up, a0 + 1.5f, a1 - 1.5f, GAUGE_RM + 3, 1, lerp565(fill, UI_WHITE, 8, 16));
    }
    int x0, y0, x1, y1;
    needGaugeLabelRect(i, &x0, &y0, &x1, &y1);
    hudTextAt(gaugeLabel(i), x0, y0, v < 25 ? UI_BAR_BAD : hudInk(), 2);
  }
}

// ---- bond and streak, just above the band ----
#define INFO_Y 316
void drawInfoLine() {
  char b[20];
  snprintf(b, sizeof(b), "%s %u", T(S_VIN), (unsigned)pet.bond);
  int w = (int)strlen(b) * 12 + 18;
  int x = CX - 80 - w / 2;
  uint16_t heart = C565(0xf0, 0x5a, 0x8a);
  gfx->fillCircle(x + 4, INFO_Y + 5, 4, hudHalo());
  gfx->fillCircle(x + 11, INFO_Y + 5, 4, hudHalo());
  gfx->fillCircle(x + 4, INFO_Y + 5, 3, heart);
  gfx->fillCircle(x + 10, INFO_Y + 5, 3, heart);
  gfx->fillTriangle(x + 1, INFO_Y + 7, x + 13, INFO_Y + 7, x + 7, INFO_Y + 13, heart);
  hudTextAt(b, x + 18, INFO_Y, hudInk(), 2);
  if (pet.streak < 1) return;
  // the daily streak's flame (it used to sit at (26,16), which is off the round panel)
  snprintf(b, sizeof(b), "%u", (unsigned)pet.streak);
  w = (int)strlen(b) * 12 + 22;
  x = CX + 80 - w / 2;
  gfx->fillTriangle(x + 8, INFO_Y - 2, x, INFO_Y + 15, x + 16, INFO_Y + 15, hudHalo());
  gfx->fillTriangle(x + 8, INFO_Y, x + 2, INFO_Y + 14, x + 14, INFO_Y + 14, UI_BAR_BAD);
  gfx->fillTriangle(x + 8, INFO_Y + 6, x + 4, INFO_Y + 14, x + 12, INFO_Y + 14, UI_BAR_WARN);
  hudTextAt(b, x + 22, INFO_Y, hudInk(), 2);
}

// ---- the pastel action band ----
int hudBandTop() { return HUD_BAND_Y; }   // for the tests
uint16_t hudBandColor(int y) {
  uint16_t a = gNight ? C565(0x26, 0x2a, 0x4c) : C565(0xf2, 0xec, 0xfa);
  uint16_t b = gNight ? C565(0x16, 0x19, 0x30) : C565(0xe2, 0xd6, 0xf2);
  return lerp565(a, b, y - HUD_BAND_Y, 466 - HUD_BAND_Y);
}
void drawHudBand() {
  for (int y = HUD_BAND_Y + 3; y < 466; y += 8)
    gfx->fillRect(0, y, 466, 8, hudBandColor(y));
  gfx->fillRect(0, HUD_BAND_Y, 466, 2, gNight ? C565(0x50, 0x62, 0xa8) : C565(0x86, 0xb2, 0xe6));
  gfx->fillRect(0, HUD_BAND_Y + 2, 466, 1, gNight ? C565(0x3a, 0x42, 0x70) : UI_WHITE);
}

// Each button wears its own pastel: peach food, lilac sleep, sky bath, mint train.
static const uint16_t BTN_FILL[BTN_COUNT] = { C565(0xff, 0xe4, 0xd0), C565(0xe8, 0xe2, 0xff),
                                              C565(0xd8, 0xee, 0xff), C565(0xdc, 0xf6, 0xe2) };
static const uint16_t BTN_EDGE[BTN_COUNT] = { C565(0xee, 0x96, 0x68), C565(0x9a, 0x8a, 0xe0),
                                              C565(0x62, 0xaa, 0xe6), C565(0x5e, 0xbc, 0x84) };
void drawButtons() {
  for (int i = 0; i < BTN_COUNT; i++) {
    bool off = uiButtonDisabled(i);   // asleep, only the LIGHT works
    int cx = buttons[i].cx, cy = buttons[i].cy;
    int bx = cx - BTN_HALF, by = cy - BTN_HALF, s = 2 * BTN_HALF;
    uint16_t fill = BTN_FILL[i], edge = BTN_EDGE[i];
    if (off) {
      fill = gNight ? C565(0x2e, 0x32, 0x50) : C565(0xe6, 0xe2, 0xec);
      edge = gNight ? C565(0x46, 0x4c, 0x70) : C565(0xb4, 0xb0, 0xc4);
    } else if (gNight) {
      fill = lerp565(fill, C565(0x26, 0x2a, 0x4c), 6, 16);
    }
    gfx->fillRoundRect(bx, by + 3, s, s, 14, lerp565(edge, C565(0x10, 0x10, 0x20), 6, 16));   // drop shadow
    gfx->fillRoundRect(bx, by, s, s, 14, edge);
    gfx->fillRoundRect(bx + 2, by + 2, s - 4, s - 4, 12, fill);
    if (!off) {
      gfx->fillRoundRect(bx + 10, by + 5, s - 20, 3, 1, lerp565(fill, UI_WHITE, 10, 16));   // gloss
      drawMap(buttons[i].icon, 16, cx - 16, cy - 14, 2, false);
    }
    const char *l = homeButtonLabel(i);
    gfx->setTextSize(2);
    gfx->setTextColor(off ? edge : hudInk());
    gfx->setCursor(cx - (int)strlen(l) * 6, by - 19);
    gfx->print(l);
  }
}

// ---- asleep: three Z's drifting up and growing ----
void drawSleepZ(uint32_t now) {
  for (int k = 0; k < 3; k++) {
    int t = (int)((now / 30 + k * 40) % 120);
    int x = 300 + t / 3 + (int)(4 * sinf(now * 0.003f + k));
    int y = 196 - t;
    hudTextAt("Z", x, y, t < 90 ? UI_INK_NIGHT : C565(0x7a, 0x84, 0xb0), (uint8_t)(t < 40 ? 2 : (t < 80 ? 3 : 4)));
  }
}
