// Move effects: what a move LOOKS like on the battle screen.
//
// Every move gets a shape (a projectile, a beam, a lightning strike, a slash, a
// bite, a quake...) drawn in its TYPE's colours with a small pixel-art sprite
// per type (a flame, a drop, a leaf, an ice shard...), and an impact burst
// where it lands. Both actions of a turn are laid out on ONE timeline: the
// second move starts once the first has played, the target flinches and its
// HP bar starts draining at the moment of impact -- not the instant the turn
// was resolved, which used to make both hits land on the same frame.
//
// Purely cosmetic: nothing here reads or writes battle state other than the
// animation timers (lunge / flinch / faint / HP-bar hold), so a fight resolves
// exactly the same whether or not a frame is ever drawn. movefx_ui_test pins
// that, the timeline and that every move has an effect.

// ---- sprites: 12x12 maps over a five-colour ramp -------------------------
// K outline, D dark, M main, L light, W highlight. One shape recolours to any
// type, so a FLAME drawn with the DRAGON ramp is dragon fire.
typedef char FxMap[12][13];

static const FxMap FXP_FLAME = {
  "....K.......", "...KLK......", "...KLMK..K..", "..KLMMK.KLK.",
  "..KLMMDKKMK.", ".KLMWMMDKMDK", ".KMWWLMMDMDK", "KMWLLLWMMMDK",
  "KMWLLLLWMMDK", "KMWLLLLWMDK.", ".KMWLLWMMDK.", "..KKKKKKKK..",
};
static const FxMap FXP_DROP = {
  ".....KK.....", ".....KK.....", "....KLMK....", "....KLMK....",
  "...KLMMMK...", "..KLWMMMDK..", "..KWWMMMDK..", ".KLWMMMMMDK.",
  ".KLWMMMMMDK.", ".KLMMMMMDDK.", "..KMMMMDDK..", "...KKKKKK...",
};
static const FxMap FXP_LEAF = {
  "..........KK", "........KKMK", "......KKLMDK", "....KKLLMMDK",
  "...KLLMMMDK.", "..KLMMWMDDK.", ".KLMMWMMDK..", ".KMMWMMDK...",
  "KMMWMDDK....", "KMWDDKK.....", "KWKK........", "K...........",
};
static const FxMap FXP_SHARD = {
  ".....KK.....", "....KWLK....", "....KWLK....", "...KWLLMK...",
  "...KWLMMK...", "..KWLLMMDK..", "..KWLMMMDK..", "..KWLMMDDK..",
  "...KLMMDK...", "...KLMDDK...", "....KMDK....", ".....KK.....",
};
static const FxMap FXP_ROCK = {
  "....KKKK....", "..KKLLMMKK..", ".KLLWLMMMDK.", ".KLWLMMMMDK.",
  "KLLLMMMDMMDK", "KLMMMMDMMMDK", "KMMMMDMMMDDK", "KMMDMMMMDDDK",
  ".KMMMMMDDDK.", ".KDMMDDDDDK.", "..KKDDDDKK..", "....KKKK....",
};
static const FxMap FXP_BOLT = {
  "......KKKK..", ".....KLLWK..", "....KLLWK...", "...KLLWK....",
  "..KLLWKKKK..", ".KLWWWWLLK..", ".KKKKLWLK...", "....KLWK....",
  "...KLWK.....", "..KLWK......", "..KWK.......", "..KK........",
};
static const FxMap FXP_STAR = {
  ".....KK.....", ".....KWK....", "KK..KLWK..KK", "KWK.KLWK.KWK",
  ".KLKKLLKKLK.", "..KLLWWLLK..", "KKKLWWWWLKKK", "KWLLLWWLLLWK",
  ".KKKLLLLKKK.", "..KLKLLKLK..", ".KLK.KK.KLK.", ".KK......KK.",
};
static const FxMap FXP_SPARK = {
  ".....K......", ".....K......", "....KWK.....", "....KWK.....",
  "...KWLWK....", "KKKWLLLWKKK.", "...KWLWK....", "....KWK.....",
  "....KWK.....", ".....K......", ".....K......", "............",
};
static const FxMap FXP_ORB = {
  "....KKKK....", "..KKMMMMKK..", ".KMLLMMMMDK.", ".KLWWLMMMDK.",
  "KMLWWLMMMMDK", "KMLLLMMMMMDK", "KMMMMMMMMDDK", "KMMMMMMMMDDK",
  ".KMMMMMMDDK.", ".KDMMMMDDDK.", "..KKDDDDKK..", "....KKKK....",
};
static const FxMap FXP_FEATHER = {
  "..........KK", ".........KWK", "........KWLK", ".......KWLMK",
  "......KWLMK.", ".....KWLMMK.", "....KWLMMK..", "...KWLMMK...",
  "..KWLMDK....", "..KLMDK.....", ".KDKK.......", "KK..........",
};
static const FxMap FXP_NEEDLE = {
  "..........KK", ".........KWK", "........KWK.", ".......KWK..",
  "......KLK...", ".....KLK....", "....KMK.....", "...KMK......",
  "..KMDK......", ".KMDK.......", "KDDK........", "KKK.........",
};
static const FxMap FXP_HEART = {
  "............", ".KKK...KKK..", "KLWLK.KLMMK.", "KWLMMKMMMDK.",
  "KLMMMMMMMDK.", "KMMMMMMMDDK.", ".KMMMMMMDK..", "..KMMMMDK...",
  "...KMMDK....", "....KDK.....", ".....K......", "............",
};
static const FxMap FXP_ARROW = {
  ".....KK.....", "....KWLK....", "...KWLLMK...", "..KWLLLMMK..",
  ".KWLLLLMMMK.", "KKKKLLMMKKKK", "...KLLMMK...", "...KLLMMK...",
  "...KLLMDK...", "...KLMMDK...", "...KMMDDK...", "...KKKKKK...",
};
static const FxMap FXP_NOTE = {
  "......KKKKK.", "......KWLLK.", "......KLKKK.", "......KLK...",
  "......KLK...", "......KLK...", "......KLK...", "...KKKKLK...",
  "..KWLLMMK...", "..KLMMMDK...", "..KMMMDDK...", "...KKKKK....",
};

