// ---------- battle screen art (v3.18) ----------
// The look of the fight: the title banner, the HP cards with their type pills,
// the platforms the creatures stand on, the ground and vignette over the
// backdrop, the chunky command buttons, the move buttons, the message box.
// What the fight DOES stays in TamaPoke.ino (renderBattle/battleTap); this tab
// only draws, and answers "where is X drawn" through btlUiRect() so the tap
// handlers, ui_movefx.ino and battle_ui_test all ask the same geometry.
//
// Everything here is procedural: no new bitmaps. The blends (shadows, the
// ground grid, the vignette) read and write the canvas directly, a few thousand
// pixels a frame, because the battle redraws every 85 ms during effects.

// Six-language rows, ES EN FR DE IT PT, ASCII only.
static const char *const S_BB_GYM[LANG_COUNT]    = { "GIMNASIO %u", "GYM %u", "ARENE %u", "ARENA %u", "PALESTRA %u", "GINASIO %u" };
static const char *const S_BB_E4[LANG_COUNT]     = { "ALTO MANDO", "ELITE 4", "CONSEIL 4", "TOP VIER", "SUPERQUATTRO", "ELITE 4" };
static const char *const S_BB_CHAMP[LANG_COUNT]  = { "CAMPEON", "CHAMPION", "MAITRE", "CHAMP", "CAMPIONE", "CAMPEAO" };
static const char *const S_BB_WILD[LANG_COUNT]   = { "SALVAJE", "WILD", "SAUVAGE", "WILD", "SELVATICO", "SELVAGEM" };
static const char *const S_BB_TOWER[LANG_COUNT]  = { "TORRE P%u", "TOWER F%u", "TOUR E%u", "TURM E%u", "TORRE P%u", "TORRE A%u" };
static const char *const S_BB_REMATCH[LANG_COUNT]= { "REVANCHA", "REMATCH", "REVANCHE", "REVANCHE", "RIVINCITA", "REVANCHE" };
static const char *const S_BB_CHAL[LANG_COUNT]   = { "DESAFIO", "CHALLENGE", "DEFI", "HERAUSFORD.", "SFIDA", "DESAFIO" };
static const char *const S_BB_LAN[LANG_COUNT]    = { "LAN", "LAN", "LAN", "LAN", "LAN", "LAN" };
static const char *const S_BB_BATTLE[LANG_COUNT] = { "COMBATE", "BATTLE", "COMBAT", "KAMPF", "LOTTA", "BATALHA" };
static const char *const S_BB_WON[LANG_COUNT]    = { "VICTORIA", "VICTORY", "VICTOIRE", "SIEG", "VITTORIA", "VITORIA" };
static const char *const S_BB_LOST[LANG_COUNT]   = { "DERROTA", "DEFEAT", "DEFAITE", "NIEDERLAGE", "SCONFITTA", "DERROTA" };
static const char *const S_BB_BACK[LANG_COUNT]   = { "VOLVER", "BACK", "RETOUR", "ZURUECK", "INDIETRO", "VOLTAR" };

// The command buttons' faces: face, its darker underside, its highlight.
#define BB_RED      0xEA47
#define BB_RED_D    0xA144
#define BB_RED_L    0xFC4E
#define BB_ORANGE   0xF506
#define BB_ORANGE_D 0xBB43
#define BB_ORANGE_L 0xFE8E
#define BB_PALE     0xF79F
#define BB_PALE_D   0xAD98
#define BB_BLUE     0x33DC
#define BB_BLUE_D   0x1A54
#define BB_BLUE_L   0x85DF
#define BB_NAVY     0x1928    // outlines, the banner, the menu band
#define BB_NAVY_D   0x0886
#define BB_CARD     0xF7BE    // the HP cards' face
#define BB_PLAT     0xE73C    // a platform's top
#define BB_PLAT_HI  0xFFDF    // ...and its lit centre
#define BB_PLAT_RIM 0x94B2
#define BB_HP_TRACK 0x3186
#define BB_HP_OK    0x3F2A    // the bar itself: green, amber, red
#define BB_HP_WARN  0xFD80
#define BB_HP_BAD   0xF0E4

