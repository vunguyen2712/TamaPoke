// Gym rematches through the REAL sketch: the gym screen's difficulty pill, the
// rows, the picker, the battle and its ending.
//
// The guard that matters most is that a rematch win NEVER touches a badge. On a
// normal save that would be vacuous -- every easy badge is already held by the
// time the champion falls, so even a fight that tried to award one would find
// it held. So this test unlocks rematches with the CHAMPION's badge alone and
// Brock's bit clear: put the BK_GYM guard in btlResolve back to the old
// `btlTrainer >= 0` and the rematch hands out Brock's badge, which fails here.
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include "battle.h"
#include "trainers.h"
#include "items.h"
#include "tower.h"
#include "game_events.h"
#include <cstdio>
#include <cstring>
#include <chrono>
#include <thread>
uint32_t g_seed = 616; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX = 0, g_touchY = 0; volatile bool g_touchDown = false;
void FakeESP::restart() { exit(0); }
int FakeSerial::available() { return 0; }
String FakeSerial::readStringUntil(char) { return String(""); }
void setup(); void loop(); void render();
void onSwipe(int dir);
uint8_t uiCurrentScreen();
extern const char *const SCREEN_NAME[];
extern Arduino_Canvas *gfx;
extern Pet pet;
extern bool battleOpen, btlWon, pickOpen, gymOpen, gymHard, gymRematch, pickRematch, btlHard;
extern Combatant btlYou, btlFoe;
extern uint8_t btlMsgCount, btlKind, pickTrainer, gymPage, gymRegion, btlRegion;
extern int8_t btlTrainer;
extern uint32_t btlWinUntil;
extern TrainerMon btlFoeTeam[];
extern uint8_t btlFoeTeamN;
extern uint16_t squadMask;
uint8_t squadCap(uint8_t idx, bool hard);
bool gymRematchOn();
#define BTL_CELL_X(i) (69 + ((i) % 2) * 168)
#define BTL_CELL_Y(i) (286 + ((i) / 2) * 52)
#define BK_GYM_ 1          // the sketch's anonymous enum
#define BK_REMATCH_ 5
#define GYM_ROW_Y_(i) (110 + (i) * 50)
#define PILL_Y 82          // GYMDIF_Y 60 + half of UI_TAP_MIN

static int bad = 0;
static void ck(bool ok, const char *w) { printf("%s  %s\n", ok ? "PASS" : "FAIL", w); if (!ok) bad++; }

static int evRm = 0, evGym = 0;
static int16_t rmArg = -1;
static void sink(GameEv ev, int16_t a) {
  if (ev == EV_REMATCH_WIN) { evRm++; rmArg = a; }
  if (ev == EV_GYM_WIN) evGym++;
}

static void pump(int n) {
  for (int i = 0; i < n; i++) { std::this_thread::sleep_for(std::chrono::milliseconds(30)); loop(); }
}
static void click(int x, int y) {
  g_touchX = x; g_touchY = y; g_touchDown = true; emuFireInterrupt(); pump(5);
  g_touchDown = false; emuFireInterrupt(); pump(3);
}
static bool on(const char *scr) { render(); return !strcmp(SCREEN_NAME[uiCurrentScreen()], scr); }

// Wins, steering only HP. Returns whether every foe that came out was the
// battle team's next member, in order.
static bool fightToWin(const TrainerMon *want, uint8_t n) {
  bool ok = true;
  int16_t seen = btlFoe.dex; int idx = 0;
  if (n && btlFoe.dex != (int16_t)want[0].dex) ok = false;
  for (int taps = 0; battleOpen && taps < 600; taps++) {
    if (btlFoe.dex != seen) {
      seen = btlFoe.dex; idx++;
      if (idx >= n || btlFoe.dex != (int16_t)want[idx].dex) ok = false;
    }
    if (btlWinUntil) break;                 // leave the win screen up for a look
    if (btlMsgCount) { click(233, 320); continue; }
    btlFoe.hp = 1; btlYou.hp = btlYou.maxHp;
    int slot = 0;
    for (int i = 0; i < MOVE_SLOTS; i++) if (btlYou.moves[i]) { slot = i; break; }
    click(BTL_CELL_X(slot) + 40, BTL_CELL_Y(slot) + 20);
  }
  if (idx != n - 1) ok = false;
  return ok;
}
static void openGym(uint8_t region) {
  gymOpen = true; gymRegion = region; gymPage = 0; pickOpen = false; battleOpen = false;
  pump(1);
}
static void fightPicked() { click(233 + 7 + 70, 350 + 22); }

