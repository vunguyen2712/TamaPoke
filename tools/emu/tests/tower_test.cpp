// The Battle Tower and gym rematches, as LOGIC: tower.cpp against the real
// Pet, Bag and dex. The screens and the fights are driven through the real
// sketch in tower_ui_test and rematch_ui_test; this one measures the numbers.
//
// Strength is MEASURED, not assumed: the mean base-stat total and the IV of
// generated teams are computed from what towerTeamFor() returns, and the
// rematch teams are compared against the real rosters slot by slot.
#include "Arduino.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include "items.h"
#include "tower.h"
#include "noart.h"
#include "game_events.h"
#include "moves.h"
#include <cstdio>
#include <cstring>
uint32_t g_seed = 9; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX = 0, g_touchY = 0; volatile bool g_touchDown = false; bool wasPressed = false;
static uint32_t g_ms = 0; uint32_t millis() { return g_ms; }
void FakeESP::restart() { exit(0); }
int FakeSerial::available() { return 0; } String FakeSerial::readStringUntil(char) { return String(""); }
void sfxPlay(uint8_t) {}

static int bad = 0;
static void ck(bool ok, const char *w) { printf("%s  %s\n", ok ? "PASS" : "FAIL", w); if (!ok) bad++; }

static int evWin = 0, evLoss = 0, evRm = 0;
static int16_t lastWinArg = -1, lastLossArg = -1, lastRmArg = -1;
static bool winArgsInOrder = true;
static void sink(GameEv ev, int16_t arg) {
  if (ev == EV_TOWER_WIN) { evWin++; if (arg != lastWinArg + 1 && !(arg == 1)) winArgsInOrder = false; lastWinArg = arg; }
  if (ev == EV_TOWER_LOSS) { evLoss++; lastLossArg = arg; }
  if (ev == EV_REMATCH_WIN) { evRm++; lastRmArg = arg; }
}

// TrainerMon has a padding byte, so compare fields, never memcmp
static bool sameTeam(const TrainerMon *a, const TrainerMon *b, int n) {
  for (int i = 0; i < n; i++) if (a[i].dex != b[i].dex || a[i].level != b[i].level) return false;
  return true;
}

static bool hasDamagingMove(int16_t dex, uint8_t lvl) {
  Pet f; f.dbgHatchAs(dex, false);
  f.ageMinutes = (uint32_t)(lvl - 1) * MINUTES_PER_LEVEL;
  f.relearnFromLevel();
  for (int i = 0; i < MOVE_SLOTS; i++)
    if (f.moves[i] && MOVE_TBL[f.moves[i]].cat != MC_STATUS) return true;
  return false;
}

