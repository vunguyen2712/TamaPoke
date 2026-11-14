// ---------- Battle Tower and gym rematches (features 4 + 6). Logic: tower.cpp ----------
// Owned by this feature alone. Reached from the Adventure hub (ui_hub.ino);
// use featBegin/featRow/featRowAt/featBackPill/featBackHit/featLine from there,
// and leave with featBackToHub(). Every render path MUST end in gfx->flush().
//
// Both fights are the sketch's ordinary trainer battle: they build the squad,
// set btlKind and hand btlOpenTeamFight() a TrainerMon team, which the battle
// copies and owns. The end comes back through towerBattleEnded().
// Design notes: docs/adventure-tower.md.

// ES, EN, FR, DE, IT, PT -- ASCII only.
static const char *const S_TW_TITLE[LANG_COUNT]  = { "TORRE BATALLA", "BATTLE TOWER", "TOUR COMBAT", "DUELLTURM", "TORRE LOTTA", "TORRE BATALHA" };
static const char *const S_TW_LOCKED[LANG_COUNT] = { "TORRE: VENCE A UN CAMPEON", "TOWER: BEAT A CHAMPION", "TOUR: BATS UN MAITRE", "TURM: BESIEGE EINEN CHAMP", "TORRE: BATTI UN CAMPIONE", "TORRE: VENCA UM CAMPEAO" };
static const char *const S_TW_WHY[LANG_COUNT]    = { "vence a un campeon de liga", "beat any league champion", "bats un maitre de la ligue", "besiege einen Liga-Champ", "batti un campione di lega", "venca um campeao da liga" };
static const char *const S_TW_FLOOR[LANG_COUNT]  = { "PISO %u", "FLOOR %u", "ETAGE %u", "EBENE %u", "PIANO %u", "ANDAR %u" };
static const char *const S_TW_STREAK[LANG_COUNT] = { "RACHA %u  MEJOR %u", "STREAK %u  BEST %u", "SERIE %u  RECORD %u", "SERIE %u  BESTE %u", "SERIE %u  RECORD %u", "SEQUENCIA %u  RECORDE %u" };
static const char *const S_TW_NEXTRW[LANG_COUNT] = { "premio al ganar el piso %u", "reward for clearing floor %u", "prix en gagnant l'etage %u", "Preis fuer Ebene %u", "premio vincendo il piano %u", "premio ao vencer o andar %u" };
static const char *const S_TW_RULES[LANG_COUNT]  = { "todos a Nv.50, 3 contra 3", "everyone at Lv.50, 3 vs 3", "tous au N.50, 3 contre 3", "alle auf Lv.50, 3 gegen 3", "tutti al Lv.50, 3 contro 3", "todos no Nv.50, 3 contra 3" };
static const char *const S_TW_SMART[LANG_COUNT]  = { "IA experta", "expert AI", "IA experte", "Experten-KI", "IA esperta", "IA experta" };
static const char *const S_TW_START[LANG_COUNT]  = { "EMPEZAR", "START", "COMMENCER", "STARTEN", "INIZIA", "COMECAR" };
static const char *const S_TW_CONT[LANG_COUNT]   = { "CONTINUAR", "CONTINUE", "CONTINUER", "WEITER", "CONTINUA", "CONTINUAR" };
static const char *const S_TW_REWARD[LANG_COUNT] = { "PREMIO!", "REWARD!", "PRIX!", "PREIS!", "PREMIO!", "PREMIO!" };
static const char *const S_TW_CLEAR[LANG_COUNT]  = { "piso superado", "floor cleared", "etage reussi", "Ebene geschafft", "piano superato", "andar vencido" };
static const char *const S_TW_ENDED[LANG_COUNT]  = { "fin de la racha: %u", "run over: %u wins", "serie finie: %u", "Serie vorbei: %u", "serie finita: %u", "sequencia acabou: %u" };
static const char *const S_RM_PILL[LANG_COUNT]   = { "REVANCHA", "REMATCH", "REVANCHE", "REVANCHE", "RIVINCITA", "REVANCHE" };
static const char *const S_RM_COUNT[LANG_COUNT]  = { "REVANCHAS %u/%u", "REMATCHES %u/%u", "REVANCHES %u/%u", "REVANCHEN %u/%u", "RIVINCITE %u/%u", "REVANCHES %u/%u" };

// tower.cpp is pure logic and cannot name the sketch's pet; bind it once, at
// static-init time, so towerUnlocked() answers for the real creature from the
// first frame (the tasks feature asks it too).
static const bool s_towerPetBound __attribute__((unused)) = towerBindPet(&pet);

