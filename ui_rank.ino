// ---------- trainer rank: the player card's rank page, scene themes, notice ----------
// The rules are in rank.cpp (rankPoints / rankOf / rankAvatarUnlocked /
// rankThemeUnlocked); this tab only draws them and turns taps into calls.
// Design notes: docs/adventure-rank.md.

static const bool s_rankPetBound __attribute__((unused)) = rankBindPet(&pet);

static const char *const S_RK_RANK[LANG_COUNT] = { "RANGO %u", "RANK %u", "RANG %u", "RANG %u", "GRADO %u", "PATENTE %u" };
static const char *const S_RK_UP[LANG_COUNT]   = { "SUBES DE RANGO!", "RANK UP!", "NOUVEAU RANG!", "RANG AUF!", "NUOVO GRADO!", "NOVA PATENTE!" };
static const char *const S_RK_MAX[LANG_COUNT]  = { "RANGO MAXIMO", "MAX RANK", "RANG MAX", "MAX. RANG", "GRADO MAX", "PATENTE MAX" };
static const char *const S_RK_SHORT[LANG_COUNT] = { "R%u", "R%u", "R%u", "R%u", "G%u", "P%u" };

#define RANK_GOLD C565(0xff, 0xc8, 0x3a)
#define RANK_AV_SCALE 3
#define RANK_AV_Y0 134
#define RANK_AV_ROWH 66
#define RANK_TH_Y 280
#define RANK_TH_W 92
#define RANK_TH_H 44

// Where avatar i and theme t sit on the rank page (centre). Exposed so the
// tests tap exactly what is drawn rather than a copy of these numbers.
void rankAvatarCell(uint8_t i, int16_t &x, int16_t &y) {
  x = CX - 108 + (i % 4) * 72;
  y = RANK_AV_Y0 + (i / 4) * RANK_AV_ROWH + 24;
}
void rankThemeCell(uint8_t t, int16_t &x, int16_t &y) {
  x = CX - 193 + t * (RANK_TH_W + 6) + RANK_TH_W / 2;
  y = RANK_TH_Y + RANK_TH_H / 2;
}

static void rankCentered(const char *s, int y, uint8_t size, uint16_t col) {
  gfx->setTextColor(col);
  gfx->setTextSize(size);
  gfx->setCursor(CX - (int)strlen(s) * 3 * size, y);
  gfx->print(s);
}

static void rankSilhouette(uint8_t which, int x, int y, int s, uint16_t col) {
  const AvatarArt &a = AVATARS[which % AVATAR_COUNT];
  for (int r = 0; r < AVATAR_PX; r++)
    for (int c = 0; c < AVATAR_PX; c++)
      if (a.idx[r * AVATAR_PX + c] != 0xFF) gfx->fillRect(x + c * s, y + r * s, s, s, col);
}

// The last page of the player card. renderPlayer() draws the background, the
// page dots, BACK and the flush.
void rankRenderPage() {
  uint8_t rk = rankShown();
  const char *t = rankTitle(rk);
  rankCentered(t, 40, strlen(t) <= 14 ? 3 : 2, UI_INK);

  char l[32];
  snprintf(l, sizeof(l), TL(S_RK_RANK), rk);
  rankCentered(l, 72, 2, UI_INK);

  // progress toward the next rank, from the points that rank started at
  uint32_t pts = rankPoints();
  int bx = CX - 120, bw = 240, by = 96;
  gfx->fillRoundRect(bx, by, bw, 12, 6, UI_TRACK);
  if (rk >= RANK_MAX) {
    gfx->fillRoundRect(bx, by, bw, 12, 6, RANK_GOLD);
    snprintf(l, sizeof(l), "%s  %lu", TL(S_RK_MAX), (unsigned long)pts);
  } else {
    uint32_t lo = rankThreshold(rk), hi = rankThreshold(rk + 1);
    uint32_t in = pts > lo ? pts - lo : 0;
    int w = (int)((uint32_t)bw * (in < hi - lo ? in : hi - lo) / (hi - lo));
    if (w > 0) gfx->fillRoundRect(bx, by, w < 12 ? 12 : w, 12, 6, UI_BAR_OK);
    snprintf(l, sizeof(l), "%lu / %lu PTS", (unsigned long)pts, (unsigned long)hi);
  }
  gfx->drawRoundRect(bx, by, bw, 12, 6, UI_INK);
  rankCentered(l, 114, 1, UI_INK);

  // avatars: locked ones greyed with the rank they need
  for (uint8_t i = 0; i < AVATAR_COUNT; i++) {
    int16_t cx, cy;
    rankAvatarCell(i, cx, cy);
    int ax = cx - AVATAR_PX * RANK_AV_SCALE / 2, ay = cy - 24;
    bool open = rankAvatarUnlocked(i);
    if (i == pet.avatar) gfx->drawRoundRect(cx - 34, ay - 4, 68, 64, 8, UI_INK);
    if (open) drawAvatar(i, ax, ay, RANK_AV_SCALE);
    else rankSilhouette(i, ax, ay, RANK_AV_SCALE, UI_TRACK);
    char tag[12];
    if (open) snprintf(tag, sizeof(tag), "%s", AVATARS[i].name);
    else snprintf(tag, sizeof(tag), TL(S_RK_SHORT), rankAvatarNeeds(i));
    gfx->setTextColor(open ? UI_INK : UI_TRACK);
    gfx->setTextSize(1);
    gfx->setCursor(cx - (int)strlen(tag) * 3, ay + 51);
    gfx->print(tag);
  }

  // home scene themes
  for (uint8_t th = 0; th < THEME_COUNT; th++) {
    int16_t cx, cy;
    rankThemeCell(th, cx, cy);
    int x = cx - RANK_TH_W / 2;
    bool open = rankThemeUnlocked(th), sel = th == rankTheme();
    gfx->fillRoundRect(x, RANK_TH_Y, RANK_TH_W, RANK_TH_H, 10, sel ? UI_BAR_OK : (open ? UI_BG_DAY : UI_TRACK));
    gfx->drawRoundRect(x, RANK_TH_Y, RANK_TH_W, RANK_TH_H, 10, UI_INK);
    const char *nm = rankThemeName(th);
    gfx->setTextColor(sel ? UI_WHITE : UI_INK);
    gfx->setTextSize(1);
    gfx->setCursor(cx - (int)strlen(nm) * 3, open ? RANK_TH_Y + 18 : RANK_TH_Y + 11);
    gfx->print(nm);
    if (!open) {
      char tag[12];
      snprintf(tag, sizeof(tag), TL(S_RK_SHORT), rankThemeNeeds(th));
      gfx->setCursor(cx - (int)strlen(tag) * 3, RANK_TH_Y + 26);
      gfx->print(tag);
    }
  }
}

