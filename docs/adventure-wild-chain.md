# Wild alerts and shiny chains

Round 2 of the WILD feature. Player-facing numbers: README.md, "Wild alerts
and shiny chains". Round 1 (encounters, weather, catching): `adventure-wild.md`.

## Files

| File | What |
|---|---|
| `wild.h` / `wild.cpp` | `Wild::peek()`, the chain (`Wild::Chain`, `chainLen()`, `chainWin()`), `wildRollSpeciesChained()`, `wildShinyNumer()`, `wildAlertWhy()` / `wildIsRare()` / `wildConditional()`. |
| `ui_wild.ino` | `wildAlertNow()` (the one alert answer), the cached peek, the badge (`wildMainBadge()` / `wildMainBadgeTap()`), the hub row's `!`, the chain line, the shiny foe. Test hooks `wildBadgeRect()`, `wildMainOverlaps()`, `wildBadgeHitTest()`. |
| `save.cpp` | `wchain` (SK_BYTES). |
| `tools/emu/tests/chain_test.cpp` | Logic: chain rules, reload, boost and shiny odds measured, peek == roll. |
| `tools/emu/tests/wildalert_ui_test.cpp` | The badge through the real sketch: placement, guards, announced == shown. |
| `tools/emu/tests/chain_ui_test.cpp` | Chains through the real WILD screen, the chain line, the shiny foe. |

## One answer to "what is next"

`Wild::peek(day, epoch, region, petLevel, charm, out)` is a `const` function
that simulates `newDay()` (a new day is full; yesterday's MEET has left) and
computes the creature, level and shiny flag from the same seed as before.
`Wild::roll()` now calls `peek()` and copies the result into `st` -- there is
no second copy of the roll. On the UI side `wildRollNow()` and `wildPeekNow()`
pass the same arguments from the same place (`pet.currentDay()`,
`pet.lastSeenEpoch`, `pet.region`, `pet.level()`, the SHINY CHARM in the bag).
`chain_test` compares peek and roll over 400 mixed inputs; `wildalert_ui_test`
taps the badge 7 times over six days and checks the screen shows exactly the
peeked creature, level and shiny flag every time.

The peek walks the dex twice (809 species), so the UI caches it, keyed on
every input it reads (day, hour window, region, pet level, charm, the stored
day/used/phase/dex and the chain). Whether the species is registered is read
live, so catching or hatching it clears a NEW alert on the next frame.

## When the alert shows

`wildAlertNow()` returns `wildAlertWhy()` bits, 0 = no alert. It is the only
thing the badge, its tap and the hub row ask.

- RARE: `wildIsRare()` -- `R_RARO`, or `R_EVO` whose line's root is rare.
- SHINY.
- COND: `wildConditional()` -- some weather/window pair forbids the species
  (`wildTimeAllows()` false), so it is out only because of today's conditions.
  Today that is exactly the GHOST types (night or fog). It reads the rule, so
  a new gated type would join automatically.
- NEW: not in the Pokedex.

Hidden: an egg or a ceremony (the pet could not fight it), day 0 (no clock:
there is no "today" that ends), no encounter left, or the creature out is
already beaten (CATCH) -- that is not news.

## The badge's place

Centre (70, 274), drawn radius 15..17 (a 0.8 s pulse, two circles and a glyph),
hit area a 44 x 44 square (48..92, 252..296). That is the patch of scenery
left of the pet zone and above the bars panel. `wildMainOverlaps()` measures a
rect against the sketch's own constants and draw rects, and the test asserts
the badge's is 0 and that each neighbour IS caught when a rect is put on it:
menu band (y 28..94), `inPetZone()`, TASK DONE (103..363 x 96..140), `EVO_BTN_*`,
`FAR_BTN_*` (x 29..437, y 176..234), `buttons[]` with `BTN_HIT`, the panel from
y 312, the celebration and party banners (to y 246 / 250), the food picker,
the release dialog (x 94..) and the round edge. The first spot tried (y 272)
touched the party banner's bottom row and the test caught it.

`wildMainBadgeTap()` also requires `uiCurrentScreen() == SCR_MAIN` (state what
is allowed). It runs after the evolve/farewell/buttons checks in `onTap()`;
since nothing overlaps, the order never matters.

## Chains

`Wild::Chain` = { dex, len, best, day of the last link }, NVS `wchain`. Raw
bytes and append-only, like `Wild::State`; sanitised on load (an impossible dex
or a zero length loads as no chain).

- A WILD win (`battleEnded(won)`) calls `chainWin(dex, st.day)`: same species
  and alive -> +1 (stops at 99, and then emits nothing); otherwise the chain
  restarts at 1 on the beaten species. `EV_CHAIN(len)` on every growth (1 included).
- Alive while `today <= day + 1`: a whole calendar day with no wild win breaks
  it. Computed on read (`chainLen(day)`), so nothing needs writing at midnight.
- Losing, running and letting go do nothing to it. A chain with no clock (day
  0) never breaks by time.

### Why the boost is a share, not a multiplier

The first idea was "x4 weight". Measured, that does not make a chain
reachable: in Kanto at level 30 PIDGEY is ~2-4% of a roll, so x4 is ~10%, and
with 3 encounters a day the odds of seeing it at least once are ~27% -- the
chain would break most days. In REGION_ALL (809 species) it is hopeless.

So the chained species, when `wildEligible()` and `wildWeight() > 0` for this
roll, has its weight raised to the sum of every other eligible weight: half the
roll, whatever the pool size (a species that already outweighs the rest keeps
its own weight). Measured in `chain_test`: PIDGEY 4% -> 49% of 3000 Kanto rolls;
through `Wild::roll()` over 1500 days 2% -> 47%. P(none of a day's three
is the chained species) = 1/8 when it is eligible all day, so a player who
plays daily keeps the chain through about 7 days in 8; reaching the 1/16 cap
(15 links) takes roughly 10 days of play. The boost never adds a
species the conditions forbid (GASTLY by day stays 0; at night it is 50%).

It lives in `wildRollSpeciesChained()`; `wildWeight()` is unchanged, since
other features read it as the plain conditions-only weight.

### Shiny odds

`wildShinyNumer(len, charm)` = min(1 + len, 16), x2 with the charm, out of
`WILD_SHINY_ODDS` (256). It applies to the chained species only; everything
else keeps 1 (2). Measured (8000 rolls each): x1 2/256 -> 0.6%, x15 -> 6.3%,
x15 + charm -> 12.5%; non-chained species under a x15 chain 0.4%.
`EV_SHINY_FOUND(dex)` fires in `roll()` when a shiny is put out (never from a
peek).

## The shiny foe

`startBattle()` builds a plain foe. `wildTap()` (FIGHT) sets `btlFoe.shiny`
and calls `btlSyncSprite(1, btlFoe)` when the encounter is shiny -- one extra
SD sprite load, only for shiny encounters. No change to `TamaPoke.ino`.

## Not done

- The CATCH face has no room for the chain line (it is shown on MEET and NONE).
- The alert does not say WHY (the reasons are in the bits; the screen shows
  the creature, which mostly says it).
