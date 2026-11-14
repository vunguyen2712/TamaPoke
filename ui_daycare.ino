// ---------- Daycare / breeding (feature 5). Logic: breed.cpp ----------
// Owned by this feature alone. Reached from the Adventure hub (ui_hub.ino);
// use featBegin/featRow/featRowAt/featBackPill/featBackHit/featLine from there,
// and leave with featBackToHub(). Every render path MUST end in gfx->flush().

void daycareHubLabel(char *out, size_t n, const char *name) { snprintf(out, n, "%s", name); }
bool daycareHubEnabled() { return true; }
void daycareOpened() {}

void renderDaycare() {
  featBegin("daycare");
  featBackPill();
  gfx->flush();
}

void daycareTap(int16_t x, int16_t y) {
  if (featBackHit(x, y)) { sfxPlay(SFX_TAP); featBackToHub(); }
}

void daycareSwipe(int dir) { (void)dir; featBackToHub(); }
