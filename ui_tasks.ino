// ---------- Daily and weekly tasks (feature 3). Logic: tasks.cpp ----------
// Owned by this feature alone. Reached from the Adventure hub (ui_hub.ino);
// use featBegin/featRow/featRowAt/featBackPill/featBackHit/featLine from there,
// and leave with featBackToHub(). Every render path MUST end in gfx->flush().

void tasksHubLabel(char *out, size_t n, const char *name) { snprintf(out, n, "%s", name); }
bool tasksHubEnabled() { return true; }
void tasksOpened() {}

void renderTasks() {
  featBegin("tasks");
  featBackPill();
  gfx->flush();
}

void tasksTap(int16_t x, int16_t y) {
  if (featBackHit(x, y)) { sfxPlay(SFX_TAP); featBackToHub(); }
}

void tasksSwipe(int dir) { (void)dir; featBackToHub(); }
