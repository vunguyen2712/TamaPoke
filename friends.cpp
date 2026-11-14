#include "friends.h"
#include "pet.h"
#include "party.h"
#include "items.h"
#include "dex.h"
#include "game_events.h"
#include <Preferences.h>
#include <string.h>

// Two-board gifts and visits. The protocol is written up in full in
// docs/adventure-friends.md; the comments here say why each step is ordered
// the way it is.

Friends friends;

void friendsBegin() { friends.begin(); }

// ---------------------------------------------------------------- the rules

bool friendsGiftable(uint8_t item) {
  // itemValid() rules out ITEM_NONE and anything past ITEM_COUNT -- so an id
  // off the wire can never index ITEM_TBL out of bounds -- and key items are
  // never given away (they are never consumed, so "giving" one would copy it).
  return itemValid(item) && ITEM_TBL[item].kind != IK_KEY;
}

uint8_t friendsGiftList(const Bag &b, uint8_t *out, uint8_t max) {
  uint8_t n = 0;
  for (uint8_t i = 1; i < ITEM_COUNT && n < max; i++)
    if (friendsGiftable(i) && b.count(i) > 0) out[n++] = i;
  return n;
}

uint8_t friendsSmallGift(uint32_t seed) {
  return (seed & 1) ? ITEM_ORAN_BERRY : ITEM_POKEBALL;
}

void friendsCardFrom(LinkMon &out, const Pet &p) {
  memset(&out, 0, sizeof(out));
  // An egg goes out as dex 0, which the other side reads as "not a creature":
  // a visit needs two hatched pets.
  bool ok = !p.isEgg() && p.speciesId >= 1 && p.speciesId <= DEX_COUNT;
  out.dex = ok ? p.speciesId : 0;
  out.level = ok ? p.level() : 1;
  out.shiny = (ok && p.shiny) ? 1 : 0;
  const char *nm = p.nick[0] ? p.nick : (ok ? DEX_TBL[out.dex].name : "");
  // the wire field need not be terminated, but ours always is
  strncpy(out.name, nm, LINK_NAME_LEN - 1);
  out.name[LINK_NAME_LEN - 1] = 0;
}

bool friendsPeerMon(const LinkMon &m, FriendPeerMon &out) {
  out = FriendPeerMon();
  // Same discipline as linkMonTo(): everything here is untrusted. A dex out of
  // range is NOT clamped to 1 the way a battle does it -- that would show a
  // BULBASAUR for an egg -- it is simply not a creature.
  if (m.dex < 1 || m.dex > DEX_COUNT) return false;
  out.valid = true;
  out.dex = m.dex;
  out.level = m.level < 1 ? 1 : (m.level > MAX_LEVEL ? MAX_LEVEL : m.level);
  out.shiny = m.shiny != 0;
  // NOT a C-string read: the name off the wire need not be terminated.
  memcpy(out.name, m.name, LINK_NAME_LEN - 1);
  out.name[LINK_NAME_LEN - 1] = 0;
  for (uint8_t i = 0; out.name[i]; i++)   // ASCII only: the font has nothing else
    if ((uint8_t)out.name[i] < 32 || (uint8_t)out.name[i] > 126) out.name[i] = '?';
  return true;
}

static uint32_t random32() {
  uint32_t v = ((uint32_t)random(0x10000) << 16) ^ (uint32_t)random(0x10000);
  return v ? v : 1;
}

static void wr32(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}
static uint32_t rd32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static void wr16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

// ---------------------------------------------------------------- persistence

#define JRN_LEN 18   // state, peer4, xid4, psid2, give, get, free, day4