// The geometry, in one place. btlUiRect() hands it out; nothing else restates it.
#define BB_CARD_W 160
#define BB_CARD_H 56
#define BB_FOE_CARD_X 84
#define BB_FOE_CARD_Y 62
#define BB_YOU_CARD_X 236
#define BB_YOU_CARD_Y 170
#define BB_PILL_H 14
#define BB_BANNER_Y 22
#define BB_BANNER_H 26
#define BB_BANNER_MAXW 196

// ---- canvas blends. Clipped to the panel; a no-op if there is no canvas.
static inline uint16_t bbDark(uint16_t c, uint8_t q) {   // q 1/2/3 = 25/50/75% darker
  if (q == 1) return c - ((c >> 2) & 0x39E7);
  if (q == 2) return (c >> 1) & 0x7BEF;
  return (c >> 2) & 0x39E7;
}
static inline uint16_t bbLight(uint16_t c) {               // 25% toward white
  return c + (((uint16_t)~c >> 2) & 0x39E7);
}
// mode 1..3 darkens 25/50/75%, 4 lightens 25%, 5 lightens ~44%
static void bbSpan(int y, int x0, int x1, uint8_t mode) {
  if (y < 0 || y >= 466) return;
  if (x0 < 0) x0 = 0;
  if (x1 > 465) x1 = 465;
  uint16_t *fb = gfx->getFramebuffer();
  if (!fb || x0 > x1) return;
  uint16_t *p = fb + y * 466 + x0;
  for (int x = x0; x <= x1; x++, p++)
    *p = mode == 5 ? bbLight(bbLight(*p)) : mode == 4 ? bbLight(*p) : bbDark(*p, mode);
}
static void bbBlendPx(int x, int y, uint8_t mode) { bbSpan(y, x, x, mode); }

// Half-width of an ellipse's row dy, integer only.
static int bbHalf(int rx, int ry, int dy) {
  int32_t q = (int32_t)ry * ry - (int32_t)dy * dy;
  if (q <= 0) return 0;
  int32_t r = (int32_t)sqrtf((float)q);
  return (int)(rx * r / ry);
}
static void bbFillEllipse(int cx, int cy, int rx, int ry, uint16_t col) {
  for (int dy = -ry; dy <= ry; dy++) {
    int h = bbHalf(rx, ry, dy);
    gfx->drawFastHLine(cx - h, cy + dy, h * 2 + 1, col);
  }
}
static void bbBlendEllipse(int cx, int cy, int rx, int ry, uint8_t mode) {
  for (int dy = -ry; dy <= ry; dy++) {
    int h = bbHalf(rx, ry, dy);
    bbSpan(cy + dy, cx - h, cx + h, mode);
  }
}
// A line blended into the canvas, drawn only between rows y0 and y1.
static void bbBlendLine(int x0, int y0, int x1, int y1, int yMin, int yMax, uint8_t mode) {
  int dx = abs(x1 - x0), dy = -abs(y1 - y0);
  int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx + dy;
  for (int guard = 0; guard < 1200; guard++) {
    if (y0 >= yMin && y0 <= yMax) bbBlendPx(x0, y0, mode);
    if (x0 == x1 && y0 == y1) break;
    int e2 = 2 * err;
    if (e2 >= dy) { err += dy; x0 += sx; }
    if (e2 <= dx) { err += dx; y0 += sy; }
  }
}

// ---- what the fight is: the banner's words and colour
static bool bbArenaFight() {
  return btlKind == BK_GYM || btlKind == BK_REMATCH || btlKind == BK_TOWER ||
         btlKind == BK_CHALLENGE || btlKind == BK_LAN;
}

