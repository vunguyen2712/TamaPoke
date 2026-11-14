// ---------- Battle Tower and gym rematches (features 4 + 6). Logic: tower.cpp ----------
// Owned by this feature alone. Reached from the Adventure hub (ui_hub.ino);
// use featBegin/featRow/featRowAt/featBackPill/featBackHit/featLine from there,
// and leave with featBackToHub(). Every render path MUST end in gfx->flush().

void towerHubLabel(char *out, size_t n, const char *name) { snprintf(out, n, "%s", name); }
bool towerHubEnabled() { return true; }
void towerOpened() {}

void renderTower() {
  featBegin("tower");
  featBackPill();
  gfx->flush();
}

void towerTap(int16_t x, int16_t y) {
  if (featBackHit(x, y)) { sfxPlay(SFX_TAP); featBackToHub(); }
}

void towerSwipe(int dir) { (void)dir; featBackToHub(); }

void towerBattleEnded(bool won, bool ran) { (void)won; (void)ran; }
