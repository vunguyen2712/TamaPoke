// ---------- Daily and weekly tasks (feature 3). Logic: tasks.cpp ----------
// Owned by this feature alone. Reached from the Adventure hub (ui_hub.ino);
// use featBegin/featRow/featRowAt/featBackPill/featBackHit/featLine from there,
// and leave with featBackToHub(). Every render path MUST end in gfx->flush().
//
// Two pages: DAILY (three tasks and the bonus) and WEEKLY (two). A horizontal
// swipe pages, and backs out to the hub past either end -- never closes on a
// page swipe. A finished task reads CLAIM!; tapping it pays into the bag.

#define TASKS_PAGES 2
uint8_t tasksPage = 0;   // 0 daily, 1 weekly

// Feature-local rows, ES EN FR DE IT PT, ASCII only. Indexed by TaskKind.
static const char *const S_TK_LABEL[TK_COUNT][LANG_COUNT] = {
  { "-", "-", "-", "-", "-", "-" },
  { "ALIMENTAR", "FEED", "NOURRIR", "FUTTERN", "NUTRIRE", "ALIMENTAR" },
  { "BAYA FAVORITA", "FAV BERRY", "BAIE PREFEREE", "LIEBLINGSBEERE", "BACCA PREFERITA", "BAGA FAVORITA" },
  { "BANAR", "CLEAN", "LAVER", "BADEN", "LAVARE", "DAR BANHO" },
  { "JUGAR", "PLAY", "JOUER", "SPIELEN", "GIOCARE", "BRINCAR" },
  { "ENTRENAR", "TRAIN", "ENTRAINER", "TRAINIEREN", "ALLENARE", "TREINAR" },
  { "DIAS CUIDADO", "CARE DAYS", "JOURS SOINS", "PFLEGETAGE", "GIORNI CURA", "DIAS CUIDADO" },
  { "ECLOSIONAR", "HATCH", "ECLORE", "SCHLUEPFEN", "SCHIUDERE", "CHOCAR" },
  { "EVOLUCIONAR", "EVOLVE", "EVOLUER", "ENTWICKELN", "EVOLVERE", "EVOLUIR" },
  { "GANAR GIMNASIO", "GYM WIN", "VICT. ARENE", "ARENA-SIEG", "VITT. PALESTRA", "VITORIA GINASIO" },
  { "REVANCHA", "REMATCH WIN", "REVANCHE", "REVANCHE", "RIVINCITA", "REVANCHE" },
  { "VER SALVAJES", "SEE WILD", "VOIR SAUVAGES", "WILDE SEHEN", "VEDI SELVATICI", "VER SELVAGENS" },
  { "VENCER SALVAJE", "BEAT WILD", "BATTRE SAUVAGE", "WILDE BESIEGEN", "BATTI SELVATICI", "VENCER SELVAGEM" },
  { "CAPTURAR", "CATCH", "CAPTURER", "FANGEN", "CATTURARE", "CAPTURAR" },
  { "PISO TORRE", "TOWER FLOOR", "ETAGE TOUR", "TURM-ETAGE", "PIANO TORRE", "ANDAR TORRE" },
  { "CRIAR HUEVO", "BREED", "ELEVER OEUF", "ZUECHTEN", "ALLEVARE", "CRIAR OVO" },
  { "USAR OBJETO", "USE ITEM", "UTILISER OBJET", "ITEM NUTZEN", "USA OGGETTO", "USAR ITEM" },
  { "NUEVO EN DEX", "NEW DEX ENTRY", "NOUVEAU DEX", "NEU IM DEX", "NUOVO NEL DEX", "NOVO NA DEX" },
  { "BIEN CUIDADO", "KEEP IT WELL", "BIEN SOIGNE", "GUT GEPFLEGT", "BEN CURATO", "BEM CUIDADO" },
};
static const char *const S_TK_DAILY[LANG_COUNT]   = { "DIARIAS", "DAILY", "QUOTIDIEN", "TAEGLICH", "GIORNALIERE", "DIARIAS" };
static const char *const S_TK_WEEKLY[LANG_COUNT]  = { "SEMANALES", "WEEKLY", "HEBDO", "WOECHENTLICH", "SETTIMANALI", "SEMANAIS" };
static const char *const S_TK_CLAIM[LANG_COUNT]   = { "COBRAR!", "CLAIM!", "RECLAMER!", "ABHOLEN!", "RISCATTA!", "RESGATAR!" };
static const char *const S_TK_CLAIMED[LANG_COUNT] = { "COBRADO", "CLAIMED", "RECU", "ERHALTEN", "RISCATTATO", "RESGATADO" };
static const char *const S_TK_BONUS[LANG_COUNT]   = { "EXTRA 3/3", "BONUS 3/3", "BONUS 3/3", "BONUS 3/3", "BONUS 3/3", "BONUS 3/3" };
static const char *const S_TK_DONE[LANG_COUNT]    = { "TAREA HECHA!", "TASK DONE!", "TACHE FINIE!", "AUFGABE FERTIG!", "MISSIONE FATTA!", "TAREFA FEITA!" };
static const char *const S_TK_MIN[LANG_COUNT]     = { "MIN", "MIN", "MIN", "MIN", "MIN", "MIN" };

// For the tests: every label in every language, without drawing.
const char *tasksKindLabel(uint8_t kind, uint8_t lang) {
  if (kind >= TK_COUNT || lang >= LANG_COUNT) return nullptr;
  return S_TK_LABEL[kind][lang];
}
const char *tasksUiString(uint8_t which, uint8_t lang) {
  static const char *const *const ALL[] = { S_TK_DAILY, S_TK_WEEKLY, S_TK_CLAIM, S_TK_CLAIMED, S_TK_BONUS, S_TK_DONE, S_TK_MIN };
  if (which >= sizeof(ALL) / sizeof(ALL[0]) || lang >= LANG_COUNT) return nullptr;
  return ALL[which][lang];
}

