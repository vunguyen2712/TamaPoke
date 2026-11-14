// ---------- The Pokedex habitat view (feature 8). Logic: habitat.h ----------
// Reached from the Pokedex grid's HABITAT pill: for the region on show, the
// species still MISSING from the player's Pokedex, five to a page, each with
// where and when it is most likely in the wild -- or how else it is obtained.
// Tapping a row opens that species' detail page (which also carries its
// habitat line); backing out of the detail lands here again.
//
// It is a sub-screen of the gallery: galleryOpen stays set and galleryRegion
// is the region shown, so the gallery's own back-out paths keep working.
// Horizontal swipe pages and backs out to the grid past either end; vertical
// changes region, as on the grid. A region without its pack shows LOCKED.
//
// Names: this is a HINT list. It never shows a name the gallery hides -- a
// missing species is "N.092 ???" with its silhouette, exactly as the gallery
// draws it; what it adds is where to look. Design: docs/adventure-habitat.md.
#include "habitat.h"

bool habitatOpen = false;
uint8_t habitatPage = 0;
static HabTotals gHabTot;   // the region's totals, recomputed on a region change

#define HAB_PER_PAGE 5
#define HAB_ROW_Y(i) (74 + (i) * 62)
#define HAB_ROW_H 56
#define HAB_ROW_X 70
#define HAB_ROW_W 326
#define HAB_TXT_X 128
#define HAB_TXT_CH 22         // characters a row line holds at size 2
#define HAB_BACK_Y 392        // = FEAT_BACK_Y (ui_hub.ino; that tab comes after this one)
#define HAB_PAGE_Y 440
// the HABITAT pill on the Pokedex grid, beside the page number
#define HAB_BTN_X 200
#define HAB_BTN_Y 410
#define HAB_BTN_W 128
#define HAB_BTN_H 32

static const char *const S_HB_BTN[LANG_COUNT]  = { "HABITAT", "HABITAT", "HABITAT", "HABITAT", "HABITAT", "HABITAT" };
static const char *const S_HB_HEAD[LANG_COUNT] = { "%s: %u ???", "%s: %u ???", "%s: %u ???", "%s: %u ???", "%s: %u ???", "%s: %u ???" };
static const char *const S_HB_EGG[LANG_COUNT]  = { "SOLO EN HUEVO", "EGG ONLY", "OEUF SEULEMENT", "NUR AUS EI", "SOLO DA UOVO", "SO EM OVO" };
static const char *const S_HB_EVO[LANG_COUNT]  = { "SOLO EVOLUCION", "EVOLVE ONLY", "EVOLUTION SEULE", "NUR ENTWICKLUNG", "SOLO EVOLUZIONE", "SO EVOLUCAO" };
static const char *const S_HB_NOCARD[LANG_COUNT] = { "NO ESTA EN LA SD", "NOT ON THIS CARD", "PAS SUR LA CARTE", "NICHT AUF KARTE", "NON SULLA SCHEDA", "NAO ESTA NO SD" };
static const char *const S_HB_ONLY[LANG_COUNT] = { "SOLO %s", "%s ONLY", "%s SEUL.", "NUR %s", "SOLO %s", "SO %s" };
static const char *const S_HB_LVL[LANG_COUNT]  = { "Nv.%u+", "Lv.%u+", "Niv.%u+", "Lv.%u+", "Lv.%u+", "Niv.%u+" };
static const char *const S_HB_DONE[LANG_COUNT] = { "COMPLETO!", "COMPLETE!", "COMPLET!", "KOMPLETT!", "COMPLETO!", "COMPLETO!" };
static const char *const S_HB_WX[WX_COUNT][LANG_COUNT] = {
  { "CLARO", "CLEAR", "CLAIR", "KLAR", "SERENO", "LIMPO" },
  { "SOL", "SUN", "SOLEIL", "SONNE", "SOLE", "SOL" },
  { "LLUVIA", "RAIN", "PLUIE", "REGEN", "PIOGGIA", "CHUVA" },
  { "NIEVE", "SNOW", "NEIGE", "SCHNEE", "NEVE", "NEVE" },
  { "NIEBLA", "FOG", "BRUME", "NEBEL", "NEBBIA", "NEVOA" },
  { "VIENTO", "WIND", "VENT", "WIND", "VENTO", "VENTO" },
};
static const char *const S_HB_WT[WT_COUNT][LANG_COUNT] = {
  { "MANANA", "MORNING", "MATIN", "MORGEN", "MATTINA", "MANHA" },
  { "DIA", "DAY", "JOUR", "TAG", "GIORNO", "DIA" },
  { "TARDE", "EVENING", "SOIR", "ABEND", "SERA", "TARDE" },
  { "NOCHE", "NIGHT", "NUIT", "NACHT", "NOTTE", "NOITE" },
};

