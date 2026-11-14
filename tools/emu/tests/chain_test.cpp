// Shiny chains and the "what is next" peek -- the LOGIC (wild.cpp), against the
// real Pet / Bag and a real NVS store. The screens are wildalert_ui_test and
// chain_ui_test.
//
// Every "never" has a companion proving the mechanism engaged (CLAUDE.md trap
// 3): no boost for an ineligible species is paired with the boost at night, no
// shiny bonus for other species with the bonus for the chained one.
#include "Arduino.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include "items.h"
#include "wild.h"
#include "dex.h"
#include "game_events.h"
#include <cstdio>
#include <vector>
uint32_t g_seed=13; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX=0,g_touchY=0; volatile bool g_touchDown=false; bool wasPressed=false;
uint32_t millis(){return 0;} void FakeESP::restart(){exit(0);}
int FakeSerial::available(){return 0;} String FakeSerial::readStringUntil(char){return String("");}
void sfxPlay(uint8_t){}
static int bad=0;
static void ck(bool ok,const char*w){printf("%s  %s\n",ok?"PASS":"FAIL",w); if(!ok)bad++;}

static std::vector<int> chainEv; static int shinyEv=0, seenEv=0; static int16_t shinyLast=0;
static void sink(GameEv e, int16_t a){
  if (e==EV_CHAIN) chainEv.push_back(a);
  if (e==EV_SHINY_FOUND) { shinyEv++; shinyLast=a; }
  if (e==EV_WILD_SEEN) seenEv++;
}

#define DAY0 20400u
static uint32_t at(uint32_t day, int hour){ return day*86400u + hour*3600u + 120u; }
#define PIDGEY 16
#define RATTATA 19
#define GASTLY 92

// Beat `dex` on `day` through the real battleEnded().
static void winAgainst(int16_t dex, uint32_t day){
  wild.st.day=day; wild.st.encDay=day; wild.st.dex=dex; wild.st.level=20;
  wild.st.phase=WP_FIGHT;
  wild.battleEnded(true,false,99,bag);
  wild.runAway();                       // let it go: the chain is about the win
}

static bool sameNext(const WildNext &a, const WildNext &b){
  return a.dex==b.dex && a.level==b.level && a.shiny==b.shiny && a.weather==b.weather && a.tod==b.tod;
}