// "GYM 3 1/3": the fight's context, then which of the opponent's creatures this
// is. Exposed for battle_ui_test.
void btlBannerText(char *out, int n) {
  if (btlOver) { snprintf(out, n, "%s", TL(btlWon ? S_BB_WON : S_BB_LOST)); return; }
  char head[24];
  switch (btlKind) {
    case BK_GYM:
      if (btlTrainer >= 0 && btlTrainer < TRAINER_GYMS) snprintf(head, sizeof(head), TL(S_BB_GYM), (unsigned)(btlTrainer + 1));
      else if (btlTrainer == TRAINER_COUNT - 1) snprintf(head, sizeof(head), "%s", TL(S_BB_CHAMP));
      else snprintf(head, sizeof(head), "%s", TL(S_BB_E4));
      break;
    case BK_REMATCH:   snprintf(head, sizeof(head), "%s", TL(S_BB_REMATCH)); break;
    case BK_TOWER:     snprintf(head, sizeof(head), TL(S_BB_TOWER), (unsigned)towerFloor()); break;
    case BK_CHALLENGE: snprintf(head, sizeof(head), "%s", TL(S_BB_CHAL)); break;
    case BK_LAN:       snprintf(head, sizeof(head), "%s", TL(S_BB_LAN)); break;
    case BK_WILD:      snprintf(head, sizeof(head), "%s", TL(S_BB_WILD)); break;
    default:           snprintf(head, sizeof(head), "%s", TL(S_BB_BATTLE)); break;
  }
  uint8_t of = btlLink ? btlFoeSquadN : btlFoeTeamN;
  if (of > 1) snprintf(out, n, "%s %u/%u", head, (unsigned)(btlFoeAt + 1), (unsigned)of);
  else snprintf(out, n, "%s", head);
}

// The banner's trim. Hard mode is gold whatever the fight.
uint16_t btlBannerColour() {
  if (btlOver) return btlWon ? 0x3F2A : 0xF0E4;
  if (btlHard && (btlKind == BK_GYM || btlKind == BK_TOWER)) return 0xFE60;
  switch (btlKind) {
    case BK_GYM: return (btlTrainer >= TRAINER_GYMS) ? 0xB21F : 0xE8E4;
    case BK_REMATCH: return 0xFC80;
    case BK_TOWER: return 0x9A5F;
    case BK_CHALLENGE: return 0x2E7A;
    case BK_LAN: return 0x3D7F;
    case BK_WILD: return 0x4EA8;
  }
  return 0xA534;
}

// Which side's types, and the ailment as a third pill.
static uint8_t bbPillCount(uint8_t who, uint8_t *types) {
  const Combatant &c = who ? btlFoe : btlYou;
  uint8_t n = 0;
  if (c.dex >= 1 && c.dex <= DEX_COUNT) {
    types[n++] = DEX_TBL[c.dex].type1;
    if (DEX_TBL[c.dex].type2 != T_NONE && DEX_TBL[c.dex].type2 != DEX_TBL[c.dex].type1)
      types[n++] = DEX_TBL[c.dex].type2;
  }
  return n;
}
static const StrId BB_AIL_STR[] = { S_AIL_PARA, S_AIL_PARA, S_AIL_BURN, S_AIL_POISON,
                                    S_AIL_SLEEP, S_AIL_FREEZE, S_AIL_CONFUSE };

