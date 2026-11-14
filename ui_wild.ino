// ---------- Wild encounters (feature 1 + 2). Logic: wild.cpp ----------
// Owned by this feature alone. Reached from the Adventure hub (ui_hub.ino);
// use featBegin/featRow/featRowAt/featBackPill/featBackHit/featLine from there,
// and leave with featBackToHub(). Every render path MUST end in gfx->flush().

void wildHubLabel(char *out, size_t n, const char *name) { snprintf(out, n, "%s", name); }
bool wildHubEnabled() { return true; }
void wildOpened() {}

void renderWild() {
  featBegin("wild");
  featBackPill();
  gfx->flush();
}

void wildTap(int16_t x, int16_t y) {
  if (featBackHit(x, y)) { sfxPlay(SFX_TAP); featBackToHub(); }
}

void wildSwipe(int dir) { (void)dir; featBackToHub(); }

void wildBattleEnded(bool won, bool ran) { (void)won; (void)ran; }
