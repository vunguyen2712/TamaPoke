// Two-board gifts and visits, over the real Link, cross-wired in one process
// the way link_test and lossy_test do it. The radio has never run and the
// owner has ONE board, so every claim in docs/adventure-friends.md is proven
// here instead: a swap moves exactly one item each way, a dropped, doubled or
// silenced frame at ANY point never duplicates or destroys one, a side left in
// doubt settles at the next meeting, once a day survives a reload, an invalid
// or key item off the wire is refused, and a visit pays once a day.
//
// Each board has its OWN NVS: the emulator's store is one global map, so it is
// swapped in and out around every call that may write (use()). Nothing that
// runs inside Link::onPacket writes NVS -- friends.cpp only reads the parked
// frame on the next poll -- which is what makes the swap sound.
#include "Arduino.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include "battle.h"
#include "items.h"
#include "link.h"
#include "friends.h"
#include "game_events.h"
#include <cstdio>
uint32_t g_seed=77; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX=0,g_touchY=0; volatile bool g_touchDown=false; bool wasPressed=false;
uint32_t millis(){return 0;} void FakeESP::restart(){exit(0);}
int FakeSerial::available(){return 0;} String FakeSerial::readStringUntil(char){return String("");}
void sfxPlay(uint8_t){}
static int bad=0;
static void ck(bool ok,const char*w){printf("%s  %s\n",ok?"PASS":"FAIL",w); if(!ok)bad++;}

// ---- two boards ------------------------------------------------------------
struct Board {
  Link L; Friends F; Bag B; Pet P; NvsStore nv;
  int ev[EV_COUNT] = {0}; int16_t lastArg[EV_COUNT] = {0};
};
static Board X[2];
static int cur = -1;
static void use(int b) {
  if (cur == b) return;
  if (cur >= 0) X[cur].nv = nvs();
  nvs() = X[b].nv;
  cur = b;
}
static void sink(GameEv e, int16_t a) { if (cur >= 0) { X[cur].ev[e]++; X[cur].lastArg[e] = a; } }

// ---- a radio that misbehaves on request ------------------------------------
static int dropEvery = 0, dupEvery = 0, seen = 0, dropped = 0;
static bool deaf = false;
// a targeted fault: return true to drop this frame (from = sending board)
static bool (*filter)(int from, const uint8_t *b, uint8_t n) = nullptr;
// and a forger: may rewrite a frame in flight
static void (*forge)(int from, uint8_t *b, uint8_t n) = nullptr;
static void wire(int from, const uint8_t *b, uint8_t n) {
  seen++;
  if (deaf) { dropped++; return; }
  if (dropEvery && (seen % dropEvery) == 0) { dropped++; return; }
  if (filter && filter(from, b, n)) { dropped++; return; }
  uint8_t tmp[260]; if (n > sizeof(tmp)) return;
  memcpy(tmp, b, n);
  if (forge) forge(from, tmp, n);
  X[1 - from].L.onPacket(tmp, n);
  if (dupEvery && (seen % dupEvery) == 0) X[1 - from].L.onPacket(tmp, n);
}
static void toB(void*, const uint8_t*b, uint8_t n){ wire(0, b, n); }
static void toA(void*, const uint8_t*b, uint8_t n){ wire(1, b, n); }
static bool isFriend(const uint8_t *b, uint8_t n) { return n >= 2 + FRIEND_FRAME_LEN && b[0] == LM_FRIEND; }

static uint32_t T = 1000, DAY = 100;
static void step() {
  T += 100;
  X[0].L.tick(T); X[1].L.tick(T);
  for (int b = 0; b < 2; b++) { use(b); X[b].F.poll(X[b].L, X[b].P, X[b].B, DAY, T); }
}
static void run(uint32_t ms) { for (uint32_t t = 0; t < ms; t += 100) step(); }

