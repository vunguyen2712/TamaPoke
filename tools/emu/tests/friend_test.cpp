// Friendship evolution: the bond gate, EEVEE by day and by night, the region/
// art filter on the target -- and that the CARD agrees with canEvolveNow().
//
// That last part drives the card's own function (cardEvoLine(), which
// renderCardProgress() prints) rather than evoCheck() alone: the evolution
// threshold has already shipped once with the card holding its own copy of the
// rule (CLAUDE.md trap 1), and a test of the rule alone would not have caught it.
// So this links the sketch.
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include "items.h"
#include "dex.h"
#include "i18n.h"
#include <cstdio>
#include <cstring>
uint32_t g_seed = 99; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX = 0, g_touchY = 0; volatile bool g_touchDown = false;
void FakeESP::restart() { exit(0); }
int FakeSerial::available() { return 0; }
String FakeSerial::readStringUntil(char) { return String(""); }
void setup(); void render();
extern Pet pet;
uint8_t cardEvoLine(char *out, size_t n, uint16_t *col);

static int bad = 0;
static void ck(bool ok, const char *w) { printf("%s  %s\n", ok ? "PASS" : "FAIL", w); if (!ok) bad++; }

// A creature of `dex` at `lvl`, cared for, awake, with `bond`.
static void ready(Pet &p, int16_t dex, uint8_t lvl, uint8_t bond) {
  p.dbgHatchAs(dex, false);
  p.ageMinutes = (uint32_t)(lvl - 1) * MINUTES_PER_LEVEL;
  p.fullness = p.joy = p.energy = p.hygiene = 100;
  p.sleeping = false; p.frozen = false; p.careMistakes = 0;
  p.bond = bond;
}

static const int16_t FRIENDS[] = { 42, 113, 172, 173, 174, 175, 298, 406, 427, 433, 440, 446, 447, 527, 541, 772 };

