#pragma once
#include <Arduino.h>
#include "link.h"

// Two-board extras: daily gift swap and pet visits (feature 7)
//
// GAME LOGIC ONLY: no gfx, no SD, no touch -- this file links into the
// headless test binaries next to pet.cpp. Design notes and the full commit
// protocol: docs/adventure-friends.md.
//
// It rides the SAME link the LAN battles use (link.h): HOST/JOIN, the hello
// with its version and build fingerprint, the MAC lock, resend-until-
// superseded and the deadlines. A friends session is a Link in
// LINK_MODE_FRIENDS whose "squad" is one creature -- the live pet, which is
// exactly what a visit needs to show -- and whose traffic after pairing is one
// kind of message, LM_FRIEND: this side's WHOLE state, resent until the next
// one supersedes it. Like a LinkResult it is absolute, never a delta, so a
// lost frame costs a delay and a duplicated one changes nothing.
//
// GIFT SWAP (once a day per board): a two-phase commit with the Link host as
// coordinator and BOTH boards keeping a one-slot journal in NVS. The guest
// votes yes by journalling PREPARED and setting its item aside (escrow); the
// host commits by journalling COMMITTED and swapping its own side; the guest
// swaps its side on seeing that. A side left in doubt by a lost packet or a
// vanished peer waits -- it never guesses -- and settles the next time the same
// two boards pair. That is what makes "a lost packet can never duplicate or
// destroy an item" true rather than likely.
//
// VISIT: the two pets meet; each side shows the other's creature, and both get
// FRIEND_VISIT_JOY joy and FRIEND_VISIT_BOND bond once a day, through
// Pet::friendVisit() so the daily bond cap still applies.

class Pet;
class Bag;

#define FRIEND_VISIT_JOY 15
#define FRIEND_VISIT_BOND 3
#define FRIEND_VISIT_SHOW_MS 5000   // how long the two pets stand together
#define FRIEND_NO_DAY 0xFFFFFFFFu   // "never": no day compares equal to it

// The state frame (LM_FRIEND body). Laid out byte by byte, not memcpy'd from a
// struct, so padding can never differ between two builds.
#define FF_FID 0       // 4: this board's friend id
#define FF_TO 4        // 4: the friend id this board thinks it is talking to (0 = not yet)
#define FF_SID 8       // 2: this session
#define FF_SEQ 10      // 2: bumps on every change; an older frame is ignored
#define FF_GPH 12      // gift phase (FG_*)
#define FF_GMINE 13    // the item I offer
#define FF_GTHEIRS 14  // the item I was offered, as I heard it
#define FF_XID 15      // 4: exchange id (chosen by the coordinator)
#define FF_VNUM 19     // visit counter
#define FF_RST 20      // my open journal record ABOUT YOU (FR_*), meaningful only if FF_TO is you
#define FF_RXID 21     // 4: its exchange id
#define FF_FLAGS 25    // bit0: I can swap today; bit1: my pet is hatched
#define FRIEND_FRAME_LEN 26

enum FriendGift : uint8_t {
  FG_IDLE = 0,     // nothing offered yet
  FG_OFFER,        // my item is on the table
  FG_PREPARED,     // guest: voted yes, item set aside, waiting for the host
  FG_COMMITTED,    // host: swapped my side, telling the guest
  FG_DONE,         // swapped, both sides
  FG_NO,           // called off (gNo says why); nothing moved
};

enum FriendNo : uint8_t {
  FN_NONE = 0,
  FN_DAY_USED,     // this board already swapped today
  FN_PENDING,      // an earlier swap is still in doubt (settles on meeting that friend)
  FN_BAD_ITEM,     // what the friend offered is not a giftable item
  FN_FULL,         // our bag holds BAG_MAX of what we would receive
  FN_PEER_NO,      // the friend called it off
  FN_NO_ITEM,      // the item we offered is no longer in the bag
  FN_SAME_ID,      // both boards have the same friend id (a restored save)
};

enum FriendRecState : uint8_t { FR_NONE = 0, FR_PREPARED, FR_COMMITTED };

// The journal: ONE open exchange at most, persisted in NVS ("frJrn").
struct FriendRecord {
  uint8_t state = FR_NONE;
  uint32_t peer = 0;     // the other board's friend id
  uint32_t xid = 0;      // which exchange
  uint16_t psid = 0;     // the peer's session id at the time: same session or a later one?
  uint8_t give = 0;      // my item (in escrow while PREPARED)
  uint8_t get = 0;       // theirs
  uint8_t giveFree = 0;  // 1: my side was the small gift, nothing left the bag
  uint32_t day = 0;      // the day it was journalled
};

// The peer's creature, validated. Nothing off the wire indexes a table until
// it has been through friendsPeerMon().
struct FriendPeerMon {
  bool valid = false;    // a hatched creature with a real dex
  int16_t dex = 0;
  uint8_t level = 1;
  bool shiny = false;
  char name[LINK_NAME_LEN] = "";
};

