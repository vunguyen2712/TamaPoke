// ---------- the three training games (v3.17) ----------
// ATK = the punching bag, SPE = the reaction test, DEF = the ball game. They
// used to be a 10 s masher, a single red target and a lone pokeball; each now
// has a skill to learn, things that change during the session and feedback for
// every tap. README, "Training games", has the player-facing numbers.
//
// The shared rules, so the three read as one family:
//  - a 3-2-1 countdown (TRN_INTRO_MS) before the clock starts; nothing scores
//    during it, and leaving during it banks exactly what leaving at t=0 did
//  - a COMBO counter whose multiplier trnMult() is x2 from 5 and x3 from 10
//  - pop-up points, particle bursts, a jolt on the big hits, a blip that climbs
//    in pitch with the combo (sfxRise)
//  - a results card with a grade S/A/B/C, stars, the gain and NEW RECORD
//
// Points vs the record. Every game keeps its RAW count -- sackHits, spdHits,
// gameScore -- and that is still what strHi/spdHi/gameHi record, so a record
// set before this change means the same thing after it. The stat gain is read
// from an EFFECTIVE score derived from the session's points (sackEff/spdEff/
// gameEff); the Pet trainers take both (pet.h). One function per game answers
// "what is this session worth", and the natural end, leaving early and the
// grade all ask it.
//
// Globals the main sketch touches (sackOpen, gameScore, ...) are declared in
// TamaPoke.ino; the rest live here. Helpers that take no primitive-only
// signature are static, so the emulator's prototype generator never sees them.

#define TRN_INTRO_MS 1800UL   // "3", "2", "1" at 600 ms each; GO! overlays the start
#define TRN_STEP_MS 600UL
#define TRN_GO_MS 600UL

uint32_t trnGoAt = 0;          // when play (and the session clock) starts
uint16_t trnCombo = 0, trnBestCombo = 0;
static uint32_t trnComboAt = 0, trnMilestoneAt = 0;
static uint32_t trnJoltFrom = 0, trnJoltUntil = 0;
static uint8_t trnJoltAmp = 0;
static int8_t trnLastCount = -1;   // which countdown number last ticked
static bool trnGoPlayed = false;
// the results card
uint8_t trnGrade = 0;          // 0 C, 1 B, 2 A, 3 S
static uint8_t trnStarsShown = 0;

// x1 below 5 in a row, x2 from 5, x3 from 10. Shared by the three games.
uint8_t trnMult(uint16_t combo) { return combo >= 10 ? 3 : (combo >= 5 ? 2 : 1); }

// The gain a session's effective score would buy before the IV ceiling: what
// the trainer in pet.cpp computes, restated ONLY for the grade -- the stat
// itself always comes from the trainer's own return value.
uint8_t trnPotential(uint16_t eff, uint8_t div) {
  uint16_t g = eff / (div ? div : 1);
  return g > 18 ? 18 : (uint8_t)g;
}
// S = the per-session ceiling reached, A = two thirds of it, B = a third.
uint8_t trnGradeOf(uint8_t potential) {
  return potential >= 18 ? 3 : potential >= 12 ? 2 : potential >= 6 ? 1 : 0;
}

static bool trnInIntro(uint32_t now) { return (int32_t)(now - trnGoAt) < 0; }

// ---- pop-ups and particles: small fixed pools, nothing allocated
#define TRN_POPS 6
#define TRN_POP_MS 650
#define TRN_PARTS 28
#define TRN_PART_MS 480
static struct { int16_t x, y; uint16_t col; uint32_t born; uint8_t size, cross; char txt[16]; } trnPop[TRN_POPS];
static uint8_t trnPopNext = 0;
static struct { float x, y, vx, vy; uint16_t col; uint32_t born; } trnPart[TRN_PARTS];
static uint8_t trnPartNext = 0;
static char trnBannerTxt[24];
static uint16_t trnBannerCol = 0;
static uint32_t trnBannerFrom = 0, trnBannerUntil = 0;

static void trnFxReset() {
  for (auto &p : trnPop) p.born = 0;
  for (auto &p : trnPart) p.born = 0;
  trnBannerUntil = 0;
  trnJoltUntil = 0;
  trnCombo = trnBestCombo = 0;
  trnComboAt = trnMilestoneAt = 0;
  trnLastCount = -1;
  trnGoPlayed = false;
  trnStarsShown = 0;
  trnGrade = 0;
}

static void trnPopup(int x, int y, const char *t, uint16_t col, uint8_t size) {
  // The same word still rising nearby is replaced rather than stacked: four
  // quick crits used to pile four CRITICAL!s into one unreadable smear.
  uint8_t slot = trnPopNext;
  bool reuse = false;
  for (uint8_t i = 0; i < TRN_POPS && t[0]; i++) {
    auto &q = trnPop[i];
    if (q.born && !q.cross && abs(q.x - x) < 90 && !strcmp(q.txt, t)) { slot = i; reuse = true; break; }
  }
  if (!reuse) trnPopNext = (uint8_t)((trnPopNext + 1) % TRN_POPS);
  auto &p = trnPop[slot];
  p.x = (int16_t)x; p.y = (int16_t)y; p.col = col; p.size = size; p.cross = 0;
  p.born = millis() | 1;
  strncpy(p.txt, t, sizeof(p.txt) - 1);
  p.txt[sizeof(p.txt) - 1] = 0;
}
// a red X where a target was missed or an empty tap landed
static void trnCross(int x, int y) {
  trnPopup(x, y, "", UI_BAR_BAD, 2);
  trnPop[(trnPopNext + TRN_POPS - 1) % TRN_POPS].cross = 1;
}
static void trnBurst(int x, int y, uint16_t col, int n, float speed) {
  uint32_t now = millis() | 1;
  for (int i = 0; i < n; i++) {
    auto &p = trnPart[trnPartNext];
    trnPartNext = (uint8_t)((trnPartNext + 1) % TRN_PARTS);
    float a = (random(360)) * 0.01745f;
    float s = speed * (0.5f + random(50) / 100.0f);
    p.x = x; p.y = y; p.vx = cosf(a) * s; p.vy = sinf(a) * s - speed * 0.3f;
    p.col = col; p.born = now;
  }
}
static void trnBanner(const char *t, uint16_t col, uint16_t ms) {
  strncpy(trnBannerTxt, t, sizeof(trnBannerTxt) - 1);
  trnBannerTxt[sizeof(trnBannerTxt) - 1] = 0;
  trnBannerCol = col;
  trnBannerFrom = millis();
  trnBannerUntil = trnBannerFrom + ms;
}
static void trnJolt(uint8_t amp, uint16_t ms) {
  trnJoltFrom = millis();
  trnJoltUntil = trnJoltFrom + ms;
  trnJoltAmp = amp;
}
// the screen jolt, as an offset the games add to what should shake
static int trnJx(uint32_t now) {
  if ((int32_t)(now - trnJoltUntil) >= 0) return 0;
  int left = (int)(trnJoltUntil - now), span = (int)(trnJoltUntil - trnJoltFrom);
  int a = trnJoltAmp * left / (span ? span : 1);
  return ((now / 28) & 1) ? a : -a;
}
static int trnJy(uint32_t now) {
  int x = trnJx(now);
  return ((now / 42) & 1) ? x / 2 : -x / 2;
}

// a combo link: count it, climb the blip, celebrate the multiplier steps
static void trnComboUp() {
  trnCombo++;
  trnComboAt = millis();
  if (trnCombo > trnBestCombo) trnBestCombo = trnCombo;
  sfxRise((uint8_t)(trnCombo - 1 < SFX_RISE_STEPS ? trnCombo - 1 : SFX_RISE_STEPS - 1));
  // the multiplier steps: the HUD's combo line swells for a moment
  if (trnCombo == 5 || trnCombo == 10 || (trnCombo > 10 && trnCombo % 10 == 0)) {
    trnMilestoneAt = trnComboAt;
    trnJolt(4, 120);
  }
}
static void trnComboBreak(int x, int y) {
  if (trnCombo >= 3) trnPopup(x, y - 30, TL(S_TRN_MISS), UI_BAR_BAD, 2);
  trnCombo = 0;
}

static void trnText(const char *s, int cx, int y, uint8_t size, uint16_t col) {
  gfx->setTextSize(size);
  gfx->setTextColor(col);
  gfx->setCursor(cx - (int)strlen(s) * 3 * size, y);
  gfx->print(s);
}
// text with a 2 px dark outline, for pop-ups drawn over a busy sky
static void trnTextO(const char *s, int cx, int y, uint8_t size, uint16_t col) {
  trnText(s, cx - 2, y, size, UI_INK);
  trnText(s, cx + 2, y, size, UI_INK);
  trnText(s, cx, y - 2, size, UI_INK);
  trnText(s, cx, y + 2, size, UI_INK);
  trnText(s, cx, y, size, col);
}

