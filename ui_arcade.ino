// ---------- the ARCADE (v3.18) ----------
// Four short games played for fun, joy and the bond -- unlike the three
// training games (ui_train.ino) they train no stat. Reached from the Adventure
// hub's ARCADE row (page 2): the hub is the list of activities, it had room,
// and it keeps this off the main screen and its action bar, which are being
// redesigned separately. README "Arcade" has the player-facing numbers.
//
//   BERRY CATCH  drag (or tap) to move the basket; catch berries, dodge rocks
//   MEMORY       flip pairs of type cards; clear the board for a new one
//   ECHO         four type pads light up in a growing sequence; repeat it
//   POP-UP       critters pop out of seven holes; bop them, never the bomb
//
// The family look is the training games': the same 3-2-1 countdown, combo
// counter and multiplier, pop-ups, particles, jolt and rising blip, all
// borrowed from ui_train.ino rather than copied. This tab is concatenated
// BEFORE ui_train.ino and ui_movefx.ino (tabs go alphabetically), so the
// helpers it borrows are forward-declared just below.
//
// One function answers "this session is over": arcFinish(). The natural end,
// a lost last life, leaving by swipe or header and the pet falling asleep all
// go through it, so a session is banked exactly once and the same way.
//
// Rewards (only for a session that scored): joy and the bond through
// Pet::friendVisit() -- the daily bond cap in addBond() applies as it does to
// playing -- 3 energy, an EV_PLAY event so the tasks count it, and once a day
// a small item for an S grade. Bests and the prize day live in NVS under
// "arcHi" and "arcDay" (save.cpp SAVE_FIELDS).

// ---- borrowed from ui_train.ino (defined later in the same translation unit)
extern uint32_t trnGoAt;
extern uint16_t trnCombo, trnBestCombo;
uint8_t trnMult(uint16_t combo);
static bool trnInIntro(uint32_t now);
static void trnFxReset();
static void trnPopup(int x, int y, const char *t, uint16_t col, uint8_t size);
static void trnCross(int x, int y);
static void trnBurst(int x, int y, uint16_t col, int n, float speed);
static void trnBanner(const char *t, uint16_t col, uint16_t ms);
static void trnJolt(uint8_t amp, uint16_t ms);
static int trnJx(uint32_t now);
static int trnJy(uint32_t now);
static void trnComboUp();
static void trnComboBreak(int x, int y);
static void trnText(const char *s, int cx, int y, uint8_t size, uint16_t col);
static void trnTextO(const char *s, int cx, int y, uint8_t size, uint16_t col);
static void trnDrawFx(uint32_t now);
static void trnHud(uint16_t pts, uint32_t now, uint32_t until, uint32_t total, uint16_t ink);
static bool trnDrawIntro(uint32_t now, const char *hint);
static void trnStar(int cx, int cy, int r, uint16_t col);
static void trnPet(uint8_t act, int x, int ground, uint32_t t, bool loop, uint8_t maxS);
void drawGameScene();
// ---- from ui_movefx.ino: the 12x12 type sprites and their colour ramps
void fxTypeIcon(uint8_t type, int cx, int cy, int s);
uint16_t fxTypeCol(uint8_t type, uint8_t k);

// ---- strings: ES EN FR DE IT PT, ASCII only
static const char *const S_ARC_TITLE[LANG_COUNT] = { "RECREATIVOS", "ARCADE", "ARCADE", "SPIELHALLE", "SALA GIOCHI", "FLIPERAMA" };
static const char *const S_ARC_CATCH[LANG_COUNT] = { "ATRAPA BAYAS", "BERRY CATCH", "CUEILLETTE", "BEERENFANG", "PRENDI BACCHE", "PEGA BAGAS" };
static const char *const S_ARC_MEMORY[LANG_COUNT] = { "PAREJAS", "MEMORY", "MEMORY", "MEMORY", "MEMORY", "MEMORIA" };
static const char *const S_ARC_ECHO[LANG_COUNT] = { "ECO", "ECHO", "ECHO", "ECHO", "ECO", "ECO" };
static const char *const S_ARC_POP[LANG_COUNT] = { "TOPOS", "POP-UP", "TAUPES", "MAULWURF", "TALPE", "TOUPEIRAS" };
static const char *const S_ARC_BEST[LANG_COUNT] = { "RECORD %u", "BEST %u", "RECORD %u", "BESTE %u", "RECORD %u", "RECORDE %u" };
static const char *const S_ARC_HCATCH[LANG_COUNT] = { "ATRAPA, EVITA ROCAS", "CATCH, DODGE ROCKS", "ATTRAPE, EVITE", "FANGEN, AUSWEICHEN", "PRENDI, SCHIVA", "PEGUE, DESVIE" };
static const char *const S_ARC_HMEMORY[LANG_COUNT] = { "BUSCA PAREJAS", "FIND THE PAIRS", "TROUVE LES PAIRES", "FINDE PAARE", "TROVA LE COPPIE", "ACHE OS PARES" };
static const char *const S_ARC_HECHO[LANG_COUNT] = { "MIRA Y REPITE", "WATCH AND REPEAT", "REGARDE, REPETE", "SCHAU, WIEDERHOLE", "GUARDA E RIPETI", "OLHE E REPITA" };
static const char *const S_ARC_HPOP[LANG_COUNT] = { "GOLPEA, NO LA BOMBA", "BOP THEM, NOT BOMBS", "TAPE, PAS LA BOMBE", "HAUEN, KEINE BOMBE", "COLPISCI, NO BOMBE", "BATA, SEM BOMBAS" };
static const char *const S_ARC_WATCH[LANG_COUNT] = { "MIRA", "WATCH", "REGARDE", "SCHAU", "GUARDA", "OLHE" };
static const char *const S_ARC_TURN[LANG_COUNT] = { "TU TURNO", "YOUR TURN", "A TOI", "DEIN ZUG", "TOCCA A TE", "SUA VEZ" };
static const char *const S_ARC_ROUND[LANG_COUNT] = { "RONDA %u", "ROUND %u", "MANCHE %u", "RUNDE %u", "TURNO %u", "RODADA %u" };
static const char *const S_ARC_REWARD[LANG_COUNT] = { "ALEGRIA +%u VINC +%u", "JOY +%u BOND +%u", "JOIE +%u LIEN +%u", "FREUDE +%u BAND +%u", "GIOIA +%u LEGAME +%u", "ALEGRIA +%u LACO +%u" };
static const char *const S_ARC_PRIZE[LANG_COUNT] = { "PREMIO: %s!", "PRIZE: %s!", "PRIX: %s!", "PREIS: %s!", "PREMIO: %s!", "PREMIO: %s!" };
static const char *const S_ARC_PRIZE_READY[LANG_COUNT] = { "PREMIO DIARIO: RANGO S", "DAILY PRIZE: GET AN S", "PRIX DU JOUR: RANG S", "TAGESPREIS: RANG S", "PREMIO: OTTIENI UNA S", "PREMIO DIARIO: NOTA S" };
static const char *const S_ARC_PRIZE_DONE[LANG_COUNT] = { "PREMIO DE HOY: YA", "PRIZE CLAIMED TODAY", "PRIX DEJA GAGNE", "PREIS HEUTE ERHALTEN", "PREMIO GIA PRESO", "PREMIO JA GANHO" };
static const char *const S_ARC_TAPON[LANG_COUNT] = { "TOCA PARA SEGUIR", "TAP TO GO ON", "TOUCHE POUR SUIVRE", "TIPPEN: WEITER", "TOCCA: AVANTI", "TOQUE P/ SEGUIR" };
static const char *const S_ARC_CLEAR[LANG_COUNT] = { "TABLERO!", "BOARD CLEAR!", "GRILLE OK!", "GESCHAFFT!", "FATTO!", "LIMPO!" };
static const char *const S_ARC_OUCH[LANG_COUNT] = { "AY!", "OUCH!", "AIE!", "AUA!", "AHI!", "AI!" };
static const char *const S_ARC_NEW[LANG_COUNT] = { "NUEVO RECORD!", "NEW BEST!", "RECORD!", "REKORD!", "RECORD!", "RECORDE!" };

// ---- the four games and their tables
#define ARC_GAMES 4
enum : uint8_t { ARC_CATCH = 0, ARC_MEMORY, ARC_ECHO, ARC_POP };
#define ARC_NONE 0xFF            // arcGame on the picker
#define ARC_INTRO_MS 1800UL      // trnDrawIntro's 3-2-1 (600 ms a number)
#define ARC_HEADER_Y 60          // a tap above this leaves a game (banking)
#define ARC_RESULT_MS 7000UL     // the card goes back to the picker by itself
#define ARC_RESULT_TAP_MS 700UL  // ...or on a tap, once it has been up this long
#define ARC_ENERGY 3             // what a scoring session costs

// points for grade B / A / S, per game
static const uint16_t ARC_GRADE_AT[ARC_GAMES][3] = {
  { 25, 50, 80 },     // catch
  { 40, 90, 150 },    // memory
  { 12, 30, 56 },     // echo  (n rounds cleared = n*(n+1): 3, 5, 7 rounds)
  { 20, 40, 65 },     // pop-up
};
// the once-a-day prize for an S, per game (existing items only)
static const uint8_t ARC_PRIZE_ITEM[ARC_GAMES] = { ITEM_ORAN_BERRY, ITEM_POKEBALL, ITEM_POKEBALL, ITEM_ORAN_BERRY };
static const uint16_t ARC_GAME_COL[ARC_GAMES] = { C565(0xd8, 0x3c, 0x4a), C565(0x2a, 0x7a, 0xd0), C565(0x9a, 0x4a, 0xd8), C565(0x9a, 0x6a, 0x3a) };