// Pair the two boards in FRIENDS mode, board 0 preferring to host.
static void pair(uint32_t ms = 3000) {
  X[0].L.send = toB; X[1].L.send = toA;
  for (int b = 0; b < 2; b++) {
    use(b);
    X[b].L.begin(b == 0, b ? "BEA" : "ALI");
    snprintf(X[b].L.peerName, sizeof(X[b].L.peerName), "%s", b ? "BEA" : "ALI");
    X[b].L.id = b ? 0x2222 : 0x1111;
    X[b].F.sessionStart(X[b].L, X[b].P);
  }
  X[0].L.tick(T); X[1].L.tick(T);
  X[0].L.start(); X[1].L.start();
  run(ms);
}
static void leave() {   // both sides walk away cleanly
  for (int b = 0; b < 2; b++) { X[b].F.sessionEnd(); X[b].L.state = LINK_OFF; }
}
static void reloadFriends(int b) { use(b); X[b].F = Friends(); X[b].F.begin(); X[b].B.begin(); }

// The ledger while a swap is in doubt. A guest's escrowed item still counts as
// its own UNLESS the host has committed (then it is already in the host's
// bag); a host that committed while the guest is still unsure OWES its own
// item to the guest (it left the host's bag, and arrives at the next meeting).
static int pending(int b, uint8_t it) {
  const FriendRecord &r = X[b].F.rec, &o = X[1 - b].F.rec;
  bool otherCommitted = o.state == FR_COMMITTED && o.xid == r.xid;
  bool otherPrepared = o.state == FR_PREPARED && o.xid == r.xid;
  if (r.state == FR_PREPARED && !r.giveFree && r.give == it && !otherCommitted) return 1;
  if (r.state == FR_COMMITTED && !r.giveFree && r.give == it && otherPrepared) return 1;
  return 0;
}
static int total(uint8_t it) {
  return X[0].B.count(it) + X[1].B.count(it) + pending(0, it) + pending(1, it);
}
static void fill(int b, uint8_t it, uint8_t n) { use(b); X[b].B.add(it, n); }
static void freshBoards() {
  for (int b = 0; b < 2; b++) {
    use(b);
    nvs().clear();
    X[b].F = Friends(); X[b].F.begin();
    X[b].B.begin(); X[b].B.clearAll();
    X[b].P.dbgHatchAs(b ? 25 : 133, b == 1);   // EEVEE, and a shiny PIKACHU
    X[b].P.joy = 50;
    for (int e = 0; e < EV_COUNT; e++) X[b].ev[e] = 0;
  }
  filter = nullptr; forge = nullptr; deaf = false; dropEvery = dupEvery = 0;
}
static bool offer(int b, uint8_t it) { use(b); return X[b].F.offerGift(X[b].L, X[b].B, it, DAY); }

// The standard swap: ALI (host) offers an ORAN BERRY, BEA a POKE BALL.
static void standardBags() {
  fill(0, ITEM_ORAN_BERRY, 2);
  fill(1, ITEM_POKEBALL, 1);
  fill(1, ITEM_LUM_BERRY, 1);
}
static bool swapped() {
  return X[0].B.count(ITEM_ORAN_BERRY) == 1 && X[0].B.count(ITEM_POKEBALL) == 1 &&
         X[1].B.count(ITEM_ORAN_BERRY) == 1 && X[1].B.count(ITEM_POKEBALL) == 0 &&
         X[1].B.count(ITEM_LUM_BERRY) == 1;
}
static bool untouched() {
  return X[0].B.count(ITEM_ORAN_BERRY) == 2 && X[0].B.count(ITEM_POKEBALL) == 0 &&
         X[1].B.count(ITEM_ORAN_BERRY) == 0 && X[1].B.count(ITEM_POKEBALL) == 1 &&
         X[1].B.count(ITEM_LUM_BERRY) == 1;
}
static bool conserved() { return total(ITEM_ORAN_BERRY) == 2 && total(ITEM_POKEBALL) == 1; }