// Colour ramps per type, K D M L W, indexed by PkType.
static const uint16_t FX_PAL[18][5] = {
  { 0x2104, 0x8410, 0xC618, 0xEF7D, 0xFFFF },  // NORMAL
  { 0x5800, 0xD800, 0xFC00, 0xFFE0, 0xFFFF },  // FIRE
  { 0x0010, 0x021F, 0x2C7F, 0x969F, 0xFFFF },  // WATER
  { 0x4200, 0xE5A0, 0xFFE0, 0xFFF4, 0xFFFF },  // ELECTRIC
  { 0x0200, 0x1C03, 0x5647, 0xAF8F, 0xE7F9 },  // GRASS
  { 0x018C, 0x3CBB, 0x869E, 0xCF9F, 0xFFFF },  // ICE
  { 0x3000, 0xA142, 0xDAC6, 0xFDAF, 0xFFFF },  // FIGHTING
  { 0x2004, 0x68F1, 0xAA39, 0xDCBE, 0xFFFF },  // POISON
  { 0x3901, 0x82A5, 0xBC68, 0xE650, 0xFF9A },  // GROUND
  { 0x194A, 0x6C19, 0xADFE, 0xDF3F, 0xFFFF },  // FLYING
  { 0x4004, 0xC94D, 0xFAD4, 0xFD5A, 0xFFFF },  // PSYCHIC
  { 0x2100, 0x6C61, 0xAE45, 0xDF8F, 0xFFFF },  // BUG
  { 0x2902, 0x7B27, 0xB50C, 0xDE72, 0xF79A },  // ROCK
  { 0x1004, 0x414F, 0x7A97, 0xBCBE, 0xF71F },  // GHOST
  { 0x0806, 0x40F7, 0x6ADF, 0xB51F, 0xFFFF },  // DRAGON
  { 0x0000, 0x3103, 0x6248, 0xA40E, 0xE656 },  // DARK
  { 0x2124, 0x6B90, 0xAD77, 0xD6FC, 0xFFFF },  // STEEL
  { 0x4006, 0xDAD4, 0xFCB9, 0xFE7C, 0xFFFF },  // FAIRY
};
// The particle each type throws around.
static const FxMap *const FX_PART[18] = {
  &FXP_STAR, &FXP_FLAME, &FXP_DROP, &FXP_BOLT, &FXP_LEAF, &FXP_SHARD,
  &FXP_STAR, &FXP_ORB, &FXP_ROCK, &FXP_FEATHER, &FXP_SPARK, &FXP_NEEDLE,
  &FXP_ROCK, &FXP_ORB, &FXP_FLAME, &FXP_ORB, &FXP_SPARK, &FXP_HEART,
};

// ---- shapes -----------------------------------------------------------------
enum : uint8_t {
  FXS_NONE = 0, FXS_CONTACT, FXS_PROJ, FXS_STREAM, FXS_BEAM, FXS_BOLTS, FXS_FALL,
  FXS_QUAKE, FXS_SLASH, FXS_BITE, FXS_WAVE, FXS_MULTI, FXS_BUFF, FXS_DEBUFF,
  FXS_HEAL, FXS_CHARGE, FXS_SELFHIT, FXS_JET, FXS_COUNT
};

// THE answer to "what does this move look like". Signature moves first, then
// the move's category decides: physical hits up close, special flies across.
uint8_t moveFxShape(uint8_t mv) {
  if (!mv || mv >= MOVE_COUNT) return FXS_NONE;
  const MoveEntry &m = MOVE_TBL[mv];
  switch (mv) {
    case MV_HYPER_BEAM: case MV_SOLAR_BEAM: case MV_ICE_BEAM: case MV_AURORA_BEAM:
    case MV_PSYBEAM: case MV_FLASH_CANNON: case MV_DRAGON_RAGE:
      return FXS_BEAM;
    case MV_WATER_GUN: case MV_HYDRO_PUMP:
      return FXS_JET;
    case MV_FLAMETHROWER: case MV_FIRE_BLAST:
    case MV_SURF: case MV_BUBBLE: case MV_ACID: case MV_SLUDGE: case MV_BLIZZARD:
    case MV_ABSORB: case MV_MEGA_DRAIN:
      return FXS_STREAM;
    case MV_THUNDERSHOCK: case MV_THUNDERBOLT: case MV_THUNDER:
      return FXS_BOLTS;
    case MV_ROCK_THROW: case MV_ROCK_SLIDE: case MV_ANCIENTPOWER:
      return FXS_FALL;
    case MV_EARTHQUAKE: case MV_DIG: case MV_SEISMIC_TOSS:
      return FXS_QUAKE;
    case MV_SCRATCH: case MV_X_SCISSOR: case MV_DRAGON_CLAW: case MV_KARATE_CHOP:
    case MV_WING_ATTACK: case MV_RAZOR_LEAF: case MV_VINE_WHIP: case MV_PLAY_ROUGH:
      return mv == MV_RAZOR_LEAF ? FXS_PROJ : FXS_SLASH;
    case MV_BITE: case MV_CRUNCH: case MV_BUG_BITE: case MV_LEECH_LIFE:
      return FXS_BITE;
    case MV_PSYCHIC: case MV_CONFUSION: case MV_PSYWAVE: case MV_BUG_BUZZ:
    case MV_DAZZLE_GLEAM: case MV_DARK_PULSE: case MV_NIGHT_SHADE: case MV_MOONBLAST:
      return FXS_WAVE;
    default: break;
  }
  if (m.cat == MC_STATUS) {
    if (m.effect == EF_HEAL) return FXS_HEAL;
    return m.target == TG_SELF ? FXS_BUFF : FXS_DEBUFF;
  }
  if (m.effect == EF_MULTI) return FXS_MULTI;
  return m.cat == MC_PHYS ? FXS_CONTACT : FXS_PROJ;
}

