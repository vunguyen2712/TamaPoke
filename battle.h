#pragma once
#include <Arduino.h>
#include "pet.h"
#include "party.h"
#include "moves.h"

// Turn resolution. Deliberately free of any UI or hardware so the whole thing
// can be simulated headlessly -- see tools/emu.
//
// Integer maths only, like types.h: no floats anywhere on the MCU.

enum : uint8_t { SI_ATK = 0, SI_DEF, SI_SPA, SI_SPD, SI_SPE, SI_COUNT };

// A creature as it exists inside a battle. Built from the live pet or from a
// banked party member; nothing here is ever written back, which is what keeps
// ailments battle-only.
struct Combatant {
  int16_t dex = 0;
  uint8_t level = 1;
  uint16_t maxHp = 1, hp = 1;
  uint16_t base[SI_COUNT] = { 1, 1, 1, 1, 1 };  // before stat stages
  uint8_t moves[MOVE_SLOTS] = { 0, 0, 0, 0 };
  int8_t stage[SI_COUNT] = { 0, 0, 0, 0, 0 };   // -6..+6
  uint8_t ailment = AIL_NONE;
  uint8_t ailTurns = 0;      // sleep/freeze countdown
  uint8_t confuseTurns = 0;  // confusion runs alongside a real ailment
  bool recharge = false;     // EF_RECHARGE spent this creature's next turn
  uint8_t charging = 0;      // EF_CHARGE move already wound up
  bool shiny = false;        // which sprite variant to stream
  char name[12] = "";
  // Held item (items.h). `itemUsed` makes every once-per-battle item fire only
  // once, and stays with this copy through switches. `holder` names the stored
  // creature it was built from (HOLDER_PET / HOLDER_PARTY(i)), so a spent
  // berry can be removed from THAT creature; -1 for foes and link fights.
  uint8_t held = 0;
  bool itemUsed = false;
  int8_t holder = -1;

  bool fainted() const { return hp == 0; }
};

void combatantFromPet(Combatant &c, const Pet &p);
void combatantFromParty(Combatant &c, const PartyMon &m);

// What one action did, so the UI can narrate it without recomputing anything.
struct TurnLog {
  uint8_t move = 0;
  uint16_t damage = 0;
  uint16_t effPct = 100;   // type effectiveness, percent
  uint8_t hits = 0;        // >1 for EF_MULTI
  uint8_t inflicted = AIL_NONE;
  int8_t stageDelta = 0;   // for EF_STAGE, so the UI can say "rose sharply"
  uint8_t stageMask = 0;
  bool missed = false;
  bool crit = false;
  bool immune = false;     // type chart says it does nothing
  bool skipped = false;    // asleep, frozen, fully paralysed or recharging
  bool hurtSelf = false;   // confusion
  bool charged = false;    // spent the turn winding up
  bool healed = false;
  bool targetFainted = false;
  // A held item that triggered during this action, on either side, so the
  // UI can narrate it. In battleEndTurn() it is the creature's own (atkItem).
  uint8_t atkItem = 0;
  uint8_t defItem = 0;
};

uint16_t stagedStat(uint16_t base, int8_t stage);
uint16_t battleDamage(const Combatant &atk, const Combatant &def, uint8_t mv,
                      bool crit, uint8_t roll);

// True if `a` using `ma` acts before `b` using `mb`. Priority first, then the
// staged speed, and paralysis halves it.
// A held QUICK CLAW gets a 20% roll to jump the speed order (never priority).
// `claw`, if given, reports which side's claw fired: 1 = a, 2 = b, 0 = none.
bool battleMovesFirst(const Combatant &a, uint8_t ma,
                      const Combatant &b, uint8_t mb, uint8_t *claw = nullptr);

// One creature's action. Applies damage, ailments, stat stages and recoil.
void battleAct(Combatant &atk, Combatant &def, uint8_t mv, TurnLog &log);

// Burn and poison chip damage, applied to one creature after both have acted,
// then LEFTOVERS (and a berry, if the chip took it to half).
void battleEndTurn(Combatant &c, TurnLog &log);

// Picks the attacker's move. `smart` is what separates hard mode from easy:
// easy picks uniformly at random, hard reads the type chart, accuracy, whether
// a move kills this turn, and whether a status or a stat boost is worth the
// turn it costs.
uint8_t aiChooseMove(const Combatant &self, const Combatant &foe, bool smart);
