// The daycare's rules, against the real breed.cpp + pet.cpp + party.cpp.
//
// The rule (compatibility, the egg's species, inheritance) is checked on its
// own, and then again THROUGH its callers -- breedMake(), newEgg(), hatch(),
// the swap-in -- because a rule that is right while its caller ignores it is
// CLAUDE.md trap 1. Every guard here was broken on purpose once and watched to
// fail; see docs/adventure-daycare.md for which.
#include "Arduino.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include "items.h"
#include "breed.h"
#include "noart.h"
#include "game_events.h"
#include <cstdio>
uint32_t g_seed = 77; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX = 0, g_touchY = 0; volatile bool g_touchDown = false; bool wasPressed = false;
static uint32_t g_ms = 0; uint32_t millis() { return g_ms; }
void FakeESP::restart() { exit(0); }
int FakeSerial::available() { return 0; } String FakeSerial::readStringUntil(char) { return String(""); }
void sfxPlay(uint8_t) {}
static int bad = 0;
static void ck(bool ok, const char *w) { printf("%s  %s\n", ok ? "PASS" : "FAIL", w); if (!ok) bad++; }

static PartyMon mon(int16_t dex, uint8_t a, uint8_t d, uint8_t s, uint8_t h, bool shiny = false) {
  PartyMon m; m.dex = dex; m.level = 30; m.ivAtk = a; m.ivDef = d; m.ivSpe = s; m.ivHp = h;
  m.shiny = shiny ? 1 : 0; return m;
}
static PartyMon mon(int16_t dex) { return mon(dex, 16, 16, 16, 16); }

static bool isBase(int16_t d) {
  for (int16_t x = 1; x <= DEX_COUNT; x++) if (DEX_TBL[x].evolvesTo == (uint16_t)d) return false;
  for (int i = 0; i < EEVEE_EVO_COUNT; i++) if (EEVEE_EVOS[i] == d) return false;
  return true;
}

static int evBreeds = 0; static int16_t evLast = 0;
static void sink(GameEv e, int16_t arg) { if (e == EV_BREED) { evBreeds++; evLast = arg; } }

// What an inherited IV hatches as: itself, lifted to 20 if the egg came out
// shiny -- rollIVs()'s shiny floor applies to a bred egg like any other.
static uint8_t expectIV(uint8_t iv, bool shiny) { return (shiny && iv < 20) ? 20 : iv; }

#define DAY 20000UL   // an arbitrary game day
static void setDay(Pet &p, uint32_t day) { p.dbgSetSeen(day * 86400UL + 3600); }