static void trnDrawFx(uint32_t now) {
  for (auto &p : trnPart) {
    if (!p.born) continue;
    uint32_t t = now - p.born;
    if (t > TRN_PART_MS) { p.born = 0; continue; }
    float f = t / 16.0f;                     // ~frames at 60 fps, any frame rate
    int x = (int)(p.x + p.vx * f), y = (int)(p.y + p.vy * f + 0.06f * f * f);
    int s = t < TRN_PART_MS / 2 ? 5 : 3;
    gfx->fillRect(x - s / 2, y - s / 2, s, s, p.col);
  }
  for (auto &p : trnPop) {
    if (!p.born) continue;
    uint32_t t = now - p.born;
    if (t > TRN_POP_MS) { p.born = 0; continue; }
    int y = p.y - (int)(t / 14);
    if (p.cross) {
      int r = 16;
      for (int k = -2; k <= 2; k++) {
        gfx->drawLine(p.x - r + k, p.y - r, p.x + r + k, p.y + r, UI_BAR_BAD);
        gfx->drawLine(p.x + r + k, p.y - r, p.x - r + k, p.y + r, UI_BAR_BAD);
      }
      continue;
    }
    uint8_t sz = (t < 90) ? p.size + 1 : p.size;   // pops in, then settles
    trnTextO(p.txt, p.x, y - 4 * sz, sz, p.col);
  }
  if (trnBannerUntil && (int32_t)(now - trnBannerUntil) < 0) {
    uint8_t sz = (now - trnBannerFrom < 110) ? 4 : 3;
    trnTextO(trnBannerTxt, CX, 112, sz, trnBannerCol);
  }
}

// the score, the clock and the combo, the same in all three games
static void trnHud(uint16_t pts, uint32_t now, uint32_t until, uint32_t total, uint16_t ink) {
  char b[12];
  snprintf(b, sizeof(b), "%u", pts);
  trnText(b, CX, 22, 4, ink);
  uint32_t left = (int32_t)(until - now) > 0 ? until - now : 0;
  if (left > total) left = total;               // during the countdown: full
  int bw = 180, fw = (int)((uint64_t)bw * left / (total ? total : 1));
  gfx->fillRoundRect(CX - bw / 2, 60, bw, 8, 4, UI_TRACK);
  bool hurry = left < 3000 && ((now / 200) & 1);
  if (fw > 2) gfx->fillRoundRect(CX - bw / 2, 60, fw, 8, 4, left < 3000 ? (hurry ? UI_BAR_BAD : UI_BAR_WARN) : UI_BAR_OK);
  if (trnCombo >= 2) {
    char c[24];
    snprintf(c, sizeof(c), TL(S_TRN_COMBO), trnCombo);
    uint8_t m = trnMult(trnCombo);
    if (m > 1) { size_t n = strlen(c); snprintf(c + n, sizeof(c) - n, " x%u", m); }
    uint16_t col = m >= 3 ? UI_BAR_BAD : m == 2 ? UI_BAR_WARN : ink;
    int dy = (now - trnComboAt < 120) ? -2 : 0;  // a little hop on each link
    bool big = trnMilestoneAt && now - trnMilestoneAt < 800;
    trnTextO(c, CX, 76 + dy, big ? 3 : 2, big && ((now / 100) & 1) ? UI_WHITE : col);
  }
}

// 3, 2, 1 -- then GO! over the first moments of play. Returns true while the
// countdown is still running (the caller then draws nothing interactive).
static bool trnDrawIntro(uint32_t now, const char *hint) {
  if (trnInIntro(now)) {
    uint32_t left = trnGoAt - now;
    int n = (int)((left + TRN_STEP_MS - 1) / TRN_STEP_MS);   // 3, 2, 1
    if (n > 3) n = 3;
    if (n < 1) n = 1;
    if (n != trnLastCount) { trnLastCount = (int8_t)n; sfxPlay(SFX_TAP); }
    uint32_t in = TRN_STEP_MS - (left - (uint32_t)(n - 1) * TRN_STEP_MS);   // ms into this number
    int r = 70 - (int)(in * 40 / TRN_STEP_MS);
    gfx->fillCircle(CX, CY - 20, r, UI_WHITE);
    gfx->drawCircle(CX, CY - 20, r, UI_INK);
    gfx->drawCircle(CX, CY - 20, r - 1, UI_INK);
    char b[2] = { (char)('0' + n), 0 };
    trnText(b, CX, CY - 20 - 35, 10, n == 1 ? UI_BAR_BAD : UI_INK);
    if (hint) trnTextO(hint, CX, CY + 64, 2, UI_WHITE);
    return true;
  }
  uint32_t t = now - trnGoAt;
  if (t < TRN_GO_MS) {
    if (!trnGoPlayed) { trnGoPlayed = true; sfxPlay(SFX_LEVEL); }
    trnTextO(TL(S_TRN_GO), CX, CY - 50, t < 120 ? 9 : 7, UI_BAR_OK);
  }
  return false;
}

// a five-pointed star, filled or as an outline-ish grey
static void trnStar(int cx, int cy, int r, uint16_t col) {
  float ox[5], oy[5], ix[5], iy[5];
  for (int k = 0; k < 5; k++) {
    float a = -1.5708f + k * 1.2566f, b = a + 0.6283f;
    ox[k] = cx + cosf(a) * r;       oy[k] = cy + sinf(a) * r;
    ix[k] = cx + cosf(b) * r * 0.45f; iy[k] = cy + sinf(b) * r * 0.45f;
  }
  for (int k = 0; k < 5; k++) {
    int p = (k + 4) % 5;
    gfx->fillTriangle(cx, cy, (int)ix[p], (int)iy[p], (int)ox[k], (int)oy[k], col);
    gfx->fillTriangle(cx, cy, (int)ox[k], (int)oy[k], (int)ix[k], (int)iy[k], col);
  }
}

static const uint16_t TRN_GRADE_COL[4] = { 0x8410, 0x3B9F, UI_BAR_OK, UI_BAR_WARN };

// The results card. Not a full-screen fill: a panel over the game scene.
static void trnDrawResults(uint32_t now, uint32_t shownAt, const char *gainTxt, uint16_t gainCol,
                           uint16_t pts, uint16_t raw, bool newHi, uint16_t hi, const char *extra) {
  uint32_t t = now - shownAt;
  const int px = CX - 150, py = 92, pw = 300, ph = 284;
  gfx->fillRoundRect(px, py, pw, ph, 24, UI_WHITE);
  gfx->drawRoundRect(px, py, pw, ph, 24, UI_INK);
  gfx->drawRoundRect(px + 1, py + 1, pw - 2, ph - 2, 23, UI_INK);
  static const char GR[4] = { 'C', 'B', 'A', 'S' };
  char g[2] = { GR[trnGrade & 3], 0 };
  uint8_t gs = t < 140 ? 11 : 8;              // slams in, then settles
  int gy = py + 14 + (gs == 11 ? -10 : 0);
  if (trnGrade == 3 && ((now / 160) & 1)) gfx->fillCircle(CX, py + 42, 44, C565(0xff, 0xf1, 0xc4));
  trnText(g, CX, gy, gs, TRN_GRADE_COL[trnGrade & 3]);
  // stars pop in one at a time, each with a rising blip
  uint8_t earned = trnGrade;                   // S 3, A 2, B 1, C 0
  for (int i = 0; i < 3; i++) {
    uint32_t at = 260 + i * 200;
    int sx = CX - 54 + i * 54, sy = py + 98;
    if (t >= at && i < earned) {
      if (trnStarsShown <= i) { trnStarsShown = (uint8_t)(i + 1); sfxRise((uint8_t)(4 + i * 3)); }
      trnStar(sx, sy, (t - at < 90) ? 20 : 16, UI_BAR_WARN);
    } else {
      trnStar(sx, sy, 16, UI_TRACK);
    }
  }
  trnText(gainTxt, CX, py + 124, 3, gainCol);
  char b[40], r[20];
  snprintf(b, sizeof(b), TL(S_TRN_PTS), pts);
  snprintf(r, sizeof(r), T(S_HITS_FMT), raw);
  size_t n = strlen(b);
  snprintf(b + n, sizeof(b) - n, "  %s", r);
  trnText(b, CX, py + 160, 2, UI_INK);
  if (newHi && raw > 0) {
    if ((now / 250) & 1) trnText(T(S_NEW_RECORD), CX, py + 186, 2, UI_BAR_WARN);
    else trnText(T(S_NEW_RECORD), CX, py + 186, 2, UI_BAR_BAD);
  } else {
    snprintf(r, sizeof(r), T(S_RECORD_FMT), hi);
    trnText(r, CX, py + 186, 2, UI_INK);
  }
  if (trnBestCombo >= 2) {
    snprintf(r, sizeof(r), TL(S_TRN_COMBO), trnBestCombo);
    trnText(r, CX, py + 212, 2, 0x8410);
  }
  if (extra) trnText(extra, CX, py + 238, 2, UI_INK);
}