void Friends::begin() {
  Preferences prefs;
  prefs.begin("tamapoke", false);
  opened = true;
  fid = prefs.getUInt("frId", 0);
  giftDay = prefs.getUInt("frGday", FRIEND_NO_DAY);
  visitDay = prefs.getUInt("frVday", FRIEND_NO_DAY);
  rec = FriendRecord();
  uint8_t j[JRN_LEN];
  if (prefs.getBytesLength("frJrn") == JRN_LEN && prefs.getBytes("frJrn", j, JRN_LEN) == JRN_LEN) {
    rec.state = j[0];
    rec.peer = rd32(j + 1);
    rec.xid = rd32(j + 5);
    rec.psid = rd16(j + 9);
    rec.give = j[11];
    rec.get = j[12];
    rec.giveFree = j[13] ? 1 : 0;
    rec.day = rd32(j + 14);
    // A record that cannot be real is not acted on. The items were validated
    // before they were journalled, so this only trips on a damaged store.
    if (rec.state > FR_COMMITTED || !friendsGiftable(rec.give) || !friendsGiftable(rec.get))
      rec = FriendRecord();
  }
  prefs.end();
  // No id is minted here: boot must not consume the random stream (every
  // seeded test downstream would shift) nor write NVS for a feature nobody
  // has used. The first session mints it.
}

void Friends::save() {
  if (!opened) return;      // a Friends nobody begin()'d never writes
  Preferences prefs;
  prefs.begin("tamapoke", false);
  prefs.putUInt("frId", fid);
  prefs.putUInt("frGday", giftDay);
  prefs.putUInt("frVday", visitDay);
  uint8_t j[JRN_LEN];
  j[0] = rec.state;
  wr32(j + 1, rec.peer);
  wr32(j + 5, rec.xid);
  wr16(j + 9, rec.psid);
  j[11] = rec.give;
  j[12] = rec.get;
  j[13] = rec.giveFree;
  wr32(j + 14, rec.day);
  prefs.putBytes("frJrn", j, JRN_LEN);
  prefs.end();
}

uint8_t Friends::giftBlock(uint32_t day) const {
  if (rec.state != FR_NONE) return FN_PENDING;   // first: it is the one that needs explaining
  if (giftDay == day) return FN_DAY_USED;
  if (havePeer && peerFid == fid) return FN_SAME_ID;
  return FN_NONE;
}

// ---------------------------------------------------------------- the session

void Friends::sessionStart(Link &l, const Pet &p) {
  l.mode = LINK_MODE_FRIENDS;
  LinkMon m;
  friendsCardFrom(m, p);
  l.addMon(m);
  if (!fid) {                // first session ever: a random id, kept for life
    fid = random32();
    save();                   // (no-op for a Friends nobody begin()'d)
  }
  active = true;
  peerKnown = havePeer = false;
  peerCanGift = false;
  peerMon = FriendPeerMon();
  peerFid = 0;
  peerSid = peerSeq = 0;
  memset(peer, 0, sizeof(peer));
  gph = FG_IDLE;
  gNo = FN_NONE;
  gmine = gtheirs = 0;
  gMineFree = false;
  gotItem = gaveItem = 0;
  xid = 0;
  vnum = 0;
  visitAt = 0;
  visitBonus = false;
  visitBond = 0;
  grantedVnum = 0;
  settled = settledGot = false;
  sid = (uint16_t)(random(0xFFFF) + 1);
  seq = 0;
  myHatched = !p.isEgg();
  dirty = true;
}

void Friends::encode(uint8_t *f) const {
  memset(f, 0, FRIEND_FRAME_LEN);
  wr32(f + FF_FID, fid);
  wr32(f + FF_TO, havePeer ? peerFid : 0);
  wr16(f + FF_SID, sid);
  wr16(f + FF_SEQ, seq);
  f[FF_GPH] = gph;
  f[FF_GMINE] = gmine;
  f[FF_GTHEIRS] = gtheirs;
  wr32(f + FF_XID, xid);
  f[FF_VNUM] = vnum;
  // Our open record is only reported to the board it is ABOUT. To anyone else
  // (or before we know who this is) we say nothing -- and the peer only trusts
  // this field when FF_TO names it, so "nothing" can never be misread as
  // "I hold no record of that swap".
  if (rec.state != FR_NONE && havePeer && rec.peer == peerFid) {
    f[FF_RST] = rec.state;
    wr32(f + FF_RXID, rec.xid);
  }
  uint8_t fl = 0;
  if (gph != FG_IDLE || giftBlock(lastDay) == FN_NONE) fl |= 1;
  if (myHatched) fl |= 2;
  f[FF_FLAGS] = fl;
}

