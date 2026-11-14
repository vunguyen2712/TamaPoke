# Two-board gifts and visits (feature 7)

Player-facing rules and numbers: README, "Two-board gifts and visits".
Logic: `friends.cpp/.h` (no gfx, links into the tests). Screen: `ui_friends.ino`.
Tests: `friends_test` (protocol, cross-wired Links, lossy) and `friends_ui_test`
(the screen, against the real sketch).

**The radio has never run.** Everything below is proven with two `Link`s
cross-wired in one process, including drop/duplicate/silence faults aimed at
every step of the swap. First bring-up on two boards is debugging, exactly as
for LAN battles.

## Riding the LAN link, not beside it

A FRIENDS session IS a LAN session: the same global `lan` Link, the same
`linkNowBegin()`/`linkNowPoll()`/`lan.tick()` pump in `loop()`, the same hello
(protocol version, build fingerprint, the two-host tie-break by id), the same
MAC lock, resend-until-superseded and deadlines (20 s to pair, 12 s of silence
once paired). What changed in `link.h/.cpp`:

- `LINK_PROTO` 2 -> 3. The hello's flags byte gains bit1 = **friends mode**, and
  a mode mismatch is REFUSED (`LINK_WHY_MODE`) -- otherwise a board on FRIENDS
  could pair with one on LAN BATTLE and read gift frames as a fight.
- `Link::refusedWhy` says which refusal it was (proto / build / same id / mode,
  and RADIO which the UI sets when `linkNowBegin()` fails), so the screen can
  say "DIFFERENT VERSION" rather than a generic error.
- A refusal for proto, build or mode now **answers with our own hello once**,
  so the peer refuses for the same reason immediately instead of resending into
  silence until its pairing deadline and reporting a vanished friend. It cannot
  volley: a refused link ignores everything.
- `start()` only acts from LISTENING: it no longer revives a refused link or
  knocks an already-paired one back into a handshake.
- **Two livelocks under one-frame-in-two loss**, found by `friends_test`'s
  sweep (the old `lossy_test` only uses one in three) and fixed for battles too:
  - A READY side answers a late hello with its squad, but that answer always
    sits right after the hello, so a period-2 loss ate it every time and the
    other side never left SQUADS. A READY side now also owes `LINK_SQUAD_OWE`
    (8) squad resends on its OWN timer (`squadOwe`), stopped by any
    non-pairing traffic from the peer. A squad landing on a side that is
    already READY (or mid-turn) no longer resets it to READY -- that would
    have cancelled the action it was resending.
  - In friends mode both sides resend one frame on similar timers, which can
    settle into strict alternation that a period-2 loss turns into total
    silence from one side. The kept friends frame is now resent once and
    twice on alternate rounds (`Link::burst`, its own counter: `resendSeq`
    also moves when a late hello is answered and kept the parity fixed).
- `linkBuildTag()` now includes `ITEM_COUNT`: gift frames carry item ids.
- One new message, `LM_FRIEND`, parked in a one-slot mailbox (`fx`/`fxN`/
  `fxNew`) for `friends.cpp` to read on the main loop. In friends mode battle
  messages (ACT/RESULT/END/REMATCH) are ignored outright; in battle mode
  `LM_FRIEND` is.