int main() {
  gameEventSubscribe(sink);
  Pet p; p.begin();
  if (p.awaitingStarter()) p.chooseStarter(4);
  if (p.isEgg()) p.dbgHatchAs(4, false);
  bag.begin(); bag.clearAll();
  towerBegin(); towerClearAll();

  // ---- unlocking
  ck(!towerUnlocked(), "unbound: the tower is locked");
  towerBindPet(&p);
  ck(!towerUnlocked(), "fresh save: locked");
  p.winBadge(0, TRAINER_COUNT - 2, false);
  ck(!towerUnlocked(), "the last Elite 4 member is not enough");
  p.winBadge(3, TRAINER_COUNT - 1, true);
  ck(towerUnlocked(), "a champion beaten on HARD, in Sinnoh, unlocks it");
  {
    Pet q; q.begin();
    ck(towerUnlockedFor(q), "and that survives a reload of the badges");
  }
  {
    Pet e; e.badges = 0; e.badgesHard = 0;
    for (int r = 0; r < GYM_REGIONS - 1; r++) { e.badgesX[r] = 0; e.badgesHardX[r] = 0; }
    ck(!towerUnlockedFor(e), "(a blank creature is locked)");
    e.winBadge(GYM_REGIONS - 1, TRAINER_COUNT - 1, false);
    ck(towerUnlockedFor(e), "a champion on EASY in the last ladder unlocks it too");
  }

  // ---- generated teams
  bool allValid = true, noDup = true, allDrawable = true, allLv50 = true, inBand = true,
       noEarlyLegend = true, determ = true, smartOk = true, ivMono = true, movesOk = true;
  uint8_t lastIv = 0;
  const uint32_t seeds[4] = { 1, 77, 12345, 0xDEADBEEF };
  for (uint16_t fl = 1; fl <= 40; fl++) {
    uint8_t iv = towerIvFor(fl);
    if (iv < lastIv) ivMono = false;
    lastIv = iv;
    if (towerSmartFor(fl) != (fl >= TOWER_SMART_FLOOR)) smartOk = false;
    uint16_t lo, hi; towerBand(fl, lo, hi);
    for (int si = 0; si < 4; si++) {
      TowerTeam t, t2;
      if (!towerTeamFor(fl, seeds[si], t) || t.count != TOWER_TEAM) { allValid = false; continue; }
      towerTeamFor(fl, seeds[si], t2);
      if (!sameTeam(t.mon, t2.mon, TOWER_TEAM)) determ = false;
      if (t.iv != iv) allValid = false;
      for (int a = 0; a < t.count; a++) {
        int16_t d = (int16_t)t.mon[a].dex;
        if (!towerDrawable(d) || !speciesHasArt(d)) allDrawable = false;
        if (t.mon[a].level != TOWER_LEVEL) allLv50 = false;
        if (speciesBst(d) < lo || speciesBst(d) > hi) inBand = false;
        if (fl < TOWER_LEGEND_FLOOR && speciesLegendary(d)) noEarlyLegend = false;
        for (int b = 0; b < a; b++) if (t.mon[b].dex == t.mon[a].dex) noDup = false;
        if (si < 2 && !hasDamagingMove(d, TOWER_LEVEL)) {
          printf("      floor %u: %s has no damaging move at 50\n", fl, DEX_TBL[d].name);
          movesOk = false;
        }
      }
    }
  }
  ck(allValid, "every floor 1-40 fields a full team of 3 at the floor's IV");
  ck(noDup, "no duplicate within a team");
  ck(allDrawable, "every creature can be drawn (art + pack)");
  ck(allLv50, "every foe is level 50");
  ck(inBand, "every foe sits in its floor's base-stat band");
  ck(noEarlyLegend, "no legendary before floor 22");
  ck(determ, "a floor's team is deterministic for its seed (what is shown is what is fought)");
  ck(smartOk, "hard AI exactly from floor 7");
  ck(ivMono && towerIvFor(1) == EASY_IV && towerIvFor(15) == HARD_IV && towerIvFor(99) == HARD_IV,
     "IVs climb EASY_IV (floor 1) -> HARD_IV (floor 15) and stay there");
  ck(movesOk, "every foe has a damaging move at level 50");
  {
    TowerTeam a, b; towerTeamFor(1, 1, a); towerTeamFor(1, 2, b);
    ck(!sameTeam(a.mon, b.mon, TOWER_TEAM), "a different run climbs a different tower");
  }
  // strength, measured over 40 seeds
  {
    auto meanBst = [](uint16_t f0, uint16_t f1) {
      uint32_t sum = 0, n = 0;
      for (uint32_t s = 1; s <= 40; s++)
        for (uint16_t f = f0; f <= f1; f++) {
          TowerTeam t; towerTeamFor(f, s * 2654435761u, t);
          for (int k = 0; k < t.count; k++) { sum += speciesBst((int16_t)t.mon[k].dex); n++; }
        }
      return n ? sum / n : 0;
    };
    uint32_t early = meanBst(1, 3), mid = meanBst(10, 12), late = meanBst(25, 27);
    printf("      mean BST: floors 1-3 = %u, 10-12 = %u, 25-27 = %u\n", early, mid, late);
    ck(early + 60 < mid && mid + 60 < late, "teams get measurably stronger with the floor");
    int legends = 0;
    for (uint32_t s = 1; s <= 40; s++) {
      TowerTeam t; towerTeamFor(30, s, t);
      for (int k = 0; k < t.count; k++) legends += speciesLegendary((int16_t)t.mon[k].dex);
    }
    printf("      legendaries on floor 30 over 40 runs: %d\n", legends);
    ck(legends > 0, "and legendaries do turn up late (the gate is real, not a blanket ban)");
  }
  // only Kanto on the card: still a full team, all from Kanto
  {
    uint16_t keep = gRegionArt;
    gRegionArt = 1;   // Kanto only
    bool ok = true;
    for (uint16_t fl : { 1, 9, 30 }) {
      TowerTeam t;
      if (!towerTeamFor(fl, 5, t) || t.count != TOWER_TEAM) ok = false;
      for (int k = 0; k < t.count; k++) if (t.mon[k].dex > 151) ok = false;
    }
    ck(ok, "with only the Kanto pack, a full team and every creature from Kanto");
    gRegionArt = keep;
  }

  // ---- streak, best, rewards
  ck(towerStreak() == 0 && towerBest() == 0 && !towerRunActive(), "a fresh tower: streak 0, best 0");
  bool rwOnlyAt7 = true;
  for (uint16_t s = 0; s <= 100; s++) { TowerReward r; if (towerRewardFor(s, r) != (s && s % 7 == 0)) rwOnlyAt7 = false; }
  ck(rwOnlyAt7, "a reward exactly at every multiple of 7");
  uint8_t ub0 = bag.count(ITEM_ULTRABALL);
  bool noEarly = true;
  for (int i = 1; i <= 6; i++) { TowerReward g; towerWin(&g); if (g.n) noEarly = false; }
  ck(noEarly && bag.count(ITEM_ULTRABALL) == ub0, "wins 1-6 pay nothing");
  ck(towerStreak() == 6 && towerBest() == 6 && towerRunActive(), "six wins: streak 6, best 6, run active");
  TowerReward g7; towerWin(&g7);
  ck(g7.n == 2 && bag.count(ITEM_ULTRABALL) == ub0 + 3 && bag.count(ITEM_SITRUS_BERRY) == 2,
     "the 7th win bags 3 ULTRA BALL + 2 SITRUS BERRY");
  towerBegin();          // a reload: the run is on flash
  {
    Bag b2; b2.begin();
    ck(towerStreak() == 7 && towerBest() == 7, "the streak and the best survive a reload");
    ck(b2.count(ITEM_ULTRABALL) == ub0 + 3, "and a reload does not pay the reward again");
  }
  TowerReward g8; towerWin(&g8);
  ck(g8.n == 0 && bag.count(ITEM_ULTRABALL) == ub0 + 3, "the 8th win pays nothing (once, not every win after 7)");
  for (int i = 9; i <= 13; i++) towerWin(nullptr);
  TowerReward g14; towerWin(&g14);
  ck(g14.n == 3 && bag.count(ITEM_LUM_BERRY) == 2 && bag.count(ITEM_LEFTOVERS) == 1 &&
     bag.count(ITEM_ULTRABALL) == ub0 + 6, "the 14th: 3 ULTRA BALL + 2 LUM BERRY + LEFTOVERS");
  ck(evWin == 14 && lastWinArg == 14 && winArgsInOrder, "EV_TOWER_WIN fired 14 times, arg = the new streak");
  uint32_t seedBefore = towerSeed();
  towerLoss();
  ck(towerStreak() == 0 && towerBest() == 14 && !towerRunActive(), "a loss ends the run; the best is kept");
  ck(evLoss == 1 && lastLossArg == 14, "EV_TOWER_LOSS carries the streak that ended");
  ck(towerSeed() != seedBefore, "and the next run gets new floors");
  towerBegin();
  ck(towerStreak() == 0 && towerBest() == 14, "reset and best both on flash");
  towerWin(nullptr);
  ck(towerStreak() == 1 && towerBest() == 14, "a new run starts from 1 under the old best");
  towerStartRun(4242);
  ck(towerStreak() == 0 && towerSeed() == 4242, "towerStartRun seeds a fresh run");

  // ---- rematches
  ck(rematchLevel(12) == 25 && rematchLevel(50) == 72 && rematchLevel(60) == 85 &&
     rematchLevel(72) == 100 && rematchLevel(90) == 100, "rematch level = lvl*5/4 + 10, capped at 100");
  {
    Pet q; q.badges = 0; q.badgesHard = 0;
    for (int r = 0; r < GYM_REGIONS - 1; r++) { q.badgesX[r] = 0; q.badgesHardX[r] = 0; }
    ck(!rematchUnlockedFor(q, 1), "rematch locked before the champion");
    q.winBadge(1, TRAINER_COUNT - 1, true);
    ck(!rematchUnlockedFor(q, 1), "a HARD champion win does not open the EASY rematch");
    q.winBadge(1, TRAINER_COUNT - 1, false);
    ck(rematchUnlockedFor(q, 1) && !rematchUnlockedFor(q, 0), "the easy champion opens THAT region only");
    ck(rematchOpenFor(q, 1, 0) && !rematchOpenFor(q, 1, 1), "rematches are sequential");
    uint8_t ub = bag.count(ITEM_ULTRABALL), ob = bag.count(ITEM_ORAN_BERRY);
    uint16_t b0 = q.badges, bx = q.badgesX[0];
    int ev0 = evRm;
    TowerReward r;
    bool first = rematchWin(1, 0, &r);
    ck(first && rematchWon(1, 0) && rematchMask(1) == 1 && rematchMask(0) == 0,
       "a rematch win lands in ITS region's rematch mask");
    ck(q.badges == b0 && q.badgesX[0] == bx, "and in no badge mask");
    ck(bag.count(ITEM_ORAN_BERRY) == ob + 1 && bag.count(ITEM_ULTRABALL) == ub + 2,
       "first win: an ORAN BERRY and 2 ULTRA BALLs");
    ck(evRm == ev0 + 1 && lastRmArg == 0, "EV_REMATCH_WIN with the trainer index");
    ck(rematchOpenFor(q, 1, 1), "and the next rematch opens");
    first = rematchWin(1, 0, &r);
    ck(!first && bag.count(ITEM_ORAN_BERRY) == ob + 2 && bag.count(ITEM_ULTRABALL) == ub + 2,
       "a repeat win: the berry only");
    towerBegin();
    ck(rematchWon(1, 0) && !rematchWon(0, 0), "the rematch mask survives a reload");
  }
  // additive storage: an older, shorter blob reads into the front
  {
    Preferences pr; pr.begin("tamapoke", false);
    uint16_t two[2] = { 0x0005, 0x0002 };
    pr.putBytes("rmWin", two, sizeof(two));
    towerBegin();
    ck(rematchMask(0) == 5 && rematchMask(1) == 2 && rematchMask(2) == 0,
       "a shorter rmWin blob (fewer ladders) keeps its meaning");
  }
  // stronger teams, for every trainer of every ladder
  {
    bool full = true, raised = true, stronger = true, drawable = true, themed = true,
         aceLast = true, stable = true;
    for (uint8_t rg = 0; rg < GYM_REGIONS; rg++)
      for (uint8_t i = 0; i < TRAINER_COUNT; i++) {
        const Trainer &t = TRAINER_SETS[rg].list[i];
        TrainerMon m[TRAINER_TEAM_MAX], m2[TRAINER_TEAM_MAX];
        uint8_t n = rematchTeam(rg, i, m);
        rematchTeam(rg, i, m2);
        if (!sameTeam(m, m2, n)) stable = false;
        if (n != TRAINER_TEAM_MAX) { full = false; printf("      %s %s: %u\n", TRAINER_SETS[rg].region, t.name, n); }
        uint32_t ob = 0, nb = 0, ol = 0, nl = 0;
        for (int k = 0; k < t.count; k++) { ob += speciesBst((int16_t)t.team[k].dex); ol += t.team[k].level; }
        for (int k = 0; k < n; k++) { nb += speciesBst((int16_t)m[k].dex); nl += m[k].level; }
        if (nb < ob || nl <= ol || n < t.count) {
          stronger = false;
          printf("      %s %s: levels %u -> %u, BST %u -> %u\n", TRAINER_SETS[rg].region, t.name, ol, nl, ob, nb);
        }
        // the originals are all there, in order, each raised and evolved
        int fillers = n - t.count;
        for (int k = 0; k < t.count; k++) {
          int at = (k == t.count - 1) ? n - 1 : k;   // fillers go before the ace
          if (m[at].level != rematchLevel(t.team[k].level)) raised = false;
          if (speciesBst((int16_t)m[at].dex) < speciesBst((int16_t)t.team[k].dex)) raised = false;
          if (k == t.count - 1 && evolveForLevel((int16_t)t.team[k].dex, m[at].level) != (int16_t)m[at].dex) aceLast = false;
        }
        for (int k = t.count - 1; k < t.count - 1 + fillers; k++) {
          const DexEntry &e = DEX_TBL[m[k].dex];
          if (!towerDrawable((int16_t)m[k].dex) || speciesLegendary((int16_t)m[k].dex)) drawable = false;
          if (e.type1 != t.type && e.type2 != t.type) themed = false;
        }
      }
    ck(full, "every rematch team is filled to six");
    ck(raised, "every original creature returns at its raised level, never weaker");
    ck(aceLast, "the leader's ace still closes the team");
    ck(stronger, "every rematch team: more total levels, no less total base stats, no fewer creatures");
    ck(drawable, "fillers are drawable and never legendary");
    ck(themed, "fillers keep the leader's type");
    ck(stable, "a rematch team is the same every time it is built");
    TrainerMon m[TRAINER_TEAM_MAX];
    uint8_t n = rematchTeam(0, 0, m);
    printf("      KANTO BROCK rematch:");
    for (int k = 0; k < n; k++) printf(" %s L%u", DEX_TBL[m[k].dex].name, m[k].level);
    printf("\n");
  }

  printf(bad ? "tower_test: %d FAILED\n" : "tower_test: all passed\n", bad);
  return bad ? 1 : 0;
}