// The sprite a move's particles use. A few moves wear their own.
static const FxMap *moveFxSprite(uint8_t mv, uint8_t type) {
  switch (mv) {
    case MV_BUBBLE: case MV_ACID: case MV_SLUDGE: case MV_SLUDGE_BOMB:
    case MV_SHADOW_BALL: case MV_ABSORB: case MV_MEGA_DRAIN: return &FXP_ORB;
    case MV_SWIFT: return &FXP_STAR;
    case MV_PECK: case MV_DRILL_PECK: case MV_POISON_STING: case MV_PIN_MISSILE:
    case MV_FURY_ATTACK: case MV_MEGAHORN: return &FXP_NEEDLE;
    case MV_MOONBLAST: case MV_DAZZLE_GLEAM: return &FXP_SPARK;
    default: break;
  }
  return FX_PART[type < 18 ? type : 0];
}

// ---- timeline -----------------------------------------------------------------
struct BtlFx {
  uint8_t mv = 0, from = 0, shape = FXS_NONE, type = 0, n = 1, power = 60;
  uint8_t statMask = 0;
  uint32_t t0 = 0;
  uint16_t impact = 0, dur = 0;
  bool land = false, missed = false, crit = false, superEff = false;
  bool drain = false, recoil = false, healSelf = false;
};
#define BTL_FX_SLOTS 4
BtlFx btlFx[BTL_FX_SLOTS];
#define FX_MULTI_GAP 170
#define FX_BURST_MS 380
#define FX_DRAIN_MS 520
// Water Gun / Hydro Pump: the jet's head reaches the target at FX_JET_HEAD, the
// stream keeps pouring until FX_JET_HOLD, its tail then races after it for
// FX_JET_TAIL, and the last of the splash settles over FX_JET_SPLASH.
#define FX_JET_HEAD 280
#define FX_JET_HOLD 620
#define FX_JET_TAIL 170
#define FX_JET_SPLASH 260

void btlFxReset() {
  for (int i = 0; i < BTL_FX_SLOTS; i++) btlFx[i] = BtlFx();
  btlLungeFrom[0] = btlLungeFrom[1] = btlHitFrom[0] = btlHitFrom[1] = 0;
  btlHpHoldUntil[0] = btlHpHoldUntil[1] = 0;
}
// A side was swapped out: every effect involves both sides, so drop them all
// and that side's pending timers.
void btlFxClearSide(uint8_t who) {
  for (int i = 0; i < BTL_FX_SLOTS; i++) btlFx[i] = BtlFx();
  btlLungeFrom[who] = btlHitFrom[who] = 0;
  btlHpHoldUntil[who] = 0;
}

bool btlFxActive() {
  uint32_t now = millis();
  for (int i = 0; i < BTL_FX_SLOTS; i++)
    if (btlFx[i].shape != FXS_NONE && now < btlFx[i].t0 + btlFx[i].dur) return true;
  return false;
}
// The moment every queued effect has finished (now, if none).
uint32_t btlFxEnd() {
  uint32_t e = millis();
  for (int i = 0; i < BTL_FX_SLOTS; i++)
    if (btlFx[i].shape != FXS_NONE && btlFx[i].t0 + btlFx[i].dur > e) e = btlFx[i].t0 + btlFx[i].dur;
  return e;
}
// For tests and the faint timing: the shape of the effect `from` is playing.
uint8_t btlFxShapeFor(uint8_t from) {
  uint32_t now = millis();
  for (int i = 0; i < BTL_FX_SLOTS; i++)
    if (btlFx[i].shape != FXS_NONE && btlFx[i].from == from && now < btlFx[i].t0 + btlFx[i].dur)
      return btlFx[i].shape;
  return FXS_NONE;
}

// When the latest effect from side `from` lands and ends (0 if it has none).
// The turn's timeline in two numbers, for the tests.
uint32_t btlFxImpactOf(uint8_t from) {
  uint32_t r = 0, t0 = 0;
  for (int i = 0; i < BTL_FX_SLOTS; i++)
    if (btlFx[i].shape != FXS_NONE && btlFx[i].from == from && btlFx[i].t0 >= t0) {
      t0 = btlFx[i].t0; r = btlFx[i].t0 + btlFx[i].impact;
    }
  return r;
}
uint32_t btlFxEndOf(uint8_t from) {
  uint32_t r = 0, t0 = 0;
  for (int i = 0; i < BTL_FX_SLOTS; i++)
    if (btlFx[i].shape != FXS_NONE && btlFx[i].from == from && btlFx[i].t0 >= t0) {
      t0 = btlFx[i].t0; r = btlFx[i].t0 + btlFx[i].dur;
    }
  return r;
}

static void fxTiming(BtlFx &f) {
  uint16_t imp = 0, dur = 0;
  switch (f.shape) {
    case FXS_CONTACT: imp = 130; dur = 130 + FX_BURST_MS + 60; break;
    case FXS_PROJ:    imp = 380 + 90 * (f.n - 1); dur = imp + FX_BURST_MS; break;
    case FXS_STREAM:  imp = 260; dur = 860; break;
    case FXS_BEAM:    imp = 220; dur = 720; break;
    case FXS_BOLTS:   imp = 140; dur = 640; break;
    case FXS_FALL:    imp = 300; dur = 300 + 130 * (f.n - 1) + FX_BURST_MS; break;
    case FXS_QUAKE:   imp = 120; dur = 840; break;
    case FXS_SLASH:   imp = 90;  dur = 140 + FX_BURST_MS + 40; break;
    case FXS_BITE:    imp = 230; dur = 230 + FX_BURST_MS; break;
    case FXS_WAVE:    imp = 360; dur = 780; break;
    case FXS_MULTI:   imp = 140; dur = 140 + FX_MULTI_GAP * (f.n - 1) + FX_BURST_MS; break;
    case FXS_BUFF:    imp = 0;   dur = 860; break;
    case FXS_DEBUFF:  imp = 300; dur = 940; break;
    case FXS_HEAL:    imp = 0;   dur = 860; break;
    case FXS_CHARGE:  imp = 0;   dur = 720; break;
    case FXS_SELFHIT: imp = 60;  dur = 60 + FX_BURST_MS; break;
    case FXS_JET:     imp = FX_JET_HEAD; dur = FX_JET_HOLD + FX_JET_TAIL + FX_JET_SPLASH; break;
    default: break;
  }
  if (f.drain) dur = imp + FX_BURST_MS / 2 + FX_DRAIN_MS;
  if (f.recoil && dur < imp + 150 + FX_BURST_MS) dur = imp + 150 + FX_BURST_MS;
  f.impact = imp;
  f.dur = dur;
}