// ---- what the last fight left to say on the tower screen
enum : uint8_t { TWM_NONE = 0, TWM_CLEARED, TWM_REWARD, TWM_ENDED };
static uint8_t twMsg = TWM_NONE;
static TowerReward twReward = { { 0 }, { 0 }, 0 };
static uint16_t twEndedAt = 0;
static TowerReward rmReward = { { 0 }, { 0 }, 0 };

// The next floor's team, cached: generating it scans the dex, and the screen
// redraws ten times a second. Keyed on everything it depends on, including the
// packs on the card, so the fight is always exactly the team that was shown.
static TowerTeam twNext;
static uint16_t twNextFloor = 0, twNextArt = 0;
static uint32_t twNextSeed = 0;
static const TowerTeam &twTeam() {
  if (twNextFloor != towerFloor() || twNextSeed != towerSeed() || twNextArt != gRegionArt) {
    twNextFloor = towerFloor();
    twNextSeed = towerSeed();
    twNextArt = gRegionArt;
    if (!towerTeamFor(twNextFloor, twNextSeed, twNext)) twNext.count = 0;
  }
  return twNext;
}

static void twSmall(int y, const char *txt, uint16_t col) {
  gfx->setTextColor(col);
  gfx->setTextSize(1);
  gfx->setCursor(CX - (int)strlen(txt) * 3, y);
  gfx->print(txt);
}

static void twRewardText(const TowerReward &r, char *out, size_t n) {
  out[0] = 0;
  for (uint8_t i = 0; i < r.n; i++) {
    size_t at = strlen(out);
    snprintf(out + at, n - at, "%s%u %s", i ? ", " : "", (unsigned)r.qty[i], ITEM_TBL[r.item[i]].name);
  }
}

// ---------- hub ----------
// Locked rows say WHY, in the row itself: a greyed TOWER with no reason reads
// as a broken button.
void towerHubLabel(char *out, size_t n, const char *name) {
  if (!towerUnlocked()) { snprintf(out, n, "%s", TL(S_TW_LOCKED)); return; }
  if (!towerRunActive()) { snprintf(out, n, "%s", name); return; }
  char f[16];
  snprintf(f, sizeof(f), TL(S_TW_FLOOR), (unsigned)towerFloor());
  snprintf(out, n, "%s - %s", name, f);
}
bool towerHubEnabled() { return towerUnlocked(); }
void towerOpened() { twMsg = TWM_NONE; }

// ---------- the tower screen ----------
void renderTower() {
  featBegin(TL(S_TW_TITLE));
  if (!towerUnlocked()) {
    featLine(150, TL(S_TW_WHY), UI_TRACK);
    featBackPill();
    gfx->flush();
    return;
  }
  char l[64];
  snprintf(l, sizeof(l), TL(S_TW_FLOOR), (unsigned)towerFloor());
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(3);
  gfx->setCursor(CX - (int)strlen(l) * 9, 74);
  gfx->print(l);
  snprintf(l, sizeof(l), TL(S_TW_STREAK), (unsigned)towerStreak(), (unsigned)towerBest());
  featLine(108, l, UI_INK);
  uint16_t nextRw = (towerStreak() / TOWER_REWARD_EVERY + 1) * TOWER_REWARD_EVERY;
  snprintf(l, sizeof(l), TL(S_TW_NEXTRW), (unsigned)nextRw);
  twSmall(132, l, UI_TRACK);

  // who is waiting on this floor
  const TowerTeam &tm = twTeam();
  for (uint8_t i = 0; i < tm.count; i++) {
    int x = CX - 130 + i * 90;
    int16_t d = (int16_t)tm.mon[i].dex;
    const uint8_t *th = thumbs.get(d);
    if (th) drawThumb(th, x, 146, 2, false);
    const char *nm = DEX_TBL[d].name;
    gfx->setTextColor(UI_INK);
    gfx->setTextSize(1);
    gfx->setCursor(x + 40 - (int)strlen(nm) * 3, 230);
    gfx->print(nm);
  }
  snprintf(l, sizeof(l), "%s  IV %u%s%s", towerFoeName(towerFloor()), (unsigned)tm.iv,
           tm.smart ? "  " : "", tm.smart ? TL(S_TW_SMART) : "");
  twSmall(248, l, tm.smart ? UI_BAR_BAD : UI_INK);
  twSmall(264, TL(S_TW_RULES), UI_TRACK);

  if (twMsg == TWM_REWARD) {
    featLine(284, TL(S_TW_REWARD), UI_BAR_OK);
    twRewardText(twReward, l, sizeof(l));
    twSmall(308, l, UI_BAR_OK);
  } else if (twMsg == TWM_CLEARED) {
    featLine(290, TL(S_TW_CLEAR), UI_BAR_OK);
  } else if (twMsg == TWM_ENDED) {
    snprintf(l, sizeof(l), TL(S_TW_ENDED), (unsigned)twEndedAt);
    featLine(290, l, UI_BAR_BAD);
  }
  featRow(5, TL(towerRunActive() ? S_TW_CONT : S_TW_START), tm.count == 0);
  featBackPill();
  gfx->flush();
}

