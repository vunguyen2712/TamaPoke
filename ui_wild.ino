// ---------- Wild encounters (feature 1 + 2). Logic: wild.cpp ----------
// Owned by this feature alone. Reached from the Adventure hub (ui_hub.ino);
// use featBegin/featRow/featRowAt/featBackPill/featBackHit/featLine from there,
// and leave with featBackToHub(). Every render path MUST end in gfx->flush().
//
// One screen, three faces, all decided by wild.st.phase (persisted, so leaving
// and coming back -- or a reboot -- lands on the same face):
//   MEET   the creature, its level, FIGHT / RUN
//   CATCH  beaten: POKE / GREAT / ULTRA BALL rows with counts, LET GO
//   NONE   the last result, and SEARCH if any encounters are left today

static const char *const S_WX[WX_COUNT][LANG_COUNT] = {
  { "DESPEJADO", "CLEAR", "DEGAGE", "KLAR", "SERENO", "LIMPO" },
  { "SOLEADO", "SUNNY", "SOLEIL", "SONNIG", "SOLE", "SOL" },
  { "LLUVIA", "RAIN", "PLUIE", "REGEN", "PIOGGIA", "CHUVA" },
  { "NIEVE", "SNOW", "NEIGE", "SCHNEE", "NEVE", "NEVE" },
  { "NIEBLA", "FOG", "BROUILLARD", "NEBEL", "NEBBIA", "NEVOA" },
  { "VIENTO", "WIND", "VENT", "WIND", "VENTO", "VENTO" },
};
static const char *const S_WT[WT_COUNT][LANG_COUNT] = {
  { "MANANA", "MORNING", "MATIN", "MORGEN", "MATTINA", "MANHA" },
  { "DIA", "DAY", "JOUR", "TAG", "GIORNO", "DIA" },
  { "TARDE", "EVENING", "SOIR", "ABEND", "SERA", "TARDE" },
  { "NOCHE", "NIGHT", "NUIT", "NACHT", "NOTTE", "NOITE" },
};
static const char *const S_W_FIGHT[LANG_COUNT]  = { "LUCHAR", "FIGHT", "COMBAT", "KAMPF", "LOTTA", "LUTAR" };
static const char *const S_W_RUN[LANG_COUNT]    = { "HUIR", "RUN", "FUIR", "FLUCHT", "FUGA", "FUGIR" };
static const char *const S_W_LETGO[LANG_COUNT]  = { "SOLTAR", "LET GO", "LIBERER", "FREILASSEN", "LASCIA", "SOLTAR" };
static const char *const S_W_SEARCH[LANG_COUNT] = { "BUSCAR", "SEARCH", "CHERCHER", "SUCHEN", "CERCA", "PROCURAR" };
static const char *const S_W_TODAY[LANG_COUNT]  = { "HOY %u/%u", "TODAY %u/%u", "AUJ. %u/%u", "HEUTE %u/%u", "OGGI %u/%u", "HOJE %u/%u" };
static const char *const S_W_THROWS[LANG_COUNT] = { "TIROS %u/%u", "THROWS %u/%u", "LANCERS %u/%u", "WUERFE %u/%u", "LANCI %u/%u", "LANCES %u/%u" };
static const char *const S_W_DONE[LANG_COUNT]   = { "VUELVE MANANA", "COME BACK TOMORROW", "REVIENS DEMAIN", "KOMM MORGEN WIEDER", "TORNA DOMANI", "VOLTE AMANHA" };
static const char *const S_W_EGG[LANG_COUNT]    = { "PRIMERO ECLOSIONA", "HATCH YOUR EGG FIRST", "FAIS ECLORE L OEUF", "ERST AUSBRUETEN", "PRIMA FAI SCHIUDERE", "CHOQUE O OVO ANTES" };
static const char *const S_W_MISSED[LANG_COUNT] = { "SE ESCAPO!", "IT BROKE FREE!", "IL SE LIBERE!", "BEFREIT!", "SI E LIBERATO!", "ESCAPOU!" };
static const char *const S_W_FLED[LANG_COUNT]   = { "HUYO...", "IT FLED...", "IL A FUI...", "GEFLOHEN...", "E FUGGITO...", "FUGIU..." };
static const char *const S_W_PARTY[LANG_COUNT]  = { "ATRAPADO! AL EQUIPO", "CAUGHT! TO THE PARTY", "ATTRAPE! EQUIPE", "GEFANGEN! INS TEAM", "PRESO! IN SQUADRA", "CAPTURADO! EQUIPE" };
static const char *const S_W_BOX[LANG_COUNT]    = { "ATRAPADO! A LA CAJA", "CAUGHT! TO THE BOX", "ATTRAPE! BOITE", "GEFANGEN! IN DIE BOX", "PRESO! NEL BOX", "CAPTURADO! CAIXA" };
static const char *const S_W_FULL[LANG_COUNT]   = { "EQUIPO Y CAJA LLENOS", "PARTY AND BOX FULL", "EQUIPE+BOITE PLEINES", "TEAM UND BOX VOLL", "SQUADRA E BOX PIENI", "EQUIPE E CAIXA CHEIAS" };
static const char *const S_W_NOBALL[LANG_COUNT] = { "NO TE QUEDAN", "NONE LEFT", "PLUS AUCUNE", "KEINE MEHR", "NESSUNA", "NENHUMA" };
static const char *const S_W_LOST[LANG_COUNT]   = { "SE FUE...", "IT GOT AWAY...", "IL EST PARTI...", "ES IST WEG...", "E ANDATO VIA...", "FOI EMBORA..." };
static const char *const S_W_RAN[LANG_COUNT]    = { "HUISTE", "YOU RAN", "TU AS FUI", "DU BIST GEFLOHEN", "SEI FUGGITO", "VOCE FUGIU" };
static const char *const S_W_WON[LANG_COUNT]    = { "LANZA UNA BALL!", "THROW A BALL!", "LANCE UNE BALL!", "WIRF EINEN BALL!", "LANCIA UNA BALL!", "JOGUE UMA BALL!" };

