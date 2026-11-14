// The BAG screen, driven through the real sketch with taps: give an item, take
// it back, the item surviving banking and a reload, the let-go paths returning
// the item to the bag, and paging (a horizontal swipe pages, and past the end
// backs out one level -- CLAUDE.md "swipe_test": that bug shipped four times).
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include "items.h"
#include <cstdio>
#include <cstring>
#include <chrono>
#include <thread>
uint32_t g_seed = 31337; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX = 0, g_touchY = 0; volatile bool g_touchDown = false;
void FakeESP::restart() { exit(0); }
int FakeSerial::available() { return 0; }
String FakeSerial::readStringUntil(char) { return String(""); }
void setup(); void loop(); void render();
void onSwipe(int dir);
void partyTap(int16_t, int16_t);
extern Pet pet;
extern Arduino_Canvas *gfx;
extern bool hubOpen, bagOpen, partyOpen, partyPick;
extern uint8_t bagPage, bagGive, bagGivePage;
uint8_t bagListPages();
#include "battle.h"
extern bool battleOpen;
extern Combatant btlYou;
extern uint8_t btlMsgCount;
void startBattle(int16_t dex, uint8_t lvl);
#define BTL_CELL_X(i) (69 + ((i) % 2) * 168)
#define BTL_CELL_Y(i) (286 + ((i) / 2) * 52)
uint8_t bagGivePages();

static int bad = 0;
static void ck(bool ok, const char *w) { printf("%s  %s\n", ok ? "PASS" : "FAIL", w); if (!ok) bad++; }

// ui_hub.ino's geometry: rows at 88 + i*50, 44 tall, centred; BACK pill at 392.
#define ROW_Y(i) (88 + (i) * 50 + 22)
#define BACK_Y (392 + 20)
static void pump(int n) { for (int i = 0; i < n; i++) { std::this_thread::sleep_for(std::chrono::milliseconds(30)); loop(); } }
static void tap(int x, int y) {
  g_touchX = x; g_touchY = y; g_touchDown = true; emuFireInterrupt(); pump(5);
  g_touchDown = false; emuFireInterrupt(); pump(3);
}
static void flushed(const char *what) {
  gfx->frameReady = false; render();
  ck(gfx->frameReady, what);
}
static PartyMon mon(int16_t dex, const char *nick) {
  PartyMon m; m.dex = dex; m.level = 30; m.ivAtk = m.ivDef = m.ivSpe = m.ivHp = 20;
  snprintf(m.nick, sizeof(m.nick), "%s", nick); return m;
}
static void openBag() {
  hubOpen = true; bagOpen = false; pump(2);
  tap(233, ROW_Y(4));                       // the hub's BAG row
}

