# Daily and weekly tasks -- design notes

Player-facing numbers live in the README, section "Daily and weekly tasks".
This file is the why.

## Shape

- `tasks.h` / `tasks.cpp`: pure logic, linked into every headless test next to
  `pet.cpp`. No gfx, SD or touch.
- `ui_tasks.ino`: the screen, reached from the Adventure hub.
- Everything is fed by the `game_events.h` bus. `tasksBegin(pet)` subscribes
  `tasksOnEvent`; the tasks never call into the features they count.

`tasksBegin` takes the `Pet` by reference because the global `pet` lives in
the sketch, and `tasks.cpp` links into ~35 test binaries that do not have it.
A reference to `pet` from `tasks.cpp` would break every one of them at link time.

## The day

`Pet::today()` -- the RTC day the streak counts in -- was private and is now
public, so the tasks roll over on exactly the streak's boundary rather than a
second copy of `lastSeenEpoch / 86400`. Weeks are `(day + 3) / 7`, which starts
them on a Monday (epoch day 0 was a Thursday).

The day is PASSED IN to the logic (`tasksRollTo(day)`, `tasksTickAt(day, ok)`),
so tests choose it. The sketch passes `pet.today()` via `tasksRefresh()` /
`tasksTick()`.

At boot `tasksBegin()` only loads; it does not roll. `setup()` runs it before
`syncClock()`, so the pet's day is still the last-seen day at that point --
rolling then would roll to yesterday. The first event, minute tick or screen
open rolls to the real day.

A changed day re-rolls in EITHER direction. Rolling only forward would leave a
board whose RTC was set far ahead and then corrected stuck with no new tasks
until that future day; and moving the clock forward is already an unbounded
farm, exactly as it is for the streak, so refusing the backward step would buy
nothing.

## Choosing tasks

`tasksPick(seed, weekly, world, out)` filters the table by `taskPossible()`,
then does a seeded partial Fisher-Yates. It never calls `random()`: the answer
must be the same every time it is asked, and must not disturb the game's RNG
(battles and eggs draw from it).

The chosen ROW INDICES are stored when the day rolls, not recomputed. "Possible"
depends on the player's state, which changes during a day -- beat the last gym
at noon and a recomputed selection would swap the GYM WIN task out from under
its progress. Storing five bytes makes "the same day always shows the same
tasks" true by construction. Hence the tables are APPEND-ONLY, like `MOVES`.

One row per kind per table keeps the three dailies distinct. At least five
daily and five weekly rows are always possible, so the pick never runs short.

### `taskPossible()` is THE answer

One function decides, from a `TaskWorld` snapshot built by `taskWorldOf(pet)`:

| world field | rule |
|---|---|
| `eggToday` | the live pet is an egg (it hatches within minutes), or a farewell is open (which hands over a new egg) |
| `evolveToday` / `evolveWeek` | next evolution within 12 levels / within `12 x days left in the week`. Uses `evolveLevel + careMistakes + evoPenalty()`, the same threshold `canEvolveNow()` uses. A frozen companion never evolves |
| `dexNewWeek` | that evolution's target is not registered yet (Eevee: only VAPOREON is checked, which errs toward not offering) |
| `gymsLeft` | some ladder whose region pack is available still has an unearned EASY badge. Hard badges are not counted: a hard-only leader in one day is not a sane daily |
| `towerOpen` | `towerUnlocked()`, declared WEAK in `tasks.cpp`: any region's champion (badge `TRAINER_COUNT-1`) beaten. The tower feature's strong definition replaces it at link time. Rematches use the same gate -- conservative: if rematches open earlier the task is merely offered later, never offered impossibly |
| `hasBall` | a ball in the bag (a catch needs one) |
| `hasHeld` | a held item in the bag (USE ITEM) |
| `canBreed` | two creatures in the party. A guess at the daycare's rule; the daycare feature should replace it with its own predicate at merge |

The care tasks (feed, bath, play, train, care days, keep it well) and wild
SEE/BEAT are always possible: an egg hatches within minutes, and the wild
screen has no unlock of its own as far as this feature knows.

## Counting

