// Wild encounters, weather and catching -- the LOGIC (wild.cpp), against the
// real Pet / Party / Bag and a real NVS store. The screen is wild_ui_test.
//
// Every "never" here has a companion proving the mechanism engaged, because a
// negative check that cannot fail is not a test (CLAUDE.md trap 3): no ghost by
// day is paired with ghosts at night, no evolved form at level 2 with evolved
// forms at level 100, and so on.
#include "Arduino.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include "items.h"
#include "wild.h"
#include "dex.h"
#include "noart.h"
#include "game_events.h"
#include <cstdio>
uint32_t g_seed=11; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX=0,g_touchY=0; volatile bool g_touchDown=false; bool wasPressed=false;
uint32_t millis(){return 0;} void FakeESP::restart(){exit(0);}
int FakeSerial::available(){return 0;} String FakeSerial::readStringUntil(char){return String("");}
void sfxPlay(uint8_t){}
static int bad=0;
static void ck(bool ok,const char*w){printf("%s  %s\n",ok?"PASS":"FAIL",w); if(!ok)bad++;}

static int evSeen=0, evWin=0, evCatch=0; static int16_t evLast=0;
static void sink(GameEv e, int16_t a){
  if (e==EV_WILD_SEEN) evSeen++;
  if (e==EV_WILD_WIN) evWin++;
  if (e==EV_WILD_CATCH) { evCatch++; evLast=a; }
}

static bool hasT(int16_t d, uint8_t t){ return DEX_TBL[d].type1==t || DEX_TBL[d].type2==t; }

// share of rolls carrying any of `ts`, in permille
static int share(uint8_t region, uint8_t wx, uint8_t tod, uint8_t lvl, const uint8_t *ts, int nt, int n=3000){
  int hit=0;
  for (int i=0;i<n;i++){
    int16_t d = wildRollSpecies(region, wx, tod, lvl, 1000u+i*7919u);
    for (int k=0;k<nt;k++) if (hasT(d,ts[k])) { hit++; break; }
  }
  return hit*1000/n;
}

#define DAY0 20400u                  // 2025-11-08, any real day
static uint32_t at(uint32_t day, int hour){ return day*86400u + hour*3600u + 120u; }

