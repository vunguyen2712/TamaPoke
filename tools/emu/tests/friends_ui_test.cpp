// The FRIENDS screen, driven through the REAL sketch: setup(), the hub's
// second page, taps on the screen's rows, the real `lan` Link pumped by loop(),
// the real bag and pet. The emulator has no radio (linkNowBegin() fails), so
// the far board is a second Link + Friends in this process, cross-wired to
// `lan` the way link_test does it. The protocol itself is friends_test's job;
// this proves the SCREEN drives it, flushes every face, and leaves cleanly.
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include "battle.h"
#include "items.h"
#include "link.h"
#include "friends.h"
#include "rtcbat.h"
#include <cstdio>
#include <cstring>
#include <chrono>
#include <thread>
uint32_t g_seed = 4242; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX = 0, g_touchY = 0; volatile bool g_touchDown = false;
void FakeESP::restart() { exit(0); }
int FakeSerial::available() { return 0; }
String FakeSerial::readStringUntil(char) { return String(""); }
void setup(); void loop(); void render(); void onSwipe(int dir); void onSwipeV(int dir);
uint8_t uiCurrentScreen();
extern const char *const SCREEN_NAME[];
extern Pet pet;
extern Link lan;
extern Arduino_Canvas *gfx;
extern bool hubOpen, friendsOpen, lanOpen;
extern uint8_t hubPage;
void friendsHubLabel(char *out, size_t n, const char *name);
#define ROW(i) (88 + (i) * 50 + 22)
#define CXT 233
#define BACK_Y 410

static int bad = 0;
static void ck(bool ok, const char *w) { printf("%s  %s\n", ok ? "PASS" : "FAIL", w); if (!ok) bad++; }

// ---- the far board ----------------------------------------------------------
static Link peer; static Friends peerF; static Bag peerBag; static Pet peerPet;
static bool peerOn = false;
static void toPeer(void*, const uint8_t *b, uint8_t n) {
  uint8_t t[260]; memcpy(t, b, n); peer.onPacket(t, n);
}
static void toLan(void*, const uint8_t *b, uint8_t n) {
  uint8_t t[260]; memcpy(t, b, n); lan.onPacket(t, n);
}

