#pragma once
// Where and when a species turns up in the wild (feature 8, the Pokedex
// habitat view). GAME LOGIC ONLY: no gfx, no SD, no touch. Header-only so it
// needs no new entry in the build lists; the screen is ui_habitat.ino.
//
// It never restates a wild rule. Every answer here is asked of wild.cpp:
//   - can it be met at all, and from what level  -> wildEligible()
//   - is it held back by the hour or the weather  -> wildTimeAllows()
//   - how likely in each condition                -> wildWeight()
// Design notes: docs/adventure-habitat.md.
#include <Arduino.h>
#include "dex.h"
#include "pet.h"
#include "noart.h"
#include "wild.h"

// How a species is obtained.
enum : uint8_t {
  HB_WILD = 0,   // it can be met in the wild (see the rest of HabInfo)
  HB_EGG,        // never wild: only from an egg (the legendaries)
  HB_EVOLVE,     // never wild: only by evolving something (split / stone branches)
  HB_NOCARD,     // cannot be drawn here: no art, or its region's pack is not on the card
};

// The total spawn weight of a region in each of the 24 conditions, so a
// species' weight can be read as its SHARE of that condition: in RAIN a
// NORMAL type keeps its weight while the WATER types around it triple, so its
// odds fall. Counted at MAX_LEVEL (every evolved form that can be wild is in).
struct HabTotals {
  uint8_t region = 0xFF;
  uint32_t t[WX_COUNT][WT_COUNT];
};

struct HabInfo {
  uint8_t how = HB_NOCARD;
  uint8_t bestWx = 0xFF, bestWx2 = 0xFF;    // 2 = a runner-up within 90% of the best
  uint8_t bestTod = 0xFF, bestTod2 = 0xFF;
  bool restricted = false;  // some condition is closed to it (wildTimeAllows)
  uint8_t onlyTod = 0;      // bits: windows open to it in EVERY weather
  uint8_t onlyWx = 0;       // bits: weathers open to it in EVERY window
  uint8_t minLevel = 0;     // lowest wild level it appears at (1 = any)
};

inline void habitatTotals(uint8_t region, HabTotals &out) {
  out.region = region;
  for (uint8_t w = 0; w < WX_COUNT; w++)
    for (uint8_t t = 0; t < WT_COUNT; t++) out.t[w][t] = 0;
  if (region >= REGION_COUNT || region == REGION_ALL) return;
  for (int16_t d = (int16_t)REGIONS[region].lo; d <= (int16_t)REGIONS[region].hi && d <= DEX_COUNT; d++) {
    if (!wildEligible(d, region, MAX_LEVEL)) continue;
    for (uint8_t w = 0; w < WX_COUNT; w++)
      for (uint8_t t = 0; t < WT_COUNT; t++) out.t[w][t] += wildWeight(d, w, t);
  }
}

// Picks the best index of `n` scores and a runner-up within 90% of it.
inline void habitatTop(const uint64_t *s, uint8_t n, uint8_t &best, uint8_t &second) {
  best = second = 0xFF;
  for (uint8_t i = 0; i < n; i++)
    if (s[i] && (best == 0xFF || s[i] > s[best])) best = i;
  if (best == 0xFF) return;
  for (uint8_t i = 0; i < n; i++) {
    if (i == best || !s[i]) continue;
    if (s[i] * 10 >= s[best] * 9 && (second == 0xFF || s[i] > s[second])) second = i;
  }
}

// The one answer for a species. `tot` must be habitatTotals() of the
// species' own region (regionOfDex); it is recomputed here if it is not.
inline HabInfo habitatOf(int16_t dex, HabTotals &tot) {
  HabInfo h;
  if (dex < 1 || dex > DEX_COUNT) return h;
  uint8_t region = regionOfDex(dex);
  if (region >= REGION_COUNT || !regionAvailable(region) || !speciesHasArt(dex)) return h;   // HB_NOCARD
  if (!wildEligible(dex, region, MAX_LEVEL)) {
    h.how = DEX_TBL[dex].rarity == R_EVO ? HB_EVOLVE : HB_EGG;
    return h;
  }
  h.how = HB_WILD;
  for (uint16_t l = 1; l <= MAX_LEVEL; l++)
    if (wildEligible(dex, region, (uint8_t)l)) { h.minLevel = (uint8_t)l; break; }

  // Which conditions let it out at all.
  uint8_t todAll = 0xFF & ((1u << WT_COUNT) - 1), wxAll = (uint8_t)((1u << WX_COUNT) - 1);
  for (uint8_t w = 0; w < WX_COUNT; w++)
    for (uint8_t t = 0; t < WT_COUNT; t++)
      if (!wildTimeAllows(dex, w, t)) {
        h.restricted = true;
        todAll &= (uint8_t)~(1u << t);
        wxAll &= (uint8_t)~(1u << w);
      }
  if (h.restricted) { h.onlyTod = todAll; h.onlyWx = wxAll; }

  // Its share of each condition, summed per weather and per window.
  if (tot.region != region) habitatTotals(region, tot);
  uint64_t sw[WX_COUNT] = { 0 }, st[WT_COUNT] = { 0 };
  for (uint8_t w = 0; w < WX_COUNT; w++)
    for (uint8_t t = 0; t < WT_COUNT; t++) {
      uint32_t all = tot.t[w][t];
      if (!all) continue;
      uint64_t s = (uint64_t)wildWeight(dex, w, t) * 1000000ull / all;
      sw[w] += s;
      st[t] += s;
    }
  habitatTop(sw, WX_COUNT, h.bestWx, h.bestWx2);
  habitatTop(st, WT_COUNT, h.bestTod, h.bestTod2);
  return h;
}

// The species of `region` that are NOT in the player's Pokedex, in dex order:
// the `idx`-th one, or 0 past the end. habitatMissingCount() counts them.
inline uint16_t habitatMissingCount(const Pet &p, uint8_t region) {
  if (region >= REGION_COUNT || region == REGION_ALL) return 0;
  uint16_t n = 0;
  for (int16_t d = (int16_t)REGIONS[region].lo; d <= (int16_t)REGIONS[region].hi && d <= DEX_COUNT; d++)
    if (!p.isRegistered(d)) n++;
  return n;
}
inline int16_t habitatMissingAt(const Pet &p, uint8_t region, uint16_t idx) {
  if (region >= REGION_COUNT || region == REGION_ALL) return 0;
  for (int16_t d = (int16_t)REGIONS[region].lo; d <= (int16_t)REGIONS[region].hi && d <= DEX_COUNT; d++)
    if (!p.isRegistered(d)) { if (!idx) return d; idx--; }
  return 0;
}