// Lays one action on the timeline starting at `at` and returns when the next
// may start. hurtTarget / hurtSelf say who actually LOST health, measured by
// the caller around battleAct(), so the flinch can never disagree with the HP.
uint32_t btlFxQueue(uint8_t from, uint8_t mv, const TurnLog &lg, uint32_t at,
                    bool hurtTarget, bool hurtSelf) {
  from &= 1;
  uint8_t tgt = 1 - from;
  if (lg.skipped) return at + 220;            // asleep / frozen: a beat, no show
  if (!lg.hurtSelf && (!mv || mv >= MOVE_COUNT)) return at;
  // reuse a slot that has finished playing NOW (not merely by `at`: this
  // turn's first effect ends exactly where the second starts), else the oldest
  int s = 0;
  uint32_t now = millis();
  for (int i = 0; i < BTL_FX_SLOTS; i++) {
    if (btlFx[i].shape == FXS_NONE || now >= btlFx[i].t0 + btlFx[i].dur) { s = i; break; }
    if (btlFx[i].t0 < btlFx[s].t0) s = i;
  }
  BtlFx &f = btlFx[s];
  f = BtlFx();
  const MoveEntry &m = MOVE_TBL[mv < MOVE_COUNT ? mv : 0];
  f.mv = mv; f.from = from; f.type = m.type < 18 ? m.type : 0;
  f.power = m.power ? m.power : 60;
  f.statMask = m.statMask;
  f.missed = lg.missed || lg.immune;
  f.crit = lg.crit;
  f.superEff = lg.effPct > 100;
  if (lg.hurtSelf) {                          // confused: it hit itself
    f.shape = FXS_SELFHIT; f.type = T_NORMAL; f.land = true;
  } else if (lg.charged) {
    f.shape = FXS_CHARGE;
  } else {
    f.shape = moveFxShape(mv);
    f.land = !f.missed;
  }
  if (f.shape == FXS_MULTI) f.n = lg.hits > 1 ? (lg.hits > 5 ? 5 : lg.hits) : 2;
  else if (f.shape == FXS_PROJ) f.n = f.power >= 90 ? 3 : f.power >= 55 ? 2 : 1;
  else if (f.shape == FXS_FALL) f.n = f.power >= 70 ? 4 : 3;
  f.drain = m.effect == EF_DRAIN && hurtTarget;
  f.recoil = hurtSelf && f.shape != FXS_SELFHIT && !f.drain;
  f.healSelf = lg.healed;
  fxTiming(f);
  f.t0 = at;

  uint32_t imp = at + f.impact;
  if (lg.damage && !lg.hurtSelf) {            // the attacker swings
    btlLungeFrom[from] = at;
    btlLungeUntil[from] = at + BTL_LUNGE_MS;
  }
  if (f.shape == FXS_SELFHIT && hurtSelf) {
    btlHitFrom[from] = imp; btlHitUntil[from] = imp + BTL_HIT_MS;
    btlHpHoldUntil[from] = imp;
  } else {
    if (hurtTarget) {
      uint32_t last = imp + (f.shape == FXS_MULTI ? FX_MULTI_GAP * (f.n - 1) : 0);
      btlHitFrom[tgt] = imp;
      btlHitUntil[tgt] = last + BTL_HIT_MS;
      btlHpHoldUntil[tgt] = imp;
    }
    if (hurtSelf) {                            // recoil
      btlHitFrom[from] = imp + 150; btlHitUntil[from] = imp + 150 + BTL_HIT_MS;
      btlHpHoldUntil[from] = imp + 150;
    }
    if (f.drain || lg.healed)                  // health flows back once it arrives
      btlHpHoldUntil[from] = at + (f.drain ? f.impact + FX_BURST_MS / 2 + FX_DRAIN_MS - 120 : 300);
  }
  return at + f.dur;
}

// ---- drawing ------------------------------------------------------------------
// Body centres and feet of the two creatures (you, foe) as btlSide draws them.
static const int16_t FX_CX[2] = { 108, 330 };
static const int16_t FX_CY[2] = { 192, 72 };
static const int16_t FX_FEET[2] = { 244, 116 };
#define FX_CLIP_Y 252        // the scene ends here: the menu band is below

static const int16_t FX_COS[16] = { 1000, 924, 707, 383, 0, -383, -707, -924,
                                    -1000, -924, -707, -383, 0, 383, 707, 924 };
#define FX_SIN(i) FX_COS[((i) + 12) & 15]

static inline bool fxOnPanel(int x, int y) {
  if (y < 0 || y >= FX_CLIP_Y || x < 0 || x >= 466) return false;
  int dx = x - CX, dy = y - CY;
  return dx * dx + dy * dy < 230 * 230;
}

// o: bit0 mirror X, bit1 mirror Y, bit2 transpose. 0,5,3,6 rotate 0/90/180/270.
static const uint8_t FX_ROT[4] = { 0, 5, 3, 6 };
static void fxSpr(const FxMap &m, int cx, int cy, int s, const uint16_t *p, uint8_t o) {
  if (s < 1) s = 1;
  int x0 = cx - 6 * s, y0 = cy - 6 * s;
  for (int r = 0; r < 12; r++) {
    for (int c = 0; c < 12; c++) {
      int rr = r, cc = c;
      if (o & 4) { rr = c; cc = r; }
      if (o & 1) cc = 11 - cc;
      if (o & 2) rr = 11 - rr;
      char ch = m[rr][cc];
      int k;
      switch (ch) {
        case 'K': k = 0; break; case 'D': k = 1; break; case 'M': k = 2; break;
        case 'L': k = 3; break; case 'W': k = 4; break; default: continue;
      }
      int x = x0 + c * s, y = y0 + r * s;
      if (!fxOnPanel(x, y)) continue;
      gfx->fillRect(x, y, s, s, p[k]);
    }
  }
}

