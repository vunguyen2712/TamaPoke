// Held items, the bag, the consumption rule, the PartyMon migration and the
// Pokedex rewards -- against the REAL Bag/Party/Pet/battle code.
//
// Battle effects are checked with TWINS: two copies of the same fight run from
// the same RNG seed, one holding the item and one not. Every random() call
// lines up, so the difference between the two IS the item -- no damage figure
// restated in the test.
#include "Arduino.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include "battle.h"
#include "items.h"
#include "game_events.h"
#include "dex.h"
#include "moves.h"
#include <cstdio>
#include <cstring>
#include <vector>

uint32_t g_seed = 777; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX = 0, g_touchY = 0; volatile bool g_touchDown = false; bool wasPressed = false;
static uint32_t fakeNow = 0;
uint32_t millis() { return fakeNow; }
void FakeESP::restart() { exit(0); }
int FakeSerial::available() { return 0; }
String FakeSerial::readStringUntil(char) { return String(""); }
void sfxPlay(uint8_t) {}

static int bad = 0;
static void ck(bool ok, const char *w) { printf("%s  %s\n", ok ? "PASS" : "FAIL", w); if (!ok) bad++; }

static int evItems = 0, evLast = 0;
static void sink(GameEv ev, int16_t arg) { if (ev == EV_ITEM_USED) { evItems++; evLast = arg; } }

static uint8_t findMove(const char *name) {
  for (uint8_t i = 1; i < MOVE_COUNT; i++) if (!strcmp(MOVE_TBL[i].name, name)) return i;
  return 0;
}
// A plain creature with hand-set stats, so nothing about a species leaks in.
static Combatant mk(int16_t dex, uint16_t hp, uint16_t atk, uint16_t def) {
  Combatant c;
  c.dex = dex; c.level = 50; c.maxHp = c.hp = hp;
  for (int i = 0; i < SI_COUNT; i++) c.base[i] = def;
  c.base[SI_ATK] = c.base[SI_SPA] = atk;
  c.base[SI_SPE] = 50;
  snprintf(c.name, sizeof(c.name), "T%d", dex);
  return c;
}
// Runs atk->def from `seed` on a copy of def that holds `item` and one that
// does not. Returns both defenders after the action.
static void twin(const Combatant &atk, const Combatant &def, uint8_t mv, uint8_t item,
                 uint32_t seed, Combatant &without, Combatant &with, TurnLog &lgWith) {
  Combatant a1 = atk, a2 = atk;
  without = def; with = def; with.held = item;
  TurnLog lg;
  g_seed = seed; battleAct(a1, without, mv, lg);
  g_seed = seed; battleAct(a2, with, mv, lgWith);
}

static PartyMon pm(int16_t dex, uint16_t lvl, uint8_t held = 0) {
  PartyMon m; m.dex = dex; m.level = lvl; m.ivAtk = m.ivDef = m.ivSpe = m.ivHp = 20;
  snprintf(m.nick, sizeof(m.nick), "M%d", dex); m.held = held; return m;
}