// the creature taking part: the PMD action when the SD has one, else the flat
// sprite, else nothing (no SD -- the graceful path)
static void trnPet(uint8_t act, int x, int ground, uint32_t t, bool loop, uint8_t maxS) {
  if (pmd.loaded) {
    if (!pmd.has(act)) { act = PMD_IDLE; t = millis(); loop = true; }
    drawPmdAct(act, x, ground, t, loop, false, maxS);
  } else if (mon.loaded) {
    int s = (mon.h * 2 > 130) ? 1 : 2;
    int w = mon.w * s, h = mon.h * s;
    uint16_t fm = mon.frameMs ? mon.frameMs : 100;
    uint16_t fi = (millis() / fm) % (mon.frames ? mon.frames : 1);
    const uint8_t *fr = mon.data + (uint32_t)fi * mon.w * mon.h;
    int px = x - w / 2, py = ground - h;
    for (int r = 0; r < mon.h; r++)
      for (int c = 0; c < mon.w; c++) {
        uint8_t idx = fr[r * mon.w + c];
        if (idx == 0xFF) continue;
        gfx->fillRect(px + c * s, py + r * s, s, s, mon.pal[idx]);
      }
  }
}
static uint32_t trnActMs(uint8_t act) {
  return (pmd.loaded && pmd.has(act)) ? pmdActTotalMs(pmd.acts[act]) : 300;
}

// fondo del minijuego: habitat del bicho (cielo por hora + suelo del bioma)
void drawGameScene() {
  int hh = sceneHour();
  bool night = hh < 6 || hh >= 20;
  uint16_t top, bot;
  if (night)       { top = C565(0x0c, 0x12, 0x24); bot = C565(0x1e, 0x26, 0x46); }
  else if (hh < 8) { top = C565(0xd1, 0x6a, 0x86); bot = C565(0xf3, 0xb8, 0x7c); }
  else if (hh < 18){ top = C565(0x8f, 0xc8, 0xea); bot = C565(0xdc, 0xee, 0xe6); }
  else             { top = C565(0xc7, 0x5a, 0x4a); bot = C565(0xf0, 0xae, 0x64); }
  int hor = 376;
  for (int y = 0; y < hor; y += 8)
    gfx->fillRect(0, y, 466, 8, lerp565(top, bot, y, hor));
  if (night)
    for (auto &st : STARS) gfx->fillRect(st[0], st[1], 4, 4, UI_WHITE);
  uint8_t bio = pet.isEgg() ? 0 : DEX_TBL[pet.speciesId].biome;
  uint16_t soil = BIOME_SOIL[bio < 6 ? bio : 0];
  if (night) soil = lerp565(soil, C565(0x16, 0x1c, 0x30), 9, 16);
  gfx->fillRect(0, hor, 466, 466 - hor, soil);
}

// Leaving a minigame early banks what was actually earned rather than voiding
// it. The over-guards stop a swipe during the results screen paying twice.
uint16_t sackEff();
uint16_t spdEff();
uint16_t gameEff();
void leaveGame() {
  if (!gameOverUntil) pet.playResult(gameScore, gameEff());
  gameOpen = false;
}
void leaveSack() {
  if (!sackOverUntil) pet.trainStrength(sackHits, sackEff());
  sackOpen = false;
}
void leaveSpeed() {
  if (!spdOverUntil) pet.trainSpeed(spdHits, spdEff());
  spdOpen = false;
}

// =====================================================================
// ATK -- the punching bag
// =====================================================================
// Mashing still works exactly as it always did: every tap is a hit worth one
// point, so a pure masher trains as before. The skill on top is TIMING. A
// needle sweeps a power meter under the bag; a punch thrown while it is in the
// gold zone is a CRITICAL, worth 2 + the combo multiplier. A punch thrown
// within SACK_WINDUP_MS of the previous one is a JAB: it counts, but cannot
// crit and does not break the chain, so mashing earns no crits. A punch that
// was wound up and missed the zone breaks the chain. SACK_FEVER_AT crits in a
// row light FEVER: every punch is worth 2 for SACK_FEVER_MS. A WEAK SPOT glows
// on the bag now and then; landing on it adds SACK_SPOT_PTS.
#define SACK_MS 10000UL
#define SACK_SWEEP_MS 650UL     // the needle crosses the meter in this, then comes back
#define SACK_ZONE0 100          // gold zone half-width, thousandths of the meter...
#define SACK_ZONE_STEP 8        // ...narrowing this much per link of the combo
#define SACK_ZONE_MIN 56
#define SACK_WINDUP_MS 220UL
#define SACK_FEVER_AT 6
#define SACK_FEVER_MS 2500UL
#define SACK_SPOT_MS 1100UL
#define SACK_SPOT_PTS 3
#define SACK_SPOT_HIT 36        // touch radius around the spot (drawn at 16)
#define SACK_X (CX + 40)
#define SACK_TOP 116
#define SACK_W 84
#define SACK_H 132
#define SACK_METER_Y 268
#define SACK_METER_W 280
#define SACK_PET_X (CX - 108)

// what a tap turned out to be (sackTap's return; |SACK_SPOT when it found the spot)
#define SACK_NONE 0
#define SACK_JAB 1
#define SACK_OFF 2
#define SACK_CRIT 3
#define SACK_FEVERHIT 4
#define SACK_SPOT 8

uint16_t sackPts = 0, sackCrits = 0, sackSpots = 0;
uint32_t sackMeterT0 = 0, sackLastTap = 0, sackFeverUntil = 0;
uint32_t sackSpotAt = 0, sackSpotUntil = 0;
static uint32_t sackNextSpot = 0, sackPunchAt = 0, sackCritAt = 0, sackOverAt = 0;
int16_t sackSpotDX = 0, sackSpotDY = 0;     // the spot, relative to the bag's top centre
static float sackAng = 0, sackAngV = 0;

uint16_t sackEff() { return sackPts; }   // gain = min(18, pts / 4)
uint8_t sackPotential() { return trnPotential(sackEff(), 4); }   // trainStrength divides by 4

// 0..1000 along the meter, ping-pong
uint16_t sackNeedle(uint32_t now) {
  uint32_t ph = (now - sackMeterT0) % (2 * SACK_SWEEP_MS);
  return (uint16_t)(ph < SACK_SWEEP_MS ? ph * 1000 / SACK_SWEEP_MS
                                       : (2 * SACK_SWEEP_MS - ph) * 1000 / SACK_SWEEP_MS);
}
uint16_t sackZone() {
  int z = SACK_ZONE0 - (int)trnCombo * SACK_ZONE_STEP;
  return (uint16_t)(z < SACK_ZONE_MIN ? SACK_ZONE_MIN : z);
}
bool sackInZone(uint32_t now) {
  int d = (int)sackNeedle(now) - 500;
  return (d < 0 ? -d : d) <= (int)sackZone();
}
int sackBagX() {
  int off = (int)(sinf(sackAng) * 120);
  if (off > 60) off = 60;
  if (off < -60) off = -60;
  return SACK_X + off;
}

void startSack() {
  if (pet.isEgg() || pet.sleeping || pet.ceremony) return;
  trnFxReset();
  uint32_t now = millis();
  sackOpen = true;
  trnGoAt = now + TRN_INTRO_MS;
  sackUntil = trnGoAt + SACK_MS;
  sackOverUntil = 0;
  sackHits = 0;
  sackPts = sackCrits = sackSpots = 0;
  sackGain = 0;
  sackNewHi = false;
  sackMeterT0 = trnGoAt;
  sackLastTap = 0;
  sackFeverUntil = 0;
  sackSpotAt = sackSpotUntil = 0;
  sackNextSpot = trnGoAt + 2000;
  sackPunchAt = sackCritAt = 0;
  sackAng = sackAngV = 0;
}

