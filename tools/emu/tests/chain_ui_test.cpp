// Shiny chains through the REAL sketch: wins on the WILD screen grow the
// chain, the screen shows it, the boost makes the chained species come back,
// a day off breaks it, it survives a reload, and a shiny encounter is shiny in
// the fight too. The rules themselves are chain_test.
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include "battle.h"
#include "items.h"
#include "wild.h"
#include "rtcbat.h"
#include "species.h"
#include "game_events.h"
#include <cstdio>
#include <cstring>
#include <set>
#include <vector>
#include <chrono>
#include <thread>
uint32_t g_seed = 777; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
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
#define BTL_CELL_X(i) (69 + ((i) % 2) * 168)
#define BTL_CELL_Y(i) (286 + ((i) / 2) * 52)
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
#define DAY0 20400u
static void setDay(uint32_t day, int hour) {
  rtcSetEpoch(day * 86400u + hour * 3600u + 60u);
  pet.lastSeenEpoch = rtcEpoch();
}
static void winFight() {
  btlFoe.hp = 1;
  for (int taps = 0; battleOpen && taps < 60; taps++) {
    if (btlMsgCount) { click(CXT, 320); continue; }
    btlFoe.hp = 1;
    int slot = -1;
    for (int i = 0; i < MOVE_SLOTS; i++) if (btlYou.moves[i]) { slot = i; break; }
    if (slot < 0) break;
    click(BTL_CELL_X(slot) + 40, BTL_CELL_Y(slot) + 20);
  }
}
// The chain line lives at y 266..282 on the MEET and NONE faces, alone in that
// band: anything but the plain backdrop there means it was drawn.
static bool chainLineDrawn() {
  gfx->frameReady = false;
  render();
  std::set<uint16_t> cols;
  for (int y = 266; y < 282; y++) for (int x = 60; x < 406; x++) cols.insert(gfx->buffer()[y * 466 + x]);
  return cols.size() > 1 && cols.count(UI_BAR_WARN);
}
static std::vector<int> chainEv; static int shinyEv = 0; static int16_t shinyDex = 0;
static void sink(GameEv e, int16_t a) {
  if (e == EV_CHAIN) chainEv.push_back(a);
  if (e == EV_SHINY_FOUND) { shinyEv++; shinyDex = a; }
}