The "squad" of a friends session is one creature: the live pet as a `LinkMon`
(`friendsCardFrom`), which is exactly the card a visit shows. An egg goes out
as dex 0. The peer's card is read through `friendsPeerMon()`, which follows
`linkMonTo()`'s discipline -- dex outside 1..DEX_COUNT is NOT a creature (not
clamped to 1, which would show an egg as a BULBASAUR), level clamped to
1..MAX_LEVEL, the name terminated and non-ASCII replaced. The screen draws a
dex only through the thumbnail when `speciesHasArt()` and the region's pack is
present, and as a dex number in a ring otherwise (the wild screen's no-art path).

## One frame, absolute state

After pairing each side sends ONE kind of message: its whole state
(`FRIEND_FRAME_LEN` = 26 bytes, laid out byte by byte in `friends.h`):

| field | meaning |
|---|---|
| `fid` | this board's random 32-bit friend id (NVS `frId`, minted by the first session) |
| `to` | the friend id this board believes it is talking to (0 = not yet) |
| `sid`, `seq` | this session's random id; a counter bumped on every change |
| `gph`, `gmine`, `gtheirs`, `xid` | gift phase, my item, the item I was offered, exchange id |
| `vnum` | visit counter |
| `rst`, `rxid` | my open journal record ABOUT YOU, and its exchange id |
| `flags` | bit0 I may swap today, bit1 my pet is hatched |

It is resent until superseded (Link rule 2) and carries absolute state (rule
3), so a dropped frame is a delay and a duplicated one changes nothing. A
frame older than the newest accepted one (`seq`), or from another friend id or
session, is ignored -- duplicates delivered late can never move the state
backwards. Nothing in a frame is acted on inside `onPacket`; `Friends::poll()`
reads the mailbox on the main loop.

## The gift swap: two-phase commit with a journal

The Link host is the **coordinator**, the guest the **participant**. Each board
has a one-slot journal in NVS (`frJrn`, 18 bytes: state, peer fid, xid, the
peer's session id, give, get, giveFree, day).

1. **OFFER.** Each player picks an item; it goes out in `gmine`. The host also
   names the exchange (`xid`, random 32 bits).
2. **VOTE (guest).** Seeing the host's offer, the guest checks it
   (`friendsGiftable()`: a real, non-key item; room for one more), then writes
   `PREPARED` to its journal **first**, then takes its own item out of the bag
   into escrow, and says `gph = PREPARED` echoing what it expects to get.
3. **COMMIT (host).** Seeing `PREPARED` for its own `xid`, echoing its own item,
   with a giftable item on offer, the host writes `COMMITTED` to its journal,
   takes its item, adds the guest's, and reports `rst = COMMITTED`.
   **This is the moment the swap happens.** Before it, the host can still call
   it off (`gph = NO`), and never commits afterwards.
4. **FINISH (guest).** Seeing the host's `COMMITTED` for its `xid`, the guest
   adds the host's item and clears its journal (its own item already left).
5. **FORGET (host).** Seeing the guest name it (`to`) with no open record for
   that `xid`, the host clears its journal.

Either side calls it off with `gph = NO` before committing; a guest that
already voted only releases its escrow on an explicit NO for its own `xid`.

### Why a lost packet can never duplicate or destroy an item

No finite exchange over a lossy link can guarantee both sides decide together
(the two generals). So this one never asks a side to guess: a side that cannot
know the outcome keeps a record and **waits**, and the record settles the
next time the same two boards pair. Every case:

| link broke after... | host | guest | at the next meeting |
|---|---|---|---|
| offers only | nothing | nothing | nothing to settle |
| the vote was sent, not heard | nothing | PREPARED, item in escrow | host has no record -> guest **aborts**: item back, day not spent |
| the commit was made, not heard | COMMITTED, its half done | PREPARED | guest sees COMMITTED -> **finishes**; host then forgets |
| the guest finished, not heard | COMMITTED | nothing | guest names host, holds nothing -> host **forgets** |

Why each settlement is safe:

- **Guest aborts only when the host provably never committed.** The host
  commits only inside the session the exchange belongs to (the `xid` is
  per-session and a frame from another session is ignored), so once that
  session is over and the host's journal has no `COMMITTED` for the `xid`, it
  never did and never can. Inside the same session the guest never aborts on
  silence, only on an explicit NO.
- **Host forgets only when the guest provably finished.** The guest votes
  before the host can commit and only ever clears a `PREPARED` record by
  finishing (or by an abort, which needs the host to hold no record -- but the
  host holds one).
- **"No record" is only believed from a board that knows who it is talking
  to.** `rst` is only filled for the board the record is about, and a receiver
  only trusts `rst` when the frame's `to` names it. Without that, a frame sent
  before the peer had heard from us would read as "I hold nothing" and make the
  guest abort a swap the host had committed -- a duplicated item.
- The session ids tell "the same conversation" (wait for the host) from "a
  later meeting" (settle now). A collision of two random 16-bit session ids only
  makes a side wait one more meeting, never decide wrongly.

While a record is open the board cannot swap with anyone (`FN_PENDING`); it can
still visit. Meeting a different board does not touch it.

The item leaves the guest's bag into escrow at the vote, not at the commit:
otherwise a guest left in doubt could spend the item before the next meeting
and the swap would then complete with nothing to give. The escrowed item is
counted by the test as still owned, and comes back on an abort.

### What it does not cover

- **A power cut in the milliseconds between two NVS writes** of one step (the
  journal, then the bag). Each step writes the journal first.
- **A friend who is never met again.** The record then stays open, and that
  board cannot swap with anyone else. Any time-out that settled it alone would
  re-open exactly the duplicate/destroy window this exists to close; this is
  the documented price.
- **Two boards with the same friend id** (one save restored onto a second
  board): swaps between them are refused (`FN_SAME_ID`).
- A delayed swap that settles after the bag has meanwhile filled to 99 of the
  item loses the extra, like any `Bag::add` at the cap.

### Once a day

`frGday` holds the day this board last swapped (`pet.currentDay()`, the
streak's day). It is set at the vote (guest) or the commit (host), and restored
to "never" if that board's swap is aborted on the same day it was journalled.
An offer is refused while `giftDay == today`; the frame's `flags` bit0 tells
the friend so the screen can say "FRIEND SWAPPED TODAY".

### The small gift

A board whose bag holds no giftable item (none, or only key items) offers a
POKE BALL or an ORAN BERRY, picked once at the offer so resends repeat it. It
is created, not taken: nothing leaves that bag, and an abort returns nothing.

## The visit

Either side taps VISIT; it bumps its `vnum`, and the other follows when it sees
a higher one. The pets have met when both frames carry the same `vnum`, and
that is when each board pays its bonus -- once a day (`frVday`):
`Pet::friendVisit(15, 3)`, which adds joy and calls `addBond()`, so the daily
bond cap (20) holds against visits exactly as against play. Both pets must be
hatched and the friend's card valid (`Friends::canMeet()`, the one rule the
screen and the logic share). `EV_VISIT` (arg = the friend's dex) is emitted
only with the bonus, so a task cannot be farmed by visiting repeatedly.
Nothing moves between boards, so a visit needs no commit: if the link breaks
mid-visit, at worst one side was paid and the other was not.

## Events

`EV_GIFT_SENT` (arg = item given) and `EV_GIFT_RECEIVED` (item got) on each
board when its half of the swap completes -- host at commit, guest at finish,
including a finish at a later meeting. Never for a swap that was called off.

## NVS

`frId` (U32), `frGday` (U32), `frVday` (U32), `frJrn` (18 bytes), under
"two-board gifts and visits" in `save.cpp`'s `SAVE_FIELDS`.