// The peer's latest frame, if it is newer than the one we hold. Frames carry
// absolute state, so all that matters is never going BACKWARDS: an older frame
// (a late duplicate) is dropped by its sequence number.
bool Friends::accept(const uint8_t *f, uint8_t n) {
  if (n < FRIEND_FRAME_LEN) return false;            // runt
  uint32_t from = rd32(f + FF_FID);
  if (!from) return false;
  uint16_t s = rd16(f + FF_SID), q = rd16(f + FF_SEQ);
  if (havePeer) {
    if (from != peerFid || s != peerSid) return false;   // not this conversation
    if ((int16_t)(q - peerSeq) <= 0) return false;       // a repeat, or older
  }
  bool first = !havePeer;
  memcpy(peer, f, FRIEND_FRAME_LEN);
  peerFid = from;
  peerSid = s;
  peerSeq = q;
  havePeer = peerKnown = true;
  peerCanGift = (f[FF_FLAGS] & 1) != 0;
  if (first) dirty = true;   // our FF_TO must name them from now on
  return true;
}

void Friends::flush(Link &l) {
  if (!dirty || l.state != LINK_READY) return;
  dirty = false;
  seq++;
  uint8_t f[FRIEND_FRAME_LEN];
  encode(f);
  // Everything above is already updated: sendFriend() can deliver at once and
  // a reply can re-enter before it returns (CLAUDE.md, "state before sending").
  l.sendFriend(f, FRIEND_FRAME_LEN);
}

void Friends::poll(Link &l, Pet &p, Bag &b, uint32_t day, uint32_t now) {
  if (!active) return;
  myHatched = !p.isEgg();
  lastDay = day;
  if (l.state != LINK_READY) return;
  if (l.theirsN >= 1) friendsPeerMon(l.theirs[0], peerMon);
  else peerMon = FriendPeerMon();
  if (l.fxNew) {
    l.fxNew = false;
    accept(l.fx, l.fxN);
  }
  reconcile(b);
  if (gph == FG_OFFER && havePeer) {
    if (l.isHost) commit(b, day);   // the coordinator decides
    else prepare(b, day);           // the guest votes
  }
  visit(p, day, now);
  flush(l);
}

// ---------------------------------------------------------------- gift swap

bool Friends::offerGift(Link &l, const Bag &b, uint8_t item, uint32_t day) {
  if (!active || l.state != LINK_READY || !havePeer || gph != FG_IDLE) return false;
  lastDay = day;
  if (giftBlock(day) != FN_NONE) return false;
  uint8_t list[ITEM_COUNT];
  uint8_t n = friendsGiftList(b, list, ITEM_COUNT);
  bool free = false;
  if (n) {
    bool found = false;
    for (uint8_t i = 0; i < n; i++) if (list[i] == item) found = true;
    if (!found) return false;
  } else {
    if (item != 0) return false;
    item = friendsSmallGift(random32());   // chosen ONCE: resends repeat it
    free = true;
  }
  gmine = item;
  gMineFree = free;
  gNo = FN_NONE;
  if (l.isHost) xid = random32();   // the coordinator names the exchange
  gph = FG_OFFER;
  dirty = true;
  flush(l);
  return true;
}

uint8_t Friends::peerOffer() const {
  if (!havePeer) return 0;
  uint8_t g = peer[FF_GPH];
  if (g == FG_IDLE || g == FG_NO) return 0;
  return friendsGiftable(peer[FF_GMINE]) ? peer[FF_GMINE] : 0;
}

