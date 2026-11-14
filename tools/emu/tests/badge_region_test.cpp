// A gym win must be credited to the ladder it was fought on -- and survive a
// reload. Every other gym check fights Kanto, which is region 0, and that is
// exactly why this bug hid for months: `btlRegion` was declared, read by the
// badge award, the win screen and the replacement roster, and NEVER ASSIGNED.
// It stayed 0, so beating FALKNER (Johto #0) set BROCK's bit (Kanto #0), or
// nothing at all if Brock was already beaten, and his PIDGEOTTO came out as
// Brock's ONIX. From the player's side: badges "sometimes don't save".
//
// Fought through the REAL sketch -- startTrainerBattle() and taps on the move
// grid -- because the rule (`winBadge(region, ...)`) was always right; it was
// the caller that passed the wrong region. CLAUDE.md trap 1.
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include "battle.h"
#include "trainers.h"
#include <cstdio>
#include <chrono>
#include <thread>
uint32_t g_seed = 4242; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX = 0, g_touchY = 0; volatile bool g_touchDown = false;
void FakeESP::restart() { exit(0); }
int FakeSerial::available() { return 0; }
String FakeSerial::readStringUntil(char) { return String(""); }
void setup(); void loop();
extern Pet pet;
extern bool battleOpen, btlWon, pickOpen;
extern Combatant btlYou, btlFoe;
extern uint8_t btlMsgCount, btlFoeAt, gymRegion;
extern uint16_t squadMask;
void startTrainerBattle(uint8_t idx, bool hard);
#define BTL_CELL_X(i) (69 + ((i) % 2) * 168)
#define BTL_CELL_Y(i) (286 + ((i) / 2) * 52)

static int bad = 0;
static void ck(bool ok, const char *w) { printf("%s  %s\n", ok ? "PASS" : "FAIL", w); if (!ok) bad++; }

static void pump(int n) {
  for (int i = 0; i < n; i++) { std::this_thread::sleep_for(std::chrono::milliseconds(30)); loop(); }
}
static void click(int x, int y) {
  g_touchX = x; g_touchY = y; g_touchDown = true; emuFireInterrupt(); pump(5);
  g_touchDown = false; emuFireInterrupt(); pump(3);
}

// Taps through a whole fight. Returns true if every foe creature that came out
// belongs to `want`'s team, in order.
static bool fight(const Trainer &want) {
  bool rosterOk = true;
  int16_t seen = -1;
  for (int taps = 0; battleOpen && taps < 300; taps++) {
    if (btlFoe.dex != seen && !btlFoe.fainted()) {
      seen = btlFoe.dex;
      if (btlFoeAt >= want.count || want.team[btlFoeAt].dex != btlFoe.dex) {
        printf("      foe slot %u is dex %d, %s's slot holds %d\n", btlFoeAt, btlFoe.dex,
               want.name, btlFoeAt < want.count ? want.team[btlFoeAt].dex : -1);
        rosterOk = false;
      }
    }
    if (btlMsgCount) { click(233, 320); continue; }
    int slot = -1;
    for (int i = 0; i < MOVE_SLOTS; i++) if (btlYou.moves[i]) { slot = i; break; }
    if (slot < 0) break;
    click(BTL_CELL_X(slot) + 40, BTL_CELL_Y(slot) + 20);
  }
  return rosterOk;
}

int main() {
  setup(); pump(4);
  click(233, 138); pump(2);            // region: KANTO
  click(233, 145);                     // starter row 0
  ck(!pet.awaitingStarter(), "starter picked");
  if (pet.isEgg()) pet.dbgHatchAs(9, false);  // BLASTOISE: both ladders cap it to
                                              // the leader's level, and water beats Brock
  ck(!pet.isEgg(), "the starter has hatched");

  pet.ageMinutes = 100 * MINUTES_PER_LEVEL;   // capped down to each leader's best anyway
  pet.relearnFromLevel();
  while (pet.hasLearnOffer()) pet.declineLearn();
  squadMask = 0xFFFF; pickOpen = false;

  ck(GYM_REGIONS > 1, "there is a ladder past Kanto to test");
  const Trainer &falkner = TRAINER_SETS[1].list[0];
  ck(falkner.count >= 2, "Johto's first leader has a replacement to send out");

  // ---- win on JOHTO's ladder
  for (int r = 0; r < GYM_REGIONS; r++)
    ck(!pet.hasBadge(r, 0, false), "fresh save: no first badge anywhere");
  gymRegion = 1;
  startTrainerBattle(0, false);
  ck(battleOpen, "Johto gym battle starts");
  bool rosterOk = fight(falkner);
  ck(btlWon, "a L100 creature beats Johto's first leader");
  ck(rosterOk, "every creature sent out comes from the Johto leader's own team");
  ck(pet.hasBadge(1, 0, false), "the JOHTO badge is awarded");
  ck(!pet.hasBadge(0, 0, false), "and Kanto's badge at the same index is NOT");
  ck(pet.badgeCountIn(1, false) == 1 && pet.badgeCount(false) == 1, "exactly one badge in total");

  // leaving the ladder screen mid-win must not move the badge either
  gymRegion = 0;
  if (battleOpen) click(233, 320);     // dismiss the win screen

  // ---- it is on FLASH, not just in RAM: a second Pet reading the same NVS
  {
    Pet fresh; fresh.begin();
    ck(fresh.hasBadge(1, 0, false), "the Johto badge survives a reload");
    ck(!fresh.hasBadge(0, 0, false), "and the reload has no phantom Kanto badge");
  }

  // ---- and Kanto still works, credited to Kanto
  gymRegion = 0;
  startTrainerBattle(0, false);
  fight(TRAINER_SETS[0].list[0]);
  ck(btlWon && pet.hasBadge(0, 0, false), "a Kanto win still lands on Kanto");
  ck(pet.badgeCount(false) == 2, "two badges, one per region");
  {
    Pet fresh; fresh.begin();
    ck(fresh.hasBadge(0, 0, false) && fresh.hasBadge(1, 0, false), "both survive a reload");
  }

  printf(bad ? "badge_region_test: %d FAILED\n" : "badge_region_test: all passed\n", bad);
  return bad ? 1 : 0;
}