int main() {
  setup(); pump(4);
  gameEventSubscribe(sink);
  click(233, 138); pump(2);            // region: KANTO
  click(233, 145);                     // starter row 0
  pet.dbgHatchAs(9, false);
  pet.ageMinutes = 30 * MINUTES_PER_LEVEL;
  pet.relearnFromLevel();
  while (pet.hasLearnOffer()) pet.declineLearn();
  setDay(DAY0, 13);

  hubOpen = true;
  click(CXT, ROW(0));
  ck(wildOpen && on("wild") && wild.st.phase == WP_MEET, "WILD is open with a creature out");
  ck(wild.chainLen(pet.currentDay()) == 0 && !chainLineDrawn(), "no chain yet: no chain line");

  // ---------------- the first win starts it ----------------
  int16_t first = wild.st.dex;
  click(CXT, ROW(4)); winFight();
  ck(wild.st.phase == WP_CATCH, "won");
  ck(wild.ch.dex == first && wild.chainLen(pet.currentDay()) == 1, "the win started a chain on it");
  ck(chainEv.size() == 1 && chainEv[0] == 1, "EV_CHAIN 1");
  click(CXT, ROW(5));                  // LET GO
  ck(wild.st.phase == WP_NONE && chainLineDrawn(), "the screen shows the chain");
  ck(gfx->frameReady, "and flushes");

  // ---------------- hunt through the screen: fight the chained, run from the rest
  int seen = 0, chained = 0;
  uint32_t day = DAY0, lastLink = DAY0;
  for (int guard = 0; guard < 60 && wild.chainLen(pet.currentDay()) < 4; guard++) {
    if (!wild.left()) { day++; setDay(day, 13); }
    if (wild.st.phase == WP_NONE) click(CXT, ROW(4));   // SEARCH
    if (wild.st.phase != WP_MEET) break;
    seen++;
    if (wild.st.dex == first) {
      chained++;
      if (!chainLineDrawn()) { ck(false, "the MEET face shows the chain"); }
      click(CXT, ROW(4)); winFight();
      click(CXT, ROW(5));                                 // LET GO
      lastLink = day;
    } else click(CXT, ROW(5));                            // RUN
  }
  printf("      %d encounters, %d of them the chained species; chain now %u\n", seen, chained,
         (unsigned)wild.chainLen(pet.currentDay()));
  ck(wild.chainLen(pet.currentDay()) >= 4, "the chain reached 4 through the screen");
  ck(chained * 100 >= seen * 30, "the boost brought the chained species back (>= 30% of encounters)");
  { bool inc = true; for (size_t i = 0; i < chainEv.size(); i++) if (chainEv[i] != (int)i + 1) inc = false;
    ck(inc && chainEv.size() == wild.ch.len, "EV_CHAIN 1, 2, 3, 4 -- once per link"); }

  // ---------------- a reload keeps it ----------------
  { Wild w; w.begin();
    ck(w.ch.dex == first && w.ch.len == wild.ch.len && w.ch.day == lastLink, "the chain survives a reload"); }

  // ---------------- a day off breaks it ----------------
  setDay(lastLink + 1, 13);
  ck(wild.chainLen(pet.currentDay()) >= 4 && chainLineDrawn(), "the next day it is still there");
  setDay(lastLink + 2, 13);
  ck(wild.chainLen(pet.currentDay()) == 0, "a whole day with no win: broken");
  ck(!chainLineDrawn(), "and the screen stops showing it");

  // ---------------- a shiny is shiny in the fight too ----------------
  // A long chain with the charm (1/8 for the chained species): find a day whose
  // next encounter the peek says is a shiny, then meet it through the screen.
  bag.add(ITEM_SHINY_CHARM);
  uint32_t sd = 0; WildNext n;
  for (uint32_t d = DAY0 + 500; d < DAY0 + 1500 && !sd; d++) {
    wild.ch.dex = first; wild.ch.len = 15; wild.ch.day = d;
    wild.st = Wild::State(); wild.st.day = d;
    setDay(d, 13);
    if (wild.peek(pet.currentDay(), pet.lastSeenEpoch, pet.region, pet.level(), true, n) && n.shiny) sd = d;
  }
  ck(sd != 0, "a shiny is reachable on a long chain");
  shinyEv = 0;
  click(CXT, ROW(4));                  // SEARCH
  ck(wild.st.phase == WP_MEET && wild.st.shiny && wild.st.dex == n.dex, "the screen meets the shiny the peek promised");
  ck(shinyEv == 1 && shinyDex == n.dex, "EV_SHINY_FOUND with its dex");
  click(CXT, ROW(4));                  // FIGHT
  ck(battleOpen && btlFoe.dex == n.dex && btlFoe.shiny, "the battle's foe is the shiny one");
  winFight();
  click(CXT, ROW(5));                  // LET GO
  // and a plain one is plain (the companion)
  { uint32_t pd = 0;
    for (uint32_t d = sd + 1; d < sd + 200 && !pd; d++) {
      wild.st = Wild::State(); wild.st.day = d; setDay(d, 13);
      if (wild.peek(pet.currentDay(), pet.lastSeenEpoch, pet.region, pet.level(), true, n) && !n.shiny) pd = d;
    }
    click(CXT, ROW(4));                // SEARCH
    click(CXT, ROW(4));                // FIGHT
    ck(battleOpen && !btlFoe.shiny && btlFoe.dex == n.dex, "a plain encounter's foe is plain");
    winFight(); }

  printf(bad ? "chain_ui_test: %d FAILED\n" : "chain_ui_test: all passed\n", bad);
  return bad ? 1 : 0;
}