uint8_t sackTap(int16_t x, int16_t y) {
  uint32_t now = millis();
  if (!sackOpen || sackOverUntil || trnInIntro(now) || (int32_t)(now - sackUntil) >= 0)
    return SACK_NONE;
  sackHits++;
  bool ready = !sackLastTap || now - sackLastTap >= SACK_WINDUP_MS;
  bool fever = (int32_t)(now - sackFeverUntil) < 0;
  sackLastTap = now;
  sackPunchAt = now;
  uint8_t kind;
  uint16_t pts = 1;
  int bx = sackBagX();
  char b[16];
  if (fever) {
    kind = SACK_FEVERHIT;
    pts = 2;
    sackAngV += 0.07f;
    trnBurst(bx - SACK_W / 2, y, UI_BAR_WARN, 3, 4.0f);
  } else if (!ready) {
    kind = SACK_JAB;
    sackAngV += 0.045f;
  } else if (sackInZone(now)) {
    kind = SACK_CRIT;
    trnComboUp();
    sackCrits++;
    pts = 2 + trnMult(trnCombo);
    sackCritAt = now;
    sackAngV += 0.20f;
    trnJolt(7, 150);
    trnBurst(bx - SACK_W / 2 + 6, SACK_TOP + 60, UI_BAR_WARN, 10, 6.0f);
    trnBurst(bx - SACK_W / 2 + 6, SACK_TOP + 60, UI_WHITE, 6, 5.0f);
    trnPopup(bx - 30, SACK_TOP + 84, TL(S_TRN_CRIT), UI_BAR_WARN, 3);   // on the impact
    sfxPlay(SFX_HIT);
    if (trnCombo >= SACK_FEVER_AT) {
      sackFeverUntil = now + SACK_FEVER_MS;
      trnCombo = 0;
      sfxPlay(SFX_SUPER);
    }
  } else {
    kind = SACK_OFF;
    trnComboBreak(bx, SACK_TOP + 40);
    sackAngV += 0.06f;
  }
  // the weak spot: anywhere a finger lands near it, whatever the timing
  if ((int32_t)(now - sackSpotUntil) < 0 && (int32_t)(now - sackSpotAt) >= 0) {
    int dx = x - (bx + sackSpotDX), dy = y - (SACK_TOP + sackSpotDY);
    if (dx * dx + dy * dy <= SACK_SPOT_HIT * SACK_SPOT_HIT) {
      pts += SACK_SPOT_PTS;
      sackSpots++;
      sackSpotUntil = now;
      kind |= SACK_SPOT;
      trnBurst(bx + sackSpotDX, SACK_TOP + sackSpotDY, C565(0x9f, 0xe8, 0xff), 12, 6.5f);
      trnPopup(bx + sackSpotDX, SACK_TOP + sackSpotDY - 20, TL(S_TRN_WEAK), C565(0x9f, 0xe8, 0xff), 2);
      trnJolt(5, 120);
      sfxPlay(SFX_BEAM);
    }
  }
  sackPts += pts;
  if (pts > 1 || fever) {
    snprintf(b, sizeof(b), "+%u", pts);
    trnPopup(x < 60 ? 60 : (x > 406 ? 406 : x), y - 24, b,
             (kind & 7) == SACK_CRIT ? UI_BAR_WARN : UI_WHITE, 2);
  }
  return kind;
}

void renderSack() {
  uint32_t now = millis();
  drawGameScene();
  bool night = sceneHour() < 6 || sceneHour() >= 20;
  uint16_t ink = night ? UI_INK_NIGHT : UI_INK;

  if (sackOverUntil) {
    if ((int32_t)(now - sackOverUntil) > 0) { sackOpen = false; return; }
    trnPet(PMD_POSE, SACK_PET_X, 400, now - sackOverAt, true, 3);
    char g[24];
    if (sackGain || !sackPotential()) snprintf(g, sizeof(g), T(S_STR_GAIN_FMT), sackGain);
    else snprintf(g, sizeof(g), "%s", TL(S_TRN_MAXED));
    trnDrawResults(now, sackOverAt, g, UI_BAR_BAD, sackPts, sackHits, sackNewHi, pet.strHi, nullptr);
    trnDrawFx(now);
    gfx->flush();
    return;
  }

  // the 10 s are up: train, grade, show the card
  if ((int32_t)(now - sackUntil) >= 0) {
    sackNewHi = (sackHits > pet.strHi);
    sackGain = pet.trainStrength(sackHits, sackEff());
    trnGrade = trnGradeOf(sackPotential());
    sfxPlay(sackNewHi ? SFX_MEDAL : SFX_PLAY);
    sackOverAt = now;
    sackOverUntil = now + 3500;
    if (trnGrade >= 2) trnBurst(CX, 140, UI_BAR_WARN, 16, 7.0f);
    gfx->flush();
    return;
  }

  bool intro = trnInIntro(now);
  bool fever = (int32_t)(now - sackFeverUntil) < 0;
  int jx = trnJx(now), jy = trnJy(now);

  // the bag swings like a pendulum; a crit sends it flying
  sackAngV += -sackAng * 0.10f;
  sackAngV *= 0.88f;
  sackAng += sackAngV;
  if (!intro && !sackSpotUntil && (int32_t)(now - sackNextSpot) >= 0) {
    sackSpotAt = now;
    sackSpotUntil = now + SACK_SPOT_MS;
    sackSpotDX = (int16_t)(random(45) - 22);
    sackSpotDY = (int16_t)(30 + random(80));
  }
  if (sackSpotUntil && (int32_t)(now - sackSpotUntil) >= 0) {   // gone (hit or timed out)
    sackSpotUntil = 0;
    sackNextSpot = now + 1200 + random(1300);
  }

  int bx = sackBagX() + jx, top = SACK_TOP + jy;
  // hook, chain (a line from the hook to the swinging bag), bag
  gfx->fillRect(SACK_X - 12, 86, 24, 6, ink);
  for (int k = -2; k <= 2; k++) gfx->drawLine(SACK_X + k, 92, bx + k, top, ink);
  bool flash = sackCritAt && now - sackCritAt < 90;          // the impact frame
  uint16_t body = flash ? UI_WHITE : fever ? C565(0xe8, 0x8a, 0x2a) : C565(0xb5, 0x3a, 0x3a);
  uint16_t dark = fever ? C565(0xb0, 0x5a, 0x10) : C565(0x7e, 0x28, 0x28);
  gfx->fillRoundRect(bx - SACK_W / 2, top, SACK_W, SACK_H, 26, body);
  gfx->fillRoundRect(bx - SACK_W / 2, top, SACK_W, 22, 18, dark);
  gfx->fillRect(bx - SACK_W / 2, top + 62, SACK_W, 4, dark);
  gfx->drawRoundRect(bx - SACK_W / 2, top, SACK_W, SACK_H, 26, ink);
  if (flash) {
    gfx->drawCircle(bx, top + 60, 70, UI_WHITE);
    gfx->drawCircle(bx, top + 60, 76, UI_BAR_WARN);
  }
  if (fever && (now / 60) % 2 == 0)   // flames licking up the bag
    trnBurst(bx - SACK_W / 2 + (int)random(SACK_W), top + SACK_H - 10, (now / 120) & 1 ? UI_BAR_BAD : UI_BAR_WARN, 1, 2.5f);
  // the weak spot: a pulsing cyan target on the bag
  if (sackSpotUntil && (int32_t)(now - sackSpotAt) >= 0) {
    int sx = bx + sackSpotDX, sy = top + sackSpotDY;
    int pr = 14 + (int)((now / 70) % 4) * 2;
    gfx->fillCircle(sx, sy, pr + 6, C565(0x2a, 0x6a, 0x9a));
    gfx->fillCircle(sx, sy, pr, C565(0x9f, 0xe8, 0xff));
    gfx->fillCircle(sx, sy, 6, UI_WHITE);
  }

  // the power meter: gold zone in the middle, the needle sweeping across it
  int mx = CX - SACK_METER_W / 2 + jx / 2, my = SACK_METER_Y;
  gfx->fillRoundRect(mx - 3, my - 3, SACK_METER_W + 6, 24, 9, ink);
  gfx->fillRoundRect(mx, my, SACK_METER_W, 18, 7, fever ? UI_BAR_WARN : UI_TRACK);
  int zh = (int)sackZone() * SACK_METER_W / 1000;
  if (!fever) {
    gfx->fillRect(CX + jx / 2 - zh, my, zh * 2, 18, UI_BAR_WARN);
    gfx->fillRect(CX + jx / 2 - 2, my, 4, 18, UI_WHITE);
  }
  bool ready = !sackLastTap || now - sackLastTap >= SACK_WINDUP_MS;
  int nx = mx + (int)((uint32_t)sackNeedle(now) * SACK_METER_W / 1000);
  uint16_t nc = (intro || !ready) ? 0x8410 : (sackInZone(now) ? UI_BAR_BAD : UI_WHITE);
  gfx->fillRect(nx - 3, my - 8, 6, 34, nc);
  gfx->fillTriangle(nx - 8, my - 14, nx + 8, my - 14, nx, my - 4, nc);
  gfx->drawRect(nx - 4, my - 9, 8, 36, ink);

  // the creature throwing the punches
  uint32_t atk = trnActMs(PMD_ATTACK);
  if (fever) trnPet(PMD_ATTACK, SACK_PET_X, 400, now, true, 3);
  else if (sackPunchAt && now - sackPunchAt < atk) trnPet(PMD_ATTACK, SACK_PET_X, 400, now - sackPunchAt, false, 3);
  else trnPet(PMD_IDLE, SACK_PET_X, 400, now, true, 3);

  trnHud(sackPts, now, sackUntil, SACK_MS, ink);
  if (fever) trnTextO(TL(S_TRN_FEVER), CX, 96, (now - (sackFeverUntil - SACK_FEVER_MS) < 120) ? 4 : 3,
                      (now / 150) & 1 ? UI_BAR_BAD : UI_BAR_WARN);
  trnText(TL(S_TRN_HBAG), CX, 300, 2, ink);
  trnDrawFx(now);
  trnDrawIntro(now, nullptr);   // the bag's hint is always on screen
  gfx->flush();
}