`tasksOnEvent` refreshes the day first (an event at 00:00:30 counts for the new
day), then advances every unfinished task whose kind matches. Several tasks can
count one event -- FEED and FAV BERRY both move on a favourite berry, and a
daily and a weekly of the same kind both move. Progress stops at the target.

Filters: FAV BERRY needs `arg == 1`; PLAY needs a score above 0, so opening the
ball game and swiping straight out is not a play. `EV_TRAIN` is only emitted
for a session with at least one hit.

### The two emitters (in `pet.cpp`, not the sketch)

`EV_PLAY` is emitted at the end of `Pet::playResult()` and `EV_TRAIN` at the
end of `Pet::trainStrength()` (arg 0) and `Pet::trainSpeed()` (arg 2), each
behind `opened` like the skeleton's other emitters. The sketch has SEVEN call
sites for those three functions -- the clock running out, three misses, and the
early exits `leaveGame/leaveSack/leaveSpeed` -- and the early exits are exactly
the twins that bank sessions the timed path would otherwise miss (CLAUDE.md
"Minigames", trap 1). One emitter per function covers every path, including
any added later. Passive DEF training (`defTick`) is not a session and emits
nothing.

## The state task: KEEP IT WELL

Every stat >= 50 (`TASK_WELL_MIN`) for 60 minutes IN A ROW. `loop()` calls
`tasksTick()` once a minute; each tick with the condition adds a minute, a tick
without it starts over. The running count lives in RAM only: persisting it every
minute would cost an NVS write -- a ~1 s flash stall -- per minute. So a reboot
also starts the hour over, and time with the board off never counts.

Why 60 and 50: awake, FOOD drains 2/min, so holding 50 needs a berry roughly
every quarter hour. The six hours originally sketched would be a chore, not a
task. Asleep the drain is slow and the stats can hold, which is fine -- a
creature put to bed well fed is a creature kept well.

## Claiming

The claimed bit is SAVED BEFORE `bag.add()`. A power cut between the two loses
a reward rather than paying it twice. A full bag (99) still marks the task
claimed. The bonus needs all three dailies CLAIMED, not just done, so it is the
last thing you tap.

## Storage

One key, `"tasks"`, SK_BYTES, 20 bytes: version, claimed bits (5 slots + bonus),
five row indices, five progress bytes, day (u32 LE), week (u32 LE). Listed under
the tasks comment in `save.cpp`'s `SAVE_FIELDS`. A blob of the wrong length or
version, or with a row index past its table, is dropped whole and the next
refresh rolls fresh tasks. Progress is clamped to the row's target on load.
NVS is written on a roll, on an event that moved a task, on completing KEEP IT
WELL, and on a claim -- never on a plain minute tick.

## Tests

- `tools/emu/tests/tasks_test.cpp` (logic): determinism, distinct dailies,
  reset per day, week boundary on Monday and not a day earlier, exact event
  matching (FAV BERRY's arg filter, PLAY's score filter, the real `Pet`
  emitters), claim once / items once / across a reload, bonus once, impossible
  tasks never offered over 2000 days for a fresh save and for a save with every
  badge -- plus the companion check that with everything possible every row IS
  offered, and the real world's gates flipping (champion badge, a ball,
  evolution in reach, a frozen companion). The state window, the backup table
  containing `"tasks"`, an export/import round trip, and a garbage blob.
- `tools/emu/tests/tasks_ui_test.cpp` (real sketch): hub row opens it, label
  counts claimable, tap claims once, paging and backing out, BACK, vertical
  swipe, flushes, the sketch's minigame exits reach the bus, the TASK DONE
  banner on the main screen, `loop()` rolling the day and ticking the state
  task, and every label ASCII and fitting its row in six languages.

## Known gaps

- Wild, tower, rematch, breed and item-used tasks count events emitted by
  other features. Until those land, nothing emits them, so those tasks (the
  wild ones are "always possible") cannot complete on this branch alone.
- `canBreed` and rematch gating are guesses at other features' rules; see the
  table above.
- The tasks screen is paged but is tested in `tasks_ui_test`, not added to
  `swipe_test`, to keep this feature in its own files.
