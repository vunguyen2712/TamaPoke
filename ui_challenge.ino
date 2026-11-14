// ---------- Weekly gym challenge (feature 4). Logic: challenge.cpp ----------
// Owned by this feature alone. Reached from the Adventure hub (ui_hub.ino);
// use featBegin/featRow/featRowAt/featBackPill/featBackHit/featLine from there,
// and leave with featBackToHub(). Every render path MUST end in gfx->flush().

void challengeHubLabel(char *out, size_t n, const char *name) { snprintf(out, n, "%s", name); }
bool challengeHubEnabled() { return true; }
void challengeOpened() {}

void renderChallenge() {
  featBegin("challenge");
  featBackPill();
  gfx->flush();
}

void challengeTap(int16_t x, int16_t y) {
  if (featBackHit(x, y)) { sfxPlay(SFX_TAP); featBackToHub(); }
}

void challengeSwipe(int dir) { (void)dir; featBackToHub(); }

void challengeBattleEnded(bool won, bool ran) { (void)won; (void)ran; }
