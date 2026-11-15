// The battle screen's art (ui_battle.ino, v3.18): the title banner says what
// the fight is, the HP bars change colour at the right thresholds, each side
// wears its types, the platforms sit exactly where the move effects aim, the
// command and move buttons answer taps WHERE THEY ARE DRAWN, and every state
// of the screen flushes. Driven through the REAL sketch: startBattle(),
// startTrainerBattle(), render() and battleTap().
//
// Pixels are read off the canvas and compared against the geometry
// btlUiRect() reports -- the one answer the drawing and the taps both use --
// so a button drawn in one place and hit-tested in another fails here.
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include "battle.h"
#include "moves.h"
#include "trainers.h"
#include "tower.h"
#include "types.h"
#include <cstdio>
#include <cstring>
#include <string>
uint32_t g_seed = 99; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX = 0, g_touchY = 0; volatile bool g_touchDown = false;
void FakeESP::restart() { exit(0); }
int FakeSerial::available() { return 0; }
String FakeSerial::readStringUntil(char) { return String(""); }
void setup(); void loop(); void render(); void battleTap(int16_t, int16_t);
extern Pet pet;
extern Arduino_Canvas *gfx;
extern bool battleOpen, btlOver, btlWon, btlLink, btlHard, btlNewBadge;
extern Combatant btlYou, btlFoe;
extern uint8_t btlMsgCount, btlMenu, btlKind, btlFoeAt, btlFoeTeamN, btlFoeSquadN, gymRegion;
extern int8_t btlTrainer;
extern uint16_t btlHpShown[2];
extern uint32_t btlWinUntil, btlWinFrom;
extern char btlMsg[6][40];
void startBattle(int16_t dex, uint8_t lvl);
void startTrainerBattle(uint8_t idx, bool hard);
void btlBannerText(char *out, int n);
uint16_t btlBannerColour();
bool btlUiRect(uint8_t id, int16_t *x, int16_t *y, int16_t *w, int16_t *h);
uint16_t btlHpColour(uint16_t shown, uint16_t maxHp);
void btlFxAnchor(uint8_t who, int16_t *cx, int16_t *cy, int16_t *feet);

// the sketch's btlKind values
enum { K_DEBUG = 0, K_GYM, K_LAN, K_WILD, K_TOWER, K_REMATCH, K_CHALLENGE };
// btlUiRect ids
enum { R_BANNER = 0, R_CARD_YOU, R_CARD_FOE, R_BAR_YOU, R_BAR_FOE, R_PLAT_YOU, R_PLAT_FOE,
       R_FIGHT, R_PKMN, R_RUN, R_BACK, R_PILL = 20 };

static int bad = 0;
static void ck(bool ok, const char *w) { printf("%s  %s\n", ok ? "PASS" : "FAIL", w); if (!ok) bad++; }
static uint16_t px(int x, int y) { return gfx->buffer()[y * 466 + x]; }
static std::string banner() { char b[40]; btlBannerText(b, sizeof(b)); return b; }
static bool rect(uint8_t id, int16_t &x, int16_t &y, int16_t &w, int16_t &h) { return btlUiRect(id, &x, &y, &w, &h); }
static bool flushed() { gfx->frameReady = false; render(); return gfx->frameReady; }