static const char *const *arcName(uint8_t g) {
  switch (g) { case ARC_CATCH: return S_ARC_CATCH; case ARC_MEMORY: return S_ARC_MEMORY;
               case ARC_ECHO: return S_ARC_ECHO; default: return S_ARC_POP; }
}

// ---- session state (non-static: arcade_ui_test reads it)
uint8_t arcGame = ARC_NONE;
bool arcOver = false;            // the results card is up
uint32_t arcUntil = 0;           // the session clock ends here
uint32_t arcOverAt = 0;
uint16_t arcPts = 0;
uint8_t arcGrade = 0;            // 0 C, 1 B, 2 A, 3 S
bool arcNewHi = false;
uint8_t arcJoyGot = 0, arcBondGot = 0, arcPrizeGot = 0;   // what the session paid
uint16_t arcBest[ARC_GAMES] = { 0 };
uint32_t arcPrizeDay = 0;        // day+1 of the last prize, 0 = never
uint16_t arcSessions = 0;        // sessions banked since boot (tests)
static uint32_t arcLastFrame = 0;
static uint8_t arcStarsShown = 0;

// ---- persistence: "arcHi" 4 x uint16 bests, "arcDay" the prize day (+1)
static Preferences arcPrefs;
static bool arcPrefsOpen = false;
void arcLoad() {
  if (!arcPrefsOpen) { arcPrefs.begin("tamapoke", false); arcPrefsOpen = true; }
  memset(arcBest, 0, sizeof(arcBest));
  if (arcPrefs.getBytesLength("arcHi") == sizeof(arcBest)) arcPrefs.getBytes("arcHi", arcBest, sizeof(arcBest));
  arcPrizeDay = arcPrefs.getUInt("arcDay", 0);
}
static void arcSave() {
  if (!arcPrefsOpen) { arcPrefs.begin("tamapoke", false); arcPrefsOpen = true; }
  arcPrefs.putBytes("arcHi", arcBest, sizeof(arcBest));
  arcPrefs.putUInt("arcDay", arcPrizeDay);
}

// THE gate, for the hub row and every start alike.
bool arcadeAllowed() { return !pet.isEgg() && !pet.sleeping && !pet.ceremony; }
bool arcadePrizeToday() { return arcPrizeDay == pet.currentDay() + 1; }

uint8_t arcGradeOf(uint8_t g, uint16_t pts) {
  if (g >= ARC_GAMES) return 0;
  return pts >= ARC_GRADE_AT[g][2] ? 3 : pts >= ARC_GRADE_AT[g][1] ? 2 : pts >= ARC_GRADE_AT[g][0] ? 1 : 0;
}
uint8_t arcJoyFor(uint8_t grade) { return (uint8_t)(4 + 4 * grade); }    // 4 / 8 / 12 / 16
uint8_t arcBondFor(uint8_t grade) { return grade >= 2 ? 2 : 1; }

static bool arcPlaying() { return arcadeOpen && arcGame != ARC_NONE && !arcOver; }

// The one end of a session. Idempotent: a second call (a swipe on the card)
// pays nothing.
void arcFinish() {
  if (arcGame == ARC_NONE || arcOver) return;
  uint32_t now = millis();
  arcOver = true;
  arcOverAt = now;
  arcStarsShown = 0;
  arcGrade = arcGradeOf(arcGame, arcPts);
  arcNewHi = arcPts > arcBest[arcGame];
  if (arcNewHi) arcBest[arcGame] = arcPts;
  arcJoyGot = arcBondGot = arcPrizeGot = 0;
  if (arcPts > 0) {
    pet.energy = pet.energy > ARC_ENERGY ? pet.energy - ARC_ENERGY : 0;
    arcJoyGot = arcJoyFor(arcGrade);
    arcBondGot = pet.friendVisit(arcJoyGot, arcBondFor(arcGrade));   // joy, capped bond, heart, save
    if (arcGrade == 3 && !arcadePrizeToday()) {
      uint8_t it = ARC_PRIZE_ITEM[arcGame];
      if (bag.add(it)) { arcPrizeGot = it; arcPrizeDay = pet.currentDay() + 1; }
    }
    gameEvent(EV_PLAY, (int16_t)(arcPts > 32767 ? 32767 : arcPts));
  }
  arcSave();
  arcSessions++;
  sfxPlay(arcNewHi && arcPts ? SFX_MEDAL : SFX_LEVEL);
  if (arcGrade >= 2) trnBurst(CX, 140, UI_BAR_WARN, 16, 7.0f);
}

// Leave the game for the picker, banking what was earned.
void arcLeave() {
  arcFinish();
  arcGame = ARC_NONE;
  arcOver = false;
}

// =====================================================================
// BERRY CATCH
// =====================================================================
// The basket (and the creature behind it) runs along the ground after your
// finger: drag anywhere, or tap where it should go. Berries are worth
// 1 + trnMult(combo) - 1, a golden one 5 + trnMult(combo) - 1. A berry that
// lands breaks the combo. From CAT_ROCK_FROM rocks fall too: one in the basket
// costs a life (of CAT_LIVES), the combo and stuns the basket for CAT_STUN_MS.
// Ends at CAT_MS or on the last life. Things fall faster and closer together
// as the session goes on.
#define CAT_MS 45000UL
#define CAT_OBJS 6
#define CAT_GROUND 398
#define CAT_CATCH_Y 352          // an object is judged when it reaches the basket rim
#define CAT_REACH 46             // basket half-width that catches
#define CAT_XMIN 104
#define CAT_XMAX 362
#define CAT_LIVES 3
#define CAT_GOLD_PCT 8
#define CAT_ROCK_FROM 6000UL
#define CAT_ROCK_PCT 25
#define CAT_STUN_MS 700UL
#define CAT_SPEED 0.9f           // basket px per ms
#define CAT_K_NONE 0
#define CAT_K_BERRY 1
#define CAT_K_GOLD 2
#define CAT_K_ROCK 3
// catStep's report of what happened, a bit each
#define CAT_EV_CATCH 1
#define CAT_EV_GOLD 2
#define CAT_EV_ROCK 4
#define CAT_EV_DROP 8

uint8_t catK[CAT_OBJS];
float catX[CAT_OBJS], catY[CAT_OBJS], catV[CAT_OBJS];
bool catPassed[CAT_OBJS];
float catPetX = CX;
int16_t catGoal = CX;
uint8_t catLives = CAT_LIVES;
uint32_t catNext = 0, catStunUntil = 0, catCatchAt = 0;
uint16_t catCaught = 0, catDropped = 0, catRocks = 0;

static int16_t catClampX(int x) { return (int16_t)(x < CAT_XMIN ? CAT_XMIN : x > CAT_XMAX ? CAT_XMAX : x); }

static uint32_t catElapsed(uint32_t now) { return trnInIntro(now) ? 0 : now - trnGoAt; }

void catSpawnAt(uint8_t i, uint8_t kind, int16_t x) {
  uint32_t e = catElapsed(millis());
  catK[i] = kind;
  catX[i] = catClampX(x);
  catY[i] = 70;
  float v = 0.16f + e * 0.004f / 1000.0f;   // px per ms: 0.16 at the start, 0.34 at 45 s
  catV[i] = kind == CAT_K_ROCK ? v * 1.15f : v;
  catPassed[i] = false;
}

static void catSpawn(uint32_t now) {
  uint32_t e = catElapsed(now);
  for (uint8_t i = 0; i < CAT_OBJS; i++) {
    if (catK[i]) continue;
    int r = random(100);
    uint8_t k = (e >= CAT_ROCK_FROM && r < CAT_ROCK_PCT) ? CAT_K_ROCK
              : (random(100) < CAT_GOLD_PCT ? CAT_K_GOLD : CAT_K_BERRY);
    catSpawnAt(i, k, (int16_t)(CAT_XMIN + random(CAT_XMAX - CAT_XMIN + 1)));
    break;
  }
  int gap = 950 - (int)(e * 12 / 1000);    // 950 ms between drops, down to 420
  catNext = now + (gap < 420 ? 420 : gap);
}

void startCatch() {
  catPetX = CX; catGoal = CX;
  catLives = CAT_LIVES;
  catStunUntil = catCatchAt = 0;
  catCaught = catDropped = catRocks = 0;
  for (uint8_t i = 0; i < CAT_OBJS; i++) catK[i] = CAT_K_NONE;
  catNext = trnGoAt + 200;
}

