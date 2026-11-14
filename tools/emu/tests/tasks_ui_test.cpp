// The TASKS screen and its hooks, driven through the REAL sketch: setup(),
// loop(), render(), taps and swipes. tasks_test proves the rules; this proves
// the callers actually reach them (CLAUDE.md trap 1: test the CALLER).
//
//  - the hub row opens the screen, and its label counts what is claimable
//  - a tap on a finished task pays once; a tap on an unfinished one pays nothing
//  - horizontal swipe pages and only backs out PAST a page, never on one
//  - every render path flushes
//  - the minigame/training exits in the sketch reach the event bus
//  - loop() calls tasksTick(): the clock rolls the day, the state task ticks
//  - every label is ASCII in all six languages and fits its row
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include "items.h"
#include "tasks.h"
#include "game_events.h"
#include "i18n.h"
#include "species.h"
#include <cstdio>
#include <cstring>
#include <chrono>
#include <thread>
uint32_t g_seed = 4243; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX = 0, g_touchY = 0; volatile bool g_touchDown = false;
void FakeESP::restart() { exit(0); }
int FakeSerial::available() { return 0; }
String FakeSerial::readStringUntil(char) { return String(""); }
void setup(); void loop(); void render(); void onSwipe(int dir); void onSwipeV(int dir);
void emuSetTimeScale(uint32_t s);
void rtcSetEpoch(uint32_t e);
extern Arduino_Canvas *gfx;
extern Pet pet;
extern bool hubOpen, wildOpen, tasksOpen, towerOpen, daycareOpen, bagOpen, menuOpen;
extern bool gameOpen, sackOpen, spdOpen;
extern uint8_t tasksPage;
extern uint8_t gameScore; extern uint16_t sackHits, spdHits;
void startGame(); void leaveGame(); void startSack(); void leaveSack(); void startSpeedGame(); void leaveSpeed();
void tasksHubLabel(char *out, size_t n, const char *name);
const char *tasksKindLabel(uint8_t kind, uint8_t lang);
const char *tasksUiString(uint8_t which, uint8_t lang);

static int bad = 0;
static void ck(bool ok, const char *w) { printf("%s  %s\n", ok ? "PASS" : "FAIL", w); if (!ok) bad++; }
static void pump(int n) {
  for (int i = 0; i < n; i++) { std::this_thread::sleep_for(std::chrono::milliseconds(30)); loop(); }
}
static void click(int x, int y) {
  g_touchX = x; g_touchY = y; g_touchDown = true; emuFireInterrupt(); pump(5);
  g_touchDown = false; emuFireInterrupt(); pump(3);
}
static bool flushes() { gfx->frameReady = false; render(); return gfx->frameReady; }
static void setDay(uint32_t day) {
  uint32_t e = day * 86400u + 12 * 3600u;
  rtcSetEpoch(e); pet.lastSeenEpoch = e;
}
static int slotOfKind(uint8_t kind) {
  for (uint8_t s = 0; s < TASK_SLOTS; s++) if (tasksDef(s).kind == kind) return s;
  return -1;
}
// A day (searching forward from `from`) whose dailies include `kind`.
static uint32_t dayWith(uint8_t kind, uint32_t from) {
  TaskWorld w = taskWorldOf(pet);
  for (uint32_t d = from; d < from + 600; d++) {
    uint8_t x[TASK_DAILY]; tasksPick(d, false, w, x);
    for (int i = 0; i < TASK_DAILY; i++) if (DAILY_TASKS[x[i]].kind == kind) return d;
  }
  return 0;
}
#define ROW_Y(i) (88 + (i) * 50 + 22)   // FEAT_ROW_Y(i) + half a row

