// ---------- personalities (feature 6) ----------
//
// The trait is drawn in three places, all from here so they cannot drift:
//   - the creature card's profile page (renderCardProfile)
//   - the party / box sheet (renderMonSheet), on the typing line
//   - a short cheer on the main screen when it gets its favourite care
// The rules live in persona.cpp; this tab only draws them.

#define PERSONA_INK C565(0xd4, 0x52, 0x7e)   // the bond bar's pink: it is about the bond
#define PERSONA_CHEER_Y 276                  // persona_ui_test looks here

static const char *const S_PT_LOVES_FMT[LANG_COUNT] = {
  "%s: le encanta %s", "%s: loves %s", "%s: adore %s", "%s: liebt %s", "%s: ama %s", "%s: adora %s" };

// Card profile page: "GLUTTON: loves food", centred at y.
void personaCardLine(int y) {
  if (!personaValid(pet.persona)) return;
  char b[48];
  snprintf(b, sizeof(b), TL(S_PT_LOVES_FMT), personaName(pet.persona), personaLoves(pet.persona));
  int ts = strlen(b) <= 30 ? 2 : 1;            // long German/French rows step down a size
  gfx->setTextColor(PERSONA_INK);
  gfx->setTextSize(ts);
  gfx->setCursor(CX - (int)strlen(b) * 3 * ts, y + (ts == 1 ? 4 : 0));
  gfx->print(b);
}

// The sheet's typing line with the trait after it, the pair centred together.
// A creature banked before personalities (trait 0) shows the typing alone.
void personaTypeLine(const char *ty, uint8_t trait, int y) {
  const char *nm = personaName(trait);
  int tw = (int)strlen(ty) * 6;
  int nw = personaValid(trait) ? (int)strlen(nm) * 6 + 12 : 0;
  int x = CX - (tw + nw) / 2;
  gfx->setTextSize(1);
  gfx->setTextColor(UI_TRACK);
  gfx->setCursor(x, y);
  gfx->print(ty);
  if (!nw) return;
  gfx->setTextColor(PERSONA_INK);
  gfx->setCursor(x + tw + 12, y);
  gfx->print(nm);
}

// Main screen: "<3 GLUTTON!" for PERSONA_CHEER_MS after its favourite care.
// On the ground strip below the creature, clear of the tasks notice (96..140)
// and of the evolve / farewell prompt over the middle of the screen.
void personaMainOverlay() {
  uint8_t t = personaCheerTrait();
  if (!t || pet.isEgg() || pet.ceremony) return;
  char b[24];
  snprintf(b, sizeof(b), "%s!", personaName(t));
  int w = (int)strlen(b) * 12 + 52;
  int x = CX - w / 2, y = PERSONA_CHEER_Y;
  gfx->fillRoundRect(x, y, w, 32, 12, PERSONA_INK);
  gfx->drawRoundRect(x, y, w, 32, 12, UI_INK);
  drawMap(SPR_HEART, 32, x + 6, y, 1, false);
  gfx->setTextColor(UI_WHITE);
  gfx->setTextSize(2);
  gfx->setCursor(x + 42, y + 9);
  gfx->print(b);
}