int main(){
  gameEventSubscribe(sink);

  // ---------------- weather and time ----------------
  { bool same=true; for (uint32_t d=1; d<500; d++) if (wildWeatherOf(d)!=wildWeatherOf(d)) same=false;
    ck(same, "the weather of a day is a pure function of the day");
    int cnt[WX_COUNT]={0};
    for (uint32_t d=DAY0; d<DAY0+1200; d++) cnt[wildWeatherOf(d)]++;
    bool all=true; for (int i=0;i<WX_COUNT;i++) if (cnt[i]<40) all=false;
    printf("      weather over 1200 days: clear %d sunny %d rain %d snow %d fog %d wind %d\n",
           cnt[0],cnt[1],cnt[2],cnt[3],cnt[4],cnt[5]);
    ck(all, "every weather actually happens");
    ck(cnt[WX_CLEAR] > cnt[WX_SNOW] && cnt[WX_SUNNY] > cnt[WX_FOG], "and the common ones are commoner");
    int changes=0; for (uint32_t d=DAY0; d<DAY0+60; d++) if (wildWeatherOf(d)!=wildWeatherOf(d+1)) changes++;
    ck(changes > 20, "the weather changes from day to day"); }

  // the hour is the SAME hour the pet's night uses
  { Pet p; bool agree=true;
    for (int h=0; h<24; h++){
      p.lastSeenEpoch = at(DAY0, h);
      if (wildHourOf(p.lastSeenEpoch)!=h) agree=false;
      if (p.isNightHour() && wildTimeOf(p.lastSeenEpoch)!=WT_NIGHT) agree=false;
    }
    ck(agree, "wild hour == the pet's hour, and the pet's night is always WILD night");
    ck(wildTimeOfHour(7)==WT_MORNING && wildTimeOfHour(13)==WT_DAY &&
       wildTimeOfHour(18)==WT_EVENING && wildTimeOfHour(23)==WT_NIGHT && wildTimeOfHour(3)==WT_NIGHT,
       "the four windows");
    ck(wildTimeOf(0)==WT_DAY && wildWeatherOf(0)==WX_CLEAR, "no clock: a clear midday"); }

  // ---------------- the pool shifts with the conditions ----------------
  { const uint8_t wet[]={T_WATER,T_ELECTRIC}, hot[]={T_FIRE}, ice[]={T_ICE},
                  dark[]={T_GHOST,T_DARK,T_POISON}, air[]={T_FLYING,T_DRAGON};
    int rain=share(REGION_ALL,WX_RAIN,WT_DAY,40,wet,2), sun=share(REGION_ALL,WX_SUNNY,WT_DAY,40,wet,2);
    printf("      water/electric: rain %d%%o sunny %d%%o\n", rain, sun);
    ck(rain > sun*2, "rain brings out WATER/ELECTRIC");
    int fs=share(REGION_ALL,WX_SUNNY,WT_DAY,40,hot,1), fr=share(REGION_ALL,WX_RAIN,WT_DAY,40,hot,1);
    printf("      fire: sunny %d%%o rain %d%%o\n", fs, fr);
    ck(fs > fr*2, "sun brings out FIRE");
    int is=share(REGION_ALL,WX_SNOW,WT_DAY,40,ice,1), ic=share(REGION_ALL,WX_CLEAR,WT_DAY,40,ice,1);
    ck(is > ic*2, "snow brings out ICE");
    int dn=share(REGION_ALL,WX_CLEAR,WT_NIGHT,40,dark,3), dd=share(REGION_ALL,WX_CLEAR,WT_DAY,40,dark,3);
    printf("      ghost/dark/poison: night %d%%o day %d%%o\n", dn, dd);
    ck(dn > dd*2, "night brings out GHOST/DARK/POISON");
    int ww=share(REGION_ALL,WX_WIND,WT_DAY,40,air,2), wc=share(REGION_ALL,WX_CLEAR,WT_DAY,40,air,2);
    ck(ww > wc*3/2, "wind brings out FLYING/DRAGON");
    // night-only, with its companion
    const uint8_t gh[]={T_GHOST};
    ck(share(REGION_ALL,WX_CLEAR,WT_DAY,40,gh,1)==0 && share(REGION_ALL,WX_SUNNY,WT_MORNING,40,gh,1)==0,
       "no GHOST by day");
    ck(share(REGION_ALL,WX_CLEAR,WT_NIGHT,40,gh,1)>0, "...but ghosts at night");
    ck(share(REGION_ALL,WX_FOG,WT_DAY,40,gh,1)>0, "...and in the fog"); }

  // ---------------- nothing spawns that must not ----------------
  { int badRoll=0, evoHi=0, evoLo=0, rolls=0;
    const uint8_t lv[]={2,15,40,100};
    for (uint8_t rg=0; rg<REGION_COUNT; rg++)
      for (uint8_t wx=0; wx<WX_COUNT; wx++)
        for (uint8_t t=0; t<WT_COUNT; t++)
          for (int li=0; li<4; li++)
            for (int i=0;i<12;i++){
              int16_t d = wildRollSpecies(rg,wx,t,lv[li], rg*100003u+wx*7001u+t*503u+li*37u+i);
              rolls++;
              bool ok = d>=1 && d<=DEX_COUNT && DEX_TBL[d].rarity!=R_LEGENDARIO && speciesHasArt(d)
                        && regionAvailable(regionOfDex(d))
                        && (rg==REGION_ALL || (d>=REGIONS[rg].lo && d<=REGIONS[rg].hi));
              if (ok && DEX_TBL[d].rarity==R_EVO){
                int16_t p = wildPredecessor(d);
                if (!p || lv[li] < DEX_TBL[p].evolveLevel) ok=false;
                if (lv[li]==100) evoHi++; if (lv[li]==2) evoLo++;
              }
              if (!ok){ if (badRoll<5) printf("      bad roll: region %u lvl %u -> %d\n", rg, lv[li], d); badRoll++; }
            }
    printf("      %d rolls\n", rolls);
    ck(badRoll==0, "no legendary, no art-less, no out-of-region, no under-levelled evolution");
    ck(evoHi>0, "evolved forms DO appear once the level allows (the gate engaged)");
    ck(evoLo==0, "and none at level 2");
    // exhaustive over the pool, not just sampled
    int leg=0; for (int16_t d=1; d<=DEX_COUNT; d++) if (DEX_TBL[d].rarity==R_LEGENDARIO && wildEligible(d,REGION_ALL,100)) leg++;
    ck(leg==0, "no legendary is ever eligible");
    int na=0; for (int i=0;i<NO_ART_COUNT;i++) if (wildEligible(NO_ART[i],REGION_ALL,100)) na++;
    ck(na==0, "no art-less species is ever eligible");
    ck(wildEligible(16,0,5) && wildEligible(17,0,40) && !wildEligible(17,0,10),
       "PIDGEY any time; PIDGEOTTO only from PIDGEY's evolve level"); }

  // locked regions: the pack gate, with the companion that the gate engaged
  { uint16_t keep = gRegionArt;
    gRegionArt = (1u<<0) | (1u<<2);    // Kanto + Hoenn only: Johto's pack is missing
    int johto=0, kanto=0, hoenn=0, other=0;
    for (int i=0;i<3000;i++){
      int16_t d = wildRollSpecies(REGION_ALL, i%WX_COUNT, i%WT_COUNT, 50, 77u+i);
      uint8_t r = regionOfDex(d);
      if (r==1) johto++; else if (r==0) kanto++; else if (r==2) hoenn++; else other++;
    }
    ck(johto==0 && other==0, "a locked region never spawns, even under ALL");
    ck(kanto>0 && hoenn>0, "...while the unlocked ones do");
    int fell=0; for (int i=0;i<500;i++){ int16_t d=wildRollSpecies(1,0,1,50,9u+i); if (d && regionOfDex(d)!=1) fell++; }
    ck(fell==500, "a region whose pack is gone falls back to playable ones, never to itself");
    gRegionArt = keep;
    int jo=0; for (int i=0;i<500;i++){ int16_t d=wildRollSpecies(1,0,1,50,9u+i); if (regionOfDex(d)==1) jo++; }
    ck(jo==500, "with its pack back, JOHTO spawns only Johto"); }

  // ---------------- catch chance ----------------
  // one throw carries what three used to: 1 - (1 - p)^3 over the old table
  ck(wildCatchChance(16,ITEM_POKEBALL)==83 && wildCatchChance(16,ITEM_GREATBALL)==95 &&
     wildCatchChance(16,ITEM_ULTRABALL)==95, "common: 83 / 95 / 95 (one throw = the old three)");
  { int16_t rare=0; for (int16_t d=1; d<=DEX_COUNT && !rare; d++) if (DEX_TBL[d].rarity==R_RARO) rare=d;
    ck(wildCatchChance(rare,ITEM_POKEBALL)==48 && wildCatchChance(rare,ITEM_ULTRABALL)==78, "rare: 48 / 78"); }
  ck(wildCatchChance(17,ITEM_POKEBALL)==65, "an evolved common: 65");
  // ...and it IS the old odds of three throws, for every species and ball
  { int off=0; for (int16_t d=1; d<=DEX_COUNT; d++) for (uint8_t b=ITEM_POKEBALL;b<=ITEM_ULTRABALL;b++) {
      uint8_t r=DEX_TBL[d].rarity; uint32_t base = r==R_COMUN?45 : r==R_RARO?20 : 5;
      if (r==R_EVO) continue;                     // the evolved rows are checked above
      uint32_t p=base*ITEM_TBL[b].power/10; if (p>90) p=90;
      double three = 100.0*(1.0 - (1-p/100.0)*(1-p/100.0)*(1-p/100.0));
      int want = three > WILD_CATCH_CAP ? WILD_CATCH_CAP : (int)three;
      if (abs((int)wildCatchChance(d,b) - want) > 1) off++; }
    ck(off==0, "one throw = the chance of catching it in three of the old throws"); }
  // the shakes: three for a catch, fewer the further the miss
  ck(wildShakesFor(83, 0)==WILD_SHAKES && wildShakesFor(83, 82)==WILD_SHAKES, "a catch shakes three times");
  ck(wildShakesFor(83, 83) < WILD_SHAKES, "a miss never shakes three times");
  { bool mono=true; uint8_t prev=WILD_SHAKES; for (int r=48; r<100; r++){ uint8_t k=wildShakesFor(48,(uint8_t)r); if (k>prev) mono=false; prev=k; }
    ck(mono, "the nearer the miss, the more it shakes (never more for a worse roll)"); }
  ck(wildShakesFor(48, 48)==WILD_SHAKES-1 && wildShakesFor(48, 99)==0, "a near miss shakes twice; a hopeless one bursts at once");
  { int seen[WILD_SHAKES+1]={0}; for (int r=0;r<100;r++) seen[wildShakesFor(50,(uint8_t)r)]++;
    ck(seen[0] && seen[1] && seen[2] && seen[3], "every shake count 0..3 happens"); }
  ck(wildCatchChance(16,ITEM_ORAN_BERRY)==0, "a berry is not a ball");
  { int over=0; for (int16_t d=1; d<=DEX_COUNT; d++) for (uint8_t b=ITEM_POKEBALL;b<=ITEM_ULTRABALL;b++) if (wildCatchChance(d,b)>WILD_CATCH_CAP) over++;
    ck(over==0, "never above the cap"); }
  ck(wildLevelFor(1,5)==2 && wildLevelFor(100,5)<=100, "wild level: at least 2, at most 100");
  { bool in=true; int lo=99, hi=0; for (uint32_t s=0;s<400;s++){ int l=wildLevelFor(50,s); if(l<45||l>51) in=false; if(l<lo)lo=l; if(l>hi)hi=l; }
    ck(in && lo==45 && hi==51, "the pet's level -5..+1"); }

  // ---------------- the day, against real NVS ----------------
  Pet pp; pp.begin();
  party.begin(); bag.begin(); wild.begin();
  ck(wild.st.phase==WP_NONE && wild.left()==WILD_PER_DAY && !wild.gift, "a fresh save: nothing out, 3 left, no gift yet");

  // the gift, once
  ck(wild.giveGift(bag) && bag.count(ITEM_POKEBALL)==WILD_GIFT_BALLS, "the first open gives 10 POKE BALLs");
  ck(!wild.giveGift(bag) && bag.count(ITEM_POKEBALL)==WILD_GIFT_BALLS, "...and only once");
  { Wild w; w.begin(); ck(w.gift, "the gift flag survives a reload"); w.giveGift(bag);
    ck(bag.count(ITEM_POKEBALL)==WILD_GIFT_BALLS, "a reloaded Wild does not give it again"); }

  // determinism: the same day, looked at twice from scratch, holds the same three
  int16_t first[3];
  for (int i=0;i<3;i++){ ck(wild.roll(DAY0, at(DAY0,13), 0, 30, false), "roll"); first[i]=wild.st.dex;
    int16_t again=wild.st.dex; wild.roll(DAY0, at(DAY0,13), 0, 30, false);
    if (i==0) ck(wild.st.dex==again, "rolling again while one is out does not re-roll it");
    wild.runAway(); }
  ck(wild.left()==0, "three RUNs spend the day");
  ck(!wild.roll(DAY0, at(DAY0,14), 0, 30, false) && wild.st.phase==WP_NONE, "nothing more today");
  { Wild w; w.begin(); ck(w.st.used==3 && w.st.day==DAY0, "spent encounters survive a reload"); }
  wild.st = Wild::State(); wild.save();
  bool same=true;
  for (int i=0;i<3;i++){ wild.roll(DAY0, at(DAY0,13), 0, 30, false); if (wild.st.dex!=first[i]) same=false; wild.runAway(); }
  ck(same, "the day's encounters are deterministic");
  printf("      day %u at 13h: %s %s %s\n", DAY0, DEX_TBL[first[0]].name, DEX_TBL[first[1]].name, DEX_TBL[first[2]].name);
  ck(wild.roll(DAY0+1, at(DAY0+1,13), 0, 30, false) && wild.left()==3, "a new day refills them");
  { int differ=0; for (uint32_t d=DAY0+2; d<DAY0+12; d++){ wild.roll(d, at(d,13), 0, 30, false); if (wild.st.dex!=first[0]) differ++; }
    ck(differ>=5, "and other days hold other creatures"); }
  // a creature MET yesterday leaves with the day; one BEATEN stays catchable
  wild.st = Wild::State(); wild.save();
  wild.roll(DAY0, at(DAY0,13), 0, 30, false);
  wild.roll(DAY0+1, at(DAY0+1,13), 0, 30, false);
  ck(wild.st.encDay==DAY0+1 && wild.left()==3, "yesterday's unmet creature left with the day");
  wild.fightStarted(); wild.battleEnded(true,false,99,bag);
  ck(wild.st.phase==WP_CATCH, "won: catch it");
  wild.newDay(DAY0+2);
  ck(wild.st.phase==WP_CATCH && wild.left()==3, "a beaten creature waits overnight, and the new day is full");
  wild.runAway();
  ck(wild.left()==3, "letting yesterday's go does not cost one of today's");

  // RUN and a lost fight spend; a reboot mid-fight is a loss
  wild.st = Wild::State(); wild.save();
  wild.roll(DAY0, at(DAY0,13), 0, 30, false); wild.runAway();
  ck(wild.left()==2 && wild.st.phase==WP_NONE, "RUN spends the encounter");
  wild.roll(DAY0, at(DAY0,13), 0, 30, false); wild.fightStarted(); wild.battleEnded(false,false,0,bag);
  ck(wild.left()==1 && wild.st.phase==WP_NONE, "a lost fight spends it");
  wild.roll(DAY0, at(DAY0,13), 0, 30, false); wild.fightStarted();
  { Wild w; w.begin(); ck(w.st.phase==WP_NONE && w.left()==0, "pulling the power mid-fight is a loss, not a retry"); }

  // the opened guard: a Wild nobody begin()'d cannot write
  { Wild scratch; scratch.st.used=0; scratch.st.day=12345; scratch.save(); scratch.giveGift(bag);
    Wild w; w.begin(); ck(w.st.day==DAY0 && w.gift, "a Wild nobody opened never touches NVS"); }

  // ---------------- win -> catch -> party -> box -> full ----------------
  wild.st = Wild::State(); wild.save();
  bag.add(ITEM_POKEBALL, 50);
  evSeen=evWin=evCatch=0;
  wild.roll(DAY0, at(DAY0,13), 0, 30, false);
  int16_t met = wild.st.dex; uint8_t lvl = wild.st.level;
  ck(evSeen==1, "EV_WILD_SEEN when it appears");
  wild.fightStarted();
  uint8_t oran = bag.count(ITEM_ORAN_BERRY);
  uint8_t drop = wild.battleEnded(true,false,0,bag);
  ck(evWin==1 && wild.st.phase==WP_CATCH, "EV_WILD_WIN, and the catch opens");
  ck(drop==ITEM_ORAN_BERRY && bag.count(ITEM_ORAN_BERRY)==oran+1, "a win can drop an ORAN BERRY into the bag");
  uint8_t balls = bag.count(ITEM_POKEBALL);
  uint8_t pc = party.count();
  ck(wild.throwBall(ITEM_GREATBALL,0,party,bag,pp)==WTH_NO_BALL && bag.count(ITEM_POKEBALL)==balls, "no GREAT BALL: nothing happens");
  ck(wild.throwBall(ITEM_POKEBALL,0,party,bag,pp)==WTH_PARTY, "caught, into the party");
  ck(bag.count(ITEM_POKEBALL)==balls-1, "one ball spent");
  ck(party.count()==pc+1, "the party grew by one");
  int slot=-1; for (int i=0;i<PARTY_SLOTS;i++) if (party.slots[i].dex==met) slot=i;
  ck(slot>=0 && party.slots[slot].level==lvl && party.slots[slot].nick[0]==0, "it is the creature met, at its level, unnamed");
  ck(slot>=0 && party.slots[slot].moves[0]!=0, "with moves from its learnset");
  ck(pp.isRegistered(met), "registered in the Pokedex");
  ck(evCatch==1 && evLast==met, "EV_WILD_CATCH with its dex");
  ck(wild.st.phase==WP_NONE && wild.left()==2, "the encounter is spent");
  { Party p2; p2.begin(); bool there=false; for (int i=0;i<PARTY_SLOTS;i++) if (p2.slots[i].dex==met && p2.slots[i].level==lvl) there=true;
    ck(there, "the catch survives a reload (Party::begin reads NVS)");
    Pet p3; p3.begin(); ck(p3.isRegistered(met), "...and so does its Pokedex entry"); }

  // one throw decides it: a miss and it runs away at once
  wild.roll(DAY0, at(DAY0,13), 0, 30, false); wild.fightStarted(); wild.battleEnded(true,false,99,bag);
  balls = bag.count(ITEM_POKEBALL);
  uint8_t r1=wild.throwBall(ITEM_POKEBALL,99,party,bag,pp);
  ck(r1==WTH_FLED, "a miss: it breaks free and runs away");
  ck(bag.count(ITEM_POKEBALL)==balls-1 && wild.st.phase==WP_NONE && wild.left()==1, "one ball spent and so is the encounter");
  ck(wild.throwBall(ITEM_POKEBALL,0,party,bag,pp)==WTH_NONE && bag.count(ITEM_POKEBALL)==balls-1, "no second throw at a creature that is gone");

  // party full -> box
  for (int i=0;i<PARTY_SLOTS;i++) if (party.slots[i].empty()){ PartyMon m; m.dex=19; m.level=5; party.add(m); }
  ck(party.isFull(), "party filled");
  wild.roll(DAY0, at(DAY0,13), 0, 30, false); met=wild.st.dex; wild.fightStarted(); wild.battleEnded(true,false,99,bag);
  uint8_t bc = party.boxCount();
  ck(wild.throwBall(ITEM_POKEBALL,0,party,bag,pp)==WTH_BOX && party.boxCount()==bc+1, "a full party sends the catch to the box");
  { Party p2; p2.begin(); bool there=false; for (int i=0;i<BOX_SLOTS;i++) if (p2.box[i].dex==met) there=true;
    ck(there, "the boxed catch survives a reload"); }

  // party AND box full: say so, spend nothing
  for (int i=0;i<BOX_SLOTS;i++) if (party.box[i].empty()){ PartyMon m; m.dex=19; m.level=5; party.boxAdd(m); }
  wild.newDay(DAY0+30);
  wild.roll(DAY0+30, at(DAY0+30,13), 0, 30, false); wild.fightStarted(); wild.battleEnded(true,false,99,bag);
  balls = bag.count(ITEM_POKEBALL);
  ck(wild.throwBall(ITEM_POKEBALL,0,party,bag,pp)==WTH_NO_ROOM, "party and box full: NO ROOM");
  ck(bag.count(ITEM_POKEBALL)==balls && wild.st.phase==WP_CATCH && wild.st.throws==0,
     "...and the ball is NOT consumed, nor the throw, nor the creature");

  // ---------------- shiny odds, and the charm ----------------
  { int plain=0, charm=0; const int N=20000;
    for (int i=0;i<N;i++){ wild.st=Wild::State(); wild.roll(DAY0+100+i, at(DAY0+100+i,13), REGION_ALL, 30, false); if (wild.st.shiny) plain++; }
    for (int i=0;i<N;i++){ wild.st=Wild::State(); wild.roll(DAY0+100+i, at(DAY0+100+i,13), REGION_ALL, 30, true); if (wild.st.shiny) charm++; }
    printf("      shiny in %d: plain %d (expect ~%d), charm %d\n", N, plain, N/WILD_SHINY_ODDS, charm);
    ck(plain>N/WILD_SHINY_ODDS/2 && plain<N/WILD_SHINY_ODDS*2, "about 1 in 256");
    ck(charm>plain*3/2, "the Shiny Charm roughly doubles it"); }

  printf(bad?"\n%d wild check(s) FAILED\n":"\nwild logic holds\n", bad);
  return bad?1:0;
}