// ---------- text (no gfx) ----------

// "<weather>[/<weather>], <window>[/<window>]" -- the best of each.
static void habBest(const HabInfo &h, char *out, size_t n) {
  char wx[24] = "", wt[24] = "";
  if (h.bestWx != 0xFF) {
    if (h.bestWx2 != 0xFF) snprintf(wx, sizeof(wx), "%s/%s", TL(S_HB_WX[h.bestWx]), TL(S_HB_WX[h.bestWx2]));
    else snprintf(wx, sizeof(wx), "%s", TL(S_HB_WX[h.bestWx]));
  }
  if (h.bestTod != 0xFF) {
    if (h.bestTod2 != 0xFF) snprintf(wt, sizeof(wt), "%s/%s", TL(S_HB_WT[h.bestTod]), TL(S_HB_WT[h.bestTod2]));
    else snprintf(wt, sizeof(wt), "%s", TL(S_HB_WT[h.bestTod]));
  }
  snprintf(out, n, "%s, %s", wx, wt);
}

// "NIGHT/FOG ONLY": the windows open in every weather, then the weathers open
// in every window. Empty when nothing holds it back.
static void habOnly(const HabInfo &h, char *out, size_t n) {
  out[0] = 0;
  if (!h.restricted) return;
  char list[40] = "";
  for (uint8_t t = 0; t < WT_COUNT; t++)
    if (h.onlyTod & (1u << t)) {
      size_t l = strlen(list);
      snprintf(list + l, sizeof(list) - l, "%s%s", l ? "/" : "", TL(S_HB_WT[t]));
    }
  for (uint8_t w = 0; w < WX_COUNT; w++)
    if (h.onlyWx & (1u << w)) {
      size_t l = strlen(list);
      snprintf(list + l, sizeof(list) - l, "%s%s", l ? "/" : "", TL(S_HB_WX[w]));
    }
  snprintf(out, n, TL(S_HB_ONLY), list);
}

// The one-line summary a row of the list shows (and the detail's first line).
// Non-static with primitive types only, so the tests can read it.
void habitatSummary(int16_t dex, char *out, size_t n) {
  HabInfo h = habitatOf(dex, gHabTot);
  switch (h.how) {
    case HB_EGG: snprintf(out, n, "%s", TL(S_HB_EGG)); return;
    case HB_EVOLVE: snprintf(out, n, "%s", TL(S_HB_EVO)); return;
    case HB_NOCARD: snprintf(out, n, "%s", TL(S_HB_NOCARD)); return;
  }
  if (h.restricted) habOnly(h, out, n);
  else habBest(h, out, n);
}
// The lowest wild level, 0 if it is not met in the wild or appears at any level.
uint8_t habitatMinLevel(int16_t dex) {
  HabInfo h = habitatOf(dex, gHabTot);
  return (h.how == HB_WILD && h.minLevel > 1) ? h.minLevel : 0;
}

uint8_t habitatPageCount() {
  if (!regionAvailable(galleryRegion % GAL_REGIONS)) return 1;
  uint16_t n = habitatMissingCount(pet, galleryRegion % GAL_REGIONS);
  return n ? (uint8_t)((n + HAB_PER_PAGE - 1) / HAB_PER_PAGE) : 1;
}

