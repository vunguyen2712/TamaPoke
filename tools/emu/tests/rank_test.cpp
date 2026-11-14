// Trainer rank as LOGIC: rank.cpp against the real Pet, tower and event bus.
// rank_ui_test drives the player card and the scene through the real sketch;
// this one pins the numbers and the rules the card, the avatar picker and the
// theme picker all ask.
#include "Arduino.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include "items.h"
#include "tower.h"
#include "rank.h"
#include "avatars.h"
#include "game_events.h"
#include <cstdio>
#include <cstring>
uint32_t g_seed = 31; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX = 0, g_touchY = 0; volatile bool g_touchDown = false; bool wasPressed = false;
static uint32_t g_ms = 0; uint32_t millis() { return g_ms; }
void FakeESP::restart() { exit(0); }
int FakeSerial::available() { return 0; } String FakeSerial::readStringUntil(char) { return String(""); }
void sfxPlay(uint8_t) {}

static int bad = 0;
static void ck(bool ok, const char *w) { printf("%s  %s\n", ok ? "PASS" : "FAIL", w); if (!ok) bad++; }

static int evUp = 0; static int16_t upArg = -1;
static void sink(GameEv ev, int16_t a) { if (ev == EV_RANK_UP) { evUp++; upArg = a; } }

static RankInput zero() { RankInput in; memset(&in, 0, sizeof(in)); return in; }