// faults aimed at one exact moment of the commit
static bool dropGuestVote(int from, const uint8_t *b, uint8_t n) {
  return from == 1 && isFriend(b, n) && b[2 + FF_GPH] == FG_PREPARED;
}
static bool dropHostCommit(int from, const uint8_t *b, uint8_t n) {
  return from == 0 && isFriend(b, n) && b[2 + FF_RST] == FR_COMMITTED;
}
static bool dropGuestDone(int from, const uint8_t *b, uint8_t n) {
  return from == 1 && isFriend(b, n) && b[2 + FF_GPH] == FG_DONE;
}
static uint8_t forgedItem = 0;
static void forgeHostOffer(int from, uint8_t *b, uint8_t n) {
  if (from == 0 && isFriend(b, n)) b[2 + FF_GMINE] = forgedItem;
}
static void forgeGuestOffer(int from, uint8_t *b, uint8_t n) {
  if (from == 1 && isFriend(b, n)) b[2 + FF_GMINE] = forgedItem;
}

static uint8_t stale[2 + FRIEND_FRAME_LEN]; static uint8_t staleN = 0;
static void keepGuestOffer(int from, uint8_t *b, uint8_t n) {
  if (from == 1 && isFriend(b, n) && b[2 + FF_GPH] == FG_OFFER) { memcpy(stale, b, 2 + FRIEND_FRAME_LEN); staleN = n; }
}
static void forgeGuestExpects(int from, uint8_t *b, uint8_t n) {
  if (from == 1 && isFriend(b, n) && b[2 + FF_GPH] == FG_PREPARED) b[2 + FF_GTHEIRS] = ITEM_LUM_BERRY;
}

static LinkMon battleMon(int16_t dex){
  Pet p; p.dbgHatchAs(dex,false);
  p.ivAtk=p.ivDef=p.ivSpe=p.ivHp=25; p.ageMinutes=49*MINUTES_PER_LEVEL; p.relearnFromLevel();
  Combatant c; combatantFromPet(c,p); LinkMon m; linkMonFrom(m,c); return m;
}