// =====================================================================
// SPE -- the reaction test
// =====================================================================
// A target appears; tap it before it shrinks away. The window tightens with
// every hit (spdLife). On top of that the session changes as it goes:
//  - GOLD targets (SPD_GOLD_PCT): smaller, 70 % of the window, worth 3
//  - from SPD_BOMB_FROM a BOMB can appear beside a real target: tapping it
//    costs SPD_BOMB_COST points and the combo; left alone it fizzles harmlessly
//  - from SPD_DOUBLE_FROM a second real target can appear at once
//  - from SPD_MOVE_FROM targets can drift and bounce
// A good target is worth its base (1, gold 3) + trnMult(combo) - 1. A target
// that expires, or a tap on empty panel, breaks the combo (no points lost).
#define SPD_MS 15000UL       // session length
#define SPD_LIFE0 1100       // first target's window, ms
#define SPD_LIFE_MIN 380
#define SPD_R 46             // target radius
#define SPD_GOLD_R 34
#define SPD_BOMB_R 40
#define SPD_HIT_PAD 14       // touch slop beyond the drawn radius
#define SPD_GOLD_PCT 12
#define SPD_BOMB_PCT 25
#define SPD_DOUBLE_PCT 30
#define SPD_MOVE_PCT 50
#define SPD_BOMB_FROM 3000UL
#define SPD_DOUBLE_FROM 5000UL
#define SPD_MOVE_FROM 8000UL
#define SPD_BOMB_LIFE 1000
#define SPD_BOMB_COST 3
#define SPD_FIELD 150        // targets spawn within this radius of the centre
#define SPD_SLOTS 2

#define SPD_K_NONE 0
#define SPD_K_RED 1
#define SPD_K_GOLD 2
#define SPD_K_BOMB 3
// spdTap's return
#define SPD_T_NONE 0
#define SPD_T_HIT 1
#define SPD_T_GOLD 2
#define SPD_T_BOMB 3
#define SPD_T_WHIFF 4

uint8_t spdTK[SPD_SLOTS];
float spdTX[SPD_SLOTS], spdTY[SPD_SLOTS], spdTVX[SPD_SLOTS], spdTVY[SPD_SLOTS];
uint32_t spdTBorn[SPD_SLOTS];
uint16_t spdTLife[SPD_SLOTS];
uint16_t spdPts = 0, spdGold = 0, spdBombs = 0, spdWhiffs = 0;
static uint32_t spdLastFrame = 0, spdPetAt = 0, spdOverAt = 0;
static float spdPetX = CX;
static int16_t spdPetGoal = CX;
static bool spdPetHurt = false;

uint16_t spdEff() { return (uint16_t)(spdPts * 2 / 3); }   // gain = min(18, eff / 2) ~ pts / 3
uint8_t spdPotential() { return trnPotential(spdEff(), 2); }   // trainSpeed divides by 2

static uint16_t spdLife() {
  int life = SPD_LIFE0 - spdHits * 32;
  return life < SPD_LIFE_MIN ? SPD_LIFE_MIN : life;
}
static int spdRadius(uint8_t k) { return k == SPD_K_GOLD ? SPD_GOLD_R : k == SPD_K_BOMB ? SPD_BOMB_R : SPD_R; }
static bool spdGood(uint8_t k) { return k == SPD_K_RED || k == SPD_K_GOLD; }

void spdSpawnAt(uint8_t i, uint8_t kind) {
  uint32_t now = millis();
  uint32_t e = trnInIntro(now) ? 0 : now - trnGoAt;
  // Keep the whole target inside the bezel (an angle and a radius, not a
  // square that clips at the corners), and off the other target.
  float x = CX, y = CY;
  for (int tries = 0; tries < 8; tries++) {
    float a = random(360) * 0.01745f;
    int rad = random(SPD_FIELD);
    x = CX + cosf(a) * rad;
    y = CY + sinf(a) * rad;
    uint8_t o = i ^ 1;
    if (!spdTK[o]) break;
    float dx = x - spdTX[o], dy = y - spdTY[o];
    if (dx * dx + dy * dy > 110.0f * 110.0f) break;
  }
  spdTK[i] = kind;
  spdTX[i] = x; spdTY[i] = y;
  spdTVX[i] = spdTVY[i] = 0;
  if (e >= SPD_MOVE_FROM && random(100) < SPD_MOVE_PCT) {
    float a = random(360) * 0.01745f, sp = (80 + random(60)) / 1000.0f;   // px per ms
    spdTVX[i] = cosf(a) * sp; spdTVY[i] = sinf(a) * sp;
  }
  spdTBorn[i] = now;
  uint16_t life = spdLife();
  spdTLife[i] = kind == SPD_K_BOMB ? SPD_BOMB_LIFE : kind == SPD_K_GOLD ? (uint16_t)(life * 7 / 10) : life;
}
// kept for the old name: a fresh single target in slot 0
void spdSpawn() { spdSpawnAt(0, SPD_K_RED); }

static uint8_t spdGoodKind() { return random(100) < SPD_GOLD_PCT ? SPD_K_GOLD : SPD_K_RED; }

// After anything leaves the field: there is always one real target, and the
// slot beside it may get a bomb (decoy) or, later on, a second real one.
static void spdNext() {
  uint32_t now = millis();
  uint32_t e = trnInIntro(now) ? 0 : now - trnGoAt;
  bool good = spdGood(spdTK[0]) || spdGood(spdTK[1]);
  for (uint8_t i = 0; i < SPD_SLOTS; i++) {
    if (spdTK[i]) continue;
    if (!good) { spdSpawnAt(i, spdGoodKind()); good = true; continue; }
    // (never two bombs: with two slots and a real target required, the other
    // slot here IS that real target -- training_ui_test's census pins it)
    int r = random(100);
    if (e >= SPD_BOMB_FROM && r < SPD_BOMB_PCT) spdSpawnAt(i, SPD_K_BOMB);
    else if (e >= SPD_DOUBLE_FROM && r < SPD_BOMB_PCT + SPD_DOUBLE_PCT) spdSpawnAt(i, spdGoodKind());
  }
}

void startSpeedGame() {
  if (pet.isEgg() || pet.sleeping || pet.ceremony) return;
  trnFxReset();
  uint32_t now = millis();
  spdOpen = true;
  trnGoAt = now + TRN_INTRO_MS;
  spdUntil = trnGoAt + SPD_MS;
  spdOverUntil = 0;
  spdHits = 0;
  spdMisses = 0;
  spdGain = 0;
  spdNewHi = false;
  spdPts = spdGold = spdBombs = spdWhiffs = 0;
  for (uint8_t i = 0; i < SPD_SLOTS; i++) spdTK[i] = SPD_K_NONE;
  spdLastFrame = now;
  spdPetX = CX; spdPetGoal = CX; spdPetAt = 0; spdPetHurt = false;
}

