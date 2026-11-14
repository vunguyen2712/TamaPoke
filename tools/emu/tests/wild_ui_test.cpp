// The WILD screen, driven through the REAL sketch: setup(), taps on the hub
// and on the screen's rows, the real battle, the real bag and party. The rules
// themselves are wild_test; this proves the CALLER uses them (CLAUDE.md trap
// 1: the picker once ignored a rule the logic test proved right).
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include "battle.h"
#include "items.h"
#include "wild.h"
#include "rtcbat.h"
#include <cstdio>
#include <cstring>
#include <chrono>
#include <thread>
uint32_t g_seed = 5150; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX = 0, g_touchY = 0; volatile bool g_touchDown = false;
void FakeESP::restart() { exit(0); }
int FakeSerial::available() { return 0; }
String FakeSerial::readStringUntil(char) { return String(""); }
void setup(); void loop(); void render(); void onSwipe(int dir);
uint8_t uiCurrentScreen();
extern const char *const SCREEN_NAME[];
extern Pet pet;
extern Arduino_Canvas *gfx;
extern bool battleOpen, btlWon, hubOpen, wildOpen;
extern uint8_t btlKind, btlMsgCount;
extern Combatant btlYou, btlFoe;
void wildHubLabel(char *out, size_t n, const char *name);
#define BTL_CELL_X(i) (69 + ((i) % 2) * 168)
#define BTL_CELL_Y(i) (286 + ((i) / 2) * 52)
// the screen's rows: FEAT_ROW_Y(i) = 88 + 50 i, 44 tall (ui_hub.ino)
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
static void flushes(const char *what) {
  gfx->frameReady = false;
  render();
  char b[96]; snprintf(b, sizeof(b), "the %s face flushes", what);
  ck(gfx->frameReady, b);
}
#define DAY0 20400u
static void setDay(uint32_t day, int hour) {
  rtcSetEpoch(day * 86400u + hour * 3600u + 60u);
  pet.lastSeenEpoch = rtcEpoch();
}
// Win the fight that is open. The foe is left on 1 HP: this test is about the
// wiring around the battle, not about who wins it (battle_test owns that).
static void winFight() {
  btlFoe.hp = 1;
  for (int taps = 0; battleOpen && taps < 60; taps++) {
    if (btlMsgCount) { click(CXT, 320); continue; }
    btlFoe.hp = 1;
    int slot = -1;
    for (int i = 0; i < MOVE_SLOTS; i++) if (btlYou.moves[i]) { slot = i; break; }
    if (slot < 0) break;
    click(BTL_CELL_X(slot) + 40, BTL_CELL_Y(slot) + 20);   // FIGHT, then the move
  }
}

