// ---------- Weekly gym challenge (feature 4). Logic: challenge.cpp ----------
// Owned by this feature alone. Reached from the Adventure hub (ui_hub.ino);
// use featBegin/featRow/featRowAt/featBackPill/featBackHit/featLine from there,
// and leave with featBackToHub(). Every render path MUST end in gfx->flush().
//
// The fight is the sketch's ordinary trainer battle: START opens the squad
// picker in PICK_CHALLENGE mode (which offers only what the rule allows), its
// FIGHT calls challengePickFight(), which builds the squad from COPIES, checks
// it against the rule once more and hands btlOpenTeamFight() the leader's
// adjusted team. The end comes back through challengeBattleEnded().
// Design notes: docs/adventure-challenge.md.

// ES, EN, FR, DE, IT, PT -- ASCII only.
static const char *const S_CH_TITLE[LANG_COUNT]  = { "RETO SEMANAL", "WEEKLY CHALLENGE", "DEFI DE LA SEMAINE", "WOCHEN-DUELL", "SFIDA SETTIMANALE", "DESAFIO SEMANAL" };
static const char *const S_CH_LOCKED[LANG_COUNT] = { "RETO: GANA UNA MEDALLA", "CHALLENGE: WIN A BADGE", "DEFI: GAGNE UN BADGE", "DUELL: HOL EINEN ORDEN", "SFIDA: VINCI MEDAGLIA", "DESAFIO: GANHE INSIGNIA" };
static const char *const S_CH_WHY[LANG_COUNT]    = { "gana una medalla de gimnasio", "win any gym badge first", "gagne d'abord un badge", "hol zuerst einen Orden", "vinci prima una medaglia", "ganhe uma insignia antes" };
static const char *const S_CH_DONE[LANG_COUNT]   = { "HECHO", "DONE", "FAIT", "ERLEDIGT", "FATTO", "FEITO" };
static const char *const S_CH_START[LANG_COUNT]  = { "EMPEZAR", "START", "COMMENCER", "STARTEN", "INIZIA", "COMECAR" };
static const char *const S_CH_R_TYPE[LANG_COUNT]  = { "SOLO TIPO %s", "TYPE ONLY: %s", "TYPE UNIQUE: %s", "NUR TYP %s", "SOLO TIPO %s", "SO TIPO %s" };
static const char *const S_CH_R_LEVEL[LANG_COUNT] = { "NIVEL MAX %u", "LEVEL CAP %u", "NIVEAU MAX %u", "LEVEL MAX %u", "LIVELLO MAX %u", "NIVEL MAX %u" };
static const char *const S_CH_R_SOLO[LANG_COUNT]  = { "SOLO: 1 CONTRA 1", "SOLO: 1 VS 1", "SOLO: 1 CONTRE 1", "SOLO: 1 GEGEN 1", "SOLO: 1 CONTRO 1", "SOLO: 1 CONTRA 1" };
static const char *const S_CH_R_NOIT[LANG_COUNT]  = { "SIN OBJETOS", "NO HELD ITEMS", "SANS OBJETS", "KEINE ITEMS", "NIENTE OGGETTI", "SEM ITENS" };
static const char *const S_CH_R_TRIO[LANG_COUNT]  = { "TRIO: 3 EXACTOS", "TRIO: EXACTLY 3", "TRIO: 3 PILE", "TRIO: GENAU 3", "TRIO: ESATTAMENTE 3", "TRIO: EXATAMENTE 3" };
static const char *const S_CH_D_TYPE[LANG_COUNT]  = { "todos los tuyos deben ser de ese tipo", "every creature you bring must be that type", "tous les tiens doivent etre de ce type", "alle deine muessen diesen Typ haben", "tutti i tuoi devono essere di quel tipo", "todos os seus devem ser desse tipo" };
static const char *const S_CH_D_LEVEL[LANG_COUNT] = { "los tuyos a Nv.%u como maximo, el lider a Nv.%u", "yours at most Lv.%u, the leader's all at Lv.%u", "les tiens au N.%u max, le champion au N.%u", "deine hoechstens Lv.%u, der Leiter auf Lv.%u", "i tuoi al massimo Lv.%u, il capo al Lv.%u", "os seus no maximo Nv.%u, o lider no Nv.%u" };
static const char *const S_CH_D_SOLO[LANG_COUNT]  = { "uno de los tuyos contra su as", "one of yours against the leader's ace", "un des tiens contre son meilleur", "einer von dir gegen sein Ass", "uno dei tuoi contro il suo asso", "um dos seus contra o as do lider" };
static const char *const S_CH_D_NOIT[LANG_COUNT]  = { "los objetos equipados no hacen nada", "held items do nothing this fight", "les objets tenus ne font rien", "getragene Items wirken nicht", "gli strumenti tenuti non hanno effetto", "itens equipados nao fazem nada" };
static const char *const S_CH_D_TRIO[LANG_COUNT]  = { "tres de los tuyos contra sus tres ultimos", "three of yours against the leader's last three", "trois des tiens contre ses trois derniers", "drei von dir gegen seine letzten drei", "tre dei tuoi contro i suoi ultimi tre", "tres dos seus contra os tres ultimos dele" };
static const char *const S_CH_REWARD[LANG_COUNT] = { "premio: %u ULTRA BALL + %s", "reward: %u ULTRA BALL + %s", "prix: %u ULTRA BALL + %s", "Preis: %u ULTRA BALL + %s", "premio: %u ULTRA BALL + %s", "premio: %u ULTRA BALL + %s" };
static const char *const S_CH_RESET[LANG_COUNT]  = { "nuevo reto el lunes", "a new challenge on Monday", "nouveau defi lundi", "neues Duell am Montag", "nuova sfida lunedi", "novo desafio na segunda" };
static const char *const S_CH_PAID[LANG_COUNT]   = { "RETO SUPERADO!", "CHALLENGE CLEARED!", "DEFI REUSSI!", "DUELL GEWONNEN!", "SFIDA SUPERATA!", "DESAFIO VENCIDO!" };
static const char *const S_CH_LOST[LANG_COUNT]   = { "puedes volver a intentarlo", "you can try again this week", "tu peux reessayer", "versuch es nochmal", "puoi riprovare", "pode tentar de novo" };
static const char *const S_CH_NOFIT[LANG_COUNT]  = { "ninguno de los tuyos cumple", "none of yours fit the rule", "aucun des tiens ne convient", "keiner von dir passt", "nessuno dei tuoi va bene", "nenhum dos seus serve" };
static const char *const S_CH_REFUSED[LANG_COUNT] = { "ese equipo rompe la regla", "that squad breaks the rule", "cette equipe enfreint la regle", "dieses Team bricht die Regel", "quella squadra viola la regola", "essa equipe quebra a regra" };
static const char *const S_CH_NONE[LANG_COUNT]   = { "no hay lider disponible", "no leader available", "aucun champion disponible", "kein Leiter verfuegbar", "nessun capo disponibile", "nenhum lider disponivel" };

