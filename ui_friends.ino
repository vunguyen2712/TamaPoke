// ---------- Two-board extras: gift swap and visits (feature 7). Logic: friends.cpp ----------
// Owned by this feature alone. Reached from the Adventure hub (ui_hub.ino);
// use featBegin/featRow/featRowAt/featBackPill/featBackHit/featLine from there,
// and leave with featBackToHub(). Every render path MUST end in gfx->flush().
//
// Pairing is the LAN screen's machinery, not a second copy of it: the same
// `lan` Link, the same linkNowBegin()/linkNowPoll() pump in loop(), the same
// hello (version + build fingerprint, refused loudly), the same MAC lock and
// deadlines. The Link only runs in LINK_MODE_FRIENDS, which the hello carries,
// so a board here never pairs with a board on LAN BATTLE.

static const char *const S_FR_TITLE[LANG_COUNT] = { "AMIGOS", "FRIENDS", "AMIS", "FREUNDE", "AMICI", "AMIGOS" };
static const char *const S_FR_PICK[LANG_COUNT] = { "ANFITRION O UNIRSE", "HOST OR JOIN", "HEBERGER OU REJOINDRE", "HOSTEN ODER BEITRETEN", "OSPITA O UNISCITI", "HOSPEDAR OU ENTRAR" };
static const char *const S_FR_WAIT[LANG_COUNT] = { "BUSCANDO AMIGO...", "LOOKING FOR A FRIEND", "CHERCHE UN AMI...", "SUCHE FREUND...", "CERCO UN AMICO...", "PROCURANDO AMIGO..." };
static const char *const S_FR_GONE[LANG_COUNT] = { "TU AMIGO SE FUE", "YOUR FRIEND LEFT", "TON AMI EST PARTI", "FREUND IST WEG", "L'AMICO E USCITO", "SEU AMIGO SAIU" };
static const char *const S_FR_NORADIO[LANG_COUNT] = { "SIN RADIO", "NO RADIO", "PAS DE RADIO", "KEIN FUNK", "NIENTE RADIO", "SEM RADIO" };
static const char *const S_FR_VERSION[LANG_COUNT] = { "OTRA VERSION", "DIFFERENT VERSION", "AUTRE VERSION", "ANDERE VERSION", "ALTRA VERSIONE", "OUTRA VERSAO" };
static const char *const S_FR_MODE[LANG_COUNT] = { "AMIGO EN COMBATE LAN", "FRIEND IS ON LAN", "AMI SUR COMBAT LAN", "FREUND IM LAN-KAMPF", "AMICO IN LOTTA LAN", "AMIGO NA BATALHA LAN" };
static const char *const S_FR_SAMEID[LANG_COUNT] = { "MISMO ID", "SAME ID", "MEME ID", "GLEICHE ID", "STESSO ID", "MESMO ID" };
static const char *const S_FR_GIFT[LANG_COUNT] = { "REGALO", "GIFT SWAP", "CADEAU", "GESCHENK", "REGALO", "PRESENTE" };
static const char *const S_FR_VISIT[LANG_COUNT] = { "VISITA", "VISIT", "VISITE", "BESUCH", "VISITA", "VISITA" };
static const char *const S_FR_OFFER[LANG_COUNT] = { "OFRECER", "OFFER", "OFFRIR", "ANBIETEN", "OFFRI", "OFERECER" };
static const char *const S_FR_SMALL[LANG_COUNT] = { "REGALITO", "SMALL GIFT", "PETIT CADEAU", "KLEINIGKEIT", "REGALINO", "LEMBRANCA" };
static const char *const S_FR_WAITING[LANG_COUNT] = { "ESPERANDO AL AMIGO", "WAITING FOR FRIEND", "ATTENTE DE L'AMI", "WARTE AUF FREUND", "ATTENDO L'AMICO", "ESPERANDO O AMIGO" };
static const char *const S_FR_SWAPPING[LANG_COUNT] = { "INTERCAMBIANDO...", "SWAPPING...", "ECHANGE...", "TAUSCHE...", "SCAMBIO...", "TROCANDO..." };
static const char *const S_FR_GOT[LANG_COUNT] = { "RECIBES %s", "GOT %s!", "RECU %s", "ERHALTEN %s", "RICEVI %s", "GANHOU %s" };
static const char *const S_FR_USED[LANG_COUNT] = { "YA HUBO REGALO HOY", "SWAPPED TODAY", "DEJA FAIT AUJOURD'HUI", "HEUTE GETAUSCHT", "GIA FATTO OGGI", "JA TROCOU HOJE" };
static const char *const S_FR_TRANSIT[LANG_COUNT] = { "REGALO EN CAMINO", "GIFT IN TRANSIT", "CADEAU EN ROUTE", "GESCHENK UNTERWEGS", "REGALO IN VIAGGIO", "PRESENTE A CAMINHO" };
static const char *const S_FR_BAD[LANG_COUNT] = { "REGALO NO VALIDO", "BAD GIFT REFUSED", "CADEAU REFUSE", "UNGUELTIGES GESCHENK", "REGALO RIFIUTATO", "PRESENTE RECUSADO" };
static const char *const S_FR_FULL[LANG_COUNT] = { "MOCHILA LLENA", "BAG FULL", "SAC PLEIN", "BEUTEL VOLL", "BORSA PIENA", "MOCHILA CHEIA" };
static const char *const S_FR_CALLED[LANG_COUNT] = { "CANCELADO", "SWAP CALLED OFF", "ECHANGE ANNULE", "TAUSCH ABGEBROCHEN", "SCAMBIO ANNULLATO", "TROCA CANCELADA" };
static const char *const S_FR_OFFERS[LANG_COUNT] = { "AMIGO: %s", "FRIEND: %s", "AMI: %s", "FREUND: %s", "AMICO: %s", "AMIGO: %s" };
static const char *const S_FR_PEER_USED[LANG_COUNT] = { "AMIGO YA TUVO REGALO", "FRIEND SWAPPED TODAY", "AMI DEJA ECHANGE", "FREUND HAT GETAUSCHT", "L'AMICO HA GIA FATTO", "AMIGO JA TROCOU" };
static const char *const S_FR_ARRIVED[LANG_COUNT] = { "LLEGO REGALO ANTERIOR", "OLD GIFT ARRIVED", "ANCIEN CADEAU ARRIVE", "ALTES GESCHENK DA", "ARRIVATO REGALO", "PRESENTE ANTIGO CHEGOU" };
static const char *const S_FR_RETURNED[LANG_COUNT] = { "REGALO DEVUELTO", "OLD GIFT RETURNED", "CADEAU RENDU", "GESCHENK ZURUECK", "REGALO RESO", "PRESENTE DEVOLVIDO" };
static const char *const S_FR_BONUS[LANG_COUNT] = { "ALEGRIA +%u VINC +%u", "JOY +%u BOND +%u", "JOIE +%u LIEN +%u", "FREUDE +%u BAND +%u", "GIOIA +%u LEGAME +%u", "ALEGRIA +%u LACO +%u" };
static const char *const S_FR_NOBONUS[LANG_COUNT] = { "YA VISITO HOY", "VISITED TODAY", "DEJA VISITE", "HEUTE BESUCHT", "GIA VISITATO", "JA VISITOU HOJE" };
static const char *const S_FR_EGG[LANG_COUNT] = { "FALTAN 2 ECLOSIONADOS", "NEEDS 2 HATCHED PETS", "IL FAUT 2 ECLOS", "BRAUCHT 2 GESCHLUEPFTE", "SERVONO 2 NATI", "PRECISA 2 CHOCADOS" };
static const char *const S_FR_LATER[LANG_COUNT] = { "SE RESUELVE AL VOLVER", "SETTLES NEXT MEETING", "REGLE A LA PROCHAINE", "KLAERT SICH SPAETER", "SI RISOLVE DOPO", "RESOLVE NA PROXIMA" };