void Friends::callOff(uint8_t why) {
  gph = FG_NO;
  gNo = why;
  dirty = true;
}

// GUEST, phase 1: vote yes. The record goes to NVS FIRST, then the item is set
// aside; from here on this board never decides alone -- only the host's
// COMMITTED (now or at a later meeting) or its NO / absence-of-record (at a
// later meeting) settles it.
void Friends::prepare(Bag &b, uint32_t day) {
  uint8_t pg = peer[FF_GPH];
  if (pg == FG_NO) { callOff(FN_PEER_NO); return; }
  if (pg != FG_OFFER) return;                  // the host has not offered yet
  uint32_t px = rd32(peer + FF_XID);
  if (!px) return;
  uint8_t theirs = peer[FF_GMINE];
  gtheirs = theirs;
  if (!friendsGiftable(theirs)) { callOff(FN_BAD_ITEM); return; }
  if (b.count(theirs) >= BAG_MAX) { callOff(FN_FULL); return; }
  if (!gMineFree && !b.has(gmine)) { callOff(FN_NO_ITEM); return; }
  xid = px;
  rec.state = FR_PREPARED;
  rec.peer = peerFid;
  rec.xid = px;
  rec.psid = peerSid;
  rec.give = gmine;
  rec.get = theirs;
  rec.giveFree = gMineFree ? 1 : 0;
  rec.day = day;
  giftDay = day;
  save();
  if (!gMineFree && !b.take(gmine)) {          // cannot happen on this screen; undo
    rec = FriendRecord();
    giftDay = FRIEND_NO_DAY;
    save();
    callOff(FN_NO_ITEM);
    return;
  }
  gph = FG_PREPARED;
  dirty = true;
}

// HOST, phase 2: the guest has voted yes for THIS exchange and agrees what it
// gets. Journal COMMITTED, then swap our side. From here the swap has
// happened; the record only exists to tell the guest.
void Friends::commit(Bag &b, uint32_t day) {
  uint8_t pg = peer[FF_GPH];
  if (pg == FG_NO) { callOff(FN_PEER_NO); return; }
  if (pg != FG_PREPARED) return;
  if (rd32(peer + FF_XID) != xid) return;      // not this exchange
  uint8_t theirs = peer[FF_GMINE];
  gtheirs = theirs;
  if (peer[FF_GTHEIRS] != gmine) { callOff(FN_BAD_ITEM); return; }   // they heard something else
  if (!friendsGiftable(theirs)) { callOff(FN_BAD_ITEM); return; }
  if (b.count(theirs) >= BAG_MAX) { callOff(FN_FULL); return; }
  if (!gMineFree && !b.has(gmine)) { callOff(FN_NO_ITEM); return; }
  rec.state = FR_COMMITTED;
  rec.peer = peerFid;
  rec.xid = xid;
  rec.psid = peerSid;
  rec.give = gmine;
  rec.get = theirs;
  rec.giveFree = gMineFree ? 1 : 0;
  rec.day = day;
  giftDay = day;
  save();
  if (!gMineFree) b.take(gmine);
  b.add(theirs);
  gaveItem = gmine;
  gotItem = theirs;
  gph = FG_COMMITTED;
  dirty = true;
  if (opened) {
    gameEvent(EV_GIFT_SENT, gmine);
    gameEvent(EV_GIFT_RECEIVED, theirs);
  }
}

// GUEST, phase 3: the host committed. Take what we were given; what we gave
// is already out of the bag.
void Friends::finishGuest(Bag &b) {
  FriendRecord r = rec;
  rec = FriendRecord();
  save();
  b.add(r.get);
  gaveItem = r.give;
  gotItem = r.get;
  if (r.xid == xid && r.psid == peerSid) gph = FG_DONE;   // this session's swap
  dirty = true;
  if (opened) {
    gameEvent(EV_GIFT_SENT, r.give);
    gameEvent(EV_GIFT_RECEIVED, r.get);
  }
}