// ---- what the last fight (or START) left to say
enum : uint8_t { CHM_NONE = 0, CHM_PAID, CHM_LOST, CHM_REFUSED };
static uint8_t chMsg = CHM_NONE;

// The ONE place the sketch asks for this week: pet.currentDay() is the day the
// streak counts, and the logic turns it into a Monday-based week.
static bool chRefresh() { return challengeRefreshFor(pet, party, pet.currentDay()); }

// Candidate n of the picker (0 = the live pet, 1.. = party), raw: does it exist
// and what is it. The rule is asked separately, by challengeAllowsMon().
static bool chCand(uint8_t n, int16_t &dex) {
  if (n == 0) { if (pet.isEgg()) return false; dex = pet.speciesId; return true; }
  if (n > PARTY_SLOTS || party.slots[n - 1].empty()) return false;
  dex = party.slots[n - 1].dex;
  return true;
}

// ---------- the picker's PICK_CHALLENGE mode (TamaPoke.ino asks these) ----------
// May candidate n be offered at all? (pickExists filters on this)
bool challengePickAllows(uint8_t n) {
  int16_t dex;
  if (!chCand(n, dex) || !challengeValid()) return false;
  return challengeAllowsMon(challengeCurrent(), dex);
}

// The squad currently ticked, as the rule sees it: every ticked creature that
// exists, eligible or not, so a mask carried over from another picker is judged
// rather than silently trimmed.
static uint8_t chTicked(int16_t *dex) {
  uint8_t c = 0;
  for (uint8_t n = 0; n <= PARTY_SLOTS; n++) {
    int16_t d;
    if ((squadMask & (1 << n)) && chCand(n, d)) dex[c++] = d;
  }
  return c;
}
bool challengePickOk() {
  if (!challengeValid()) return false;
  int16_t dex[PARTY_SLOTS + 1];
  uint8_t n = chTicked(dex);
  return challengeAllowsSquad(challengeCurrent(), dex, n);
}
uint8_t challengePickCap() {
  return challengeValid() ? challengeSquadMax(challengeCurrent()) : 1;
}
uint8_t challengePickHeader(char *out, size_t n) {
  const ChalWeek &c = challengeCurrent();
  TrainerMon tm[TRAINER_TEAM_MAX];
  uint8_t cnt = challengeTeam(c, tm);
  uint8_t cap = challengeYourCap(c);
  snprintf(out, n, "%s  Lv.%u x%u", TRAINER_SETS[c.region % GYM_REGIONS].list[c.leader % TRAINER_GYMS].name,
           (unsigned)cap, (unsigned)cnt);
  return cap;
}