// Advance the basket and every falling thing by dt ms. Returns CAT_EV_* bits.
uint8_t catStep(uint32_t dt) {
  uint32_t now = millis();
  uint8_t ev = 0;
  if (dt > 200) dt = 200;
  if ((int32_t)(now - catStunUntil) >= 0) {
    float d = catGoal - catPetX, mx = CAT_SPEED * dt;
    catPetX += d > mx ? mx : (d < -mx ? -mx : d);
  }
  for (uint8_t i = 0; i < CAT_OBJS && !arcOver; i++) {
    if (!catK[i]) continue;
    catY[i] += catV[i] * dt;
    int x = (int)catX[i];
    if (!catPassed[i] && catY[i] >= CAT_CATCH_Y) {
      catPassed[i] = true;
      float dx = catX[i] - catPetX;
      if (dx >= -CAT_REACH && dx <= CAT_REACH) {
        uint8_t k = catK[i];
        catK[i] = CAT_K_NONE;
        char b[8];
        if (k == CAT_K_ROCK) {
          catRocks++;
          ev |= CAT_EV_ROCK;
          trnCombo = 0;
          catStunUntil = now + CAT_STUN_MS;
          trnPopup(x, CAT_CATCH_Y - 40, TL(S_ARC_OUCH), UI_BAR_BAD, 3);
          trnBurst(x, CAT_CATCH_Y, 0x8410, 12, 6.0f);
          trnJolt(9, 220);
          sfxPlay(SFX_DENY);
          if (catLives) catLives--;
          if (!catLives) { arcFinish(); return ev; }
          continue;
        }
        catCaught++;
        catCatchAt = now;
        trnComboUp();
        uint16_t p = (k == CAT_K_GOLD ? 5 : 1) + trnMult(trnCombo) - 1;
        arcPts += p;
        ev |= k == CAT_K_GOLD ? CAT_EV_GOLD : CAT_EV_CATCH;
        snprintf(b, sizeof(b), "+%u", p);
        trnPopup(x, CAT_CATCH_Y - 34, b, k == CAT_K_GOLD ? UI_BAR_WARN : UI_WHITE, k == CAT_K_GOLD ? 3 : 2);
        trnBurst(x, CAT_CATCH_Y, k == CAT_K_GOLD ? UI_BAR_WARN : C565(0xff, 0x6a, 0x7a), k == CAT_K_GOLD ? 12 : 6, 5.0f);
        if (k == CAT_K_GOLD) sfxPlay(SFX_SUPER);
        continue;
      }
    }
    if (catY[i] >= CAT_GROUND) {
      if (catK[i] == CAT_K_ROCK) trnBurst(x, CAT_GROUND, 0x8410, 5, 3.0f);   // thud, harmless
      else {
        catDropped++;
        ev |= CAT_EV_DROP;
        trnBurst(x, CAT_GROUND, C565(0xc0, 0x30, 0x50), 6, 3.5f);       // splat
        trnComboBreak(x, CAT_GROUND - 20);
      }
      catK[i] = CAT_K_NONE;
    }
  }
  if ((int32_t)(now - catNext) >= 0) catSpawn(now);
  return ev;
}

void catTap(int16_t x, int16_t y) { (void)y; catGoal = catClampX(x); }

static void arcBerry(int x, int y, int r, bool gold, uint32_t now) {
  uint16_t body = gold ? UI_BAR_WARN : C565(0xd8, 0x3c, 0x4a);
  uint16_t dark = gold ? C565(0xb0, 0x7a, 0x10) : C565(0x8a, 0x1c, 0x2c);
  gfx->fillCircle(x, y, r + 2, UI_INK);
  gfx->fillCircle(x, y, r, dark);
  gfx->fillCircle(x - 1, y - 1, r - 2, body);
  gfx->fillCircle(x - r / 3, y - r / 3, r / 4 + 1, UI_WHITE);
  gfx->fillTriangle(x, y - r, x + r, y - r - 8, x + 3, y - r - 2, C565(0x3c, 0xa0, 0x4a));   // leaf
  gfx->fillRect(x - 1, y - r - 5, 3, 6, C565(0x6a, 0x4a, 0x2a));
  if (gold && ((now / 90) & 1)) {
    gfx->drawLine(x - r - 8, y, x + r + 8, y, UI_WHITE);
    gfx->drawLine(x, y - r - 8, x, y + r + 8, UI_WHITE);
  }
}
static void arcRock(int x, int y, int r) {
  gfx->fillCircle(x, y, r + 2, UI_INK);
  gfx->fillCircle(x - r / 3, y + 2, r * 3 / 4 + 2, UI_INK);
  gfx->fillCircle(x, y, r, C565(0x80, 0x7a, 0x74));
  gfx->fillCircle(x - r / 3, y + 2, r * 3 / 4, C565(0x6a, 0x64, 0x5e));
  gfx->fillCircle(x + r / 4, y - r / 3, r / 3, C565(0xb0, 0xaa, 0xa0));
}
// the basket: a woven bowl the creature carries
static void arcBasket(int x, int y, bool stunned, bool caught) {
  int w = CAT_REACH, h = 26, top = y - h;
  uint16_t wood = stunned ? C565(0x9a, 0x9a, 0x9a) : C565(0xc8, 0x8a, 0x3a);
  uint16_t dark = stunned ? C565(0x60, 0x60, 0x60) : C565(0x8a, 0x56, 0x1c);
  gfx->fillRoundRect(x - w - 3, top - 3 - (caught ? 3 : 0), 2 * w + 6, h + 6, 10, UI_INK);
  gfx->fillRoundRect(x - w, top - (caught ? 3 : 0), 2 * w, h, 9, wood);
  for (int k = 0; k < 3; k++) gfx->fillRect(x - w + 6, top + 6 + k * 7 - (caught ? 3 : 0), 2 * w - 12, 2, dark);
  gfx->fillRect(x - w - 3, top - 5 - (caught ? 3 : 0), 2 * w + 6, 6, dark);
}

static void renderCatchPlay(uint32_t now, bool intro, uint16_t ink) {
  drawGameScene();
  int jx = trnJx(now), jy = trnJy(now);
  bool stunned = (int32_t)(now - catStunUntil) < 0;
  // the creature behind its basket (nothing drawn without an SD; the basket stays)
  float step = catGoal - catPetX;
  uint8_t act = stunned ? PMD_HURT : step > 3 ? PMD_WALKR : step < -3 ? PMD_WALKL : PMD_IDLE;
  trnPet(act, (int)catPetX + jx, CAT_GROUND - 22, now, true, 2);
  arcBasket((int)catPetX + jx, CAT_CATCH_Y + 26 + jy, stunned, catCatchAt && now - catCatchAt < 120);
  if (stunned && ((now / 80) & 1)) trnStar((int)catPetX + 30, CAT_CATCH_Y - 30, 9, UI_BAR_WARN);
  if (!intro)
    for (uint8_t i = 0; i < CAT_OBJS; i++) {
      if (!catK[i]) continue;
      int x = (int)catX[i] + jx, y = (int)catY[i] + jy;
      if (catK[i] == CAT_K_ROCK) arcRock(x, y, 18);
      else arcBerry(x, y, catK[i] == CAT_K_GOLD ? 16 : 14, catK[i] == CAT_K_GOLD, now);
    }
  trnHud(arcPts, now, arcUntil, CAT_MS, ink);
  for (int i = 0; i < CAT_LIVES; i++) {       // lives
    int hx = CX - 28 + i * 28;
    if (i < catLives) gfx->fillCircle(hx, 98, 7, UI_BAR_BAD);
    else gfx->drawCircle(hx, 98, 7, UI_TRACK);
  }
}

// =====================================================================
// MEMORY
// =====================================================================
// A 4x4 board of face-down cards, 8 pairs of type symbols drawn with the move
// effects' own sprites (ui_movefx.ino). Flip two: a pair stays up and is worth
// 10 x trnMult(combo), where the combo counts pairs in a row; a mismatch shows
// for MEM_PEEK_MS and turns back (no points lost, combo broken) -- or at once
// on the next tap. Clearing a board is worth MEM_CLEAR_PTS and deals a fresh
// one. MEM_MS on the clock.
#define MEM_MS 60000UL
#define MEM_CARDS 16
#define MEM_CW 62
#define MEM_CH 62
#define MEM_GAP 8
#define MEM_X0 (CX - (4 * MEM_CW + 3 * MEM_GAP) / 2)
#define MEM_Y0 116
#define MEM_PEEK_MS 700UL
#define MEM_FLIP_MS 160UL
#define MEM_CLEAR_PTS 20
#define MEM_DEAL_MS 650UL
#define MEM_DOWN 0
#define MEM_UP 1
#define MEM_DONE 2
// memTap's return
#define MEM_T_NONE 0
#define MEM_T_FLIP 1
#define MEM_T_MATCH 2
#define MEM_T_MISS 3
#define MEM_T_CLEAR 4

// the twelve types whose sprites are all different shapes
static const uint8_t MEM_TYPES[12] = { T_NORMAL, T_FIRE, T_WATER, T_ELECTRIC, T_GRASS, T_ICE,
                                       T_POISON, T_GROUND, T_FLYING, T_PSYCHIC, T_BUG, T_FAIRY };
uint8_t memType[MEM_CARDS], memState[MEM_CARDS];
uint32_t memFlipAt[MEM_CARDS];
int8_t memA = -1, memB = -1;
uint32_t memHideAt = 0, memDealAt = 0;
uint16_t memPairs = 0, memMisses = 0, memBoards = 0;

