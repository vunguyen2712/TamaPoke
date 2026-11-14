# Personalities (round 2, feature 6)

Player-facing numbers: README "Personalities" and the drain/actions notes in the
game manual. This file is the why.

## Shape

- `persona.cpp/.h` -- logic only. Two functions are the whole rule:
  `personaCare(trait, care, baseJoy)` (what a trait does to one act of care) and
  `personaDrain(trait, minute, poops, asleep)` (what it does to one minute).
  Every care path in `pet.cpp` asks the first through one private helper,
  `Pet::enjoy()`; `tick()` and `syncClock()` ask the second. No caller has its
  own copy of a trait's number.
- `ui_persona.ino` -- the card line, the sheet's typing line, the main-screen
  cheer (`featMainOverlay()` in `ui_hub.ino` calls it).
- Storage: the live pet under NVS `"prsn"` (written by `Pet::save()`, listed in
  `SAVE_FIELDS`); banked creatures in `PartyMon::spare`, which round 1 left as
  defined spare room -- no struct change, no migration. `Party::sanitize()` used
  to zero `spare` on every load; it now keeps a valid trait and zeroes junk.

## Decisions

- **Rolled in `hatch()`, only for the begin()'d Pet.** A scratch Pet (the battle
  foe) gets none and spends no `random()` call, so battle tests that seed the RNG
  see the same sequence they always did.
- **Old saves.** `Pet::load()` with no `"prsn"` key and a hatched creature rolls
  once and writes the key immediately, so a reboot before the next autosave
  cannot re-roll. A key holding 0 is a real "none" (a creature revived from a
  pre-feature bank record) and is never re-rolled. An egg gets nothing; it rolls
  when it hatches like any other.
- **The night is never harder.** `personaDrain(..., asleep=true)` returns zero
  for everything except SLEEPY's extra energy, and `syncClock()` uses the same
  call, so all three drain paths (CLAUDE.md "The three drain paths") keep their
  floors exactly. The awake nudges are 1/4 point a minute (one extra point every
  `PERSONA_NUDGE_EVERY` = 4 minutes), TIDY's poop dislike aside -- poop is the
  player's to clear and the bath is TIDY's favourite.
- **The nudge counts its own minutes** (`Pet::nudgeTick`), not `ageMinutes`,
  because a revived creature is frozen: `ageMinutes % 4` would be constant and
  the nudge would fire every minute or never.
- **The cheer** is module state in `persona.cpp` (`personaCheer()` /
  `personaCheering()`), set only by the opened Pet, drawn on the ground strip
  (y 276) under the creature: clear of the tasks notice and of the evolve /
  farewell prompt that sits over the middle of the screen.

## Known gaps

- A **wild catch** is created as a `PartyMon` directly (`wild.cpp`), so it has
  no trait. That is consistent with "rolled at hatch", but if catches should
  have one, `wild.cpp` can set `m.spare = personaRoll()` -- left to the wild
  feature's owner.
- **Pre-existing, found while testing this, NOT fixed here:** the SLEEP drain in
  `tick()` is keyed on `ageMinutes % 2` / `% 3`, and a revived (frozen)
  creature's `ageMinutes` never advances -- so asleep it either drains food/joy
  every minute (even age) or never (odd age). The floors still hold, so it is
  not dangerous, but it is wrong. Likewise `syncClock()` does `ageMinutes++`
  without checking `frozen`, so a revived companion appears to age while the
  board is off. Both belong to whoever owns revive/sleep next.
- The lights-off bonus (SLEEPY) and the petting bonus (CUDDLY) can be repeated
  like the base actions they ride on; joy is capped at 100 and bond by the
  daily cap, so there is nothing to farm beyond what petting already allows.