// The last thing that happened, for the line under the creature. UI only:
// what matters is persisted in wild.st; a reboot just loses the caption.
enum : uint8_t {
  WM_NONE = 0, WM_GIFT, WM_EGG, WM_MISSED, WM_FLED, WM_PARTY, WM_BOX, WM_FULL,
  WM_NOBALL, WM_LOST, WM_RAN, WM_WON, WM_LETGO,
};
static uint8_t wildMsg = WM_NONE;
static int16_t wildMsgDex = 0;     // who the caption is about (a catch, a flight)
static uint8_t wildMsgItem = 0;    // a berry that dropped on the win

#define WILD_ROW_FIGHT 4
#define WILD_ROW_RUN 5
#define WILD_ROW_BALL0 2           // POKE / GREAT / ULTRA on rows 2..4
#define WILD_ROW_LETGO 5
#define WILD_ROW_SEARCH 4

// ONE answer to "may the player fight now", for the row's look AND its tap:
// the same guard startBattle() applies, so a lit FIGHT always starts a fight.
static bool wildCanFight() { return !pet.isEgg() && pet.ceremony == CER_NONE; }

// Encounters left today, without writing anything (render paths only read):
// a day the state has not caught up with yet is a full day.
static uint8_t wildLeftNow() {
  return wild.st.day == pet.currentDay() ? wild.left() : WILD_PER_DAY;
}

static void wildRollNow() {
  wild.roll(pet.currentDay(), pet.lastSeenEpoch, pet.region, pet.level(), bag.has(ITEM_SHINY_CHARM));
}

void wildHubLabel(char *out, size_t n, const char *name) {
  snprintf(out, n, "%s %u/%u", name, (unsigned)wildLeftNow(), (unsigned)WILD_PER_DAY);
}
// Always open: locked never hidden, and today's weather is worth a look even
// with no encounters left.
bool wildHubEnabled() { return true; }

void wildOpened() {
  wildMsg = WM_NONE;
  wildMsgDex = 0;
  wildMsgItem = 0;
  // A fight that is not running any more never reported back (it cannot, short
  // of a crash between the two): settle it as lost rather than leave the
  // screen stuck on a battle that does not exist.
  if (wild.st.phase == WP_FIGHT && !battleOpen) wild.battleEnded(false, true, 99, bag);
  if (wild.giveGift(bag)) wildMsg = WM_GIFT;
  wildRollNow();
}