void memDeal() {
  uint8_t pool[12];
  memcpy(pool, MEM_TYPES, sizeof(pool));
  for (int i = 0; i < 8; i++) { int j = i + random(12 - i); uint8_t t = pool[i]; pool[i] = pool[j]; pool[j] = t; }
  for (int i = 0; i < MEM_CARDS; i++) memType[i] = pool[i / 2];
  for (int i = MEM_CARDS - 1; i > 0; i--) { int j = random(i + 1); uint8_t t = memType[i]; memType[i] = memType[j]; memType[j] = t; }
  uint32_t now = millis();
  for (int i = 0; i < MEM_CARDS; i++) { memState[i] = MEM_DOWN; memFlipAt[i] = now; }
  memA = memB = -1;
  memDealAt = 0;
}

void startMemory() {
  memPairs = memMisses = memBoards = 0;
  memDeal();
}

static void memCardRect(uint8_t i, int &x, int &y) {
  x = MEM_X0 + (i % 4) * (MEM_CW + MEM_GAP);
  y = MEM_Y0 + (i / 4) * (MEM_CH + MEM_GAP);
}
int memCardAt(int16_t x, int16_t y) {
  for (uint8_t i = 0; i < MEM_CARDS; i++) {
    int cx, cy;
    memCardRect(i, cx, cy);
    // the gap is split between neighbours: no dead strip between cards
    if (x >= cx - MEM_GAP / 2 && x < cx + MEM_CW + MEM_GAP / 2 && y >= cy - MEM_GAP / 2 && y < cy + MEM_CH + MEM_GAP / 2) return i;
  }
  return -1;
}

static void memHide() {
  uint32_t now = millis();
  if (memA >= 0) { memState[memA] = MEM_DOWN; memFlipAt[memA] = now; }
  if (memB >= 0) { memState[memB] = MEM_DOWN; memFlipAt[memB] = now; }
  memA = memB = -1;
}

uint8_t memTap(int16_t x, int16_t y) {
  uint32_t now = millis();
  if (memDealAt) return MEM_T_NONE;              // a fresh board is being dealt
  if (memB >= 0) memHide();                      // a mismatch on show: turn it back now
  int i = memCardAt(x, y);
  if (i < 0 || memState[i] != MEM_DOWN) return MEM_T_NONE;
  memState[i] = MEM_UP;
  memFlipAt[i] = now;
  sfxPlay(SFX_TAP);
  if (memA < 0) { memA = (int8_t)i; return MEM_T_FLIP; }
  memB = (int8_t)i;
  int ax, ay, bx, by;
  memCardRect((uint8_t)memA, ax, ay);
  memCardRect((uint8_t)memB, bx, by);
  if (memType[memA] != memType[memB]) {
    memMisses++;
    memHideAt = now + MEM_PEEK_MS;
    trnComboBreak(bx + MEM_CW / 2, by);
    trnCross(bx + MEM_CW / 2, by + MEM_CH / 2);
    return MEM_T_MISS;
  }
  memState[memA] = memState[memB] = MEM_DONE;
  memPairs++;
  trnComboUp();
  uint16_t p = 10 * trnMult(trnCombo);
  arcPts += p;
  char b[8];
  snprintf(b, sizeof(b), "+%u", p);
  uint16_t col = fxTypeCol(memType[memB], 3);
  trnPopup(bx + MEM_CW / 2, by - 6, b, col, 3);
  trnBurst(ax + MEM_CW / 2, ay + MEM_CH / 2, col, 8, 5.0f);
  trnBurst(bx + MEM_CW / 2, by + MEM_CH / 2, col, 8, 5.0f);
  memA = memB = -1;
  for (uint8_t k = 0; k < MEM_CARDS; k++) if (memState[k] != MEM_DONE) return MEM_T_MATCH;
  arcPts += MEM_CLEAR_PTS;                       // the whole board
  memBoards++;
  memDealAt = now + MEM_DEAL_MS;
  trnBanner(TL(S_ARC_CLEAR), UI_BAR_WARN, 900);
  trnJolt(5, 160);
  sfxPlay(SFX_SUPER);
  return MEM_T_CLEAR;
}

static void memDrawCard(uint8_t i, uint32_t now) {
  int x, y;
  memCardRect(i, x, y);
  // a flip squeezes the card to nothing and opens it on the other face
  uint32_t ft = now - memFlipAt[i];
  bool faceUp = memState[i] != MEM_DOWN;
  int w = MEM_CW;
  if (ft < MEM_FLIP_MS) {
    int half = MEM_FLIP_MS / 2;
    int k = (int)ft < half ? half - (int)ft : (int)ft - half;
    w = MEM_CW * k / half;
    if ((int)ft < half) faceUp = !faceUp;        // the first half still shows the old face
    if (w < 4) w = 4;
  }
  int cx = x + MEM_CW / 2, lx = cx - w / 2;
  gfx->fillRoundRect(lx - 2, y - 2, w + 4, MEM_CH + 4, 10, UI_INK);
  if (!faceUp) {
    gfx->fillRoundRect(lx, y, w, MEM_CH, 8, C565(0x2a, 0x4a, 0x9a));
    if (w > 30) {
      gfx->drawRoundRect(lx + 5, y + 5, w - 10, MEM_CH - 10, 6, C565(0x6a, 0x8a, 0xda));
      gfx->fillCircle(cx, y + MEM_CH / 2, 13, UI_WHITE);
      gfx->fillCircle(cx, y + MEM_CH / 2, 9, C565(0xd8, 0x3c, 0x4a));
      gfx->fillRect(cx - 13, y + MEM_CH / 2 - 1, 26, 3, UI_INK);
      gfx->fillCircle(cx, y + MEM_CH / 2, 4, UI_WHITE);
    }
    return;
  }
  uint8_t t = memType[i];
  bool done = memState[i] == MEM_DONE;
  gfx->fillRoundRect(lx, y, w, MEM_CH, 8, done ? fxTypeCol(t, 3) : UI_WHITE);
  if (w > 40) {
    gfx->drawRoundRect(lx + 3, y + 3, w - 6, MEM_CH - 6, 6, fxTypeCol(t, 2));
    fxTypeIcon(t, cx, y + MEM_CH / 2, 4);
  }
  if (done && ((now / 400 + i) % 6 == 0)) {      // a matched pair twinkles now and then
    gfx->drawLine(lx + w - 12, y + 6, lx + w - 6, y + 12, UI_WHITE);
    gfx->drawLine(lx + w - 6, y + 6, lx + w - 12, y + 12, UI_WHITE);
  }
}

static void renderMemoryPlay(uint32_t now, bool intro) {
  // a card table: dark felt with a lighter rim
  gfx->fillScreen(RGB565_BLACK);
  gfx->fillCircle(CX, CY, 232, C565(0x1c, 0x5a, 0x4a));
  gfx->fillCircle(CX, CY, 214, C565(0x16, 0x4a, 0x3c));
  if (memB >= 0 && (int32_t)(now - memHideAt) >= 0) memHide();
  if (memDealAt && (int32_t)(now - memDealAt) >= 0) memDeal();
  if (!intro && !memDealAt)
    for (uint8_t i = 0; i < MEM_CARDS; i++) memDrawCard(i, now);
  trnHud(arcPts, now, arcUntil, MEM_MS, UI_WHITE);
  char b[16];
  snprintf(b, sizeof(b), "%u/8", (unsigned)(memPairs % 8 == 0 && memDealAt ? 8 : memPairs % 8));
  trnText(b, CX, 96, 2, C565(0xa0, 0xd8, 0xc0));
}

// =====================================================================
// ECHO
// =====================================================================
// Four type pads round a ring -- FIRE top, WATER right, GRASS bottom,
// ELECTRIC left -- each with its own note. The board plays a sequence; repeat
// it. Every right pad is 1 point, a cleared round is worth its length again,
// and the next round adds one pad (so clearing n rounds is n*(n+1) points). A
// wrong pad, or ECHO_IDLE_MS without a tap on your turn, costs one of
// ECHO_LIVES and replays the same sequence; the last one ends the game. The
// playback quickens with the length. ECHO_MS caps the session.
#define ECHO_MS 60000UL
#define ECHO_MAX 32
#define ECHO_LIVES 2
#define ECHO_IDLE_MS 5000UL
#define ECHO_GAP_MS 140
#define ECHO_PAUSE_MS 700UL
#define ECHO_CY 250
#define ECHO_RIN 72
#define ECHO_ROUT 176
#define ECHO_LIT_MS 220UL
#define ECHO_SHOW 0
#define ECHO_INPUT 1
#define ECHO_PAUSE 2
// echoTap's return
#define ECHO_T_NONE 0
#define ECHO_T_OK 1
#define ECHO_T_ROUND 2
#define ECHO_T_WRONG 3

static const uint8_t ECHO_TYPE[4] = { T_FIRE, T_WATER, T_GRASS, T_ELECTRIC };
static const uint8_t ECHO_NOTE[4] = { 0, 2, 4, 7 };   // C E G C' on sfxRise's scale
uint8_t echoSeq[ECHO_MAX];
uint8_t echoLen = 0, echoPos = 0, echoState = ECHO_SHOW, echoLives = ECHO_LIVES;
uint32_t echoT0 = 0, echoLitUntil = 0;
int8_t echoLit = -1, echoShown = -1;
uint16_t echoRounds = 0;
static bool echoReplay = false;               // the pause after a mistake replays, not extends

uint16_t echoOnMs() { int on = 460 - 18 * echoLen; return (uint16_t)(on < 240 ? 240 : on); }

static void echoBegin(uint8_t state, uint32_t at) { echoState = state; echoT0 = at; echoPos = 0; echoShown = -1; }

