// ---------- The bag: items, held items (feature 8). Logic: items.cpp ----------
// Owned by this feature alone. Reached from the Adventure hub (ui_hub.ino);
// use featBegin/featRow/featRowAt/featBackPill/featBackHit/featLine from there,
// and leave with featBackToHub(). Every render path MUST end in gfx->flush().
//
// Two modes on one screen:
//   LIST  -- every item you own with its count, then the key items (charms),
//            then every creature that is HOLDING something. Tap an item to give
//            it; tap a holder to take its item back into the bag.
//   GIVE  -- the creatures that can hold it: the live pet, the party, the box.
//            Tap one and it holds the item (whatever it held comes back).
// Both are paged, BAG_ROWS a page: a horizontal swipe pages, and past either
// end backs out one level (GIVE -> LIST -> hub). See CLAUDE.md "swipe_test".
//
// Design notes and the numbers: docs/adventure-items.md.

static const char *const S_BAG_NAME[LANG_COUNT]  = { "MOCHILA", "BAG", "SAC", "BEUTEL", "BORSA", "MOCHILA" };
static const char *const S_BAG_GIVE[LANG_COUNT]  = { "DAR", "GIVE", "DONNER", "GEBEN", "DAI", "DAR" };
static const char *const S_BAG_EMPTY[LANG_COUNT] = { "Mochila vacia", "The bag is empty", "Sac vide", "Beutel leer", "Borsa vuota", "Mochila vazia" };
static const char *const S_BAG_KEY[LANG_COUNT]   = { "CLAVE", "KEY", "CLE", "BASIS", "CHIAVE", "CHAVE" };
static const char *const S_BAG_HOLDS[LANG_COUNT] = { "LLEVA", "HOLDS", "TIENT", "TRAEGT", "TIENE", "SEGURA" };
static const char *const S_BAG_HINT[LANG_COUNT]  = {
  "objeto: dar   portador: quitar", "item: give   holder: take back",
  "objet: donner   porteur: reprendre", "Item: geben   Traeger: zuruecknehmen",
  "oggetto: dai   portatore: riprendi", "item: dar   portador: tirar" };
static const char *const S_BAG_NOONE[LANG_COUNT] = { "Nadie puede llevarlo", "Nobody can hold it", "Personne pour le tenir", "Niemand kann es tragen", "Nessuno puo tenerlo", "Ninguem pode segurar" };
static const char *const S_EVO_NOPACK[LANG_COUNT] = { "Falta su pack", "Needs its pack", "Pack manquant", "Pack fehlt", "Manca il pack", "Falta o pack" };
// Battle narration: %s the creature, %s the item. <= 39 chars with an 11-char
// name and a 12-char item, the width of a battle message line.
static const char *const S_IT_USE[LANG_COUNT]  = { "%s uso %s", "%s used its %s", "%s utilise %s", "%s nutzt %s", "%s usa %s", "%s usou %s" };
static const char *const S_IT_SASH[LANG_COUNT] = { "%s aguanto con %s", "%s hung on: %s", "%s tient bon: %s", "%s haelt durch: %s", "%s resiste: %s", "%s aguentou: %s" };
static const char *const S_IT_CLAW[LANG_COUNT] = { "%s va primero: %s", "%s moved first: %s", "%s agit vite: %s", "%s ist schneller: %s", "%s va prima: %s", "%s foi primeiro: %s" };

#define BAG_ROWS 5
enum : uint8_t { BROW_ITEM = 0, BROW_KEY, BROW_HOLDER };
struct BagRow { uint8_t kind; uint8_t item; uint8_t who; };
#define BAG_ROW_MAX (ITEM_COUNT + HOLDER_COUNT)

uint8_t bagPage = 0;       // LIST page
uint8_t bagGive = 0;       // the item being given; 0 = LIST mode
uint8_t bagGivePage = 0;   // GIVE page

// ---- the rows, built fresh each time so they can never go stale

static uint8_t bagListRows(BagRow *out) {
  uint8_t n = 0;
  for (uint8_t it = 1; it < ITEM_COUNT; it++)
    if (bag.count(it) && ITEM_TBL[it].kind != IK_KEY) out[n++] = { BROW_ITEM, it, 0 };
  for (uint8_t it = 1; it < ITEM_COUNT; it++)
    if (bag.count(it) && ITEM_TBL[it].kind == IK_KEY) out[n++] = { BROW_KEY, it, 0 };
  for (uint8_t w = 0; w < HOLDER_COUNT; w++) {
    uint8_t h = heldBy(pet, party, w);
    if (h) out[n++] = { BROW_HOLDER, h, w };
  }
  return n;
}