int main() {
  setup(); pump(4);
  click(233, 138); pump(2);
  click(233, 145);
  if (pet.isEgg()) pet.dbgHatchAs(9, false);
  pet.ageMinutes = 99 * MINUTES_PER_LEVEL;
  pet.relearnFromLevel();
  while (pet.hasLearnOffer()) pet.declineLearn();
  gameEventSubscribe(sink);

  // ---- before the champion: the pill is EASY <-> HARD only
  openGym(0); gymHard = false; gymRematch = false;
  click(233, PILL_Y);
  ck(gymHard && !gymRematchOn(), "pill: EASY -> HARD");
  click(233, PILL_Y);
  ck(!gymHard && !gymRematchOn(), "and HARD -> EASY while the champion stands: no REMATCH");
  pet.winBadge(0, TRAINER_COUNT - 1, true);
  click(233, PILL_Y); click(233, PILL_Y);
  ck(!gymRematchOn(), "a HARD champion win does not unlock rematches");

  // ---- the champion on EASY -- and ONLY the champion, so a leak would show
  pet.badges = (uint16_t)(1u << (TRAINER_COUNT - 1));
  uint16_t badges0 = pet.badges, hard0 = pet.badgesHard;
  uint16_t x0[GYM_REGIONS - 1], hx0[GYM_REGIONS - 1];
  memcpy(x0, pet.badgesX, sizeof(x0)); memcpy(hx0, pet.badgesHardX, sizeof(hx0));
  ck(rematchUnlocked(0) && !rematchUnlocked(1), "Kanto's easy champion unlocks Kanto's rematches only");
  gymHard = false; gymRematch = false;
  click(233, PILL_Y); click(233, PILL_Y);
  ck(gymRematchOn() && !gymHard, "pill: EASY -> HARD -> REMATCH");
  ck(on("gym"), "still the gym screen");
  gfx->frameReady = false; render();
  ck(gfx->frameReady, "the rematch ladder flushes");

  // ---- paging still pages in rematch mode
  onSwipe(-1);
  ck(gymOpen && gymPage == 1 && gymRematchOn(), "a horizontal swipe pages the rematch ladder");
  onSwipe(1);
  ck(gymPage == 0, "and back");

  // ---- rows: sequential, their own mask
  click(233, GYM_ROW_Y_(1) + 20);           // MISTY
  ck(!pickOpen, "the second rematch is locked until the first is won");
  click(233, GYM_ROW_Y_(0) + 20);           // BROCK
  ck(pickOpen && pickRematch && pickTrainer == 0, "the first opens the picker as a REMATCH");
  ck(squadCap(0, false) == TRAINER_TEAM_MAX, "uncapped team size, like the easy ladder");
  gfx->frameReady = false; render();
  ck(gfx->frameReady && on("pick"), "the rematch picker flushes");

  TrainerMon want[TRAINER_TEAM_MAX];
  uint8_t wn = rematchTeam(0, 0, want);
  uint8_t top = 0;
  for (int k = 0; k < wn; k++) if (want[k].level > top) top = want[k].level;
  const Trainer &brock = TRAINER_SETS[0].list[0];
  ck(wn == TRAINER_TEAM_MAX && wn > brock.count && top > 14, "Brock returns with six, stronger");

  uint8_t oran = bag.count(ITEM_ORAN_BERRY), ultra = bag.count(ITEM_ULTRABALL);
  fightPicked();
  ck(battleOpen && btlKind == BK_REMATCH_ && btlTrainer == 0 && btlRegion == 0, "FIGHT starts a REMATCH battle");
  bool sameTeam = btlFoeTeamN == wn;
  for (int k = 0; k < btlFoeTeamN && k < wn; k++)
    sameTeam &= btlFoeTeam[k].dex == want[k].dex && btlFoeTeam[k].level == want[k].level;
  ck(sameTeam, "the battle holds the rematch team, not the roster");
  ck(btlFoe.level == rematchLevel(brock.team[0].level), "the lead comes out at its raised level");
  ck(btlYou.level == top, "your level is capped to the rematch leader's best");
  ck(!btlHard, "rematches use the easy AI (the levels are the difficulty)");
  bool order = fightToWin(want, wn);
  ck(order, "all six came out, each the battle team's next");
  ck(btlWon && btlWinUntil, "won, and the win screen is due");
  // the final blow plays out on the battle screen first (ui_movefx.ino)
  for (int i = 0; i < 200 && !on("win"); i++) pump(1);
  gfx->frameReady = false; render();
  ck(gfx->frameReady && on("win"), "the rematch win screen flushes");
  click(233, 320);
  ck(!battleOpen, "and a tap leaves it");

  ck(rematchWon(0, 0) && rematchMask(0) == 1, "the win is recorded in the rematch mask");
  ck(pet.badges == badges0, "and NOT as Brock's badge (the champion-only save makes this able to fail)");
  ck(pet.badgesHard == hard0 && !memcmp(x0, pet.badgesX, sizeof(x0)) && !memcmp(hx0, pet.badgesHardX, sizeof(hx0)),
     "no other badge mask moved either");
  ck(evRm == 1 && rmArg == 0 && evGym == 0, "EV_REMATCH_WIN(0), and no EV_GYM_WIN");
  ck(bag.count(ITEM_ORAN_BERRY) == oran + 1 && bag.count(ITEM_ULTRABALL) == ultra + 2,
     "the reward: an ORAN BERRY and, the first time, 2 ULTRA BALLs");
  towerBegin();
  {
    Pet fresh; fresh.begin();
    ck(rematchWon(0, 0) && fresh.badges == badges0, "both survive a reload unchanged");
  }

  // ---- the next rematch is open now
  openGym(0);
  ck(gymRematchOn(), "rematch mode is still selected");
  click(233, GYM_ROW_Y_(1) + 20);
  ck(pickOpen && pickRematch && pickTrainer == 1, "winning the first opened the second");

  // ---- the ordinary ladder still behaves exactly as before
  openGym(0);
  click(233, PILL_Y);
  ck(!gymRematchOn() && !gymHard, "pill: REMATCH -> EASY");
  click(233, GYM_ROW_Y_(0) + 20);
  ck(pickOpen && !pickRematch, "Brock on EASY opens a normal fight");
  fightPicked();
  ck(battleOpen && btlKind == BK_GYM_, "a gym battle, BK_GYM");
  bool gymOrder = fightToWin(brock.team, brock.count);
  ck(gymOrder, "the regular Brock still sends his own roster");
  ck(pet.hasBadge(0, 0, false), "and his badge is still awarded the regular way");
  ck(rematchMask(0) == 1, "without touching the rematch mask");
  if (btlWinUntil) click(233, 320);

  // ---- a region whose champion stands drops back to the ladder
  openGym(0); gymHard = false; gymRematch = true;
  ck(gymRematchOn(), "(rematch mode on in Kanto)");
  gymRegion = 2;
  ck(!gymRematchOn(), "switching to Hoenn, champion unbeaten: not in rematch mode");
  click(233, GYM_ROW_Y_(1) + 20);
  ck(!pickOpen, "and Hoenn's second leader is locked by the normal ladder");

  printf(bad ? "rematch_ui_test: %d FAILED\n" : "rematch_ui_test: all passed\n", bad);
  return bad ? 1 : 0;
}