// ---------- state ----------

// Is the habitat list the screen on the panel? It is dropped whenever the
// gallery is closed or back on its region chooser, so re-opening the Pokedex
// can never land on a stale habitat page.
bool habitatShown() {
  if (!galleryOpen || galleryPick) { habitatOpen = false; return false; }
  return habitatOpen && !galleryDetail;
}

void habitatOpenFromGallery() {
  habitatOpen = true;
  habitatPage = 0;
  gHabTot.region = 0xFF;   // packs may have arrived since: count afresh
}
static void habitatBack() {
  habitatOpen = false;
  habitatPage = 0;
  galleryDirty = true;     // the grid only redraws when dirty
}

// ---------- drawing ----------

// A thumbnail as a one-colour silhouette at 1x, one span per run of pixels.
static void habSilhouette(int16_t dex, int x, int y, uint16_t col) {
  const uint8_t *b = thumbs.get(dex);
  if (!b) return;
  uint8_t w = b[0], h = b[1], n = b[2];
  const uint8_t *d = b + 3 + n * 2;
  x += (40 - w) / 2; y += (40 - h) / 2;
  for (int r = 0; r < h; r++) {
    int run = -1;
    for (int c = 0; c <= w; c++) {
      bool on = c < w && d[r * w + c] != 0xFF;
      if (on && run < 0) run = c;
      if (!on && run >= 0) { gfx->drawFastHLine(x + run, y + r, c - run, col); run = -1; }
    }
  }
}

static void habText(int x, int y, const char *s, uint16_t col) {
  char b[HAB_TXT_CH + 1];
  snprintf(b, sizeof(b), "%s", s);    // clipped to the row
  gfx->setTextColor(col);
  gfx->setTextSize(2);
  gfx->setCursor(x, y);
  gfx->print(b);
}

void renderHabitat() {
  uint8_t region = galleryRegion % GAL_REGIONS;
  const RegionInfo &rg = REGIONS[region];
  bool open = regionAvailable(region);
  uint16_t missing = habitatMissingCount(pet, region);
  uint8_t pages = habitatPageCount();
  if (habitatPage >= pages) habitatPage = pages - 1;
  char ttl[32];
  snprintf(ttl, sizeof(ttl), TL(S_HB_HEAD), rg.name, (unsigned)missing);
  featBegin(ttl);

  if (!open) {
    // locked, never hidden: the region is there, its pack is not
    featLine(200, T(S_NEED_PACK), UI_TRACK);
  } else if (!missing) {
    featLine(200, TL(S_HB_DONE), UI_BAR_OK);
  } else {
    for (uint8_t i = 0; i < HAB_PER_PAGE; i++) {
      int16_t dex = habitatMissingAt(pet, region, (uint16_t)(habitatPage * HAB_PER_PAGE + i));
      if (!dex) break;
      int y = HAB_ROW_Y(i);
      gfx->drawRoundRect(HAB_ROW_X, y, HAB_ROW_W, HAB_ROW_H, 10, UI_TRACK);
      habSilhouette(dex, HAB_ROW_X + 8, y + 8, INK_K);
      char l1[24];
      snprintf(l1, sizeof(l1), "N.%03d ???", dex);   // the name stays hidden, as in the gallery
      habText(HAB_TXT_X, y + 10, l1, UI_INK);
      uint8_t lv = habitatMinLevel(dex);
      if (lv) {
        char lb[12];
        snprintf(lb, sizeof(lb), TL(S_HB_LVL), lv);
        habText(HAB_ROW_X + HAB_ROW_W - 10 - (int)strlen(lb) * 12, y + 10, lb, UI_BAR_WARN);
      }
      char l2[40];
      habitatSummary(dex, l2, sizeof(l2));
      HabInfo h = habitatOf(dex, gHabTot);
      uint16_t c2 = h.how != HB_WILD ? 0x8410 : (h.restricted ? UI_BAR_WARN : 0x4C98);
      habText(HAB_TXT_X, y + 32, l2, c2);
    }
  }
  featBackPill();
  char pg[12];
  snprintf(pg, sizeof(pg), "%u/%u", habitatPage + 1, (unsigned)pages);
  featLine(HAB_PAGE_Y, pg, 0x8410);
  gfx->flush();
}