static void towerOpenPicker() {
  pickTrainer = PICK_TOWER;
  pickRematch = false;
  pickHard = false;
  pickPage = 0;
  pickDefault(squadCap(PICK_TOWER, false));
  pickOpen = true;            // the tower screen stays up underneath
}

void towerTap(int16_t x, int16_t y) {
  if (featBackHit(x, y)) { sfxPlay(SFX_TAP); featBackToHub(); return; }
  if (!towerUnlocked()) return;
  if (featRowAt(x, y, 6) == 5) {
    if (!twTeam().count) { sfxPlay(SFX_DENY); return; }
    sfxPlay(SFX_TAP);
    towerOpenPicker();
  }
}

// Not paged: any horizontal swipe goes back to the hub.
void towerSwipe(int dir) { (void)dir; featBackToHub(); }

// The picker's FIGHT for PICK_TOWER. Everyone at TOWER_LEVEL: the squad is
// built from copies (buildSquad's fixLvl), the foes at TOWER_LEVEL by
// construction, so nothing stored is touched.
void towerPickFight() {
  if (pet.ceremony != CER_NONE || !towerUnlocked()) return;
  const TowerTeam &tm = twTeam();
  if (!tm.count) return;
  buildSquad(0, TOWER_TEAM, squadMask, TOWER_LEVEL);
  if (!btlSquadN) return;
  btlKind = BK_TOWER;
  btlTrainer = -1;           // no badge, no win screen, no training
  btlHard = tm.smart;        // the AI tier, nothing else reads it for a tower fight
  twMsg = TWM_NONE;
  btlOpenTeamFight(tm.mon, tm.count, tm.iv, towerFoeName(towerFloor()));
}

// Routed here by featBattleEnded() for BK_TOWER and BK_REMATCH, once per fight,
// before the win screen. A RUN arrives as (false, true): for the tower that
// ends the run exactly like a loss.
void towerBattleEnded(bool won, bool ran) {
  (void)ran;
  if (btlKind == BK_TOWER) {
    if (won) {
      towerWin(&twReward);
      twMsg = twReward.n ? TWM_REWARD : TWM_CLEARED;
    } else {
      twEndedAt = towerStreak();
      towerLoss();
      twMsg = TWM_ENDED;
    }
  } else if (btlKind == BK_REMATCH) {
    rmReward.n = 0;
    if (won && btlTrainer >= 0) rematchWin(btlRegion, (uint8_t)btlTrainer, &rmReward);
  }
}

// ---------- gym rematches: the gym screen's third mode ----------
bool gymRematchOn() {
  return gymRematch && !gymHard && rematchUnlockedFor(pet, gymRegion % GYM_REGIONS);
}

// EASY -> HARD -> REMATCH -> EASY, with REMATCH skipped until this region's
// champion has fallen on easy. Locked never shows as a mode you can enter.
void gymCycleDifficulty() {
  if (gymRematchOn()) { gymRematch = false; gymHard = false; }
  else if (gymHard) { gymHard = false; gymRematch = rematchUnlockedFor(pet, gymRegion % GYM_REGIONS); }
  else { gymHard = true; gymRematch = false; }
}

const char *rematchPillLabel() { return TL(S_RM_PILL); }

// The ONE answer the gym list's draw and its tap both ask.
bool gymRowOpen(uint8_t idx) {
  if (gymRematchOn()) return rematchOpenFor(pet, gymRegion % GYM_REGIONS, idx);
  return gymUnlocked(idx, gymHard);
}
bool gymRowDone(uint8_t idx) {
  if (gymRematchOn()) return rematchWon(gymRegion % GYM_REGIONS, idx);
  return pet.hasBadge(gymRegion, idx, gymHard);
}

