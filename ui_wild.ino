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
static const char *const S_W_GOTCHA[LANG_COUNT] = { "ATRAPADO!", "GOTCHA!", "ATTRAPE!", "GEFANGEN!", "PRESO!", "PEGUEI!" };
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
  wildCatchCancel();   // a throw left mid-animation was already settled and saved
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


// ---------- the catch: throw, shake, click (or burst and flee) ----------
//
// ONE throw decides it (see WILD_THROWS in wild.h). The result is decided and
// SAVED the moment the ball leaves your hand -- Wild::throwBall() -- and this
// only replays it, so a reset mid-animation can neither lose a catch nor give
// a second try. Taps are ignored while it plays; leaving the screen drops it.

// The three balls share one 16x16 shape; the palette makes the ball.
//   K outline  T top  H top highlight  S top markings  W white  G white shade
static const char *const BALL_MAP[16] = {
  ".....KKKKKK.....",
  "...KKTTTTTTKK...",
  "..KTSSTTTTSSTK..",
  ".KTHSSTTTTSSTTK.",
  ".KHHTSTTTTSTTTK.",
  "KTHTTTTTTTTTTTTK",
  "KTTTTTKKKKTTTTTK",
  "KKKKKKWWWWKKKKKK",
  "KKKKKKWWWWKKKKKK",
  "KWWWWWKKKKWWWWWK",
  "KWWWWWWWWWWWWWWK",
  ".KWWWWWWWWWWWWK.",
  ".KWWWWWWWWWWWGK.",
  "..KWWWWWWWWWGK..",
  "...KKWWWWWWKK...",
  ".....KKKKKK.....",
};
// T, H, S per ball: POKE (red), GREAT (blue, red wings), ULTRA (black, yellow H)
static const uint16_t BALL_PAL[3][3] = {
  { 0xE8A4, 0xFCD3, 0xE8A4 },
  { 0x2B7F, 0x7D9F, 0xE8A4 },
  { 0x31A6, 0x7BEF, 0xFEA0 },
};

static uint16_t ballColor(uint8_t kind, char ch) {
  switch (ch) {
    case 'K': return 0x0000;
    case 'W': return 0xFFFF;
    case 'G': return 0xBDF7;
    case 'T': return BALL_PAL[kind][0];
    case 'H': return BALL_PAL[kind][1];
    case 'S': return BALL_PAL[kind][2];
    default: return 0;
  }
}

// The ball centred on (cx, cy), `s` px per sprite pixel, turned `deg` degrees.
// `gap` opens it: the top half rises and the bottom half drops by that many px.
// Rotation maps each source pixel's centre and paints one s+1 square, which
// leaves no holes at the small angles a shake uses and costs 256 rects at most.
void wildDrawBall(uint8_t kind, int cx, int cy, int s, int deg, int gap) {
  if (kind > 2) kind = 0;
  float a = deg * 0.0174533f, ca = cosf(a), sa = sinf(a);
  for (int r = 0; r < 16; r++) {
    int dy = (r < 8) ? -gap : gap;
    for (int c = 0; c < 16; c++) {
      char ch = BALL_MAP[r][c];
      if (ch == '.') continue;
      float ox = (c - 7.5f) * s, oy = (r - 7.5f) * s;
      int px = cx + (int)(ox * ca - oy * sa);
      int py = cy + (int)(ox * sa + oy * ca) + dy;
      gfx->fillRect(px - s / 2, py - s / 2, s + 1, s + 1, ballColor(kind, ch));
    }
  }
}

// A four-point sparkle, for the click: bold arms with an ink outline, so it
// reads on the pale day background as well as at night.
static void wildStar(int x, int y, int r, uint16_t col) {
  if (r < 2) r = 2;
  gfx->fillRect(x - r - 1, y - 2, 2 * r + 3, 5, UI_INK);
  gfx->fillRect(x - 2, y - r - 1, 5, 2 * r + 3, UI_INK);
  gfx->fillRect(x - r, y - 1, 2 * r + 1, 3, col);
  gfx->fillRect(x - 1, y - r, 3, 2 * r + 1, col);
  gfx->fillRect(x - 2, y - 2, 5, 5, col);
}

// Stage lengths (ms). Shakes repeat WC_SHAKE each; the end depends on the result.
#define WC_THROW 650
#define WC_ABSORB 400
#define WC_DROP 350
#define WC_SHAKE 650
#define WC_SHAKE_MOVE 320       // the wobble; the rest of WC_SHAKE is a held breath
#define WC_CLICK 1300
#define WC_BURST 350
#define WC_FLEE 900
#define WC_MON_Y 150            // the creature's centre on this screen
#define WC_BALL_Y 206           // where the ball lands and shakes
#define WC_BALL_S 3             // 48 px ball