uint8_t spdTap(int16_t x, int16_t y) {
  uint32_t now = millis();
  if (!spdOpen || spdOverUntil || trnInIntro(now) || (int32_t)(now - spdUntil) >= 0) return SPD_T_NONE;
  // a real target wins over a bomb it overlaps
  int hit = -1;
  for (int pass = 0; pass < 2 && hit < 0; pass++)
    for (uint8_t i = 0; i < SPD_SLOTS; i++) {
      uint8_t k = spdTK[i];
      if (!k || (pass == 0) != spdGood(k)) continue;
      int r = spdRadius(k) + SPD_HIT_PAD;
      float dx = x - spdTX[i], dy = y - spdTY[i];
      if (dx * dx + dy * dy <= (float)r * r) { hit = i; break; }
    }
  if (hit < 0) {                       // empty panel: a whiff
    spdWhiffs++;
    trnCross(x, y);
    trnComboBreak(x, y);
    return SPD_T_WHIFF;
  }
  uint8_t k = spdTK[hit];
  int tx = (int)spdTX[hit], ty = (int)spdTY[hit];
  spdTK[hit] = SPD_K_NONE;
  spdPetGoal = (int16_t)(tx < 150 ? 150 : tx > 316 ? 316 : tx);
  spdPetAt = now;
  char b[12];
  if (k == SPD_K_BOMB) {
    spdBombs++;
    spdPts = spdPts > SPD_BOMB_COST ? spdPts - SPD_BOMB_COST : 0;
    trnCombo = 0;
    spdPetHurt = true;
    snprintf(b, sizeof(b), "-%u", SPD_BOMB_COST);
    trnPopup(tx, ty - 20, b, UI_BAR_BAD, 3);
    trnBurst(tx, ty, UI_BAR_BAD, 12, 7.0f);
    trnBurst(tx, ty, 0x4208, 8, 5.0f);
    trnJolt(10, 240);
    sfxPlay(SFX_DENY);
    spdNext();
    return SPD_T_BOMB;
  }
  spdHits++;
  spdPetHurt = false;
  trnComboUp();
  uint16_t pts = (k == SPD_K_GOLD ? 3 : 1) + trnMult(trnCombo) - 1;
  spdPts += pts;
  snprintf(b, sizeof(b), "+%u", pts);
  if (k == SPD_K_GOLD) {
    spdGold++;
    trnPopup(tx, ty - 20, b, UI_BAR_WARN, 3);
    trnBurst(tx, ty, UI_BAR_WARN, 14, 6.5f);
    trnJolt(4, 100);
    sfxPlay(SFX_SUPER);
  } else {
    trnPopup(tx, ty - 20, b, UI_WHITE, 2);
    trnBurst(tx, ty, UI_BAR_BAD, 6, 5.0f);
    trnBurst(tx, ty, UI_WHITE, 4, 4.0f);
  }
  spdNext();
  return k == SPD_K_GOLD ? SPD_T_GOLD : SPD_T_HIT;
}

static void spdDrawTarget(uint8_t i, uint32_t now, int jx, int jy) {
  uint8_t k = spdTK[i];
  uint32_t age = now - spdTBorn[i];
  int life = spdTLife[i] ? spdTLife[i] : 1;
  int R = spdRadius(k);
  int r = R - (int)((uint32_t)R * age / life / 2);
  if (r < 8) r = 8;
  int x = (int)spdTX[i] + jx, y = (int)spdTY[i] + jy;
  if (spdTVX[i] != 0 || spdTVY[i] != 0) {    // a moving target leaves a short trail
    gfx->fillCircle(x - (int)(spdTVX[i] * 120), y - (int)(spdTVY[i] * 120), r / 3, UI_TRACK);
    gfx->fillCircle(x - (int)(spdTVX[i] * 60), y - (int)(spdTVY[i] * 60), r / 2, UI_TRACK);
  }
  if (k == SPD_K_BOMB) {
    gfx->fillCircle(x, y, r, C565(0x2c, 0x2c, 0x36));
    gfx->fillCircle(x - r / 3, y - r / 3, r / 4, C565(0x70, 0x70, 0x80));
    gfx->fillRect(x - 3, y - r - 10, 6, 12, C565(0x8a, 0x6a, 0x40));   // the fuse
    gfx->fillCircle(x, y - r - 12, 5 + (int)((now / 60) & 1) * 2, (now / 60) & 1 ? UI_BAR_WARN : UI_BAR_BAD);
    gfx->drawCircle(x, y, r, UI_BAR_BAD);
    gfx->drawCircle(x, y, r + 1, UI_BAR_BAD);
    return;
  }
  uint16_t outer = k == SPD_K_GOLD ? UI_BAR_WARN : UI_BAR_BAD;
  gfx->fillCircle(x, y, r, outer);
  gfx->fillCircle(x, y, r * 2 / 3, UI_WHITE);
  gfx->fillCircle(x, y, r / 3, outer);
  if (k == SPD_K_GOLD) {                      // a twinkle that turns
    int s = r + 8, d = (now / 90) & 1 ? s : s * 7 / 10;
    gfx->drawLine(x - d, y, x + d, y, UI_WHITE);
    gfx->drawLine(x, y - d, x, y + d, UI_WHITE);
  }
}

void renderSpeed() {
  uint32_t now = millis();
  drawGameScene();
  bool night = sceneHour() < 6 || sceneHour() >= 20;
  uint16_t ink = night ? UI_INK_NIGHT : UI_INK;

  if (spdOverUntil) {
    if ((int32_t)(now - spdOverUntil) > 0) { spdOpen = false; return; }
    trnPet(PMD_POSE, CX, 430, now - spdOverAt, true, 2);
    char g[24];
    if (spdGain || !spdPotential()) snprintf(g, sizeof(g), T(S_SPD_GAIN_FMT), spdGain);
    else snprintf(g, sizeof(g), "%s", TL(S_TRN_MAXED));
    trnDrawResults(now, spdOverAt, g, UI_BAR_WARN, spdPts, spdHits, spdNewHi, pet.spdHi, nullptr);
    trnDrawFx(now);
    gfx->flush();
    return;
  }

  // session over?
  if ((int32_t)(now - spdUntil) >= 0) {
    spdNewHi = (spdHits > pet.spdHi);
    spdGain = pet.trainSpeed(spdHits, spdEff());
    trnGrade = trnGradeOf(spdPotential());
    sfxPlay(spdNewHi ? SFX_MEDAL : SFX_PLAY);
    spdOverAt = now;
    spdOverUntil = now + 3500;
    if (trnGrade >= 2) trnBurst(CX, 140, UI_BAR_WARN, 16, 7.0f);
    gfx->flush();
    return;
  }

  bool intro = trnInIntro(now);
  uint32_t dt = now - spdLastFrame;
  if (dt > 200) dt = 200;
  spdLastFrame = now;
  if (!intro) {
    if (!spdGood(spdTK[0]) && !spdGood(spdTK[1])) spdNext();   // the very first, or after a lone bomb
    for (uint8_t i = 0; i < SPD_SLOTS; i++) {
      if (!spdTK[i]) continue;
      if (spdTVX[i] != 0 || spdTVY[i] != 0) {          // drift, bouncing inside the field
        spdTX[i] += spdTVX[i] * dt;
        spdTY[i] += spdTVY[i] * dt;
        float dx = spdTX[i] - CX, dy = spdTY[i] - CY, d = sqrtf(dx * dx + dy * dy);
        if (d > SPD_FIELD + 20) {
          float nx = dx / d, ny = dy / d, dot = spdTVX[i] * nx + spdTVY[i] * ny;
          if (dot > 0) { spdTVX[i] -= 2 * dot * nx; spdTVY[i] -= 2 * dot * ny; }
          spdTX[i] = CX + nx * (SPD_FIELD + 20); spdTY[i] = CY + ny * (SPD_FIELD + 20);
        }
      }
      if (now - spdTBorn[i] > spdTLife[i]) {           // expired
        if (spdGood(spdTK[i])) {
          spdMisses++;
          trnCross((int)spdTX[i], (int)spdTY[i]);
          trnComboBreak((int)spdTX[i], (int)spdTY[i]);
        } else {
          trnBurst((int)spdTX[i], (int)spdTY[i], 0x8410, 5, 2.5f);   // the bomb fizzles
        }
        spdTK[i] = SPD_K_NONE;
        spdNext();
      }
    }
  }

  int jx = trnJx(now), jy = trnJy(now);
  if (!intro)
    for (uint8_t i = 0; i < SPD_SLOTS; i++)
      if (spdTK[i] == SPD_K_BOMB) spdDrawTarget(i, now, jx, jy);   // bombs under real targets
  if (!intro)
    for (uint8_t i = 0; i < SPD_SLOTS; i++)
      if (spdGood(spdTK[i])) spdDrawTarget(i, now, jx, jy);

  // the creature dashes under each target you hit
  float step = (spdPetGoal - spdPetX) * 0.35f;
  if (step > 22) step = 22;
  if (step < -22) step = -22;
  spdPetX += step;
  uint8_t act = PMD_IDLE;
  uint32_t at = now - spdPetAt;
  bool loop = true;
  if (spdPetAt && spdPetHurt && at < 500) { act = PMD_HURT; loop = false; }
  else if (step > 2) act = PMD_WALKR;
  else if (step < -2) act = PMD_WALKL;
  else if (spdPetAt && at < trnActMs(PMD_ATTACK)) { act = PMD_ATTACK; loop = false; }
  trnPet(act, (int)spdPetX, 430, loop ? now : at, loop, 2);

  trnHud(spdPts, now, spdUntil, SPD_MS, ink);
  trnDrawFx(now);
  trnDrawIntro(now, TL(S_TRN_HSPD));
  gfx->flush();
}

