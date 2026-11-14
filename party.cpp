#include "party.h"
#include <stdlib.h>
#include <string.h>
#include "dex.h"
#include "items.h"   // itemIsHeld(): a stored held id is checked like a dex number
#include <stddef.h>

Party party;

// One record off flash, made safe to use.
static void sanitize(PartyMon &s) {
  if (s.dex < 1 || s.dex > DEX_COUNT) s = PartyMon();
  s.nick[sizeof(s.nick) - 1] = 0;
  if (!itemIsHeld(s.held)) s.held = 0;   // an unknown or non-holdable id
  s.spare = 0;
}

// Same NVS namespace as the pet on purpose: WIPE (Pet::factoryReset) calls
// clear() on it, and a factory reset that left the party behind would be a lie.
// Reads `n` records stored under `key` into dst[0..n), whatever layout wrote
// them. Returns true when the blob was in an OLDER layout, so the caller can
// rewrite it once in the current one.
//
// The blob is raw structs, so growing PartyMon changes its stride. Reading an
// older, shorter blob straight into the new array would land record 1 onward at
// the wrong offset and quietly invent creatures out of misaligned bytes -- the
// dex-range check in begin() would not reliably catch it, since a stray byte is
// often a valid Pokedex number.
//
// So the stride is never GUESSED from the length (stored / slots), which cannot
// tell a stride change from a slot-count change and is exactly what would have
// read an 18-slot box as 10-byte records. It is matched against the strides
// PartyMon has actually had (party.h), newest first, and each old record is
// copied into the FRONT of a fresh one: every field appended since (moves[],
// held) keeps its zero initialiser.
static bool loadRecords(Preferences &p, const char *key, PartyMon *dst, size_t n) {
  const size_t want = n * sizeof(PartyMon);
  size_t stored = p.getBytesLength(key);
  if (!stored) return false;               // absent: dst stays empty
  if (stored == want) { p.getBytes(key, dst, want); return false; }
  // getBytes copies NOTHING when the stored blob exceeds the buffer, so every
  // other case reads through a temporary of the stored size.
  uint8_t *tmp = (uint8_t *)malloc(stored);
  if (!tmp) return false;
  bool migrated = false;
  if (p.getBytes(key, tmp, stored) == stored) {
    if (stored > want && stored % sizeof(PartyMon) == 0) {
      // A build with MORE SLOTS at our own stride: keep the first n. Anything
      // else that is merely "too long" could equally be fewer slots at a bigger
      // stride, so that is left empty rather than misread.
      memcpy(dst, tmp, want);
    } else {
      static const size_t OLD_STRIDES[] = { PARTYMON_V2_SIZE, PARTYMON_V1_SIZE };
      for (size_t stride : OLD_STRIDES) {
        if (stored % stride || stored / stride > n) continue;
        for (size_t i = 0; i < stored / stride; i++)
          memcpy((uint8_t *)&dst[i], tmp + i * stride, stride);
        migrated = true;
        break;
      }
    }
  }
  free(tmp);
  return migrated;
}

static_assert(sizeof(PartyMon) > PARTYMON_V2_SIZE,
              "a new PartyMon field must GROW the record, or old blobs cannot be told apart");
static_assert(offsetof(PartyMon, held) == PARTYMON_V2_SIZE,
              "held must sit right after moves[]: fields are only ever appended");

// Same NVS namespace as the pet on purpose: WIPE (Pet::factoryReset) calls
// clear() on it, and a factory reset that left the party behind would be a lie.
void Party::begin() {
  // Start from empty: getBytes() leaves the destination untouched when the key
  // is missing, so without this a reload after a wipe would keep showing the
  // old party out of RAM.
  for (auto &s : slots) s = PartyMon();
  for (auto &s : box) s = PartyMon();
  prefs.begin("tamapoke", false);
  // The box is a separate key and simply absent on a pre-box save, which leaves
  // it zeroed -- exactly what an empty box is. Both go through the SAME reader:
  // the box once had no migration at all, so the first stride change after it
  // existed (the held item) would have emptied every player's box.
  bool oldParty = loadRecords(prefs, "party", slots, PARTY_SLOTS);
  bool oldBox = loadRecords(prefs, "box", box, BOX_SLOTS);
  // a blob written by an older/newer build could hold nonsense; drop anything
  // that is not a real Pokedex number rather than indexing DEX_TBL with it
  for (auto &s : slots) sanitize(s);
  for (auto &s : box) sanitize(s);
  // rewrite in the current layout so the migration only ever happens once
  if (oldParty) save();
  if (oldBox) boxSave();
}