static void fxThick(float x0, float y0, float x1, float y1, float w, uint16_t col) {
  float dx = x1 - x0, dy = y1 - y0;
  float len = sqrtf(dx * dx + dy * dy);
  if (len < 0.5f || w < 0.5f) return;
  float nx = -dy / len * w * 0.5f, ny = dx / len * w * 0.5f;
  int ax = (int)(x0 + nx), ay = (int)(y0 + ny), bx = (int)(x0 - nx), by = (int)(y0 - ny);
  int cx2 = (int)(x1 + nx), cy2 = (int)(y1 + ny), ex = (int)(x1 - nx), ey = (int)(y1 - ny);
  gfx->fillTriangle(ax, ay, bx, by, cx2, cy2, col);
  gfx->fillTriangle(bx, by, ex, ey, cx2, cy2, col);
}

static void fxRing(int x, int y, int r, int w, uint16_t col) {
  for (int i = 0; i < w; i++) if (r - i > 0) gfx->drawCircle(x, y, r - i, col);
}

static inline int fxLerp(int a, int b, int p) { return a + (b - a) * p / 1000; }

// The impact: a flash ring, a star, the type's particles flung outward. Super
// effective throws more of them and harder; a critical adds speed lines.
static void fxBurst(int x, int y, int t, const BtlFx &f, int big) {
  if (t < 0 || t >= FX_BURST_MS) return;
  const uint16_t *P = FX_PAL[f.type];
  const FxMap &part = *moveFxSprite(f.mv, f.type);
  if (t < 200) {
    int r = 10 + t / 4 + big * 4;
    fxRing(x, y, r, 4, P[3]);
    fxRing(x, y, r - 4, 2, P[4]);
  }
  if (f.crit && t < 240) {
    for (int i = 0; i < 16; i += 2) {
      int r0 = 26 + t / 3, r1 = r0 + 22;
      fxThick(x + FX_COS[i] * r0 / 1000, y + FX_SIN(i) * r0 / 1000,
              x + FX_COS[i] * r1 / 1000, y + FX_SIN(i) * r1 / 1000, 3, 0xFFFF);
    }
  }
  int k = f.superEff ? 10 : 6;
  for (int i = 0; i < k; i++) {
    int dir = (i * 16 / k + (f.mv & 3)) & 15;
    int sp = (f.superEff ? 120 : 85) + (i % 3) * 15;
    int d = 10 + t * sp / FX_BURST_MS;
    int px = x + FX_COS[dir] * d / 1000;
    int py = y + FX_SIN(dir) * d / 1000 + t * t / 6000;   // they fall a little
    fxSpr(part, px, py, t < 260 ? 2 : 1, P, FX_ROT[(i + t / 90) & 3]);
  }
  if (t < 220) {                                         // the star, last, on top
    int s = t < 90 ? 2 + t / 30 : 5 - (t - 90) / 50;
    fxSpr(FXP_STAR, x, y, s + big + 1, P, 0);
  }
}

static void fxArrowRise(int x, int y, int t, const uint16_t *P, bool up, int i) {
  int span = 520;
  int tt = t - i * 90;
  if (tt < 0 || tt >= span) return;
  int dy = tt * 70 / span;
  fxSpr(FXP_ARROW, x, up ? y - dy : y + dy - 70, tt < span - 120 ? 2 : 1, P, up ? 0 : 2);
}

static const uint16_t *fxStatPal(uint8_t mask) {
  if (mask & ST_ATK) return FX_PAL[T_FIRE];
  if (mask & ST_DEF) return FX_PAL[T_WATER];
  if (mask & ST_SPE) return FX_PAL[T_ELECTRIC];
  if (mask & ST_SPA) return FX_PAL[T_PSYCHIC];
  if (mask & ST_SPD) return FX_PAL[T_GRASS];
  return FX_PAL[T_NORMAL];
}

// A filled disc that respects the scene clip (fillCircle alone would spill into
// the menu band below FX_CLIP_Y).
static void fxDisc(int x, int y, int r, uint16_t col) {
  if (r < 1 || !fxOnPanel(x, y)) return;
  if (y + r >= FX_CLIP_Y) r = FX_CLIP_Y - 1 - y;
  if (r >= 1) gfx->fillCircle(x, y, r, col); else gfx->fillRect(x, y, 1, 1, col);
}

// A point along the jet: a shallow arc from mouth to target, p in 0..1000.
static void fxJetAt(int ax, int ay, int tx, int ty, int p, int &x, int &y) {
  x = fxLerp(ax, tx, p);
  y = fxLerp(ay, ty, p) - 26 * 4 * p / 1000 * (1000 - p) / 1000;
}