// =====================================================================
// DEF -- the ball game
// =====================================================================
// Keep the pokeball up: 3 balls on the floor or GAME_MS, whichever comes
// first. Every hit is worth 1 + trnMult(combo) - 1, where the combo is hits in
// a row without ANY ball touching the floor. On top of that:
//  - a GOLDEN ball (GAME_GOLD_PCT of respawns, from GAME_GOLD_FROM hits) is
//    worth GAME_GOLD_PTS more per hit until it lands
//  - a berry or a star floats across now and then: tap it for GAME_ITEM_PTS
//    (it is not a hit and does not touch the combo)
//  - at GAME_BALL2_AT hits a SECOND ball drops in
//  - from GAME_WIND_FROM ms into the session, gusts push the balls sideways
#define GAME_BALLS 2
#define GAME_BALL_HIT 74       // touch radius of a ball (drawn 24)
#define GAME_GOLD_PCT 20
#define GAME_GOLD_FROM 3
#define GAME_GOLD_PTS 2
#define GAME_ITEM_PTS 3
#define GAME_ITEM_HIT 40
#define GAME_BALL2_AT 12
#define GAME_WIND_FROM 12000UL
#define GAME_WIND_MS 1400UL
#define GAME_WIND_ACC 0.16f

float gBX[GAME_BALLS], gBY[GAME_BALLS], gBVX[GAME_BALLS], gBVY[GAME_BALLS];
bool gBOn[GAME_BALLS], gBGold[GAME_BALLS];
uint16_t gamePts = 0, gameGolds = 0, gameItems = 0;
uint8_t gameDefGain = 0;
// the floating pickup: 0 none, 1 berry, 2 star
uint8_t gItKind = 0;
float gItX = 0, gItY0 = 0;
int8_t gItDir = 1;
uint32_t gItNext = 0;
int8_t gWindDir = 0;
uint32_t gWindFrom = 0, gWindUntil = 0, gWindNext = 0;
static uint32_t gameLastFrame = 0, gameHopAt = 0, gameOverAt = 0;

uint16_t gameEff() { return (uint16_t)(gamePts * 2 / 3); }   // gain = min(18, eff / 2) ~ pts / 3
uint8_t gamePotential() { return trnPotential(gameEff(), 2); }   // playResult divides by 2

void ballSpawn(uint8_t i) {
  gBX[i] = 150 + random(166);
  gBY[i] = 96;
  float sp = 1.6f + gameScore * 0.05f;  // mas viva segun avanzas
  if (sp > 4.0f) sp = 4.0f;
  gBVX[i] = random(2) ? sp : -sp;
  gBVY[i] = 0;
  gBGold[i] = gameScore >= GAME_GOLD_FROM && random(100) < GAME_GOLD_PCT;
  gBOn[i] = true;
}
void respawnBall() { ballSpawn(0); }   // the old name: the first ball

void startGame() {
  if (pet.isEgg() || pet.sleeping || pet.ceremony) return;
  trnFxReset();
  uint32_t now = millis();
  gameOpen = true;
  gameOverUntil = 0;
  trnGoAt = now + TRN_INTRO_MS;
  gameUntil = trnGoAt + GAME_MS;
  gameScore = 0;
  gameMisses = 0;
  gameNewHi = false;
  hitTime = 0;
  gamePetX = 233;
  gamePts = gameGolds = gameItems = 0;
  gameDefGain = 0;
  for (uint8_t i = 0; i < GAME_BALLS; i++) gBOn[i] = false;
  ballSpawn(0);
  gItKind = 0;
  gItNext = trnGoAt + 4000;
  gWindDir = 0;
  gWindFrom = gWindUntil = 0;
  gWindNext = trnGoAt + GAME_WIND_FROM;
  gameLastFrame = now;
  gameHopAt = 0;
}

static float gameItY(uint32_t now) { return gItY0 + sinf(now * 0.006f) * 12; }

void gameTap(int16_t x, int16_t y) {
  if (gameOverUntil) return;
  uint32_t now = millis();
  bool intro = trnInIntro(now);
  // The balls are checked BEFORE the quit strip. A ball bounces inside a circle
  // of radius 205 about the centre, so it reaches y=28 -- well inside the y<72
  // header. Reaching up to hit a high ball used to abandon the game instead,
  // silently forfeiting the score, the training and the record.
  int best = -1;
  float bestD = (float)GAME_BALL_HIT * GAME_BALL_HIT;
  if (!intro)
    for (uint8_t i = 0; i < GAME_BALLS; i++) {
      if (!gBOn[i]) continue;
      float dx = gBX[i] - x, dy = gBY[i] - y, d = dx * dx + dy * dy;
      if (d < bestD) { bestD = d; best = i; }
    }
  if (best >= 0) {  // toque a la bola!
    uint8_t i = (uint8_t)best;
    float dx = gBX[i] - x;
    gameScore++;
    trnComboUp();
    uint16_t pts = 1 + trnMult(trnCombo) - 1 + (gBGold[i] ? GAME_GOLD_PTS : 0);
    gamePts += pts;
    if (gBGold[i]) gameGolds++;
    char b[8];
    snprintf(b, sizeof(b), "+%u", pts);
    trnPopup((int)gBX[i], (int)gBY[i] - 34, b, gBGold[i] ? UI_BAR_WARN : UI_WHITE, gBGold[i] ? 3 : 2);
    trnBurst((int)gBX[i], (int)gBY[i], gBGold[i] ? UI_BAR_WARN : C565(0xff, 0xe7, 0x9f), gBGold[i] ? 10 : 5, 5.0f);
    // golpe mas suave: impulso moderado que crece poco a poco con la puntuacion
    float lift = 6.6f + (gameScore > 16 ? 3.5f : gameScore * 0.22f);
    gBVY[i] = -lift;
    gBVX[i] += dx * 0.12f;
    if (gBVX[i] > 6.5f) gBVX[i] = 6.5f;
    if (gBVX[i] < -6.5f) gBVX[i] = -6.5f;
    hitX = gBX[i];
    hitY = gBY[i];
    hitTime = now;
    if (trnCombo % 5 == 0) gameHopAt = now;     // the creature cheers the streak
    if (gameScore == GAME_BALL2_AT && !gBOn[1]) {
      ballSpawn(1);
      gBX[1] = gBX[0] < CX ? 300 : 166;
      gBGold[1] = false;
      trnBanner(TL(S_TRN_2BALL), UI_BAR_WARN, 900);
      sfxPlay(SFX_LEVEL);
    }
    return;
  }
  if (!intro && gItKind) {          // the floating pickup
    float dx = gItX - x, dy = gameItY(now) - y;
    if (dx * dx + dy * dy <= GAME_ITEM_HIT * GAME_ITEM_HIT) {
      gamePts += GAME_ITEM_PTS;
      gameItems++;
      char b[8];
      snprintf(b, sizeof(b), "+%u", GAME_ITEM_PTS);
      trnPopup((int)gItX, (int)gameItY(now) - 30, b, C565(0x9f, 0xe8, 0xff), 3);
      trnBurst((int)gItX, (int)gameItY(now), gItKind == 2 ? UI_BAR_WARN : UI_BAR_BAD, 12, 6.0f);
      sfxPlay(SFX_HEART);
      gItKind = 0;
      gItNext = now + 3500 + random(1500);
      return;
    }
  }
  if (y < 72) {  // tocar la cabecera = salir, conservando lo ganado
    leaveGame();
    return;
  }
}