int main() {
  // ---------------------------------------------------------------- the rule
  PartyMon ditto = mon(BREED_DITTO), ditto2 = mon(BREED_DITTO);
  PartyMon pika = mon(25), raichu = mon(26), squirtle = mon(7), wartortle = mon(8);
  ck(breedCheck(pika, raichu) == BV_OK, "PIKACHU + RAICHU share ELECTRIC: compatible");
  ck(breedEggSpecies(pika, raichu) == 172, "...and the egg is the line's BASE form, PICHU");
  ck(breedCheck(pika, squirtle) == BV_NO_TYPE, "PIKACHU + SQUIRTLE share no type: refused");
  ck(breedCheck(wartortle, ditto) == BV_OK && breedEggSpecies(wartortle, ditto) == 7,
     "WARTORTLE + DITTO -> a SQUIRTLE egg");
  ck(breedCheck(ditto, wartortle) == BV_OK && breedEggSpecies(ditto, wartortle) == 7,
     "DITTO + WARTORTLE -> SQUIRTLE too: Ditto never decides the species");
  ck(breedCheck(ditto, ditto2) == BV_TWO_DITTO, "two DITTOs: refused");
  ck(breedEggSpecies(ditto, ditto2) == 0, "...and give no egg");
  ck(breedCheck(squirtle, squirtle) == BV_SAME, "a creature cannot breed with itself");
  ck(breedCheck(mon(150), ditto) == BV_LEGEND && breedCheck(ditto, mon(150)) == BV_LEGEND,
     "MEWTWO + DITTO refused, either order");
  ck(breedCheck(squirtle, mon(245)) == BV_LEGEND, "SQUIRTLE + SUICUNE (both WATER) refused: legendary");
  ck(breedCheck(mon(490), ditto) == BV_LEGEND, "MANAPHY refused although its own row is not legendary (PHIONE is)");
  ck(breedCheck(mon(197), ditto) == BV_OK && breedEggSpecies(mon(197), ditto) == DEX_EEVEE,
     "UMBREON (an EEVEE_EVOS branch) breeds an EEVEE");
  ck(breedCheck(PartyMon(), ditto) == BV_EMPTY, "an empty slot cannot breed");
  // parent A decides when both qualify
  ck(breedEggSpecies(mon(5), mon(58)) == 4 && breedEggSpecies(mon(58), mon(5)) == 58,
     "CHARMELEON + GROWLITHE: the egg follows whichever is parent A");

  // every legendary in the dex, both ways and with a same-typed partner
  int legends = 0, legendLeaks = 0;
  for (int16_t d = 1; d <= DEX_COUNT; d++) {
    if (DEX_TBL[d].rarity != R_LEGENDARIO) continue;
    legends++;
    PartyMon L = mon(d), mate = mon(d == 1 ? 2 : 1);
    for (int16_t x = 1; x <= DEX_COUNT; x++)   // a non-legendary that shares its type
      if (DEX_TBL[x].rarity == R_COMUN && (DEX_TBL[x].type1 == DEX_TBL[d].type1)) { mate = mon(x); break; }
    if (breedCheck(L, ditto) != BV_LEGEND || breedCheck(ditto, L) != BV_LEGEND ||
        breedCheck(L, mate) != BV_LEGEND || breedCheck(mate, L) != BV_LEGEND) legendLeaks++;
  }
  ck(legends > 20 && legendLeaks == 0, "every legendary is refused, as parent A or B, with Ditto or a same-type mate");

  // ---- roll many pairs: every egg is a base form that the egg pool could hatch
  int ok = 0, wrong = 0, missedRule = 0;
  for (int i = 0; i < 40000; i++) {
    PartyMon a = mon(1 + random(DEX_COUNT)), b = (random(6) == 0) ? ditto : mon(1 + random(DEX_COUNT));
    BreedVerdict v = breedCheck(a, b);
    if (v != BV_OK) continue;
    ok++;
    int16_t e = breedEggSpecies(a, b);
    bool good = e >= 1 && e <= DEX_COUNT && isBase(e) && speciesHasArt(e) &&
                regionAvailable(regionOfDex(e)) &&
                (DEX_TBL[e].rarity == R_COMUN || DEX_TBL[e].rarity == R_RARO) &&
                !breedLineLegendary(a.dex) && !breedLineLegendary(b.dex);
    if (!good) { if (wrong < 5) printf("      bad egg %d from %d + %d\n", e, a.dex, b.dex); wrong++; }
    const DexEntry &x = DEX_TBL[a.dex], &y = DEX_TBL[b.dex];
    bool share = (x.type1 == y.type1 || x.type1 == y.type2 || (x.type2 != T_NONE && (x.type2 == y.type1 || x.type2 == y.type2)));
    if (!share && a.dex != BREED_DITTO && b.dex != BREED_DITTO) missedRule++;
  }
  ck(ok > 2000, "random pairs: plenty of compatible ones to judge");
  ck(wrong == 0, "every bred egg: a base form, drawable, region-available, egg-pool rarity, no legendary line");
  ck(missedRule == 0, "every accepted pair shares a type or has exactly one Ditto");

  // ---- an art-less species never comes out of an egg (Unova has some)
  {
    int checked = 0, leaked = 0;
    for (int i = 0; i < NO_ART_COUNT; i++) {
      int16_t n = NO_ART[i];
      if (breedLineLegendary(n)) continue;
      checked++;
      int16_t e = breedEggSpecies(mon(n), ditto);
      if (e && !speciesHasArt(e)) leaked++;
    }
    ck(checked > 0 && leaked == 0, "a species with no art never hatches from a bred egg");
  }

  // ---- region gating: only the Kanto pack on the card
  gRegionArt = 1u << 0;
  ck(breedCheck(pika, raichu) == BV_NO_EGG, "Kanto pack only: PIKACHU's egg (PICHU, Johto) is refused");
  ck(breedCheck(squirtle, ditto) == BV_OK, "...while a Kanto line still breeds");
  {
    int n = 0, outside = 0;
    for (int i = 0; i < 20000; i++) {
      PartyMon a = mon(1 + random(DEX_COUNT));
      int16_t e = breedEggSpecies(a, ditto);
      if (!e) continue;
      n++;
      if (regionOfDex(e) != 0) outside++;
    }
    ck(n > 500 && outside == 0, "Kanto pack only: every bred egg is a Kanto species");
  }
  gRegionArt = 0xFFFF;
  ck(breedCheck(pika, raichu) == BV_OK, "all packs again: PICHU is back");

  // ---- inheritance, measured
  {
    PartyMon A = mon(7, 10, 11, 12, 13), B = mon(7, 20, 21, 22, 23);
    int fromA = 0, inherited = 0, notThree = 0, foreign = 0, rolledAt[4] = { 0, 0, 0, 0 };
    const int N = 4000;
    for (int i = 0; i < N; i++) {
      uint8_t iv[4];
      breedInherit(A, B, false, iv);
      int k = 0;
      for (int s = 0; s < 4; s++) {
        if (iv[s] == BREED_IV_ROLL) { rolledAt[s]++; continue; }
        k++; inherited++;
        if (iv[s] == 10 + s) fromA++;
        else if (iv[s] != 20 + s) foreign++;
      }
      if (k != BREED_INHERIT) notThree++;
    }
    printf("      no charm: %d of %d inherited from A; rolled slot spread %d/%d/%d/%d\n",
           fromA, inherited, rolledAt[0], rolledAt[1], rolledAt[2], rolledAt[3]);
    ck(notThree == 0, "no charm: exactly 3 of the 4 IVs are inherited, every time");
    ck(foreign == 0, "no charm: an inherited IV is always one of the parents' own");
    ck(fromA > inherited * 45 / 100 && fromA < inherited * 55 / 100, "no charm: each parent gives about half");
    bool spread = true;
    for (int s = 0; s < 4; s++) if (rolledAt[s] < N / 5 || rolledAt[s] > N * 3 / 10) spread = false;
    ck(spread, "no charm: the rolled IV is any of the four, about a quarter each");
    PartyMon C = mon(7, 31, 5, 31, 5), D = mon(7, 5, 31, 5, 31);
    int notBest = 0;
    for (int i = 0; i < 500; i++) {
      uint8_t iv[4];
      breedInherit(C, D, true, iv);
      for (int s = 0; s < 4; s++) if (iv[s] != 31) notBest++;
    }
    ck(notBest == 0, "OVAL CHARM: all four inherited, each the better parent's (31 31 31 31)");
  }

  // ---------------------------------------------------------------- the state
  party.begin(); Pet pet; pet.begin(); bag.begin(); breedBegin();
  gameEventSubscribe(sink);
  if (pet.awaitingStarter()) pet.chooseStarter(7);
  if (pet.isEgg()) pet.dbgHatchAs(7, false);
  pet.dbgHatchAs(25, false); pet.dbgHatchAs(7, false);   // a Pokedex to keep
  for (int i = 0; i < PARTY_SLOTS; i++) party.releaseAt(i);
  for (int i = 0; i < BOX_SLOTS; i++) party.boxReleaseAt(i);
  party.add(mon(8, 10, 11, 12, 13));          // 0: WARTORTLE
  party.add(mon(BREED_DITTO, 20, 21, 22, 23)); // 1: DITTO
  party.add(mon(25, 31, 31, 31, 31, true));   // 2: PIKACHU, shiny
  party.boxAdd(mon(26, 30, 30, 30, 30, true)); // box 0 = banked 6: RAICHU, shiny
  pet.winBadge(0, 0, false);
  const uint16_t dexBefore = pet.registeredCount();
  const uint8_t partyBefore = party.count(), boxBefore = party.boxCount();
  setDay(pet, DAY);
  ck(pet.currentDay() == DAY, "the game day is the clock's day");

  ck(breedCheckIdx(0, 0) == BV_SAME, "the same banked slot twice is refused");
  ck(breedCheckIdx(2, PARTY_SLOTS + 0) == BV_OK, "a party creature + a BOX creature can breed");
  ck(breedCheckIdx(0, 9) == BV_EMPTY, "an empty box slot cannot");
  ck(breedMake(0, 2, DAY) == BR_BAD_PAIR, "breedMake refuses an incompatible pair (WARTORTLE + PIKACHU)");
  ck(!breedHasQueued(), "...and queues nothing");

  ck(breedMake(0, 1, DAY) == BR_OK, "WARTORTLE + DITTO breed");
  ck(breedHasQueued() && breedQueued().dex == 7, "the queued egg is a SQUIRTLE");
  ck(evBreeds == 1 && evLast == 7, "EV_BREED fired once, with the egg's dex");
  {
    int k = 0;
    for (int s = 0; s < 4; s++) if (breedQueued().iv[s] != BREED_IV_ROLL) k++;
    ck(k == BREED_INHERIT, "breedMake without the charm: 3 inherited, 1 left to roll");
  }
  ck(party.slots[0].dex == 8 && party.slots[1].dex == BREED_DITTO && party.count() == partyBefore &&
     party.boxCount() == boxBefore, "the parents are still banked");
  {
    Party fresh; fresh.begin();
    ck(fresh.slots[0].dex == 8 && fresh.slots[1].dex == BREED_DITTO, "...and still banked after a reload");
  }
  ck(breedMake(2, PARTY_SLOTS, DAY) == BR_QUEUE_FULL, "one queued egg at a time");
  ck(breedOnCooldown(DAY), "bred today: on cooldown");

  // ---- persists across a reload
  BredEgg q = breedQueued();
  breedBegin();   // zeroes RAM, then reads NVS
  ck(breedHasQueued() && breedQueued().dex == q.dex && !memcmp(breedQueued().iv, q.iv, 4) &&
     breedQueued().pa == 8 && breedQueued().pb == BREED_DITTO, "the queued egg survives a reload");
  ck(breedOnCooldown(DAY), "...and so does today's cooldown");

  // ---- the hatched live creature: newEgg() takes the queued egg, exactly once
  ck(!breedCanSwapIn(pet), "a HATCHED live creature cannot be swapped");
  {
    Pet scratch;                 // a Pet nobody opened: the battle's opponent
    scratch.newEgg();
    scratch.dbgHatchAs(25, false);
    ck(breedHasQueued() && breedQueued().dex == 7, "a scratch Pet's newEgg()/hatch() never spends the queue");
  }
  pet.newEgg();
  ck(pet.isEgg() && pet.eggPeek() == 7, "newEgg() makes the queued SQUIRTLE the next egg");
  ck(!breedHasQueued() && breedLive().dex == 7, "...the queue is spent and the egg is the live bred one");
  ck(pet.eggRarity() == DEX_TBL[7].rarity, "the egg's rarity reads the bred species");
  {
    uint8_t other = pet.region == 1 ? 2 : 1;
    pet.setRegion(other);
    ck(pet.eggPeek() == 7, "changing region does not re-roll a bred egg");
  }
  breedBegin();
  ck(breedLive().dex == 7, "the live bred egg survives a reload before it hatches");
  {
    Pet reborn; reborn.begin();   // the board rebooted while the egg sat there
    ck(reborn.isEgg() && reborn.eggPeek() == 7, "...and so does the egg itself");
    reborn.eggTap(); reborn.eggTap(); reborn.eggTap();
    ck(!reborn.isEgg() && reborn.speciesId == 7, "it hatches a SQUIRTLE");
    uint8_t got[4] = { reborn.ivAtk, reborn.ivDef, reborn.ivSpe, reborn.ivHp };
    bool same = true, inRange = true;
    for (int s = 0; s < 4; s++) {
      if (q.iv[s] == BREED_IV_ROLL) { if (got[s] < 8 || got[s] > 31) inRange = false; }
      else if (got[s] != expectIV(q.iv[s], reborn.shiny)) same = false;
    }
    ck(same, "hatch() applies the inherited IVs");
    ck(inRange, "...and rolls the one the parents left open");
    ck(breedLive().dex == 0, "the bred egg is consumed by its hatch");
    pet.begin();                  // the player's Pet picks the hatch back up
  }
  pet.newEgg();
  ck(pet.isEgg() && breedLive().dex == 0, "the NEXT newEgg() is an ordinary egg: consumed exactly once");

  // ---- cooldown: same day refused, next day allowed
  ck(breedMake(2, PARTY_SLOTS, DAY) == BR_COOLDOWN, "a second breed the same day is refused");
  ck(!breedHasQueued(), "...and queues nothing");
  ck(breedMake(2, PARTY_SLOTS, DAY + 1) == BR_OK, "the next day it is allowed");
  ck(breedQueued().dex == 172, "PIKACHU + RAICHU queue a PICHU");

  // ---- swap-in: the live creature is an uncracked egg
  setDay(pet, DAY + 1);
  ck(breedCanSwapIn(pet), "an uncracked egg is offered the swap");
  pet.eggTap();
  ck(!breedCanSwapIn(pet) && !breedSwapIn(pet), "a CRACKED egg is not");
  pet.newEgg();                  // an ending: newEgg() takes the queue, so re-queue below
  ck(pet.eggPeek() == 172, "(newEgg took the queued PICHU)");
  breedClearAll();
  ck(breedMake(0, 1, DAY + 2) == BR_OK, "breed again on a later day");
  int16_t before = pet.eggPeek();
  ck(breedSwapIn(pet), "swap it in now");
  ck(pet.isEgg() && pet.eggPeek() == 7 && pet.eggPeek() != before, "the live egg is the bred SQUIRTLE now");
  ck(!breedHasQueued() && breedLive().dex == 7, "the queue moved to the live egg");
  ck(pet.eggRarity() == DEX_TBL[7].rarity, "the swapped egg's rarity reads the bred species");
  {
    Pet reborn; reborn.begin();
    ck(reborn.eggPeek() == 7, "the swap is saved");
  }
  uint8_t sw[4]; memcpy(sw, breedLive().iv, 4);
  pet.eggTap(); pet.eggTap(); pet.eggTap();
  {
    uint8_t got[4] = { pet.ivAtk, pet.ivDef, pet.ivSpe, pet.ivHp };
    bool same = true;
    for (int s = 0; s < 4; s++) if (sw[s] != BREED_IV_ROLL && got[s] != expectIV(sw[s], pet.shiny)) same = false;
    ck(pet.speciesId == 7 && same, "the swapped-in egg hatches with its inherited IVs");
  }

  // ---- the Oval Charm through breedMake (the caller, not just the rule)
  breedClearAll();
  bag.add(ITEM_OVAL_CHARM);
  ck(breedMake(0, 1, DAY + 3) == BR_OK, "breed with the OVAL CHARM");
  {
    const BredEgg &e = breedQueued();
    ck(e.iv[0] == 20 && e.iv[1] == 21 && e.iv[2] == 22 && e.iv[3] == 23,
       "breedMake with the charm: all 4 inherited, the better parent's each");
  }
  bag.clearAll();

  // ---- both parents shiny: a separate roll on the queued egg
  {
    int shinies = 0, plain = 0;
    for (int i = 0; i < 3200; i++) {
      breedClearAll();
      breedMake(2, PARTY_SLOTS, DAY + 10);
      if (breedQueued().shiny) shinies++;
      breedClearAll();
      breedMake(0, 1, DAY + 10);
      if (breedQueued().shiny) plain++;
    }
    printf("      both shiny: %d / 3200 shiny bonus (1/%d expected ~%d)\n", shinies, BREED_SHINY_BONUS, 3200 / BREED_SHINY_BONUS);
    ck(shinies > 3200 / BREED_SHINY_BONUS / 2 && shinies < 3200 / BREED_SHINY_BONUS * 2, "both shiny: about 1 in 16 queued eggs win the bonus");
    ck(plain == 0, "non-shiny parents never get the bonus");
    // and the flag reaches the creature
    int tries = 0;
    do { breedClearAll(); breedMake(2, PARTY_SLOTS, DAY + 11); } while (!breedQueued().shiny && ++tries < 2000);
    ck(breedQueued().shiny, "(found a shiny-bonus egg)");
    pet.newEgg();
    pet.eggTap(); pet.eggTap(); pet.eggTap();
    ck(pet.speciesId == 172 && pet.shiny, "the bonus makes the hatched PICHU shiny");
    ck(pet.ivAtk >= 20 && pet.ivDef >= 20 && pet.ivSpe >= 20 && pet.ivHp >= 20,
       "...and the shiny IV floor of 20 still applies to inherited IVs");
  }

  // ---- no clock (day 0): one breed, then the cooldown holds
  breedClearAll();
  ck(breedMake(0, 1, 0) == BR_OK, "no clock: the first breed is allowed");
  breedClearAll();
  ck(breedMake(0, 1, 0) == BR_OK, "(a cleared daycare forgets it)");
  pet.newEgg();
  ck(breedMake(0, 1, 0) == BR_COOLDOWN, "no clock: a second breed on day 0 is refused");

  // ---- player-wide state untouched
  ck(pet.registeredCount() >= dexBefore && pet.hasBadge(0, 0, false), "breeding never takes Pokedex or badges");

  printf(bad ? "\n%d FAILED\n" : "\nall passed\n", bad);
  return bad ? 1 : 0;
}