int main() {
  setup();
  for (int i = 0; i < 3; i++) render();
  if (pet.awaitingStarter()) pet.chooseStarter(4);
  gRegionArt = 0xFFFF;

  // ---- every friendship species: no evolution without the bond, then with it
  {
    bool gated = true, opens = true, lands = true, isFriend = true;
    for (int16_t d : FRIENDS) {
      if (!isFriendEvo(d)) isFriend = false;
      Pet p;
      ready(p, d, 100, FRIEND_BOND - 1);
      EvoCheck e = evoCheck(p);
      if (p.canEvolveNow() || e.block != EVO_BOND) { gated = false; printf("      %s evolves at bond %u\n", DEX_TBL[d].name, p.bond); }
      p.bond = FRIEND_BOND;
      if (!p.canEvolveNow()) { opens = false; printf("      %s refuses at bond %u (block %u)\n", DEX_TBL[d].name, p.bond, evoCheck(p).block); }
      p.evolve();
      if (p.speciesId != (int16_t)DEX_TBL[d].evolvesTo) lands = false;
    }
    ck(isFriend, "all sixteen are on the friendship list");
    ck(gated, "a friendship species does NOT evolve one point below FRIEND_BOND");
    ck(opens, "and does at FRIEND_BOND");
    ck(lands, "into its table target");
  }
  // ---- the level proxy and the care floor still apply on top of the bond
  {
    Pet p; ready(p, 172, 2, 100);
    ck(!p.canEvolveNow() && evoCheck(p).block == EVO_LEVEL, "a bonded PICHU still waits for its level");
    ready(p, 172, 100, 100); p.hygiene = 10;
    ck(!p.canEvolveNow() && evoCheck(p).block == EVO_CARE, "and for the care floor");
    ready(p, 172, 100, 100); p.careMistakes = 90;
    ck(!p.canEvolveNow(), "and care mistakes still push the level back");
  }
  // ---- nothing else changed: a non-friendship species ignores the bond
  {
    Pet p; ready(p, 4, 100, 0);
    ck(p.canEvolveNow() && !evoCheck(p).friendship, "CHARMANDER evolves with no bond at all, as before");
    p.evolve();
    ck(p.speciesId == 5, "into CHARMELEON");
  }
  // ---- the target must be drawable on this card
  {
    Pet p; ready(p, 406, 100, 100);          // BUDEW (Sinnoh) -> ROSELIA (Hoenn)
    gRegionArt = 0xFFFF & ~(1u << 2);        // no Hoenn pack
    ck(!p.canEvolveNow() && evoCheck(p).block == EVO_NO_ART, "BUDEW does not evolve into ROSELIA without Hoenn's pack");
    p.evolve();
    ck(p.speciesId == 406, "and evolve() refuses too");
    gRegionArt = 0xFFFF;
    ck(p.canEvolveNow(), "with the pack it does (so the refusal above is the filter)");
  }
  // ---- EEVEE
  {
    const uint32_t NOON = 1700000000UL - (1700000000UL % 86400) + 12 * 3600;
    const uint32_t NIGHT = NOON + 10 * 3600;      // 22:00
    Pet p;
    ready(p, DEX_EEVEE, 100, 100); p.lastSeenEpoch = NOON;
    ck(!friendIsNight(p), "(noon is day)");
    p.evolve();
    ck(p.speciesId == DEX_ESPEON, "a bonded EEVEE at noon becomes ESPEON");
    ready(p, DEX_EEVEE, 100, 100); p.lastSeenEpoch = NIGHT;
    ck(friendIsNight(p), "(22:00 is night)");
    p.evolve();
    ck(p.speciesId == DEX_UMBREON, "a bonded EEVEE at 22:00 becomes UMBREON");
    bool never = true;
    for (int i = 0; i < 60; i++) {
      ready(p, DEX_EEVEE, 100, FRIEND_BOND - 1); p.lastSeenEpoch = (i & 1) ? NIGHT : NOON;
      p.evolve();
      if (p.speciesId == DEX_ESPEON || p.speciesId == DEX_UMBREON) never = false;
    }
    ck(never, "an unbonded EEVEE never takes the two friendship branches (60 tries)");
    // the filter: no Johto pack -> a bonded noon Eevee cannot become ESPEON
    gRegionArt = 0xFFFF & ~(1u << 1);
    ready(p, DEX_EEVEE, 100, 100); p.lastSeenEpoch = NOON;
    ck(p.canEvolveNow(), "without Johto's pack a bonded EEVEE still evolves");
    p.evolve();
    ck(p.speciesId != DEX_ESPEON && p.speciesId != DEX_UMBREON && p.speciesId != DEX_EEVEE,
       "but into a level branch it can draw, not ESPEON");
    gRegionArt = 0xFFFF;
  }

  // ---- THE CALLER: the card says READY exactly when canEvolveNow() is true
  {
    int agree = 0, total = 0, sawBond = 0, sawReady = 0;
    char line[40]; uint16_t col;
    const int16_t SPECIES[] = { 4, 133, 172, 42, 113, 440, 406, 25, 10, 772, 150 };
    for (int16_t d : SPECIES)
      for (uint8_t lvl : { 2, 20, 28, 40, 100 })
        for (uint8_t bond : { 0, 49, 50, 100 })
          for (int care = 0; care < 2; care++) {
            ready(pet, d, lvl, bond);
            if (!care) pet.joy = 20;
            uint8_t code = cardEvoLine(line, sizeof(line), &col);
            bool cardReady = !strcmp(line, T(S_EVO_READY));
            total++;
            if (cardReady == pet.canEvolveNow() && (code == EVO_OK) == pet.canEvolveNow()) agree++;
            else printf("      %s L%u bond %u care %d: card '%s', canEvolveNow %d\n",
                        DEX_TBL[d].name, lvl, bond, care, line, pet.canEvolveNow());
            if (code == EVO_BOND) { sawBond++; if (!strstr(line, "/50")) agree--; }
            if (cardReady) sawReady++;
          }
    printf("      %d/%d states agree (%d showed the bond, %d READY)\n", agree, total, sawBond, sawReady);
    ck(agree == total, "the card's evolution line never disagrees with canEvolveNow()");
    ck(sawBond > 0 && sawReady > 0, "and the sweep really saw both a bond wait and a READY");
  }

  printf("%s\n", bad ? "FAILURES" : "all good");
  return bad ? 1 : 0;
}