static uint8_t chEligible() {
  uint8_t c = 0;
  for (uint8_t n = 0; n <= PARTY_SLOTS; n++) c += challengePickAllows(n);
  return c;
}
static bool chCanStart() {
  return challengeUnlockedFor(pet) && chRefresh() && !challengeDone() &&
         challengeTeamDrawable(challengeCurrent()) &&
         chEligible() >= challengeSquadMin(challengeCurrent());
}

// ---------- hub ----------
void challengeHubLabel(char *out, size_t n, const char *name) {
  if (!challengeUnlockedFor(pet)) { snprintf(out, n, "%s", TL(S_CH_LOCKED)); return; }
  if (chRefresh() && challengeDone()) { snprintf(out, n, "%s - %s", name, TL(S_CH_DONE)); return; }
  snprintf(out, n, "%s", name);
}
bool challengeHubEnabled() { return challengeUnlockedFor(pet); }
void challengeOpened() { chMsg = CHM_NONE; chRefresh(); }

static void chSmall(int y, const char *txt, uint16_t col) {
  gfx->setTextColor(col);
  gfx->setTextSize(1);
  gfx->setCursor(CX - (int)strlen(txt) * 3, y);
  gfx->print(txt);
}

static void chRuleText(const ChalWeek &c, char *rule, size_t rn, char *desc, size_t dn) {
  desc[0] = 0;
  switch (c.rule) {
    case CR_TYPE:
      snprintf(rule, rn, TL(S_CH_R_TYPE), typeName(c.param));
      snprintf(desc, dn, "%s", TL(S_CH_D_TYPE));
      break;
    case CR_LEVEL:
      snprintf(rule, rn, TL(S_CH_R_LEVEL), (unsigned)c.param);
      snprintf(desc, dn, TL(S_CH_D_LEVEL), (unsigned)c.param, (unsigned)c.param);
      break;
    case CR_SOLO: snprintf(rule, rn, "%s", TL(S_CH_R_SOLO)); snprintf(desc, dn, "%s", TL(S_CH_D_SOLO)); break;
    case CR_NOITEMS: snprintf(rule, rn, "%s", TL(S_CH_R_NOIT)); snprintf(desc, dn, "%s", TL(S_CH_D_NOIT)); break;
    default: snprintf(rule, rn, "%s", TL(S_CH_R_TRIO)); snprintf(desc, dn, "%s", TL(S_CH_D_TRIO)); break;
  }
}

// ---------- the challenge screen ----------
void renderChallenge() {
  featBegin(TL(S_CH_TITLE));
  if (!challengeUnlockedFor(pet)) {
    featLine(150, TL(S_CH_WHY), UI_TRACK);
    featBackPill();
    gfx->flush();
    return;
  }
  if (!chRefresh()) {
    featLine(150, TL(S_CH_NONE), UI_TRACK);
    featBackPill();
    gfx->flush();
    return;
  }
  const ChalWeek &c = challengeCurrent();
  const Trainer &t = TRAINER_SETS[c.region % GYM_REGIONS].list[c.leader % TRAINER_GYMS];
  char l[72], d[72];
  gfx->setTextColor(typeColor(t.type));
  gfx->setTextSize(3);
  gfx->setCursor(CX - (int)strlen(t.name) * 9, 70);
  gfx->print(t.name);
  snprintf(l, sizeof(l), "%s - %s", t.place, TRAINER_SETS[c.region % GYM_REGIONS].region);
  featLine(102, l, UI_INK);

  // who the leader brings, adjusted to the rule
  TrainerMon tm[TRAINER_TEAM_MAX];
  uint8_t n = challengeTeam(c, tm);
  for (uint8_t i = 0; i < n; i++) {
    int x = CX - n * 25 + i * 50 + 5;
    const uint8_t *th = thumbs.get((int16_t)tm[i].dex);
    if (th) drawThumb(th, x, 126, 1, false);
  }
  snprintf(l, sizeof(l), "Lv.%u x%u  IV %u", (unsigned)challengeYourCap(c), (unsigned)n, (unsigned)CHAL_IV);
  chSmall(172, l, UI_INK);

  chRuleText(c, l, sizeof(l), d, sizeof(d));
  featLine(192, l, UI_BAR_BAD);
  chSmall(216, d, UI_INK);
  uint8_t held = challengeRewardHeld(c.week);
  snprintf(l, sizeof(l), TL(S_CH_REWARD), (unsigned)CHAL_BALLS, ITEM_TBL[held].name);
  chSmall(234, l, UI_TRACK);

  if (challengeDone()) {
    featLine(256, chMsg == CHM_PAID ? TL(S_CH_PAID) : TL(S_CH_DONE), UI_BAR_OK);
    chSmall(280, TL(S_CH_RESET), UI_TRACK);
  } else if (chMsg == CHM_LOST) {
    chSmall(262, TL(S_CH_LOST), UI_BAR_BAD);
  } else if (chMsg == CHM_REFUSED) {
    chSmall(262, TL(S_CH_REFUSED), UI_BAR_BAD);
  } else if (chEligible() < challengeSquadMin(c)) {
    chSmall(262, TL(S_CH_NOFIT), UI_BAR_BAD);
  }
  featRow(5, challengeDone() ? TL(S_CH_DONE) : TL(S_CH_START), !chCanStart());
  featBackPill();
  gfx->flush();
}