void tasksHubLabel(char *out, size_t n, const char *name) {
  tasksRefresh();
  uint8_t c = tasksClaimable();
  if (c) snprintf(out, n, "%s %u!", name, c);
  else snprintf(out, n, "%s", name);
}
bool tasksHubEnabled() { return true; }   // there are always tasks to look at
void tasksOpened() { tasksPage = 0; tasksRefresh(); }

// How many tappable rows the current page has (the bonus is the daily page's 4th).
static uint8_t tasksRows() { return tasksPage == 0 ? TASK_DAILY + 1 : TASK_WEEKLY; }
static uint8_t tasksSlotOf(uint8_t row) { return tasksPage == 0 ? row : TASK_DAILY + row; }

// One task row: label and n/target on top, reward and status underneath.
static void tasksDrawRow(uint8_t row, const char *label, const char *count,
                         uint8_t item, uint8_t qty, bool ready, bool paid) {
  int y = FEAT_ROW_Y(row);
  uint16_t bg = ready ? UI_BAR_OK : paid ? UI_TRACK : UI_BG_DAY;
  uint16_t ink = ready ? UI_WHITE : paid ? 0x8410 : UI_INK;
  gfx->fillRoundRect(FEAT_ROW_X, y, FEAT_ROW_W, FEAT_ROW_H, 10, bg);
  gfx->drawRoundRect(FEAT_ROW_X, y, FEAT_ROW_W, FEAT_ROW_H, 10, UI_INK);
  gfx->setTextColor(ink);
  gfx->setTextSize(2);
  gfx->setCursor(FEAT_ROW_X + 14, y + 6);
  gfx->print(label);
  gfx->setCursor(FEAT_ROW_X + FEAT_ROW_W - 14 - (int)strlen(count) * 12, y + 6);
  gfx->print(count);
  char rw[24];
  snprintf(rw, sizeof(rw), "+%u %s", qty, itemValid(item) ? ITEM_TBL[item].name : "?");
  gfx->setTextSize(1);
  gfx->setCursor(FEAT_ROW_X + 14, y + 30);
  gfx->print(rw);
  const char *st = ready ? TL(S_TK_CLAIM) : paid ? TL(S_TK_CLAIMED) : "";
  gfx->setCursor(FEAT_ROW_X + FEAT_ROW_W - 14 - (int)strlen(st) * 6, y + 30);
  gfx->print(st);
}

void renderTasks() {
  tasksRefresh();
  featBegin(TL(tasksPage == 0 ? S_TK_DAILY : S_TK_WEEKLY));
  uint8_t n = tasksPage == 0 ? TASK_DAILY : TASK_WEEKLY;
  for (uint8_t r = 0; r < n; r++) {
    uint8_t s = tasksSlotOf(r);
    const TaskDef &d = tasksDef(s);
    char cnt[16];
    if (d.kind == TK_WELL_KEPT) snprintf(cnt, sizeof(cnt), "%u/%u%s", tasksProgress(s), d.target, TL(S_TK_MIN));
    else snprintf(cnt, sizeof(cnt), "%u/%u", tasksProgress(s), d.target);
    tasksDrawRow(r, TL(S_TK_LABEL[d.kind < TK_COUNT ? d.kind : 0]), cnt, d.item, d.qty,
                 tasksDone(s) && !tasksClaimed(s), tasksClaimed(s));
  }
  if (tasksPage == 0) {
    uint8_t got = 0;
    for (uint8_t i = 0; i < TASK_DAILY; i++) if (tasksClaimed(i)) got++;
    char cnt[8];
    snprintf(cnt, sizeof(cnt), "%u/%u", got, TASK_DAILY);
    tasksDrawRow(TASK_DAILY, TL(S_TK_BONUS), cnt, tasksBonusItem(), tasksBonusQty(),
                 tasksBonusReady(), tasksBonusClaimed());
  }
  char pg[12];
  snprintf(pg, sizeof(pg), "< %u/%u >", tasksPage + 1, TASKS_PAGES);
  featLine(352, pg, UI_INK);
  featBackPill();
  gfx->flush();
}

void tasksTap(int16_t x, int16_t y) {
  if (featBackHit(x, y)) { sfxPlay(SFX_TAP); featBackToHub(); return; }
  int r = featRowAt(x, y, tasksRows());
  if (r < 0) return;
  bool paid = (tasksPage == 0 && r == TASK_DAILY) ? tasksClaimBonus()
                                                  : tasksClaim(tasksSlotOf((uint8_t)r));
  sfxPlay(paid ? SFX_MEDAL : SFX_DENY);
}

// dir > 0 is back a page, like every other paged screen in the sketch.
void tasksSwipe(int dir) {
  int p = (int)tasksPage + (dir > 0 ? -1 : 1);
  if (p < 0 || p >= TASKS_PAGES) { featBackToHub(); return; }
  tasksPage = (uint8_t)p;
}

// "TASK DONE!" over the main screen for a moment after one completes. Called
// from render()'s main-screen path, so it never draws over another screen.
void drawTasksNotice() {
  if (!tasksNoticeActive()) return;
  const char *t = TL(S_TK_DONE);
  gfx->fillRoundRect(103, 96, 260, 44, 14, UI_BAR_OK);
  gfx->drawRoundRect(103, 96, 260, 44, 14, UI_INK);
  gfx->setTextColor(UI_WHITE);
  gfx->setTextSize(2);
  gfx->setCursor(CX - (int)strlen(t) * 6, 110);
  gfx->print(t);
}