// The tabs are concatenated alphabetically, so ui_hub.ino's layout macros are
// not defined yet here. Same definition, token for token: if the hub's ever
// changes, the compiler flags the redefinition instead of the rows drifting.
#ifndef FEAT_ROW_Y
#define FEAT_ROW_Y(i) (88 + (i) * 50)
#endif

static bool frPicking = false;   // the gift item chooser is up
static uint8_t frPickIdx = 0;

static uint32_t frDay() { return pet.currentDay(); }

void friendsHubLabel(char *out, size_t n, const char *name) {
  // A swap still in doubt is worth a mark: it settles the next time the two
  // boards meet, and until then this board cannot swap with anyone else.
  if (friends.rec.state != FR_NONE) snprintf(out, n, "%s *", name);
  else snprintf(out, n, "%s", name);
}
bool friendsHubEnabled() { return true; }
void friendsOpened() { frPicking = false; frPickIdx = 0; }

// Leaving on purpose: goodbye to the peer, the radio off. Only ever touches the
// link if a FRIENDS session owns it -- a LAN battle is none of our business.
void friendsLeave() {
  frPicking = false;
  if (!friends.active) return;
  if (lan.live()) lan.sendBye();
  linkNowEnd();
  lan.state = LINK_OFF;
  friends.sessionEnd();
}