int main(){
  gameEventSubscribe(sink);

  // --- the rule, and the card --------------------------------------------------
  ck(friendsGiftable(ITEM_ORAN_BERRY) && friendsGiftable(ITEM_POKEBALL), "berries and balls are giftable");
  ck(!friendsGiftable(ITEM_SHINY_CHARM) && !friendsGiftable(ITEM_OVAL_CHARM), "key items are not");
  ck(!friendsGiftable(ITEM_NONE) && !friendsGiftable(ITEM_COUNT) && !friendsGiftable(0xEE),
     "nor is nothing, or an id past the table");
  {
    LinkMon m; memset(&m, 'A', sizeof(m));       // a name with no NUL anywhere
    m.dex = 25; m.level = 0; m.shiny = 7;
    FriendPeerMon v;
    ck(friendsPeerMon(m, v) && v.dex == 25 && v.level == 1 && v.shiny, "a peer card is read and clamped");
    ck(strlen(v.name) == LINK_NAME_LEN - 1, "and an unterminated name is cut, not overrun");
    m.level = 255; friendsPeerMon(m, v);
    ck(v.level == MAX_LEVEL, "a level past the cap is clamped");
    m.dex = 0;  ck(!friendsPeerMon(m, v) && !v.valid, "dex 0 (an egg) is not a creature");
    m.dex = DEX_COUNT + 1; ck(!friendsPeerMon(m, v), "a dex past DEX_COUNT is not a creature");
    m.dex = -5; ck(!friendsPeerMon(m, v), "nor a negative one");
    m.dex = DEX_COUNT; ck(friendsPeerMon(m, v), "DEX_COUNT itself is");
    Pet egg; LinkMon c; friendsCardFrom(c, egg);
    ck(egg.isEgg() && c.dex == 0, "our own egg goes out as dex 0");
  }

  // --- mode: a FRIENDS board never pairs with a LAN BATTLE board --------------
  {
    freshBoards();
    X[0].L.send = toB; X[1].L.send = toA;
    use(0); X[0].L.begin(true, "ALI"); X[0].L.id = 1; X[0].F.sessionStart(X[0].L, X[0].P);
    use(1); X[1].L.begin(false, "BEA"); X[1].L.id = 2; X[1].L.addMon(battleMon(6));
    X[1].L.start(); X[0].L.start();
    for (int i = 0; i < 10; i++) { T += 100; X[0].L.tick(T); X[1].L.tick(T); }
    ck(X[0].L.state == LINK_REFUSED && X[1].L.state == LINK_REFUSED, "friends vs battle is refused on both sides");
    ck(X[0].L.refusedWhy == LINK_WHY_MODE && X[1].L.refusedWhy == LINK_WHY_MODE, "and says it is the mode");
    leave();
  }

  // --- a clean swap ----------------------------------------------------------
  freshBoards(); standardBags();
  pair();
  ck(X[0].L.ready() && X[1].L.ready() && X[0].F.havePeer && X[1].F.havePeer, "two boards pair in friends mode");
  ck(X[0].L.isHost != X[1].L.isHost, "with exactly one coordinator");
  ck(X[0].F.peerMon.valid && X[0].F.peerMon.dex == 25 && X[0].F.peerMon.shiny,
     "and each holds the other's validated creature");
  ck(!offer(0, ITEM_POKEBALL), "you cannot offer what is not in your bag");
  ck(!offer(1, ITEM_SHINY_CHARM), "nor a key item");
  ck(offer(0, ITEM_ORAN_BERRY), "ALI offers an ORAN BERRY");
  run(1500);
  ck(untouched(), "nothing moves while only one side has offered");
  ck(X[1].F.peerOffer() == ITEM_ORAN_BERRY, "but BEA sees what is offered");
  ck(offer(1, ITEM_POKEBALL), "BEA offers a POKE BALL");
  run(3000);
  ck(swapped(), "both bags are right afterwards");
  ck(X[0].F.gph == FG_DONE && X[1].F.gph == FG_DONE, "both sides end DONE");
  ck(X[0].F.rec.state == FR_NONE && X[1].F.rec.state == FR_NONE, "with nothing left in either journal");
  ck(X[0].ev[EV_GIFT_SENT] == 1 && X[0].ev[EV_GIFT_RECEIVED] == 1 &&
     X[1].ev[EV_GIFT_SENT] == 1 && X[1].ev[EV_GIFT_RECEIVED] == 1, "one SENT and one RECEIVED each");
  ck(X[1].lastArg[EV_GIFT_RECEIVED] == ITEM_ORAN_BERRY && X[0].lastArg[EV_GIFT_RECEIVED] == ITEM_POKEBALL,
     "naming the right items");
  run(5000);
  ck(swapped() && X[0].ev[EV_GIFT_SENT] == 1, "resends after the end change nothing");

  // --- once a day, across a reload --------------------------------------------
  leave();
  reloadFriends(0); reloadFriends(1);
  ck(X[0].F.giftDay == DAY && X[1].F.giftDay == DAY, "the day survives a reload");
  ck(swapped(), "and so do both bags");
  pair();
  ck(X[0].F.giftBlock(DAY) == FN_DAY_USED, "a second swap the same day is blocked");
  ck(!offer(0, ITEM_ORAN_BERRY) && !offer(1, ITEM_ORAN_BERRY), "on both boards");
  ck(!X[0].F.peerCanGift, "and each tells the other so");
  leave();
  DAY++;
  pair();
  ck(offer(0, ITEM_ORAN_BERRY) && offer(1, ITEM_ORAN_BERRY), "the next day it is open again");
  run(3000);
  ck(X[0].B.count(ITEM_ORAN_BERRY) == 1 && X[1].B.count(ITEM_ORAN_BERRY) == 1 &&
     X[0].F.gph == FG_DONE, "and an ORAN for an ORAN swaps cleanly");
  leave();

  // --- an empty bag gives a small gift -----------------------------------------
  freshBoards();
  fill(0, ITEM_SHINY_CHARM, 1);          // only a key item: nothing giftable
  fill(1, ITEM_LUM_BERRY, 1);
  pair();
  ck(!offer(0, ITEM_SHINY_CHARM), "a key item is never the offer");
  ck(offer(0, 0), "an empty-handed board offers the small gift");
  ck(X[0].F.gMineFree && (X[0].F.gmine == ITEM_POKEBALL || X[0].F.gmine == ITEM_ORAN_BERRY),
     "a POKE BALL or an ORAN BERRY");
  uint8_t small = X[0].F.gmine;
  offer(1, ITEM_LUM_BERRY);
  run(3000);
  ck(X[1].B.count(small) == 1 && X[1].B.count(ITEM_LUM_BERRY) == 0, "the friend gets it");
  ck(X[0].B.count(ITEM_LUM_BERRY) == 1 && X[0].B.count(ITEM_SHINY_CHARM) == 1, "and the giver loses nothing");
  leave();

  // --- invalid items off the wire ---------------------------------------------
  freshBoards(); standardBags();
  forge = forgeHostOffer; forgedItem = ITEM_SHINY_CHARM;
  pair();
  offer(0, ITEM_ORAN_BERRY); offer(1, ITEM_POKEBALL);
  run(3000);
  ck(X[1].F.gph == FG_NO && X[1].F.gNo == FN_BAD_ITEM, "a KEY item offered off the wire is refused");
  ck(untouched() && X[1].F.rec.state == FR_NONE, "and nothing moves");
  ck(X[1].B.count(ITEM_SHINY_CHARM) == 0, "the charm never reaches the bag");
  leave();
  freshBoards(); standardBags();
  forge = forgeGuestOffer; forgedItem = 0xEE;   // past ITEM_COUNT, past BAG_SLOTS
  pair();
  offer(0, ITEM_ORAN_BERRY); offer(1, ITEM_POKEBALL);
  run(3000);
  ck(X[0].F.gph == FG_NO && X[0].F.gNo == FN_BAD_ITEM, "an id past the table is refused by the host");
  ck(X[1].F.gph == FG_NO && X[1].F.rec.state == FR_NONE, "the guest is told, and its record cleared");
  ck(untouched(), "and the guest's POKE BALL came back out of escrow");
  ck(X[1].F.giftBlock(DAY) == FN_NONE, "a swap called off does not spend the day");
  leave();

  // a vote that disagrees about what it gets is not a vote for this swap
  freshBoards(); standardBags();
  forge = forgeGuestExpects;
  pair();
  offer(0, ITEM_ORAN_BERRY); offer(1, ITEM_POKEBALL);
  run(3000);
  ck(X[0].F.gph == FG_NO && X[0].F.gNo == FN_BAD_ITEM, "a vote expecting another item is refused by the host");
  ck(untouched() && X[1].F.rec.state == FR_NONE, "and the guest is made whole");
  leave();

  // --- lossy: drop / duplicate, never a duplicated or destroyed item -----------
  {
    const int drops[] = { 2, 3, 4, 5, 7 };
    const int dups[] = { 0, 3, 1 };
    int allOk = 0, n = 0, settledLater = 0;
    for (int d : drops) for (int u : dups) {
      freshBoards(); standardBags();
      dropEvery = d; dupEvery = u; seen = dropped = 0;
      pair(6000);
      bool paired = X[0].L.ready() && X[1].L.ready() && X[0].F.havePeer && X[1].F.havePeer;
      if (paired) { offer(0, ITEM_ORAN_BERRY); offer(1, ITEM_POKEBALL); run(8000); }
      bool cons = conserved();
      // whatever state it was left in, a clean meeting settles it
      dropEvery = dupEvery = 0;
      bool doubt = X[0].F.rec.state || X[1].F.rec.state;
      if (doubt) { leave(); pair(); run(1000); settledLater++; }
      bool end = (swapped() || untouched()) && conserved() &&
                 !X[0].F.rec.state && !X[1].F.rec.state;
      bool consistent = swapped() ? (X[0].ev[EV_GIFT_RECEIVED] == 1 && X[1].ev[EV_GIFT_RECEIVED] == 1)
                                  : (X[0].ev[EV_GIFT_RECEIVED] == 0 && X[1].ev[EV_GIFT_RECEIVED] == 0);
      if (paired && cons && end && consistent && swapped()) allOk++;
      if (!(paired && cons && end && consistent && swapped()))
        printf("      drop 1/%d dup 1/%d: paired=%d cons=%d end=%d events=%d swapped=%d gph=%d/%d\n", d, u, paired, cons, end, consistent, swapped(), X[0].F.gph, X[1].F.gph);
      n++;
      leave();
    }
    printf("      (%d of %d lossy runs swapped; %d needed a second meeting)\n", allOk, n, settledLater);
    ck(allOk == n, "every lossy run swaps exactly once, conserving every item");
  }

  // --- silence at each moment of the commit ------------------------------------
  // 1) the guest's vote never reaches the host: nobody committed
  freshBoards(); standardBags();
  pair();
  filter = dropGuestVote;
  offer(0, ITEM_ORAN_BERRY); offer(1, ITEM_POKEBALL);
  run(LINK_BATTLE_TIMEOUT_MS * 2 + 4000);
  ck(X[0].L.state == LINK_LOST && X[1].L.state == LINK_LOST, "a vote that never lands ends as LOST, not a hang");
  ck(X[1].F.rec.state == FR_PREPARED && X[0].F.rec.state == FR_NONE, "the guest is in doubt, the host never committed");
  ck(X[1].B.count(ITEM_POKEBALL) == 0 && conserved(), "its POKE BALL is in escrow, not gone");
  ck(X[0].B.count(ITEM_POKEBALL) == 0, "and the host did not get it");
  leave();
  reloadFriends(1);
  ck(X[1].F.rec.state == FR_PREPARED, "the doubt survives a reboot");
  filter = nullptr;
  // meeting SOMEBODY ELSE first changes nothing
  uint32_t realFid = X[0].F.fid;
  X[0].F.fid = 0xC0FFEE;
  pair(); run(1000);
  ck(X[1].F.rec.state == FR_PREPARED, "meeting another board does not settle it");
  ck(X[1].F.giftBlock(DAY) == FN_PENDING && !offer(1, ITEM_LUM_BERRY), "and blocks swapping with them");
  use(1); ck(X[1].F.startVisit(X[1].L, X[1].P, T), "but a visit is still fine");
  leave();
  X[0].F.fid = realFid;
  pair(); run(1000);
  ck(X[1].F.rec.state == FR_NONE && X[1].F.settled && !X[1].F.settledGot, "meeting the same friend again calls it off");
  ck(untouched() && conserved(), "and the POKE BALL is back where it was");
  ck(X[1].F.giftBlock(DAY) == FN_NONE, "and the day is not spent");
  ck(X[0].ev[EV_GIFT_SENT] == 0 && X[1].ev[EV_GIFT_SENT] == 0, "no gift event for a swap that never happened");
  leave();

  // 2) the host committed and the guest never heard: the guest finishes later
  freshBoards(); standardBags();
  pair();
  filter = dropHostCommit;
  offer(0, ITEM_ORAN_BERRY); offer(1, ITEM_POKEBALL);
  run(LINK_BATTLE_TIMEOUT_MS * 2 + 4000);
  ck(X[0].F.rec.state == FR_COMMITTED && X[1].F.rec.state == FR_PREPARED, "host committed, guest in doubt");
  ck(conserved(), "nothing duplicated or lost while they are apart");
  ck(X[0].B.count(ITEM_POKEBALL) == 1 && X[1].B.count(ITEM_ORAN_BERRY) == 0, "only the host's half is done");
  leave(); filter = nullptr;
  reloadFriends(0); reloadFriends(1);
  pair(); run(1000);
  ck(swapped() && conserved(), "the next meeting completes the guest's half");
  ck(X[0].F.rec.state == FR_NONE && X[1].F.rec.state == FR_NONE, "and both journals clear");
  ck(X[1].F.settled && X[1].F.settledGot, "the guest is told the old gift arrived");
  ck(X[1].ev[EV_GIFT_RECEIVED] == 1 && X[0].ev[EV_GIFT_RECEIVED] == 1, "each side emits its events exactly once");
  pair(); run(1000);
  ck(swapped(), "and a third meeting adds nothing");
  leave();

  // 2b) a LATE, older frame from the guest must not make the host forget early.
  //     Its offer-phase frame says "no record"; believed after the commit, the
  //     host would clear its journal while the guest is still unsure -- and the
  //     next meeting would then return the guest's item that the host already
  //     holds: a duplicate.
  freshBoards(); standardBags();
  forge = keepGuestOffer; staleN = 0;
  pair();
  filter = dropHostCommit;
  offer(0, ITEM_ORAN_BERRY); offer(1, ITEM_POKEBALL);
  run(2000);
  ck(staleN && X[0].F.rec.state == FR_COMMITTED && X[1].F.rec.state == FR_PREPARED, "host committed, guest unsure");
  X[0].L.onPacket(stale, staleN);      // the old frame turns up again
  run(300);
  ck(X[0].F.rec.state == FR_COMMITTED, "an older frame replayed late is ignored");
  forge = nullptr; filter = nullptr;
  run(1000);
  ck(swapped() && !X[0].F.rec.state && !X[1].F.rec.state, "and the swap still completes exactly once");
  leave();

  // 3) the guest finished and the host never heard: the host forgets later
  freshBoards(); standardBags();
  pair();
  filter = dropGuestDone;
  offer(0, ITEM_ORAN_BERRY); offer(1, ITEM_POKEBALL);
  run(LINK_BATTLE_TIMEOUT_MS * 2 + 4000);
  ck(swapped() && X[0].F.rec.state == FR_COMMITTED && X[1].F.rec.state == FR_NONE,
     "both halves done; only the host still holds a record");
  leave(); filter = nullptr;
  pair(); run(1000);
  ck(swapped() && X[0].F.rec.state == FR_NONE, "the next meeting clears it without moving anything");
  ck(X[1].ev[EV_GIFT_RECEIVED] == 1, "and the guest is not paid twice");
  leave();

  // 4) the host leaves (goodbye) after the vote and before committing
  freshBoards(); standardBags();
  pair();
  filter = dropGuestVote;
  offer(0, ITEM_ORAN_BERRY); offer(1, ITEM_POKEBALL);
  run(1500);
  filter = nullptr;
  X[0].L.sendBye();
  ck(X[1].L.state == LINK_LOST, "a goodbye mid-swap is reported at once");
  run(500);
  ck(X[1].F.rec.state == FR_PREPARED && conserved(), "the guest waits rather than guesses");
  leave(); pair(); run(1000);
  ck(untouched() && !X[1].F.rec.state, "and is made whole at the next meeting");
  leave();

  // --- visits -------------------------------------------------------------------
  freshBoards();
  pair();
  ck(X[0].F.canMeet() && X[1].F.canMeet(), "two hatched pets can meet");
  use(0); ck(X[0].F.startVisit(X[0].L, X[0].P, T), "ALI starts a visit");
  run(1000);
  ck(X[1].F.vnum == X[0].F.vnum && X[1].F.visiting(T), "BEA follows without tapping");
  ck(X[0].P.joy == 50 + FRIEND_VISIT_JOY && X[1].P.joy == 50 + FRIEND_VISIT_JOY, "both pets gain the joy");
  ck(X[0].P.bond == FRIEND_VISIT_BOND && X[1].P.bond == FRIEND_VISIT_BOND, "and the bond");
  ck(X[0].ev[EV_VISIT] == 1 && X[0].lastArg[EV_VISIT] == 25 &&
     X[1].ev[EV_VISIT] == 1 && X[1].lastArg[EV_VISIT] == 133, "EV_VISIT names the PEER's dex");
  run(FRIEND_VISIT_SHOW_MS + 500);
  ck(!X[0].F.visiting(T), "the meeting lasts a moment, then ends");
  use(1); ck(X[1].F.startVisit(X[1].L, X[1].P, T), "a second visit the same day is allowed");
  run(1000);
  ck(X[0].P.joy == 50 + FRIEND_VISIT_JOY && X[1].P.bond == FRIEND_VISIT_BOND && !X[1].F.visitBonus,
     "but pays nothing");
  ck(X[0].ev[EV_VISIT] == 1, "and emits nothing");
  leave();
  reloadFriends(0); reloadFriends(1);
  pair();
  use(0); X[0].F.startVisit(X[0].L, X[0].P, T); run(1000);
  ck(X[0].P.joy == 50 + FRIEND_VISIT_JOY && X[0].F.visitDay == DAY, "nor after a reload");
  leave();
  DAY++;
  // BEA spends its daily bond on play first: the cap must hold for a visit too
  for (int i = 0; i < 12; i++) X[1].P.play();
  uint8_t capped = X[1].P.bond;
  pair();
  use(0); X[0].F.startVisit(X[0].L, X[0].P, T); run(1000);
  ck(X[0].P.bond == 2 * FRIEND_VISIT_BOND, "the next day ALI's bond grows again");
  ck(X[1].F.visitBonus && X[1].F.visitBond == 0 && X[1].P.bond == capped,
     "but BEA's daily bond cap still holds against a visit");
  leave();

  // a peer whose card is garbage, or an egg, is never visited
  freshBoards();
  X[0].L.send = toB; X[1].L.send = toA;
  for (int b = 0; b < 2; b++) {
    use(b); X[b].L.begin(b == 0, "x"); X[b].L.id = b ? 0x2222 : 0x1111;
    X[b].F.sessionStart(X[b].L, X[b].P);
  }
  X[1].L.mine[0].dex = 9999;          // forged on the way out
  X[0].L.start(); X[1].L.start(); run(3000);
  ck(X[0].L.ready() && !X[0].F.peerMon.valid, "a peer card with a bad dex is not a creature");
  use(0); ck(!X[0].F.startVisit(X[0].L, X[0].P, T), "and cannot be visited");
  use(1); X[1].F.startVisit(X[1].L, X[1].P, T); run(1000);
  ck(X[0].P.joy == 50 && X[0].ev[EV_VISIT] == 0, "nor does a visit it starts pay anything");
  leave();
  freshBoards();
  X[1].P.speciesId = -1;   // BEA's is an egg
  pair();
  ck(!X[0].F.peerMon.valid && !X[0].F.canMeet() && !X[1].F.canMeet(), "an egg cannot visit or be visited");
  use(0); ck(!X[0].F.startVisit(X[0].L, X[0].P, T), "the visit is refused");
  leave();

  // --- a LAN battle still pairs and fights on the same Links ----------------------
  {
    seen = dropped = 0; dropEvery = 3; dupEvery = 5; filter = nullptr; forge = nullptr;
    X[0].L.send = toB; X[1].L.send = toA;
    X[0].L.begin(true, "A"); X[1].L.begin(false, "B");
    X[0].L.id = 0x1111; X[1].L.id = 0x2222;
    ck(X[0].L.mode == LINK_MODE_BATTLE, "begin() puts a used Link back in battle mode");
    X[0].L.addMon(battleMon(6)); X[0].L.addMon(battleMon(9)); X[1].L.addMon(battleMon(65));
    X[0].L.tick(T); X[1].L.tick(T); X[0].L.start();
    for (int i = 0; i < 40; i++) { T += 100; X[0].L.tick(T); X[1].L.tick(T); }
    ck(X[0].L.ready() && X[1].L.ready() && X[1].L.theirsN == 2, "a LAN battle pairs after a friends session");
    int turns = 0;
    for (int i = 0; i < 6; i++) {
      X[1].L.sendAct(1);
      for (int k = 0; k < 30 && !X[0].L.hasPeerAct(); k++) { T += 100; X[0].L.tick(T); X[1].L.tick(T); }
      if (!X[0].L.hasPeerAct()) break;
      LinkResult r{}; r.hostHp = r.guestHp = (uint16_t)(100 - i * 10);
      X[0].L.sendResult((const uint8_t*)&r, sizeof(r));
      for (int k = 0; k < 30 && !X[1].L.resultNew; k++) { T += 100; X[0].L.tick(T); X[1].L.tick(T); }
      if (X[1].L.resultNew) { X[1].L.resultNew = false; turns++; }
    }
    ck(turns == 6 && X[0].L.turn == X[1].L.turn, "and fights six turns over a lossy link");
    // and friend frames are ignored mid-fight
    uint8_t f[2 + FRIEND_FRAME_LEN] = { LM_FRIEND, FRIEND_FRAME_LEN };
    X[1].L.onPacket(f, sizeof(f));
    ck(!X[1].L.fxNew, "a friend frame in a battle is ignored");
  }

  printf(bad ? "\nFAILURES: %d\n" : "\nall friends checks passed\n", bad);
  return bad ? 1 : 0;
}