static void gameEnd(uint32_t now) {
  gameNewHi = (gameScore > pet.gameHi);
  gameDefGain = pet.playResult(gameScore, gameEff());  // record, joy, and DEF
  trnGrade = trnGradeOf(gamePotential());
  sfxPlay(gameNewHi && gameScore > 0 ? SFX_MEDAL : SFX_LEVEL);
  gameOverAt = now;
  gameOverUntil = now + 4000;
  if (trnGrade >= 2) trnBurst(CX, 140, UI_BAR_WARN, 16, 7.0f);
}

void stepGame() {
  uint32_t now = millis();
  // the clock, or three misses, whichever lands first
  if (gameUntil && (int32_t)(now - gameUntil) >= 0 && !gameOverUntil) { gameEnd(now); return; }
  uint32_t e = now - trnGoAt;
  // wind: a gust every few seconds in the back part of the session
  if (e >= GAME_WIND_FROM && (int32_t)(now - gWindNext) >= 0) {
    gWindFrom = now;
    gWindUntil = now + GAME_WIND_MS;
    gWindDir = random(2) ? 1 : -1;
    gWindNext = gWindUntil + 1800 + random(1200);
  }
  bool windy = (int32_t)(now - gWindUntil) < 0;
  float grav = 0.40f + gameScore * 0.013f;  // cae un poco mas rapido cada vez
  if (grav > 0.80f) grav = 0.80f;
  int low = -1;
  for (uint8_t i = 0; i < GAME_BALLS; i++) {
    if (!gBOn[i]) continue;
    gBVY[i] += grav;
    if (windy) gBVX[i] += gWindDir * GAME_WIND_ACC;
    gBX[i] += gBVX[i];
    gBY[i] += gBVY[i];
    // rebote en la pared circular
    float dx = gBX[i] - CX, dy = gBY[i] - CY;
    float d = sqrtf(dx * dx + dy * dy);
    if (d > 205) {
      float nx = dx / d, ny = dy / d;
      float dot = gBVX[i] * nx + gBVY[i] * ny;
      if (dot > 0) {
        gBVX[i] = (gBVX[i] - 2 * dot * nx) * 0.85f;
        gBVY[i] = (gBVY[i] - 2 * dot * ny) * 0.85f;
      }
      gBX[i] = CX + nx * 205;
      gBY[i] = CY + ny * 205;
    }
    if (gBY[i] > 384) {  // al suelo
      trnCross((int)gBX[i], 372);
      trnComboBreak((int)gBX[i], 372);
      trnJolt(5, 140);
      if (++gameMisses >= 3) { gameEnd(now); return; }
      ballSpawn(i);
    }
    if (low < 0 || gBY[i] > gBY[low]) low = i;
  }
  // the pickup drifts across, bobbing
  uint32_t dt = now - gameLastFrame;
  if (dt > 200) dt = 200;
  gameLastFrame = now;
  if (!gItKind && (int32_t)(now - gItNext) >= 0) {
    gItKind = (uint8_t)(1 + random(2));
    gItDir = random(2) ? 1 : -1;
    gItX = gItDir > 0 ? 50 : 416;
    gItY0 = 160 + random(90);
  }
  if (gItKind) {
    gItX += gItDir * 0.075f * dt;
    if (gItX < 30 || gItX > 436) { gItKind = 0; gItNext = now + 2500 + random(1500); }
  }
  // el bicho sigue a la bola que cae mas abajo
  if (low >= 0) {
    float chase = (gBX[low] - gamePetX) * 0.12f;
    if (chase > 7) chase = 7;
    if (chase < -7) chase = -7;
    gamePetX += chase;
  }
}

void renderGame() {
  // sin fillScreen(NEGRO): drawGameScene cubre los 466x466 completos. Si el
  // DMA del flush anterior aun lee el buffer, vera contenido valido (no negro
  // a medio pintar), que era el parpadeo a 25 fps.
  uint32_t now = millis();
  bool night = sceneHour() < 6 || sceneHour() >= 20;
  uint16_t ink = night ? UI_INK_NIGHT : UI_INK;

  if (gameOverUntil) {
    drawGameScene();
    if ((int32_t)(now - gameOverUntil) > 0) {
      gameOpen = false;
      return;
    }
    trnPet(PMD_POSE, (int)gamePetX, 394, now - gameOverAt, true, 3);
    char g[24];
    if (gameDefGain || !gamePotential()) snprintf(g, sizeof(g), TL(S_TRN_DEF), gameDefGain);
    else snprintf(g, sizeof(g), "%s", TL(S_TRN_MAXED));
    trnDrawResults(now, gameOverAt, g, UI_BAR_OK, gamePts, gameScore, gameNewHi, pet.gameHi,
                   gameScore >= 10 ? T(S_GREAT_JOY) : T(S_PLUS_JOY));
    trnDrawFx(now);
    gfx->flush();
    return;
  }

  drawGameScene();
  bool intro = trnInIntro(now);
  if (!intro) stepGame();
  if (gameOverUntil) { gfx->flush(); return; }   // it just ended: the card starts next frame
  int jx = trnJx(now), jy = trnJy(now);

  // wind streaks across the sky while a gust blows
  bool windy = (int32_t)(now - gWindUntil) < 0;
  if (windy) {
    for (int k = 0; k < 7; k++) {
      int yy = 130 + k * 32;
      int xx = (int)((now * 0.7f * gWindDir + k * 97) ) % 520;
      if (xx < 0) xx += 520;
      xx -= 30;
      gfx->drawFastHLine(xx, yy, 46, UI_WHITE);
      gfx->drawFastHLine(xx + 10, yy + 1, 30, UI_WHITE);
    }
    char w[16];
    snprintf(w, sizeof(w), gWindDir > 0 ? "%s >>" : "<< %s", TL(S_TRN_WIND));
    trnTextO(w, CX, 122, 2, UI_WHITE);
  }

  trnHud(gamePts, now, gameUntil, GAME_MS, ink);
  for (int i = 0; i < 3; i++) {   // lives
    if (i < 3 - gameMisses) gfx->fillCircle(CX - 28 + i * 28, 98, 6, UI_BAR_BAD);
    else gfx->drawCircle(CX - 28 + i * 28, 98, 6, UI_TRACK);
  }

  // the creature: chases the lowest ball, hops on every fifth link
  uint8_t act;
  uint32_t ht = now - gameHopAt;
  bool hop = gameHopAt && ht < 600;
  if (hop) act = pmd.has(PMD_HOP) ? PMD_HOP : PMD_POSE;
  else {
    int low = gBOn[1] && gBY[1] > gBY[0] ? 1 : 0;
    act = (gBX[low] > gamePetX + 4) ? PMD_WALKR : (gBX[low] < gamePetX - 4) ? PMD_WALKL : PMD_IDLE;
  }
  trnPet(act, (int)gamePetX, 394, hop ? ht : now, !hop, 3);

  // the floating berry / star
  if (gItKind && !intro) {
    int ix = (int)gItX, iy = (int)gameItY(now);
    gfx->fillCircle(ix, iy, 26, C565(0xff, 0xf6, 0xd0));
    if (gItKind == 2) trnStar(ix, iy, 20, UI_BAR_WARN);
    else drawMap(SPR_ICON_FOOD, 16, ix - 16, iy - 16, 2, false);
  }

  // anillo de impacto que se expande y desvanece (feedback suave del golpe)
  uint32_t hit = now - hitTime;
  if (hitTime && hit < 260) {
    int rad = 22 + (int)(hit / 6);
    gfx->drawCircle((int)hitX, (int)hitY, rad, C565(0xff, 0xe7, 0x9f));
    gfx->drawCircle((int)hitX, (int)hitY, rad - 2, C565(0xff, 0xd9, 0x8a));
  }

  // the pokeballs (a golden one wears a pulsing halo)
  for (uint8_t i = 0; i < GAME_BALLS; i++) {
    if (!gBOn[i]) continue;
    int bx = (int)gBX[i] + jx, by = (int)gBY[i] + jy;
    if (gBGold[i]) {
      gfx->fillCircle(bx, by, 30 + (int)((now / 80) % 3) * 2, UI_BAR_WARN);
      gfx->fillCircle(bx, by, 26, C565(0xff, 0xf1, 0xc4));
    }
    drawMap(SPR_ICON_PLAY, 16, bx - 24, by - 24, 3, false);
  }

  trnDrawFx(now);
  trnDrawIntro(now, TL(S_TRN_HBALL));
  gfx->flush();
}
