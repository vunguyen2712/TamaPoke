// Personalities (persona.cpp), measured through the REAL Pet methods.
//
// Each trait's effect is read off the creature after the actual care call --
// feedBerry, playResult, clean, caress, toggleLight, the trainers, tick() --
// against a no-trait twin given exactly the same treatment. So a trait that
// leaks into somebody else's care, or a care path that forgets to ask
// personaCare(), shows up as a difference where none is allowed.
//
// And the night: no trait may make sleeping (or the board being off) any
// harder to survive. CLAUDE.md "The three drain paths".
#include "Arduino.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include "persona.h"
#include <cstdio>
#include <cstring>
#include <string>
uint32_t g_seed = 61; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX = 0, g_touchY = 0; volatile bool g_touchDown = false; bool wasPressed = false;
static uint32_t g_ms = 1000; uint32_t millis() { return g_ms; }
void FakeESP::restart() { exit(0); }
int FakeSerial::available() { return 0; }
String FakeSerial::readStringUntil(char) { return String(""); }
void sfxPlay(uint8_t) {}
static int bad = 0;
static void ck(bool ok, const char *w) { printf("%s  %s\n", ok ? "PASS" : "FAIL", w); if (!ok) bad++; }

static const char *TN[PT_COUNT] = { "none", "GLUTTON", "SLEEPY", "PLAYFUL", "TIDY", "CUDDLY", "ENERGETIC", "PICKY" };
static const char *CN[PC_COUNT] = { "berry", "fav berry", "candy", "play", "train", "bath", "caress", "lights off" };

// A hatched creature in a known state with a chosen trait. No clock, so the
// first-care-of-the-day bond (registerCare) never fires and muddies a delta.
static void prep(Pet &p, uint8_t trait) {
  p.dbgHatchAs(4, false);            // CHARMANDER: loves berry 4 % 3 = 1
  p.persona = trait;
  p.frozen = false;                  // begin() may have loaded a revived one
  p.ageMinutes = 600;                // same age, so the same sleep-drain phase for every twin
  p.lastSeenEpoch = 0;
  p.sleeping = false; p.sleepAuto = SLEEP_NONE;
  p.fullness = p.joy = p.energy = p.hygiene = 50;
  p.poops = 0; p.weight = 0; p.bond = 0;
  g_ms += 100000;                    // any earlier heart / cheer has expired
}

struct Delta { int food, joy, bond; bool cheer; };

static Delta doCare(uint8_t trait, uint8_t care) {
  Pet p; p.begin(); prep(p, trait);
  int f0 = p.fullness, j0 = p.joy, b0 = p.bond;
  uint32_t seed = g_seed;
  switch (care) {
    case PC_BERRY: p.feedBerry(0); break;
    case PC_FAV_BERRY: p.feedBerry(1); break;
    case PC_CANDY: p.feedCandy(); break;
    case PC_PLAY: p.playResult(10); break;
    case PC_TRAIN: p.trainStrength(12); p.trainSpeed(6); break;
    case PC_BATH: p.poops = 2; p.clean(); break;
    case PC_CARESS: p.caress(); break;
    case PC_LIGHTS_OFF: p.toggleLight(); break;
  }
  g_seed = seed;
  return { (int)p.fullness - f0, (int)p.joy - j0, (int)p.bond - b0, personaCheering() };
}