// Water Gun / Hydro Pump. A pressurised STREAM rather than tumbling drops: a
// dark-rimmed body that thickens toward the head, a rippling highlight down its
// back, bright dashes that race along it so it reads as flowing, droplets
// peeling off its sides, and a fan of splash at the target while it pours.
static void fxJet(const BtlFx &f, int t, int ax, int ay, int tx, int ty) {
  const uint16_t *P = FX_PAL[f.type];
  bool big = f.power >= 90;
  int w0 = big ? 14 : 9, w1 = big ? 30 : 19;
  int head = t >= FX_JET_HEAD ? 1000 : t * 1000 / FX_JET_HEAD;
  int tail = t < FX_JET_HOLD ? 0 : (t - FX_JET_HOLD) * 1000 / FX_JET_TAIL;
  if (tail > 1000) tail = 1000;
  if (tail < head) {
    // Built from overlapping discs ~5 px apart rather than straight segments:
    // the lumpy outline is what makes it read as liquid instead of a pipe.
    int x0, y0, x1, y1;
    fxJetAt(ax, ay, tx, ty, tail, x0, y0);
    fxJetAt(ax, ay, tx, ty, head, x1, y1);
    int len = abs(x1 - x0) + abs(y1 - y0);
    int N = len / 5;
    if (N < 2) N = 2;
    if (N > 60) N = 60;
    for (int pass = 0; pass < 3; pass++)
      for (int j = 0; j <= N; j++) {
        int p = tail + (head - tail) * j / N;
        int x, y;
        fxJetAt(ax, ay, tx, ty, p, x, y);
        // thicker toward the target, with a ripple travelling down its length
        int w = w0 + (w1 - w0) * p / 1000;
        w += (int)(w * 0.25f * sinf(p * 0.035f - t * 0.05f));
        if (tail > 0) w = w * (1000 - tail / 2) / 1000;      // thins as it runs out
        int r = w / 2 < 2 ? 2 : w / 2;
        if (pass == 0) fxDisc(x, y, r + 2, P[1]);               // rim
        else if (pass == 1) fxDisc(x, y, r, P[2]);              // water
        else {
          fxDisc(x - r / 4, y - r / 2, r / 3 + 1, P[3]);       // glint along the top
          if (((j + t / 30) % 5) == 0) fxDisc(x - r / 4, y - r / 2, r / 4 + 1, P[4]);  // racing sparkle
        }
      }
    if (head < 1000) {                                         // the blunt head
      int r = w1 * 3 / 5;
      fxDisc(x1, y1, r + 2, P[1]);
      fxDisc(x1, y1, r, P[2]);
      fxDisc(x1 - r / 3, y1 - r / 3, r / 3 + 1, P[4]);
    }
    // droplets peeling off the sides, each flung out and falling for 240 ms
    for (int k = 0; k < (big ? 9 : 6); k++) {
      int born = 40 + k * 70, q = t - born;
      if (q < 0 || q > 240 || born > FX_JET_HOLD) continue;
      int p = 120 + (k * 211) % 760;
      if (p < tail || p > head) continue;
      int x, y;
      fxJetAt(ax, ay, tx, ty, p, x, y);
      int side = (k & 1) ? 1 : -1;
      x += side * q / 9;
      y += -q / 7 + q * q / 2600;
      fxDisc(x, y, big ? 3 : 2, P[3]);
      fxDisc(x - 1, y - 1, 1, P[4]);
    }
  }
  if (!f.land || t < FX_JET_HEAD) return;
  // ---- the splash: a ring the moment it lands, then a fan of drops for as
  // long as the jet pours, each arcing up and falling back
  int st = t - FX_JET_HEAD;
  if (st < 200) {                                 // a tight splash ring
    int r = 10 + st / 8 + (big ? 6 : 0);
    fxRing(tx, ty, r, 3, P[2]);
    fxRing(tx, ty, r - 3, 1, P[4]);
  }
  int pourEnd0 = FX_JET_HOLD + FX_JET_TAIL - FX_JET_HEAD;
  if (st < pourEnd0)                              // foam churning where it hits
    for (int i = 0; i < (big ? 7 : 5); i++) {
      int ph = (st / 40 + i * 3) % 8;
      int dir = (i * 5 + st / 60) & 15;
      int d = 4 + ph * (big ? 4 : 3);
      fxDisc(tx + FX_COS[dir] * d / 1000, ty + FX_SIN(dir) * d / 1000,
             (big ? 7 : 5) - ph * (big ? 7 : 5) / 9, ph & 1 ? P[4] : P[3]);
    }
  if (f.crit && st < 240)
    for (int i = 0; i < 16; i += 2) {
      int r0 = 28 + st / 3, r1 = r0 + 22;
      fxThick(tx + FX_COS[i] * r0 / 1000, ty + FX_SIN(i) * r0 / 1000,
              tx + FX_COS[i] * r1 / 1000, ty + FX_SIN(i) * r1 / 1000, 3, 0xFFFF);
    }
  int pourEnd = FX_JET_HOLD + FX_JET_TAIL - FX_JET_HEAD;   // last moment water arrives
  int n = big ? 14 : 9;
  if (f.superEff) n += 4;
  const int life = 300;
  for (int i = 0; i < n; i++) {
    // drops are emitted in a steady cycle while the water is arriving
    int cyc = (i * life) / n;
    for (int born = cyc; born <= pourEnd; born += life) {
      int q = st - born;
      if (q < 0 || q >= life) continue;
      int ang = 9 + (i * 5) % 7;               // 9..15 of 16: up-left .. up-right
      int sp = (big ? 150 : 110) + (i % 3) * 25 + (f.superEff ? 30 : 0);
      int x = tx + FX_COS[ang & 15] * sp / 1000 * q / 300;
      int y = ty + 6 + FX_SIN(ang & 15) * sp / 1000 * q / 300 + q * q / 900;
      if (q < life - 80) fxSpr(FXP_DROP, x, y, (big && (i % 3 == 0)) ? 2 : 1, P, q > 150 ? 2 : 0);
      else fxDisc(x, y, 2, P[3]);
    }
  }
}