// One rule, one function: what may be offered / accepted as a gift. Any real,
// non-key item. Used for our own offer AND for every id off the wire.
bool friendsGiftable(uint8_t item);
// The giftable items the bag holds (count > 0), in id order. Returns how many.
uint8_t friendsGiftList(const Bag &b, uint8_t *out, uint8_t max);
// An empty-handed board gives a small gift instead: a POKE BALL or an ORAN
// BERRY, picked from `seed`. Made, not taken -- nothing leaves the bag.
uint8_t friendsSmallGift(uint32_t seed);
// Our live pet as the one-creature "squad" a friends session advertises.
void friendsCardFrom(LinkMon &out, const Pet &p);
// The peer's card off the wire -> something safe to draw. Clamped like
// linkMonTo: dex outside 1..DEX_COUNT is NOT a creature (an egg, or garbage),
// level is clamped to 1..MAX_LEVEL, the name is terminated.
bool friendsPeerMon(const LinkMon &m, FriendPeerMon &out);

class Friends {
public:
  // ---- persistent (NVS)
  uint32_t fid = 0;                  // "frId": this board's random friend id (0 = none yet)
  uint32_t giftDay = FRIEND_NO_DAY;  // "frGday": the day this board last swapped
  uint32_t visitDay = FRIEND_NO_DAY; // "frVday": the day this board last got a visit bonus
  FriendRecord rec;                  // "frJrn"

  void begin();          // load; the friend id is minted by the first session
  void save();

  // ---- the rules
  uint8_t giftBlock(uint32_t day) const;     // FN_NONE = may offer now
  bool visitBonusDue(uint32_t day) const { return visitDay != day; }

  // ---- a session, over a Link in friends mode
  // Call after l.begin(), before l.start(): sets the mode and the card.
  void sessionStart(Link &l, const Pet &p);
  void sessionEnd() { active = false; }
  // Once a frame, on the main loop. Reads the peer's latest frame, settles any
  // journal record, advances the gift and the visit, and says our new state.
  void poll(Link &l, Pet &p, Bag &b, uint32_t day, uint32_t now);

  // The player's actions. False (and nothing changes) if not allowed now.
  // `item` must be one of friendsGiftList(); 0 only when that list is empty,
  // which offers the small gift.
  bool offerGift(Link &l, const Bag &b, uint8_t item, uint32_t day);
  bool startVisit(Link &l, const Pet &p, uint32_t now);

  // ---- what the screen reads
  bool active = false;
  bool peerKnown = false;
  bool peerCanGift = false;   // the friend's flag: it may swap today
  FriendPeerMon peerMon;      // valid once the squad arrived
  uint8_t gph = FG_IDLE;
  uint8_t gNo = FN_NONE;
  uint8_t gmine = 0, gtheirs = 0;
  bool gMineFree = false;
  uint8_t gotItem = 0, gaveItem = 0;   // what the swap moved, for the screen
  uint8_t vnum = 0;
  uint32_t visitAt = 0;       // when the current visit began (millis)
  bool visitBonus = false;    // the current visit paid the bonus
  uint8_t visitBond = 0;      // bond it actually added (0 at the daily cap)
  bool settled = false;       // an earlier doubtful swap settled this session
  bool settledGot = false;    // ...as a completed swap (false: called off)

  bool visiting(uint32_t now) const {
    return vnum && visitAt && now - visitAt < FRIEND_VISIT_SHOW_MS;
  }

  // ---- session internals (public for the tests)
  uint16_t sid = 0, seq = 0;
  uint32_t peerFid = 0;
  uint16_t peerSid = 0, peerSeq = 0;
  uint8_t peer[FRIEND_FRAME_LEN];   // the last accepted frame
  bool havePeer = false;
  uint32_t xid = 0;
  uint8_t grantedVnum = 0;
  bool dirty = false;
  bool opened = false;

  bool myHatched = false;
  uint32_t lastDay = FRIEND_NO_DAY;

  // The friend's offer as something safe to name (0 = none / not giftable).
  uint8_t peerOffer() const;
  // Both pets hatched and the friend's card valid: a visit is possible.
  bool canMeet() const;

private:
  void encode(uint8_t *f) const;
  void flush(Link &l);
  bool accept(const uint8_t *f, uint8_t n);
  void reconcile(Bag &b);
  void visit(Pet &p, uint32_t day, uint32_t now);
  void prepare(Bag &b, uint32_t day);
  void commit(Bag &b, uint32_t day);
  void callOff(uint8_t why);
  void abortRec(Bag &b);
  void finishGuest(Bag &b);
};

extern Friends friends;

// Called once from setup(), after pet/party/bag have loaded.
void friendsBegin();
