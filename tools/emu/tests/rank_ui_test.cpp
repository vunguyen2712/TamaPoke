// Trainer rank through the REAL sketch: the player card's rank page, its
// paging, the avatar and theme pickers, the page-0 avatar cycle, the scene
// theme in drawScene and the rank-up notice on the main screen. rank_test owns
// the numbers; this one proves the CALLERS ask rank.cpp (CLAUDE.md trap 1).
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include "rank.h"
#include "avatars.h"
#include "game_events.h"
#include <cstdio>
#include <cstring>
uint32_t g_seed = 77; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX = 0, g_touchY = 0; volatile bool g_touchDown = false;
void FakeESP::restart() { exit(0); }
int FakeSerial::available() { return 0; }
String FakeSerial::readStringUntil(char) { return String(""); }
void setup(); void render();
void onSwipe(int dir); void onTap(int16_t x, int16_t y);
uint8_t uiCurrentScreen();
void rankAvatarCell(uint8_t i, int16_t &x, int16_t &y);
void rankThemeCell(uint8_t t, int16_t &x, int16_t &y);
extern const char *const SCREEN_NAME[];
extern Arduino_Canvas *gfx;
extern Pet pet;
extern bool playerOpen, cardOpen, menuOpen;
extern uint8_t playerPage;

static int bad = 0;
static void ck(bool ok, const char *w) { printf("%s  %s\n", ok ? "PASS" : "FAIL", w); if (!ok) bad++; }
static bool on(const char *scr) { render(); return !strcmp(SCREEN_NAME[uiCurrentScreen()], scr); }

#define CX_ 233
#define GOLD_ ((uint16_t)(((0xff >> 3) << 11) | ((0xc8 >> 2) << 5) | (0x3a >> 3)))

static void clearBadges() {
  pet.badges = pet.badgesHard = 0;
  for (int r = 0; r < GYM_REGIONS - 1; r++) pet.badgesX[r] = pet.badgesHardX[r] = 0;
}
// enough badges for rank 5+ (39 badges = 390 points)
static void manyBadges() { for (int r = 0; r < 3; r++) pet.badgesX[r] = (1u << TRAINER_COUNT) - 1; }

static uint32_t skyHash() {
  uint32_t h = 0;
  const uint16_t *b = gfx->buffer();
  for (int y = 100; y < 230; y += 3) for (int x = 20; x < 446; x += 5) h = h * 31 + b[y * 466 + x];
  return h;
}
static void tapAvatar(uint8_t i) { int16_t x, y; rankAvatarCell(i, x, y); onTap(x, y); }
static void tapTheme(uint8_t t) { int16_t x, y; rankThemeCell(t, x, y); onTap(x, y); }
static void openRankPage() { playerOpen = true; playerPage = GYM_REGIONS + 1; }