int main(){
  gameEventSubscribe(sink);
  Pet pp; pp.begin(); party.begin(); bag.begin(); wild.begin();
  wild.clearAll();

  // ---------------- the shiny numerator ----------------
  ck(wildShinyNumer(0,false)==1 && wildShinyNumer(0,true)==2, "no chain: 1/256, the charm 2/256");
  ck(wildShinyNumer(3,false)==4 && wildShinyNumer(14,false)==15, "each link adds 1/256");
  ck(wildShinyNumer(15,false)==16 && wildShinyNumer(99,false)==16, "capped at 1/16");
  ck(wildShinyNumer(15,true)==32 && wildShinyNumer(4,true)==10, "the charm still doubles");

  // ---------------- the chain grows ----------------
  chainEv.clear();
  winAgainst(PIDGEY, DAY0);
  ck(wild.ch.dex==PIDGEY && wild.chainLen(DAY0)==1, "a first win starts a chain of 1");
  winAgainst(PIDGEY, DAY0);
  winAgainst(PIDGEY, DAY0);
  ck(wild.chainLen(DAY0)==3 && wild.chainDex(DAY0)==PIDGEY, "the same species again: 3");
  ck(chainEv.size()==3 && chainEv[0]==1 && chainEv[1]==2 && chainEv[2]==3, "EV_CHAIN 1, 2, 3");
  // a loss and a run do not touch it
  wild.st.day=DAY0; wild.st.encDay=DAY0; wild.st.dex=RATTATA; wild.st.phase=WP_FIGHT;
  wild.battleEnded(false,false,99,bag);
  wild.st.dex=RATTATA; wild.st.phase=WP_MEET; wild.runAway();
  ck(wild.chainLen(DAY0)==3, "losing to, or running from, another species keeps it");
  // the next day still counts
  winAgainst(PIDGEY, DAY0+1);
  ck(wild.chainLen(DAY0+1)==4, "a win the next day extends it (4)");
  ck(wild.ch.best==4, "best follows");

  // ---------------- it survives a reload ----------------
  { Wild w; w.begin(); ck(w.ch.dex==PIDGEY && w.chainLen(DAY0+1)==4 && w.ch.best==4, "the chain survives a reload"); }

  // ---------------- and it breaks ----------------
  ck(wild.chainLen(DAY0+2)==4, "alive the day after the last link");
  ck(wild.chainLen(DAY0+3)==0 && wild.chainDex(DAY0+3)==0, "a whole day with no win breaks it");
  chainEv.clear();
  winAgainst(PIDGEY, DAY0+3);
  ck(wild.chainLen(DAY0+3)==1 && chainEv.size()==1 && chainEv[0]==1, "after the break, the same species starts over at 1");
  winAgainst(PIDGEY, DAY0+3); winAgainst(PIDGEY, DAY0+3);
  ck(wild.chainLen(DAY0+3)==3, "regrown to 3");
  winAgainst(RATTATA, DAY0+3);
  ck(wild.chainDex(DAY0+3)==RATTATA && wild.chainLen(DAY0+3)==1, "beating another species restarts it on that one");
  ck(wild.ch.best==4, "and best is kept");
  // it never wraps
  wild.ch.dex=RATTATA; wild.ch.len=WILD_CHAIN_MAX; wild.ch.day=DAY0+3;
  chainEv.clear();
  winAgainst(RATTATA, DAY0+3);
  ck(wild.chainLen(DAY0+3)==WILD_CHAIN_MAX && chainEv.empty(), "at the cap it stays, and does not claim to grow");
  // garbage in NVS is sanitised
  { Wild::Chain junk; junk.dex=9999; junk.len=7; junk.day=DAY0;
    Preferences p; p.begin("tamapoke",false); p.putBytes("wchain",&junk,sizeof(junk));
    Wild w; w.begin(); ck(w.chainLen(DAY0)==0 && w.ch.dex==0, "a stored chain on an impossible dex loads as none"); }
  wild.clearAll();
  { Wild w; w.begin(); ck(w.ch.len==0 && w.ch.dex==0, "clearAll clears the chain on disk"); }

  // ---------------- the boost really changes spawn frequency ----------------
  { const int N=3000; int plain=0, boosted=0;
    for (int i=0;i<N;i++){
      if (wildRollSpecies(0, WX_CLEAR, WT_DAY, 30, 500u+i*7919u)==PIDGEY) plain++;
      if (wildRollSpeciesChained(0, WX_CLEAR, WT_DAY, 30, 500u+i*7919u, PIDGEY)==PIDGEY) boosted++;
    }
    printf("      PIDGEY in %d Kanto rolls: plain %d, chained %d\n", N, plain, boosted);
    ck(plain < N/10, "unchained, PIDGEY is one of many");
    ck(boosted > N*45/100 && boosted < N*60/100, "chained, it is about half the roll");
    // and the others keep their order among themselves
    int other=0; for (int i=0;i<N;i++){ int16_t d=wildRollSpeciesChained(0, WX_CLEAR, WT_DAY, 30, 500u+i*7919u, PIDGEY);
      if (d!=PIDGEY && !wildEligible(d,0,30)) other++; }
    ck(other==0, "the boost never lets an ineligible species in");
    // an ineligible chained species gets nothing: GASTLY by day, with the companion at night
    int gd=0, gn=0;
    for (int i=0;i<N;i++){
      if (wildRollSpeciesChained(0, WX_CLEAR, WT_DAY, 30, 9u+i*31u, GASTLY)==GASTLY) gd++;
      if (wildRollSpeciesChained(0, WX_CLEAR, WT_NIGHT, 30, 9u+i*31u, GASTLY)==GASTLY) gn++;
    }
    printf("      chained GASTLY: by day %d, at night %d\n", gd, gn);
    ck(gd==0, "a chain cannot bring out a species today's conditions forbid");
    ck(gn > N*45/100, "...but where it may spawn, the boost applies");
    // through Wild::roll itself, over days
    int r0=0, r1=0;
    for (int i=0;i<1500;i++){
      uint32_t d=DAY0+1000+i;
      wild.st=Wild::State(); wild.ch=Wild::Chain();
      wild.roll(d, at(d,13), 0, 30, false); if (wild.st.dex==PIDGEY) r0++;
      wild.st=Wild::State(); wild.ch.dex=PIDGEY; wild.ch.len=3; wild.ch.day=d-1;
      wild.roll(d, at(d,13), 0, 30, false); if (wild.st.dex==PIDGEY) r1++;
    }
    printf("      Wild::roll over 1500 days: PIDGEY %d unchained, %d chained\n", r0, r1);
    ck(r1 > r0*3 && r1 > 600, "Wild::roll uses the boost");
    // a broken chain boosts nothing
    int r2=0;
    for (int i=0;i<1500;i++){
      uint32_t d=DAY0+1000+i;
      wild.st=Wild::State(); wild.ch.dex=PIDGEY; wild.ch.len=3; wild.ch.day=d-2;
      wild.roll(d, at(d,13), 0, 30, false); if (wild.st.dex==PIDGEY) r2++;
    }
    ck(r2==r0, "a broken chain boosts nothing (identical to no chain)"); }

  // ---------------- the chain raises shiny odds (measured) ----------------
  { const int N=8000;
    int hits[3]={0,0,0}, shinies[3]={0,0,0}, otherShiny=0, otherN=0;
    const uint8_t lens[3]={1,15,15}; const bool charms[3]={false,false,true};
    shinyEv=0; int shinyRolled=0;
    for (int k=0;k<3;k++)
      for (int i=0;i<N;i++){
        uint32_t d=DAY0+5000+i;
        wild.st=Wild::State(); wild.ch.dex=PIDGEY; wild.ch.len=lens[k]; wild.ch.day=d;
        wild.roll(d, at(d,13), 0, 30, charms[k]);
        if (wild.st.shiny) shinyRolled++;
        if (wild.st.dex==PIDGEY){ hits[k]++; if (wild.st.shiny) shinies[k]++; }
        else if (k==1){ otherN++; if (wild.st.shiny) otherShiny++; }
      }
    printf("      chained PIDGEY shinies: x1 %d/%d, x15 %d/%d, x15+charm %d/%d; others under x15 %d/%d\n",
           shinies[0],hits[0],shinies[1],hits[1],shinies[2],hits[2],otherShiny,otherN);
    // expected: 2/256 -> ~0.8%, 16/256 -> 6.25%, 32/256 -> 12.5%
    ck(shinies[0]*1000/hits[0] >= 3 && shinies[0]*1000/hits[0] <= 16, "x1: about 2/256");
    ck(shinies[1]*1000/hits[1] >= 45 && shinies[1]*1000/hits[1] <= 80, "x15: about 1/16");
    ck(shinies[2]*1000/hits[2] >= 100 && shinies[2]*1000/hits[2] <= 150, "x15 with the charm: about 1/8");
    ck(otherShiny*1000/otherN <= 9, "the bonus is for the chained species only");
    ck(otherShiny > 0, "...while the others still roll their plain 1/256");
    ck(shinyEv==shinyRolled && shinyLast>0, "EV_SHINY_FOUND once per shiny that appears"); }

  // ---------------- the peek IS the roll ----------------
  { int same=0, total=0, writes=0, events=0;
    for (int i=0;i<400;i++){
      uint32_t d=DAY0+200+i/4; int hr=(i*5)%24; uint8_t rg=(i%3==0)?REGION_ALL:(uint8_t)(i%2);
      uint8_t lvl=5+(i*7)%90; bool charm=(i%5==0);
      wild.st=Wild::State(); wild.st.day=d; wild.st.used=(uint8_t)(i%3);
      if (i%2){ wild.ch.dex=PIDGEY; wild.ch.len=(uint8_t)(1+i%20); wild.ch.day=d; } else wild.ch=Wild::Chain();
      wild.save();
      WildNext n; int ev0=seenEv;
      bool have=wild.peek(d, at(d,hr), rg, lvl, charm, n);
      if (seenEv!=ev0) events++;
      { Wild w; w.begin(); if (w.st.phase!=WP_NONE || w.st.dex!=0) writes++; }
      bool rolled=wild.roll(d, at(d,hr), rg, lvl, charm);
      WildNext r; r.dex=wild.st.dex; r.level=wild.st.level; r.shiny=wild.st.shiny; r.weather=wild.st.weather; r.tod=wild.st.tod;
      total++; if (have==rolled && sameNext(n,r)) same++;
    }
    ck(same==total, "for 400 mixed days, hours, regions, levels, charms and chains: peek == roll");
    ck(writes==0 && events==0, "and the peek writes nothing and emits nothing"); }
  // a new day, as roll() would see it: yesterday's MEET has left, the day is full
  { wild.st=Wild::State(); wild.ch=Wild::Chain();
    wild.roll(DAY0, at(DAY0,13), 0, 30, false); wild.runAway(); wild.runAway();
    wild.roll(DAY0, at(DAY0,13), 0, 30, false);           // a MEET out, on DAY0
    WildNext n; bool h=wild.peek(DAY0+1, at(DAY0+1,13), 0, 30, false, n);
    ck(h && !n.out, "the next day, yesterday's unmet creature is not what is next");
    wild.roll(DAY0+1, at(DAY0+1,13), 0, 30, false);
    ck(wild.st.dex==n.dex && wild.left()==WILD_PER_DAY, "...and the roll agrees"); }
  // one out is what is next; none left is nothing
  { WildNext n; bool h=wild.peek(DAY0+1, at(DAY0+1,20), 0, 30, false, n);
    ck(h && n.out && n.dex==wild.st.dex, "while one is out, it is the next");
    wild.runAway(); wild.roll(DAY0+1, at(DAY0+1,13), 0, 30, false); wild.runAway();
    wild.roll(DAY0+1, at(DAY0+1,13), 0, 30, false); wild.runAway();
    ck(!wild.peek(DAY0+1, at(DAY0+1,13), 0, 30, false, n) && n.dex==0, "none left today: nothing is next");
    ck(wild.peek(DAY0+2, at(DAY0+2,13), 0, 30, false, n) && n.dex>0, "...until tomorrow"); }

  // ---------------- why the next one is worth an alert ----------------
  { int16_t rare=0; for (int16_t d=1; d<=DEX_COUNT && !rare; d++) if (DEX_TBL[d].rarity==R_RARO) rare=d;
    ck(wildAlertWhy(PIDGEY,false,true)==0, "a registered, common, plain PIDGEY: no alert");
    ck(wildAlertWhy(PIDGEY,false,false)==WA_NEW, "missing from the Pokedex: NEW");
    ck(wildAlertWhy(PIDGEY,true,true)==WA_SHINY, "shiny: SHINY");
    ck(wildAlertWhy(rare,false,true)==WA_RARE, "a rare base form: RARE");
    ck(wildIsRare(DEX_TBL[rare].evolvesTo) || !DEX_TBL[rare].evolvesTo, "an evolved form of a rare line is rare too");
    ck(!wildIsRare(17), "PIDGEOTTO (a common line) is not");
    ck(wildAlertWhy(GASTLY,false,true)==WA_COND, "GASTLY: only possible at night or in fog");
    ck(!wildConditional(PIDGEY), "PIDGEY can appear in any weather and window");
    ck(wildAlertWhy(0,true,false)==0, "no creature, no alert"); }

  printf(bad?"\n%d chain check(s) FAILED\n":"\nchain logic holds\n", bad);
  return bad?1:0;
}