void startEcho() {
  echoLen = 1;
  echoSeq[0] = (uint8_t)random(4);
  echoRounds = 0;
  echoLives = ECHO_LIVES;
  echoLit = -1; echoLitUntil = 0;
  echoReplay = false;
  echoBegin(ECHO_SHOW, trnGoAt + 300);
}

int16_t echoPadX(uint8_t p) { return (int16_t)(CX + (p == 1 ? 120 : p == 3 ? -120 : 0)); }
int16_t echoPadY(uint8_t p) { return (int16_t)(ECHO_CY + (p == 0 ? -120 : p == 2 ? 120 : 0)); }

int echoPadAt(int16_t x, int16_t y) {
  int dx = x - CX, dy = y - ECHO_CY;
  int d2 = dx * dx + dy * dy;
  if (d2 < (ECHO_RIN - 10) * (ECHO_RIN - 10) || d2 > (ECHO_ROUT + 24) * (ECHO_ROUT + 24)) return -1;
  int ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
  if (ay >= ax) return dy < 0 ? 0 : 2;
  return dx > 0 ? 1 : 3;
}

static void echoLight(uint8_t p, uint32_t now, uint32_t ms) {
  echoLit = (int8_t)p;
  echoLitUntil = now + ms;
  sfxRise(ECHO_NOTE[p & 3]);
}

static void echoMistake(uint32_t now, int8_t wrongPad) {
  trnCombo = 0;
  trnJolt(9, 220);
  sfxPlay(SFX_DENY);
  if (wrongPad >= 0) trnCross(echoPadX(wrongPad), echoPadY(wrongPad));
  // show which pad it should have been
  echoLit = (int8_t)echoSeq[echoPos];
  echoLitUntil = now + 600;
  if (echoLives) echoLives--;
  if (!echoLives) { arcFinish(); return; }
  echoReplay = true;
  echoBegin(ECHO_PAUSE, now);
}

uint8_t echoTap(int16_t x, int16_t y) {
  uint32_t now = millis();
  if (echoState != ECHO_INPUT) return ECHO_T_NONE;
  int p = echoPadAt(x, y);
  if (p < 0) return ECHO_T_NONE;               // the hub or outside the ring: nothing
  if (p != echoSeq[echoPos]) { echoMistake(now, (int8_t)p); return ECHO_T_WRONG; }
  echoLight((uint8_t)p, now, ECHO_LIT_MS);
  echoT0 = now;                                // the idle clock restarts on every right pad
  arcPts += 1;
  trnBurst(echoPadX(p), echoPadY(p), fxTypeCol(ECHO_TYPE[p], 3), 5, 4.0f);
  if (++echoPos < echoLen) return ECHO_T_OK;
  // the round: worth its length again
  arcPts += echoLen;
  echoRounds++;
  trnComboUp();
  char b[8];
  snprintf(b, sizeof(b), "+%u", echoLen);
  trnPopup(CX, ECHO_CY - 30, b, UI_BAR_WARN, 3);
  trnBurst(CX, ECHO_CY, UI_BAR_WARN, 10, 6.0f);
  echoReplay = false;
  echoBegin(ECHO_PAUSE, now);
  return ECHO_T_ROUND;
}

// The playback and the waits, once a frame.
void echoStep(uint32_t now) {
  if (echoLit >= 0 && (int32_t)(now - echoLitUntil) >= 0) echoLit = -1;
  if ((int32_t)(now - echoT0) < 0) return;
  uint32_t t = now - echoT0;
  if (echoState == ECHO_PAUSE) {
    if (t < ECHO_PAUSE_MS) return;
    if (!echoReplay && echoLen < ECHO_MAX) echoSeq[echoLen++] = (uint8_t)random(4);
    echoBegin(ECHO_SHOW, now + 200);
    return;
  }
  if (echoState == ECHO_SHOW) {
    uint32_t on = echoOnMs(), step = on + ECHO_GAP_MS;
    uint32_t k = t / step;
    if (k >= echoLen) { echoBegin(ECHO_INPUT, now); echoLit = -1; return; }
    if (t % step < on) {
      if (echoShown != (int8_t)k) { echoShown = (int8_t)k; echoLight(echoSeq[k], now, on); }
    }
    return;
  }
  // your turn: too long without a tap is a mistake
  if (t >= ECHO_IDLE_MS) echoMistake(now, -1);
}

static void echoSector(float a0, float a1, int r0, int r1, uint16_t col) {
  const float st = 0.105f;   // ~6 degrees a slice
  for (float a = a0; a < a1 - 0.001f; a += st) {
    float b = a + st > a1 ? a1 : a + st;
    float ca = cosf(a), sa = sinf(a), cb = cosf(b), sb = sinf(b);
    int x0 = CX + (int)(ca * r0), y0 = ECHO_CY + (int)(sa * r0);
    int x1 = CX + (int)(ca * r1), y1 = ECHO_CY + (int)(sa * r1);
    int x2 = CX + (int)(cb * r1), y2 = ECHO_CY + (int)(sb * r1);
    int x3 = CX + (int)(cb * r0), y3 = ECHO_CY + (int)(sb * r0);
    gfx->fillTriangle(x0, y0, x1, y1, x2, y2, col);
    gfx->fillTriangle(x0, y0, x2, y2, x3, y3, col);
  }
}

static void renderEchoPlay(uint32_t now, bool intro) {
  gfx->fillScreen(RGB565_BLACK);
  gfx->fillCircle(CX, CY, 232, C565(0x1e, 0x16, 0x3a));
  if (!intro && !arcOver) echoStep(now);
  if (arcOver) return;
  int jx = trnJx(now), jy = trnJy(now);
  // pad p spans the quarter centred on its direction, with a small gap
  static const float MID[4] = { -1.5708f, 0.0f, 1.5708f, 3.1416f };
  gfx->fillCircle(CX + jx, ECHO_CY + jy, ECHO_ROUT + 6, C565(0x0e, 0x0a, 0x20));
  for (uint8_t p = 0; p < 4; p++) {
    bool lit = echoLit == (int8_t)p;
    uint8_t t = ECHO_TYPE[p];
    int ro = ECHO_ROUT + (lit ? 4 : 0);
    echoSector(MID[p] - 0.72f, MID[p] + 0.72f, ECHO_RIN, ro, lit ? fxTypeCol(t, 3) : fxTypeCol(t, 1));
    echoSector(MID[p] - 0.66f, MID[p] + 0.66f, ECHO_RIN + 6, ro - 6, lit ? UI_WHITE : fxTypeCol(t, 2));
    if (lit) echoSector(MID[p] - 0.60f, MID[p] + 0.60f, ECHO_RIN + 12, ro - 12, fxTypeCol(t, 3));
    gfx->fillCircle(echoPadX(p) + jx, echoPadY(p) + jy, lit ? 30 : 26, fxTypeCol(t, lit ? 4 : 0));
    fxTypeIcon(t, echoPadX(p) + jx, echoPadY(p) + jy, lit ? 4 : 3);
  }
  // the hub: whose turn, the round, the lives
  gfx->fillCircle(CX + jx, ECHO_CY + jy, ECHO_RIN - 6, UI_INK);
  gfx->fillCircle(CX + jx, ECHO_CY + jy, ECHO_RIN - 10, echoState == ECHO_INPUT ? C565(0x2a, 0x6a, 0x4a) : C565(0x3a, 0x2a, 0x6a));
  char b[16];
  snprintf(b, sizeof(b), TL(S_ARC_ROUND), (unsigned)echoLen);
  trnText(b, CX, 432, 2, C565(0xc0, 0xc0, 0xe0));
  if (!intro) {
    const char *s = echoState == ECHO_INPUT ? TL(S_ARC_TURN) : TL(S_ARC_WATCH);
    trnText(s, CX + jx, ECHO_CY - 14 + jy, strlen(s) * 12 <= 116 ? 2 : 1, UI_WHITE);
  }
  for (int i = 0; i < ECHO_LIVES; i++) {
    int hx = CX - 10 + i * 20;
    if (i < echoLives) gfx->fillCircle(hx + jx, ECHO_CY + 18 + jy, 6, UI_BAR_BAD);
    else gfx->drawCircle(hx + jx, ECHO_CY + 18 + jy, 6, 0x8410);
  }
  // your turn: the idle clock as a thin arc round the hub
  if (echoState == ECHO_INPUT && !intro) {
    uint32_t t = now - echoT0;
    if (t < ECHO_IDLE_MS) {
      int n = (int)(24 - t * 24 / ECHO_IDLE_MS);
      for (int k = 0; k < n; k++) {
        float a = -1.5708f + k * 0.2618f;
        gfx->fillCircle(CX + (int)(cosf(a) * (ECHO_RIN - 2)), ECHO_CY + (int)(sinf(a) * (ECHO_RIN - 2)), 2,
                        t > ECHO_IDLE_MS - 1500 ? UI_BAR_BAD : UI_BAR_OK);
      }
    }
  }
  trnHud(arcPts, now, arcUntil, ECHO_MS, UI_WHITE);
}

