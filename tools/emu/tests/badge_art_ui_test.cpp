// Every ladder win earns a SPRITE, and every screen that shows a win draws it.
//
// Before this, Alola's eight and every region's Elite 4 and Champion were
// "just colours": a type-coloured disc on the win screen and the player card,
// a green "*" on a beaten ladder row, a bare count on the region chooser. The
// art is generated (tools/gen_badges.py upstream sheets, tools/gen_badges_extra.py
// original Alola badges + E4/Champion emblems) and every screen asks ONE
// function, badgeArtFor(). This test reads the art through badgeArtPixel() --
// the same lookup -- and then checks the PANEL: the pixels a screen drew must
// be that art, at the place and scale the screen puts it. "There is art" is not
// enough; CLAUDE.md trap 3 is exactly an assertion that passes on the wrong art.
//
// Driven through the real sketch: startTrainerBattle(), taps on the move grid,
// render().
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include "battle.h"
#include "trainers.h"
#include <cstdio>
#include <cstring>
#include <chrono>
#include <thread>
uint32_t g_seed = 4243; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX = 0, g_touchY = 0; volatile bool g_touchDown = false;
void FakeESP::restart() { exit(0); }
int FakeSerial::available() { return 0; }
String FakeSerial::readStringUntil(char) { return String(""); }
void setup(); void loop(); void render();
extern Pet pet;
extern Arduino_Canvas *gfx;
extern const char *const SCREEN_NAME[];
extern bool battleOpen, btlWon, btlOver, pickOpen, gymOpen, gymPick, gymHard, playerOpen;
extern Combatant btlYou, btlFoe;
extern uint8_t btlMsgCount, gymRegion, gymPage, playerPage, rpickPage;
extern uint16_t squadMask;
void startTrainerBattle(uint8_t idx, bool hard);
uint8_t uiCurrentScreen();
bool badgeArtExists(uint8_t region, uint8_t i);
int32_t badgeArtPixel(uint8_t region, uint8_t i, uint8_t x, uint8_t y);
void playerBadgeXY(int i, int *x, int *y);
#define BTL_CELL_X(i) (69 + ((i) % 2) * 168)
#define BTL_CELL_Y(i) (286 + ((i) / 2) * 52)
// where the screens put the art (TamaPoke.ino): the win screen's badge centre,
// the ladder row's badge, the chooser strip
#define WIN_BY 190
#define GYM_BADGE_X 372
#define GYM_ROW_Y(i) (110 + (i) * 50)
#define RPICK_X 74
#define RPICK_Y(i) (108 + (i) * 72)
#define RPICK_STRIP_X(k) (RPICK_X + 26 + (k) * 18)
#define RPICK_STRIP_DY 50

static int bad = 0;
static void ck(bool ok, const char *w) { printf("%s  %s\n", ok ? "PASS" : "FAIL", w); if (!ok) bad++; }
static void pump(int n) {
  for (int i = 0; i < n; i++) { std::this_thread::sleep_for(std::chrono::milliseconds(30)); loop(); }
}
static void click(int x, int y) {
  g_touchX = x; g_touchY = y; g_touchDown = true; emuFireInterrupt(); pump(5);
  g_touchDown = false; emuFireInterrupt(); pump(3);
}
static uint16_t px(int x, int y) { return gfx->buffer()[y * 466 + x]; }
static bool on(const char *scr) { render(); return !strcmp(SCREEN_NAME[uiCurrentScreen()], scr); }

// What share of slot i's opaque art pixels the panel shows in the art's own
// colour, with the art centred on (cx, cy) at `scale`. 1.0 = drawn exactly.
static double artMatch(uint8_t region, uint8_t i, int cx, int cy, int scale) {
  int n = 0, hit = 0;
  for (int y = 0; y < 32; y++)
    for (int x = 0; x < 32; x++) {
      int32_t c = badgeArtPixel(region, i, x, y);
      if (c < 0) continue;
      n++;
      int X = cx - 16 * scale + x * scale + scale / 2, Y = cy - 16 * scale + y * scale + scale / 2;
      if (px(X, Y) == (uint16_t)c) hit++;
    }
  return n ? (double)hit / n : 0;
}

// Fights the current battle to a WIN. The foes are knocked to 1 HP as they come
// out: this test is about what the win screen draws, not about balance.
static void winFight() {
  for (int taps = 0; battleOpen && !btlOver && taps < 300; taps++) {
    if (!btlFoe.fainted() && btlFoe.hp > 1) btlFoe.hp = 1;
    if (btlMsgCount) { click(233, 320); continue; }
    int slot = -1;
    for (int i = 0; i < MOVE_SLOTS; i++) if (btlYou.moves[i]) { slot = i; break; }
    if (slot < 0) break;
    click(BTL_CELL_X(slot) + 40, BTL_CELL_Y(slot) + 20);
  }
  for (int i = 0; i < 200 && !on("win"); i++) pump(1);
}