// Where each part of the battle screen is drawn. One answer for the drawing,
// the tap handlers, the move effects and the tests. False if it is not shown.
//   0 banner  1/2 your/foe card  3/4 your/foe HP bar  5/6 your/foe platform
//   7 FIGHT  8 POKEMON  9 RUN  10 BACK  20 + who*4 + k: side who's pill k
//   (k 0-1 types, 2 the ailment)
bool btlUiRect(uint8_t id, int16_t *x, int16_t *y, int16_t *w, int16_t *h) {
  int X = 0, Y = 0, W = 0, H = 0;
  if (id == 0) {
    char t[32];
    btlBannerText(t, sizeof(t));
    int tw = (int)strlen(t) * 12;
    if (tw > BB_BANNER_MAXW - 40) tw = (int)strlen(t) * 6;
    W = tw + 40; if (W < 124) W = 124;
    X = CX - W / 2; Y = BB_BANNER_Y; H = BB_BANNER_H;
  } else if (id == 1 || id == 2) {
    X = id == 1 ? BB_YOU_CARD_X : BB_FOE_CARD_X;
    Y = id == 1 ? BB_YOU_CARD_Y : BB_FOE_CARD_Y;
    W = BB_CARD_W; H = BB_CARD_H;
  } else if (id == 3 || id == 4) {
    X = (id == 3 ? BB_YOU_CARD_X : BB_FOE_CARD_X) + 34;
    Y = (id == 3 ? BB_YOU_CARD_Y : BB_FOE_CARD_Y) + 21;
    W = BB_CARD_W - 44; H = 10;
  } else if (id == 5 || id == 6) {
    // centred on the stand point the sprites and ui_movefx.ino use
    bool foe = id == 6;
    int rx = foe ? 62 : 74, ry = foe ? 16 : 19;
    int cx = foe ? BTL_FOE_X : BTL_YOU_X, cy = (foe ? BTL_FOE_FEET : BTL_YOU_FEET) + 2;
    X = cx - rx; Y = cy - ry; W = rx * 2 + 1; H = ry * 2 + 1;
  } else if (id == 7) {
    X = BTL_GRID_X; Y = BTL_GRID_Y; W = 328; H = BTL_CELL_H;
  } else if (id == 8 || id == 9) {
    int i = id == 8 ? 2 : 3;
    X = BTL_CELL_X(i); Y = BTL_CELL_Y(i); W = BTL_CELL_W; H = BTL_CELL_H;
  } else if (id == 10) {
    X = BTL_BACK_X; Y = BTL_BACK_Y; W = BTL_BACK_W; H = BTL_BACK_H;
  } else if (id >= 20 && id < 28) {
    uint8_t who = (id - 20) / 4, k = (id - 20) % 4;
    uint8_t ty[2];
    uint8_t n = bbPillCount(who, ty);
    const Combatant &c = who ? btlFoe : btlYou;
    if (k == 3 || (k < 2 && k >= n) || (k == 2 && (c.ailment == AIL_NONE || c.ailment >= 7))) return false;
    X = (who ? BB_FOE_CARD_X : BB_YOU_CARD_X) + 6;
    Y = (who ? BB_FOE_CARD_Y : BB_YOU_CARD_Y) + BB_CARD_H + 4;
    for (uint8_t j = 0; j < k && j < n; j++) X += (int)strlen(typeName(ty[j])) * 6 + 14 + 4;
    const char *lab = k < 2 ? typeName(ty[k]) : T(BB_AIL_STR[c.ailment]);
    W = (int)strlen(lab) * 6 + 14; H = BB_PILL_H;
  } else {
    return false;
  }
  *x = X; *y = Y; *w = W; *h = H;
  return true;
}

// The colour an HP bar is drawn in: green over half, amber over a quarter.
uint16_t btlHpColour(uint16_t shown, uint16_t maxHp) {
  if ((uint32_t)shown * 2 > maxHp) return BB_HP_OK;
  if ((uint32_t)shown * 4 > maxHp) return BB_HP_WARN;
  return BB_HP_BAD;
}

static void bbText(const char *s, int x, int y, uint8_t size, uint16_t col, int16_t shadow) {
  gfx->setTextSize(size);
  if (shadow >= 0) {
    gfx->setTextColor((uint16_t)shadow);
    gfx->setCursor(x + 1, y + 1);
    gfx->print(s);
  }
  gfx->setTextColor(col);
  gfx->setCursor(x, y);
  gfx->print(s);
}

// ---- the banner: a framed pill with diamond end caps
static void bbBanner(int x, int y, int w, int h, uint16_t trim, const char *t, uint8_t size) {
  int my = y + h / 2;
  // diamond caps first, so the pill sits over their inner halves
  for (int s = 0; s < 2; s++) {
    int ex = s ? x + w - 2 : x + 2;
    gfx->fillTriangle(ex - 11, my, ex, my - 11, ex + 11, my, trim);
    gfx->fillTriangle(ex - 11, my, ex, my + 11, ex + 11, my, trim);
    gfx->fillTriangle(ex - 6, my, ex, my - 6, ex + 6, my, 0xFFFF);
    gfx->fillTriangle(ex - 6, my, ex, my + 6, ex + 6, my, 0xFFFF);
  }
  gfx->fillRoundRect(x + 4, y, w - 8, h, h / 2, trim);
  gfx->fillRoundRect(x + 7, y + 3, w - 14, h - 6, (h - 6) / 2, BB_NAVY);
  gfx->drawFastHLine(x + 14, y + 5, w - 28, 0x3A70);
  gfx->fillCircle(x + 15, my, 2, trim);
  gfx->fillCircle(x + w - 16, my, 2, trim);
  int tw = (int)strlen(t) * 6 * size;
  bbText(t, x + (w - tw) / 2, my - 4 * size + 1, size, 0xFFFF, BB_NAVY_D);
}

