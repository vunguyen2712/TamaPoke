# Weather on the main screen, and the Pokedex habitat view

Features 5 and 8 of Adventure round 2. Player-facing numbers are in README.md,
"Weather on the main screen and the habitat view"; this file is the why.

## Files

| File | What |
|---|---|
| `ui_weather.ino` | The overlay: `weatherSceneOverlay()` (called by `featSceneOverlay()` right after `drawScene()`), `weatherOverlayShown()` (THE rule for whether it draws), and two test probes, `weatherLastPrims()` / `weatherLastDrawn()`. |
| `habitat.h` | The habitat logic, header-only (no build-list edits): `habitatOf()`, `habitatTotals()`, `habitatMissingCount/At()`. No gfx. |
| `ui_habitat.ino` | The missing-list screen, the detail-page lines, the HABITAT pill on the grid. |
| `TamaPoke.ino` | Surgical hooks only, listed below. |
| `tools/emu/tests/weather_ui_test.cpp` / `habitat_ui_test.cpp` | The sketch, driven. `swipe_test` pages the habitat list too. |

No NVS keys: the overlay has no on/off setting and the habitat view stores
nothing.

## The overlay

**One weather, one clock.** It draws `wildWeatherOf(pet.currentDay())`, the
same call the WILD screen makes, and night is `gNight` (the hour or sleeping),
which `render()` has already set. With no clock (`lastSeenEpoch == 0`) nothing
is drawn: wild.cpp calls that day 0, CLEAR, midday, and drawing a weather from
`millis()` would be a second clock.

**Skipped** over the egg (its text and the region pill sit on the scene, and
the pill guards an irreversible tap) and during a ceremony (the runaway draws
its own rain). Sunny at night draws nothing.

**Budget.** The main screen redraws the whole frame at ~10 fps into a PSRAM
canvas, so the overlay is a few dozen lines and small rects, never a per-pixel
pass and never a blend. Every primitive goes through `wxPrim()`; the test
holds the worst frame across ~1.5 s of every weather to **120** (measured:
rain ~65, fog 50, snow ~50, sun ~23, wind ~19). Fog is the only "translucent"
effect, and it fakes it with lines every 2 px.

**Placement.** Only `y < 312` is ever visible (the bottom panel is painted over
it afterwards), and only inside radius ~232. Particles whose either end would
fall outside, or on the header's two text lines, are skipped rather than drawn.
`weather_ui_test` diffs the framebuffer before and after the overlay and fails
on any changed pixel off the panel, under the bottom panel or on the name line.

**Deterministic per frame.** Every position is a hash of the particle index
plus `millis()`; there is no state between frames. The sun's position repeats
`drawScene()`'s own (360,84 before 18:00, the horizon after) -- if the scene's
sun moves, move it here too.

## The habitat view

**Nothing is restated.** Every answer is asked of wild.cpp:

- can it be met at all -> `wildEligible(dex, regionOfDex(dex), MAX_LEVEL)`;
  the test checks this agrees with `HB_WILD` for every species in the dex
- from what level -> the lowest `l` with `wildEligible(dex, region, l)`, which
  is the predecessor's `evolveLevel` (the test checks all 361 wild evolved
  forms against `wildPredecessor()`)
- held back -> `wildTimeAllows()` over the 6 x 4 conditions. "ONLY" lists the
  windows open in every weather, then the weathers open in every window, so a
  GHOST reads `NIGHT/FOG ONLY` without the view knowing what a ghost is
- how likely -> `wildWeight()` divided by the region's total weight in that
  condition (`HabTotals`), summed per weather and per window. Raw weight would
  rank every NORMAL type "best in CLEAR" even where the competition makes it
  rarer; the share does not. Totals are counted at MAX_LEVEL for the species'
  own region, so the hint is for that region (on ALL the odds are thinner).

**Never wild:** `R_LEGENDARIO` -> EGG ONLY (legendaries do hatch, pet.cpp's
tier roll); `R_EVO` -> EVOLVE ONLY (stone / second-branch forms with no level
to gate on); no art or no pack -> NOT ON THIS CARD.

**Names: a hint list, not a spoiler.** The list is by definition species the
player has NOT registered, and the gallery shows those as a silhouette and
`???`. The list does the same (`N.092 ???` + silhouette) and adds only where
to look. The detail page shows the same lines for every species.

**Entry point.** A HABITAT pill on the grid's bottom row (the page number moved
left of centre to make room) -- that row was dead space for taps (`galleryTap`
maps it to grid row 4 and returns). The list is a sub-screen of the gallery:
`galleryOpen` stays set and `galleryRegion` is the region, so tapping a row
simply sets `galleryDetail` and backing out of the detail lands on the same
page. `SCR_HABITAT` ("habitat") is its own screen id for the crash crumb.

**Stale state.** `habitatShown()` drops `habitatOpen` whenever the gallery is
closed or on its chooser, and `uiCurrentScreen()` asks it every frame (via the
crumb), so re-opening the Pokedex never lands on an old habitat page.

**Locked, never hidden.** A region without its pack (reachable by the grid's
vertical swipe) shows its habitat page with NEEDS PACK and no rows to tap.

## TamaPoke.ino hooks

`SCR_HABITAT` + "habitat" in the screen enum/names; `uiCurrentScreen()`;
`render()` (habitat before the grid); `onTap()`, `onSwipe()`, `onSwipeV()`
(habitat after the detail check, before the grid); `renderGallery()` (detail
lines via `drawHabitatDetail()`, pill via `drawGalleryHabitatBtn()`, page
number moved to x 160); `galleryTap()` (pill hit).

## Not done

- No on/off setting for the overlay (no NVS key was needed for it).
- The habitat odds ignore the player's actual egg region / ALL and level.
- Silhouettes need `thumbs.bin` on the SD; without it the row is text only.