// The habitat lines under a species on the Pokedex detail page: how / where
// and when, then what holds it back and from what level.
void drawHabitatDetail(int16_t dex) {
  char a[40], b[48] = "";
  habitatSummary(dex, a, sizeof(a));
  HabInfo h = habitatOf(dex, gHabTot);
  if (h.how == HB_WILD && h.restricted) {
    habBest(h, a, sizeof(a));       // line 1 the best, line 2 the restriction
    habOnly(h, b, sizeof(b));
  }
  uint8_t lv = habitatMinLevel(dex);
  if (lv) {
    char lvs[12], t[48];
    snprintf(lvs, sizeof(lvs), TL(S_HB_LVL), lv);
    snprintf(t, sizeof(t), "%s%s%s", b, b[0] ? " " : "", lvs);
    snprintf(b, sizeof(b), "%s", t);
  }
  featLine(330, a, h.how == HB_WILD ? 0x4C98 : 0x8410);
  if (b[0]) featLine(354, b, UI_BAR_WARN);
}

// The HABITAT pill on the Pokedex grid.
void drawGalleryHabitatBtn() {
  gfx->fillRoundRect(HAB_BTN_X, HAB_BTN_Y, HAB_BTN_W, HAB_BTN_H, 12, UI_WHITE);
  gfx->drawRoundRect(HAB_BTN_X, HAB_BTN_Y, HAB_BTN_W, HAB_BTN_H, 12, UI_INK);
  const char *t = TL(S_HB_BTN);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(2);
  gfx->setCursor(HAB_BTN_X + (HAB_BTN_W - (int)strlen(t) * 12) / 2, HAB_BTN_Y + 9);
  gfx->print(t);
}
bool galleryHabitatBtnHit(int16_t x, int16_t y) {
  return x >= HAB_BTN_X - 10 && x <= HAB_BTN_X + HAB_BTN_W + 10 &&
         y >= HAB_BTN_Y - 6 && y <= HAB_BTN_Y + HAB_BTN_H + 14;
}

// ---------- input ----------

void habitatTap(int16_t x, int16_t y) {
  if (featBackHit(x, y)) { sfxPlay(SFX_TAP); habitatBack(); return; }
  uint8_t region = galleryRegion % GAL_REGIONS;
  if (!regionAvailable(region)) return;
  if (x < HAB_ROW_X - 6 || x > HAB_ROW_X + HAB_ROW_W + 6) return;
  for (uint8_t i = 0; i < HAB_PER_PAGE; i++) {
    int ry = HAB_ROW_Y(i);
    if (y < ry - 3 || y > ry + HAB_ROW_H + 3) continue;
    int16_t dex = habitatMissingAt(pet, region, (uint16_t)(habitatPage * HAB_PER_PAGE + i));
    if (!dex) return;
    sfxPlay(SFX_TAP);
    galleryDetail = dex;                 // its detail page; back lands here again
    galleryPmd.load(dex, false);
    return;
  }
}

// Horizontal: page; past either end, back to the grid.
void habitatSwipe(int dir) {
  int p = (int)habitatPage + (dir > 0 ? -1 : 1);
  if (p < 0 || p >= habitatPageCount()) { habitatBack(); return; }
  habitatPage = (uint8_t)p;
  sfxPlay(SFX_TAP);
}

// Vertical: the next / previous region, as the grid does.
void habitatSwipeV(int dir) {
  galleryRegion = (uint8_t)((galleryRegion + (dir > 0 ? 1 : GAL_REGIONS - 1)) % GAL_REGIONS);
  galleryPage = 0;
  galleryDirty = true;
  habitatPage = 0;
  sfxPlay(SFX_TAP);
}
