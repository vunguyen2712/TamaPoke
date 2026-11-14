# Weekly gym challenge -- design notes

Player-facing numbers are in the README (§ "Weekly gym challenge"). This file
is the why, and what to keep intact when touching it.

## Files

| file | what |
|---|---|
| `challenge.h` / `challenge.cpp` | all the rules: the week, the world snapshot, the draw, the rule predicates, the adjusted team, the reward, NVS. Pure C++, links into every test |
| `ui_challenge.ino` | the CHALLENGE screen, the hub row, the picker hooks (`challengePickAllows/Ok/Cap/Header`), `challengePickFight`, `challengeBattleEnded` |
| `TamaPoke.ino` | `PICK_CHALLENGE` (0xFD) and five one-line hooks in the picker: `pickExists`, `squadCap`, `renderPick` (header + FIGHT colour), `pickTap` (BACK, FIGHT guard, dispatch) |
| `save.cpp` | the `chal` key |

## One week rule

`challengeWeekOf(day)` IS `tasksWeekOf(day)` = `(day + 3) / 7` (epoch day 4 was a
Monday). The day is `pet.currentDay()`, read in ONE place (`chRefresh()` in the
tab). The logic takes the day as a parameter so the tests drive it.

## The draw

`challengePick(week, world)` is pure. The world (`ChalWorld`) is what the player
could field: which (region, leader) pairs are playable AND reached, which types
the player's creatures have, and how many creatures there are. It is sampled
when a new week is first seen and the result is stored, like the weekly tasks,
so the rule cannot shift under the player mid-week when they hatch something.

- Leader: one of the candidate pairs, in a fixed order, picked by a hash of the
  week (not `random()`). Playable = `regionAvailable(r)` and every member of the
  real team `towerDrawable()` (art + pack). Reached = leader 0, or leader i-1
  beaten on either difficulty. Gym leaders only (index < `TRAINER_GYMS`).
- Rule: `(week + k) % CR_COUNT` for the first k whose rule is possible
  (`challengeRulePossible`). Rotation, not a hash, so with every rule possible
  two consecutive weeks never share a rule -- the Monday change is visible.
- TYPE ONLY's type is drawn from the types the player owned at draw time: the
  player can always comply with at least one creature.

If the stored leader stops being drawable (a pack removed), `challengeRollTo`
re-draws the same week, keeping `won`.

## One predicate for the picker AND the start (trap 1)

`challengeAllowsMon(c, dex)` answers "may this creature come"; the picker's
`pickExists()` asks it (through `challengePickAllows`), so the grid, the default
selection, the counts and the tap all agree. `challengeAllowsSquad(c, dex[], n)`
is that predicate over every member plus the count rule; the picker's FIGHT
(`challengePickOk`, over the ticked mask) and the fight's start (over the
squad `buildSquad` actually built) both ask it. A mask ticked behind the
picker's back is refused at start -- `challenge_ui_test` does exactly that.

## Copies only

`buildSquad(cap, max, mask)` caps levels on the copies. NO HELD ITEMS clears
`Combatant::held` on the copies after building, so nothing is consumed and
`heldBattleSync` has nothing to remove from the stored creature.
`btlTrainer = -1`: no badge (the badge guard asks `btlKind == BK_GYM` anyway),
no training, no win screen; the challenge screen reports the result.

## Paying once

`chal` blob (12 B): version, region, leader, rule, param, flags (bit0 won),
week (u32), weeks cleared (u16). `challengeWin()` sets `won`, SAVES, then bags
3 ULTRA BALL + `challengeRewardHeld(week)` and emits
`EV_CHALLENGE_WIN(week & 0xFFFF)`. A reset between the save and the bag loses
the reward rather than paying twice (the tower's rule). Once `won`, START is
refused for the rest of the week. A stored blob out of range is dropped whole
(the win count is kept when the length is right).

## Tests

- `challenge_test` -- logic: the week rule and its Monday edge, determinism,
  rule rotation, only playable regions (two packs) with a companion (all packs),
  only reached leaders with a companion, TRIO/TYPE plausibility, every rule's
  predicate, the adjusted teams (KOGA), the LEVEL CAP number, unlock, roll /
  reload / pay once / next Monday, re-draw on a missing pack, a corrupt blob.
- `challenge_ui_test` -- the real sketch: locked row, the screen and picker
  flush, the picker offers exactly the eligible, a wrong squad refused at FIGHT
  and at start, the fight's team == the adjusted team, RUN then retry then win,
  the reward and the event, no badge, nothing stored touched, DONE on the row
  and across a reload, SOLO / NO ITEMS (with a companion) / LEVEL CAP / TRIO
  fights, the swipe back, and a gym win leaving the challenge alone.
