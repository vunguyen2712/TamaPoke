// ---------- Daycare / breeding (feature 5). Logic: breed.cpp ----------
// Owned by this feature alone. Reached from the Adventure hub (ui_hub.ino);
// use featBegin/featRow/featRowAt/featBackPill/featBackHit/featLine from there,
// and leave with featBackToHub(). Every render path MUST end in gfx->flush().
//
// Two views: HOME (the two parents, BREED, SWAP IN NOW, the queued egg and the
// cooldown) and a PICKER over every banked creature, party then box, five a
// page. The picker pages on a horizontal swipe and backs out past page 1.

static const char *const S_DC_TITLE[LANG_COUNT] = { "GUARDERIA", "DAYCARE", "PENSION", "PENSION", "ASILO", "CRECHE" };
static const char *const S_DC_PICK[LANG_COUNT]  = { "ELEGIR", "PICK", "CHOISIR", "WAHLEN", "SCEGLI", "ESCOLHER" };
static const char *const S_DC_BREED[LANG_COUNT] = { "CRIAR", "BREED", "ELEVER", "ZUCHTEN", "ALLEVA", "CRIAR" };
static const char *const S_DC_SWAP[LANG_COUNT]  = { "CAMBIAR HUEVO", "SWAP IN NOW", "ECHANGER", "JETZT TAUSCHEN", "SCAMBIA ORA", "TROCAR AGORA" };
static const char *const S_DC_SURE[LANG_COUNT]  = { "SEGURO? OTRA VEZ", "SURE? TAP AGAIN", "SUR? ENCORE", "SICHER? NOCHMAL", "SICURO? ANCORA", "CERTEZA? DE NOVO" };
static const char *const S_DC_V_EMPTY[LANG_COUNT] = { "ELIGE DOS", "PICK TWO", "CHOISIS DEUX", "WAHLE ZWEI", "SCEGLINE DUE", "ESCOLHA DOIS" };
static const char *const S_DC_V_SAME[LANG_COUNT]  = { "EL MISMO DOS VECES", "SAME ONE TWICE", "LE MEME DEUX FOIS", "ZWEIMAL DERSELBE", "LO STESSO DUE VOLTE", "O MESMO DUAS VEZES" };
static const char *const S_DC_V_DITTO[LANG_COUNT] = { "DOS DITTO NO", "NOT TWO DITTO", "PAS DEUX DITTO", "NICHT ZWEI DITTO", "NON DUE DITTO", "DOIS DITTO NAO" };
static const char *const S_DC_V_LEGEND[LANG_COUNT] = { "LEGENDARIOS NO", "NO LEGENDARIES", "PAS DE LEGENDAIRE", "KEINE LEGENDEN", "NIENTE LEGGENDARI", "SEM LENDARIOS" };
static const char *const S_DC_V_TYPE[LANG_COUNT]  = { "SIN TIPO COMUN", "NO SHARED TYPE", "AUCUN TYPE COMMUN", "KEIN GLEICHER TYP", "NESSUN TIPO COMUNE", "SEM TIPO COMUM" };
static const char *const S_DC_V_NOEGG[LANG_COUNT] = { "HUEVO NO DISPONIBLE", "EGG NOT AVAILABLE", "OEUF INDISPONIBLE", "EI NICHT VERFUGBAR", "UOVO NON DISPONIBILE", "OVO INDISPONIVEL" };
static const char *const S_DC_V_OK[LANG_COUNT]    = { "COMPATIBLES", "COMPATIBLE", "COMPATIBLES", "KOMPATIBEL", "COMPATIBILI", "COMPATIVEIS" };
static const char *const S_DC_FULL[LANG_COUNT]  = { "YA HAY UN HUEVO", "AN EGG IS WAITING", "UN OEUF ATTEND", "EIN EI WARTET", "UN UOVO ASPETTA", "UM OVO ESPERA" };
static const char *const S_DC_WAIT[LANG_COUNT]  = { "VUELVE MANANA", "COME BACK TOMORROW", "REVIENS DEMAIN", "MORGEN WIEDER", "TORNA DOMANI", "VOLTE AMANHA" };
static const char *const S_DC_TODAY[LANG_COUNT] = { "PUEDE CRIAR HOY", "CAN BREED TODAY", "ELEVAGE POSSIBLE", "HEUTE MOGLICH", "SI PUO OGGI", "PODE CRIAR HOJE" };
static const char *const S_DC_MADE[LANG_COUNT]  = { "HUEVO LISTO!", "EGG READY!", "OEUF PRET!", "EI BEREIT!", "UOVO PRONTO!", "OVO PRONTO!" };
static const char *const S_DC_SWAPPED[LANG_COUNT] = { "HUEVO CAMBIADO", "EGG SWAPPED IN", "OEUF ECHANGE", "EI GETAUSCHT", "UOVO SCAMBIATO", "OVO TROCADO" };
static const char *const S_DC_NEXT[LANG_COUNT]  = { "PROXIMO", "NEXT EGG", "SUIVANT", "NACHSTES", "PROSSIMO", "PROXIMO" };
static const char *const S_DC_NONE[LANG_COUNT]  = { "NADIE GUARDADO", "NONE BANKED", "PERSONNE", "NIEMAND", "NESSUNO", "NINGUEM" };