// Rematch teams scan the dex to fill, so the list caches them per ladder; the
// cache is dropped when the packs on the card change.
static uint8_t rmTopC[GYM_REGIONS][TRAINER_COUNT], rmCntC[GYM_REGIONS][TRAINER_COUNT];
static bool rmCacheOk[GYM_REGIONS] = { false };
static uint16_t rmCacheArt = 0;
static uint8_t rmCachedTop(uint8_t rg, uint8_t idx, uint8_t *count) {
  rg %= GYM_REGIONS;
  if (idx >= TRAINER_COUNT) { if (count) *count = 0; return 0; }
  if (rmCacheArt != gRegionArt) { for (uint8_t r = 0; r < GYM_REGIONS; r++) rmCacheOk[r] = false; rmCacheArt = gRegionArt; }
  if (!rmCacheOk[rg]) {
    for (uint8_t i = 0; i < TRAINER_COUNT; i++) rmTopC[rg][i] = rematchTop(rg, i, &rmCntC[rg][i]);
    rmCacheOk[rg] = true;
  }
  if (count) *count = rmCntC[rg][idx];
  return rmTopC[rg][idx];
}
uint8_t gymRowTop(uint8_t idx, uint8_t *count) {
  uint8_t rg = gymRegion % GYM_REGIONS;
  if (idx >= TRAINER_COUNT) return 0;
  if (gymRematchOn()) return rmCachedTop(rg, idx, count);
  const Trainer &t = TRAINER_SETS[rg].list[idx];
  uint8_t top = 0;
  for (int k = 0; k < t.count; k++) if (t.team[k].level > top) top = t.team[k].level;
  if (count) *count = t.count;
  return top;
}

// The picker's title for the tower and for a rematch. Returns the level cap
// shown on each cell.
uint8_t pickFoeHeader(char *out, size_t n) {
  if (pickTrainer == PICK_TOWER) {
    char f[16];
    snprintf(f, sizeof(f), TL(S_TW_FLOOR), (unsigned)towerFloor());
    snprintf(out, n, "%s  Lv.%u x%u", f, TOWER_LEVEL, TOWER_TEAM);
    return TOWER_LEVEL;
  }
  uint8_t rg = gymRegion % GYM_REGIONS, cnt = 0;
  uint8_t top = rmCachedTop(rg, pickTrainer, &cnt);   // not a dex scan per frame
  snprintf(out, n, "%s  Lv.%u x%u", TRAINER_SETS[rg].list[pickTrainer % TRAINER_COUNT].name, top, cnt);
  return top;
}

// The picker's FIGHT for a rematch. Same rules as the ladder -- your level is
// capped to the (rematch) leader's best -- but a team the battle owns, and
// BK_REMATCH so the ending can never touch a badge.
void rematchStartBattle(uint8_t idx) {
  if (idx >= TRAINER_COUNT || pet.isEgg() || pet.ceremony != CER_NONE) return;
  uint8_t rg = gymRegion % GYM_REGIONS;
  if (!rematchOpenFor(pet, rg, idx)) return;
  TrainerMon tm[TRAINER_TEAM_MAX];
  uint8_t n = rematchTeam(rg, idx, tm);
  if (!n) return;
  uint8_t top = 0;
  for (uint8_t k = 0; k < n; k++) if (tm[k].level > top) top = tm[k].level;
  buildSquad(top, TRAINER_TEAM_MAX, squadMask);
  if (!btlSquadN) return;
  btlRegion = rg;
  btlKind = BK_REMATCH;
  btlTrainer = (int8_t)idx;
  btlHard = false;
  btlOpenTeamFight(tm, n, REMATCH_IV, TRAINER_SETS[rg].list[idx].name);
}

// The middle of the win screen for a rematch: no badge, the reward instead.
void towerWinBody() {
  featLine(150, TL(S_RM_PILL), UI_BAR_OK);
  char l[40];
  uint8_t rg = btlRegion % GYM_REGIONS, won = 0;
  for (uint8_t i = 0; i < TRAINER_COUNT; i++) won += rematchWon(rg, i);
  snprintf(l, sizeof(l), TL(S_RM_COUNT), (unsigned)won, (unsigned)TRAINER_COUNT);
  featLine(184, l, UI_INK);
  for (uint8_t i = 0; i < rmReward.n; i++) {
    snprintf(l, sizeof(l), "+%u %s", (unsigned)rmReward.qty[i], ITEM_TBL[rmReward.item[i]].name);
    featLine(226 + i * 26, l, UI_BAR_OK);
  }
}
