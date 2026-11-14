// ---------- the Adventure hub ----------
//
// Opened from the menu's ADVENTURE row. Everything that is not caring for the
// creature or the gym ladder hangs off this one screen, because every gesture
// on the main screen is already taken (see CLAUDE.md "Swipe map").
//
// Each feature owns ONE tab file (ui_wild, ui_tasks, ui_tower, ui_daycare,
// ui_bag) plus ONE logic module (.cpp/.h). This tab owns only the hub and the
// routing below; a feature never edits another feature's tab.
//
// Arduino concatenates the .ino tabs into TamaPoke.ino (main first, the rest
// alphabetically), so everything here may use the sketch's statics.

// Shared look for every feature screen, so five screens read as one game.
#define FEAT_ROW_X 70
#define FEAT_ROW_W 326
#define FEAT_ROW_H 44
#define FEAT_ROW_Y(i) (88 + (i) * 50)
#define FEAT_BACK_Y 392      // the BACK pill at the bottom of every feature screen
#define FEAT_BACK_H 40

static const char *const S_HUB_TITLE[LANG_COUNT] = { "AVENTURA", "ADVENTURE", "AVENTURE", "ABENTEUER", "AVVENTURA", "AVENTURA" };
static const char *const S_HUB_WILD[LANG_COUNT]  = { "SALVAJES", "WILD", "SAUVAGES", "WILDNIS", "SELVATICI", "SELVAGENS" };
static const char *const S_HUB_TASKS[LANG_COUNT] = { "TAREAS", "TASKS", "TACHES", "AUFGABEN", "MISSIONI", "TAREFAS" };
static const char *const S_HUB_TOWER[LANG_COUNT] = { "TORRE", "TOWER", "TOUR", "TURM", "TORRE", "TORRE" };
static const char *const S_HUB_DAYCARE[LANG_COUNT] = { "GUARDERIA", "DAYCARE", "PENSION", "PENSION", "ASILO", "CRECHE" };
static const char *const S_HUB_BAG[LANG_COUNT]   = { "MOCHILA", "BAG", "SAC", "BEUTEL", "BORSA", "MOCHILA" };

#define HUB_ROWS 6   // WILD / TASKS / TOWER / DAYCARE / BAG / BACK

const char *hubMenuLabel() { return TL(S_HUB_TITLE); }

// Screen title, centred at the top of the round panel, on the usual backdrop.
void featBegin(const char *title) {
  gfx->fillScreen(RGB565_BLACK);
  gfx->fillCircle(CX, CY, 231, UI_BG_DAY);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(2);
  gfx->setCursor(CX - (int)strlen(title) * 6, 42);
  gfx->print(title);
}

// One tappable row. `dim` greys it out (locked, nothing to do) -- locked,
// never hidden: a feature you cannot see yet is a feature that looks absent.
void featRow(uint8_t i, const char *label, bool dim) {
  int y = FEAT_ROW_Y(i);
  gfx->fillRoundRect(FEAT_ROW_X, y, FEAT_ROW_W, FEAT_ROW_H, 10, dim ? UI_TRACK : UI_BG_DAY);
  gfx->drawRoundRect(FEAT_ROW_X, y, FEAT_ROW_W, FEAT_ROW_H, 10, UI_INK);
  gfx->setTextColor(dim ? 0x8410 : UI_INK);
  gfx->setTextSize(2);
  gfx->setCursor(CX - (int)strlen(label) * 6, y + FEAT_ROW_H / 2 - 8);
  gfx->print(label);
}

// Which row (0..) a tap landed on, or -1. Rows are full width with a small pad,
// the same forgiveness the battle grid needed under a real finger.
int featRowAt(int16_t x, int16_t y, uint8_t rows) {
  if (x < FEAT_ROW_X - 6 || x > FEAT_ROW_X + FEAT_ROW_W + 6) return -1;
  for (uint8_t i = 0; i < rows; i++) {
    int ry = FEAT_ROW_Y(i);
    if (y >= ry - 3 && y <= ry + FEAT_ROW_H + 3) return i;
  }
  return -1;
}

void featBackPill() {
  const char *b = T(S_BACK);
  int w = 140;
  gfx->fillRoundRect(CX - w / 2, FEAT_BACK_Y, w, FEAT_BACK_H, 12, UI_TRACK);
  gfx->drawRoundRect(CX - w / 2, FEAT_BACK_Y, w, FEAT_BACK_H, 12, UI_INK);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(2);
  gfx->setCursor(CX - (int)strlen(b) * 6, FEAT_BACK_Y + 12);
  gfx->print(b);
}

bool featBackHit(int16_t x, int16_t y) {
  return y >= FEAT_BACK_Y - 6 && y <= FEAT_BACK_Y + FEAT_BACK_H + 10 && x >= CX - 90 && x <= CX + 90;
}