int main() {
  setup(); for (int i = 0; i < 4; i++) loop();
  if (pet.awaitingStarter()) pet.chooseStarter(4);
  if (pet.isEgg()) pet.dbgHatchAs(6, false);
  pet.ageMinutes = 50UL * MINUTES_PER_LEVEL;
  pet.relearnFromLevel();
  while (pet.hasLearnOffer()) pet.declineLearn();

  // ---- the banner says what the fight is
  startBattle(43, 12);
  ck(battleOpen, "a fight opens");
  ck(banner() == "BATTLE", "a plain fight is a BATTLE");
  btlKind = K_WILD;
  ck(banner() == "WILD", "a wild one says WILD");
  gymRegion = 0;
  startTrainerBattle(2, false);
  char want[40];
  snprintf(want, sizeof(want), "GYM 3 1/%u", (unsigned)btlFoeTeamN);
  printf("      gym banner: '%s'\n", banner().c_str());
  ck(btlFoeTeamN > 1 && banner() == want, "the third leader reads GYM 3, first of the team");
  btlFoeAt = 1;
  snprintf(want, sizeof(want), "GYM 3 2/%u", (unsigned)btlFoeTeamN);
  ck(banner() == want, "and counts on as the next creature comes out");
  btlTrainer = TRAINER_GYMS;
  ck(banner().rfind("ELITE 4 ", 0) == 0, "past the gyms it is the ELITE 4");
  btlTrainer = TRAINER_COUNT - 1;
  ck(banner().rfind("CHAMPION ", 0) == 0, "and the last trainer is the CHAMPION");
  btlKind = K_REMATCH;
  ck(banner().rfind("REMATCH ", 0) == 0, "a rematch says REMATCH");
  btlKind = K_TOWER;
  snprintf(want, sizeof(want), "TOWER F%u 2/%u", (unsigned)towerFloor(), (unsigned)btlFoeTeamN);
  ck(banner() == want, "a tower fight names its floor");
  btlKind = K_CHALLENGE;
  ck(banner().rfind("CHALLENGE", 0) == 0, "a challenge says CHALLENGE");
  // 5, not the trainer team's 3: equal counts would pass with the wrong one
  btlKind = K_LAN; btlLink = true; btlFoeSquadN = 5; btlFoeAt = 0;
  ck(btlFoeTeamN != 5 && banner() == "LAN 1/5", "LAN counts the rival's squad, not a trainer team");
  btlLink = false;
  btlOver = true; btlWon = true;
  ck(banner() == "VICTORY", "a won fight says VICTORY");
  btlWon = false;
  ck(banner() == "DEFEAT", "a lost one DEFEAT");
  btlOver = false;

  // and it is drawn: the trim on top, white text inside
  startTrainerBattle(2, false);
  render();
  int16_t x, y, w, h;
  rect(R_BANNER, x, y, w, h);
  ck(px(x + w / 2, y + 1) == btlBannerColour(), "the banner's trim is drawn where btlUiRect says");
  int white = 0;
  for (int yy = y; yy < y + h; yy++) for (int xx = x; xx < x + w; xx++) if (px(xx, yy) == 0xFFFF) white++;
  ck(white > 60, "with its text inside");
  ck(w <= 200 && x - 12 >= 0, "and it fits the top of the round panel");

  // ---- HP colours, at the thresholds and on the bar itself
  uint16_t ok = btlHpColour(100, 100), warn = btlHpColour(50, 100), red = btlHpColour(25, 100);
  ck(ok != warn && warn != red && ok != red, "three distinct bar colours");
  ck(btlHpColour(51, 100) == ok && btlHpColour(50, 100) == warn && btlHpColour(26, 100) == warn &&
     btlHpColour(25, 100) == red && btlHpColour(1, 100) == red,
     "green over half, amber over a quarter, red at a quarter and below");
  const uint16_t pcts[3] = { 80, 40, 10 };
  const uint16_t cols[3] = { ok, warn, red };
  for (int side = 0; side < 2; side++) {
    int good = 0, prop = 0;
    for (int k = 0; k < 3; k++) {
      Combatant &c = side ? btlFoe : btlYou;
      c.maxHp = 200; c.hp = pcts[k] * 2; btlHpShown[side] = c.hp;
      render();
      rect(side ? R_BAR_FOE : R_BAR_YOU, x, y, w, h);
      if (px(x + 3, y + 6) == cols[k]) good++;
      // the fill is proportional: drawn up to the value, not beyond it
      int fillEnd = x + 1 + (w - 2) * pcts[k] / 100;
      if (px(fillEnd - 3, y + 6) == cols[k] && px(fillEnd + 4, y + 6) != cols[k]) prop++;
    }
    ck(good == 3, side ? "the foe's bar is drawn in the colour for its HP" : "your bar is drawn in the colour for its HP");
    ck(prop == 3, side ? "and filled only as far as the HP goes (foe)" : "and filled only as far as the HP goes (you)");
  }

  // ---- type pills, under each card, in the type's colour
  startBattle(6, 30);                       // CHARIZARD: FIRE / FLYING
  render();
  int16_t a, b, c2, d;
  bool p0 = rect(R_PILL + 4 + 0, a, b, c2, d);
  bool p1 = rect(R_PILL + 4 + 1, x, y, w, h);
  ck(p0 && p1, "a dual-typed foe wears two pills");
  ck(p0 && px(a + 3, b + d / 2) == typeColor(DEX_TBL[6].type1), "the first in its first type's colour");
  ck(p1 && px(x + 3, y + h / 2) == typeColor(DEX_TBL[6].type2), "the second in its second type's colour");
  ck(p0 && p1 && x > a + c2, "side by side, not on top of each other");
  int16_t cx0, cy0, cw0, ch0;
  rect(R_CARD_FOE, cx0, cy0, cw0, ch0);
  ck(p0 && b >= cy0 + ch0, "under the card, not over it");
  ck(!rect(R_PILL + 4 + 2, x, y, w, h), "no status pill while it has no status");
  btlFoe.ailment = AIL_BURN;
  render();
  bool p2 = rect(R_PILL + 4 + 2, x, y, w, h);
  ck(p2 && x > a + c2 && px(x + 3, y + h / 2) != px(x + 3, y - 6), "a burn adds its own pill after the types");
  btlFoe.ailment = AIL_NONE;
  startBattle(4, 30);                       // CHARMANDER: FIRE only
  ck(DEX_TBL[4].type2 == T_NONE && !rect(R_PILL + 4 + 1, x, y, w, h), "a single type wears one pill");

  // ---- the platforms sit where the move effects aim
  for (int side = 0; side < 2; side++) {
    int16_t fx, fy, feet;
    btlFxAnchor(side, &fx, &fy, &feet);
    rect(side ? R_PLAT_FOE : R_PLAT_YOU, x, y, w, h);
    int pcx = x + w / 2, pcy = y + h / 2;
    printf("      side %d: platform (%d,%d) effects (%d,%d) feet %d\n", side, pcx, pcy, fx, fy, feet);
    ck(pcx == fx, side ? "the foe's platform is centred under its effects" : "yours is centred under its effects");
    ck(pcy >= feet && pcy <= feet + 4 && fy < y, side ? "its feet are on it, the body above it (foe)" : "its feet are on it, the body above it (you)");
    render();
    uint16_t top = px(pcx + w / 2 * 3 / 4 - 4, pcy + 1), rim = px(pcx, y + h);
    ck(top == 0xE73C || top == 0xFFDF, side ? "and the foe's platform is really drawn there" : "and yours is really drawn there");
    ck(rim == 0x94B2, side ? "with its rim below (foe)" : "with its rim below (you)");
  }
  rect(R_PLAT_YOU, x, y, w, h);
  ck(y + h <= 254, "your platform ends above the menu band");

  // ---- the command buttons answer where they are drawn
  startBattle(43, 12);
  render();
  uint16_t face[3];
  const uint8_t ids[3] = { R_FIGHT, R_PKMN, R_RUN };
  int onBand = 0;
  for (int i = 0; i < 3; i++) {
    rect(ids[i], x, y, w, h);
    face[i] = px(x + 5, y + h / 2 - 3);
    // the band is striped, so compare against the band in the SAME row
    if (face[i] != px(40, y + h / 2 - 3)) onBand++;
  }
  ck(onBand == 3, "all three buttons are drawn on the band");
  ck(face[0] != face[1] && face[1] != face[2] && face[0] != face[2], "each in its own colour");
  ck(h >= 44 && w >= 44, "FIGHT is at least a fingertip");
  rect(R_PKMN, x, y, w, h);
  ck(h >= 44 && w >= 44, "so is POKEMON");
  rect(R_RUN, x, y, w, h);
  ck(h >= 44 && w >= 44, "and RUN");
  int hits = 0;
  for (int corner = 0; corner < 5; corner++) {
    rect(R_FIGHT, x, y, w, h);
    int tx = corner == 4 ? x + w / 2 : (corner & 1 ? x + w - 3 : x + 3);
    int ty = corner == 4 ? y + h / 2 : (corner & 2 ? y + h - 3 : y + 3);
    btlMenu = 0; btlMsgCount = 0;
    battleTap(tx, ty);
    if (btlMenu == 1) hits++;
  }
  ck(hits == 5, "FIGHT opens the moves from its centre and all four corners");
  hits = 0;
  for (int corner = 0; corner < 5; corner++) {
    rect(R_PKMN, x, y, w, h);
    int tx = corner == 4 ? x + w / 2 : (corner & 1 ? x + w - 3 : x + 3);
    int ty = corner == 4 ? y + h / 2 : (corner & 2 ? y + h - 3 : y + 3);
    btlMenu = 0;
    battleTap(tx, ty);
    if (btlMenu == 2) hits++;
  }
  ck(hits == 5, "POKEMON opens the switch list from its centre and corners");

  // the move buttons: in their type's colour, and a tap on one uses THAT move
  btlMenu = 1;
  render();
  int coloured = 0, used = 0, n = 0;
  for (int i = 0; i < 4; i++) {
    uint8_t mv = btlYou.moves[i];
    if (!mv) continue;
    n++;
    int cxp = 69 + (i % 2) * 168, cyp = 274 + (i / 2) * 52;   // the cell the grid draws
    if (px(cxp + 5, cyp + 19) == typeColor(MOVE_TBL[mv].type)) coloured++;
  }
  ck(n > 0 && coloured == n, "every move button wears its type's colour");
  for (int i = 0; i < 4; i++) {
    uint8_t mv = btlYou.moves[i];
    if (!mv) continue;
    startBattle(43, 12);
    btlFoe.hp = btlFoe.maxHp = 999; btlHpShown[1] = 999;
    btlMenu = 1; btlMsgCount = 0;
    int cxp = 69 + (i % 2) * 168, cyp = 274 + (i / 2) * 52;
    battleTap(cxp + 80, cyp + 22);
    bool said = false;
    for (int k = 0; k < btlMsgCount; k++) if (strstr(btlMsg[k], MOVE_TBL[mv].name)) said = true;
    if (said) used++;
  }
  ck(used == n, "and tapping a move uses that move");
  btlMenu = 1; btlMsgCount = 0;
  render();
  rect(R_BACK, x, y, w, h);
  ck(px(x + 5, y + h / 2 - 3) != px(x - 12, y + h / 2), "BACK is drawn under the moves");
  battleTap(x + w / 2, y + h / 2);
  ck(btlMenu == 0, "and goes back to the commands");
  rect(R_RUN, x, y, w, h);
  battleTap(x + w - 3, y + h - 3);
  ck(!battleOpen, "RUN leaves the fight from its corner");

  // ---- every state of the screen flushes
  startBattle(43, 12);
  btlMenu = 0; ck(flushed(), "the commands flush");
  btlMenu = 1; ck(flushed(), "the move list flushes");
  btlMenu = 2; ck(flushed(), "the switch list flushes");
  btlMenu = 0; btlMsgCount = 1; snprintf(btlMsg[0], 40, "Hello!"); ck(flushed(), "the message box flushes");
  btlOver = true; btlWon = false; ck(flushed(), "the defeat box flushes");
  btlOver = false; btlMsgCount = 0;
  gymRegion = 0;
  startTrainerBattle(1, false);
  btlNewBadge = true; btlWinUntil = millis() + 60000; btlWinFrom = millis() - 1;
  ck(flushed(), "the win screen flushes");
  // and the win screen still shows its badge: art in the middle, not the cream
  int art = 0;
  for (int yy = 170; yy < 210; yy++) for (int xx = 213; xx < 253; xx++) {
    uint16_t c = px(xx, yy); if (c != 0xFFFF && c != 0xF77C && c != 0xFF15) art++;
  }
  ck(art > 200, "the badge art is still drawn on the win screen");
  btlWinUntil = 0;

  printf(bad ? "battle_ui_test: %d FAILED\n" : "battle_ui_test: all passed\n", bad);
  return bad ? 1 : 0;
}
