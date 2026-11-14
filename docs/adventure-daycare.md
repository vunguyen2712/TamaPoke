# Daycare (Adventure feature 5): design notes

The player-facing numbers are in the README, § "Daycare". This file is the why.

## Files

| file | what |
|---|---|
| `breed.h` / `breed.cpp` | the rule and the daycare's state. Pure C++, links into every test binary |
| `ui_daycare.ino` | the screen: HOME (parents A/B, BREED, SWAP IN NOW, the queued egg, the cooldown) and a paged PICKER |
| `pet.cpp` | `newEgg()` takes a queued egg instead of rolling; `hatch()` passes inherited IVs to `rollIVs()` |
| `pet.h` | `currentDay()`, `eggSwapTo()`, private `setBredTarget()`, `rollIVs(const uint8_t *inherited)` |
| `save.cpp` | one key, `"dcare"` |
| `tools/emu/tests/breed_test.cpp`, `daycare_ui_test.cpp` | the logic, and the screen driven through the real sketch |

## Compatibility without egg groups

The data has no egg groups, so the rule is the smallest one that still reads
like Pokemon: **share a type, or bring a DITTO**. Two DITTOs never (the games
agree). Legendaries never, judged over the whole evolution LINE from its base:
PHIONE is legendary while its "evolution" MANAPHY is not marked so in this
table, and checking only the parent's own row let MANAPHY breed (a negative
check caught it -- see below).

The egg is the **base form** of the non-Ditto parent's line, walking `evolvesTo`
backwards and `EEVEE_EVOS` for Eevee's seven branches that the single field
cannot express. If both parents qualify the egg follows **parent A** -- the
player chooses by pick order, which needed no extra control.

The egg must be something `pickEggSpecies()` itself could hatch: rarity
`R_COMUN`/`R_RARO` (never `R_EVO`, never legendary), `speciesHasArt()`, and
`regionAvailable(regionOfDex(egg))`. One private predicate, `eggable()`, asks
all three, and both `breedCheck()` and the queue's re-check at `newEgg()` use
it. When it fails the pair is refused with a reason (EGG NOT AVAILABLE) rather
than hatching a creature that draws as a number. PIKACHU + RAICHU with only the
Kanto pack is the canonical case: their egg is PICHU, a Johto species.

## Inheritance

IVs are decided when the egg is MADE and stored on the queued egg, so the screen
can show them and nothing can be re-rolled by waiting.

- No charm: a random 3 of the 4 stats are inherited, each from a random parent;
  the 4th is stored as `BREED_IV_ROLL` (0xFF) and rolled by `rollIVs()` at hatch
  with the usual care bonus. Measured over 4000 breeds: 49.9% from parent A, the
  rolled slot spread ~25% each.
- OVAL CHARM (`bag.has`): all 4, each the better parent's.
- `rollIVs(inherited)` uses any entry 0..31 and rolls the rest, and the
  legendary and shiny floors below it run unchanged. That keeps ONE place that
  knows the floors (trap 1) -- a shiny bred egg is still floored at 20, which
  `breed_test` checks (it found its own wrong expectation the first time).

No egg move. A parent's move "if the child can learn it" means a move from the
child's learnset that it has not reached the level for, which is exactly the
level-gate bypass CLAUDE.md trap 1 records (SURF at level 1). Left out on
purpose rather than built with a second copy of the gate.

## Shiny

Both parents shiny: a separate 1-in-16 roll, made once at breed time and stored
as a flag on the queued egg. `newEgg()` ORs it into `eggShiny` at the very end,
far from the shiny-odds line (which the items feature edits for the Shiny
Charm), so the two features cannot conflict. Swap-in ORs it in the same way and
keeps the current egg's own roll -- swapping can never take a shiny away.

## Where the egg goes

There is exactly one live creature, so a bred egg is QUEUED, one at a time.

- `newEgg()` (every ending, and `begin()` on a first boot) calls
  `breedTakeQueued()`. If an egg waits and is still hatchable here, it becomes
  this egg and moves to the "live" record. A queued egg whose pack has left the
  card stays queued (the screen says why) and a random egg is rolled instead.