// A tap on the rank page. True = it landed on an avatar or a theme (picked if
// unlocked, refused if not); false = anywhere else, which backs out as before.
bool rankPageTap(int16_t x, int16_t y) {
  for (uint8_t i = 0; i < AVATAR_COUNT; i++) {
    int16_t cx, cy;
    rankAvatarCell(i, cx, cy);
    if (x > cx - 36 && x < cx + 36 && y > cy - 30 && y < cy + 36) {
      if (!rankAvatarUnlocked(i)) { sfxPlay(SFX_DENY); return true; }
      pet.avatar = i;
      pet.flushSave();
      sfxPlay(SFX_TAP);
      return true;
    }
  }
  for (uint8_t th = 0; th < THEME_COUNT; th++) {
    int16_t cx, cy;
    rankThemeCell(th, cx, cy);
    if (x > cx - RANK_TH_W / 2 && x < cx + RANK_TH_W / 2 && y >= RANK_TH_Y && y < RANK_TH_Y + RANK_TH_H) {
      sfxPlay(rankSetTheme(th) ? SFX_TAP : SFX_DENY);
      return true;
    }
  }
  return false;
}

// ---------- the home scene themes (called from drawScene) ----------
// STARRY dresses the scene as night whatever the hour: moon, stars, dark soil.
bool rankSceneNight(bool night) {
  return night || rankTheme() == THEME_STARRY;
}

// SUNSET repaints the daytime sky; the night sky stays night.
void rankSceneSky(uint16_t &top, uint16_t &bot, bool night) {
  if (rankTheme() == THEME_SUNSET && !night) {
    top = C565(0x6a, 0x3a, 0x8a);
    bot = C565(0xff, 0x9a, 0x5a);
  }
}

// Drawn last in drawScene, under the weather, the header and the pet. Cheap:
// a dozen rects or triangles.
void rankSceneExtras(uint32_t now, bool night) {
  uint8_t th = rankTheme();
  if (th == THEME_STARRY) {   // a second, twinkling field over the fixed stars
    static const uint16_t TW[][2] = { {60,120},{200,60},{250,150},{420,140},{140,190},{310,180},{390,60},{30,180} };
    for (uint8_t k = 0; k < 8; k++)
      if ((now / 500 + k * 3) % 4) gfx->fillRect(TW[k][0], TW[k][1], 3, 3, C565(0xff, 0xf1, 0xc8));
  } else if (th == THEME_FESTIVAL) {   // bunting strung across the sky
    static const uint16_t FLAG[5] = { C565(0xe8, 0x4a, 0x4a), C565(0xf5, 0xc2, 0x3a), C565(0x4a, 0xb8, 0x5a),
                                      C565(0x4a, 0x8a, 0xe8), C565(0xc8, 0x5a, 0xd8) };
    uint16_t line = night ? C565(0x8a, 0x8a, 0x9a) : C565(0x5a, 0x4a, 0x3a);
    for (int x = 0; x < 466; x += 34) {
      int d = x - 233;
      int y = 104 + 24 - d * d * 24 / (233 * 233);   // sags to 128 in the middle
      gfx->fillRect(x, y, 34, 2, line);
      gfx->fillTriangle(x + 4, y + 2, x + 28, y + 2, x + 16, y + 20, FLAG[(x / 34) % 5]);
    }
  }
}

// ---------- the rank-up notice (featMainOverlay) ----------
void rankMainNotice() {
  // the MEDAL!/streak popup sits in the same place: wait, the clock not started
  if (pet.showMedal() || pet.showMilestone()) return;
  if (!rankNoticeActive(millis())) return;
  gfx->fillRoundRect(83, 148, 300, 62, 14, RANK_GOLD);
  gfx->drawRoundRect(83, 148, 300, 62, 14, UI_INK);
  rankCentered(TL(S_RK_UP), 156, 2, UI_INK);
  const char *t = rankTitle(rankShown());
  rankCentered(t, 182, 2, UI_INK);
}