void Party::save() {
  prefs.putBytes("party", slots, sizeof(slots));
}

void Party::boxSave() {
  prefs.putBytes("box", box, sizeof(box));
}

uint8_t Party::boxCount() const {
  uint8_t n = 0;
  for (auto &s : box)
    if (!s.empty()) n++;
  return n;
}

int Party::boxFirstFree() const {
  for (int i = 0; i < BOX_SLOTS; i++)
    if (box[i].empty()) return i;
  return -1;
}

bool Party::boxAdd(const PartyMon &m) {
  int i = boxFirstFree();
  if (i < 0) return false;
  box[i] = m;
  boxSave();
  return true;
}

void Party::boxReleaseAt(uint8_t i) {
  if (i >= BOX_SLOTS) return;
  box[i] = PartyMon();
  boxSave();
}

void Party::swapPartyBox(uint8_t partyIdx, uint8_t boxIdx) {
  if (partyIdx >= PARTY_SLOTS || boxIdx >= BOX_SLOTS) return;
  PartyMon t = slots[partyIdx];
  slots[partyIdx] = box[boxIdx];
  box[boxIdx] = t;
  save();
  boxSave();
}

uint8_t Party::count() const {
  uint8_t n = 0;
  for (auto &s : slots)
    if (!s.empty()) n++;
  return n;
}

int Party::firstFree() const {
  for (int i = 0; i < PARTY_SLOTS; i++)
    if (slots[i].empty()) return i;
  return -1;
}

bool Party::add(const PartyMon &m) {
  int i = firstFree();
  if (i < 0) return false;
  slots[i] = m;
  save();
  return true;
}

void Party::replaceAt(uint8_t i, const PartyMon &m) {
  if (i >= PARTY_SLOTS) return;
  slots[i] = m;
  save();
}

void Party::releaseAt(uint8_t i) {
  if (i >= PARTY_SLOTS) return;
  slots[i] = PartyMon();
  save();
}

// Mirrors calcStat() in pet.cpp: base + level + IV contribution + training.
// Kept in step with it by hand; there is no shared home for it that both the
// live pet and a frozen party member could use without dragging Pet in here.
static uint16_t calcStat(uint8_t base, uint8_t iv, uint16_t lvl, uint8_t tr) {
  return (uint16_t)base + lvl + (uint32_t)iv * lvl / 100 + tr;
}

uint16_t Party::atkOf(const PartyMon &m) const {
  return m.empty() ? 0 : calcStat(DEX_TBL[m.dex].bAtk, m.ivAtk, m.level, m.trAtk);
}
uint16_t Party::defOf(const PartyMon &m) const {
  return m.empty() ? 0 : calcStat(DEX_TBL[m.dex].bDef, m.ivDef, m.level, m.trDef);
}
uint16_t Party::speOf(const PartyMon &m) const {
  return m.empty() ? 0 : calcStat(DEX_TBL[m.dex].bSpe, m.ivSpe, m.level, m.trSpe);
}
uint16_t Party::vitOf(const PartyMon &m) const {
  return m.empty() ? 0 : calcStat(DEX_TBL[m.dex].bHp, m.ivHp, m.level, 10);
}
// Special reuses the physical IV and training, same rule as Pet::spaStat().
uint16_t Party::spaOf(const PartyMon &m) const {
  return m.empty() ? 0 : calcStat(DEX_TBL[m.dex].bSpA, m.ivAtk, m.level, m.trAtk);
}
uint16_t Party::spdOf(const PartyMon &m) const {
  return m.empty() ? 0 : calcStat(DEX_TBL[m.dex].bSpD, m.ivDef, m.level, m.trDef);
}