int main() {
  setup(); pump(3);
  if (pet.awaitingStarter()) pet.chooseStarter(4);
  if (pet.isEgg()) pet.dbgHatchAs(25, false);
  pet.ageMinutes = 60UL * 30;
  while (pet.hasLearnOffer()) pet.declineLearn();
  pet.held = 0; pet.saveNow();
  for (int i = 0; i < PARTY_SLOTS; i++) party.slots[i] = PartyMon();
  for (int i = 0; i < BOX_SLOTS; i++) party.box[i] = PartyMon();
  party.slots[0] = mon(6, "ZARD"); party.save(); party.boxSave();
  bag.clearAll();
  bag.add(ITEM_ORAN_BERRY, 1);
  bag.add(ITEM_LEFTOVERS, 2);

  openBag();
  ck(bagOpen && !hubOpen, "the hub's BAG row opens the bag");
  flushed("the bag list flushes");
  // list: row 0 ORAN x1, row 1 LEFTOVERS x2
  tap(233, ROW_Y(1));
  ck(bagGive == ITEM_LEFTOVERS, "tapping LEFTOVERS starts giving it");
  flushed("the give list flushes");
  tap(233, ROW_Y(0));                       // give rows: 0 = the live pet
  ck(pet.held == ITEM_LEFTOVERS && bag.count(ITEM_LEFTOVERS) == 1 && bagGive == 0,
     "tapping the live pet gives it LEFTOVERS and returns to the list");
  // list now: ORAN, LEFTOVERS x1, holder row (pet: LEFTOVERS)
  tap(233, ROW_Y(2));
  ck(pet.held == 0 && bag.count(ITEM_LEFTOVERS) == 2, "tapping the holder row takes it back");

  // give ORAN to the party member (give row 1)
  tap(233, ROW_Y(0));
  ck(bagGive == ITEM_ORAN_BERRY, "ORAN selected");
  tap(233, BACK_Y);
  ck(bagOpen && bagGive == 0, "BACK in the give list returns to the list, not the hub");
  tap(233, ROW_Y(0));
  tap(233, ROW_Y(1));
  ck(party.slots[0].held == ITEM_ORAN_BERRY && !bag.has(ITEM_ORAN_BERRY), "a party member holds the ORAN BERRY");
  { Party q; q.begin(); ck(q.slots[0].held == ITEM_ORAN_BERRY, "and it survives a reload"); }

  // ---- banking: the live pet holds LEFTOVERS, retires by release, joins the party
  tap(233, ROW_Y(0));                       // list row 0 is LEFTOVERS now (ORAN is gone)
  ck(bagGive == ITEM_LEFTOVERS, "LEFTOVERS selected again");
  tap(233, ROW_Y(0));
  ck(pet.held == ITEM_LEFTOVERS, "the pet holds it");
  tap(233, BACK_Y);
  ck(!bagOpen && hubOpen, "BACK on the list returns to the hub");
  hubOpen = false;
  int16_t was = pet.speciesId;
  pet.release();
  for (int i = 0; i < 400 && !pet.isEgg(); i++) pump(1);   // the ceremony runs on real time
  pump(3);
  int slot = -1;
  for (int i = 0; i < PARTY_SLOTS; i++) if (party.slots[i].dex == was && i != 0) slot = i;
  ck(pet.isEgg() && slot > 0, "the released creature was banked into the party");
  ck(slot > 0 && party.slots[slot].held == ITEM_LEFTOVERS && pet.held == 0,
     "carrying its LEFTOVERS; the egg holds nothing");
  { Party q; q.begin(); ck(slot > 0 && q.slots[slot].held == ITEM_LEFTOVERS, "on flash too"); }

  // ---- the let-go paths give the item back
  {
    // releasing a banked one from its sheet
    uint8_t before = bag.count(ITEM_LEFTOVERS);
    partyOpen = true; pump(2);
    // party cells: 78 + (i%2)*160, 88 + (i/2)*78
    partyTap(78 + (slot % 2) * 160 + 40, 88 + (slot / 2) * 78 + 30);
    partyTap(320, 336 + 24);                // RELEASE
    partyTap(233, 206 + 26);                // YES
    ck(party.slots[slot].empty() && bag.count(ITEM_LEFTOVERS) == before + 1,
       "releasing a banked creature puts its item back in the bag");
    partyOpen = false; pump(2);
  }
  {
    // a newcomer with nowhere to go, let go from the party picker
    for (int i = 0; i < PARTY_SLOTS; i++) party.slots[i] = mon(10 + i, "P");
    for (int i = 0; i < BOX_SLOTS; i++) party.box[i] = mon(40 + i, "B");
    party.save(); party.boxSave();
    pet.endedMon = mon(150, "MEW2"); pet.endedMon.held = ITEM_SITRUS_BERRY;
    pet.endedKind = CER_FAREWELL;
    pump(3);
    ck(partyPick && partyOpen, "a full party and box asks who to replace");
    tap(233, 392);                          // the party screen's exit button
    ck(!partyPick && pet.endedKind == CER_NONE && bag.count(ITEM_SITRUS_BERRY) == 1,
       "declining lets it go -- and its SITRUS BERRY comes back to the bag");
    pump(2);
    // ...or it REPLACES someone, who leaves their item behind in the bag
    party.slots[2].held = ITEM_LUM_BERRY; party.save();
    uint8_t lums = bag.count(ITEM_LUM_BERRY);
    pet.endedMon = mon(151, "MEW"); pet.endedMon.held = ITEM_QUICK_CLAW;
    pet.endedKind = CER_FAREWELL;
    pump(3);
    ck(partyPick, "asked again");
    partyTap(78 + (2 % 2) * 160 + 40, 88 + (2 / 2) * 78 + 30);
    ck(party.slots[2].dex == 151 && party.slots[2].held == ITEM_QUICK_CLAW,
       "the newcomer takes slot 3, holding its own QUICK CLAW");
    ck(bag.count(ITEM_LUM_BERRY) == lums + 1, "and the one it replaced left its LUM BERRY in the bag");
    pump(2);
  }

  // ---- paging: enough rows for more than one page in BOTH modes
  for (uint8_t it = 1; it < ITEM_COUNT; it++) bag.add(it, 1);
  openBag();
  ck(bagListPages() > 1, "the list has more than one page");
  ck(bagPage == 0, "it opens on page 1");
  onSwipe(-1);
  ck(bagOpen && bagPage == 1, "a horizontal swipe PAGES the list (and the bag stays open)");
  flushed("page 2 flushes");
  onSwipe(+1);
  ck(bagOpen && bagPage == 0, "and swiping back returns to page 1");
  onSwipe(+1);
  ck(!bagOpen && hubOpen, "past page 1 it backs out to the hub");
  openBag();
  tap(233, ROW_Y(3));                       // a held item on page 1 (balls are rows 0-2)
  ck(bagGive != 0, "a held item is being given");
  ck(bagGivePages() > 1, "the give list (pet, 6 party, 18 box) has more than one page");
  onSwipe(-1);
  ck(bagOpen && bagGive && bagGivePage == 1, "the give list pages too");
  onSwipe(+1); onSwipe(+1);
  ck(bagOpen && bagGive == 0, "and past its first page backs out to the LIST, not the hub");
  // the last page is a real end, not a wrap
  uint8_t pages = bagListPages();
  for (int i = 0; i < pages - 1; i++) onSwipe(-1);
  ck(bagPage == pages - 1, "every page of the list is reachable");
  onSwipe(-1);
  ck(!bagOpen, "and swiping past the LAST page leaves too, rather than wrapping");
  hubOpen = false;

  // ---- a real fight: the FOCUS SASH saves the live pet once and is then gone
  //      from it (the consumption rule), through btlResolve() -> bagBattleSync()
  {
    pet.dbgHatchAs(19, false);              // a level 1 RATTATA
    while (pet.hasLearnOffer()) pet.declineLearn();
    bag.add(ITEM_FOCUS_SASH, 1);
    ck(heldGive(pet, party, HOLDER_PET, ITEM_FOCUS_SASH), "the pet holds a FOCUS SASH");
    uint8_t sashes = bag.count(ITEM_FOCUS_SASH);
    startBattle(150, 100);                  // MEWTWO L100: one hit is a knockout
    ck(battleOpen && btlYou.held == ITEM_FOCUS_SASH, "the fight starts with the sash on the pet's combatant");
    bool saved = false;
    for (int taps = 0; battleOpen && taps < 60 && !btlYou.fainted(); taps++) {
      if (btlYou.itemUsed && btlYou.hp == 1) saved = true;
      if (btlMsgCount) { tap(233, 320); continue; }
      int slot = -1;
      for (int i = 0; i < MOVE_SLOTS; i++) if (btlYou.moves[i]) { slot = i; break; }
      if (slot < 0) break;
      tap(BTL_CELL_X(slot) + 40, BTL_CELL_Y(slot) + 20);
      if (btlYou.itemUsed && btlYou.hp == 1) saved = true;
    }
    ck(saved, "MEWTWO's knockout blow left it at 1 HP");
    ck(pet.held == 0, "and the sash is gone from the live pet the moment it fired");
    { Pet q; q.begin(); ck(q.held == 0, "on flash too"); }
    ck(bag.count(ITEM_FOCUS_SASH) == sashes, "and not back in the bag either: it was used");
  }

  printf("%s\n", bad ? "FAILURES" : "all good");
  return bad ? 1 : 0;
}
