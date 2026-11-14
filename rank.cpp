#include "rank.h"
#include "pet.h"
#include "party.h"
#include "items.h"
#include "tower.h"
#include "i18n.h"
#include "avatars.h"
#include "game_events.h"
#include <Preferences.h>

// Trainer rank: one score from player-wide progress that already exists, a
// rank from the score, and the highest rank ever reached kept on flash so the
// card never shows a demotion. Numbers: README "Trainer rank and titles";
// design: docs/adventure-rank.md.

static Preferences s_prefs;
static bool s_opened = false;          // nobody begin()'d us: never write
static const Pet *s_pet = nullptr;
static uint8_t s_top = 1;              // highest rank reached (1..RANK_MAX)
static uint16_t s_catches = 0;
static uint16_t s_chWins = 0;
static uint16_t s_chLast = 0;          // last challenge week counted, +1 (0 = none)
static uint8_t s_theme = THEME_CLASSIC;
static uint8_t s_avGrant = 0;          // avatars kept from before they were locked
static bool s_notice = false;          // a rank-up the main screen has not shown yet
static bool s_noticeOn = false;        // ... and it has started showing
static uint32_t s_noticeAt = 0;

// Points needed for each rank, index = rank - 1. Strictly increasing.
static const uint16_t RANK_AT[RANK_MAX] = { 0, 30, 100, 200, 350, 600, 1000, 1600, 2500, 4000 };

// Trainer avatars by index (avatars.h): RED BLUE GIRL ACE_M ACE_F HIKER SAILOR YOUNGSTER
static const uint8_t AVATAR_NEEDS[AVATAR_COUNT] = { 1, 1, 1, 4, 4, 6, 8, 2 };
static const uint8_t THEME_NEEDS[THEME_COUNT] = { 1, 3, 5, 7 };

static const char *const S_RK_TITLE[RANK_MAX][LANG_COUNT] = {
  { "NOVATO", "ROOKIE", "DEBUTANT", "NEULING", "NOVELLINO", "NOVATO" },
  { "ENTRENADOR", "TRAINER", "DRESSEUR", "TRAINER", "ALLENATORE", "TREINADOR" },
  { "ENTRENADOR AS", "ACE TRAINER", "DRESSEUR AS", "ASS-TRAINER", "ASSO", "TREINADOR AS" },
  { "RETADOR", "GYM CHALLENGER", "CHALLENGER", "HERAUSFORDERER", "SFIDANTE", "DESAFIANTE" },
  { "LIDER", "GYM LEADER", "CHAMPION ARENE", "ARENALEITER", "CAPOPALESTRA", "LIDER" },
  { "VETERANO", "VETERAN", "VETERAN", "VETERAN", "VETERANO", "VETERANO" },
  { "ELITE", "ELITE", "ELITE", "ELITE", "ELITE", "ELITE" },
  { "CAMPEON", "CHAMPION", "CHAMPION", "CHAMPION", "CAMPIONE", "CAMPEAO" },
  { "MAESTRO", "MASTER", "MAITRE", "MEISTER", "MAESTRO", "MESTRE" },
  { "LEYENDA", "LEGEND", "LEGENDE", "LEGENDE", "LEGGENDA", "LENDA" },
};
static const char *const S_RK_THEME[THEME_COUNT][LANG_COUNT] = {
  { "CLASICO", "CLASSIC", "CLASSIQUE", "KLASSISCH", "CLASSICO", "CLASSICO" },
  { "OCASO", "SUNSET", "COUCHANT", "ABENDROT", "TRAMONTO", "POR DO SOL" },
  { "ESTRELLAS", "STARRY", "ETOILES", "STERNE", "STELLE", "ESTRELAS" },
  { "FIESTA", "FESTIVAL", "FETE", "FEST", "FESTA", "FESTA" },
};

bool rankBindPet(const Pet *p) { s_pet = p; return p != nullptr; }