enum : uint8_t { WCS_OFF = 0, WCS_THROW, WCS_ABSORB, WCS_DROP, WCS_SHAKE, WCS_CLICK, WCS_BURST, WCS_FLEE, WCS_DONE };

static struct {
  bool on = false;
  uint8_t ball = 0;         // 0 POKE, 1 GREAT, 2 ULTRA
  uint8_t shakes = 0;       // 0..WILD_SHAKES
  bool caught = false;
  int16_t dex = 0;
  uint8_t level = 0;
  bool shiny = false;
  uint32_t t0 = 0;
  uint8_t lastStage = WCS_OFF;
  uint8_t lastShake = 0;
} wc;

static uint32_t wcEndOf(uint8_t st) {   // ms from t0 at which stage `st` ends
  uint32_t t = WC_THROW;
  if (st == WCS_THROW) return t;
  t += WC_ABSORB; if (st == WCS_ABSORB) return t;
  t += WC_DROP;   if (st == WCS_DROP) return t;
  t += (uint32_t)wc.shakes * WC_SHAKE; if (st == WCS_SHAKE) return t;
  if (wc.caught) { t += WC_CLICK; return t; }
  t += WC_BURST; if (st == WCS_BURST) return t;
  return t + WC_FLEE;
}

// Where the animation is, from the clock alone. `into` = ms into that stage.
uint8_t wildCatchStage(uint32_t *into) {
  if (!wc.on) { if (into) *into = 0; return WCS_OFF; }
  uint32_t e = millis() - wc.t0;
  static const uint8_t ORDER_OK[] = { WCS_THROW, WCS_ABSORB, WCS_DROP, WCS_SHAKE, WCS_CLICK };
  static const uint8_t ORDER_NO[] = { WCS_THROW, WCS_ABSORB, WCS_DROP, WCS_SHAKE, WCS_BURST, WCS_FLEE };
  const uint8_t *ord = wc.caught ? ORDER_OK : ORDER_NO;
  uint8_t n = wc.caught ? sizeof(ORDER_OK) : sizeof(ORDER_NO);
  uint32_t start = 0;
  for (uint8_t i = 0; i < n; i++) {
    uint32_t end = wcEndOf(ord[i]);
    if (e < end) { if (into) *into = e - start; return ord[i]; }
    start = end;
  }
  if (into) *into = 0;
  return WCS_DONE;
}

bool wildCatchAnimating() { return wildCatchStage(nullptr) != WCS_OFF && wildCatchStage(nullptr) != WCS_DONE; }
uint8_t wildCatchShakesShown() { return wc.on ? wc.shakes : 0; }
void wildCatchCancel() { wc.on = false; }

static void wcStart(uint8_t ballItem, uint8_t shakes, bool caught, int16_t dex, uint8_t lvl, bool shiny) {
  wc.on = true;
  wc.ball = (uint8_t)(ballItem - ITEM_POKEBALL);
  wc.shakes = shakes > WILD_SHAKES ? WILD_SHAKES : shakes;
  wc.caught = caught;
  wc.dex = dex; wc.level = lvl; wc.shiny = shiny;
  wc.t0 = millis();
  wc.lastStage = WCS_OFF;
  wc.lastShake = 0;
  sfxPlay(SFX_PLAY);                       // the throw
}

// Sounds fire on the frame a stage (or a shake) begins, not on a timer of
// their own, so they can never drift from what is drawn.
static void wcSounds(uint8_t st, uint32_t into) {
  if (st != wc.lastStage) {
    if (st == WCS_CLICK) sfxPlay(SFX_VICTORY);
    else if (st == WCS_BURST) sfxPlay(SFX_DENY);
    else if (st == WCS_ABSORB) sfxPlay(SFX_BEAM);
    wc.lastStage = st;
  }
  if (st == WCS_SHAKE) {
    uint8_t k = (uint8_t)(into / WC_SHAKE) + 1;
    if (k != wc.lastShake) { wc.lastShake = k; sfxPlay(SFX_TAP); }
  }
}

