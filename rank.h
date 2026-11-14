#pragma once
#include <Arduino.h>

// Trainer rank, titles and cosmetic unlocks (feature 3)
//
// GAME LOGIC ONLY: no gfx, no SD, no touch -- this file links into the
// headless test binaries next to pet.cpp. The rank page of the player card,
// the scene themes and the rank-up notice live in ui_rank.ino.
// Design notes: docs/adventure-rank.md.
//
// ONE RULE, ONE FUNCTION: rankPoints() is the score, rankOf() turns a score
// into a rank, and rankAvatarUnlocked()/rankThemeUnlocked() are the only
// answers to "may the player pick this?". The card, the avatar picker on page
// 0, the rank page's grid and the theme picker all ask them.

class Pet;

#define RANK_MAX 10

// ---- points: what each piece of player-wide progress is worth
#define RANK_PTS_BADGE 10       // per badge on easy, every region (13 a ladder)
#define RANK_PTS_BADGE_HARD 10  // per badge on hard, on top of the easy one
#define RANK_PTS_REMATCH 10     // per gym rematch won (first win per trainer)
#define RANK_PTS_DEX 2          // per species registered in the Pokedex
#define RANK_PTS_TOWER 5        // per floor of the best Battle Tower streak
#define RANK_TOWER_CAP 100      //   counted up to this streak
#define RANK_PTS_CATCH 2        // per wild catch
#define RANK_CATCH_CAP 250      //   counted up to this many catches
#define RANK_PTS_CHALLENGE 30   // per weekly gym challenge cleared
#define RANK_CHALLENGE_CAP 50   //   counted up to this many

// Everything the score is built from, so tests can feed synthetic state.
struct RankInput {
  uint16_t badges;       // easy badges, all regions
  uint16_t badgesHard;   // hard badges, all regions
  uint16_t rematches;    // rematch trainers beaten, all regions
  uint16_t dex;          // Pokedex registered
  uint16_t towerBest;    // best tower streak
  uint16_t catches;      // wild catches (counted by this module)
  uint16_t challenges;   // weekly challenge wins (counted by this module)
};

uint32_t rankPointsFrom(const RankInput &in);   // the formula
RankInput rankInputFor(const Pet &p);           // gathers it from a Pet + tower + counters
uint32_t rankPoints();                          // ... for the bound pet (0 unbound)

// Points -> rank 1..RANK_MAX. Non-decreasing in points.
uint8_t rankOf(uint32_t points);
uint32_t rankThreshold(uint8_t rank);           // points needed for `rank` (rank 1 = 0)
const char *rankTitle(uint8_t rank);            // six languages via TL(), ASCII

// The rank SHOWN: the highest ever reached, persisted. Never goes down.
uint8_t rankShown();
// Re-evaluates the score; on a new high, persists it, raises the main-screen
// notice and emits EV_RANK_UP(new rank) -- once per rank-up. Returns rankShown().
uint8_t rankUpdate();

// The counters this module owns (from EV_WILD_CATCH / EV_CHALLENGE_WIN).
uint16_t rankCatches();
uint16_t rankChallengeWins();

// ---- unlocks
uint8_t rankAvatarNeeds(uint8_t avatar);        // rank that unlocks it
bool rankAvatarUnlocked(uint8_t avatar);        // reached, or grandfathered
// The next avatar after `cur` that is unlocked (tap-to-cycle on page 0).
uint8_t rankNextAvatar(uint8_t cur);

enum : uint8_t { THEME_CLASSIC = 0, THEME_SUNSET, THEME_STARRY, THEME_FESTIVAL, THEME_COUNT };
uint8_t rankThemeNeeds(uint8_t theme);
bool rankThemeUnlocked(uint8_t theme);
const char *rankThemeName(uint8_t theme);
uint8_t rankTheme();                            // the chosen home scene theme
bool rankSetTheme(uint8_t theme);               // refused (false) while locked

// ---- the rank-up notice on the main screen
#define RANK_NOTICE_MS 4000
bool rankNoticePending();
// Called by the main screen with the time: the first call starts the clock,
// true while it should show, and it clears itself once shown long enough.
bool rankNoticeActive(uint32_t now);

// Which Pet the rank is computed from. The sketch binds its live pet
// (ui_rank.ino); tests bind their own. Unbound = no badges, no dex.
bool rankBindPet(const Pet *p);

// Called once from setup(), after pet/party/bag have loaded. Also re-reads
// NVS, which is how the tests prove the state is on flash and not just in RAM.
void rankBegin();
void rankClearAll();   // tests: forget every counter, the shown rank and the theme