int main() {
  setup(); pump(4);
  if (pet.awaitingStarter()) pet.chooseStarter(4);
  if (pet.isEgg()) pet.dbgHatchAs(9, false);
  pet.ageMinutes = 100 * MINUTES_PER_LEVEL;
  pet.relearnFromLevel();
  while (pet.hasLearnOffer()) pet.declineLearn();
  squadMask = 0xFFFF; pickOpen = false;

  // ---- 1. the art itself: every region x all 13 ladder slots
  {
    int missing = 0, flat = 0;
    for (int r = 0; r < GYM_REGIONS; r++)
      for (int i = 0; i < TRAINER_COUNT; i++) {
        if (!badgeArtExists(r, i)) { missing++; continue; }
        // a sprite, not a blob: a real silhouette and several colours
        int opaque = 0, cols = 0; int32_t seen[16];
        for (int y = 0; y < 32; y++)
          for (int x = 0; x < 32; x++) {
            int32_t c = badgeArtPixel(r, i, x, y);
            if (c < 0) continue;
            opaque++;
            bool f = false;
            for (int k = 0; k < cols; k++) f |= seen[k] == c;
            if (!f && cols < 16) seen[cols++] = c;
          }
        if (opaque < 150 || cols < 5) { flat++; printf("      %s #%d: %d px, %d colours\n", TRAINER_SETS[r].region, i, opaque, cols); }
      }
    ck(missing == 0, "every region x all 13 ladder trainers has art");
    ck(flat == 0, "and each is a real sprite (>= 150 px, >= 5 colours)");
  }
  // no two Alola badges are the same picture
  {
    const int A = GYM_REGIONS - 1;
    ck(!strcmp(TRAINER_SETS[A].region, "ALOLA"), "the last ladder is Alola");
    int dup = 0;
    for (int a = 0; a < TRAINER_GYMS; a++)
      for (int b = a + 1; b < TRAINER_GYMS; b++) {
        int diff = 0;
        for (int y = 0; y < 32; y++)
          for (int x = 0; x < 32; x++) diff += badgeArtPixel(A, a, x, y) != badgeArtPixel(A, b, x, y);
        if (diff < 40) { dup++; printf("      Alola #%d and #%d differ in %d px\n", a, b, diff); }
      }
    ck(dup == 0, "no two Alola badges are alike (each differs in >= 40 px)");
  }
  // the same Elite 4 / champion slot looks different from region to region
  {
    int same = 0;
    for (int i = TRAINER_GYMS; i < TRAINER_COUNT; i++)
      for (int a = 0; a < GYM_REGIONS; a++)
        for (int b = a + 1; b < GYM_REGIONS; b++) {
          bool eq = true;
          for (int y = 0; y < 32 && eq; y++)
            for (int x = 0; x < 32 && eq; x++) eq = badgeArtPixel(a, i, x, y) == badgeArtPixel(b, i, x, y);
          if (eq) same++;
        }
    ck(same == 0, "an E4/champion emblem is tinted per region: no two regions share one");
  }

  // ---- 2. the win screen, for an ELITE 4 win (Kanto's Lorelei, slot 8)
  for (int i = 0; i < TRAINER_GYMS; i++) pet.winBadge(0, i, false);   // the ladder is sequential
  gymRegion = 0;
  startTrainerBattle(TRAINER_GYMS, false);
  ck(battleOpen, "an Elite 4 battle starts");
  winFight();
  ck(btlWon && pet.hasBadge(0, TRAINER_GYMS, false), "the Elite 4 win is recorded");
  ck(on("win"), "and the win screen is up");
  {
    double m = artMatch(0, TRAINER_GYMS, 233, WIN_BY, 3);
    printf("      E4 win screen: %.0f%% of the emblem drawn\n", m * 100);
    ck(m > 0.99, "the win screen draws the Elite 4 EMBLEM at 3x, not a disc");
  }
  click(233, 320); pump(2);

  // ---- 3. the win screen for an ALOLA win (Mina, slot 5), on hard
  {
    const int A = GYM_REGIONS - 1;
    for (int i = 0; i < 5; i++) { pet.winBadge(A, i, false); pet.winBadge(A, i, true); }
    gymRegion = A;
    startTrainerBattle(5, true);
    ck(battleOpen, "an Alola trial battle starts");
    winFight();
    ck(btlWon && pet.hasBadge(A, 5, true), "the Alola win is recorded on hard");
    ck(on("win"), "and the win screen is up");
    double m = artMatch(A, 5, 233, WIN_BY, 3);
    printf("      Alola win screen: %.0f%% of the badge drawn\n", m * 100);
    ck(m > 0.99, "the win screen draws Alola's own badge at 3x");
    // the hard halo survived: gold on the ring around it
    int gold = 0;
    for (int r = 56; r <= 62; r++) gold += px(233 + r, WIN_BY) == 0xFF60 || px(233 + r, WIN_BY) == 0xFEA0 ||
                                           px(233 + r, WIN_BY) == 0xFDE0;
    ck(gold >= 3, "and a hard-mode win keeps its golden halo");
    click(233, 320); pump(2);
  }

  // ---- 4. the gym ladder: a beaten row carries its badge, an open one only its outline
  battleOpen = false;
  {
    const int A = GYM_REGIONS - 1;
    gymOpen = true; gymPick = false; gymHard = false; gymRegion = A; gymPage = 0;
    ck(on("gym"), "the Alola ladder is on screen");
    // row 0 (Ilima) was beaten on easy above
    double beaten = artMatch(A, 0, GYM_BADGE_X, GYM_ROW_Y(0) + 22, 1);
    printf("      beaten row: %.0f%% of its badge drawn\n", beaten * 100);
    ck(pet.hasBadge(A, 0, false) && beaten > 0.99, "a BEATEN row draws its badge, in full");
    gymPage = 1;                                  // rows 5..9
    ck(on("gym"), "page 2 of the ladder");
    ck(!pet.hasBadge(A, 6, false), "(slot 6 is not beaten on easy)");
    double open = artMatch(A, 6, GYM_BADGE_X, GYM_ROW_Y(1) + 22, 1);
    int rim = 0;
    for (int y = 0; y < 32; y++)
      for (int x = 0; x < 32; x++)
        if (badgeArtPixel(A, 6, x, y) >= 0 && px(GYM_BADGE_X - 16 + x, GYM_ROW_Y(1) + 6 + y) != px(80, GYM_ROW_Y(1) + 22)) rim++;
    printf("      unbeaten row: %.0f%% art, %d px of outline\n", open * 100, rim);
    ck(open < 0.2 && rim > 40, "an unbeaten row draws only a faint outline of its badge");
    gymOpen = false;
  }

  // ---- 5. the player card: the Elite 4 row exists and shows what was won
  {
    playerOpen = true; playerPage = 0;
    ck(on("player"), "the player card is on screen");
    int x, y;
    playerBadgeXY(TRAINER_GYMS, &x, &y);
    ck(artMatch(0, TRAINER_GYMS, x, y, 1) > 0.99, "the player card draws the won Elite 4 emblem");
    playerBadgeXY(TRAINER_COUNT - 1, &x, &y);
    ck(artMatch(0, TRAINER_COUNT - 1, x, y, 1) < 0.2, "and only an outline for the champion not yet beaten");
    int gx, gy;
    playerBadgeXY(TRAINER_GYMS - 1, &gx, &gy);
    ck(gy < y && artMatch(0, TRAINER_GYMS - 1, gx, gy, 1) > 0.99, "with the eight gym badges in a row above");
    // nothing overlaps: 13 slots, each at least a badge apart from the next
    bool clash = false;
    for (int a = 0; a < TRAINER_COUNT; a++)
      for (int b = a + 1; b < TRAINER_COUNT; b++) {
        int ax, ay, bx, by;
        playerBadgeXY(a, &ax, &ay); playerBadgeXY(b, &bx, &by);
        if (abs(ax - bx) < 34 && abs(ay - by) < 34) clash = true;
      }
    ck(!clash, "no two badges on the card overlap");
    playerOpen = false;
  }

  // ---- 6. the region chooser's mini strip: Kanto's first nine are won
  {
    gymOpen = true; gymPick = true; rpickPage = 0; gymHard = false;
    ck(on("gympick"), "the gym region chooser is on screen");
    int y = RPICK_Y(0) + RPICK_STRIP_DY;
    uint16_t bg = px(RPICK_X + 6, RPICK_Y(0) + 31);
    auto colours = [&](int k) {
      int n = 0; uint16_t seen[8];
      for (int dy = -8; dy < 8; dy++)
        for (int dx = -8; dx < 8; dx++) {
          uint16_t c = px(RPICK_STRIP_X(k) + dx, y + dy);
          if (c == bg) continue;
          bool f = false;
          for (int q = 0; q < n; q++) f |= seen[q] == c;
          if (!f && n < 8) seen[n++] = c;
        }
      return n;
    };
    ck(colours(0) >= 3, "a won badge shows in colour in the chooser strip");
    ck(colours(TRAINER_COUNT - 1) == 1, "and one not won as a one-colour silhouette");
    gymOpen = false; gymPick = false;
  }

  printf(bad ? "badge_art_ui_test: %d FAILED\n" : "badge_art_ui_test: all passed\n", bad);
  return bad ? 1 : 0;
}