int main() {
  { Preferences pr; pr.clear(); }
  gameEventSubscribe(sink);

  // ---- rankOf: the thresholds, and monotonic
  ck(rankOf(0) == 1 && rankOf(29) == 1, "0..29 points is rank 1 (ROOKIE)");
  ck(rankOf(30) == 2, "30 points is rank 2");
  ck(rankOf(3999) == 9 && rankOf(4000) == RANK_MAX, "4000 points is the top rank");
  ck(rankOf(0xFFFFFFFFu) == RANK_MAX, "and nothing goes past it");
  {
    bool mono = true, framed = true;
    for (uint32_t p = 1; p < 6000; p++) {
      if (rankOf(p) < rankOf(p - 1)) mono = false;
      uint8_t r = rankOf(p);
      if (rankThreshold(r) > p || (r < RANK_MAX && rankThreshold(r + 1) <= p)) framed = false;
    }
    ck(mono, "rankOf never goes down as points go up");
    ck(framed, "rankThreshold(r) <= points < rankThreshold(r+1), for every score");
    bool distinct = true;
    for (uint8_t r = 1; r < RANK_MAX; r++) if (rankThreshold(r + 1) <= rankThreshold(r)) distinct = false;
    ck(distinct, "every rank needs strictly more than the last");
  }
  {
    bool titled = true;
    for (uint8_t r = 1; r <= RANK_MAX; r++) {
      const char *t = rankTitle(r);
      if (!t || !*t) titled = false;
      for (const char *c = t; c && *c; c++) if ((unsigned char)*c > 126) titled = false;
    }
    ck(titled, "every rank has an ASCII title");
  }

  // ---- the formula from synthetic state
  {
    RankInput in = zero();
    ck(rankPointsFrom(in) == 0, "nothing is worth nothing");
    in = zero(); in.badges = 8;       ck(rankPointsFrom(in) == 80, "8 easy badges = 80");
    in = zero(); in.badgesHard = 3;   ck(rankPointsFrom(in) == 30, "3 hard badges = 30");
    in = zero(); in.rematches = 4;    ck(rankPointsFrom(in) == 40, "4 rematches = 40");
    in = zero(); in.dex = 151;        ck(rankPointsFrom(in) == 302, "151 in the dex = 302");
    in = zero(); in.towerBest = 7;    ck(rankPointsFrom(in) == 35, "tower best 7 = 35");
    in = zero(); in.towerBest = 900;  ck(rankPointsFrom(in) == 500, "the tower counts up to 100 floors");
    in = zero(); in.catches = 10;     ck(rankPointsFrom(in) == 20, "10 catches = 20");
    in = zero(); in.catches = 60000;  ck(rankPointsFrom(in) == 500, "catches count up to 250");
    in = zero(); in.challenges = 2;   ck(rankPointsFrom(in) == 60, "2 challenge wins = 60");
    in = zero(); in.challenges = 999; ck(rankPointsFrom(in) == 1500, "challenge wins count up to 50");
    in = zero(); in.badges = 8; in.dex = 10; in.catches = 1;
    ck(rankPointsFrom(in) == 102, "the parts add up");
  }

  // ---- a real player
  Pet p; p.begin();
  if (p.awaitingStarter()) p.chooseStarter(4);
  if (p.isEgg()) p.dbgHatchAs(4, false);
  p.avatar = 0;
  towerBegin(); towerClearAll();
  rankBindPet(&p);
  rankBegin(); rankClearAll(); rankBegin();
  evUp = 0;
  ck(rankShown() == 1, "a new player is rank 1");
  {
    RankInput in = rankInputFor(p);
    ck(in.dex == p.registeredCount() && in.badges == p.badgeCount(false), "the input reads the pet's own counts");
    ck(rankPoints() == rankPointsFrom(in), "rankPoints() is the formula over that input");
  }
  uint32_t base = rankPoints();
  ck(base + 25 < 30, "(a starter alone is still rank 1, with room for the next three steps)");
  p.winBadge(0, 0, false); gameEvent(EV_GYM_WIN, 0);
  ck(rankPoints() == base + RANK_PTS_BADGE, "a badge adds RANK_PTS_BADGE");
  p.winBadge(0, 0, true); gameEvent(EV_GYM_WIN, 0);
  ck(rankPoints() == base + RANK_PTS_BADGE + RANK_PTS_BADGE_HARD, "the same badge on hard adds again");
  towerStartRun(5); towerWin(nullptr);
  ck(rankPoints() == base + 20 + RANK_PTS_TOWER, "the tower's best streak counts");
  ck(rankShown() == 1 && evUp == 0, "(still rank 1, no event)");

  // ---- rank-up: once
  p.winBadge(0, 1, false); gameEvent(EV_GYM_WIN, 1);   // base + 40 >= 30
  ck(rankShown() == 2, "crossing 30 points from an event is rank 2");
  ck(evUp == 1 && upArg == 2, "EV_RANK_UP fired once, with the new rank");
  ck(rankNoticePending(), "and the main screen has a notice waiting");
  rankUpdate(); rankUpdate(); gameEvent(EV_GYM_WIN, 1);
  ck(evUp == 1, "re-evaluating does not fire it again");
  rankBegin();
  ck(evUp == 1 && rankShown() == 2, "nor does a reload, and rank 2 is on flash");

  // ---- the shown rank never goes down
  {
    uint16_t keepB = p.badges, keepH = p.badgesHard;
    p.badges = 0; p.badgesHard = 0;
    towerClearAll();
    rankUpdate();
    ck(rankOf(rankPoints()) == 1, "(the points really did fall back to rank 1)");
    ck(rankShown() == 2, "but the rank shown stays 2");
    rankBegin();
    ck(rankShown() == 2, "and after a reload too");
    p.badges = keepB; p.badgesHard = keepH;
  }

  // ---- catches and challenge wins: counted from events, persisted
  {
    uint32_t before = rankPoints(); uint8_t shownBefore = rankShown();
    for (int i = 0; i < 3; i++) gameEvent(EV_WILD_CATCH, 16);
    ck(rankCatches() == 3, "three EV_WILD_CATCH are three catches");
    ck(rankPoints() == before + 3 * RANK_PTS_CATCH, "and are worth RANK_PTS_CATCH each");
    ck(rankShown() == shownBefore, "(no rank-up here, so nothing else saved them)");
    rankBegin();
    ck(rankCatches() == 3, "the catches are on flash at once");
    gameEvent(EV_WILD_WIN, 16); gameEvent(EV_WILD_SEEN, 16);
    ck(rankCatches() == 3, "a win or a sighting is not a catch");
    gameEvent(EV_CHALLENGE_WIN, 5); gameEvent(EV_CHALLENGE_WIN, 5);
    ck(rankChallengeWins() == 1, "the same week's challenge counts once");
    gameEvent(EV_CHALLENGE_WIN, 6);
    ck(rankChallengeWins() == 2, "the next week's counts again");
    gameEvent(EV_CHALLENGE_WIN, 0);
    ck(rankChallengeWins() == 3, "week 0 is a real week, not 'none'");
    rankBegin();
    ck(rankCatches() == 3 && rankChallengeWins() == 3, "both counters survive a reload");
    gameEvent(EV_CHALLENGE_WIN, 0);
    ck(rankChallengeWins() == 3, "and so does the last week counted");
  }

  // ---- avatars
  rankClearAll(); rankBegin();
  ck(rankShown() >= 1, "(cleared)");
  rankClearAll();   // rank 1 exactly, whatever the points say, until the next update
  ck(rankAvatarUnlocked(0) && rankAvatarUnlocked(1) && rankAvatarUnlocked(2), "RED, BLUE and GIRL are open at rank 1");
  {
    bool someLocked = false;
    for (uint8_t a = 0; a < AVATAR_COUNT; a++) if (!rankAvatarUnlocked(a)) someLocked = true;
    ck(someLocked, "(and some are locked, or none of this tests anything)");
    bool consistent = true;
    for (uint8_t a = 0; a < AVATAR_COUNT; a++)
      if (rankAvatarUnlocked(a) != (rankShown() >= rankAvatarNeeds(a))) consistent = false;
    ck(consistent, "unlocked means the shown rank reached rankAvatarNeeds()");
  }
  ck(rankNextAvatar(2) == 0, "cycling from GIRL at rank 1 skips every locked one back to RED");
  ck(!rankAvatarUnlocked(6), "SAILOR is locked at rank 1");
  ck(!rankAvatarUnlocked(AVATAR_COUNT), "an index past the art is never unlocked");
  // grandfathering: a save that already wears a locked avatar keeps it
  p.avatar = 0; rankClearAll(); rankBegin();
  ck(!rankAvatarUnlocked(6), "(wearing RED: SAILOR stays locked)");
  p.avatar = 6; rankClearAll(); rankBegin();
  ck(rankAvatarUnlocked(6), "wearing SAILOR from before ranks: it stays selectable");
  ck(!rankAvatarUnlocked(5), "but only that one: HIKER is still locked");
  p.avatar = 0; rankBegin();
  ck(rankAvatarUnlocked(6), "and it stays theirs after they switch away");

  // ---- themes
  rankClearAll(); rankBegin();
  {
    uint8_t r = rankShown();
    ck(r < rankThemeNeeds(THEME_STARRY), "(below the STARRY rank)");
    ck(rankThemeUnlocked(THEME_CLASSIC), "CLASSIC is always there");
    ck(!rankSetTheme(THEME_STARRY) && rankTheme() == THEME_CLASSIC, "a locked theme cannot be chosen");
    ck(!rankSetTheme(THEME_COUNT), "nor one that does not exist");
    int ev0 = evUp;
    for (uint8_t rg = 1; rg <= 3; rg++) p.badgesX[rg - 1] = (1u << TRAINER_COUNT) - 1;   // 39 badges
    gameEvent(EV_GYM_WIN, 12);
    ck(rankShown() >= rankThemeNeeds(THEME_STARRY), "39 more badges reach it");
    ck(evUp == ev0 + 1 && upArg == rankShown(), "a jump of several ranks is ONE event, with the rank reached");
    ck(rankSetTheme(THEME_STARRY) && rankTheme() == THEME_STARRY, "and now it can be chosen");
    rankBegin();
    ck(rankTheme() == THEME_STARRY, "the theme survives a reload");
  }

  // ---- first boot with ranks: seeded from the save, silently
  {
    Preferences pr; pr.clear();     // no rank keys at all, as on an upgrade
    int ev0 = evUp;
    rankBegin();
    ck(rankShown() == rankOf(rankPoints()) && rankShown() > 1, "an existing save starts at the rank its progress earns");
    ck(evUp == ev0, "without an EV_RANK_UP: nothing was just achieved");
    ck(rankNoticePending(), "but the main screen says so once");
  }

  // ---- the notice
  ck(rankNoticeActive(1000), "the notice shows from the first frame");
  ck(rankNoticeActive(1000 + RANK_NOTICE_MS - 1), "for RANK_NOTICE_MS");
  ck(!rankNoticeActive(1000 + RANK_NOTICE_MS), "and then goes");
  ck(!rankNoticePending() && !rankNoticeActive(99999), "and stays gone");

  printf("%s\n", bad ? "FAILURES" : "all good");
  return bad ? 1 : 0;
}