// =====================================================================
// POP-UP
// =====================================================================
// Seven holes. A critter pops up for a moment; tap it before it ducks back.
// A critter is worth 1 + trnMult(combo) - 1, a golden one (POP_GOLD_PCT, a
// shorter stay) 3 + trnMult(combo) - 1. From POP_BOMB_FROM a BOMB can pop up:
// tapping it costs POP_BOMB_COST points and the combo; left alone it fizzles.
// A critter that ducks untapped, or a tap on empty ground, breaks the combo.
// One critter at a time at first, two from POP_TWO_FROM, three from
// POP_THREE_FROM; each stays a little shorter with every hit. POP_MS long.
#define POP_MS 45000UL
#define POP_HOLES 7
#define POP_LIFE0 1150
#define POP_LIFE_MIN 550
#define POP_RISE_MS 120
#define POP_HIT_MS 260UL
#define POP_HIT_R 58             // touch radius around a critter
#define POP_GOLD_PCT 10
#define POP_BOMB_PCT 18
#define POP_BOMB_FROM 5000UL
#define POP_BOMB_LIFE 1300
#define POP_BOMB_COST 5
#define POP_TWO_FROM 8000UL
#define POP_THREE_FROM 25000UL
#define POP_GAP_MS 480UL
#define POP_K_NONE 0
#define POP_K_MOLE 1
#define POP_K_GOLD 2
#define POP_K_BOMB 3
// popTap's return
#define POP_T_NONE 0
#define POP_T_HIT 1
#define POP_T_GOLD 2
#define POP_T_BOMB 3
#define POP_T_WHIFF 4

static const int16_t POP_HX[POP_HOLES] = { 163, 303, 103, 233, 363, 163, 303 };
static const int16_t POP_HY[POP_HOLES] = { 186, 186, 282, 282, 282, 378, 378 };
uint8_t popK[POP_HOLES];
uint32_t popBorn[POP_HOLES], popHitAt[POP_HOLES];
uint16_t popLife[POP_HOLES];
uint32_t popNext = 0;
uint16_t popHits = 0, popGolds = 0, popBombs = 0, popMissed = 0, popWhiffs = 0;

int16_t popHoleX(uint8_t h) { return POP_HX[h % POP_HOLES]; }
int16_t popHoleY(uint8_t h) { return POP_HY[h % POP_HOLES]; }

static uint16_t popLifeNow() { int l = POP_LIFE0 - popHits * 14; return (uint16_t)(l < POP_LIFE_MIN ? POP_LIFE_MIN : l); }

void popSpawnAt(uint8_t h, uint8_t kind) {
  popK[h] = kind;
  popBorn[h] = millis();
  popHitAt[h] = 0;
  uint16_t l = popLifeNow();
  popLife[h] = kind == POP_K_BOMB ? POP_BOMB_LIFE : kind == POP_K_GOLD ? (uint16_t)(l * 3 / 4) : l;
}

void startPop() {
  for (uint8_t h = 0; h < POP_HOLES; h++) popK[h] = POP_K_NONE;
  popHits = popGolds = popBombs = popMissed = popWhiffs = 0;
  popNext = trnGoAt + 250;
}

static uint8_t popUp() {
  uint8_t n = 0;
  for (uint8_t h = 0; h < POP_HOLES; h++) if (popK[h] && !popHitAt[h] && popK[h] != POP_K_BOMB) n++;
  return n;
}

void popStep(uint32_t now) {
  uint32_t e = trnInIntro(now) ? 0 : now - trnGoAt;
  for (uint8_t h = 0; h < POP_HOLES; h++) {
    if (!popK[h]) continue;
    if (popHitAt[h]) { if (now - popHitAt[h] >= POP_HIT_MS) popK[h] = POP_K_NONE; continue; }
    if (now - popBorn[h] < popLife[h]) continue;
    if (popK[h] == POP_K_BOMB) trnBurst(POP_HX[h], POP_HY[h] - 20, 0x8410, 5, 2.5f);   // fizzles
    else { popMissed++; trnComboBreak(POP_HX[h], POP_HY[h] - 60); }
    popK[h] = POP_K_NONE;
  }
  uint8_t cap = e >= POP_THREE_FROM ? 3 : e >= POP_TWO_FROM ? 2 : 1;
  if ((int32_t)(now - popNext) < 0 || popUp() >= cap) return;
  uint8_t free[POP_HOLES], nf = 0;
  for (uint8_t h = 0; h < POP_HOLES; h++) if (!popK[h]) free[nf++] = h;
  if (!nf) return;
  uint8_t h = free[random(nf)];
  int r = random(100);
  uint8_t k = (e >= POP_BOMB_FROM && r < POP_BOMB_PCT) ? POP_K_BOMB
            : (random(100) < POP_GOLD_PCT ? POP_K_GOLD : POP_K_MOLE);
  popSpawnAt(h, k);
  popNext = now + POP_GAP_MS + random(220);
}

uint8_t popTap(int16_t x, int16_t y) {
  uint32_t now = millis();
  // a critter wins over a bomb it is next to
  int hit = -1;
  for (int pass = 0; pass < 2 && hit < 0; pass++)
    for (uint8_t h = 0; h < POP_HOLES; h++) {
      uint8_t k = popK[h];
      if (!k || popHitAt[h] || (pass == 0) == (k == POP_K_BOMB)) continue;
      int dx = x - POP_HX[h], dy = y - (POP_HY[h] - 30);
      if (dx * dx + dy * dy <= POP_HIT_R * POP_HIT_R) { hit = h; break; }
    }
  if (hit < 0) {
    popWhiffs++;
    trnComboBreak(x, y);
    return POP_T_WHIFF;
  }
  uint8_t k = popK[hit];
  popHitAt[hit] = now;
  int hx = POP_HX[hit], hy = POP_HY[hit] - 40;
  char b[8];
  if (k == POP_K_BOMB) {
    popBombs++;
    arcPts = arcPts > POP_BOMB_COST ? arcPts - POP_BOMB_COST : 0;
    trnCombo = 0;
    snprintf(b, sizeof(b), "-%u", POP_BOMB_COST);
    trnPopup(hx, hy - 20, b, UI_BAR_BAD, 3);
    trnBurst(hx, hy, UI_BAR_BAD, 14, 7.0f);
    trnBurst(hx, hy, UI_BAR_WARN, 8, 5.0f);
    trnJolt(10, 240);
    sfxPlay(SFX_DENY);
    return POP_T_BOMB;
  }
  popHits++;
  trnComboUp();
  uint16_t p = (k == POP_K_GOLD ? 3 : 1) + trnMult(trnCombo) - 1;
  arcPts += p;
  snprintf(b, sizeof(b), "+%u", p);
  if (k == POP_K_GOLD) {
    popGolds++;
    trnPopup(hx, hy - 20, b, UI_BAR_WARN, 3);
    trnBurst(hx, hy, UI_BAR_WARN, 12, 6.0f);
    sfxPlay(SFX_SUPER);
  } else {
    trnPopup(hx, hy - 20, b, UI_WHITE, 2);
    trnBurst(hx, hy, UI_WHITE, 6, 5.0f);
    sfxPlay(SFX_HIT);
  }
  return k == POP_K_GOLD ? POP_T_GOLD : POP_T_HIT;
}

static void arcEllipse(int cx, int cy, int rx, int ry, uint16_t col) {
  for (int dy = -ry; dy <= ry; dy++) {
    int w = (int)(rx * sqrtf(1.0f - (float)(dy * dy) / (float)(ry * ry)));
    gfx->drawFastHLine(cx - w, cy + dy, 2 * w + 1, col);
  }
}

// An original burrowing critter: a round brown head with a pink snout, two
// beady eyes and little paws on the rim. `rise` 0..48 is how far it is up;
// everything below the hole's rim is simply not drawn.
static void popCritter(int hx, int hy, int rise, uint8_t kind, bool bonked, uint32_t now) {
  int top = hy - rise;                       // the top of the head
  int cy = top + 26;
  int bot = hy + 6;                          // nothing below the hole's near rim
  int bodyH = bot - (top - 2); if (bodyH > 66) bodyH = 66;
  if (kind == POP_K_BOMB) {
    uint16_t body = C565(0x34, 0x30, 0x40);
    gfx->fillCircle(hx, cy, 26, UI_INK);
    gfx->fillCircle(hx, cy, 23, body);
    gfx->fillCircle(hx - 8, cy - 9, 6, C565(0x70, 0x6a, 0x80));
    gfx->fillRect(hx - 3, top - 8, 6, 10, C565(0x8a, 0x6a, 0x40));            // the fuse
    gfx->fillCircle(hx, top - 10, 5 + (int)((now / 60) & 1) * 2, (now / 60) & 1 ? UI_BAR_WARN : UI_BAR_BAD);
    // angry eyes
    gfx->fillTriangle(hx - 15, cy - 2, hx - 4, cy + 2, hx - 13, cy + 6, UI_BAR_BAD);
    gfx->fillTriangle(hx + 15, cy - 2, hx + 4, cy + 2, hx + 13, cy + 6, UI_BAR_BAD);
  } else {
    bool gold = kind == POP_K_GOLD;
    uint16_t fur = gold ? UI_BAR_WARN : C565(0x8a, 0x5a, 0x34);
    uint16_t dark = gold ? C565(0xb0, 0x7a, 0x10) : C565(0x5a, 0x38, 0x1e);
    uint16_t belly = gold ? C565(0xff, 0xf1, 0xc4) : C565(0xd8, 0xb0, 0x84);
    gfx->fillRoundRect(hx - 30, top - 2, 60, bodyH, 28, UI_INK);
    gfx->fillRoundRect(hx - 27, top + 1, 54, bodyH - 3, 25, fur);
    if (bot - (cy + 4) > 8) gfx->fillRoundRect(hx - 18, cy + 4, 36, bot - (cy + 4) > 40 ? 40 : bot - (cy + 4), 16, belly);
    gfx->fillCircle(hx - 19, top + 6, 6, dark);                                // ears
    gfx->fillCircle(hx + 19, top + 6, 6, dark);
    if (bonked) {                                                              // X eyes
      for (int s = -1; s <= 1; s += 2) {
        int ex = hx + s * 11;
        gfx->drawLine(ex - 4, cy - 10, ex + 4, cy - 2, UI_INK);
        gfx->drawLine(ex + 4, cy - 10, ex - 4, cy - 2, UI_INK);
      }
    } else {
      gfx->fillCircle(hx - 11, cy - 6, 4, UI_INK);
      gfx->fillCircle(hx + 11, cy - 6, 4, UI_INK);
      gfx->fillRect(hx - 12, cy - 9, 2, 2, UI_WHITE);
      gfx->fillRect(hx + 10, cy - 9, 2, 2, UI_WHITE);
    }
    gfx->fillCircle(hx, cy + 4, 7, C565(0xf0, 0x7a, 0x9a));                    // snout
    gfx->fillCircle(hx - 2, cy + 2, 2, UI_WHITE);
    if (gold && ((now / 90) & 1)) trnStar(hx + 26, top + 2, 8, UI_WHITE);
  }
}