- `setBredTarget()` writes the bred species into EVERY region's `eggByRegion`
  slot. Without that, touching the region pill would have re-rolled a bred egg
  into a random creature of the same tier (`setRegion()` rolls a region it has
  no answer for). `eggRarity()` reads `eggTarget`, so the rarity label is the
  bred species' own.
- `hatch()` calls `breedTakeLive(speciesId)`: the inherited IVs if the egg
  cracking IS the bred one, and the live record is cleared either way. The
  console's `EGG <dex>` changes `eggTarget`, so it hatches normally.
- Both hooks run only for the `begin()`'d Pet (`opened`). The battle's scratch
  opponents hatch through `dbgHatchAs()` and must never spend a queued egg --
  the same lesson as the scratch Pet overwriting the save.
- Swap-in: offered only while the live creature is an egg with 0 cracks, no
  ceremony and no starter pick. Two taps, because it replaces the egg the
  player has (possibly a rare one) and cannot be undone.

`breed.cpp` loads lazily on its first hook call as well as in `breedBegin()`,
since `pet.begin()` (and a first boot's `newEgg()`) runs before `breedBegin()`
in `setup()`.

## Cooldown

`Pet::currentDay()` is the private `today()` that the care streak already uses,
so the daycare and the streak can never disagree about what "a day" is. One
breed per day number (`day != lastDay`, the same comparison as
`registerCare()`). With no clock the day is 0: the first breed is allowed and
the next waits for a real day.

## Persistence: `"dcare"`, 23 bytes, little-endian, append-only

```
[0] version 1
[1..2] queued dex   [3..6] queued IVs (atk def spe hp; 0xFF = roll)   [7] shiny bonus
[8..9] parent A dex [10..11] parent B dex
[12..13] live dex   [14..17] live IVs
[18..21] last breed day   [22] bred ever
```

A longer blob from a newer build keeps the prefix (the same `loadBlob` reasoning
as `pet.cpp`); every dex and IV is range-checked on load. A `breedBegin()` that
never ran never writes.

## Tests and the guards they were broken against

`breed_test` (logic, 80+ checks) and `daycare_ui_test` (the real sketch). Each
guard below was removed or inverted and the suite watched to fail:

| broken on purpose | caught by |
|---|---|
| two-Ditto guard | `two DITTOs: refused` |
| legendary guard | `MEWTWO + DITTO refused`, `SQUIRTLE + SUICUNE refused` |
| legendary checked on the parent's row only | `MANAPHY refused ...` |
| shared-type rule | `PIKACHU + SQUIRTLE ... refused`, the 40000-pair sweep |
| base-form walk | `the egg is the line's BASE form, PICHU` |
| region gate / art gate in `eggable()` | the Kanto-only checks / the sweep + NO_ART check |
| cooldown, one-at-a-time queue | `bred today: on cooldown`, `one queued egg at a time` |
| `saveState()` | `the queued egg survives a reload` |
| Oval Charm in `breedMake()` (the caller) | `breedMake with the charm ...` |
| uncracked check on swap | `a CRACKED egg is not` |
| `hatch()` passing inherited IVs | `hatch() applies the inherited IVs` |
| `opened` guard in `newEgg()` | `a scratch Pet's newEgg()/hatch() never spends the queue` |
| all regions answering the bred egg | `changing region does not re-roll a bred egg` |
| bred shiny into `eggShiny` | `the bonus makes the hatched PICHU shiny` |
| hatch consuming the live record | `the bred egg is consumed by its hatch` |
| UI: two-tap swap | `the first SWAP tap only arms it` |
| UI: picker paging / back-swipe to HOME | `page 2's pick bred ...`, `swiping back past page 1 ...` |

## Known gaps

- The picker is a new paged screen and is NOT in `swipe_test` (a shared file);
  its paging is covered in `daycare_ui_test` instead. Worth folding in at merge.
- `flush_test` (shared) opens the daycare's HOME view only; `daycare_ui_test`
  checks the picker flushes.
- With a frozen companion as the live creature (no ending is offered) a queued
  egg waits until that companion is released.
- Reviving a banked creature over a bred egg discards that egg; the revive
  dialog does not say so.
- Touch sizes are the hub's shared rows; untested under a real finger.
