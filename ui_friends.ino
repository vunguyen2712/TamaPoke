// ---------- Two-board extras: gift swap and visits (feature 7). Logic: friends.cpp ----------
// Owned by this feature alone. Reached from the Adventure hub (ui_hub.ino);
// use featBegin/featRow/featRowAt/featBackPill/featBackHit/featLine from there,
// and leave with featBackToHub(). Every render path MUST end in gfx->flush().

void friendsHubLabel(char *out, size_t n, const char *name) { snprintf(out, n, "%s", name); }
bool friendsHubEnabled() { return true; }
void friendsOpened() {}

void renderFriends() {
  featBegin("friends");
  featBackPill();
  gfx->flush();
}

void friendsTap(int16_t x, int16_t y) {
  if (featBackHit(x, y)) { sfxPlay(SFX_TAP); featBackToHub(); }
}

void friendsSwipe(int dir) { (void)dir; featBackToHub(); }