int main() {
  gameEventSubscribe(sink);
  { Preferences w; w.begin("tamapoke", false); w.clear(); w.end(); }

  // ================= the bag
  bag.begin();
  ck(bag.add(ITEM_ORAN_BERRY, 120) == BAG_MAX && bag.count(ITEM_ORAN_BERRY) == BAG_MAX,
     "an item stacks to BAG_MAX and no further");
  ck(bag.add(ITEM_ORAN_BERRY) == 0, "a full stack takes nothing more");
  ck(bag.add(ITEM_SHINY_CHARM, 5) == 1 && bag.count(ITEM_SHINY_CHARM) == 1, "a key item is owned once");
  ck(bag.take(ITEM_SHINY_CHARM) && bag.count(ITEM_SHINY_CHARM) == 1, "and is never spent");
  ck(!itemIsHeld(ITEM_SHINY_CHARM) && !itemIsHeld(ITEM_POKEBALL) && itemIsHeld(ITEM_LEFTOVERS),
     "only the IK_HELD items can be held");
  ck(bag.add(ITEM_COUNT) == 0 && bag.add(0) == 0, "an unknown id is refused");
  bag.clearAll();

  // ================= give / take
  Pet pet; Party pty;
  pet.begin(); pty.begin();
  pet.dbgHatchAs(25, false);
  pty.slots[1] = pm(6, 50); pty.save();
  pty.box[4] = pm(9, 50); pty.boxSave();
  ck(!heldGive(pet, pty, HOLDER_PET, ITEM_LEFTOVERS), "nothing to give from an empty bag");
  bag.add(ITEM_LEFTOVERS, 2); bag.add(ITEM_ORAN_BERRY, 1); bag.add(ITEM_POKEBALL, 3);
  ck(!heldGive(pet, pty, HOLDER_PET, ITEM_POKEBALL), "a ball cannot be held");
  ck(!heldGive(pet, pty, HOLDER_PARTY(0), ITEM_LEFTOVERS), "an empty party slot holds nothing");
  ck(heldGive(pet, pty, HOLDER_PET, ITEM_LEFTOVERS) && pet.held == ITEM_LEFTOVERS &&
     bag.count(ITEM_LEFTOVERS) == 1, "the live pet takes LEFTOVERS out of the bag");
  ck(heldGive(pet, pty, HOLDER_PET, ITEM_ORAN_BERRY) && pet.held == ITEM_ORAN_BERRY &&
     bag.count(ITEM_LEFTOVERS) == 2 && bag.count(ITEM_ORAN_BERRY) == 0,
     "giving another item swaps: the old one goes back to the bag");
  ck(heldGive(pet, pty, HOLDER_PARTY(1), ITEM_LEFTOVERS) && pty.slots[1].held == ITEM_LEFTOVERS,
     "a party member holds one");
  ck(heldGive(pet, pty, HOLDER_BOX(4), ITEM_LEFTOVERS) && pty.box[4].held == ITEM_LEFTOVERS &&
     bag.count(ITEM_LEFTOVERS) == 0, "and so does a box member");
  {
    Pet p2; Party q2; p2.begin(); q2.begin(); Bag b2; b2.begin();
    ck(p2.held == ITEM_ORAN_BERRY && q2.slots[1].held == ITEM_LEFTOVERS && q2.box[4].held == ITEM_LEFTOVERS,
       "all three survive a reload");
    ck(b2.count(ITEM_LEFTOVERS) == 0 && b2.count(ITEM_ORAN_BERRY) == 0, "and so does the bag that paid for them");
  }
  ck(heldTake(pet, pty, HOLDER_BOX(4)) && pty.box[4].held == 0 && bag.count(ITEM_LEFTOVERS) == 1,
     "taking it back returns it to the bag");
  ck(!heldTake(pet, pty, HOLDER_BOX(4)), "taking from a creature holding nothing does nothing");

  // travels with the creature: party <-> box
  pty.swapPartyBox(1, 7);
  ck(pty.box[7].held == ITEM_LEFTOVERS && pty.slots[1].empty(), "a deposit carries the item into the box");
  pty.swapPartyBox(1, 7);
  ck(pty.slots[1].held == ITEM_LEFTOVERS, "and a withdraw carries it back");

  // ================= battle effects
  const uint8_t TACKLE = findMove("TACKLE");
  ck(TACKLE != 0, "found a plain damaging move to test with");
  evItems = 0;
  {  // SITRUS: heals a quarter once at <= half
    Combatant a = mk(19, 100, 120, 60), d = mk(19, 200, 60, 60);
    bool seen = false;
    for (uint32_t s = 1; s < 400 && !seen; s++) {
      Combatant wo, wi; TurnLog lg;
      d.hp = 120;
      twin(a, d, TACKLE, ITEM_SITRUS_BERRY, s, wo, wi, lg);
      if (wo.fainted() || wo.hp * 2 > wo.maxHp) continue;
      seen = true;
      ck(wi.hp == wo.hp + 50 && wi.itemUsed && lg.defItem == ITEM_SITRUS_BERRY,
         "SITRUS BERRY: at half HP or less it heals 25% (twin fight)");
      Combatant a2 = a; TurnLog l2; uint16_t before = wi.hp;
      battleEndTurn(wi, l2);
      ck(wi.hp == before && !l2.atkItem, "and only once");
    }
    ck(seen, "the SITRUS case was reached");
    Combatant wo, wi; TurnLog lg; d.hp = 200;
    twin(a, d, TACKLE, ITEM_SITRUS_BERRY, 5, wo, wi, lg);
    ck(wo.hp * 2 > wo.maxHp && wi.hp == wo.hp && !wi.itemUsed, "above half it waits");
  }
  {  // ORAN: flat heal, via the end of turn too
    Combatant c = mk(19, 100, 50, 50); c.held = ITEM_ORAN_BERRY; c.hp = 50;
    TurnLog lg; battleEndTurn(c, lg);
    ck(c.hp == 50 + ITEM_TBL[ITEM_ORAN_BERRY].power && lg.atkItem == ITEM_ORAN_BERRY,
       "ORAN BERRY: heals its flat amount at exactly half");
    c.hp = 51; c.itemUsed = false; battleEndTurn(c, lg);
    ck(c.hp == 51 && !c.itemUsed, "and not one HP above half");
  }
  {  // LUM: cures, once
    Combatant c = mk(19, 160, 50, 50); c.held = ITEM_LUM_BERRY; c.ailment = AIL_BURN;
    TurnLog lg; battleEndTurn(c, lg);
    ck(c.ailment == AIL_NONE && c.itemUsed && lg.atkItem == ITEM_LUM_BERRY && c.hp == 150,
       "LUM BERRY: the burn chips once, then it is cured");
    c.ailment = AIL_POISON; battleEndTurn(c, lg);
    ck(c.ailment == AIL_POISON, "a second ailment is not cured");
    Combatant d = mk(19, 160, 50, 50); d.held = ITEM_LUM_BERRY; d.confuseTurns = 3;
    battleEndTurn(d, lg);
    ck(d.confuseTurns == 0 && d.itemUsed, "it cures confusion too");
  }
  {  // LUM in battleAct: a move that inflicts a status
    uint8_t mv = 0, best = 0;
    for (uint8_t i = 1; i < MOVE_COUNT; i++)
      if (MOVE_TBL[i].ailment != AIL_NONE && MOVE_TBL[i].ailment != AIL_CONFUSE &&
          MOVE_TBL[i].cat != MC_STATUS && MOVE_TBL[i].ailChance > best) { best = MOVE_TBL[i].ailChance; mv = i; }
    Combatant a = mk(19, 300, 40, 300), d = mk(19, 300, 40, 300);
    d.dex = 19;   // RATTATA: normal, immune to nothing in the ailment list
    bool seen = false;
    for (uint32_t s = 1; s < 2000 && !seen; s++) {
      Combatant wo, wi; TurnLog lg;
      twin(a, d, mv, ITEM_LUM_BERRY, s, wo, wi, lg);
      if (wo.ailment == AIL_NONE) continue;
      seen = true;
      ck(wi.ailment == AIL_NONE && lg.defItem == ITEM_LUM_BERRY, "LUM BERRY cures a status the moment it lands");
    }
    ck(seen, "the LUM-in-battle case was reached");
  }
  {  // FOCUS SASH
    Combatant a = mk(19, 100, 600, 60), d = mk(19, 60, 30, 20);
    Combatant wo, wi; TurnLog lg;
    twin(a, d, TACKLE, ITEM_FOCUS_SASH, 9, wo, wi, lg);
    ck(wo.fainted(), "(without it, the hit knocks it out)");
    ck(wi.hp == 1 && wi.itemUsed && lg.defItem == ITEM_FOCUS_SASH && !lg.targetFainted,
       "FOCUS SASH: a knockout blow from full HP leaves 1 HP");
    Combatant a2 = a; battleAct(a2, wi, TACKLE, lg);
    ck(wi.fainted(), "and it does not save it twice");
    Combatant e = d; e.held = ITEM_FOCUS_SASH; e.hp = e.maxHp - 1;
    Combatant a3 = a; battleAct(a3, e, TACKLE, lg);
    ck(e.fainted() && !e.itemUsed, "it does nothing below full HP");
  }
  {  // LEFTOVERS
    Combatant c = mk(19, 160, 50, 50); c.held = ITEM_LEFTOVERS; c.hp = 100;
    TurnLog lg; battleEndTurn(c, lg);
    ck(c.hp == 110 && lg.atkItem == ITEM_LEFTOVERS, "LEFTOVERS: 1/16 of max HP at the end of a turn");
    battleEndTurn(c, lg);
    ck(c.hp == 120, "every turn: it is never used up");
    c.hp = c.maxHp; battleEndTurn(c, lg);
    ck(!lg.atkItem, "and says nothing at full HP");
  }
  {  // QUICK CLAW
    Combatant slow = mk(19, 100, 50, 50), fast = mk(19, 100, 50, 50);
    slow.base[SI_SPE] = 10; fast.base[SI_SPE] = 200;
    int first = 0, claws = 0;
    for (int i = 0; i < 2000; i++) { uint8_t cl; if (battleMovesFirst(slow, TACKLE, fast, TACKLE, &cl)) first++; if (cl == 1) claws++; }
    ck(first == 0, "(the slow one never moves first without the claw)");
    slow.held = ITEM_QUICK_CLAW;
    first = 0; claws = 0;
    for (int i = 0; i < 2000; i++) { uint8_t cl; if (battleMovesFirst(slow, TACKLE, fast, TACKLE, &cl)) first++; if (cl == 1) claws++; }
    printf("      QUICK CLAW: first %d / 2000\n", first);
    ck(first > 300 && first < 500 && claws == first, "QUICK CLAW: about 20% of the time it moves first");
    uint8_t prio = 0;
    for (uint8_t i = 1; i < MOVE_COUNT; i++) if (MOVE_TBL[i].effect == EF_PRIORITY && MOVE_TBL[i].param > 0) prio = i;
    first = 0;
    for (int i = 0; i < 500 && prio; i++) if (battleMovesFirst(slow, TACKLE, fast, prio, nullptr)) first++;
    ck(prio && first == 0, "but never over a priority move");
  }
  ck(evItems > 0 && evLast != 0, "every trigger emits EV_ITEM_USED");
  {
    Combatant c = mk(19, 160, 50, 50); c.held = ITEM_LEFTOVERS; c.hp = 100;
    int before = evItems; TurnLog lg; battleEndTurn(c, lg);
    ck(evItems == before + 1 && evLast == ITEM_LEFTOVERS, "with the item's id");
  }
  {  // combatants carry the item from both sources
    Combatant c; combatantFromPet(c, pet);
    ck(c.held == ITEM_ORAN_BERRY, "combatantFromPet copies the live pet's item");
    combatantFromParty(c, pty.slots[1]);
    ck(c.held == ITEM_LEFTOVERS, "combatantFromParty copies a banked one's");
  }

  // ================= the consumption rule
  {
    Combatant c; combatantFromParty(c, pty.slots[1]);
    c.holder = HOLDER_PARTY(1); c.itemUsed = true;
    heldBattleSync(pet, pty, c);
    ck(pty.slots[1].held == ITEM_LEFTOVERS, "LEFTOVERS are never consumed");
    pty.slots[1].held = ITEM_SITRUS_BERRY; pty.save();
    combatantFromParty(c, pty.slots[1]); c.holder = HOLDER_PARTY(1);
    heldBattleSync(pet, pty, c);
    ck(pty.slots[1].held == ITEM_SITRUS_BERRY, "an unused berry stays");
    c.itemUsed = true;
    heldBattleSync(pet, pty, c);
    ck(pty.slots[1].held == 0, "a berry that fired is gone from the banked creature");
    Party q; q.begin();
    ck(q.slots[1].held == 0, "and that is saved, not just in RAM");

    Combatant p; combatantFromPet(p, pet); p.holder = HOLDER_PET; p.itemUsed = true;
    heldBattleSync(pet, pty, p);
    ck(pet.held == 0, "a berry the live pet used is gone from it");
    Pet p2; p2.begin();
    ck(p2.held == 0, "saved too");

    pty.slots[1].held = ITEM_FOCUS_SASH; pty.save();
    Combatant f; combatantFromParty(f, pty.slots[1]); f.itemUsed = true;   // holder -1: a foe
    heldBattleSync(pet, pty, f);
    ck(pty.slots[1].held == ITEM_FOCUS_SASH, "a combatant with no holder touches no stored creature");
  }

  // ================= endings: the item never lands on the next egg
  {
    bag.clearAll();
    // the REAL ending path: update() snapshots, then newEgg()
    auto finish = [&]() { fakeNow += CEREMONY_MS + 1000; pet.update(fakeNow); };
    pet.held = ITEM_LEFTOVERS; pet.release();
    finish();
    ck(pet.endedMon.held == ITEM_LEFTOVERS && pet.endedKind == CER_RELEASE && pet.isEgg() && pet.held == 0,
       "a released (banked) creature takes its item along");
    ck(bag.count(ITEM_LEFTOVERS) == 0, "rather than dropping it in the bag");
    pet.dbgHatchAs(25, false);
    ck(pet.held == 0, "and the next creature starts empty-handed");
    PartyMon back = pet.endedMon;
    pet.reviveFrom(back);
    ck(pet.held == ITEM_LEFTOVERS, "bringing it back brings its item");
    pet.frozen = false;
    pet.startRunaway();
    finish();
    ck(pet.isEgg() && pet.held == 0 && bag.count(ITEM_LEFTOVERS) == 1,
       "a creature that runs away leaves its item in the bag");
    Pet chk; chk.begin();
    ck(chk.held == 0, "and the egg after it holds nothing on flash either");
    uint8_t h = ITEM_SITRUS_BERRY; heldReturn(h);
    ck(h == 0 && bag.count(ITEM_SITRUS_BERRY) == 1, "heldReturn: a creature let go returns its item");
  }

  // ================= PartyMon migration: old 30-byte records, party AND box
  {
    ck(sizeof(PartyMon) == 32 && offsetof(PartyMon, held) == PARTYMON_V2_SIZE,
       "the record grew at the END, to 32 bytes");
    const size_t S = PARTYMON_V2_SIZE;
    std::vector<uint8_t> party(PARTY_SLOTS * S, 0), box(BOX_SLOTS * S, 0);
    for (int i = 0; i < PARTY_SLOTS; i++) {
      PartyMon m = pm(10 + i, 20 + i); m.moves[0] = 5; m.moves[3] = 9;
      memcpy(&party[i * S], &m, S);
    }
    for (int i = 0; i < BOX_SLOTS; i++) {
      PartyMon m = pm(100 + i, 40 + i); m.moves[1] = 7;
      memcpy(&box[i * S], &m, S);
    }
    Preferences w; w.begin("tamapoke", false);
    w.putBytes("party", party.data(), party.size());
    w.putBytes("box", box.data(), box.size());
    w.end();
    Party q; q.begin();
    bool okP = true, okB = true;
    for (int i = 0; i < PARTY_SLOTS; i++) {
      const PartyMon &m = q.slots[i];
      char nk[12]; snprintf(nk, sizeof(nk), "M%d", 10 + i);
      if (m.dex != 10 + i || m.level != 20 + i || m.moves[0] != 5 || m.moves[3] != 9 || strcmp(m.nick, nk) || m.held) okP = false;
    }
    for (int i = 0; i < BOX_SLOTS; i++) {
      const PartyMon &m = q.box[i];
      if (m.dex != 100 + i || m.level != 40 + i || m.moves[1] != 7 || m.held) okB = false;
    }
    ck(okP, "a pre-held PARTY blob keeps every record aligned, holding nothing");
    ck(okB, "and so does a pre-held BOX blob -- all 18 records");
    Preferences r; r.begin("tamapoke", true);
    ck(r.getBytesLength("party") == sizeof(q.slots) && r.getBytesLength("box") == sizeof(q.box),
       "both are rewritten in the new layout, once");
    r.end();
    Party q2; q2.begin();
    ck(q2.box[17].dex == 117 && q2.slots[5].dex == 15, "and read back the same afterwards");

    // a held byte off flash that is not a holdable item is dropped
    q2.slots[0].held = ITEM_SHINY_CHARM; q2.save();
    Party q3; q3.begin();
    ck(q3.slots[0].held == 0 && q3.slots[0].dex == 10, "a stored key item as 'held' is cleaned, the creature kept");
  }

  // ================= Pokedex rewards
  {
    Preferences w; w.begin("tamapoke", false); w.clear(); w.end();
    bag.begin(); bag.clearAll();
    gRegionArt = 0xFFFF;
    Pet p; p.begin();
    dexRewardsReset();
    dexRewardsBegin(&p);
    ck(dexGiftsGranted() == 0 && !bag.has(ITEM_OVAL_CHARM), "a fresh save is owed nothing");
    int16_t d = 1;
    while (p.registeredCount() < 24) p.dbgHatchAs(d++, false);
    ck(dexGiftsGranted() == 0, "24 registrations: no gift yet");
    p.dbgHatchAs(d++, false);
    uint8_t q; uint8_t g0 = dexGiftItem(0, &q);
    ck(dexGiftsGranted() == 1 && bag.count(g0) == q, "the 25th registration pays the first gift (via EV_DEX_REGISTER)");
    // reboot: the grant is remembered
    dexRewardsReset();
    dexRewardsBegin(&p);
    ck(dexGiftsGranted() == 1 && bag.count(g0) == q, "after a reboot it is not paid again");
    while (p.registeredCount() < 99) p.dbgHatchAs(d++, false);
    ck(!bag.has(ITEM_OVAL_CHARM) && dexGiftsGranted() == 3, "99: three gifts, no OVAL CHARM");
    p.dbgHatchAs(d++, false);
    ck(bag.has(ITEM_OVAL_CHARM) && dexGiftsGranted() == 4 && (dexRewardFlags() & DXRW_OVAL),
       "100 registrations: the OVAL CHARM and the fourth gift");

    // region completion, on a Kanto-only card
    gRegionArt = 1u << 0;
    uint16_t need = regionObtainableCount(0);
    printf("      Kanto obtainable with only its pack: %u\n", need);
    ck(need >= 130 && need <= 151, "Kanto's obtainable dex is most of it");
    ck(!dexObtainable(25) && !dexObtainable(113),
       "PIKACHU (from Johto's PICHU) and CHANSEY (from Sinnoh's HAPPINY) are not required on a Kanto-only card");
    gRegionArt = 0xFFFF;
    ck(dexObtainable(25) && dexObtainable(113) && dexObtainable(242), "but they are once those packs are present");
    gRegionArt = 1u << 0;
    ck(regionObtainableCount(1) == 0, "a region whose pack is missing has nothing obtainable");
    ck(!regionDexComplete(p, 1), "and so can never count as complete");
    ck(!(dexRewardFlags() & DXRW_SHINY), "no SHINY CHARM yet");
    int16_t last = 0;
    for (int16_t k = 1; k <= 151; k++) if (dexObtainable(k) && !p.isRegistered(k)) last = k;
    for (int16_t k = 1; k <= 151; k++)
      if (dexObtainable(k) && !p.isRegistered(k) && k != last) {
        p.dexReg[(k - 1) >> 3] |= 1 << ((k - 1) & 7);
      }
    dexRewardsCheck(p);
    ck(!regionDexComplete(p, 0) && !bag.has(ITEM_SHINY_CHARM), "one obtainable species short: no charm");
    p.dbgHatchAs(last, false);
    ck(regionDexComplete(p, 0) && bag.has(ITEM_SHINY_CHARM) && (dexRewardFlags() & DXRW_SHINY),
       "the last one completes Kanto and grants the SHINY CHARM");
    gRegionArt = 0xFFFF;
    // the charm in an egg roll
    ck(shinyCharmOdds(48) == 24 && shinyCharmOdds(8) == 4, "the charm halves the egg's shiny odds, floor 4");
    bag.clearAll();
    ck(shinyCharmOdds(48) == 48, "and without it nothing changes");
    Preferences r; r.begin("tamapoke", true);
    printf("      dxrw=%u dxgf=%u\n", r.getUChar("dxrw", 0), r.getUChar("dxgf", 0));
    ck(r.getUChar("dxrw", 0) == (DXRW_SHINY | DXRW_OVAL) && r.getUChar("dxgf", 0) == dexGiftsGranted() && dexGiftsGranted() == p.registeredCount() / DEX_GIFT_EVERY,
       "what was granted is on flash");
    r.end();
  }

  printf("%s\n", bad ? "FAILURES" : "all good");
  return bad ? 1 : 0;
}
