// Personalities on screen, through the real sketch: the creature card's profile
// page, the party and box sheets, and the main screen's cheer when a creature
// gets its favourite care. Each is checked by looking for the trait's ink in
// the framebuffer where it should be AND for its absence when there is no
// trait, so neither half can pass on its own. Every screen must flush.
//
// PERSONA_SHOT=dir writes the screens as PPM files for a human to look at.
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include "persona.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <thread>
uint32_t g_seed = 4242; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX = 0, g_touchY = 0; volatile bool g_touchDown = false;
void FakeESP::restart() { exit(0); }
int FakeSerial::available() { return 0; }
String FakeSerial::readStringUntil(char) { return String(""); }
void setup(); void loop(); void render();
extern Pet pet;
extern Arduino_Canvas *gfx;
extern bool cardOpen, partyOpen, boxOpen;
extern uint8_t cardPage, partyDetail, boxDetail;

static int bad = 0;
static void ck(bool ok, const char *w) { printf("%s  %s\n", ok ? "PASS" : "FAIL", w); if (!ok) bad++; }
static void pump(int n) { for (int i = 0; i < n; i++) { std::this_thread::sleep_for(std::chrono::milliseconds(30)); loop(); } }

// ui_persona.ino's PERSONA_INK: C565(0xd4, 0x52, 0x7e)
static const uint16_t INK = (uint16_t)(((0xd4 >> 3) << 11) | ((0x52 >> 2) << 5) | (0x7e >> 3));

static int inkIn(int y0, int y1) {
  const uint16_t *fb = gfx->buffer();
  int n = 0;
  for (int y = y0; y < y1; y++)
    for (int x = 0; x < 466; x++) if (fb[y * 466 + x] == INK) n++;
  return n;
}
static void shot(const char *name) {
  const char *dir = getenv("PERSONA_SHOT");
  if (!dir) return;
  char path[256]; snprintf(path, sizeof(path), "%s/%s.ppm", dir, name);
  FILE *f = fopen(path, "wb"); if (!f) return;
  fprintf(f, "P6 466 466 255\n");
  const uint16_t *fb = gfx->buffer();
  for (int i = 0; i < 466 * 466; i++) {
    uint16_t c = fb[i];
    unsigned char rgb[3] = { (unsigned char)((c >> 11) << 3), (unsigned char)(((c >> 5) & 63) << 2), (unsigned char)((c & 31) << 3) };
    fwrite(rgb, 1, 3, f);
  }
  fclose(f);
}
static bool flushed() { gfx->frameReady = false; render(); return gfx->frameReady; }

int main() {
  setup(); pump(3);
  if (pet.awaitingStarter()) pet.chooseStarter(4);
  if (pet.isEgg()) pet.dbgHatchAs(4, false);
  ck(personaValid(pet.persona), "the hatched creature has a personality");
  pet.ageMinutes = 60UL * 20;
  while (pet.hasLearnOffer()) pet.declineLearn();
  pet.sleeping = false;
  pet.fullness = pet.joy = pet.energy = pet.hygiene = 90;
  pet.bond = 0;        // the bond bar shares the trait's pink; keep it empty

  // ---- the card's profile page
  pet.persona = PT_GLUTTON;
  cardOpen = true; cardPage = 0;
  ck(flushed(), "the card profile flushes");
  int withTrait = inkIn(314, 340);
  shot("card_glutton");
  pet.persona = PT_NONE;
  ck(flushed(), "and flushes with no trait too");
  int without = inkIn(314, 340);
  printf("      card line ink: %d with a trait, %d without\n", withTrait, without);
  ck(withTrait > 100 && without == 0, "the card's profile page shows the trait (and nothing when there is none)");
  // every trait's line fits: drawn and inside the panel's width at that height
  bool allFit = true;
  for (uint8_t t = 1; t < PT_COUNT; t++) {
    pet.persona = t; render();
    const uint16_t *fb = gfx->buffer();
    for (int y = 314; y < 340; y++)
      for (int x = 0; x < 466; x++)
        if (fb[y * 466 + x] == INK) {
          int dx = x - 233, dy = y - 233;
          if (dx * dx + dy * dy > 225 * 225) allFit = false;
        }
    if (!inkIn(314, 340)) allFit = false;
  }
  ck(allFit, "every trait's card line is drawn inside the round panel");
  cardOpen = false;

  // ---- the party sheet and the box sheet
  for (int i = 0; i < PARTY_SLOTS; i++) party.slots[i] = PartyMon();
  for (int i = 0; i < BOX_SLOTS; i++) party.box[i] = PartyMon();
  PartyMon m; m.dex = 6; m.level = 40; m.ivAtk = m.ivDef = m.ivSpe = m.ivHp = 20;
  snprintf(m.nick, sizeof(m.nick), "ZARD");
  m.spare = PT_TIDY; party.slots[0] = m;
  m.spare = 0; party.slots[1] = m;                // banked before personalities
  m.spare = PT_PICKY; party.box[0] = m;
  party.save(); party.boxSave();

  partyOpen = true; partyDetail = 1;
  ck(flushed(), "the party sheet flushes");
  int sheetT = inkIn(60, 76);
  shot("sheet_tidy");
  partyDetail = 2;
  ck(flushed(), "the sheet of a pre-feature creature flushes");
  int sheet0 = inkIn(60, 76);
  printf("      sheet ink: %d with TIDY, %d with spare 0\n", sheetT, sheet0);
  ck(sheetT > 30 && sheet0 == 0, "the party sheet shows a banked trait; spare 0 shows none");
  partyDetail = 0;
  boxOpen = true; boxDetail = 1;             // the box lives under the party screen
  ck(flushed(), "the box sheet flushes");
  ck(inkIn(60, 76) > 30, "the box sheet shows it too (one renderMonSheet for both)");
  boxDetail = 0; boxOpen = false; partyOpen = false;
  pump(2);

  // ---- the cheer on the main screen
  pet.persona = PT_CUDDLY;
  ck(flushed(), "the main screen flushes");
  int before = inkIn(276, 308);
  pet.caress();
  ck(personaCheering(), "petting a CUDDLY creature starts its cheer");
  ck(flushed(), "the main screen flushes while it cheers");
  int during = inkIn(276, 308);
  shot("main_cheer");
  printf("      cheer pill ink: %d before, %d during\n", before, during);
  ck(during > 1000 && during > before + 1000, "the cheer pill is on the main screen");
  // a care that is NOT its favourite does not cheer
  uint32_t until = millis() + PERSONA_CHEER_MS + 100;
  while (millis() < until) pump(5);
  ck(!personaCheering(), "the cheer ends");
  pet.feedCandy();
  ck(!personaCheering(), "candy is not a CUDDLY creature's favourite: no cheer");
  ck(flushed(), "and the main screen flushes");
  ck(inkIn(276, 308) < 1000, "and no pill is drawn");

  printf("%s\n", bad ? "FAILURES" : "all good");
  return bad ? 1 : 0;
}
