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
static const char *const S_W_CHAIN[LANG_COUNT]  = { "CADENA: %s x%u", "CHAIN: %s x%u", "CHAINE: %s x%u", "KETTE: %s x%u", "CATENA: %s x%u", "CORRENTE: %s x%u" };

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
#define WILD_CHAIN_Y 266           // between the caption (246) and row 4 (288)

// ONE answer to "may the player fight now", for the row's look AND its tap:
// the same guard startBattle() applies, so a lit FIGHT always starts a fight.
static bool wildCanFight() { return !pet.isEgg() && pet.ceremony == CER_NONE; }

// Encounters left today, without writing anything (render paths only read):
// a day the state has not caught up with yet is a full day.
static uint8_t wildLeftNow() {
  return wild.st.day == pet.currentDay() ? wild.left() : WILD_PER_DAY;
}

// The roll and the peek take the SAME arguments from the SAME place, so the
// main-screen alert can only ever announce what the screen will then show.
static void wildRollNow() {
  wild.roll(pet.currentDay(), pet.lastSeenEpoch, pet.region, pet.level(), bag.has(ITEM_SHINY_CHARM));
}

// The next encounter, by Wild::peek() -- which is what roll() is built on.
// Cached: the peek walks the whole dex twice, and the main screen redraws at
// 10 fps. Everything the answer depends on is in the key, so a stale answer
// is impossible rather than merely unlikely.
struct WildPeekKey {
  uint32_t day, stDay, chDay;
  int16_t stDex, chDex;
  uint8_t tod, region, level, charm, used, phase, chLen;
};
static WildPeekKey wildPkKey;
static bool wildPkValid = false, wildPkHave = false;
static WildNext wildPkNext;

static bool wildPeekNow(WildNext &out) {
  WildPeekKey k;
  memset(&k, 0, sizeof(k));   // padding too: the key is compared with memcmp
  k.day = pet.currentDay(); k.stDay = wild.st.day; k.chDay = wild.ch.day;
  k.stDex = wild.st.dex; k.chDex = wild.ch.dex;
  k.tod = wildTimeOf(pet.lastSeenEpoch); k.region = pet.region; k.level = pet.level();
  k.charm = bag.has(ITEM_SHINY_CHARM); k.used = wild.st.used; k.phase = wild.st.phase;
  k.chLen = wild.ch.len;
  if (!wildPkValid || memcmp(&k, &wildPkKey, sizeof(k)) != 0) {
    wildPkKey = k;
    wildPkHave = wild.peek(pet.currentDay(), pet.lastSeenEpoch, pet.region, pet.level(),
                           bag.has(ITEM_SHINY_CHARM), wildPkNext);
    wildPkValid = true;
  }
  out = wildPkNext;
  return wildPkHave;
}

// ONE answer to "is the next encounter worth an alert", for the main-screen
// badge, its tap AND the hub row: the WildAlertWhy bits, 0 = no alert. Quiet
// while the live pet is an egg or mid-ceremony (it could not fight), and with
// no clock (day 0 never ends, so there is no "today" to announce).
uint8_t wildAlertNow() {
  if (pet.isEgg() || pet.ceremony != CER_NONE || pet.currentDay() == 0) return 0;
  WildNext n;
  if (!wildPeekNow(n)) return 0;
  if (n.out && wild.st.phase == WP_CATCH) return 0;   // already beaten: no news
  return wildAlertWhy(n.dex, n.shiny, pet.isRegistered(n.dex));
}

void wildHubLabel(char *out, size_t n, const char *name) {
  snprintf(out, n, "%s %u/%u%s", name, (unsigned)wildLeftNow(), (unsigned)WILD_PER_DAY,
           wildAlertNow() ? " !" : "");
}