static void renderPopPlay(uint32_t now, bool intro) {
  // a meadow: a band of sky over mown grass
  gfx->fillScreen(RGB565_BLACK);
  gfx->fillCircle(CX, CY, 232, C565(0x8f, 0xc8, 0xea));
  for (int y = 120; y < 465; y++) {           // row by row, so the stripes meet the bezel cleanly
    int dy = y - CY, half = 232 * 232 - dy * dy;
    if (half <= 0) continue;
    int w = (int)sqrtf((float)half);
    gfx->drawFastHLine(CX - w, y, 2 * w, ((y / 20) & 1) ? C565(0x6a, 0xb4, 0x5a) : C565(0x5e, 0xa8, 0x50));
  }
  if (!intro && !arcOver) popStep(now);
  int jx = trnJx(now), jy = trnJy(now);
  for (uint8_t h = 0; h < POP_HOLES; h++) {
    int hx = POP_HX[h] + jx, hy = POP_HY[h] + jy;
    arcEllipse(hx, hy, 42, 15, C565(0x4a, 0x30, 0x1c));
    arcEllipse(hx, hy + 2, 36, 11, C565(0x1e, 0x12, 0x0a));
    if (!popK[h] || intro) continue;
    uint32_t age = now - popBorn[h];
    int rise = 52;
    if (popHitAt[h]) {                          // bonked: squash and sink
      uint32_t ht = now - popHitAt[h];
      rise = 52 - (int)(ht * 52 / POP_HIT_MS);
    } else if (age < POP_RISE_MS) rise = (int)(age * 52 / POP_RISE_MS);
    else if (age > (uint32_t)popLife[h] - POP_RISE_MS) rise = (int)((popLife[h] - age) * 52 / POP_RISE_MS);
    if (rise < 0) rise = 0;
    if (rise > 52) rise = 52;
    if (rise > 2) popCritter(hx, hy + 6, rise, popK[h], popHitAt[h] != 0, now);
    // the near rim, drawn over the critter so it sits IN the hole
    for (int dy = 0; dy <= 15; dy++) {
      int w = (int)(42 * sqrtf(1.0f - (float)(dy * dy) / 225.0f));
      gfx->drawFastHLine(hx - w, hy + dy, 2 * w + 1, dy < 4 ? C565(0x4a, 0x30, 0x1c) : ((hy + dy) / 20 & 1 ? C565(0x6a, 0xb4, 0x5a) : C565(0x5e, 0xa8, 0x50)));
    }
    if (popHitAt[h] && now - popHitAt[h] < 160) {   // the bonk
      gfx->drawCircle(hx, hy - 36, 30, UI_WHITE);
      gfx->drawCircle(hx, hy - 36, 34, UI_BAR_WARN);
    }
  }
  trnHud(arcPts, now, arcUntil, POP_MS, UI_INK);
}

// =====================================================================
// the arcade screen: picker, sessions, results
// =====================================================================

void arcStart(uint8_t g) {
  if (g >= ARC_GAMES || !arcadeAllowed()) { sfxPlay(SFX_DENY); return; }
  trnFxReset();
  uint32_t now = millis();
  arcGame = g;
  arcOver = false;
  arcPts = 0;
  arcGrade = 0;
  arcNewHi = false;
  arcJoyGot = arcBondGot = arcPrizeGot = 0;
  trnGoAt = now + ARC_INTRO_MS;
  arcLastFrame = now;
  switch (g) {
    case ARC_CATCH: arcUntil = trnGoAt + CAT_MS; startCatch(); break;
    case ARC_MEMORY: arcUntil = trnGoAt + MEM_MS; startMemory(); break;
    case ARC_ECHO: arcUntil = trnGoAt + ECHO_MS; startEcho(); break;
    default: arcUntil = trnGoAt + POP_MS; startPop(); break;
  }
  sfxPlay(SFX_TAP);
}

// ---- the picker: one tall row per game with its icon and its best
#define ARC_ROW_X 66
#define ARC_ROW_W 334
#define ARC_ROW_H 58
#define ARC_ROW_Y(i) (88 + (i) * 66)

int arcRowAt(int16_t x, int16_t y) {
  if (x < ARC_ROW_X - 6 || x > ARC_ROW_X + ARC_ROW_W + 6) return -1;
  for (uint8_t i = 0; i < ARC_GAMES; i++) {
    int ry = ARC_ROW_Y(i);
    if (y >= ry - 4 && y <= ry + ARC_ROW_H + 4) return i;
  }
  return -1;
}

static void arcIcon(uint8_t g, int cx, int cy, uint32_t now) {
  switch (g) {
    case ARC_CATCH:
      arcBerry(cx, cy + 2, 13, false, now);
      break;
    case ARC_MEMORY:
      gfx->fillRoundRect(cx - 18, cy - 20, 26, 34, 5, UI_INK);
      gfx->fillRoundRect(cx - 16, cy - 18, 22, 30, 4, C565(0x2a, 0x4a, 0x9a));
      gfx->fillRoundRect(cx - 6, cy - 14, 26, 34, 5, UI_INK);
      gfx->fillRoundRect(cx - 4, cy - 12, 22, 30, 4, UI_WHITE);
      fxTypeIcon(T_FIRE, cx + 7, cy + 3, 1);
      break;
    case ARC_ECHO:
      for (uint8_t p = 0; p < 4; p++) {
        uint16_t c = fxTypeCol(ECHO_TYPE[p], ((now / 300) & 3) == p ? 3 : 2);
        int ox = p == 1 ? 7 : p == 3 ? -7 : 0, oy = p == 0 ? -7 : p == 2 ? 7 : 0;
        gfx->fillCircle(cx + ox, cy + oy, 8, c);
      }
      gfx->fillCircle(cx, cy, 5, UI_INK);
      break;
    default:
      arcEllipse(cx, cy + 14, 20, 6, C565(0x1e, 0x12, 0x0a));
      gfx->fillRoundRect(cx - 13, cy - 14, 26, 28, 12, C565(0x8a, 0x5a, 0x34));
      gfx->fillCircle(cx - 5, cy - 6, 2, UI_INK);
      gfx->fillCircle(cx + 5, cy - 6, 2, UI_INK);
      gfx->fillCircle(cx, cy, 4, C565(0xf0, 0x7a, 0x9a));
      break;
  }
}

static void renderArcadePicker(uint32_t now) {
  featBegin(TL(S_ARC_TITLE));
  bool ok = arcadeAllowed();
  for (uint8_t i = 0; i < ARC_GAMES; i++) {
    int y = ARC_ROW_Y(i);
    gfx->fillRoundRect(ARC_ROW_X, y, ARC_ROW_W, ARC_ROW_H, 14, ok ? UI_WHITE : UI_TRACK);
    gfx->drawRoundRect(ARC_ROW_X, y, ARC_ROW_W, ARC_ROW_H, 14, UI_INK);
    gfx->drawRoundRect(ARC_ROW_X + 1, y + 1, ARC_ROW_W - 2, ARC_ROW_H - 2, 13, UI_INK);
    // the coloured tab with the game's icon
    gfx->fillRoundRect(ARC_ROW_X + 6, y + 6, 50, ARC_ROW_H - 12, 10, ok ? ARC_GAME_COL[i] : 0x8410);
    gfx->fillCircle(ARC_ROW_X + 31, y + ARC_ROW_H / 2, 20, UI_WHITE);
    arcIcon(i, ARC_ROW_X + 31, y + ARC_ROW_H / 2, now);
    gfx->setTextColor(ok ? UI_INK : 0x8410);
    gfx->setTextSize(2);
    gfx->setCursor(ARC_ROW_X + 68, y + 10);
    gfx->print(TL(arcName(i)));
    char b[24];
    snprintf(b, sizeof(b), TL(S_ARC_BEST), arcBest[i]);
    gfx->setTextColor(0x8410);
    gfx->setCursor(ARC_ROW_X + 68, y + 32);
    gfx->print(b);
    if (arcBest[i] && arcGradeOf(i, arcBest[i]) == 3)       // a game you have S-ranked wears a star
      trnStar(ARC_ROW_X + ARC_ROW_W - 26, y + ARC_ROW_H / 2, 13, UI_BAR_WARN);
  }
  featLine(358, TL(arcadePrizeToday() ? S_ARC_PRIZE_DONE : S_ARC_PRIZE_READY), arcadePrizeToday() ? 0x8410 : C565(0xc0, 0x80, 0x10));
  featBackPill();
  gfx->flush();
}