// Once a loop, from loop(): the link itself is pumped just above it.
void friendsLoop(uint32_t now) {
  if (!friends.active) return;
  friends.poll(lan, pet, bag, frDay(), now);
  // A peer that left, went quiet or was refused: the radio costs current, so
  // it goes off now. The session stays "active" so the screen can say why.
  if (!lan.live() && linkNowUp()) linkNowEnd();
}

static void friendsConnect(bool host) {
  lan.begin(host, pet.trainerName);
  snprintf(lan.peerName, sizeof(lan.peerName), "%s", pet.trainerName);
  friends.sessionStart(lan, pet);     // mode + our one-creature card, before the radio
  if (!linkNowBegin(&lan)) {          // no radio: say so rather than hang
    lan.state = LINK_REFUSED;
    lan.refusedWhy = LINK_WHY_RADIO;
    return;
  }
  lan.start();
}

static bool frSessionOn() {
  return friends.active && lan.mode == LINK_MODE_FRIENDS && lan.state != LINK_OFF;
}

static const char *frItemName(uint8_t it) {
  return friendsGiftable(it) ? ITEM_TBL[it].name : "?";
}

// A creature centred on (cx, cy): its thumbnail when the pack is here,
// otherwise its dex number in a ring -- the graceful no-art path.
static void frDrawMon(int16_t dex, int cx, int cy) {
  if (dex < 1 || dex > DEX_COUNT) return;
  const uint8_t *th = nullptr;
  if (speciesHasArt(dex) && regionAvailable(regionOfDex(dex))) th = thumbs.get(dex);
  if (th) { drawThumb(th, cx - 40, cy - 40, 2, false); return; }
  gfx->drawCircle(cx, cy, 30, UI_INK);
  char b[8];
  snprintf(b, sizeof(b), "%03d", dex);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(2);
  gfx->setCursor(cx - 18, cy - 8);
  gfx->print(b);
}

static void frSmallText(int cx, int y, const char *t, uint16_t col) {
  gfx->setTextColor(col);
  gfx->setTextSize(2);
  gfx->setCursor(cx - (int)strlen(t) * 6, y);
  gfx->print(t);
}

static void renderFriendsVisit() {
  featBegin(TL(S_FR_VISIT));
  int16_t me = pet.isEgg() ? 0 : pet.speciesId;
  frDrawMon(me, CX - 80, 170);
  frDrawMon(friends.peerMon.dex, CX + 80, 170);
  char b[32];
  snprintf(b, sizeof(b), "%s%s", pet.nick[0] ? pet.nick : DEX_TBL[me].name, pet.shiny ? "*" : "");
  frSmallText(CX - 80, 230, b, UI_INK);
  snprintf(b, sizeof(b), "LV %u", pet.level());
  frSmallText(CX - 80, 252, b, UI_TRACK);
  snprintf(b, sizeof(b), "%s%s", friends.peerMon.name[0] ? friends.peerMon.name
                                                         : DEX_TBL[friends.peerMon.dex].name,
           friends.peerMon.shiny ? "*" : "");
  frSmallText(CX + 80, 230, b, UI_INK);
  snprintf(b, sizeof(b), "LV %u", friends.peerMon.level);
  frSmallText(CX + 80, 252, b, UI_TRACK);
  if (friends.grantedVnum == friends.vnum) {
    if (friends.visitBonus) snprintf(b, sizeof(b), TL(S_FR_BONUS), FRIEND_VISIT_JOY, friends.visitBond);
    else snprintf(b, sizeof(b), "%s", TL(S_FR_NOBONUS));
    featLine(300, b, UI_BAR_OK);
  } else {
    featLine(300, TL(S_FR_WAITING), UI_TRACK);
  }
  featBackPill();
  gfx->flush();
}