static void rankSave() {
  if (!s_opened) return;
  s_prefs.putUChar("rkTop", s_top);
  s_prefs.putUShort("rkCat", s_catches);
  s_prefs.putUShort("rkChW", s_chWins);
  s_prefs.putUShort("rkChL", s_chLast);
  s_prefs.putUChar("rkThm", s_theme);
  s_prefs.putUChar("rkAvG", s_avGrant);
}

// ---------- the score ----------
static uint16_t capU16(uint32_t v, uint32_t cap) { return (uint16_t)(v < cap ? v : cap); }

uint32_t rankPointsFrom(const RankInput &in) {
  uint32_t p = 0;
  p += (uint32_t)in.badges * RANK_PTS_BADGE;
  p += (uint32_t)in.badgesHard * RANK_PTS_BADGE_HARD;
  p += (uint32_t)in.rematches * RANK_PTS_REMATCH;
  p += (uint32_t)in.dex * RANK_PTS_DEX;
  p += (uint32_t)capU16(in.towerBest, RANK_TOWER_CAP) * RANK_PTS_TOWER;
  p += (uint32_t)capU16(in.catches, RANK_CATCH_CAP) * RANK_PTS_CATCH;
  p += (uint32_t)capU16(in.challenges, RANK_CHALLENGE_CAP) * RANK_PTS_CHALLENGE;
  return p;
}

RankInput rankInputFor(const Pet &p) {
  RankInput in;
  in.badges = p.badgeCount(false);
  in.badgesHard = p.badgeCount(true);
  in.rematches = 0;
  for (uint8_t r = 0; r < GYM_REGIONS; r++)
    for (uint8_t i = 0; i < TRAINER_COUNT; i++)
      if (rematchWon(r, i)) in.rematches++;
  in.dex = p.registeredCount();
  in.towerBest = towerBest();
  in.catches = s_catches;
  in.challenges = s_chWins;
  return in;
}

uint32_t rankPoints() {
  if (!s_pet) {   // unbound: only what this module counts itself
    RankInput in = { 0, 0, 0, 0, 0, s_catches, s_chWins };
    return rankPointsFrom(in);
  }
  return rankPointsFrom(rankInputFor(*s_pet));
}

uint8_t rankOf(uint32_t points) {
  uint8_t r = 1;
  for (uint8_t i = 1; i < RANK_MAX; i++)
    if (points >= RANK_AT[i]) r = i + 1;
  return r;
}

uint32_t rankThreshold(uint8_t rank) {
  if (rank < 1) rank = 1;
  if (rank > RANK_MAX) rank = RANK_MAX;
  return RANK_AT[rank - 1];
}

const char *rankTitle(uint8_t rank) {
  if (rank < 1) rank = 1;
  if (rank > RANK_MAX) rank = RANK_MAX;
  return TL(S_RK_TITLE[rank - 1]);
}

// ---------- the shown rank ----------
uint8_t rankShown() { return s_top; }

uint8_t rankUpdate() {
  uint8_t now = rankOf(rankPoints());
  if (now > s_top) {
    s_top = now;
    s_notice = true; s_noticeOn = false;
    rankSave();
    gameEvent(EV_RANK_UP, s_top);   // once: s_top has already moved
  }
  return s_top;
}

uint16_t rankCatches() { return s_catches; }
uint16_t rankChallengeWins() { return s_chWins; }

static void rankOnEvent(GameEv ev, int16_t arg) {
  switch (ev) {
    case EV_WILD_CATCH:
      if (s_catches < 0xFFFF) s_catches++;
      rankSave();
      break;
    case EV_CHALLENGE_WIN: {
      // one per week: a second event for the same week is not a second win
      uint16_t key = (uint16_t)((uint16_t)arg + 1);
      if (key == 0) key = 1;
      if (key == s_chLast) return;
      s_chLast = key;
      if (s_chWins < 0xFFFF) s_chWins++;
      rankSave();
      break;
    }
    case EV_GYM_WIN: case EV_REMATCH_WIN: case EV_DEX_REGISTER: case EV_TOWER_WIN:
      break;
    default:
      return;   // EV_RANK_UP among them: never recurse
  }
  rankUpdate();
}