#define DC_NONE 0xFF
#define DC_PER_PAGE 5
#define DC_PAGE_Y 350
#define DC_MSG_MS 4000UL

static uint8_t dcView = 0;          // 0 home, 1 picker
static uint8_t dcPickFor = 0;       // 0 = parent A, 1 = parent B
static uint8_t dcA = DC_NONE, dcB = DC_NONE;   // banked index (breed.h BREED_BANK)
static uint8_t dcPage = 0;
static const char *const *dcMsg = nullptr;
static uint32_t dcMsgUntil = 0;
static uint32_t dcSwapArmUntil = 0;  // SWAP IN NOW takes two taps: it replaces an egg

static void dcSay(const char *const *six) { dcMsg = six; dcMsgUntil = millis() + DC_MSG_MS; }

// Banked indices that hold a creature, in party-then-box order.
static uint8_t dcList(uint8_t *out) {
  uint8_t n = 0;
  for (uint8_t i = 0; i < BREED_BANK; i++) {
    const PartyMon *m = breedBanked(i);
    if (m && !m->empty()) out[n++] = i;
  }
  return n;
}

static const char *dcName(const PartyMon &m) {
  return m.nick[0] ? m.nick : DEX_TBL[m.dex].name;
}

// A pick goes stale if the slot changed under it (released, swapped to the box).
static void dcValidate() {
  const PartyMon *a = dcA == DC_NONE ? nullptr : breedBanked(dcA);
  const PartyMon *b = dcB == DC_NONE ? nullptr : breedBanked(dcB);
  if (!a || a->empty()) dcA = DC_NONE;
  if (!b || b->empty()) dcB = DC_NONE;
}

static const char *const *dcVerdictText(uint8_t v) {
  switch (v) {
    case BV_OK: return S_DC_V_OK;
    case BV_SAME: return S_DC_V_SAME;
    case BV_TWO_DITTO: return S_DC_V_DITTO;
    case BV_LEGEND: return S_DC_V_LEGEND;
    case BV_NO_TYPE: return S_DC_V_TYPE;
    case BV_NO_EGG: return S_DC_V_NOEGG;
    default: return S_DC_V_EMPTY;
  }
}

static uint8_t dcVerdict() {
  if (dcA == DC_NONE || dcB == DC_NONE) return BV_EMPTY;
  return breedCheckIdx(dcA, dcB);
}

// One predicate for "the BREED row is live", asked by the draw AND the tap.
static bool dcBreedLive() {
  return dcVerdict() == BV_OK && !breedHasQueued() && !breedOnCooldown(pet.currentDay());
}

