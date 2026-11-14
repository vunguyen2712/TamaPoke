# Trainer rank and titles (round 2, feature 3)

Player-facing numbers: README "Trainer rank and titles". This file is the why.

## One rule, one function

| question | the only answer |
|---|---|
| how many points? | `rankPoints()` = `rankPointsFrom(rankInputFor(pet))` |
| which rank is that? | `rankOf(points)` (`rankThreshold()` for the bar) |
| which rank is SHOWN? | `rankShown()` -- the highest ever reached, `rkTop` |
| may I pick this avatar? | `rankAvatarUnlocked(i)` (`rankNextAvatar()` for the page-0 cycle) |
| may I pick this scene? | `rankThemeUnlocked(t)`, enforced inside `rankSetTheme()` |

The rank page (`ui_rank.ino`), the page-0 avatar tap (`TamaPoke.ino`) and the
theme picker all call these; none keeps its own table.

## Where the inputs come from

Everything except two counters already existed and is read live, never copied:
`Pet::badgeCount(false/true)` (all regions, all 13 trainers a ladder),
`rematchWon(region, idx)` over every ladder, `Pet::registeredCount()`, and
`towerBest()`. Catches and weekly challenge wins had no player-wide total, so
`rank.cpp` counts them from `EV_WILD_CATCH` and `EV_CHALLENGE_WIN` and keeps them
in NVS (`rkCat`, `rkChW`). A challenge win is counted once per week: the event's
arg (the week) is remembered in `rkChL` (stored +1 so week 0 is not "none").

The sink re-evaluates the rank on the events that can raise it (gym, rematch,
dex, tower, catch, challenge). Every emitter updates its state BEFORE emitting,
so the score read inside the sink already includes the win.

## Never down, event once

`rankUpdate()` only moves `s_top` up; `EV_RANK_UP(new rank)` is emitted after
`s_top` has moved, so re-evaluating, reloading or a second event cannot repeat
it. A jump of several ranks is one event with the rank reached. The sink
ignores `EV_RANK_UP` itself, so there is no recursion.

First boot with this feature (no `rkTop` key): the starting rank is whatever
the save's progress earns, set silently -- no `EV_RANK_UP`, because nothing was
just achieved -- but the main-screen notice does show once.

## Never take away what a player has

Avatars 3..7 are now rank-gated, but a save can already be wearing one. On
`rankBegin()` the worn avatar, if locked, is added to a grandfather mask
(`rkAvG`), and `rankAvatarUnlocked()` honours it -- so it stays selectable even
after switching away. Only a tap can change the avatar, and taps refuse locked
ones, so the mask cannot grow by any other route.

## Scene themes

`drawScene()` has three one-line hooks into `ui_rank.ino`:
`rankSceneNight()` (STARRY forces the night look: moon, stars, darkened soil),
`rankSceneSky()` (SUNSET recolours the daytime gradient) and
`rankSceneExtras()` (STARRY twinkles 8 more stars, FESTIVAL strings 14 flags).
All are a handful of rects/triangles. They run inside drawScene, before
`featSceneOverlay()`, so the weather draws on top of the theme.

## Notice

`featMainOverlay()` calls `rankMainNotice()`; the clock starts the first time
the main screen draws it (`rankNoticeActive(now)`), so a rank-up during a battle
is announced when you are back on the pet, for `RANK_NOTICE_MS` (4 s). It
waits out a MEDAL!/streak popup (same spot) without starting its clock. It is not
persisted: a reboot before it shows drops it (the rank itself is saved).