// The results card, in the training games' style but with the arcade's own
// lines: points, the best, what the creature got, the prize.
static const uint16_t ARC_GRADE_COL[4] = { 0x8410, 0x3B9F, UI_BAR_OK, UI_BAR_WARN };
static void renderArcResults(uint32_t now) {
  uint32_t t = now - arcOverAt;
  const int px = CX - 156, py = 84, pw = 312, ph = 300;
  gfx->fillRoundRect(px, py, pw, ph, 24, UI_WHITE);
  gfx->drawRoundRect(px, py, pw, ph, 24, UI_INK);
  gfx->drawRoundRect(px + 1, py + 1, pw - 2, ph - 2, 23, UI_INK);
  trnText(TL(arcName(arcGame)), CX, py + 12, 2, ARC_GAME_COL[arcGame < ARC_GAMES ? arcGame : 0]);
  static const char GR[4] = { 'C', 'B', 'A', 'S' };
  char g[2] = { GR[arcGrade & 3], 0 };
  uint8_t gs = t < 140 ? 10 : 8;
  if (arcGrade == 3 && ((now / 160) & 1)) gfx->fillCircle(CX, py + 64, 40, C565(0xff, 0xf1, 0xc4));
  trnText(g, CX, py + 36 + (gs == 10 ? -6 : 0), gs, ARC_GRADE_COL[arcGrade & 3]);
  for (int i = 0; i < 3; i++) {
    uint32_t at = 260 + i * 200;
    int sx = CX - 54 + i * 54, sy = py + 118;
    if (t >= at && i < arcGrade) {
      if (arcStarsShown <= i) { arcStarsShown = (uint8_t)(i + 1); sfxRise((uint8_t)(4 + i * 3)); }
      trnStar(sx, sy, (t - at < 90) ? 19 : 15, UI_BAR_WARN);
    } else trnStar(sx, sy, 15, UI_TRACK);
  }
  char b[40];
  snprintf(b, sizeof(b), TL(S_TRN_PTS), arcPts);
  trnText(b, CX, py + 144, 3, UI_INK);
  if (arcNewHi && arcPts) trnText(TL(S_ARC_NEW), CX, py + 176, 2, ((now / 250) & 1) ? UI_BAR_WARN : UI_BAR_BAD);
  else { snprintf(b, sizeof(b), TL(S_ARC_BEST), arcBest[arcGame < ARC_GAMES ? arcGame : 0]); trnText(b, CX, py + 176, 2, UI_INK); }
  if (trnBestCombo >= 2) { snprintf(b, sizeof(b), TL(S_TRN_COMBO), trnBestCombo); trnText(b, CX, py + 200, 2, 0x8410); }
  if (arcJoyGot) { snprintf(b, sizeof(b), TL(S_ARC_REWARD), arcJoyGot, arcBondGot); trnText(b, CX, py + 224, 2, C565(0xd4, 0x52, 0x7e)); }
  if (arcPrizeGot) {
    snprintf(b, sizeof(b), TL(S_ARC_PRIZE), ITEM_TBL[arcPrizeGot].name);
    trnText(b, CX, py + 248, 2, ((now / 200) & 1) ? UI_BAR_OK : C565(0x1a, 0x8a, 0x3a));
  }
  if (t >= ARC_RESULT_TAP_MS) trnText(TL(S_ARC_TAPON), CX, py + ph - 24, 1, 0x8410);
}

void renderArcade() {
  uint32_t now = millis();
  if (arcGame == ARC_NONE) { renderArcadePicker(now); return; }
  // asleep, an egg, a ceremony: no game runs -- bank it and go back to the list
  if (!arcOver && !arcadeAllowed()) { arcLeave(); renderArcadePicker(now); return; }
  if (arcOver && (int32_t)(now - arcOverAt) >= (int32_t)ARC_RESULT_MS) { arcGame = ARC_NONE; arcOver = false; renderArcadePicker(now); return; }
  bool intro = trnInIntro(now);
  uint32_t dt = now - arcLastFrame;
  arcLastFrame = now;
  if (!arcOver && !intro && (int32_t)(now - arcUntil) >= 0) arcFinish();   // the clock
  bool night = sceneHour() < 6 || sceneHour() >= 20;
  uint16_t ink = night ? UI_INK_NIGHT : UI_INK;
  switch (arcGame) {
    case ARC_CATCH:
      if (!intro && !arcOver) catStep(dt);
      renderCatchPlay(now, intro, ink);
      break;
    case ARC_MEMORY: renderMemoryPlay(now, intro); break;
    case ARC_ECHO:
      renderEchoPlay(now, intro);
      break;
    default: renderPopPlay(now, intro); break;
  }
  if (arcOver) renderArcResults(now);
  trnDrawFx(now);
  if (!arcOver) {
    static const char *const *HINT[ARC_GAMES] = { S_ARC_HCATCH, S_ARC_HMEMORY, S_ARC_HECHO, S_ARC_HPOP };
    trnDrawIntro(now, TL(HINT[arcGame & 3]));
  }
  gfx->flush();
}

void arcadeTap(int16_t x, int16_t y) {
  uint32_t now = millis();
  if (arcGame == ARC_NONE) {
    if (featBackHit(x, y)) { sfxPlay(SFX_TAP); arcadeBack(); return; }
    int r = arcRowAt(x, y);
    if (r >= 0) arcStart((uint8_t)r);
    return;
  }
  if (arcOver) {
    if (now - arcOverAt >= ARC_RESULT_TAP_MS) { arcGame = ARC_NONE; arcOver = false; sfxPlay(SFX_TAP); }
    return;
  }
  // the header strip leaves, banking; no game puts a target up there
  if (y < ARC_HEADER_Y) { arcLeave(); return; }
  if (trnInIntro(now)) return;
  switch (arcGame) {
    case ARC_CATCH: catTap(x, y); break;
    case ARC_MEMORY: memTap(x, y); break;
    case ARC_ECHO: echoTap(x, y); break;
    default: popTap(x, y); break;
  }
}

// A finger held down on BERRY CATCH steers the basket while it moves. Called
// from handleTouch for every poll; it only reads, so the gesture resolver
// still sees the press (a vertical swipe leaves, as everywhere).
void arcadeTouchPeek(bool pressed, int16_t x, int16_t y) {
  if (!pressed || !arcPlaying() || arcGame != ARC_CATCH || y < ARC_HEADER_Y) return;
  catGoal = catClampX(x);
}

// Horizontal: on the list, back to the training menu; in a game, leave it banking --
// except BERRY CATCH, where a horizontal drag is how you play.
void arcadeSwipe(int dir) {
  (void)dir;
  if (arcGame == ARC_NONE) { arcadeBack(); return; }
  if (arcOver) { arcGame = ARC_NONE; arcOver = false; return; }
  if (arcGame == ARC_CATCH) return;
  arcLeave();
}
// Vertical: one level back -- a game (banked) to the list, the list to the
// training menu (featSwipeV does that part).
// True when it was handled here.
bool arcadeSwipeV(int dir) {
  (void)dir;
  if (arcGame == ARC_NONE) return false;
  if (arcOver) { arcGame = ARC_NONE; arcOver = false; return true; }
  arcLeave();
  return true;
}

// ---- training-menu hooks (since v3.20 the arcade is the TRAIN panel's 4th row,
// not an Adventure hub row: the menu with the games is where games are looked for)
const char *arcadeTitle() { return TL(S_ARC_TITLE); }
bool arcadeEnabled() { return arcadeAllowed(); }
// The four game names in the player's language, for the row's second line.
// Cut to fit the row (46 small characters) rather than overflowing it.
void arcadeGamesLine(char *out, size_t n) {
  size_t cap = n < 47 ? n : 47;
  out[0] = 0;
  for (uint8_t g = 0; g < ARC_GAMES; g++) {
    size_t l = strlen(out);
    snprintf(out + l, cap - l, g ? "  %s" : "%s", TL(arcName(g)));
    if (strlen(out) + 1 >= cap) break;
  }
}
void arcadeOpened() { arcGame = ARC_NONE; arcOver = false; arcLoad(); }
void arcadeOpenFromTrain() {
  featCloseAll();
  trainOpen = false;
  arcadeOpen = true;
  arcadeOpened();
}
// The list's way out: back to the training menu it was opened from.
void arcadeBack() {
  arcadeClose();
  trainOpen = true;
}
// Closing the screen from anywhere (featCloseAll) banks a game in progress.
void arcadeClose() {
  if (arcadeOpen && arcGame != ARC_NONE) arcLeave();
  arcGame = ARC_NONE;
  arcOver = false;
  arcadeOpen = false;
}