static void bbDrawBanner() {
  int16_t x, y, w, h;
  btlUiRect(0, &x, &y, &w, &h);
  char t[32];
  btlBannerText(t, sizeof(t));
  uint8_t size = ((int)strlen(t) * 12 > BB_BANNER_MAXW - 40) ? 1 : 2;
  bbBanner(x, y, w, h, btlBannerColour(), t, size);
}

// ---- the ground: vignette top and bottom, a perspective grid in an arena,
// and a platform under each creature
#define BB_SCENE_Y0 30
#define BB_HORIZON 118
static void bbDrawPlatform(uint8_t id) {
  int16_t x, y, w, h;
  btlUiRect(id, &x, &y, &w, &h);
  int rx = w / 2, ry = h / 2, cx = x + rx, cy = y + ry;
  bbBlendEllipse(cx + 4, cy + 5, rx + 4, ry + 3, 2);           // soft shadow
  bbFillEllipse(cx, cy + 2, rx, ry, BB_PLAT_RIM);               // the platform's side
  bbFillEllipse(cx, cy, rx - 1, ry - 2, BB_PLAT);               // its top
  bbFillEllipse(cx - rx / 8, cy - 1, rx * 5 / 8, ry * 4 / 7, BB_PLAT_HI);
}

static void bbDrawGround() {
  // fade the backdrop's top edge into the bezel, and its bottom into the band
  for (int i = 0; i < 12; i++) {
    uint8_t q = i < 4 ? 3 : (i < 8 ? 2 : 1);
    bbSpan(BB_SCENE_Y0 + i, 0, 465, q);
    bbSpan(BTL_SCENE_Y1 - 1 - i, 0, 465, q);
  }
  if (bbArenaFight()) {
    // an arena floor: lines toward a vanishing point above the centre, and
    // rungs that crowd together toward the horizon
    const int vx = CX, vy = BB_HORIZON - 24;
    for (int k = -6; k <= 6; k++)
      bbBlendLine(vx, vy, CX + k * 96, BTL_SCENE_Y1 + 30, BB_HORIZON + 1, BTL_SCENE_Y1 - 13, 5);
    static const uint8_t RUNG[] = { 0, 8, 19, 34, 55, 84 };
    for (uint8_t i = 0; i < sizeof(RUNG); i++) {
      int ry = BB_HORIZON + RUNG[i];
      if (ry >= BTL_SCENE_Y1 - 12) break;
      bbSpan(ry, 0, 465, i ? 4 : 5);
    }
  }
  bbDrawPlatform(6);
  bbDrawPlatform(5);
}

// ---- the HP cards
static void bbHpBar(int x, int y, int w, uint16_t shown, uint16_t maxHp) {
  gfx->fillRoundRect(x - 1, y - 1, w + 2, 12, 5, BB_NAVY);
  gfx->fillRoundRect(x + 1, y + 1, w - 2, 8, 3, BB_HP_TRACK);
  int fw = maxHp ? (int)((int32_t)(w - 2) * shown / maxHp) : 0;
  if (fw > 0) {
    uint16_t col = btlHpColour(shown, maxHp);
    gfx->fillRoundRect(x + 1, y + 1, fw, 8, 3, col);
    if (fw > 4) gfx->drawFastHLine(x + 3, y + 2, fw - 4, bbLight(bbLight(col)));
  }
}

static void bbPill(int x, int y, int w, uint16_t fill, const char *t, uint16_t ink) {
  gfx->fillRoundRect(x, y, w, BB_PILL_H, 7, BB_NAVY);
  gfx->fillRoundRect(x + 1, y + 1, w - 2, BB_PILL_H - 2, 6, fill);
  gfx->drawFastHLine(x + 5, y + 2, w - 10, bbLight(fill));
  gfx->setTextSize(1);
  gfx->setTextColor(ink);
  gfx->setCursor(x + 7, y + 4);
  gfx->print(t);
}

