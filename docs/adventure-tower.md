# Battle Tower and gym rematches -- design notes

Player-facing numbers are in the README (§ "Battle Tower and gym rematches").
This file is the why, and what to keep intact when touching it.

## Files

| file | what |
|---|---|
| `tower.h` / `tower.cpp` | all the rules: unlocking, floors, team generation, streak, rewards, rematch teams and mask. Pure C++, links into every test |
| `ui_tower.ino` | the TOWER screen, the hub row, and the gym screen's REMATCH hooks (`gymRematchOn`, `gymCycleDifficulty`, `gymRowOpen/Done/Top`, `rematchStartBattle`, `towerWinBody`, `pickFoeHeader`, `towerPickFight`) |
| `TamaPoke.ino` | the battle's foe team (below), `buildSquad`'s `fixLvl`, `PICK_TOWER`, `pickRematch`, `gymRematch`, and the BK_GYM badge guard |

## The battle owns its foe team

A trainer fight used to find its next creature by re-reading
`BTL_TRAINERS[btlTrainer].team[...]`, and the win check read that roster's
`count`. That made "the roster" and "the fight" the same thing, which is
exactly what the tower and rematches are not.

Now `btlOpenTeamFight(team, n, iv, name)` copies a `TrainerMon` array into
`btlFoeTeam[]` / `btlFoeTeamN`, with `btlFoeIv` and `btlFoeName`, and from then
on ONE source answers every question: the lead, each replacement
(`btlDoSwap`), whether a fainted foe has anyone left (`btlResolve`), and the
"X sends out Y" line. `startTrainerBattle()` copies its roster in and calls it;
the rematch and the tower hand it their generated teams. A debug fight and a
LAN fight set `btlFoeTeamN = 0` (LAN keeps its own live `btlFoeSquad`, because
a linked opponent can switch out and back in).

The gym ladder's behaviour is unchanged, including the order of `random()`
calls (`foeFromSpecies` is called at the same points), so `gym_test`,
`badge_region_test`, `touch_test` and the rest are untouched.

## Kinds of fight, and what each may touch

| | btlKind | btlTrainer | badge | training reward | win screen |
|---|---|---|---|---|---|
| gym ladder | BK_GYM | leader idx | yes | yes | badge |
| rematch | BK_REMATCH | leader idx | **never** | yes | `towerWinBody()`: REMATCH, count, items |
| tower | BK_TOWER | -1 | never | no | none (WIN message, then the tower screen shows the result) |

The badge award in `btlResolve` asks `btlKind == BK_GYM`. Before rematches it
asked `btlTrainer >= 0`, which a rematch also satisfies. On a normal save that
guard would be **vacuous** for rematches -- every easy badge is already held
when the champion falls -- so `rematch_ui_test` unlocks rematches with the
champion's badge ALONE, and putting the old guard back hands out Brock's badge
there and fails the test.

## Tower

- **Unlock** = `hasBadge(r, TRAINER_COUNT-1, easy or hard)` for any ladder.
  `towerUnlocked()` asks the creature bound with `towerBindPet()`: tower.cpp
  cannot name the sketch's `pet` (pet lives in the sketch, and every test binary
  links tower.cpp whether it has one or not). `ui_tower.ino` binds it at static
  initialisation, so it answers from the first frame.
- **Levels**: `buildSquad(..., fixLvl = TOWER_LEVEL)` raises AND caps on the
  copies it builds (the same principle as the hard-mode caps); foes are built at
  level 50 by `foeFromSpecies`. The stored pet and party are never written.
- **Floors are deterministic**: `towerTeamFor(floor, seed)` uses its own
  xorshift, not `random()`, so the tower screen can show the floor's team and the
  fight is exactly that team. The seed is rolled when a run ENDS (`towerLoss`),
  not on START, so the floor-1 team on screen before START is the one fought.
  A power cut mid-fight leaves the streak as it was and the same floor waiting.
- **Pool**: drawable (`speciesHasArt` + `regionAvailable(regionOfDex)`), BST in
  the floor's band, no legendary before floor 22 (judged by the ROOT of the
  line, so COSMOEM counts; Ultra Beasts are R_RARO in the data but count as
  legendary). If the band holds fewer than three (only one small pack on the
  card), it widens by 30 a side until it does. A pick that has no damaging move
  at level 50 (PYUKUMUKU) is re-rolled.
- **Rewards** are granted inside `towerWin()`, after the streak is saved: a reset
  between the two loses a reward rather than paying it twice.
- **Late floors are legendary-heavy** (measured: 93 of 120 creatures on floor 30
  over 40 runs). That is what a 550-650 band contains; it is the endgame, not a
  bug. Lower `TOWER_BST_TOP` if it should stay mortal.

## Rematches

- **Unlock** per region: that region's champion on EASY. A hard-only champion
  does not open it (the hard ladder is its own run already).
- `rematchTeam()` is deterministic per (region, trainer), so the list, the
  picker header and the fight agree. The gym list caches top level and count per
  ladder (`gymRowTop`), dropped when `gRegionArt` changes.
- Fillers: the leader's type, drawable, not legendary, the form it would be at
  the fill level AND reachable by it (a stage whose pre-evolution evolves above
  that level is too young), able to attack. The six highest-BST are the
  shortlist; a per-trainer seed picks. They go before the ace.
- Wins are stored in `rmWin`, one `uint16_t` per ladder as bytes. A shorter blob
  (fewer ladders) reads into the front -- additive, the same way as `badgX`.

## NVS keys (save.cpp, under the tower comment)

`twStrk` (U16 streak), `twBest` (U16), `twSeed` (U32), `rmWin` (bytes,
`uint16_t[GYM_REGIONS]`). All new; nothing existing is reinterpreted.

## Tests

- `tower_test` -- logic: unlock, team validity over floors 1-40 x 4 seeds, the
  measured strength curve, Kanto-only card, streak/best/reload, reward exactly at
  multiples of 7 and once, rematch unlock/sequence/mask/reward/reload, additive
  `rmWin`, every rematch team of every ladder against its original.
- `tower_ui_test` -- the real sketch: locked hub row, START -> picker (3) ->
  FIGHT, level 50 both sides with stored creatures untouched, the fought team ==
  the shown team, replacements in order, win / 7th-win reward / RUN / loss on the
  streak, the swipe back, a gym win leaving the streak alone.
- `rematch_ui_test` -- the pill cycle, rows, picker, the fight, no badge leak,
  mask + reload, the next rematch opening, the ordinary ladder unchanged.