// The creature, centred on (CX, cy): the gallery's thumbnail, or its dex
// number in a ring when the card has no sprites (the graceful no-SD path).
static void wildDrawMon(int16_t dex, int cy, int scale) {
  const uint8_t *th = thumbs.get(dex);
  if (th) { drawThumb(th, CX - 40, cy - 40, scale, false); return; }
  gfx->drawCircle(CX, cy, 14 * scale, UI_INK);
  char b[8];
  snprintf(b, sizeof(b), "%03d", dex);
  featLine(cy - 8, b, UI_INK);
}

static void wildNameLine(int y, int16_t dex, uint8_t lvl, bool shiny) {
  char b[32];
  if (lvl) snprintf(b, sizeof(b), "%s%s L%u", shiny ? "*" : "", DEX_TBL[dex].name, (unsigned)lvl);
  else snprintf(b, sizeof(b), "%s%s", shiny ? "*" : "", DEX_TBL[dex].name);
  featLine(y, b, DEX_TBL[dex].accent);
}

static void wildCaption(int y) {
  const char *s = nullptr;
  char b[32];
  switch (wildMsg) {
    case WM_GIFT:
      snprintf(b, sizeof(b), "+%u %s", (unsigned)WILD_GIFT_BALLS, ITEM_TBL[ITEM_POKEBALL].name);
      s = b; break;
    case WM_EGG: s = TL(S_W_EGG); break;
    case WM_MISSED: s = TL(S_W_MISSED); break;
    case WM_FLED: s = TL(S_W_FLED); break;
    case WM_PARTY: s = TL(S_W_PARTY); break;
    case WM_BOX: s = TL(S_W_BOX); break;
    case WM_FULL: s = TL(S_W_FULL); break;
    case WM_NOBALL: s = TL(S_W_NOBALL); break;
    case WM_LOST: s = TL(S_W_LOST); break;
    case WM_RAN: s = TL(S_W_RAN); break;
    case WM_LETGO: s = TL(S_W_FLED); break;
    case WM_WON:
      if (wildMsgItem) { snprintf(b, sizeof(b), "+1 %s", ITEM_TBL[wildMsgItem].name); s = b; }
      else s = TL(S_W_WON);
      break;
    default: break;
  }
  if (s) featLine(y, s, wildMsg == WM_FULL || wildMsg == WM_EGG || wildMsg == WM_NOBALL ? UI_BAR_BAD : UI_INK);
}

void renderWild() {
  featBegin(TL(S_HUB_WILD));
  char b[40];
  const Wild::State &s = wild.st;

  if (s.phase == WP_CATCH) {
    snprintf(b, sizeof(b), TL(S_W_THROWS), (unsigned)(WILD_THROWS - s.throws), (unsigned)WILD_THROWS);
    featLine(68, b, UI_INK);
    wildCaption(90);
    wildDrawMon(s.dex, 138, 2);
    wildNameLine(170, s.dex, s.level, s.shiny);
    for (uint8_t i = 0; i < 3; i++) {
      uint8_t it = ITEM_POKEBALL + i;
      snprintf(b, sizeof(b), "%s x%u", ITEM_TBL[it].name, (unsigned)bag.count(it));
      featRow(WILD_ROW_BALL0 + i, b, !bag.has(it));
    }
    featRow(WILD_ROW_LETGO, TL(S_W_LETGO), false);
    featBackPill();
    gfx->flush();
    return;
  }

  // Today: the weather of the day and the window of the hour, from the pet's
  // clock -- the same hour Pet::isNightHour() reads.
  snprintf(b, sizeof(b), "%s - %s", TL(S_WX[wildWeatherOf(pet.currentDay())]), TL(S_WT[wildTimeOf(pet.lastSeenEpoch)]));
  featLine(68, b, UI_INK);
  snprintf(b, sizeof(b), TL(S_W_TODAY), (unsigned)wildLeftNow(), (unsigned)WILD_PER_DAY);
  featLine(92, b, 0x8410);

  if (s.phase == WP_MEET) {
    wildDrawMon(s.dex, 162, 3);
    wildNameLine(218, s.dex, s.level, s.shiny);
    wildCaption(246);
    featRow(WILD_ROW_FIGHT, TL(S_W_FIGHT), !wildCanFight());
    featRow(WILD_ROW_RUN, TL(S_W_RUN), false);
  } else {
    // NONE (or a FIGHT the battle screen is drawing over): the last result.
    if (wildMsgDex >= 1 && wildMsgDex <= DEX_COUNT) {
      wildDrawMon(wildMsgDex, 162, 3);
      wildNameLine(218, wildMsgDex, 0, false);
    }
    wildCaption(246);
    if (wildLeftNow()) featRow(WILD_ROW_SEARCH, TL(S_W_SEARCH), false);
    else featLine(FEAT_ROW_Y(WILD_ROW_SEARCH) + 12, TL(S_W_DONE), UI_INK);
  }
  featBackPill();
  gfx->flush();
}