int main() {
  setup();
  for (int i = 0; i < 4; i++) render();
  if (pet.awaitingStarter()) pet.chooseStarter(4);
  if (pet.isEgg()) pet.dbgHatchAs(6, false);
  clearBadges(); pet.avatar = 0;
  rankClearAll();
  render();

  // ---- paging: one more page, last, and the ends still back out
  {
    playerOpen = true; playerPage = 0;
    int pages = 1; bool stayed = true;
    for (int i = 0; i < 20 && playerOpen; i++) {
      uint8_t before = playerPage;
      onSwipe(-1);
      if (!playerOpen) break;
      if (playerPage != before + 1) stayed = false;
      pages++;
    }
    ck(stayed, "every swipe left advances one page and keeps the card open");
    ck(pages == GYM_REGIONS + 2, "badges per region, medals, and the rank page last");
    ck(!playerOpen, "swiping past the rank page backs out");
    openRankPage();
    onSwipe(1);
    ck(playerOpen && playerPage == GYM_REGIONS, "swiping right from the rank page goes back to the medals");
    openRankPage();
    gfx->frameReady = false;
    ck(on("player"), "the rank page is the player card");
    ck(gfx->frameReady, "and it flushes");
  }

  // ---- the avatar grid
  rankClearAll(); pet.avatar = 0;
  ck(rankShown() == 1 && !rankAvatarUnlocked(6) && rankAvatarUnlocked(1), "(rank 1: SAILOR locked, BLUE open)");
  openRankPage(); render();
  tapAvatar(6);
  ck(pet.avatar == 0, "tapping a locked avatar does not choose it");
  ck(playerOpen && playerPage == GYM_REGIONS + 1, "and the tap is swallowed, not a back-out");
  tapAvatar(1);
  ck(pet.avatar == 1, "tapping an unlocked one chooses it");
  ck(playerOpen, "and keeps the card open");
  onTap(CX_, 350);
  ck(!playerOpen, "a tap off the grid still backs out");

  // ---- page 0's tap-to-cycle asks the same rule
  playerOpen = true; playerPage = 0; pet.avatar = 2; render();
  onTap(CX_, 100);
  ck(pet.avatar == 0, "page 0 cycles from GIRL past every locked avatar to RED");
  manyBadges(); rankUpdate();
  ck(rankAvatarUnlocked(3), "(rank raised: ACE M open)");
  playerOpen = true; playerPage = 0; pet.avatar = 2; render();
  onTap(CX_, 100);
  ck(pet.avatar == 3, "and once ACE M is unlocked the cycle reaches it");
  playerOpen = false;

  // ---- a save that already wears a locked avatar keeps it
  clearBadges(); rankClearAll();
  pet.avatar = 6; pet.saveNow();
  pet.begin();
  rankBegin();
  ck(pet.avatar == 6, "a locked avatar already selected survives a load");
  ck(rankShown() < rankAvatarNeeds(6) && rankAvatarUnlocked(6), "and stays selectable below its rank");
  openRankPage(); render();
  tapAvatar(5);
  ck(pet.avatar == 6, "a tap on another locked one does not take it away");
  tapAvatar(0);
  tapAvatar(6);
  ck(pet.avatar == 6, "and the player can switch back to it");
  playerOpen = false;

  // ---- themes
  clearBadges(); rankClearAll(); pet.avatar = 0;
  openRankPage(); render();
  tapTheme(THEME_STARRY);
  ck(rankTheme() == THEME_CLASSIC, "a locked theme cannot be chosen by tapping");
  ck(playerOpen, "and the tap is swallowed");
  manyBadges(); rankUpdate();
  ck(rankThemeUnlocked(THEME_FESTIVAL) == (rankShown() >= rankThemeNeeds(THEME_FESTIVAL)), "(unlock follows the rank)");
  tapTheme(THEME_STARRY);
  ck(rankTheme() == THEME_STARRY, "once unlocked, tapping chooses it");
  rankBegin();
  ck(rankTheme() == THEME_STARRY, "and it is on flash");
  playerOpen = false;

  // ---- the theme reaches the scene
  {
    rankSetTheme(THEME_CLASSIC);
    while (rankNoticeActive(millis())) rankNoticeActive(millis() + RANK_NOTICE_MS);
    ck(on("main"), "(on the main screen)");
    uint32_t classic = skyHash();
    rankSetTheme(THEME_STARRY);
    render();
    uint32_t starry = skyHash();
    ck(starry != classic, "STARRY changes the sky drawn by drawScene");
    // at noon, awake: only the theme can make the ground the night colour
    pet.sleeping = false; pet.lastSeenEpoch = 12 * 3600;
    rankSetTheme(THEME_CLASSIC); render();
    uint16_t soilDay = gfx->buffer()[300 * 466 + 30];
    rankSetTheme(THEME_STARRY); render();
    ck(gfx->buffer()[300 * 466 + 30] != soilDay, "STARRY dresses the whole scene as night at noon (ground too)");
    rankSetTheme(THEME_CLASSIC); render();
    uint32_t noon = skyHash();
    ck(rankSetTheme(THEME_SUNSET), "(SUNSET unlocked)");
    render();
    ck(skyHash() != noon, "SUNSET repaints the noon sky");
    rankSetTheme(THEME_CLASSIC); render();
    // FESTIVAL needs rank 7: grant it the honest way
    for (int r = 3; r < GYM_REGIONS - 1; r++) pet.badgesX[r] = (1u << TRAINER_COUNT) - 1;
    for (int r = 0; r < GYM_REGIONS - 1; r++) pet.badgesHardX[r] = (1u << TRAINER_COUNT) - 1;
    rankUpdate();
    while (rankNoticeActive(millis())) rankNoticeActive(millis() + RANK_NOTICE_MS);
    ck(rankSetTheme(THEME_FESTIVAL), "(FESTIVAL unlocked)");
    render();
    uint16_t px = gfx->buffer()[124 * 466 + 84];
    rankSetTheme(THEME_CLASSIC); render();
    uint16_t px0 = gfx->buffer()[124 * 466 + 84];
    ck(px != px0, "FESTIVAL draws its bunting into the sky");
  }

  // ---- the rank-up notice
  {
    clearBadges(); rankClearAll();
    while (rankNoticeActive(millis())) rankNoticeActive(millis() + RANK_NOTICE_MS);
    ck(on("main"), "(main)");
    ck(gfx->buffer()[200 * 466 + 95] != GOLD_, "(no notice before a rank-up)");
    manyBadges(); rankUpdate();
    ck(rankNoticePending(), "a rank-up leaves a notice for the main screen");
    pet.medals &= ~MED_FINAL; pet.dbgHatchAs(6, false);   // re-earns FINAL FORM: MEDAL! is up
    while (pet.hasLearnOffer()) pet.declineLearn();
    ck(pet.showMedal(), "(the MEDAL! popup is up)");
    ck(on("main"), "(main)");
    ck(gfx->buffer()[200 * 466 + 95] != GOLD_ && rankNoticePending(), "the notice waits while the MEDAL! popup is up");
    while (pet.showMedal()) delay(50);
    ck(on("main"), "(main)");
    ck(gfx->buffer()[200 * 466 + 95] == GOLD_, "the main screen draws it");
  }

  printf("%s\n", bad ? "FAILURES" : "all good");
  return bad ? 1 : 0;
}