int main() {
  setup(); pump(4);
  click(233, 138); pump(2);            // region: KANTO
  click(233, 145);                     // starter row 0
  ck(!pet.awaitingStarter() && pet.isEgg(), "starter picked, the egg is waiting");
  setDay(DAY0, 13);

  // ---- open WILD from the hub
  hubOpen = true;
  click(CXT, ROW(0));
  ck(wildOpen && on("wild"), "the hub's WILD row opens the wild screen");
  ck(bag.count(ITEM_POKEBALL) == WILD_GIFT_BALLS, "the first visit gives 10 POKE BALLs");
  ck(wild.st.phase == WP_MEET && wild.st.dex >= 1, "a creature is out");
  flushes("meet");
  int16_t met = wild.st.dex;

  // ---- an egg cannot fight: FIGHT is refused, and nothing is spent
  click(CXT, ROW(4));
  ck(!battleOpen && wild.st.phase == WP_MEET && wild.left() == WILD_PER_DAY, "FIGHT with only an egg: refused, nothing spent");
  flushes("meet, refused");

  // ---- a horizontal swipe goes back to the hub and loses nothing
  onSwipe(1); pump(2);
  ck(hubOpen && !wildOpen && on("hub"), "a swipe goes back to the hub");
  click(CXT, ROW(0));
  ck(wildOpen && wild.st.dex == met, "coming back shows the same creature (no re-roll)");
  ck(bag.count(ITEM_POKEBALL) == WILD_GIFT_BALLS, "and no second gift");

  // ---- hatch, then FIGHT really starts a wild battle
  pet.dbgHatchAs(9, false);
  pet.ageMinutes = 30 * MINUTES_PER_LEVEL;
  pet.relearnFromLevel();
  while (pet.hasLearnOffer()) pet.declineLearn();
  click(CXT, ROW(4));
  ck(battleOpen && on("battle"), "FIGHT opens the battle");
  ck(btlKind == 3 /* BK_WILD */, "as a WILD battle, so its end is routed back here");
  ck(btlFoe.dex == met && btlFoe.level == wild.st.level, "against the creature shown, at its level");
  ck(wild.st.phase == WP_FIGHT, "the fight is on record before it starts");
  winFight();
  ck(!battleOpen && btlWon, "won");
  ck(wildOpen && on("wild") && wild.st.phase == WP_CATCH, "the win lands on the catch face");
  flushes("catch");

  // ---- throw until it is caught or flees; every throw costs exactly one ball
  int caught = 0, pc0 = party.count();
  int16_t lastCaught = 0;
  bag.add(ITEM_ULTRABALL, 30);
  for (int enc = 0; enc < 8 && !caught; enc++) {
    if (wild.st.phase != WP_CATCH) {     // another encounter: search, fight, win
      if (!wild.left()) setDay(DAY0 + 1 + enc, 13);
      if (wild.st.phase == WP_NONE) click(CXT, ROW(4));
      if (wild.st.phase != WP_MEET) break;
      click(CXT, ROW(4));
      winFight();
    }
    int16_t d = wild.st.dex;
    while (wild.st.phase == WP_CATCH) {
      uint8_t u = bag.count(ITEM_ULTRABALL);
      click(CXT, ROW(4));               // ULTRA BALL
      if (bag.count(ITEM_ULTRABALL) != u - 1) { ck(false, "a throw spends one ball"); break; }
    }
    for (int i = 0; i < PARTY_SLOTS; i++) if (party.slots[i].dex == d) { caught = 1; lastCaught = d; }
  }
  ck(caught, "a creature is caught through the screen");
  ck(party.count() == pc0 + 1, "it joined the party");
  ck(pet.isRegistered(lastCaught), "and the Pokedex");
  flushes("after a catch");

  // ---- RUN spends the encounter
  setDay(DAY0 + 20, 23);
  click(CXT, ROW(4));                    // SEARCH (a new day: full)
  ck(wild.st.phase == WP_MEET && wild.left() == WILD_PER_DAY, "a new day: SEARCH puts a creature out, 3 left");
  click(CXT, ROW(5));                    // RUN
  ck(wild.st.phase == WP_NONE && wild.left() == WILD_PER_DAY - 1, "RUN spends it");
  click(CXT, ROW(4)); click(CXT, ROW(5));
  click(CXT, ROW(4)); click(CXT, ROW(5));
  ck(wild.left() == 0, "three spent");
  click(CXT, ROW(4));
  ck(wild.st.phase == WP_NONE, "with none left, the SEARCH row is gone");
  flushes("none left");
  char lbl[32]; wildHubLabel(lbl, sizeof(lbl), "WILD");
  ck(!strcmp(lbl, "WILD 0/3"), "the hub row says 0/3");
  setDay(DAY0 + 21, 9);
  wildHubLabel(lbl, sizeof(lbl), "WILD");
  ck(!strcmp(lbl, "WILD 3/3"), "and 3/3 the next day, before anything is written");

  // ---- party AND box full: the ball is not consumed
  for (int i = 0; i < PARTY_SLOTS; i++) if (party.slots[i].empty()) { PartyMon m; m.dex = 19; m.level = 5; party.add(m); }
  for (int i = 0; i < BOX_SLOTS; i++) if (party.box[i].empty()) { PartyMon m; m.dex = 19; m.level = 5; party.boxAdd(m); }
  click(CXT, ROW(4)); click(CXT, ROW(4)); winFight();
  ck(wild.st.phase == WP_CATCH, "won again, with nowhere to put it");
  uint8_t pb = bag.count(ITEM_POKEBALL);
  click(CXT, ROW(2));                    // POKE BALL
  ck(bag.count(ITEM_POKEBALL) == pb && wild.st.phase == WP_CATCH && wild.st.throws == 0,
     "PARTY AND BOX FULL: the ball is kept and the creature waits");
  flushes("catch, full");
  click(CXT, ROW(5));                    // LET GO
  ck(wild.st.phase == WP_NONE, "LET GO lets it go");

  // ---- it was all written down
  { Party p2; p2.begin(); bool there = false;
    for (int i = 0; i < PARTY_SLOTS; i++) if (p2.slots[i].dex == lastCaught) there = true;
    ck(there, "the caught creature survives a reload");
    Wild w; w.begin(); ck(w.gift && w.st.day == DAY0 + 21, "the gift flag and the day survive a reload"); }

  printf(bad ? "wild_ui_test: %d FAILED\n" : "wild_ui_test: all passed\n", bad);
  return bad ? 1 : 0;
}
