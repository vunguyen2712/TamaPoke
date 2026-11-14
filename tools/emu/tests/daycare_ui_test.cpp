// The DAYCARE screen, driven through the real sketch: taps on the hub, the
// parent rows, the picker (paged), BREED and SWAP IN NOW. breed_test proves the
// rule; this proves the screen asks it -- CLAUDE.md trap 1, "test the CALLER".
// A fresh harness from a known state (trap 3: a test at the end of a long
// suite runs in whatever state the suite left behind).
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include "items.h"
#include "breed.h"
#include <cstdio>
#include <cstring>
#include <chrono>
#include <thread>
uint32_t g_seed = 5150; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
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
extern bool hubOpen, daycareOpen;

static int bad = 0;
static void ck(bool ok, const char *w) { printf("%s  %s\n", ok ? "PASS" : "FAIL", w); if (!ok) bad++; }
static void pump(int n) {
  for (int i = 0; i < n; i++) { std::this_thread::sleep_for(std::chrono::milliseconds(30)); loop(); }
}
static void click(int x, int y) {
  g_touchX = x; g_touchY = y; g_touchDown = true; emuFireInterrupt(); pump(5);
  g_touchDown = false; emuFireInterrupt(); pump(3);
}
static const char *screen() { return SCREEN_NAME[uiCurrentScreen()]; }
static bool flushes() { gfx->frameReady = false; render(); return gfx->frameReady; }

// ui_hub.ino's row geometry: rows at y = 88 + i*50, 44 tall, x 70..396
static void row(int i) { click(233, 88 + i * 50 + 22); }
#define ROW_A 0
#define ROW_B 1
#define ROW_BREED 2
#define ROW_SWAP 3

static PartyMon mon(int16_t dex, uint8_t iv) {
  PartyMon m; m.dex = dex; m.level = 20; m.ivAtk = m.ivDef = m.ivSpe = m.ivHp = iv; return m;
}

int main() {
  setup(); pump(4);
  click(233, 138); pump(2);            // region: KANTO
  click(233, 145);                     // starter row 0
  ck(!pet.awaitingStarter(), "starter picked");
  ck(pet.isEgg() && pet.eggCracks() == 0, "the live creature is an uncracked egg");
  breedClearAll();

  // banked, party then box: list order is the picker's order, five a page
  for (int i = 0; i < PARTY_SLOTS; i++) party.releaseAt(i);
  for (int i = 0; i < BOX_SLOTS; i++) party.boxReleaseAt(i);
  party.add(mon(8, 10));               // list 0: WARTORTLE
  party.add(mon(BREED_DITTO, 20));     // list 1: DITTO
  party.add(mon(25, 12));              // list 2: PIKACHU
  party.add(mon(26, 14));              // list 3: RAICHU
  party.add(mon(1, 15));               // list 4: BULBASAUR
  party.boxAdd(mon(131, 25));          // list 5: LAPRAS -- page 2

  // ---- into the daycare from the hub
  hubOpen = true; pump(2);
  ck(!strcmp(screen(), "hub"), "the hub is up");
  row(3);                              // DAYCARE is the hub's 4th row
  ck(!strcmp(screen(), "daycare"), "the hub's DAYCARE row opens the daycare");
  ck(flushes(), "the daycare home view flushes");

  // ---- BREED with nothing picked does nothing
  row(ROW_BREED);
  ck(!breedHasQueued(), "BREED with no parents queues nothing");

  // ---- an incompatible pair through the picker: WARTORTLE + PIKACHU
  row(ROW_A);
  ck(!strcmp(screen(), "daycare") && flushes(), "parent A's picker opens, and flushes");
  row(0);                              // WARTORTLE
  row(ROW_B); row(2);                  // PIKACHU
  row(ROW_BREED);
  ck(!breedHasQueued(), "WARTORTLE + PIKACHU (no shared type): BREED refuses");

  // ---- the picker pages, and backs out past page 1
  row(ROW_A);
  onSwipe(-1);                         // next page
  ck(!strcmp(screen(), "daycare"), "a horizontal swipe in the picker stays on the daycare");
  row(0);                              // page 2, row 0 = LAPRAS
  row(ROW_B); row(1);                  // DITTO
  row(ROW_A);
  onSwipe(+1);                         // past page 1: back to HOME, not the hub
  ck(!strcmp(screen(), "daycare"), "swiping back past page 1 returns to the daycare home");
  row(ROW_BREED);                      // only HOME has BREED on row 2
  ck(breedHasQueued() && breedQueued().dex == 131,
     "page 2's pick bred: LAPRAS + DITTO queued a LAPRAS egg (the swipe paged, the back-swipe went home)");
  ck(party.count() == 5 && party.boxCount() == 1 && party.box[0].dex == 131,
     "the parents are still banked");

  // ---- one at a time, and once a day
  breedClearAll();
  row(ROW_A); row(0);                  // WARTORTLE
  row(ROW_BREED);
  ck(breedHasQueued() && breedQueued().dex == 7, "WARTORTLE + DITTO queue a SQUIRTLE");
  BredEgg q = breedQueued();
  row(ROW_A); row(3);                  // RAICHU
  row(ROW_B); row(2);                  // PIKACHU
  row(ROW_BREED);
  ck(breedQueued().dex == 7, "a second BREED while an egg waits changes nothing");

  // ---- SWAP IN NOW: two taps, then the egg on screen is the bred one
  int16_t before = pet.eggPeek();
  row(ROW_SWAP);
  ck(pet.eggPeek() == before && breedHasQueued(), "the first SWAP tap only arms it");
  row(ROW_SWAP);
  ck(pet.isEgg() && pet.eggPeek() == 7 && !breedHasQueued(), "the second SWAP tap puts the SQUIRTLE egg in");
  row(ROW_BREED);
  ck(!breedHasQueued(), "same day after a swap: BREED is on cooldown");

  // ---- the swapped egg hatches with what it inherited
  pet.eggTap(); pet.eggTap(); pet.eggTap();
  {
    uint8_t got[4] = { pet.ivAtk, pet.ivDef, pet.ivSpe, pet.ivHp };
    bool same = pet.speciesId == 7;
    for (int s = 0; s < 4; s++) {
      if (q.iv[s] == BREED_IV_ROLL) continue;
      uint8_t want = (pet.shiny && q.iv[s] < 20) ? 20 : q.iv[s];
      if (got[s] != want) same = false;
    }
    ck(same, "it hatches a SQUIRTLE with the inherited IVs");
  }
  ck(!strcmp(screen(), "daycare"), "(still on the daycare)");
  ck(flushes(), "the home view with a hatched creature flushes");

  // ---- a horizontal swipe on HOME goes back to the hub
  onSwipe(-1);
  ck(!strcmp(screen(), "hub"), "a horizontal swipe on the daycare home returns to the hub");

  printf(bad ? "\n%d FAILED\n" : "\nall passed\n", bad);
  return bad ? 1 : 0;
}