void wildTap(int16_t x, int16_t y) {
  if (featBackHit(x, y)) { sfxPlay(SFX_TAP); featBackToHub(); return; }
  int r = featRowAt(x, y, 6);
  if (r < 0) return;
  const Wild::State &s = wild.st;

  if (s.phase == WP_MEET) {
    if (r == WILD_ROW_FIGHT) {
      if (!wildCanFight()) { sfxPlay(SFX_DENY); wildMsg = WM_EGG; return; }
      int16_t d = s.dex;
      sfxPlay(SFX_TAP);
      startBattle(d, s.level);
      if (!battleOpen) { sfxPlay(SFX_DENY); return; }
      btlKind = BK_WILD;          // after startBattle(), which sets BK_DEBUG
      wild.fightStarted();
      wildMsgDex = d;
      wildMsg = WM_NONE;
    } else if (r == WILD_ROW_RUN) {
      sfxPlay(SFX_TAP);
      wildMsgDex = s.dex;
      wildMsg = WM_RAN;
      wild.runAway();
    }
    return;
  }

  if (s.phase == WP_CATCH) {
    if (r >= WILD_ROW_BALL0 && r < WILD_ROW_BALL0 + 3) {
      uint8_t ball = ITEM_POKEBALL + (r - WILD_ROW_BALL0);
      int16_t d = s.dex;
      uint8_t res = wild.throwBall(ball, (uint8_t)random(100), party, bag, pet);
      wildMsgDex = d;
      switch (res) {
        case WTH_NO_BALL: wildMsg = WM_NOBALL; sfxPlay(SFX_DENY); break;
        case WTH_NO_ROOM: wildMsg = WM_FULL; sfxPlay(SFX_DENY); break;
        case WTH_MISSED: wildMsg = WM_MISSED; sfxPlay(SFX_TAP); break;
        case WTH_FLED: wildMsg = WM_FLED; sfxPlay(SFX_DENY); break;
        case WTH_PARTY: wildMsg = WM_PARTY; sfxPlay(SFX_VICTORY); break;
        case WTH_BOX: wildMsg = WM_BOX; sfxPlay(SFX_VICTORY); break;
        default: break;
      }
    } else if (r == WILD_ROW_LETGO) {
      sfxPlay(SFX_TAP);
      wildMsgDex = s.dex;
      wildMsg = WM_LETGO;
      wild.runAway();
    }
    return;
  }

  if (r == WILD_ROW_SEARCH && wildLeftNow()) {
    sfxPlay(SFX_TAP);
    wildMsg = WM_NONE;
    wildMsgDex = 0;
    wildRollNow();
  }
}

// Not paged: a horizontal swipe goes back to the hub. Nothing is lost -- the
// encounter and the catch are persisted and wait there.
void wildSwipe(int dir) { (void)dir; featBackToHub(); }

void wildBattleEnded(bool won, bool ran) {
  wildMsgDex = wild.st.dex;
  uint8_t drop = wild.battleEnded(won, ran, (uint8_t)random(100), bag);
  wildMsgItem = drop;
  wildMsg = won ? WM_WON : (ran ? WM_RAN : WM_LOST);
}