static void btlCard(const Combatant &c, uint8_t who, uint16_t shown) {
  int16_t x, y, w, h;
  btlUiRect(who ? 2 : 1, &x, &y, &w, &h);
  for (int r = 0; r < h; r++) bbSpan(y + 4 + r, x + 4, x + w + 3, 2);   // drop shadow
  gfx->fillRoundRect(x, y, w, h, 9, BB_NAVY);
  gfx->fillRoundRect(x + 2, y + 2, w - 4, h - 4, 7, BB_CARD);
  gfx->drawFastHLine(x + 10, y + 3, w - 20, 0xFFFF);
  // the side's stripe: red for the opponent, blue for you
  gfx->fillRoundRect(x + 5, y + 8, 4, h - 16, 2, who ? BB_RED : BB_BLUE);

  char l[16];
  bbText(c.name, x + 14, y + 8, 1, UI_INK, -1);
  bbText(c.name, x + 15, y + 8, 1, UI_INK, -1);   // one pixel wider: bold
  snprintf(l, sizeof(l), "Lv%u", c.level);
  bbText(l, x + w - 10 - (int)strlen(l) * 6, y + 8, 1, UI_INK, -1);

  // HP: the yellow-on-dark label the games use, then the bar
  gfx->fillRoundRect(x + 13, y + 19, 19, 13, 3, BB_NAVY);
  bbText("HP", x + 17, y + 22, 1, 0xFEA0, -1);
  int16_t bx, by, bw, bh;
  btlUiRect(who ? 4 : 3, &bx, &by, &bw, &bh);
  bbHpBar(bx, by, bw, shown, c.maxHp);

  snprintf(l, sizeof(l), "%u/%u", shown, c.maxHp);
  bbText(l, x + w - 10 - (int)strlen(l) * 12, y + 35, 2, UI_INK, -1);

  // the type pills, and a status as a pill of its own
  uint8_t ty[2];
  uint8_t n = bbPillCount(who, ty);
  for (uint8_t k = 0; k < 3; k++) {
    int16_t px, py, pw, ph;
    if (!btlUiRect(20 + who * 4 + k, &px, &py, &pw, &ph)) continue;
    if (k < n) bbPill(px, py, pw, typeColor(ty[k]), typeName(ty[k]),
                      typeColorIsLight(ty[k]) ? UI_INK : 0xFFFF);
    else bbPill(px, py, pw, 0x8010, T(BB_AIL_STR[c.ailment]), 0xFFFF);
  }
}

// ---- the menu band and its buttons
static void bbDrawBand() {
  // a deep blue band, darker toward the bottom, under a red rule
  static const uint16_t BAND[6] = { 0x2A4F, 0x220D, 0x21CB, 0x1989, 0x1147, 0x1105 };
  for (int i = 0; i < 6; i++) {
    int y0 = BTL_SCENE_Y1 + i * 36;
    gfx->fillRect(0, y0, 466, 36, BAND[i]);
  }
  gfx->fillRect(0, BTL_SCENE_Y1, 466, 3, 0xE8E4);
  gfx->drawFastHLine(0, BTL_SCENE_Y1 + 3, 466, 0x7A08);
}

// A chunky button: a darker underside, the face, a highlight, a dark rim.
static void bbChunky(int x, int y, int w, int h, uint16_t face, uint16_t under, uint16_t hi) {
  gfx->fillRoundRect(x, y + 2, w, h - 2, 11, BB_NAVY_D);
  gfx->fillRoundRect(x + 1, y + 3, w - 2, h - 4, 10, under);
  gfx->fillRoundRect(x + 1, y + 1, w - 2, h - 7, 10, face);
  gfx->drawFastHLine(x + 12, y + 4, w - 24, hi);
  gfx->drawFastHLine(x + 12, y + 5, w - 24, hi);
  gfx->drawRoundRect(x, y, w, h, 11, BB_NAVY_D);
}