int main() {
  // ---------------------------------------------------------------- the rule
  {
    Pet p; p.begin();
    if (p.awaitingStarter()) p.chooseStarter(4);
    prep(p, PT_NONE);
    ck(!p.lovesBerry(0) && p.lovesBerry(1), "fixture: CHARMANDER hates red, loves blue");
  }

  // Every trait x every care: the difference from a no-trait twin.
  int favCount[PT_COUNT] = { 0 };
  bool onlyOwn = true;
  for (uint8_t c = 0; c < PC_COUNT; c++) {
    Delta base = doCare(PT_NONE, c);
    if (base.cheer) { printf("      baseline cheered on %s\n", CN[c]); onlyOwn = false; }
    for (uint8_t t = 1; t < PT_COUNT; t++) {
      Delta d = doCare(t, c);
      PersonaEffect e = personaCare(t, c, 0);
      int dj = d.joy - base.joy, df = d.food - base.food, db = d.bond - base.bond;
      bool changed = dj || df || db || d.cheer;
      if (d.cheer) favCount[t]++;
      // the measured difference must be exactly what the trait claims, and zero
      // for every care the trait does not mention
      bool claims = e.fav || e.food || e.joy || e.bond;
      if (changed != claims) {
        printf("      %s on %s: measured joy%+d food%+d bond%+d cheer=%d, rule says fav=%d food=%d\n",
               TN[t], CN[c], dj, df, db, (int)d.cheer, (int)e.fav, e.food);
        onlyOwn = false;
      }
      if (changed) printf("      %-9s %-10s joy%+d food%+d bond%+d%s\n", TN[t], CN[c], dj, df, db, d.cheer ? "  <3" : "");
    }
  }
  ck(onlyOwn, "each trait changes only the care it names, and every care path asks the rule");
  bool oneFav = true;
  for (uint8_t t = 1; t < PT_COUNT; t++) if (!favCount[t]) oneFav = false;
  ck(oneFav, "every trait has a favourite care it cheers for");

  // The headline numbers, read off the real methods.
  ck(doCare(PT_GLUTTON, PC_CANDY).joy - doCare(PT_NONE, PC_CANDY).joy == 6, "GLUTTON: candy +6 joy more");
  ck(doCare(PT_GLUTTON, PC_BERRY).joy - doCare(PT_NONE, PC_BERRY).joy == 6, "GLUTTON: a plain berry +6 joy too");
  ck(doCare(PT_PLAYFUL, PC_PLAY).joy == 2 * doCare(PT_NONE, PC_PLAY).joy, "PLAYFUL: the ball game's joy, doubled");
  ck(doCare(PT_TIDY, PC_BATH).joy == 10 && doCare(PT_TIDY, PC_BATH).bond == doCare(PT_NONE, PC_BATH).bond + 2,
     "TIDY: a bath gives +10 joy and +2 bond");
  ck(doCare(PT_CUDDLY, PC_CARESS).joy == 10 && doCare(PT_CUDDLY, PC_CARESS).bond == 2,
     "CUDDLY: a caress is +10 joy and +2 bond (base +5 / +1)");
  ck(doCare(PT_SLEEPY, PC_LIGHTS_OFF).joy == 8, "SLEEPY: tucked in by hand, +8 joy");
  ck(doCare(PT_ENERGETIC, PC_TRAIN).joy - doCare(PT_NONE, PC_TRAIN).joy == 16,
     "ENERGETIC: +8 joy per training session (two sessions here)");
  ck(doCare(PT_PICKY, PC_FAV_BERRY).joy == 20, "PICKY: its favourite berry is +20 joy (base +10)");
  ck(doCare(PT_PICKY, PC_BERRY).food == 20, "PICKY: any other berry fills only +20 (base +25)");

  // ------------------------------------------------------- the awake drain
  // 12 awake minutes from a known state, one poop on screen and fullness low
  // enough that no more appear. Same seed for every trait.
  struct Drain { int food, joy, ene; };
  auto drain = [](uint8_t t, bool frozen) {
    Pet p; p.begin(); prep(p, t);
    if (frozen) {
      PartyMon m; m.dex = 6; m.level = 40; m.spare = t;
      m.ivAtk = m.ivDef = m.ivSpe = m.ivHp = 20;
      p.reviveFrom(m); p.lastSeenEpoch = 0;
    }
    p.fullness = 40; p.joy = 100; p.energy = 100; p.hygiene = 100; p.poops = 1;
    uint32_t seed = g_seed;
    for (int i = 0; i < 12; i++) p.dbgTick();
    g_seed = seed;
    return Drain{ 40 - p.fullness, 100 - p.joy, 100 - p.energy };
  };
  Drain n0 = drain(PT_NONE, false);
  printf("      no trait, 12 min: food -%d joy -%d ene -%d\n", n0.food, n0.joy, n0.ene);
  for (uint8_t t = 1; t < PT_COUNT; t++) {
    Drain d = drain(t, false);
    printf("      %-9s 12 min: food %+d joy %+d ene %+d vs none\n", TN[t], d.food - n0.food, d.joy - n0.joy, d.ene - n0.ene);
  }
  ck(drain(PT_GLUTTON, false).food == n0.food + 3, "GLUTTON: food drains 1 more every 4th minute");
  ck(drain(PT_SLEEPY, false).ene == n0.ene + 3, "SLEEPY: energy drains 1 more every 4th minute awake");
  ck(drain(PT_PLAYFUL, false).joy == n0.joy + 3, "PLAYFUL: joy drains 1 more every 4th minute");
  ck(drain(PT_TIDY, false).joy == n0.joy + 12, "TIDY: joy -1 more every minute a poop is out");
  { Drain e = drain(PT_ENERGETIC, false);
    ck(e.food == n0.food + 3 && e.ene == n0.ene - 3, "ENERGETIC: eats 1 more and tires 1 less every 4th minute"); }
  { Drain c = drain(PT_CUDDLY, false), k = drain(PT_PICKY, false);
    ck(c.food == n0.food && c.joy == n0.joy && c.ene == n0.ene &&
       k.food == n0.food && k.joy == n0.joy && k.ene == n0.ene, "CUDDLY and PICKY: the drain is untouched"); }
  // A revived companion does not age, so the nudge cannot be keyed on its age
  ck(drain(PT_GLUTTON, true).food == drain(PT_NONE, true).food + 3,
     "a frozen (revived) GLUTTON still gets exactly 1 extra per 4 minutes");

  // ----------------------------------------------------- the night: never harder
  {
    bool same = true, sleepyBetter = false;
    for (uint8_t t = 0; t < PT_COUNT; t++) {
      Pet p; p.begin(); prep(p, t);
      p.fullness = p.joy = p.energy = p.hygiene = 60;
      p.sleeping = true;
      // Checked at 30 minutes too, BEFORE the floors are reached: after ten
      // hours every trait sits on the same floors whatever it drained on the
      // way down, so the end state alone cannot see a trait draining faster.
      int eneAt5 = 0, mid[3] = { 0 };
      for (int i = 0; i < 600; i++) {
        p.dbgTick();
        if (i == 4) eneAt5 = p.energy;
        if (i == 29) { mid[0] = p.fullness; mid[1] = p.joy; mid[2] = p.hygiene; }
      }
      static int ref[4], refE5, refMid[3];
      if (t == 0) { ref[0] = p.fullness; ref[1] = p.joy; ref[2] = p.energy; ref[3] = p.hygiene; refE5 = eneAt5;
                    memcpy(refMid, mid, sizeof mid); }
      else if (p.fullness != ref[0] || p.joy != ref[1] || p.hygiene != ref[3] || p.energy < ref[2] || eneAt5 < refE5 ||
               memcmp(mid, refMid, sizeof mid)) {
        printf("      %s asleep 10 h: food=%u joy=%u ene=%u hyg=%u\n", TN[t], p.fullness, p.joy, p.energy, p.hygiene);
        same = false;
      }
      if (t == PT_SLEEPY) { sleepyBetter = eneAt5 > refE5; printf("      SLEEPY energy after 5 min asleep: %d vs %d\n", eneAt5, refE5); }
      ck(!p.canRunawayNow() && p.fullness >= 30 && p.joy >= 35 && p.hygiene >= 45, TN[t]);
    }
    ck(same, "ten hours asleep: every trait lands on the same floors, energy never lower");
    ck(sleepyBetter, "and SLEEPY really does refill faster (+8/min, not +6)");
  }
  {
    // the board switched off for 10 h asleep: syncClock asks the same answer
    bool same = true;
    int ref[4] = { 0 };
    for (uint8_t t = 0; t < PT_COUNT; t++) {
      Pet p; p.begin(); prep(p, t);
      p.fullness = p.joy = p.hygiene = 60; p.energy = 10;
      p.sleeping = true;
      p.dbgSetSeen(1000000);
      p.syncClock(1000000 + 10 * 60);          // 10 minutes off
      int e10 = p.energy, f10 = p.fullness, j10 = p.joy, h10 = p.hygiene;   // before any floor
      static int rf10, rj10, rh10;
      if (t == 0) { rf10 = f10; rj10 = j10; rh10 = h10; }
      else if (f10 != rf10 || j10 != rj10 || h10 != rh10) same = false;
      p.dbgSetSeen(2000000);
      p.syncClock(2000000 + 600 * 60);         // then 10 hours off
      if (t == 0) { ref[0] = p.fullness; ref[1] = p.joy; ref[2] = e10; ref[3] = p.hygiene; }
      else if (p.fullness != ref[0] || p.joy != ref[1] || p.hygiene != ref[3] || e10 < ref[2]) same = false;
      if (t == PT_SLEEPY) ck(e10 == ref[2] + 20, "offline asleep: SLEEPY +8/min too (10 min = +20 over the rest)");
    }
    ck(same, "offline asleep: no trait drains more than none");
  }

  // ------------------------------------------------------------ the cheer
  {
    Pet scratch;                       // never begin()'d: a battle foe
    scratch.dbgHatchAs(25, false);
    ck(scratch.persona == PT_NONE, "a scratch Pet (battle foe) hatches with no trait and spends no roll");
    scratch.persona = PT_CUDDLY;
    g_ms += 100000;
    scratch.caress();
    ck(!personaCheering(), "and a scratch Pet never cheers on the player's screen");
    Pet p; p.begin(); prep(p, PT_CUDDLY);
    p.caress();
    ck(personaCheering() && personaCheerTrait() == PT_CUDDLY, "the player's CUDDLY cheers when petted");
    g_ms += PERSONA_CHEER_MS + 1;
    ck(!personaCheering(), "for PERSONA_CHEER_MS, then it stops");
  }

  // ---------------------------------------------- rolled at hatch, persisted
  {
    int seen[PT_COUNT] = { 0 };
    for (int i = 0; i < 1400; i++) seen[personaRoll()]++;
    bool all = !seen[0];
    for (uint8_t t = 1; t < PT_COUNT; t++) if (seen[t] < 120) all = false;
    ck(all, "personaRoll() gives all seven, about evenly, and never none");

    Pet p; p.begin(); p.newEgg();
    ck(p.persona == PT_NONE, "an egg has no trait");
    p.eggTap(); p.eggTap(); p.eggTap();
    ck(!p.isEgg() && personaValid(p.persona), "hatching rolls one");
    uint8_t got = p.persona;
    { Pet q; q.begin(); ck(q.persona == got, "and it survives a reload"); }
    // a few more hatches: it is a roll, not a constant
    bool varied = false;
    for (int i = 0; i < 12; i++) { p.newEgg(); p.eggTap(); p.eggTap(); p.eggTap(); if (p.persona != got) varied = true; }
    ck(varied, "different hatches roll different traits");
  }

  // ------------------------------------------- an old save: rolled once, kept
  {
    Pet p; p.begin(); p.dbgHatchAs(7, false); p.saveNow();
    nvs().erase("prsn");                               // a v3.13 save
    Pet q; q.begin();
    ck(personaValid(q.persona), "an old save's live creature gets a trait on load");
    ck(nvs().count("prsn") == 1, "written at once, not left for the next save");
    uint8_t first = q.persona;
    bool stable = true;
    for (int i = 0; i < 10; i++) { Pet r; r.begin(); if (r.persona != first) stable = false; }
    ck(stable, "and never re-rolled on later boots");

    p.newEgg(); p.saveNow(); nvs().erase("prsn");      // an old save holding an EGG
    Pet e; e.begin();
    ck(e.persona == PT_NONE && !nvs().count("prsn"), "an old save's egg gets nothing until it hatches");

    p.dbgHatchAs(7, false); p.persona = PT_NONE; p.saveNow();   // a stored 0 is a real 'none'
    Pet z; z.begin();
    ck(z.persona == PT_NONE, "a stored 0 (a revived pre-feature creature) is NOT re-rolled");
  }

  // --------------------------------------------- banking and reviving carry it
  {
    Pet p; p.begin(); prep(p, PT_TIDY);
    p.release();
    g_ms += CEREMONY_MS + 10;
    p.update(g_ms);
    ck(p.isEgg() && p.endedKind == CER_RELEASE && p.endedMon.spare == PT_TIDY,
       "banking (snapshotForParty) carries the trait in PartyMon::spare");
    ck(p.persona == PT_NONE, "and the egg that follows has none");
    for (int i = 0; i < PARTY_SLOTS; i++) party.slots[i] = PartyMon();
    party.add(p.endedMon); party.save();
    { Party q; q.begin(); ck(q.slots[0].spare == PT_TIDY, "it survives the party's NVS round trip"); }

    p.reviveFrom(party.slots[0]);
    ck(p.persona == PT_TIDY, "reviveFrom brings it back");
    { Pet q; q.begin(); ck(q.persona == PT_TIDY, "and the revived creature saves it"); }

    PartyMon old = party.slots[0]; old.spare = 0;
    p.newEgg(); p.reviveFrom(old);
    ck(p.persona == PT_NONE, "a creature banked before personalities (spare 0) comes back with none");
    { Pet q; q.begin(); ck(q.persona == PT_NONE, "and is not given one on the next boot"); }

    // junk in spare is not trusted
    party.slots[1] = party.slots[0]; party.slots[1].spare = 200; party.save();
    { Party q; q.begin(); ck(q.slots[1].spare == 0, "an out-of-range spare byte reads as no trait"); }

    // a v1.9 record (30-byte stride, before held/spare existed) reads as none
    uint8_t blob[PARTY_SLOTS * PARTYMON_V2_SIZE] = { 0 };
    PartyMon m; m.dex = 9; m.level = 30;
    memcpy(blob, &m, PARTYMON_V2_SIZE);
    Preferences pr; pr.begin("tamapoke"); pr.putBytes("party", blob, sizeof(blob));
    { Party q; q.begin(); ck(q.slots[0].dex == 9 && q.slots[0].spare == 0, "an old 30-byte record migrates with no trait"); }
  }

  // ------------------------------------------- the README tells the truth
  // Every number the manual quotes is built here from what the REAL methods
  // just measured, so retuning a constant without the README fails.
  {
    std::string here = __FILE__;
    std::string readme = here.substr(0, here.rfind("/tools/emu/tests/")) + "/README.md";
    FILE *f = fopen(readme.c_str(), "rb");
    std::string doc;
    if (f) { char buf[4096]; size_t n; while ((n = fread(buf, 1, sizeof buf, f)) > 0) doc.append(buf, n); fclose(f); }
    ck(doc.size() > 1000, "README.md is readable");
    auto has = [&](const char *what, const std::string &s) {
      bool ok = doc.find(s) != std::string::npos;
      if (!ok) printf("      README lacks: %s\n", s.c_str());
      ck(ok, what);
    };
    char b[160];
    Delta none, d;
    none = doCare(PT_NONE, PC_CANDY); d = doCare(PT_GLUTTON, PC_CANDY);
    snprintf(b, sizeof b, "**+%d JOY** from any berry or candy", d.joy - none.joy); has("GLUTTON food joy", b);
    d = doCare(PT_SLEEPY, PC_LIGHTS_OFF);
    snprintf(b, sizeof b, "**+%d JOY** when put to bed", d.joy); has("SLEEPY bedtime joy", b);
    d = doCare(PT_TIDY, PC_BATH); none = doCare(PT_NONE, PC_BATH);
    snprintf(b, sizeof b, "**+%d JOY, +%d bond** from a bath", d.joy - none.joy, d.bond - none.bond); has("TIDY bath", b);
    d = doCare(PT_CUDDLY, PC_CARESS); none = doCare(PT_NONE, PC_CARESS);
    snprintf(b, sizeof b, "petting gives **+%d JOY, +%d bond** (normally +%d / +%d)", d.joy, d.bond, none.joy, none.bond); has("CUDDLY petting", b);
    d = doCare(PT_ENERGETIC, PC_TRAIN); none = doCare(PT_NONE, PC_TRAIN);
    snprintf(b, sizeof b, "**+%d JOY, +%d bond** per bag or reaction session", (d.joy - none.joy) / 2, (d.bond - none.bond) / 2); has("ENERGETIC training", b);
    d = doCare(PT_PICKY, PC_FAV_BERRY); none = doCare(PT_NONE, PC_FAV_BERRY);
    snprintf(b, sizeof b, "favourite berry **+%d JOY, +%d bond** in all (normally +%d / +%d)", d.joy, d.bond, none.joy, none.bond); has("PICKY favourite", b);
    d = doCare(PT_PICKY, PC_BERRY); none = doCare(PT_NONE, PC_BERRY);
    snprintf(b, sizeof b, "any other berry gives **+%d FOOD**, not +%d", d.food, none.food); has("PICKY plain berry", b);
    snprintf(b, sizeof b, "the ball game's JOY **doubled**"); has("PLAYFUL", b);
    snprintf(b, sizeof b, "every %dth minute", PERSONA_NUDGE_EVERY); has("the nudge interval", b);
    Drain g = drain(PT_GLUTTON, false);
    snprintf(b, sizeof b, "FOOD -1 more every %dth minute (-%d.%02d/min)", PERSONA_NUDGE_EVERY,
             (2 * PERSONA_NUDGE_EVERY + 1) / PERSONA_NUDGE_EVERY, 100 / PERSONA_NUDGE_EVERY);
    ck(g.food - n0.food == 12 / PERSONA_NUDGE_EVERY, "GLUTTON's measured nudge is one per interval");
    std::string want = b; size_t pos;
    while ((pos = want.find("-")) != std::string::npos) want.replace(pos, 1, "\xe2\x88\x92");  // the README's minus sign
    has("GLUTTON drain rate", want);
    snprintf(b, sizeof b, "asleep ENE **+%d/min** instead of +6", 6 + (int)(-personaDrain(PT_SLEEPY, 0, 0, true).energy)); has("SLEEPY sleep refill", b);
    snprintf(b, sizeof b, "for %lu.%lu s", PERSONA_CHEER_MS / 1000, (PERSONA_CHEER_MS % 1000) / 100); has("cheer duration", b);
  }

  printf("%s\n", bad ? "FAILURES" : "all good");
  return bad ? 1 : 0;
}
