// ---------- The bag: items, held items (feature 8). Logic: items.cpp ----------
// Owned by this feature alone. Reached from the Adventure hub (ui_hub.ino);
// use featBegin/featRow/featRowAt/featBackPill/featBackHit/featLine from there,
// and leave with featBackToHub(). Every render path MUST end in gfx->flush().

void bagHubLabel(char *out, size_t n, const char *name) { snprintf(out, n, "%s", name); }
bool bagHubEnabled() { return true; }
void bagOpened() {}

void renderBag() {
  featBegin("bag");
  featBackPill();
  gfx->flush();
}

void bagTap(int16_t x, int16_t y) {
  if (featBackHit(x, y)) { sfxPlay(SFX_TAP); featBackToHub(); }
}

void bagSwipe(int dir) { (void)dir; featBackToHub(); }