// The line under the GIFT row: where the swap stands, in one phrase.
static void frGiftStatus(char *b, size_t n, uint16_t &col) {
  col = UI_TRACK;
  if (friends.settled) {
    snprintf(b, n, "%s", friends.settledGot ? TL(S_FR_ARRIVED) : TL(S_FR_RETURNED));
    col = UI_BAR_OK;
    if (friends.gph == FG_IDLE) return;
  }
  switch (friends.gph) {
    case FG_OFFER: snprintf(b, n, "%s", TL(S_FR_WAITING)); return;
    case FG_PREPARED: case FG_COMMITTED: snprintf(b, n, "%s", TL(S_FR_SWAPPING)); return;
    case FG_DONE:
      snprintf(b, n, TL(S_FR_GOT), frItemName(friends.gotItem));
      col = UI_BAR_OK;
      return;
    case FG_NO:
      col = UI_BAR_BAD;
      snprintf(b, n, "%s", friends.gNo == FN_BAD_ITEM ? TL(S_FR_BAD)
                         : friends.gNo == FN_FULL ? TL(S_FR_FULL) : TL(S_FR_CALLED));
      return;
    default: break;
  }
  if (friends.settled) return;
  switch (friends.giftBlock(frDay())) {
    case FN_DAY_USED: snprintf(b, n, "%s", TL(S_FR_USED)); return;
    case FN_PENDING: snprintf(b, n, "%s", TL(S_FR_TRANSIT)); col = UI_BAR_BAD; return;
    case FN_SAME_ID: snprintf(b, n, "%s", TL(S_FR_SAMEID)); col = UI_BAR_BAD; return;
    default: break;
  }
  uint8_t po = friends.peerOffer();
  if (po) snprintf(b, n, TL(S_FR_OFFERS), frItemName(po));
  else if (friends.havePeer && !friends.peerCanGift) snprintf(b, n, "%s", TL(S_FR_PEER_USED));
  else b[0] = 0;
}

static void renderFriendsPick() {
  featBegin(TL(S_FR_GIFT));
  uint8_t list[ITEM_COUNT];
  uint8_t n = friendsGiftList(bag, list, ITEM_COUNT);
  char b[32];
  if (n) {
    if (frPickIdx >= n) frPickIdx = 0;
    snprintf(b, sizeof(b), "< %s x%u >", ITEM_TBL[list[frPickIdx]].name,
             (unsigned)bag.count(list[frPickIdx]));
  } else {
    snprintf(b, sizeof(b), "%s", TL(S_FR_SMALL));   // an empty bag gives a small gift
  }
  featRow(1, b, false);
  featRow(2, TL(S_FR_OFFER), false);
  featBackPill();
  gfx->flush();
}

