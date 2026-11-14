#include "persona.h"
#include "pet.h"
#include "party.h"
#include "items.h"
#include "game_events.h"
#include "i18n.h"

// The numbers live here and nowhere else. README "Personalities" quotes them;
// persona_test measures each one through the real Pet methods.
#define PJ_FOOD     6   // GLUTTON: extra joy from any food
#define PJ_BED      8   // SLEEPY: joy from being put to bed by hand
#define PE_BED      2   // SLEEPY: extra energy per minute asleep (+6 -> +8)
#define PJ_BATH    10   // TIDY: joy from a bath
#define PB_BATH     2   // TIDY: bond from a bath
#define PJ_PET      5   // CUDDLY: extra joy from a caress (+5 -> +10)
#define PB_PET      1   // CUDDLY: extra bond from a caress (+1 -> +2)
#define PJ_TRAIN    8   // ENERGETIC: extra joy from a training session
#define PB_TRAIN    1
#define PJ_FAVB    10   // PICKY: extra joy from its favourite berry (+10 -> +20)
#define PB_FAVB     2
#define PF_PLAIN   -5   // PICKY: food from any other berry (+25 -> +20)

static const char *const S_PT_NAME[PT_COUNT][LANG_COUNT] = {
  { "", "", "", "", "", "" },
  { "GLOTON", "GLUTTON", "GOURMAND", "VIELFRASS", "GOLOSO", "GULOSO" },
  { "DORMILON", "SLEEPY", "DORMEUR", "SCHLAFMUETZE", "DORMIGLIONE", "DORMINHOCO" },
  { "JUGUETON", "PLAYFUL", "JOUEUR", "VERSPIELT", "GIOCOSO", "BRINCALHAO" },
  { "PULCRO", "TIDY", "SOIGNE", "ORDENTLICH", "ORDINATO", "ASSEADO" },
  { "MIMOSO", "CUDDLY", "CALIN", "VERSCHMUST", "COCCOLONE", "CARINHOSO" },
  { "ENERGICO", "ENERGETIC", "ENERGIQUE", "ENERGISCH", "ENERGICO", "ENERGICO" },
  { "EXIGENTE", "PICKY", "DIFFICILE", "WAEHLERISCH", "SCHIZZINOSO", "EXIGENTE" },
};
static const char *const S_PT_LOVES[PT_COUNT][LANG_COUNT] = {
  { "", "", "", "", "", "" },
  { "comer", "food", "manger", "Essen", "cibo", "comer" },
  { "dormir", "bedtime", "dodo", "Schlaf", "nanna", "dormir" },
  { "jugar", "play", "jouer", "Spielen", "giocare", "brincar" },
  { "el bano", "baths", "le bain", "Baden", "il bagno", "banho" },
  { "mimos", "cuddles", "calins", "Kuscheln", "coccole", "carinho" },
  { "entrenar", "training", "l'entrainement", "Training", "allenarsi", "treinar" },
  { "su baya", "its berry", "sa baie", "seine Beere", "la sua bacca", "sua baga" },
};

bool personaValid(uint8_t t) { return t > PT_NONE && t < PT_COUNT; }

uint8_t personaRoll() { return (uint8_t)(1 + random(PT_COUNT - 1)); }

const char *personaName(uint8_t t) { return personaValid(t) ? TL(S_PT_NAME[t]) : ""; }
const char *personaLoves(uint8_t t) { return personaValid(t) ? TL(S_PT_LOVES[t]) : ""; }

PersonaEffect personaCare(uint8_t trait, uint8_t care, uint8_t baseJoy) {
  PersonaEffect e;
  switch (trait) {
    case PT_GLUTTON:
      if (care == PC_BERRY || care == PC_FAV_BERRY || care == PC_CANDY) { e.joy = PJ_FOOD; e.fav = true; }
      break;
    case PT_SLEEPY:
      if (care == PC_LIGHTS_OFF) { e.joy = PJ_BED; e.fav = true; }
      break;
    case PT_PLAYFUL:
      // double the session's own joy, whatever it was worth
      if (care == PC_PLAY) { e.joy = (int8_t)(baseJoy > 100 ? 100 : baseJoy); e.fav = true; }
      break;
    case PT_TIDY:
      if (care == PC_BATH) { e.joy = PJ_BATH; e.bond = PB_BATH; e.fav = true; }
      break;
    case PT_CUDDLY:
      if (care == PC_CARESS) { e.joy = PJ_PET; e.bond = PB_PET; e.fav = true; }
      break;
    case PT_ENERGETIC:
      if (care == PC_TRAIN) { e.joy = PJ_TRAIN; e.bond = PB_TRAIN; e.fav = true; }
      break;
    case PT_PICKY:
      if (care == PC_FAV_BERRY) { e.joy = PJ_FAVB; e.bond = PB_FAVB; e.fav = true; }
      else if (care == PC_BERRY) e.food = PF_PLAIN;
      break;
    default: break;
  }
  return e;
}

PersonaDrain personaDrain(uint8_t trait, uint32_t minute, uint8_t poops, bool asleep) {
  PersonaDrain d;
  if (asleep) {
    // The night is never harder: the only thing a trait does asleep is help.
    if (trait == PT_SLEEPY) d.energy = -PE_BED;
    return d;
  }
  bool nudge = (minute % PERSONA_NUDGE_EVERY) == 0;
  switch (trait) {
    case PT_GLUTTON:   if (nudge) d.food = 1; break;               // -2 -> -2.25/min
    case PT_SLEEPY:    if (nudge) d.energy = 1; break;             // -1 -> -1.25/min
    case PT_PLAYFUL:   if (nudge) d.joy = 1; break;                // -1 -> -1.25/min
    case PT_TIDY:      if (poops) d.joy = 1; break;                // -1 more a minute with poop out
    case PT_ENERGETIC: if (nudge) { d.food = 1; d.energy = -1; } break;  // eats more, tires less
    default: break;   // CUDDLY and PICKY: no change to the drain
  }
  return d;
}

static uint32_t cheerUntil = 0;
static uint8_t cheerTrait = PT_NONE;

void personaCheer(uint8_t trait) {
  if (!personaValid(trait)) return;
  cheerTrait = trait;
  cheerUntil = millis() + PERSONA_CHEER_MS;
}
bool personaCheering() { return cheerTrait != PT_NONE && millis() < cheerUntil; }
uint8_t personaCheerTrait() { return personaCheering() ? cheerTrait : PT_NONE; }

// Nothing to do at boot: the live creature's trait is loaded (and, for a save
// from before this feature, rolled once and written) by Pet::load(), and
// banked creatures carry theirs in PartyMon::spare. Kept as the hook setup()
// already calls.
void personaBegin() {}