static void renderCatchAnim() {
  uint32_t into = 0;
  uint8_t st = wildCatchStage(&into);
  if (st == WCS_DONE) { wc.on = false; return; }   // the caller draws the result
  wcSounds(st, into);

  featBegin(TL(S_HUB_WILD));
  wildNameLine(68, wc.dex, wc.level, wc.shiny);
  // the ground the ball lands on, so a still ball reads as resting, not floating
  gfx->fillRoundRect(CX - 70, WC_BALL_Y + 22, 140, 10, 5, UI_TRACK);

  int bx = CX, by = WC_BALL_Y, deg = 0, gap = 0;
  bool monShown = false;
  int monX = CX;
  switch (st) {
    case WCS_THROW: {
      // from the bottom right, in an arc, spinning, onto the creature
      float t = into / (float)WC_THROW;
      bx = (CX + 120) + (int)((CX - (CX + 120)) * t);
      by = 420 + (int)((WC_MON_Y - 420) * t) - (int)(110 * 4 * t * (1 - t));
      deg = (int)(t * 900);
      monShown = true;
      break;
    }
    case WCS_ABSORB: {
      // the ball opens over it, a flash, and the creature is drawn inside
      float t = into / (float)WC_ABSORB;
      bx = CX; by = WC_MON_Y - 30; gap = 6;
      if (t < 0.5f) monShown = true;
      int r = t < 0.5f ? (int)(10 + 90 * t) : (int)(55 * (1 - t) * 2);
      gfx->fillCircle(CX, WC_MON_Y, r, UI_WHITE);
      gfx->drawCircle(CX, WC_MON_Y, r + 3, 0xFE60);
      break;
    }
    case WCS_DROP: {
      float t = into / (float)WC_DROP;
      int y0 = WC_MON_Y - 30;
      by = y0 + (int)((WC_BALL_Y - y0) * t * t);
      if (t > 0.85f) by -= (int)(6 * (1 - t) / 0.15f);   // a small bounce
      break;
    }
    case WCS_SHAKE: {
      uint32_t ms = into % WC_SHAKE;
      if (ms < WC_SHAKE_MOVE) {
        float ph = ms / (float)WC_SHAKE_MOVE;              // one left-right-centre
        deg = (int)(28 * sinf(ph * 6.2832f));
        bx = CX + deg / 5;
      }
      break;
    }
    case WCS_CLICK: {
      // three sparkles rise and fade: it is yours
      float t = into / (float)WC_CLICK;
      static const int SX[4] = { -44, -15, 15, 44 };
      static const int SY[4] = { 4, -12, -12, 4 };
      for (int i = 0; i < 4; i++) {
        int sy = WC_BALL_Y - 24 + SY[i] - (int)(46 * t);
        wildStar(CX + SX[i], sy, 10 - (int)(5 * t), 0xFE60);
      }
      featLine(262, TL(S_W_GOTCHA), UI_BAR_OK);
      break;
    }
    case WCS_BURST: {
      float t = into / (float)WC_BURST;
      gap = (int)(4 + 26 * t);
      gfx->fillCircle(CX, WC_BALL_Y, (int)(10 + 40 * t), UI_WHITE);
      monShown = t > 0.5f;
      break;
    }
    case WCS_FLEE: {
      // out of the ball, and off the edge of the screen
      float t = into / (float)WC_FLEE;
      monShown = true;
      monX = CX - (int)(t * t * 330);
      bx = -100;                                           // the ball is gone
      featLine(262, TL(S_W_FLED), UI_BAR_BAD);
      break;
    }
  }
  if (monShown) {
    const uint8_t *th = thumbs.get(wc.dex);
    if (th) drawThumb(th, monX - 40, WC_MON_Y - 40, 2, false);
    else { gfx->drawCircle(monX, WC_MON_Y, 28, UI_INK); }
  }
  if (bx > -50) wildDrawBall(wc.ball, bx, by, WC_BALL_S, deg, gap);
  gfx->flush();
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
  if (wc.on) {
    renderCatchAnim();
    if (wc.on) return;          // still playing; else fall through to the result
  }
  featBegin(TL(S_HUB_WILD));
  char b[40];
  const Wild::State &s = wild.st;

  if (s.phase == WP_CATCH) {
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
  if (wildCatchAnimating()) return;   // the throw plays out; nothing to press
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
      uint8_t lvl = s.level;
      bool shiny = s.shiny;
      // The roll, the shakes it earns and the result are fixed HERE, and the
      // result is saved by throwBall() before a single frame is drawn.
      uint8_t roll = (uint8_t)random(100);
      uint8_t shakes = wildShakesFor(wildCatchChance(d, ball), roll);
      uint8_t res = wild.throwBall(ball, roll, party, bag, pet);
      wildMsgDex = d;
      bool thrown = (res == WTH_MISSED || res == WTH_FLED || res == WTH_PARTY || res == WTH_BOX);
      if (thrown) wcStart(ball, shakes, res == WTH_PARTY || res == WTH_BOX, d, lvl, shiny);
      switch (res) {
        case WTH_NO_BALL: wildMsg = WM_NOBALL; sfxPlay(SFX_DENY); break;
        case WTH_NO_ROOM: wildMsg = WM_FULL; sfxPlay(SFX_DENY); break;
        // a thrown ball's sounds come from the animation (wcSounds)
        case WTH_MISSED: wildMsg = WM_MISSED; break;
        case WTH_FLED: wildMsg = WM_FLED; break;
        case WTH_PARTY: wildMsg = WM_PARTY; break;
        case WTH_BOX: wildMsg = WM_BOX; break;
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
void wildSwipe(int dir) { (void)dir; wildCatchCancel(); featBackToHub(); }

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
