# Items, held items, friendship evolution, Pokedex rewards (feature 8)

Player-facing numbers are in README.md. This file is for whoever changes the
code next: what was decided, and which single function owns each rule.

## Files

- `items.h` / `items.cpp` -- all logic, no graphics (links into every headless
  test): the bag, holders, `heldGive/heldTake/heldReturn/heldBattleSync`,
  `evoCheck()`, the dex rewards, `shinyCharmOdds()`.
- `ui_bag.ino` -- the BAG screen, plus the small hooks the sketch calls:
  `bagItemLine()` (battle narration), `bagHeldLine()`, `bagBattleSync()`,
  `bagLetGoEnded()`, `cardEvoLine()`.
- Battle effects live in `battle.cpp` on `Combatant` (`held`, `itemUsed`,
  `holder`) and are reported in `TurnLog::atkItem/defItem`.

## Storage

| what | where | notes |
|---|---|---|
| live pet's item | NVS `hitm` (U8) | `Pet::held` |
| banked creature's item | `PartyMon::held`, inside `party`/`box` | appended at the END of the record |
| rewards granted | `dxrw` (U8 bits: 1 SHINY, 2 OVAL), `dxgf` (gifts paid) | player-wide, never reset by `newEgg()` |

Item ids are stored raw: `ItemId` is append-only.

### The PartyMon migration

`PartyMon` grew from 30 to 32 bytes (`held` plus an explicit `spare`, so no
undefined padding byte is ever written to flash). `Party::begin()` reads both the
`party` and `box` blobs through ONE reader, `loadRecords()`, which:

1. takes an exact-size blob as is;
2. takes the first N records of a blob with MORE records at our own stride
   (a downgrade);
3. otherwise matches the length against the strides PartyMon has really had --
   30 (`PARTYMON_V2_SIZE`, moves[]) then 26 (`PARTYMON_V1_SIZE`, the original)
   -- and copies each old record into the front of a fresh one, then rewrites the
   blob once in the new layout.

It never infers a stride as `stored / slots`, which cannot tell a stride change
from a slot-count change (CLAUDE.md, "Box size"). The box had NO stride migration
before this; without it the held item would have emptied every player's box.
A stored `held` that is not a holdable item is cleared on load.
`static_assert`s pin that `held` sits at offset 30 and the record grew.

`box_test`, `upgrade_test` and `items_test` each load an old-stride blob for the
party AND the box; `moves_test` now writes its pre-moves fixture at the real
26-byte stride (it used `sizeof(PartyMon) - 4`, which stopped being the old size).

## The consumption rule

Berries (ORAN, SITRUS, LUM) and the FOCUS SASH are spent **the moment they
trigger**, in any battle; LEFTOVERS and the QUICK CLAW never are.

Implementation: `buildSquad()` stamps each of the player's combatants with
`holder` (`HOLDER_PET` or `HOLDER_PARTY(i)`); once per turn, `btlResolve()`
calls `bagBattleSync()` -> `heldBattleSync()`, which clears the item from that
stored creature (and saves) if the active combatant spent a consumable. Doing it
per turn rather than "when the battle ends" is deliberate: a fight ends through
won/lost/ran/leave/LAN paths, and a rule hung on one of them is CLAUDE.md trap 1.
Only the active combatant can trigger an item, so syncing `btlYou` is enough.
`itemUsed` stays on the combatant through switches, so a once-per-battle item
cannot fire twice.

Foes are built with `held = 0` and `holder = -1`; link (LAN) battles rebuild
both squads from the wire format, which carries no item, so link fights have no
items at all (the guest runs no battle logic, so they could not be shared
honestly anyway). Every `EV_ITEM_USED` is therefore the player's.

## Where an item goes when its creature does

| path | owner of the rule |
|---|---|
| farewell / release (banked) | `Pet::snapshotForParty()` copies `held` into `endedMon` |
| runaway / early retire (not banked) | `Pet::snapshotForParty()` -> `heldReturn()` |
| newcomer not kept (3 decline paths) | `bagLetGoEnded()` |
| newcomer replaces a banked one | `heldReturn(party.slots[i].held)` before `replaceAt()` |
| release from the party/box sheet | `monSheetConfirmTap()` -> `heldReturn()` |
| bring back / party<->box | the whole record moves, item included |

`snapshotForParty()` is the one point every live-pet ending passes through
before `newEgg()`, so `newEgg()` itself was not touched.

## Friendship evolution

`evoCheck(const Pet&)` is THE answer; `Pet::canEvolveNow()`, `Pet::evolve()` and
the card (`cardEvoLine()` -> `renderCardProgress()`) all ask it, and
`friend_test` sweeps 440 states asserting the card says READY exactly when
`canEvolveNow()` is true. Order of checks: final form, level (table level +
care mistakes + retire debt), bond (`FRIEND_BOND` 50), target drawable
(`regionAvailable` + `speciesHasArt`), care (all stats >= 40), state
(asleep/frozen/ceremony -- the card still shows READY here, as before).

The species are a table in `items.cpp` (`FRIEND_EVOS`). Each already had a
level proxy in `dex.h`; that level still applies -- the bond is ON TOP. Every
other species evolves exactly as before (no target filter was added for them).

EEVEE: with bond >= 50, ESPEON by day / UMBREON at night (18:00-05:59 by
`lastSeenEpoch`; deliberately wider than the 00-06 auto-sleep window, since a
sleeping creature cannot evolve) if that branch is drawable. Otherwise, and when
unbonded, `eeveeLevelOptions()` = `eeveeOptions()` minus ESPEON/UMBREON (unless
those are the only drawable ones). An unset clock counts as day.

Bond pace for reference: at most ~24/day (20 from care actions + 4 for the day),
so 50 is about two to three days of attentive care.

## Pokedex rewards

`dexRewardsBegin(&pet)` (one line in `setup()`, after `bag.begin()`) loads what
was granted, subscribes to `EV_DEX_REGISTER`, and catches up an existing save
at once. `dexRewardsCheck()` is idempotent.

**Obtainable** (`dexObtainable()`): has art, its region's pack is present, and
it either hatches (rarity > 0) or is the evolution (`evolvesTo` or an Eevee
branch) of something obtainable -- a fixpoint, since evolutions can point back
down the dex (HAPPINY 440 -> CHANSEY 113). A region with nothing obtainable is
never complete. On a Kanto-only card that is 138 of 151: PIKACHU/RAICHU,
the CLEFAIRY and JIGGLYPUFF lines, CHANSEY, SNORLAX, HITMONLEE, MR. MIME, JYNX,
ELECTABUZZ and MAGMAR come only from other regions' babies, so they are not
required. Wild catches (another feature) may make more species reachable; the
definition does not count that, which only makes completion easier, never
impossible.

The SHINY CHARM halves the egg's shiny roll (`shinyCharmOdds()`, floor 4); the
wild feature applies it to wild shinies itself. The OVAL CHARM is only granted
here -- its effect belongs to the daycare.

## Known gaps

- Opponents never hold items.
- The battle message queue holds 6 lines per turn; a very busy turn can drop an
  item line (the effect still happens).
- The QUICK CLAW narration goes before the turn's moves; there is no animation.
- Items are not shown on the party grid cells, only on the creature sheet and
  the creature card's battle page.