static void chOpenPicker() {
  pickTrainer = PICK_CHALLENGE;
  pickRematch = false;
  pickHard = false;
  pickPage = 0;
  pickDefault(challengeSquadMax(challengeCurrent()));   // only the eligible are ticked
  pickOpen = true;            // the challenge screen stays up underneath
}

void challengeTap(int16_t x, int16_t y) {
  if (featBackHit(x, y)) { sfxPlay(SFX_TAP); featBackToHub(); return; }
  if (!challengeUnlockedFor(pet)) return;
  if (featRowAt(x, y, 6) == 5) {
    if (!chCanStart()) { sfxPlay(SFX_DENY); return; }
    sfxPlay(SFX_TAP);
    chMsg = CHM_NONE;
    chOpenPicker();
  }
}

// Not paged: any horizontal swipe goes back to the hub.
void challengeSwipe(int dir) { (void)dir; featBackToHub(); }

// The picker's FIGHT for PICK_CHALLENGE, and the START guard. The squad is
// built from COPIES (buildSquad caps the level there; held items are cleared
// there for NO ITEMS), and the built squad is judged by the same
// challengeAllowsSquad() the picker asked -- a squad that breaks the rule never
// reaches the battle, however it was ticked.
void challengePickFight() {
  if (pet.ceremony != CER_NONE || !chCanStart()) return;
  const ChalWeek &c = challengeCurrent();
  if (!challengePickOk()) { chMsg = CHM_REFUSED; sfxPlay(SFX_DENY); return; }
  buildSquad(challengeYourCap(c), challengeSquadMax(c), squadMask);
  int16_t dex[TRAINER_TEAM_MAX + 1];
  for (uint8_t i = 0; i < btlSquadN; i++) dex[i] = btlSquad[i].dex;
  if (!btlSquadN || !challengeAllowsSquad(c, dex, btlSquadN)) {
    btlSquadN = 0;
    chMsg = CHM_REFUSED;
    sfxPlay(SFX_DENY);
    return;
  }
  if (challengeItemsOff(c)) {
    for (uint8_t i = 0; i < btlSquadN; i++) btlSquad[i].held = ITEM_NONE;   // the copies only
    btlYou = btlSquad[0];
  }
  TrainerMon tm[TRAINER_TEAM_MAX];
  uint8_t n = challengeTeam(c, tm);
  if (!n) { btlSquadN = 0; return; }
  btlRegion = c.region;
  btlKind = BK_CHALLENGE;
  btlTrainer = -1;           // no badge, no training, no win screen: this screen reports
  btlHard = false;
  chMsg = CHM_NONE;
  btlOpenTeamFight(tm, n, CHAL_IV, TRAINER_SETS[c.region % GYM_REGIONS].list[c.leader % TRAINER_GYMS].name);
}

// Routed here by featBattleEnded() for BK_CHALLENGE, once per fight. A loss or
// a RUN changes nothing: the week's challenge waits to be tried again.
void challengeBattleEnded(bool won, bool ran) {
  (void)ran;
  if (won) { challengeWin(); chMsg = CHM_PAID; }   // pays once; DONE either way
  else chMsg = CHM_LOST;
}