void daycareHubLabel(char *out, size_t n, const char *name) {
  snprintf(out, n, breedHasQueued() ? "%s *" : "%s", name);
}
bool daycareHubEnabled() { return true; }
void daycareOpened() {
  dcView = 0;
  dcPage = 0;
  dcMsg = nullptr;
  dcSwapArmUntil = 0;
  dcValidate();
}

static void dcParentRow(uint8_t row, uint8_t idx) {
  char l[32];
  const PartyMon *m = idx == DC_NONE ? nullptr : breedBanked(idx);
  if (!m || m->empty()) snprintf(l, sizeof(l), "%c: %s", 'A' + row, TL(S_DC_PICK));
  else snprintf(l, sizeof(l), "%c: %s L%u%s", 'A' + row, dcName(*m), (unsigned)m->level, m->shiny ? "*" : "");
  featRow(row, l, false);
}

static void renderDcHome() {
  featBegin(TL(S_DC_TITLE));
  dcValidate();
  dcParentRow(0, dcA);
  dcParentRow(1, dcB);
  featRow(2, TL(S_DC_BREED), !dcBreedLive());
  bool armed = millis() < dcSwapArmUntil;
  featRow(3, armed ? TL(S_DC_SURE) : TL(S_DC_SWAP), !breedCanSwapIn(pet));

  // Line 1: the last thing that happened, else whether this pair can breed.
  uint8_t v = dcVerdict();
  if (dcMsg && millis() < dcMsgUntil) featLine(292, TL(dcMsg), UI_INK);
  else featLine(292, TL(dcVerdictText(v)), v == BV_OK ? UI_INK : UI_BAR_WARN);

  // Lines 2-3: the queued egg, or the cooldown.
  char l[40];
  if (breedHasQueued()) {
    const BredEgg &e = breedQueued();
    snprintf(l, sizeof(l), "%s: %s", TL(S_DC_NEXT), DEX_TBL[e.dex].name);
    featLine(318, l, UI_INK);
    if (!breedQueuedHatchable()) {
      featLine(342, TL(S_DC_V_NOEGG), UI_BAR_WARN);
    } else {
      char iv[4][4];
      for (int i = 0; i < 4; i++) {
        if (e.iv[i] == BREED_IV_ROLL) snprintf(iv[i], sizeof(iv[i]), "?");
        else snprintf(iv[i], sizeof(iv[i]), "%u", (unsigned)e.iv[i]);
      }
      snprintf(l, sizeof(l), "IV %s %s %s %s", iv[0], iv[1], iv[2], iv[3]);
      featLine(342, l, UI_INK);
    }
  } else {
    bool wait = breedOnCooldown(pet.currentDay());
    featLine(318, TL(wait ? S_DC_WAIT : S_DC_TODAY), wait ? UI_BAR_WARN : UI_INK);
    if (bag.has(ITEM_OVAL_CHARM)) featLine(342, ITEM_TBL[ITEM_OVAL_CHARM].name, 0x4C98);
  }
  featBackPill();
  gfx->flush();
}

static void renderDcPick() {
  uint8_t list[BREED_BANK];
  uint8_t n = dcList(list);
  uint8_t pages = n ? (n + DC_PER_PAGE - 1) / DC_PER_PAGE : 1;
  if (dcPage >= pages) dcPage = pages - 1;
  char head[24];
  snprintf(head, sizeof(head), "%s %c", TL(S_DC_PICK), 'A' + dcPickFor);
  featBegin(head);
  if (!n) featLine(200, TL(S_DC_NONE), UI_BAR_WARN);
  uint8_t other = dcPickFor == 0 ? dcB : dcA;
  for (uint8_t r = 0; r < DC_PER_PAGE; r++) {
    uint8_t k = dcPage * DC_PER_PAGE + r;
    if (k >= n) break;
    const PartyMon &m = *breedBanked(list[k]);
    char l[32];
    snprintf(l, sizeof(l), "%s L%u%s", dcName(m), (unsigned)m.level, m.shiny ? "*" : "");
    featRow(r, l, list[k] == other);    // the other parent: locked, not hidden
  }
  char pg[12];
  snprintf(pg, sizeof(pg), "%u/%u", (unsigned)(dcPage + 1), (unsigned)pages);
  featLine(DC_PAGE_Y, pg, UI_INK);
  featBackPill();
  gfx->flush();
}