static uint8_t bagGiveRows(BagRow *out) {
  uint8_t n = 0;
  for (uint8_t w = 0; w < HOLDER_COUNT; w++)
    if (holderExists(pet, party, w)) out[n++] = { BROW_HOLDER, heldBy(pet, party, w), w };
  return n;
}

static uint8_t bagPagesFor(uint8_t rows) { return rows ? (rows + BAG_ROWS - 1) / BAG_ROWS : 1; }

// For the tests: how many pages each mode has right now.
uint8_t bagListPages() { BagRow r[BAG_ROW_MAX]; return bagPagesFor(bagListRows(r)); }
uint8_t bagGivePages() { BagRow r[BAG_ROW_MAX]; return bagPagesFor(bagGiveRows(r)); }

static const char *holderName(uint8_t who) {
  if (who == HOLDER_PET) return pet.nick[0] ? pet.nick : DEX_TBL[pet.speciesId].name;
  const PartyMon &m = who < HOLDER_BOX(0) ? party.slots[who - HOLDER_PARTY(0)]
                                           : party.box[who - HOLDER_BOX(0)];
  return m.nick[0] ? m.nick : DEX_TBL[m.dex].name;
}

// ---- hub hooks

void bagHubLabel(char *out, size_t n, const char *name) { snprintf(out, n, "%s", name); }
bool bagHubEnabled() { return true; }   // always: the charms live here too
void bagOpened() { bagPage = 0; bagGive = 0; bagGivePage = 0; }

// ---- render

static void bagDrawRows(const BagRow *rows, uint8_t n, uint8_t page) {
  for (uint8_t i = 0; i < BAG_ROWS; i++) {
    uint8_t k = page * BAG_ROWS + i;
    if (k >= n) break;
    const BagRow &r = rows[k];
    char lbl[32];
    bool dim = false;
    if (r.kind == BROW_ITEM) {
      snprintf(lbl, sizeof(lbl), "%s x%u", ITEM_TBL[r.item].name, bag.count(r.item));
      dim = !itemIsHeld(r.item);          // balls are thrown, not given
    } else if (r.kind == BROW_KEY) {
      snprintf(lbl, sizeof(lbl), "%s %s", TL(S_BAG_KEY), ITEM_TBL[r.item].name);
      dim = true;                          // owned, never spent, nothing to tap
    } else if (r.item) {
      snprintf(lbl, sizeof(lbl), "%s: %s", holderName(r.who), ITEM_TBL[r.item].name);
    } else {
      snprintf(lbl, sizeof(lbl), "%s", holderName(r.who));
    }
    featRow(i, lbl, dim);
  }
}

void renderBag() {
  BagRow rows[BAG_ROW_MAX];
  char title[32];
  if (bagGive) {
    uint8_t n = bagGiveRows(rows);
    uint8_t pages = bagPagesFor(n);
    if (bagGivePage >= pages) bagGivePage = pages - 1;
    snprintf(title, sizeof(title), "%s %s", TL(S_BAG_GIVE), ITEM_TBL[bagGive].name);
    featBegin(title);
    if (!n) featLine(140, TL(S_BAG_NOONE), UI_BAR_BAD);
    bagDrawRows(rows, n, bagGivePage);
    if (pages > 1) {
      char pg[8];
      snprintf(pg, sizeof(pg), "%u/%u", bagGivePage + 1, pages);
      featLine(66, pg, UI_TRACK);
    }
  } else {
    uint8_t n = bagListRows(rows);
    uint8_t pages = bagPagesFor(n);
    if (bagPage >= pages) bagPage = pages - 1;
    if (pages > 1) snprintf(title, sizeof(title), "%s %u/%u", TL(S_BAG_NAME), bagPage + 1, pages);
    else snprintf(title, sizeof(title), "%s", TL(S_BAG_NAME));
    featBegin(title);
    if (!n) featLine(140, TL(S_BAG_EMPTY), UI_TRACK);
    else {
      gfx->setTextColor(UI_TRACK);
      gfx->setTextSize(1);
      gfx->setCursor(CX - (int)strlen(TL(S_BAG_HINT)) * 3, 70);
      gfx->print(TL(S_BAG_HINT));
    }
    bagDrawRows(rows, n, bagPage);
  }
  featBackPill();
  gfx->flush();
}

// ---- input