// A line of small text, centred. For status lines under the title.
void featLine(int y, const char *txt, uint16_t col) {
  gfx->setTextColor(col);
  gfx->setTextSize(2);
  gfx->setCursor(CX - (int)strlen(txt) * 6, y);
  gfx->print(txt);
}

// Every feature screen returns HERE, never all the way to the pet.
void featCloseAll() {
  wildOpen = tasksOpen = towerOpen = daycareOpen = bagOpen = false;
}
void featBackToHub() {
  featCloseAll();
  hubOpen = true;
}

static void hubRowLabel(uint8_t i, char *out, size_t n) {
  switch (i) {
    case 0: wildHubLabel(out, n, TL(S_HUB_WILD)); break;
    case 1: tasksHubLabel(out, n, TL(S_HUB_TASKS)); break;
    case 2: towerHubLabel(out, n, TL(S_HUB_TOWER)); break;
    case 3: daycareHubLabel(out, n, TL(S_HUB_DAYCARE)); break;
    case 4: bagHubLabel(out, n, TL(S_HUB_BAG)); break;
    default: snprintf(out, n, "%s", T(S_BACK)); break;
  }
}

static bool hubRowEnabled(uint8_t i) {
  switch (i) {
    case 0: return wildHubEnabled();
    case 1: return tasksHubEnabled();
    case 2: return towerHubEnabled();
    case 3: return daycareHubEnabled();
    case 4: return bagHubEnabled();
    default: return true;
  }
}

void renderHub() {
  featBegin(TL(S_HUB_TITLE));
  for (uint8_t i = 0; i < HUB_ROWS; i++) {
    char lbl[32];
    hubRowLabel(i, lbl, sizeof(lbl));
    featRow(i, lbl, !hubRowEnabled(i));
  }
  gfx->flush();
}

void hubTap(int16_t x, int16_t y) {
  int r = featRowAt(x, y, HUB_ROWS);
  if (r < 0) return;
  if (r == HUB_ROWS - 1) { hubOpen = false; sfxPlay(SFX_TAP); return; }
  if (!hubRowEnabled(r)) { sfxPlay(SFX_DENY); return; }
  sfxPlay(SFX_TAP);
  hubOpen = false;
  featCloseAll();
  switch (r) {
    case 0: wildOpen = true; wildOpened(); break;
    case 1: tasksOpen = true; tasksOpened(); break;
    case 2: towerOpen = true; towerOpened(); break;
    case 3: daycareOpen = true; daycareOpened(); break;
    case 4: bagOpen = true; bagOpened(); break;
  }
}

// ---------- routing ----------
// The SAME priority order in all five functions, so the screen you see is the
// screen your finger talks to. SCR_COUNT means "no feature screen is up".

uint8_t featScreen() {
  if (wildOpen) return SCR_WILD;
  if (towerOpen) return SCR_TOWER;
  if (daycareOpen) return SCR_DAYCARE;
  if (tasksOpen) return SCR_TASKS;
  if (bagOpen) return SCR_BAG;
  if (hubOpen) return SCR_HUB;
  return SCR_COUNT;
}

void featRender() {
  switch (featScreen()) {
    case SCR_WILD: renderWild(); break;
    case SCR_TOWER: renderTower(); break;
    case SCR_DAYCARE: renderDaycare(); break;
    case SCR_TASKS: renderTasks(); break;
    case SCR_BAG: renderBag(); break;
    case SCR_HUB: renderHub(); break;
  }
}

void featTap(int16_t x, int16_t y) {
  switch (featScreen()) {
    case SCR_WILD: wildTap(x, y); break;
    case SCR_TOWER: towerTap(x, y); break;
    case SCR_DAYCARE: daycareTap(x, y); break;
    case SCR_TASKS: tasksTap(x, y); break;
    case SCR_BAG: bagTap(x, y); break;
    case SCR_HUB: hubTap(x, y); break;
  }
}

// Horizontal: a paged feature pages, and backs out past its first page.
// A feature that is not paged simply goes back to the hub.
void featSwipe(int dir) {
  switch (featScreen()) {
    case SCR_WILD: wildSwipe(dir); break;
    case SCR_TOWER: towerSwipe(dir); break;
    case SCR_DAYCARE: daycareSwipe(dir); break;
    case SCR_TASKS: tasksSwipe(dir); break;
    case SCR_BAG: bagSwipe(dir); break;
    case SCR_HUB: hubOpen = false; break;
  }
}

// Vertical always goes back one level: feature -> hub -> pet.
void featSwipeV(int dir) {
  (void)dir;
  if (featScreen() == SCR_HUB) { hubOpen = false; return; }
  featBackToHub();
}

// Called once when any non-LAN fight ends (won/lost), or is run from (ran).
// Routed by who started it; the gym ladder handles its own ending in-line.
void featBattleEnded(bool won, bool ran) {
  switch (btlKind) {
    case BK_WILD: wildBattleEnded(won, ran); break;
    case BK_TOWER: case BK_REMATCH: towerBattleEnded(won, ran); break;
    default: break;
  }
}