enum : uint8_t { BBI_SWORD, BBI_BALL, BBI_RUN, BBI_BACK };
static void bbIcon(uint8_t which, int cx, int cy, uint16_t ink) {
  if (which == BBI_SWORD) {
    for (int k = -1; k <= 1; k++) gfx->drawLine(cx - 8 + k, cy + 8, cx + 8 + k, cy - 8, ink);
    gfx->drawLine(cx - 8, cy, cx, cy + 8, ink);
    gfx->drawLine(cx - 7, cy, cx + 1, cy + 8, ink);
    gfx->fillCircle(cx - 9, cy + 9, 2, ink);
  } else if (which == BBI_BALL) {
    gfx->fillCircle(cx, cy, 9, BB_NAVY_D);
    gfx->fillCircle(cx, cy, 8, 0xFFFF);
    for (int dy = -8; dy < 0; dy++) {
      int h = bbHalf(8, 8, dy);
      gfx->drawFastHLine(cx - h, cy + dy, h * 2 + 1, BB_RED);
    }
    gfx->drawFastHLine(cx - 8, cy, 17, BB_NAVY_D);
    gfx->fillCircle(cx, cy, 3, BB_NAVY_D);
    gfx->fillCircle(cx, cy, 2, 0xFFFF);
  } else if (which == BBI_RUN) {
    gfx->fillRect(cx - 9, cy - 2, 11, 5, ink);
    gfx->fillTriangle(cx + 1, cy - 8, cx + 10, cy, cx + 1, cy + 8, ink);
  } else {
    gfx->fillRect(cx - 2, cy - 2, 11, 5, ink);
    gfx->fillTriangle(cx - 1, cy - 8, cx - 10, cy, cx - 1, cy + 8, ink);
  }
}

static void bbLabelled(int x, int y, int w, int h, uint8_t icon, const char *t, const char *sub,
                       uint16_t ink, int16_t shadow) {
  int tw = (int)strlen(t) * 12 + 26 + (sub ? (int)strlen(sub) * 6 + 6 : 0);
  int lx = x + (w - tw) / 2;
  int my = y + (h - 5) / 2;
  bbIcon(icon, lx + 9, my, ink);
  bbText(t, lx + 26, my - 7, 2, ink, shadow);
  if (sub) bbText(sub, lx + 26 + (int)strlen(t) * 12 + 6, my - 1, 1, ink, -1);
}

// FIGHT (red) across the top, then POKEMON (orange, with how many can still
// fight) and RUN (white). The same three cells the taps have always used.
static void bbDrawCommands() {
  int16_t x, y, w, h;
  btlUiRect(7, &x, &y, &w, &h);
  bbChunky(x, y, w, h, BB_RED, BB_RED_D, BB_RED_L);
  bbLabelled(x, y, w, h, BBI_SWORD, T(S_FIGHT), nullptr, 0xFFFF, BB_RED_D);

  uint8_t alive = 0;
  for (uint8_t i = 0; i < btlSquadN; i++) {
    const Combatant &m = (i == btlSquadAt) ? btlYou : btlSquad[i];
    if (!m.fainted()) alive++;
  }
  char sub[8];
  snprintf(sub, sizeof(sub), "%u/%u", alive, btlSquadN);
  btlUiRect(8, &x, &y, &w, &h);
  bbChunky(x, y, w, h, BB_ORANGE, BB_ORANGE_D, BB_ORANGE_L);
  bbLabelled(x, y, w, h, BBI_BALL, T(S_BTL_SWITCH), sub, 0xFFFF, BB_ORANGE_D);

  btlUiRect(9, &x, &y, &w, &h);
  bbChunky(x, y, w, h, BB_PALE, BB_PALE_D, 0xFFFF);
  bbLabelled(x, y, w, h, BBI_RUN, T(S_BTL_RUN), nullptr, BB_NAVY, -1);
}

// BACK out of the move and switch lists. Its own word: S_BACK reads "tap: back",
// which is right for a hint line and odd on a button.
static void bbDrawBack() {
  int16_t x, y, w, h;
  btlUiRect(10, &x, &y, &w, &h);
  bbChunky(x, y, w, h, BB_BLUE, BB_BLUE_D, BB_BLUE_L);
  bbLabelled(x, y, w, h, BBI_BACK, TL(S_BB_BACK), nullptr, 0xFFFF, BB_BLUE_D);
}