void renderFriends() {
  uint32_t now = millis();
  bool on = frSessionOn();
  if (on && lan.state == LINK_READY && friends.visiting(now)) { renderFriendsVisit(); return; }
  if (on && lan.state == LINK_READY && frPicking) { renderFriendsPick(); return; }
  featBegin(TL(S_FR_TITLE));

  if (!on || lan.state == LINK_REFUSED || lan.state == LINK_LOST) {
    const char *msg = TL(S_FR_PICK);
    uint16_t col = UI_TRACK;
    if (on && lan.state == LINK_REFUSED) {
      col = UI_BAR_BAD;
      switch (lan.refusedWhy) {
        case LINK_WHY_RADIO: msg = TL(S_FR_NORADIO); break;
        case LINK_WHY_MODE: msg = TL(S_FR_MODE); break;
        case LINK_WHY_SAMEID: msg = TL(S_FR_SAMEID); break;
        default: msg = TL(S_FR_VERSION); break;    // proto or build: update both
      }
    } else if (on && lan.state == LINK_LOST) {
      col = UI_BAR_BAD;
      msg = TL(S_FR_GONE);
    }
    featLine(76, msg, col);
    // A swap cut off halfway says what happens next rather than nothing.
    if (on && friends.gph == FG_PREPARED) featLine(100, TL(S_FR_LATER), UI_TRACK);
    featRow(1, T(S_LAN_HOST), false);
    featRow(2, T(S_LAN_JOIN), false);
    if (friends.rec.state != FR_NONE) featLine(FEAT_ROW_Y(4), TL(S_FR_TRANSIT), UI_TRACK);
    featBackPill();
    gfx->flush();
    return;
  }

  if (lan.state != LINK_READY) {          // pairing
    featLine(76, TL(S_FR_WAIT), UI_TRACK);
    featBackPill();
    gfx->flush();
    return;
  }

  // Paired: who, their creature, and the two things to do together.
  char b[40];
  featLine(70, lan.peerName[0] ? lan.peerName : "?", UI_INK);
  if (friends.peerMon.valid) {
    snprintf(b, sizeof(b), "%s LV %u%s", DEX_TBL[friends.peerMon.dex].name,
             friends.peerMon.level, friends.peerMon.shiny ? " *" : "");
    featLine(FEAT_ROW_Y(0) + 4, b, UI_TRACK);
  }
  bool giftDim = !(friends.gph == FG_IDLE && friends.giftBlock(frDay()) == FN_NONE &&
                   friends.havePeer);
  featRow(1, TL(S_FR_GIFT), giftDim);
  uint16_t col;
  frGiftStatus(b, sizeof(b), col);
  if (b[0]) featLine(FEAT_ROW_Y(2) + 2, b, col);
  featRow(3, TL(S_FR_VISIT), !friends.canMeet());
  if (!friends.canMeet() && friends.havePeer) featLine(FEAT_ROW_Y(4) + 2, TL(S_FR_EGG), UI_TRACK);
  else if (friends.havePeer && !friends.visitBonusDue(frDay()))
    featLine(FEAT_ROW_Y(4) + 2, TL(S_FR_NOBONUS), UI_TRACK);
  featBackPill();
  gfx->flush();
}

void friendsTap(int16_t x, int16_t y) {
  bool on = frSessionOn();
  if (on && lan.state == LINK_READY && friends.visiting(millis())) {
    if (featBackHit(x, y)) { sfxPlay(SFX_TAP); friends.visitAt = 0; }   // done looking
    return;
  }
  if (on && lan.state == LINK_READY && frPicking) {
    if (featBackHit(x, y)) { sfxPlay(SFX_TAP); frPicking = false; return; }
    uint8_t list[ITEM_COUNT];
    uint8_t n = friendsGiftList(bag, list, ITEM_COUNT);
    int r = featRowAt(x, y, 3);
    if (r == 1 && n) { sfxPlay(SFX_TAP); frPickIdx = (uint8_t)((frPickIdx + 1) % n); return; }
    if (r == 2) {
      uint8_t it = n ? list[frPickIdx < n ? frPickIdx : 0] : 0;
      bool ok = friends.offerGift(lan, bag, it, frDay());
      sfxPlay(ok ? SFX_TAP : SFX_DENY);
      frPicking = false;
    }
    return;
  }
  if (featBackHit(x, y)) { sfxPlay(SFX_TAP); friendsLeave(); featBackToHub(); return; }
  int r = featRowAt(x, y, 4);
  if (!on || lan.state == LINK_REFUSED || lan.state == LINK_LOST) {
    if (r == 1 || r == 2) {
      sfxPlay(SFX_TAP);
      friendsLeave();               // a refused / lost session is closed first
      friendsConnect(r == 1);
    }
    return;
  }
  if (lan.state != LINK_READY) return;
  if (r == 1) {
    if (friends.gph == FG_IDLE && friends.havePeer &&
        friends.giftBlock(frDay()) == FN_NONE) {
      sfxPlay(SFX_TAP);
      frPicking = true;
      frPickIdx = 0;
    } else {
      sfxPlay(SFX_DENY);
    }
  } else if (r == 3) {
    sfxPlay(friends.startVisit(lan, pet, millis()) ? SFX_TAP : SFX_DENY);
  }
}

// Not paged: a horizontal swipe leaves, like every unpaged feature screen.
void friendsSwipe(int dir) {
  (void)dir;
  if (frPicking) { frPicking = false; return; }
  friendsLeave();
  featBackToHub();
}