void bagTap(int16_t x, int16_t y) {
  if (featBackHit(x, y)) {
    sfxPlay(SFX_TAP);
    if (bagGive) { bagGive = 0; return; }   // GIVE backs out to the LIST
    featBackToHub();
    return;
  }
  int r = featRowAt(x, y, BAG_ROWS);
  if (r < 0) return;
  BagRow rows[BAG_ROW_MAX];
  if (bagGive) {
    uint8_t n = bagGiveRows(rows);
    uint8_t k = bagGivePage * BAG_ROWS + r;
    if (k >= n) return;
    if (heldGive(pet, party, rows[k].who, bagGive)) {
      sfxPlay(SFX_MEDAL);
      bagGive = 0;                            // done: back to the list
    } else {
      sfxPlay(SFX_DENY);
    }
    return;
  }
  uint8_t n = bagListRows(rows);
  uint8_t k = bagPage * BAG_ROWS + r;
  if (k >= n) return;
  const BagRow &row = rows[k];
  if (row.kind == BROW_ITEM && itemIsHeld(row.item)) {
    bagGive = row.item;
    bagGivePage = 0;
    sfxPlay(SFX_TAP);
  } else if (row.kind == BROW_HOLDER) {
    sfxPlay(heldTake(pet, party, row.who) ? SFX_TAP : SFX_DENY);
  } else {
    sfxPlay(SFX_DENY);                        // a ball or a key item
  }
}

// Horizontal pages; past either end backs out one level, never wraps.
void bagSwipe(int dir) {
  uint8_t &page = bagGive ? bagGivePage : bagPage;
  uint8_t pages = bagGive ? bagGivePages() : bagListPages();
  int p = (int)page + (dir > 0 ? -1 : 1);
  if (p < 0 || p >= pages) {
    if (bagGive) bagGive = 0;
    else featBackToHub();
    return;
  }
  page = (uint8_t)p;
}

// ---- hooks the rest of the sketch calls

// Battle narration format for a held item that triggered (see btlNarrate()).
const char *bagItemLine(uint8_t item) {
  if (item == ITEM_FOCUS_SASH) return TL(S_IT_SASH);
  if (item == ITEM_QUICK_CLAW) return TL(S_IT_CLAW);
  return TL(S_IT_USE);
}

// "HOLDS: LEFTOVERS", for the creature card and the party/box sheet.
void bagHeldLine(char *out, size_t n, uint8_t item) {
  if (!itemValid(item)) { if (n) out[0] = 0; return; }
  snprintf(out, n, "%s: %s", TL(S_BAG_HOLDS), ITEM_TBL[item].name);
}

// After each battle turn: a berry or sash the active creature spent is removed
// from the stored creature it came from (the CONSUMPTION RULE, items.h).
void bagBattleSync() { heldBattleSync(pet, party, btlYou); }

// The newcomer from a farewell was NOT given a slot: it leaves, its item stays.
// Every "let it go" path on the party screen ends here, not in its own copy.
void bagLetGoEnded() {
  heldReturn(pet.endedMon.held);
  pet.endedKind = CER_NONE;
}

// The card's evolution line. Returns evoCheck()'s verdict (EVO_*), which is
// what canEvolveNow() returns true for -- the card never decides on its own.
// READY is also shown while asleep/frozen (EVO_STATE), as it always was: the
// conditions are met and it will be offered on waking.
uint8_t cardEvoLine(char *out, size_t n, uint16_t *col) {
  EvoCheck e = evoCheck(pet);
  uint16_t c = UI_INK;
  switch (e.block) {
    case EVO_FINAL: snprintf(out, n, "%s", T(S_FINAL_FORM)); break;
    case EVO_LEVEL:
      snprintf(out, n, T(S_EVO_IN_FMT), (unsigned)(e.needLevel - pet.level()));
      break;
    case EVO_BOND:
      snprintf(out, n, "%s %u/%u", T(S_VIN), (unsigned)pet.bond, (unsigned)FRIEND_BOND);
      c = UI_BAR_WARN;
      break;
    case EVO_NO_ART: snprintf(out, n, "%s", TL(S_EVO_NOPACK)); c = UI_BAR_BAD; break;
    case EVO_CARE: snprintf(out, n, "%s", T(S_EVO_BLOCKED)); c = UI_BAR_BAD; break;
    default: snprintf(out, n, "%s", T(S_EVO_READY)); c = UI_BAR_OK; break;
  }
  if (col) *col = c;
  return e.block;
}