// ---------- unlocks ----------
uint8_t rankAvatarNeeds(uint8_t avatar) {
  return AVATAR_NEEDS[avatar % AVATAR_COUNT];
}

bool rankAvatarUnlocked(uint8_t avatar) {
  if (avatar >= AVATAR_COUNT) return false;
  if ((s_avGrant >> avatar) & 1) return true;
  return s_top >= AVATAR_NEEDS[avatar];
}

uint8_t rankNextAvatar(uint8_t cur) {
  for (uint8_t k = 1; k <= AVATAR_COUNT; k++) {
    uint8_t a = (uint8_t)((cur + k) % AVATAR_COUNT);
    if (rankAvatarUnlocked(a)) return a;
  }
  return cur;
}

uint8_t rankThemeNeeds(uint8_t theme) {
  return theme < THEME_COUNT ? THEME_NEEDS[theme] : 0xFF;
}

bool rankThemeUnlocked(uint8_t theme) {
  return theme < THEME_COUNT && s_top >= THEME_NEEDS[theme];
}

const char *rankThemeName(uint8_t theme) {
  return TL(S_RK_THEME[theme < THEME_COUNT ? theme : 0]);
}

uint8_t rankTheme() { return s_theme; }

bool rankSetTheme(uint8_t theme) {
  if (!rankThemeUnlocked(theme)) return false;
  s_theme = theme;
  rankSave();
  return true;
}

// ---------- the notice ----------
bool rankNoticePending() { return s_notice; }

bool rankNoticeActive(uint32_t now) {
  if (!s_notice) return false;
  if (!s_noticeOn) { s_noticeOn = true; s_noticeAt = now; }
  if (now - s_noticeAt < RANK_NOTICE_MS) return true;
  s_notice = s_noticeOn = false;
  return false;
}

// ---------- lifecycle ----------
void rankBegin() {
  if (!s_opened) s_prefs.begin("tamapoke", false);
  s_opened = true;
  gameEventSubscribe(rankOnEvent);
  bool fresh = !s_prefs.isKey("rkTop");
  s_top = s_prefs.getUChar("rkTop", 1);
  s_catches = s_prefs.getUShort("rkCat", 0);
  s_chWins = s_prefs.getUShort("rkChW", 0);
  s_chLast = s_prefs.getUShort("rkChL", 0);
  s_theme = s_prefs.getUChar("rkThm", THEME_CLASSIC);
  s_avGrant = s_prefs.getUChar("rkAvG", 0);
  s_notice = s_noticeOn = false;
  // never trust a stored value
  if (s_top < 1) s_top = 1;
  if (s_top > RANK_MAX) s_top = RANK_MAX;
  if (s_theme >= THEME_COUNT) s_theme = THEME_CLASSIC;
  s_avGrant &= (uint8_t)((1u << AVATAR_COUNT) - 1);

  if (fresh) {
    // First boot with ranks: the save's progress is the starting rank, with a
    // notice but no EV_RANK_UP -- nothing was just achieved.
    uint8_t r = rankOf(rankPoints());
    if (r > s_top) s_top = r;
    s_notice = s_top > 1;
  }
  // Never take away what a player has: an avatar already selected stays
  // selectable even if its rank is not reached (a save from before ranks).
  if (s_pet && s_pet->avatar < AVATAR_COUNT && !rankAvatarUnlocked(s_pet->avatar))
    s_avGrant |= (uint8_t)(1u << s_pet->avatar);
  rankSave();
  rankUpdate();   // progress made since (an import, say) counts now
}

void rankClearAll() {
  s_top = 1;
  s_catches = s_chWins = s_chLast = 0;
  s_theme = THEME_CLASSIC;
  s_avGrant = 0;
  s_notice = s_noticeOn = false;
  rankSave();
}