// The guest's swap is called off: the item comes back out of escrow, and the
// day's swap was not spent.
void Friends::abortRec(Bag &b) {
  FriendRecord r = rec;
  rec = FriendRecord();
  if (r.day == giftDay) giftDay = FRIEND_NO_DAY;
  save();
  if (!r.giveFree) b.add(r.give);
  dirty = true;
}

// Settles the journal against what the SAME friend says now. Runs every poll;
// every branch is idempotent. Nothing happens until the friend's frame names
// us (FF_TO), because before that its FF_RST cannot be about us.
void Friends::reconcile(Bag &b) {
  if (rec.state == FR_NONE || !havePeer || rec.peer != peerFid) return;
  if (rd32(peer + FF_TO) != fid) return;
  uint8_t prst = peer[FF_RST];
  uint32_t prx = rd32(peer + FF_RXID);
  bool sameSession = (rec.psid == peerSid);

  if (rec.state == FR_PREPARED) {
    if (prst == FR_COMMITTED && prx == rec.xid) {          // the host did it
      bool late = !sameSession;
      finishGuest(b);
      if (late) { settled = true; settledGot = true; }
      return;
    }
    if (sameSession) {
      // Still the conversation it was voted in: the host may yet commit. Only
      // an explicit NO for this exchange releases the item now.
      if (peer[FF_GPH] == FG_NO && rd32(peer + FF_XID) == rec.xid) {
        abortRec(b);
        callOff(FN_PEER_NO);
      }
      return;
    }
    // A LATER meeting, and the host holds no COMMITTED record of it. It only
    // ever commits inside the session the exchange belonged to, and that
    // session is over -- so it never committed and now never can.
    abortRec(b);
    settled = true;
    settledGot = false;
    return;
  }

  if (rec.state == FR_COMMITTED) {
    if (prst == FR_PREPARED && prx == rec.xid) return;    // they finish on seeing ours
    // The guest names us and holds nothing open for this exchange. It votes
    // (PREPARED) before we can commit and only clears that by finishing, so
    // it has finished: our record has done its job.
    bool late = !sameSession;
    rec = FriendRecord();
    save();
    dirty = true;
    if (!late && gph == FG_COMMITTED) gph = FG_DONE;
    if (late) { settled = true; settledGot = true; }
  }
}

// ---------------------------------------------------------------- visit

bool Friends::canMeet() const {
  return myHatched && havePeer && peerMon.valid && (peer[FF_FLAGS] & 2);
}

bool Friends::startVisit(Link &l, const Pet &p, uint32_t now) {
  myHatched = !p.isEgg();
  if (!active || l.state != LINK_READY || !canMeet() || visiting(now)) return false;
  uint8_t pv = peer[FF_VNUM];
  uint8_t base = pv > vnum ? pv : vnum;
  if (base == 255) return false;
  vnum = (uint8_t)(base + 1);
  visitAt = now ? now : 1;
  dirty = true;
  flush(l);
  return true;
}

// Either side may start a visit; the other follows. The pets have met when
// both frames carry the same visit number, and THAT is when the bonus is paid
// -- once a day per board, whoever started it.
void Friends::visit(Pet &p, uint32_t day, uint32_t now) {
  if (!havePeer) return;
  uint8_t pv = peer[FF_VNUM];
  if (!canMeet()) return;
  if (pv > vnum) {
    vnum = pv;
    visitAt = now ? now : 1;
    dirty = true;
  }
  if (vnum && pv == vnum && grantedVnum != vnum) {
    grantedVnum = vnum;
    visitBonus = false;
    visitBond = 0;
    if (visitBonusDue(day)) {
      visitDay = day;
      save();
      visitBond = p.friendVisit(FRIEND_VISIT_JOY, FRIEND_VISIT_BOND);
      visitBonus = true;
      if (opened) gameEvent(EV_VISIT, peerMon.dex);
    }
  }
}