static void pump(int n) {
  for (int i = 0; i < n; i++) {
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    loop();
    if (peerOn) {
      peer.tick(millis());
      peerF.poll(peer, peerPet, peerBag, pet.currentDay(), millis());
    }
  }
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

// What friendsConnect() does, minus the radio this emulator does not have:
// the sketch's own session on `lan`, and the far board joining it.
static void connectPeer() {
  lan.begin(true, pet.trainerName);
  friends.sessionStart(lan, pet);
  lan.send = toPeer;
  peer.begin(false, "BEA");
  snprintf(peer.peerName, sizeof(peer.peerName), "%s", "BEA");
  peer.id = 0x2222; lan.id = 0x1111;
  peerF.sessionStart(peer, peerPet);
  peer.send = toLan;
  peerOn = true;
  peer.start();
  lan.start();
  pump(20);
}
static void openFriends() {
  hubOpen = true; hubPage = 0;
  click(CXT, ROW(5));                  // MORE >
  click(CXT, ROW(1));                  // page 2, row 1: FRIENDS
}

int main() {
  setup(); pump(4);
  click(233, 138); pump(2);            // region: KANTO
  click(233, 145);                     // starter row 0
  setDay(DAY0, 13);
  pet.dbgHatchAs(4, false);
  pet.joy = 50;
  peerPet.dbgHatchAs(25, true);
  peerPet.joy = 50;

  // ---- the hub's FRIENDS row opens the screen
  openFriends();
  ck(friendsOpen && on("friends"), "page 2 of the hub opens FRIENDS");
  ck(!friends.active && lan.state == LINK_OFF, "opening it touches no link");
  flushes("host-or-join");

  // ---- HOST with no radio says so instead of hanging
  click(CXT, ROW(1));
  ck(friends.active && lan.mode == LINK_MODE_FRIENDS, "HOST starts a friends-mode session on the LAN link");
  ck(lan.state == LINK_REFUSED && lan.refusedWhy == LINK_WHY_RADIO, "no radio: refused with the reason");
  ck(lan.mineN == 1 && lan.mine[0].dex == 4, "advertising the live pet as the card");
  flushes("no radio");
  lan.refusedWhy = LINK_WHY_MODE; flushes("friend-on-LAN");
  lan.refusedWhy = LINK_WHY_BUILD; flushes("different version");

  // ---- paired with the far board
  connectPeer();
  ck(lan.ready() && peer.ready() && friends.havePeer && peerF.havePeer, "the screen's session pairs with the far board");
  ck(friends.peerMon.valid && friends.peerMon.dex == 25 && friends.peerMon.shiny, "and shows its creature");
  ck(on("friends"), "still on FRIENDS");
  flushes("paired");

  // ---- a gift swap from the screen's own rows
  bag.clearAll();
  bag.add(ITEM_ORAN_BERRY, 1);
  bag.add(ITEM_SITRUS_BERRY, 2);
  peerBag.add(ITEM_GREATBALL, 1);
  click(CXT, ROW(1));                  // GIFT
  flushes("gift picker");
  click(CXT, ROW(1));                  // cycle: ORAN -> SITRUS
  click(CXT, ROW(2));                  // OFFER
  ck(friends.gph == FG_OFFER && friends.gmine == ITEM_SITRUS_BERRY, "the picker's item is what gets offered");
  ck(bag.count(ITEM_SITRUS_BERRY) == 2, "and nothing leaves the bag on offering");
  flushes("waiting for friend");
  peerF.offerGift(peer, peerBag, ITEM_GREATBALL, pet.currentDay());
  pump(20);
  ck(friends.gph == FG_DONE && peerF.gph == FG_DONE, "the swap completes on both boards");
  ck(bag.count(ITEM_SITRUS_BERRY) == 1 && bag.count(ITEM_GREATBALL) == 1 &&
     peerBag.count(ITEM_SITRUS_BERRY) == 1 && peerBag.count(ITEM_GREATBALL) == 0, "both bags are right");
  ck(friends.giftDay == pet.currentDay(), "the sketch's day is the one recorded");
  flushes("got it");
  click(CXT, ROW(1));                  // GIFT again
  ck(friends.gph == FG_DONE && bag.count(ITEM_SITRUS_BERRY) == 1, "a second gift today is refused at the row");
  ck(on("friends"), "and the screen stays put");

  // ---- a visit from the VISIT row
  click(CXT, ROW(3));
  pump(5);
  ck(friends.visiting(millis()), "VISIT shows the two pets together");
  ck(peerF.visiting(millis()), "on the far board too");
  ck(pet.joy == 50 + FRIEND_VISIT_JOY && pet.bond >= FRIEND_VISIT_BOND, "the live pet got the visit bonus");
  flushes("visit");
  click(CXT, BACK_Y);                  // BACK during a visit just ends the look
  ck(!friends.visiting(millis()) && friendsOpen && lan.ready(), "BACK during the visit returns to the menu, still paired");

  // ---- the horizontal swipe leaves: goodbye, radio off, back to the hub
  onSwipe(1); pump(3);
  ck(!friendsOpen && hubOpen && on("hub"), "a swipe goes back to the hub");
  ck(lan.state == LINK_OFF && !friends.active, "and the session is closed");
  ck(peer.state == LINK_LOST, "with a goodbye the far board hears at once");
  peerOn = false;

  // ---- a VERTICAL swipe leaves through the hub's own path, and must too
  openFriends();
  ck(friendsOpen, "back on FRIENDS");
  connectPeer();
  ck(lan.ready(), "paired again");
  onSwipeV(1); pump(3);
  ck(!friendsOpen && lan.state == LINK_OFF && !friends.active, "a vertical swipe closes the session too");
  ck(peer.state == LINK_LOST, "and says goodbye");
  peerOn = false;

  // ---- a friend who walks away is reported, and HOST is offered again
  openFriends();
  connectPeer();
  peer.sendBye(); peerOn = false;
  pump(3);
  ck(lan.state == LINK_LOST && friends.active, "the far board leaving is noticed at once");
  flushes("friend left");
  click(CXT, ROW(1));                  // HOST again
  ck(lan.state == LINK_REFUSED && lan.refusedWhy == LINK_WHY_RADIO, "and HOST can be tapped again");
  click(CXT, BACK_Y);
  ck(!friendsOpen && hubOpen && !friends.active && lan.state == LINK_OFF, "BACK leaves to the hub");

  // ---- the hub label marks a swap left in doubt
  char lbl[32];
  friendsHubLabel(lbl, sizeof(lbl), "FRIENDS");
  ck(!strcmp(lbl, "FRIENDS"), "no mark with nothing in transit");
  friends.rec.state = FR_PREPARED;
  friendsHubLabel(lbl, sizeof(lbl), "FRIENDS");
  ck(!strcmp(lbl, "FRIENDS *"), "a mark while a gift is in transit");
  friends.rec.state = FR_NONE;

  printf(bad ? "\nFAILURES: %d\n" : "\nall friends UI checks passed\n", bad);
  return bad ? 1 : 0;
}
