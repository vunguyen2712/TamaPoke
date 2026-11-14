# Wild encounters, catching, time and weather

Design notes for the WILD row of the Adventure hub. The player-facing numbers
are in README.md, "Wild encounters and weather"; this file is the why.

## Files

| File | What |
|---|---|
| `wild.h` / `wild.cpp` | All the rules: weather, time windows, eligibility, weights, the deterministic roll, catch chance, and the persisted `Wild` state. Pure C++; links into the headless tests. |
| `ui_wild.ino` | The screen (three faces: MEET, CATCH, NONE) and `wildBattleEnded()`. |
| `pet.h` / `pet.cpp` | `Pet::registerCaught(dex, shiny)` (goes through `registerSpecies()`, then saves) and `Pet::currentDay()` (public view of the private `today()`). |
| `save.cpp` | `wild` (SK_BYTES) and `wgift` (SK_BOOL). |
| `tools/emu/tests/wild_test.cpp` | Logic against real Pet/Party/Bag and NVS. |
| `tools/emu/tests/wild_ui_test.cpp` | The real sketch: hub tap, FIGHT, the real battle, ball rows, RUN, reload. |

## One clock

There is no second clock. The day is `Pet::today()` (`lastSeenEpoch / 86400`)
and the hour is `(lastSeenEpoch / 3600) % 24`, the formula `isNightHour()`
uses; `wild_test` checks that every hour the pet calls night is also WILD
night. The logic takes day and epoch as arguments so the tests can set any day
and hour.

With no clock set (`lastSeenEpoch == 0`) it is day 0, CLEAR, midday. Day 0
never changes, so a board with no clock gets 3 encounters and no refill until
the clock is set. That is deliberate: inventing a day from `millis()` would be
a second clock.

## Deterministic days

An encounter is rolled from a hash (`mix32`) of the day, which encounter of the
day it is (0..2), the time window and the region. Nothing uses `random()` for
the content of the day, so leaving and coming back, or rebooting, shows the
same creature. Once rolled, the creature is **persisted** (`st.dex/level/shiny`)
so that a time window ticking over while you look at it cannot change it.

The catch roll, the IVs and the berry drop DO use `random()`. They are the luck
of the moment, not the content of the day.

Re-roll exploits that are closed:

- Leaving and re-opening does not re-roll: the creature out is persisted.
- Changing region does not re-roll the creature already out (only the next).
- Pulling the power during a losing fight: the phase is `WP_FIGHT` on record
  BEFORE the battle starts, and `Wild::begin()` turns a `WP_FIGHT` into a spent
  encounter.

## Day change

`newDay()` resets `used`. A creature merely MET yesterday leaves with the day
(`WP_MEET` -> `WP_NONE`). A creature already BEATEN (`WP_CATCH`) waits: taking
it away would rob the player of a win. Spending it does not cost one of the new
day's three, because `spend()` only counts it when `encDay == day`.

## Who can spawn: `wildEligible()`

The single answer every roll goes through, and the tests ask it too.

1. Inside the region's dex range. `REGION_ALL`, or a region whose pack is gone,
   means "anywhere playable" -- the same fallback the egg uses: the player's
   choice is kept, only the roll falls through.
2. `regionAvailable(regionOfDex(d))` and `speciesHasArt(d)` -- nothing appears
   that cannot be drawn.
3. Never `R_LEGENDARIO`.
4. `R_EVO` (evolution-only) only with a sound rule: it must have a predecessor,
   the predecessor must evolve by level, the wild level must be at least that
   level, and the line's root must be common or rare. `DexEntry` only stores
   `evolvesTo`, so a predecessor table is built once (1.6 KB) by inverting it,
   plus Eevee's eight from `EEVEE_EVOS`. The second branch of any other split
   evolution (Bellossom, Politoed, Slowking, Gallade, Froslass...) has no
   predecessor in the table and so never spawns wild. That is a gap, but an
   honest one: there is no level to gate it on.

## Weights

`wildWeight()` = base x weather rule x time rule.
Base: common 12, rare 3, evolved 4 (1 if the line is rare); everything x4 first
so a halving never rounds to 0. A rule multiplies once if the species has ANY
favoured type, halves once if it has any disfavoured type, and doubles for the
weather's biome. GHOST types return 0 unless it is NIGHT or FOG.

Measured in `wild_test` (REGION_ALL, level 40, DAY window, 3000 rolls):
WATER/ELECTRIC share 53% in rain vs 10% in sun; FIRE 28% in sun vs 2% in rain;
GHOST/DARK/POISON 31% at night vs 10% by day.

## Catching

Chance = rarity base (45 / 20 / 30 / 13) x ball power / 10, capped at 90.
`ItemInfo.power` already holds the multiplier x10 (10/15/20). Three throws;
the third miss flees. Room is checked BEFORE the ball is taken, so a full party
and box costs nothing. The caught creature is built like `foeFromSpecies()`
builds the battle's opponent: a scratch `Pet` (never `begin()`'d, so it cannot
write NVS) hatched as the species, aged to the level, `relearnFromLevel()`.

Note the battle opponent itself is `startBattle()`'s usual IV-20 build; the
random IVs belong to the creature you catch, not to the one you fought. A shiny
encounter is shown with a `*` before its name, and since round 2 the foe is
drawn shiny too (`wildTap()` sets `btlFoe.shiny` and re-syncs its sprite).

## Persistence

| Key | Kind | What |
|---|---|---|
| `wild` | bytes | `Wild::State`: day, used, phase, dex, level, shiny, throws, weather, time, encDay. Raw struct, so append-only; a shorter blob reads into the front. Sanitised on load. |
| `wgift` | bool | the 10 POKE BALLs were given |

`Wild` has the same `opened` guard as `Pet` and `Bag`: an instance nobody
`begin()`'d never writes.

## Events

`EV_WILD_SEEN` when a creature is rolled out, `EV_WILD_WIN` when it is beaten,
`EV_WILD_CATCH` when it is caught (arg = dex for all three).

## Round 2

The main-screen alert and shiny chains: `docs/adventure-wild-chain.md`.
`Wild::roll()` is now built on `Wild::peek()`.

## Not done

- No weather glyph on the main screen (optional; left out to keep
  `TamaPoke.ino` untouched).
- Encounter screen shows the gallery thumbnail, not the animated PMD sprite.