int main() {
  setup(); pump(4);
  click(233, 138); pump(2);            // region: KANTO
  click(233, 145);                     // starter row 0
  if (pet.isEgg()) pet.dbgHatchAs(1, false);
  ck(!pet.awaitingStarter() && !pet.isEgg(), "a hatched starter");
  pet.fullness = pet.joy = pet.energy = pet.hygiene = 100;

  // ---- open through the hub, from a known state
  const uint32_t D = 20400 + 70;
  uint32_t day = dayWith(TK_FEED, D);
  ck(day != 0, "a day with FEED among the dailies");
  setDay(day);
  hubOpen = true; menuOpen = false;
  ck(flushes(), "the hub flushes");
  click(233, ROW_Y(1));                // TASKS is the hub's second row
  ck(tasksOpen && !hubOpen, "the hub's TASKS row opens the tasks screen");
  ck(tasksPage == 0, "on the daily page");
  ck(tasksDay() == day, "rolled to the pet's day on opening");
  ck(flushes(), "the daily page flushes");

  // ---- a task to claim, through the real feed path
  int sf = slotOfKind(TK_FEED);
  ck(sf >= 0 && sf < TASK_DAILY, "FEED is a daily today");
  char lbl[32];
  tasksHubLabel(lbl, sizeof(lbl), "TASKS");
  ck(strcmp(lbl, "TASKS") == 0, "nothing claimable: the hub row is just TASKS");
  uint8_t balls = bag.count(tasksDef(sf).item);
  click(233, ROW_Y(sf));
  ck(bag.count(tasksDef(sf).item) == balls, "tapping an unfinished task pays nothing");
  for (int i = 0; i < tasksDef(sf).target; i++) { pet.fullness = 10; pet.feedBerry(i % 3); }
  ck(tasksDone(sf), "feeding the real pet completes FEED");
  ck(tasksNoticeActive(), "and raises the TASK DONE notice");
  tasksHubLabel(lbl, sizeof(lbl), "TASKS");
  char want[32]; snprintf(want, sizeof(want), "TASKS %u!", tasksClaimable());
  ck(tasksClaimable() >= 1 && strcmp(lbl, want) == 0, "the hub row counts it: TASKS n!");
  printf("      hub label: '%s'\n", lbl);
  ck(flushes(), "the page with a CLAIM row flushes");
  uint8_t item = tasksDef(sf).item, qty = tasksDef(sf).qty, had = bag.count(item);
  click(233, ROW_Y(sf));
  ck(tasksClaimed(sf), "a tap on the finished row claims it");
  ck(bag.count(item) == had + qty, "and its reward is in the bag");
  click(233, ROW_Y(sf));
  ck(bag.count(item) == had + qty, "a second tap pays nothing");
  ck(tasksOpen, "claiming keeps the screen open");

  // ---- paging: horizontal pages, and only backs out PAST a page
  onSwipe(-1);
  ck(tasksOpen && tasksPage == 1, "swipe left pages to WEEKLY and stays open");
  ck(flushes(), "the weekly page flushes");
  onSwipe(1);
  ck(tasksOpen && tasksPage == 0, "swipe right pages back to DAILY");
  onSwipe(1);
  ck(!tasksOpen && hubOpen, "swipe right past the first page goes back to the hub");
  click(233, ROW_Y(1));
  ck(tasksOpen && tasksPage == 0, "reopening starts on the daily page");
  onSwipe(-1); onSwipe(-1);
  ck(!tasksOpen && hubOpen, "past the last page also goes back to the hub, never to the pet");
  click(233, ROW_Y(1));
  onSwipeV(1);
  ck(!tasksOpen && hubOpen, "a vertical swipe goes back to the hub");
  click(233, ROW_Y(1));
  click(233, 410);                     // the BACK pill
  ck(!tasksOpen && hubOpen, "BACK goes to the hub");
  hubOpen = false;

  // ---- the minigame and training EXITS in the sketch reach the bus
  {
    uint32_t d2 = 0;
    TaskWorld w = taskWorldOf(pet);
    for (uint32_t d = day + 1; d < day + 800 && !d2; d++) {
      uint8_t x[TASK_DAILY]; tasksPick(d, false, w, x);
      bool p = false, t = false;
      for (int i = 0; i < TASK_DAILY; i++) { p |= DAILY_TASKS[x[i]].kind == TK_PLAY; t |= DAILY_TASKS[x[i]].kind == TK_TRAIN; }
      if (p && t) d2 = d;
    }
    ck(d2 != 0, "a day with PLAY and TRAIN");
    setDay(d2); tasksRefresh();
    int sp = slotOfKind(TK_PLAY), st = slotOfKind(TK_TRAIN);
    pet.sleeping = false; pet.energy = 100;
    startGame(); ck(gameOpen, "the ball game opens");
    gameScore = 4; leaveGame();
    ck(tasksProgress(sp) == 1, "leaving the ball game early counts as a play");
    startSack(); sackHits = 9; leaveSack();
    ck(tasksProgress(st) == 1, "leaving the training bag counts as training");
    startSpeedGame(); spdHits = 5; leaveSpeed();
    ck(tasksProgress(st) == 2, "leaving the reaction test counts as training");
    gameOpen = sackOpen = spdOpen = false;
  }

  // ---- the main-screen notice: drawn over the pet, flushed
  {
    uint32_t d3 = dayWith(TK_CLEAN, day + 900);
    setDay(d3); tasksRefresh();
    int sc = slotOfKind(TK_CLEAN);
    for (int i = 0; i < tasksDef(sc).target; i++) { pet.poops = 1; pet.hygiene = 10; pet.clean(); }
    ck(tasksDone(sc), "bathing the real pet completes CLEAN");
    ck(tasksNoticeActive(), "the notice is up");
    ck(flushes(), "the main screen with the notice flushes");
    uint16_t px = gfx->buffer()[118 * 466 + 120];
    ck(px == UI_BAR_OK, "the TASK DONE banner is on the main screen");
  }

  // ---- loop() drives tasksTick(): the day rolls and the state task counts
  {
    uint32_t d4 = dayWith(TK_WELL_KEPT, day + 1500);
    ck(d4 != 0, "a day with KEEP IT WELL");
    setDay(d4);
    pet.fullness = pet.joy = pet.energy = pet.hygiene = 100; pet.poops = 0; pet.sleeping = false;
    emuSetTimeScale(2000);             // ~60 s of game time per pumped loop
    pump(4);
    emuSetTimeScale(1);
    ck(tasksDay() == d4, "loop() rolled the tasks to the RTC's day");
    int sw = slotOfKind(TK_WELL_KEPT);
    printf("      kept well for %u min\n", tasksProgress(sw));
    ck(sw >= 0 && tasksProgress(sw) >= 2, "loop() ticks the state task once a minute");
  }

  // ---- strings: present, ASCII, and short enough for a row
  {
    bool ok = true, fits = true;
    for (uint8_t l = 0; l < LANG_COUNT; l++) {
      for (uint8_t k = 1; k < TK_COUNT; k++) {
        const char *s = tasksKindLabel(k, l);
        if (!s || !*s) { ok = false; printf("      kind %u lang %u missing\n", k, l); continue; }
        for (const char *c = s; *c; c++) if ((unsigned char)*c < 32 || (unsigned char)*c > 126) ok = false;
        // label at 12 px/char + a gap + the widest count ("60/60MIN") inside a
        // 326 px row with 14 px margins
        if ((int)strlen(s) * 12 + 12 + 8 * 12 > 326 - 28) { fits = false; printf("      '%s' does not fit\n", s); }
      }
      for (uint8_t w = 0; w < 7; w++) {
        const char *s = tasksUiString(w, l);
        if (!s || !*s) { ok = false; continue; }
        for (const char *c = s; *c; c++) if ((unsigned char)*c < 32 || (unsigned char)*c > 126) ok = false;
      }
    }
    ck(ok, "every task string exists and is ASCII in all six languages");
    ck(fits, "every label fits its row next to its count");
  }

  printf(bad ? "tasks_ui_test: %d FAILED\n" : "tasks_ui_test: all passed\n", bad);
  return bad ? 1 : 0;
}