// The chain line, when one is running today.
static void wildChainLine(int y) {
  uint32_t day = pet.currentDay();
  uint8_t len = wild.chainLen(day);
  int16_t cd = wild.chainDex(day);
  if (!len || cd < 1 || cd > DEX_COUNT) return;
  char b[40];
  snprintf(b, sizeof(b), TL(S_W_CHAIN), DEX_TBL[cd].name, (unsigned)len);
  featLine(y, b, UI_BAR_WARN);
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
    wildChainLine(WILD_CHAIN_Y);
    featRow(WILD_ROW_FIGHT, TL(S_W_FIGHT), !wildCanFight());
    featRow(WILD_ROW_RUN, TL(S_W_RUN), false);
  } else {
    // NONE (or a FIGHT the battle screen is drawing over): the last result.
    if (wildMsgDex >= 1 && wildMsgDex <= DEX_COUNT) {
      wildDrawMon(wildMsgDex, 162, 3);
      wildNameLine(218, wildMsgDex, 0, false);
    }
    wildCaption(246);
    wildChainLine(WILD_CHAIN_Y);
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
      // startBattle() builds a plain foe; a shiny one is shown as such. Only a
      // shiny pays for the second sprite load, and only once per fight.
      if (s.shiny) { btlFoe.shiny = true; btlSyncSprite(1, btlFoe); }
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

// ---- main-screen alert (feature 1). Called from ui_hub.ino's hooks.
// A "!" in the clear patch of scenery left of the pet, above the bars. Every
// neighbour it must miss is asserted in wildalert_ui_test:
//   header / menu band y 28..94, the pet zone x 110..356 y 95..310, the
//   TASK DONE banner y 96..140, the evolve (y 172..236) and farewell
//   (x 29..437, y 176..234) buttons, the banners up to y 250, the food picker
//   x 101.., the release dialog x 94.., the bars from y 312 and the four buttons.
#define WILD_BADGE_X 70
#define WILD_BADGE_Y 274
#define WILD_BADGE_R 15            // drawn radius, +2 at the top of the pulse
#define WILD_BADGE_HIT 22          // half-size of the square hit area

static bool wildBadgeHit(int16_t x, int16_t y) {
  return x >= WILD_BADGE_X - WILD_BADGE_HIT && x <= WILD_BADGE_X + WILD_BADGE_HIT &&
         y >= WILD_BADGE_Y - WILD_BADGE_HIT && y <= WILD_BADGE_Y + WILD_BADGE_HIT;
}

void wildMainBadge() {
  uint8_t why = wildAlertNow();
  if (!why) return;
  // A gentle pulse: 15 -> 17 -> 15 px over 0.8 s. Two circles and a glyph.
  int ph = (int)((millis() / 100) % 8);
  int r = WILD_BADGE_R + (ph < 4 ? ph : 8 - ph) / 2;
  uint16_t fill = (why & WA_SHINY) ? UI_BAR_WARN : UI_BAR_BAD;
  gfx->fillCircle(WILD_BADGE_X, WILD_BADGE_Y, r, fill);
  gfx->drawCircle(WILD_BADGE_X, WILD_BADGE_Y, r, UI_INK);
  gfx->setTextColor(UI_WHITE);
  gfx->setTextSize(2);
  gfx->setCursor(WILD_BADGE_X - 5, WILD_BADGE_Y - 7);
  gfx->print("!");
}

bool wildMainBadgeTap(int16_t x, int16_t y) {
  if (uiCurrentScreen() != SCR_MAIN) return false;
  if (!wildBadgeHit(x, y) || !wildAlertNow()) return false;
  sfxPlay(SFX_TAP);
  featCloseAll();
  wildOpen = true;
  wildOpened();
  return true;
}

// ---- test hooks (wildalert_ui_test): the badge's hit rect, and which of the
// main screen's targets a rect would land on, measured with the sketch's OWN
// constants (EVO_BTN_*, FAR_BTN_*, buttons[], BTN_HIT, inPetZone()) -- so moving
// a button, not just the badge, fails the test. Literal rects are copied from
// the one place that draws them, named here.
void wildBadgeRect(int16_t *x0, int16_t *y0, int16_t *x1, int16_t *y1) {
  *x0 = WILD_BADGE_X - WILD_BADGE_HIT; *y0 = WILD_BADGE_Y - WILD_BADGE_HIT;
  *x1 = WILD_BADGE_X + WILD_BADGE_HIT; *y1 = WILD_BADGE_Y + WILD_BADGE_HIT;
}

static bool wildRectsMeet(int ax0, int ay0, int ax1, int ay1, int bx0, int by0, int bx1, int by1) {
  return ax0 <= bx1 && bx0 <= ax1 && ay0 <= by1 && by0 <= ay1;
}

enum : uint16_t {
  WOV_BAND = 1, WOV_PET = 2, WOV_TASK = 4, WOV_EVO = 8, WOV_FAR = 16, WOV_BTN = 32,
  WOV_PANEL = 64, WOV_CELEB = 128, WOV_PARTYBN = 256, WOV_FEED = 512, WOV_RELEASE = 1024,
  WOV_ROUND = 2048,
};

uint16_t wildMainOverlaps(int16_t x0, int16_t y0, int16_t x1, int16_t y1) {
  uint16_t o = 0;
  if (y0 <= 94 && y1 >= 28) o |= WOV_BAND;                    // header + menu band (onTap)
  for (int y = y0; y <= y1 && !(o & WOV_PET); y++)            // inPetZone(), asked directly
    for (int x = x0; x <= x1; x++) if (inPetZone(x, y)) { o |= WOV_PET; break; }
  if (wildRectsMeet(x0, y0, x1, y1, 103, 96, 103 + 260, 96 + 44)) o |= WOV_TASK;  // drawTasksNotice()
  if (wildRectsMeet(x0, y0, x1, y1, EVO_BTN_X, EVO_BTN_Y, EVO_BTN_X + EVO_BTN_W, EVO_BTN_Y + EVO_BTN_H)) o |= WOV_EVO;
  if (wildRectsMeet(x0, y0, x1, y1, FAR_BTN_X, FAR_BTN_Y, FAR_BTN_X + FAR_BTN_W, FAR_BTN_Y + FAR_BTN_H)) o |= WOV_FAR;
  for (int i = 0; i < BTN_COUNT; i++) {
    int cx = buttons[i].cx, cy = buttons[i].cy;
    int nx = cx < x0 ? x0 : (cx > x1 ? x1 : cx);
    int ny = cy < y0 ? y0 : (cy > y1 ? y1 : cy);
    int dx = nx - cx, dy = ny - cy;
    if (dx * dx + dy * dy <= BTN_HIT * BTN_HIT) o |= WOV_BTN;
  }
  if (y1 >= 312) o |= WOV_PANEL;                              // bars + buttons panel
  if (wildRectsMeet(x0, y0, x1, y1, 73, 150, 73 + 320, 150 + 96)) o |= WOV_CELEB;      // drawCelebration()
  if (wildRectsMeet(x0, y0, x1, y1, 53, 176, 53 + 360, 176 + 74)) o |= WOV_PARTYBN;    // party banner
  if (wildRectsMeet(x0, y0, x1, y1, 101, 288, 101 + 264, 288 + 64)) o |= WOV_FEED;     // food picker
  if (wildRectsMeet(x0, y0, x1, y1, 94, 168, 94 + 278, 168 + 152)) o |= WOV_RELEASE;   // release dialog
  const int cs[4][2] = { { x0, y0 }, { x1, y0 }, { x0, y1 }, { x1, y1 } };
  for (auto &c : cs) {
    int dx = c[0] - CX, dy = c[1] - CY;
    if (dx * dx + dy * dy > 228 * 228) o |= WOV_ROUND;         // off the round panel
  }
  return o;
}

bool wildBadgeHitTest(int16_t x, int16_t y) { return wildBadgeHit(x, y); }