void renderDaycare() {
  if (dcView == 1) renderDcPick();
  else renderDcHome();
}

static void dcTapHome(int16_t x, int16_t y) {
  if (featBackHit(x, y)) { sfxPlay(SFX_TAP); featBackToHub(); return; }
  int r = featRowAt(x, y, 4);
  if (r < 0) return;
  if (r != 3) dcSwapArmUntil = 0;   // any other row disarms the swap
  if (r == 0 || r == 1) {
    sfxPlay(SFX_TAP);
    dcPickFor = (uint8_t)r;
    dcPage = 0;
    dcView = 1;
    return;
  }
  if (r == 2) {
    if (!dcBreedLive()) {
      sfxPlay(SFX_DENY);
      if (breedHasQueued()) dcSay(S_DC_FULL);
      else if (dcVerdict() == BV_OK) dcSay(S_DC_WAIT);
      return;
    }
    uint8_t res = breedMake(dcA, dcB, pet.currentDay());
    if (res == BR_OK) { sfxPlay(SFX_HATCH); dcSay(S_DC_MADE); }
    else sfxPlay(SFX_DENY);
    return;
  }
  // r == 3: SWAP IN NOW, armed by the first tap and done by the second
  if (!breedCanSwapIn(pet)) { sfxPlay(SFX_DENY); dcSwapArmUntil = 0; return; }
  if (millis() >= dcSwapArmUntil) { sfxPlay(SFX_TAP); dcSwapArmUntil = millis() + DC_MSG_MS; return; }
  dcSwapArmUntil = 0;
  if (breedSwapIn(pet)) { sfxPlay(SFX_HATCH); dcSay(S_DC_SWAPPED); }
  else sfxPlay(SFX_DENY);
}

static void dcTapPick(int16_t x, int16_t y) {
  if (featBackHit(x, y)) { sfxPlay(SFX_TAP); dcView = 0; return; }
  int r = featRowAt(x, y, DC_PER_PAGE);
  if (r < 0) return;
  uint8_t list[BREED_BANK];
  uint8_t n = dcList(list);
  uint8_t k = dcPage * DC_PER_PAGE + r;
  if (k >= n) return;
  uint8_t other = dcPickFor == 0 ? dcB : dcA;
  if (list[k] == other) { sfxPlay(SFX_DENY); return; }
  sfxPlay(SFX_TAP);
  if (dcPickFor == 0) dcA = list[k]; else dcB = list[k];
  dcMsg = nullptr;
  dcView = 0;
}

void daycareTap(int16_t x, int16_t y) {
  if (dcView == 1) dcTapPick(x, y);
  else dcTapHome(x, y);
}

// Horizontal: the picker pages (dir < 0 = next, the same as every paged screen)
// and backs out to HOME past its first page; HOME goes back to the hub.
void daycareSwipe(int dir) {
  if (dcView != 1) { featBackToHub(); return; }
  uint8_t list[BREED_BANK];
  uint8_t n = dcList(list);
  uint8_t pages = n ? (n + DC_PER_PAGE - 1) / DC_PER_PAGE : 1;
  int p = (int)dcPage + (dir > 0 ? -1 : 1);
  if (p < 0) { dcView = 0; return; }
  if (p >= pages) p = pages - 1;
  dcPage = (uint8_t)p;
}