// One move: its type's colour, the name large, then type and power.
static void bbDrawMove(int i, uint8_t mv, int16_t dex) {
  int x = BTL_CELL_X(i), y = BTL_CELL_Y(i);
  if (!mv) {
    gfx->fillRoundRect(x, y + 2, BTL_CELL_W, BTL_CELL_H - 2, 11, BB_NAVY_D);
    gfx->drawRoundRect(x, y + 2, BTL_CELL_W, BTL_CELL_H - 2, 11, 0x3A50);
    bbText("-", x + BTL_CELL_W / 2 - 3, y + 18, 1, 0x5AF1, -1);
    return;
  }
  const MoveEntry &m = MOVE_TBL[mv];
  uint16_t face = typeColor(m.type);
  bool light = typeColorIsLight(m.type);
  bbChunky(x, y, BTL_CELL_W, BTL_CELL_H, face, bbDark(face, 2), bbLight(bbLight(face)));
  uint16_t ink = light ? UI_INK : 0xFFFF;
  int16_t sh = light ? -1 : (int16_t)bbDark(face, 2);
  uint8_t sz = (int)strlen(m.name) * 12 <= BTL_CELL_W - 16 ? 2 : 1;
  bbText(m.name, x + 9, y + (sz == 2 ? 7 : 10), sz, ink, sh);
  bbText(typeName(m.type), x + 10, y + 27, 1, ink, -1);
  char p[12];
  if (m.cat == MC_STATUS) snprintf(p, sizeof(p), "STATUS");
  else snprintf(p, sizeof(p), "POW %u", m.power);
  bool stab = hasStab(dex, m.type) && m.cat != MC_STATUS;
  int px = x + BTL_CELL_W - 10 - (int)strlen(p) * 6;
  bbText(p, px, y + 27, 1, ink, -1);
  if (stab) {   // same type as the creature: the moves that hit hardest
    gfx->fillCircle(px - 7, y + 30, 3, BB_NAVY_D);
    gfx->fillCircle(px - 7, y + 30, 2, 0xFFE0);
  }
}

// A creature in the switch list: name, a small bar, its numbers.
static void bbDrawSwitch(int i, const Combatant &m, bool usable, bool current) {
  int x = BTL_CELL_X(i), y = BTL_CELL_Y(i);
  if (usable) bbChunky(x, y, BTL_CELL_W, BTL_CELL_H, BB_PALE, BB_PALE_D, 0xFFFF);
  else {
    gfx->fillRoundRect(x, y + 2, BTL_CELL_W, BTL_CELL_H - 2, 11, current ? 0x2A6F : BB_NAVY_D);
    gfx->drawRoundRect(x, y + 2, BTL_CELL_W, BTL_CELL_H - 2, 11, current ? BB_BLUE_L : 0x3A50);
  }
  uint16_t ink = usable ? BB_NAVY : (current ? 0xFFFF : 0x7BCF);
  bbText(m.name, x + 10, y + 9, 1, ink, -1);
  char hp[12];
  snprintf(hp, sizeof(hp), "%u/%u", m.hp, m.maxHp);
  bbText(hp, x + BTL_CELL_W - 10 - (int)strlen(hp) * 6, y + 9, 1, ink, -1);
  bbHpBar(x + 10, y + 24, BTL_CELL_W - 20, m.hp, m.maxHp);
}

// The narration box: navy frame, light face, and a bobbing arrow when a tap
// moves it on. `tone` tints the frame at the end of a fight.
static void bbMsgBox(int x, int y, int w, int h, uint16_t tone, bool more) {
  gfx->fillRoundRect(x, y, w, h, 14, BB_NAVY_D);
  gfx->fillRoundRect(x + 2, y + 2, w - 4, h - 4, 12, tone);
  gfx->fillRoundRect(x + 6, y + 6, w - 12, h - 12, 9, BB_CARD);
  gfx->drawRoundRect(x + 6, y + 6, w - 12, h - 12, 9, BB_PALE_D);
  if (more) {
    int bob = (int)((millis() / 160) % 4);
    if (bob == 3) bob = 1;
    int ax = x + w - 26, ay = y + h - 22 + bob;
    gfx->fillTriangle(ax - 6, ay, ax + 6, ay, ax, ay + 7, BB_RED);
  }
}

// The win screen's way out, the same blue button. A tap anywhere still leaves.
static void bbWinBack(int y) {
  const char *t = TL(S_BB_BACK);
  int w = (int)strlen(t) * 12 + 60;
  bbChunky(CX - w / 2, y, w, UI_TAP_MIN, BB_BLUE, BB_BLUE_D, BB_BLUE_L);
  bbLabelled(CX - w / 2, y, w, UI_TAP_MIN, BBI_BACK, t, nullptr, 0xFFFF, BB_BLUE_D);
}
