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
static const char *const S_HUB_CHALLENGE[LANG_COUNT] = { "RETO", "CHALLENGE", "DEFI", "HERAUSFORD.", "SFIDA", "DESAFIO" };
static const char *const S_HUB_FRIENDS[LANG_COUNT] = { "AMIGOS", "FRIENDS", "AMIS", "FREUNDE", "AMICI", "AMIGOS" };
static const char *const S_HUB_MORE[LANG_COUNT]  = { "MAS >", "MORE >", "PLUS >", "MEHR >", "ALTRO >", "MAIS >" };

// The hub is a list of activities, HUB_PER_PAGE to a page, with BACK as the
// last row of every page. A horizontal swipe pages; past either end it closes.
// Adding an activity is one entry in each switch below -- never a smaller row.
enum : uint8_t { HUB_WILD = 0, HUB_TASKS, HUB_TOWER, HUB_DAYCARE, HUB_BAG,
                 HUB_CHALLENGE, HUB_FRIENDS, HUB_ACTIVITIES };
#define HUB_PER_PAGE 5
#define HUB_ROWS (HUB_PER_PAGE + 1)
#define HUB_PAGES ((HUB_ACTIVITIES + HUB_PER_PAGE - 1) / HUB_PER_PAGE)

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
  if (friendsOpen) friendsLeave();   // a live FRIENDS link says goodbye, frees the radio
  wildOpen = tasksOpen = towerOpen = daycareOpen = bagOpen = false;
  challengeOpen = friendsOpen = false;
}
void featBackToHub() {
  featCloseAll();
  hubOpen = true;
}

// Activity `a` (HUB_*): its label and whether it can be opened now.
static void hubActLabel(uint8_t a, char *out, size_t n) {
  switch (a) {
    case HUB_WILD: wildHubLabel(out, n, TL(S_HUB_WILD)); break;
    case HUB_TASKS: tasksHubLabel(out, n, TL(S_HUB_TASKS)); break;
    case HUB_TOWER: towerHubLabel(out, n, TL(S_HUB_TOWER)); break;
    case HUB_DAYCARE: daycareHubLabel(out, n, TL(S_HUB_DAYCARE)); break;
    case HUB_BAG: bagHubLabel(out, n, TL(S_HUB_BAG)); break;
    case HUB_CHALLENGE: challengeHubLabel(out, n, TL(S_HUB_CHALLENGE)); break;
    case HUB_FRIENDS: friendsHubLabel(out, n, TL(S_HUB_FRIENDS)); break;
    default: out[0] = 0; break;
  }
}

static bool hubActEnabled(uint8_t a) {
  switch (a) {
    case HUB_WILD: return wildHubEnabled();
    case HUB_TASKS: return tasksHubEnabled();
    case HUB_TOWER: return towerHubEnabled();
    case HUB_DAYCARE: return daycareHubEnabled();
    case HUB_BAG: return bagHubEnabled();
    case HUB_CHALLENGE: return challengeHubEnabled();
    case HUB_FRIENDS: return friendsHubEnabled();
    default: return false;
  }
}

static void hubActOpen(uint8_t a) {
  switch (a) {
    case HUB_WILD: wildOpen = true; wildOpened(); break;
    case HUB_TASKS: tasksOpen = true; tasksOpened(); break;
    case HUB_TOWER: towerOpen = true; towerOpened(); break;
    case HUB_DAYCARE: daycareOpen = true; daycareOpened(); break;
    case HUB_BAG: bagOpen = true; bagOpened(); break;
    case HUB_CHALLENGE: challengeOpen = true; challengeOpened(); break;
    case HUB_FRIENDS: friendsOpen = true; friendsOpened(); break;
  }
}

// Row `r` of the current page: an activity index, HUB_ACTIVITIES for MORE
// (there are more pages after this one) or 0xFF for BACK / an empty row.
static uint8_t hubRowAct(uint8_t r) {
  if (r >= HUB_PER_PAGE) return 0xFF;
  uint8_t a = hubPage * HUB_PER_PAGE + r;
  return a < HUB_ACTIVITIES ? a : 0xFF;
}

void renderHub() {
  if (hubPage >= HUB_PAGES) hubPage = 0;
  featBegin(TL(S_HUB_TITLE));
  for (uint8_t r = 0; r < HUB_PER_PAGE; r++) {
    uint8_t a = hubRowAct(r);
    if (a == 0xFF) continue;
    char lbl[32];
    hubActLabel(a, lbl, sizeof(lbl));
    featRow(r, lbl, !hubActEnabled(a));
  }
  // The last row: MORE while another page follows, BACK on the last page.
  // MORE is a tap target as well as the swipe, since a swipe is invisible.
  bool more = hubPage + 1 < HUB_PAGES;
  featRow(HUB_ROWS - 1, more ? TL(S_HUB_MORE) : T(S_BACK), false);
  if (HUB_PAGES > 1) {
    char pg[8];
    snprintf(pg, sizeof(pg), "%u/%u", hubPage + 1, (unsigned)HUB_PAGES);
    featLine(FEAT_ROW_Y(HUB_ROWS) + 4, pg, 0x8410);
  }
  gfx->flush();
}

void hubTap(int16_t x, int16_t y) {
  int r = featRowAt(x, y, HUB_ROWS);
  if (r < 0) return;
  if (r == HUB_ROWS - 1) {
    sfxPlay(SFX_TAP);
    if (hubPage + 1 < HUB_PAGES) hubPage++;
    else { hubOpen = false; hubPage = 0; }
    return;
  }
  uint8_t a = hubRowAct((uint8_t)r);
  if (a == 0xFF) return;
  if (!hubActEnabled(a)) { sfxPlay(SFX_DENY); return; }
  sfxPlay(SFX_TAP);
  hubOpen = false;
  featCloseAll();
  hubActOpen(a);
}

// Horizontal on the hub: page; past either end, close it.
static void hubSwipe(int dir) {
  int p = (int)hubPage + (dir > 0 ? -1 : 1);
  if (p < 0 || p >= HUB_PAGES) { hubOpen = false; hubPage = 0; return; }
  hubPage = (uint8_t)p;
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
  if (challengeOpen) return SCR_CHALLENGE;
  if (friendsOpen) return SCR_FRIENDS;
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
    case SCR_CHALLENGE: renderChallenge(); break;
    case SCR_FRIENDS: renderFriends(); break;
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
    case SCR_CHALLENGE: challengeTap(x, y); break;
    case SCR_FRIENDS: friendsTap(x, y); break;
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
    case SCR_CHALLENGE: challengeSwipe(dir); break;
    case SCR_FRIENDS: friendsSwipe(dir); break;
    case SCR_HUB: hubSwipe(dir); break;
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
    case BK_CHALLENGE: challengeBattleEnded(won, ran); break;
    default: break;
  }
}

// ---------- main-screen hooks ----------
// Called by the main screen's render and tap paths, so features that live ON
// the pet's screen (an alert, the weather) never edit render()/onTap() itself.

// Right after the scene is drawn, BEFORE the header, the pet and the buttons:
// for things that belong to the world (weather), not to the UI.
void featSceneOverlay() {
  weatherSceneOverlay();
}

// Last, above everything but the menu: badges and notices.
void featMainOverlay() {
  wildMainBadge();
}

// A tap on the main screen, before the menu band and the pet. True = consumed.
// Keep these targets small and well clear of the four buttons and the pet.
bool featMainTap(int16_t x, int16_t y) {
  if (wildMainBadgeTap(x, y)) return true;
  return false;
}