static void fxDrawOne(const BtlFx &f, uint32_t now) {
  if (f.shape == FXS_NONE || now < f.t0 || now >= f.t0 + f.dur) return;
  int t = (int)(now - f.t0);
  const uint16_t *P = FX_PAL[f.type];
  const FxMap &part = *moveFxSprite(f.mv, f.type);
  uint8_t a = f.from, b = 1 - f.from;
  // launch from in front of the attacker, toward the target
  int ax = FX_CX[a] + (a == 0 ? 34 : -34), ay = FX_CY[a] + (a == 0 ? -18 : 16);
  int tx = FX_CX[b], ty = FX_CY[b];
  if (f.missed) { tx += (b == 1 ? 60 : -60); ty += (b == 1 ? -40 : 40); }  // sails past
  int imp = f.impact;
  int big = f.power >= 100 ? 1 : 0;

  switch (f.shape) {
    case FXS_CONTACT:
      break;                                  // the lunge carries it; the burst lands
    case FXS_SELFHIT:
      fxBurst(FX_CX[a], FX_CY[a], t - imp, f, 0);
      return;
    case FXS_PROJ: {
      int s = 2 + (f.power >= 60) + (f.power >= 100);
      for (int i = 0; i < f.n; i++) {
        int tt = t - i * 90;
        if (tt < 0 || tt > 380) continue;
        int p = tt * 1000 / 380;
        for (int g = 2; g >= 0; g--) {        // a fading trail behind it
          int pg = p - g * 70;
          if (pg < 0) continue;
          int x = fxLerp(ax, tx, pg), y = fxLerp(ay, ty, pg) - 50 * 4 * pg / 1000 * (1000 - pg) / 1000;
          fxSpr(part, x, y, g ? s - 1 : s, P, FX_ROT[(tt / 70 + g) & 3]);
        }
      }
      break;
    }
    case FXS_JET:
      fxJet(f, t, ax, ay, tx, ty);
      return;                                 // it draws its own splash
    case FXS_STREAM: {
      // a gush of particles for ~half a second, each crossing in 260 ms
      int s = f.power >= 90 ? 3 : 2;
      for (int i = 0; i < 12; i++) {
        int born = i * 45;
        int tt = t - born;
        if (tt < 0 || tt > 260) continue;
        int p = tt * 1000 / 260;
        int wob = ((i * 37) % 21) - 10;
        int x = fxLerp(ax, tx, p) + (wob * FX_SIN(4) / 1000);
        int y = fxLerp(ay, ty, p) + wob;
        fxSpr(part, x, y, s, P, FX_ROT[(i + tt / 80) & 3]);
      }
      break;
    }
    case FXS_BEAM: {
      if (t > 640) break;
      int p = t < imp ? t * 1000 / imp : 1000;
      int ex = fxLerp(ax, tx, p), ey = fxLerp(ay, ty, p);
      float w = 10 + f.power / 14;
      if (t > 520) w = w * (640 - t) / 120;
      w += ((t / 40) & 1) ? 2 : -1;         // it crackles
      fxThick(ax, ay, ex, ey, w + 6, P[1]);
      fxThick(ax, ay, ex, ey, w, P[2]);
      fxThick(ax, ay, ex, ey, w * 0.55f, P[3]);
      fxThick(ax, ay, ex, ey, w * 0.25f, P[4]);
      gfx->fillCircle(ax, ay, (int)(w * 0.7f) + 2, P[3]);
      gfx->fillCircle(ax, ay, (int)(w * 0.35f) + 1, P[4]);
      for (int i = 0; i < 4; i++) {           // sparks riding the beam
        int q = ((t * 3 + i * 250) % 1000) * p / 1000;
        fxSpr(FXP_SPARK, fxLerp(ax, tx, q), fxLerp(ay, ty, q) + ((i & 1) ? 7 : -7), 1, P, 0);
      }
      break;
    }
    case FXS_BOLTS: {
      if (t > 460) break;
      if (((t / 60) % 3) == 2) break;         // flicker
      int seg = 7;
      int px = tx + 10, py = -10;
      for (int i = 1; i <= seg; i++) {
        int ny = py + (ty + 10) / seg;
        int jit = (int)(((uint32_t)(i * 2654435761u) ^ (uint32_t)(t / 60) * 40503u) % 44) - 22;
        int nx = (i == seg) ? tx : tx + jit;
        if (i == seg) ny = ty;
        fxThick(px, py, nx, ny, 11, P[1]);
        fxThick(px, py, nx, ny, 6, P[2]);
        fxThick(px, py, nx, ny, 2, P[4]);
        px = nx; py = ny;
      }
      for (int i = 0; i < 3; i++)
        fxSpr(FXP_BOLT, tx + (i - 1) * 34, ty + 20 - ((t / 50 + i) % 3) * 8, 2, P, (i & 1) ? 1 : 0);
      break;
    }
    case FXS_FALL: {
      for (int i = 0; i < f.n; i++) {
        int tt = t - i * 130;
        if (tt < 0) continue;
        int x = tx + (i - f.n / 2) * 26 + (i & 1) * 8;
        if (tt < 300) {
          int y = fxLerp(-20, ty - 6 + (i & 1) * 12, tt * 1000 / 300);
          fxSpr(part, x, y, 3 + (i == 0), P, FX_ROT[(tt / 80 + i) & 3]);
        } else if (tt < 300 + 160) {
          BtlFx g = f; g.superEff = false; g.crit = f.crit && i == 0;
          if (f.land) fxBurst(x, ty, tt - 300, g, 0);
        }
      }
      return;                                  // its own bursts
    }
    case FXS_QUAKE: {
      // cracks run along the ground under the target, dust and rocks jump
      int gy = FX_FEET[b];
      int reach = t < 300 ? t * 70 / 300 : 70;
      int x = tx - reach, y = gy;
      for (int i = 0; i < 8; i++) {
        int nx = tx - reach + (i + 1) * reach * 2 / 8;
        int ny = gy + (((i * 7) % 5) - 2) * 3;
        fxThick(x, y, nx, ny, 4, P[0]);
        x = nx; y = ny;
      }
      for (int i = 0; i < 6; i++) {
        int tt = t - 60 - i * 70;
        if (tt < 0 || tt > 420) continue;
        int px = tx - 60 + i * 24;
        int py = gy - tt * 130 / 420 + tt * tt / 1400;
        fxSpr(FXP_ROCK, px, py, (i & 1) ? 2 : 3, P, FX_ROT[(tt / 90 + i) & 3]);
      }
      break;
    }
    case FXS_SLASH: {
      for (int i = 0; i < 3; i++) {
        int tt = t - i * 70;
        if (tt < 0 || tt > 260) continue;
        int len = tt < 90 ? tt * 1000 / 90 : 1000;
        int off = (i - 1) * 22;
        int x0 = tx - 42 + off, y0 = ty - 42, x1 = tx + 34 + off, y1 = ty + 40;
        int ex = fxLerp(x0, x1, len), ey = fxLerp(y0, y1, len);
        float w = tt < 160 ? 9 : 9.0f * (260 - tt) / 100;
        fxThick(x0, y0, ex, ey, w + 4, P[0]);
        fxThick(x0, y0, ex, ey, w, P[2]);
        fxThick(x0, y0, ex, ey, w * 0.45f, P[4]);
      }
      break;
    }
    case FXS_BITE: {
      if (t > imp + 240) break;
      int close = t < imp ? t * 1000 / imp : 1000;
      int gap = 70 - 60 * close / 1000;
      for (int j = 0; j < 2; j++) {           // upper and lower jaw
        int jy = j ? ty + gap : ty - gap;
        for (int k = -2; k <= 1; k++) {
          int fx = tx + k * 22 + (j ? 11 : 0);
          int tip = j ? jy - 26 : jy + 26;
          gfx->fillTriangle(fx - 11, jy, fx + 11, jy, fx, tip, FX_PAL[T_DARK][0]);
          gfx->fillTriangle(fx - 8, jy + (j ? -1 : 1), fx + 8, jy + (j ? -1 : 1),
                            fx, tip + (j ? 4 : -4), 0xFFFF);
        }
        int bx = tx - 56;
        gfx->fillRoundRect(bx, j ? jy : jy - 10, 112, 10, 4, P[1]);
      }
      break;
    }
    case FXS_WAVE: {
      for (int i = 0; i < 4; i++) {
        int tt = t - i * 90;
        if (tt < 0 || tt > 420) continue;
        int p = tt * 1000 / 360;
        if (p > 1000) p = 1000;
        int x = fxLerp(ax, tx, p), y = fxLerp(ay, ty, p);
        int r = 8 + tt / 6;
        fxRing(x, y, r, 4, P[2]);
        fxRing(x, y, r - 4, 2, P[4]);
      }
      if (t >= imp && t < imp + 300) {        // the target warps
        int r = 40 + ((t / 50) & 1) * 6;
        fxRing(tx, ty, r, 3, P[3]);
        fxRing(tx, ty, r + 10, 2, P[2]);
      }
      break;
    }
    case FXS_MULTI: {
      for (int i = 0; i < f.n; i++) {
        int tt = t - i * FX_MULTI_GAP;
        if (tt < 0) continue;
        if (tt < imp) {
          int p = tt * 1000 / imp;
          fxSpr(part, fxLerp(ax, tx, p), fxLerp(ay, ty, p) + ((i & 1) ? 10 : -10), 2, P, 0);
        } else if (f.land) {
          BtlFx g = f; g.superEff = f.superEff && i == f.n - 1;
          fxBurst(tx + ((i & 1) ? 16 : -16), ty + ((i % 3) - 1) * 12, tt - imp, g, 0);
        }
      }
      return;
    }
    case FXS_BUFF: case FXS_HEAL: case FXS_CHARGE: {
      const uint16_t *Q = f.shape == FXS_HEAL ? FX_PAL[T_GRASS]
                        : f.shape == FXS_BUFF ? fxStatPal(f.statMask) : P;
      int x = FX_CX[a], y = FX_CY[a];
      int pulse = ((t / 70) & 1) * 4;
      if (t < 700) {
        fxRing(x, y, 46 + pulse, 3, Q[2]);
        fxRing(x, y, 52 + pulse, 1, Q[3]);
      }
      if (f.shape == FXS_BUFF) {
        for (int i = 0; i < 5; i++) fxArrowRise(x - 44 + i * 22, FX_FEET[a] - 6, t, Q, true, i);
      } else if (f.shape == FXS_HEAL) {
        for (int i = 0; i < 6; i++) {
          int tt = t - i * 80;
          if (tt < 0 || tt > 500) continue;
          fxSpr(FXP_SPARK, x - 40 + (i * 29) % 80, FX_FEET[a] - 10 - tt * 90 / 500, 2, Q, 0);
        }
      } else {                                  // gathering power: particles fall inward
        for (int i = 0; i < 8; i++) {
          int dir = i * 2;
          int r = 80 - (t % 360) * 70 / 360;
          fxSpr(part, x + FX_COS[dir] * r / 1000, y + FX_SIN(dir) * r / 1000, 2, P, 0);
        }
      }
      return;
    }
    case FXS_DEBUFF: {
      bool sound = f.mv == MV_GROWL || f.mv == MV_SCREECH;
      if (t < imp) {
        int p = t * 1000 / imp;
        for (int i = 0; i < 3; i++) {
          int q = p - i * 150;
          if (q < 0) continue;
          int x = fxLerp(ax, tx, q), y = fxLerp(ay, ty, q) + ((i & 1) ? 12 : -12);
          if (sound) fxSpr(FXP_NOTE, x, y, 2, P, 0);
          else if (f.mv == MV_STRING_SHOT) fxThick(ax, ay, x, y, 2, 0xFFFF);
          else fxSpr(FXP_SPARK, x, y, 2, P, 0);
        }
      } else if (f.land) {
        const uint16_t *Q = FX_PAL[T_WATER];
        // anchored low enough that the foe's arrows stay inside the round panel
        for (int i = 0; i < 4; i++) fxArrowRise(tx - 33 + i * 22, FX_CY[b] + 34, t - imp, Q, false, i);
      }
      return;
    }
    default: break;
  }
  if (f.land && f.shape != FXS_MULTI) fxBurst(tx, ty, t - imp, f, big);
  if (f.recoil) fxBurst(FX_CX[a], FX_CY[a], t - imp - 150, BtlFx(), 0);
  if (f.drain) {                               // health flows back to the attacker
    int tt = t - imp - FX_BURST_MS / 2;
    for (int i = 0; i < 5; i++) {
      int q = tt - i * 60;
      if (q < 0 || q > FX_DRAIN_MS - 240) continue;
      int p = q * 1000 / (FX_DRAIN_MS - 240);
      int x = fxLerp(tx, FX_CX[a], p), y = fxLerp(ty, FX_CY[a], p) - 40 * 4 * p / 1000 * (1000 - p) / 1000;
      fxSpr(FXP_ORB, x + ((i & 1) ? 10 : -10), y, 2, FX_PAL[T_GRASS], 0);
    }
  }
}

// Called by renderBattle() over the two creatures.
void btlFxDraw() {
  uint32_t now = millis();
  for (int i = 0; i < BTL_FX_SLOTS; i++) fxDrawOne(btlFx[i], now);
}

// Extra sprite offset for side `who` this frame: an earthquake rocks both,
// a super effective hit knocks the target about.
void btlFxShake(uint8_t who, int &dx, int &dy) {
  uint32_t now = millis();
  for (int i = 0; i < BTL_FX_SLOTS; i++) {
    const BtlFx &f = btlFx[i];
    if (f.shape == FXS_NONE || now < f.t0 || now >= f.t0 + f.dur) continue;
    int t = (int)(now - f.t0);
    if (f.shape == FXS_QUAKE && t >= f.impact && t < f.impact + 560) {
      int amp = (who == f.from) ? 3 : 7;
      dx += ((t / 40) & 1) ? amp : -amp;
      dy += ((t / 60) & 1) ? amp / 2 : 0;
    }
    if (f.land && f.superEff && who != f.from && t >= f.impact && t < f.impact + 300)
      dy += ((t / 45) & 1) ? 4 : -4;
  }
}
