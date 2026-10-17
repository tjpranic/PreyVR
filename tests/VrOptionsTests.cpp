#include "preyvr/VrOptions.h"
#include "preyvr/PsychoscopeGesture.h"
#include "TestSupport.h"
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <limits>
using namespace preyvr;
using namespace preyvr::options;
using preyvr::test::Check;
Input Base(){Input i;i.valid=i.modal=true;i.dt=.01f;i.epoch=1;return i;}
void Ready(Menu& m,Input& i){m.Update(i);m.Update(i);}
void Open(Menu& m,Input& i){Ready(m,i);m.RequestOpen();m.Update(i);m.Update(i);Check(m.Open()&&m.Armed(),"open on modal");}
bool Has(std::span<const int> entries,int e){return std::find(entries.begin(),entries.end(),e)!=entries.end();}
void MenuChecks(){
 Input i=Base();Menu m;Ready(m,i);
 i.start=true;m.Update(i);Check(!m.PauseTap(),"pause deferred until tap or hold");
 i.start=false;m.Update(i);Check(m.PauseTap()&&!m.Open(),"short tap goes to native pause once");
 m.Update(i);Check(!m.PauseTap(),"no repeated pause");
 i.start=true;for(int n=0;n<70;++n)m.Update(i);
 Check(m.Open()&&m.OwnsInput()&&!m.PauseTap()&&!m.Armed(),"long hold on a game menu opens options over it, unarmed while held");
 i.start=false;i.accept=true;m.Update(i);Check(!m.Armed(),"a press carried from the hold does not arm it");
 i.accept=false;m.Update(i);Check(m.Armed()&&m.OwnsInput(),"at rest it arms, keeping the controls");
 // Only B acts on the state machine; the panel (VrOptionsUi) takes the rest.
 i.accept=i.next=i.previous=i.trigger=true;i.x=1;i.y=-1;m.Update(i);
 Check(m.Open()&&m.Armed(),"accept, grips, stick and trigger leave it open");
 i.accept=i.next=i.previous=i.trigger=false;i.x=i.y=0;
 i.cancel=true;m.Update(i);Check(!m.Open()&&m.OwnsInput(),"closing press remains quarantined");
 m.Update(i);Check(m.OwnsInput(),"held close never leaks");
 i.cancel=false;m.Update(i);Check(!m.OwnsInput(),"neutral gives input back");
 // In play the same long hold opens it over the running game: no pause.
 m={};i=Base();i.modal=false;Ready(m,i);i.start=true;
 int pauses=0;for(int n=0;n<70;++n){m.Update(i);pauses+=m.PauseTap();}
 Check(pauses==0&&m.Open()&&!m.OverMenu()&&m.OwnsInput(),"long hold from play opens over the game, posting no pause");
 i.start=false;m.Update(i);Check(!m.PauseTap()&&m.Open(),"letting the hold go posts no pause either");
 i.modal=true;m.Update(i);Check(!m.Open(),"a game menu opening under it closes it");
 m={};i=Base();i.modal=false;Ready(m,i);m.RequestOpen();m.Update(i);
 Check(m.Open()&&!m.OverMenu(),"a request (F10, vr.options 1) opens it over the game too");
 m={};i=Base();i.modal=false;Ready(m,i);i.start=true;m.Update(i);i.start=false;m.Update(i);
 Check(m.PauseTap()&&!m.Open(),"a short tap in play is still the game's pause");
 // **Menu with both grips is the recenter chord**, in either order: no pause
 // tap on its release and no VR Options however long it is held.
 m={};i=Base();i.modal=false;Ready(m,i);i.chord=true;i.start=true;
 for(int n=0;n<70;++n){m.Update(i);Check(!m.PauseTap()&&!m.Open(),"grips then Menu: the chord neither pauses nor opens");}
 i.start=false;m.Update(i);Check(!m.PauseTap(),"nor does its release pause");
 i.chord=false;m.Update(i);i.start=true;m.Update(i);i.chord=true;m.Update(i);i.chord=false;
 for(int n=0;n<70;++n)m.Update(i);
 Check(!m.Open(),"Menu then both grips: the chord, so the hold never opens it");
 i.start=false;m.Update(i);Check(!m.PauseTap(),"and its release is no pause");
 i.start=true;m.Update(i);i.start=false;m.Update(i);
 Check(m.PauseTap(),"the next tap alone is the game's pause again");
 m={};i=Base();Open(m,i);Check(m.OverMenu(),"opened in a game menu, it is over that menu");
 m={};i=Base();Open(m,i);i.modal=false;m.Update(i);Check(!m.Open(),"native modal ends closes overlay");
 m={};i=Base();Open(m,i);i.valid=false;m.Update(i);Check(!m.Open(),"tracking loss closes");
 i.valid=true;i.accept=true;m.Update(i);Check(!m.Open(),"regained held input cannot open");
 m={};i=Base();Open(m,i);i.epoch++;m.Update(i);Check(!m.Open(),"session transition closes");
 m={};i=Base();Open(m,i);i.dt=NAN;m.Update(i);Check(!m.Open(),"a nonfinite frame closes");
 // A trigger held while it opens (the laser already on the panel) must not click.
 m={};i=Base();Ready(m,i);
 i.trigger=true;m.RequestOpen();m.Update(i);
 Check(m.Open()&&!m.Armed(),"a press held while opening does not arm it");
 m.Update(i);Check(!m.Armed(),"nor while it stays held");
 i.trigger=false;i.x=.5f;m.Update(i);Check(!m.Armed(),"a deflected stick is not at rest");
 i.x=.1f;m.Update(i);Check(m.Armed()&&m.OwnsInput(),"inside the deadzone is: it arms and keeps input");
 i.x=0;i.cancel=true;m.Update(i);Check(!m.Open()&&m.OwnsInput(),"B closes it, quarantined");
 i.cancel=false;i.trigger=true;m.Update(i);Check(m.OwnsInput(),"a trigger pressed before rest after closing never leaks");
 i.trigger=false;m.Update(i);Check(!m.OwnsInput(),"neutral gives input back");
}
void SettingsChecks(){
 auto v=Defaults(),parsed=v;
 v[Psychoscope]=1;v[GripToggle]=1;v[UiMargin]=87;
 Check(Parse(Serialize(v),parsed)&&parsed==v,"settings roundtrip");
 const auto original=parsed;
 for(auto s:{"version=2\n","version=1\nturn=10\n","version=1\npsychoscope=2\n","version=1\nui_scale=500\n","version=1\nx=3\n","version=1\nturn=45oops\n","version=1\nturn=45\nturn=30\n"}){
  Check(!Parse(s,parsed)&&parsed==original,"invalid file has no partial effects");
 }
 // Turning: smooth, or any step from 15 to 90 -- a command can set 75, which no
 // menu choice is -- while 1..14 is no angle at all.
 Check(Valid(Turn,0)&&!Valid(Turn,1)&&!Valid(Turn,14)&&Valid(Turn,15)&&Valid(Turn,75)&&Valid(Turn,90)&&!Valid(Turn,91),
       "turning takes smooth or a step of 15..90");
 Check(KindOf(Turn)==Kind::choice&&Choices(Turn)==std::vector<int>{0,15,30,45,60,90},"the menu offers the six turn choices");
 Check(Display(Turn,0)==L"SMOOTH"&&Display(Turn,75)==L"75 DEGREES","a command's angle reads as itself");
 Check(Defaults()[Psychoscope]==0,"gesture opt in");
 Check(Defaults()[Holsters]==0&&Defaults()[Wrist]==1,"holsters opt in; the wrist hologram is on (2026-10-07)");
 Check(Parse("version=1\nturn=45\n",parsed)&&parsed[Holsters]==0&&parsed[WristSize]==100,"old settings upgrade safely");
 Check(Defaults()[PsiTarget]==0&&Defaults()[Medkit]==0,"abilities preserve original behavior by default");
 Check(Valid(PsiTarget,2)&&!Valid(PsiTarget,3)&&!Valid(Medkit,2),"ability ranges");
 Check(Defaults()[DebugOverlay]==0&&Defaults()[MuzzleAim]==0,"the debug overlay and shots from the muzzle are opt in");
 Check(Defaults()[AimLasers]==1&&KindOf(AimLasers)==Kind::toggle,"the aim lasers are on by default, on/off");
 {Present marked{};
  Check(Parse("version=1\naim_lasers=0\n",parsed,&marked)&&parsed[AimLasers]==0&&marked[AimLasers],"aim lasers persist as off");
  Check(Parse("version=1\ndebug_overlay=0\n",parsed,&marked)&&parsed[AimLasers]==1&&!marked[AimLasers],
        "a file saved before the lasers, debug overlay off, leaves them to VR start");}
 Check(Parse("version=1\nmuzzle_aim=1\n",parsed)&&parsed[MuzzleAim]==1&&!Valid(MuzzleAim,2),"muzzle aim persists as on/off");
 Check(Defaults()[BodyFollow]==2,"body following turns and moves by default");
 Check(Parse("version=1\nbody_follow=1\n",parsed)&&parsed[BodyFollow]==1&&Valid(BodyFollow,0)&&!Valid(BodyFollow,3),
       "body following persists as off, turn only, turn and move");
 Check(Display(BodyFollow,0)==L"OFF"&&Display(BodyFollow,1)==L"TURN ONLY"&&Display(BodyFollow,2)==L"TURN AND MOVE",
       "body following reads as what it does");
 Check(Choices(BodyFollow)==std::vector<int>{0,1,2},"body following offers its three settings");
 Check(Parse("version=1\nturn=45\n",parsed)&&parsed[BodyFollow]==2,"settings saved before body following default it on");
 Check(Parse("version=1\ndebug_overlay=1\n",parsed)&&parsed[DebugOverlay]==1&&!Valid(DebugOverlay,2),"debug overlay persists as on/off");
 // The scene reticle toggle is retired: a file saved with it still loads, its
 // other settings intact, and the world reticle takes its default.
 Present seen{};
 Check(Parse("version=1\nscene_reticle=0\nui_scale=150\n",parsed,&seen)&&parsed[UiScale]==150&&
       parsed[ReticleWorld]==1&&!seen[ReticleWorld],"a retired key is skipped, not a reason to lose the file");
 Check(Serialize(parsed).find("scene_reticle")==std::string::npos,"and the next save leaves it out");
 Check(!Parse("version=1\nscene_reticle=x\n",parsed),"a retired key still needs a number");
 Check(Defaults()[ReticleSize]==100&&Valid(ReticleSize,50)&&Valid(ReticleSize,200)&&!Valid(ReticleSize,40),
       "the reticle's size, 50..200 percent");
}
void KindChecks(){
 // Each setting is edited as what its Spec says it is.
 Check(KindOf(TwoHand)==Kind::toggle&&KindOf(DebugOverlay)==Kind::toggle&&KindOf(Holsters)==Kind::toggle,
       "a bare 0..1 is on/off");
 Check(KindOf(HeadRelative)==Kind::choice&&KindOf(PointerHand)==Kind::choice&&KindOf(HudLayer)==Kind::choice,
       "named values are a choice, two of them included");
 Check(KindOf(UiScale)==Kind::range&&KindOf(UiCurve)==Kind::range&&KindOf(UiOffsetRight)==Kind::range&&
       KindOf(HapticStrength)==Kind::range,"the rest are ranges, those with a named zero included");
 Check(Choices(PointerHand)==std::vector<int>{0,1,2}&&Choices(UiScale).empty()&&Choices(TwoHand).empty(),
       "only a choice offers a list");
 for(unsigned id=0;id<Count;++id){
  const auto values=Choices(id);
  for(const int value:values)Check(Valid(id,value),"every choice is valid");
  Check((KindOf(id)==Kind::choice)==(values.size()>=2),"a choice, and only a choice, offers two or more values");
  if(!Describe(id).names.empty())for(const int value:values)Check(Display(id,value)!=std::to_wstring(value),"every named value reads as its name");
 }
 Check(Display(TwoHand,1)==L"ON"&&Display(TwoHand,0)==L"OFF","on/off");
 Check(Display(UiCurve,0)==L"FLAT"&&Display(UiCurve,35)==L"35 DEGREES","a named zero, otherwise the unit");
 Check(Display(ReticleFallback,10000)==L"10.0 M"&&Display(ReticleFallback,12500)==L"12.5 M","millimetres read as metres");
 Check(Display(UiScale,130)==L"130%"&&Display(SwingSpeed,120)==L"120 CM/S","percent and speed");
 Check(Display(UiOffsetUp,-10)==L"10 DEGREES DOWN"&&Display(UiOffsetRight,25)==L"25 DEGREES RIGHT","directions name the side");
 Check(Display(PointerHand,7)==L"7","a value without a name reads as its number");
}
void LayoutChecks(){
 // The panel's tabs (MenuEntries): any number of rows, every setting on
 // exactly one tab, each button once, the last tab EXPERIMENTAL.
 Check(MenuPages()==9&&std::wstring(MenuPageName(8))==L"EXPERIMENTAL","nine tabs, the last experimental");
 Check(std::wstring(MenuPageName(ControlsPage()))==L"CONTROLS"&&MenuEntries(ControlsPage()).empty(),
       "the Controls tab holds no setting: the panel lists the bindings there");
 std::array<int,Count> seen{};int recenter=0,clear=0,calibrate=0;
 for(unsigned p=0;p<MenuPages();++p)for(const int e:MenuEntries(p)){
  if(e==RecenterView)++recenter;else if(e==ClearHolsterSlots)++clear;else if(e==CalibratePosture)++calibrate;
  else{Check(e>=0&&e<static_cast<int>(Count),"entries are settings or buttons");++seen[e];}
 }
 for(unsigned i=0;i<Count;++i)Check(seen[i]==1,"every setting appears on exactly one tab");
 Check(recenter==1&&clear==1&&calibrate==1,"each button appears once");
 Check(MenuEntries(MenuPages()).empty(),"past the last tab there is nothing");
 for(const int e:{CarryHand,GrenadeDeploy,LongReach,MuzzleAim,PhysicalMelee,SwingSpeed,PhysicalContacts,
                  ContactStrength,InventoryLayer,InventoryStereo,InventoryDepth})
  Check(Has(MenuEntries(MenuPages()-1),e),"features still being proven, and their tuning, on the experimental tab");
 Check(MenuEntries(0)[0]==Turn&&Has(MenuEntries(0),BodyFollow)&&Has(MenuEntries(0),BodyGlide),"comfort: turning first, body following");
 for(const int e:{UiDistance,UiOffsetRight,UiOffsetUp,UiText,PointerHand,HudLayer,ReticleWorld,ReticleSize,ReticleFallback})
  Check(Has(MenuEntries(2),e),"menu placement, the HUD and the reticle on the interface tab");
 Check(Describe(ReticleWorld).initial==1&&Display(ReticleWorld,1)==L"IN THE WORLD"&&Display(ReticleWorld,0)==L"ON THE HUD",
       "the reticle sits in the world by default");
 Check(std::wstring(MenuPageName(1))==L"HANDS"&&MenuEntries(1)[0]==AimLasers,
       "hands: the aim lasers first, apart from the interface tab's menu laser");
 Check(MenuEntries(3)[0]==Holsters&&MenuEntries(3)[1]==ClearHolsterSlots&&MenuEntries(3)[2]==Wrist,
       "equipment: holsters, clearing them, then the wrist");
 Check(MenuEntries(6).size()==3&&MenuEntries(6)[0]==CalibratePosture&&MenuEntries(6)[1]==RecenterView&&
       MenuEntries(6)[2]==DebugOverlay,"setup: its two buttons and the debug overlay");
 for(unsigned i=0;i<Count;++i){
  const Spec& s=Describe(i);
  Check(Valid(i,s.initial)&&s.min<s.max&&s.step>0,"each spec's default is valid and it can step");
  Check(std::wstring_view(s.label).find(L"EXPERIMENTAL")==std::wstring_view::npos,"the tab says experimental, not the label");
  for(unsigned j=0;j<i;++j)Check(std::string_view(Describe(j).key)!=s.key,"keys are unique");
 }
}
void PresenceChecks(){
 // Only settings chosen in the menu, or read from the file, are written back:
 // a tuner command's session value never reaches the file by riding along.
 Present present{};present[DebugOverlay]=present[UiDistance]=true;
 auto v=Defaults();v[UiDistance]=250;v[UiScale]=150;
 const auto text=Serialize(v,&present);
 Check(text=="version=1\ndebug_overlay=0\nui_distance=250\n","only present settings are written, explicit defaults included");
 Present back{};Values parsed{};
 Check(Parse(text,parsed,&back)&&back==present&&parsed[UiDistance]==250&&parsed[UiScale]==Defaults()[UiScale],
       "presence roundtrips; the rest read as defaults");
 Check(Parse("version=1\n",parsed,&back)&&back==Present{},"a file of no settings holds none");
 back[Turn]=true;
 Check(!Parse("version=1\nturn=7\n",parsed,&back)&&back[Turn],"a refused file leaves presence as it was");
 Check(Serialize(Defaults()).find("inventory_depth=25\n")!=std::string::npos,"without presence, every setting is written");
 Check(Parse("version=1\nui_offset_right=-90\nui_offset_up=45\nui_distance=400\nbody_glide=0\n",parsed)&&
       parsed[UiOffsetRight]==-90&&parsed[UiOffsetUp]==45&&parsed[UiDistance]==400&&parsed[BodyGlide]==0,
       "new settings persist at their limits");
 for(auto s:{"version=1\nui_offset_up=46\n","version=1\nui_distance=99\n","version=1\npointer_hand=3\n",
             "version=1\nwrist_along=-51\n","version=1\ninventory_depth=101\n"})
  Check(!Parse(s,parsed),"out-of-range new settings are refused");
 Check(Display(UiOffsetRight,-10)==L"10 DEGREES LEFT"&&Display(UiOffsetUp,0)==L"CENTRE"&&
       Display(PointerHand,2)==L"BUTTONS ONLY"&&Display(HudLayer,0)==L"IN THE WORLD"&&Display(BodyGlide,0)==L"0 MS"&&
       Display(WristHover,42)==L"42 MM"&&Display(UiText,140)==L"140 CM","new settings read as what they do");
}
void GestureChecks(){
 PsychoscopeGesture g;const Vec3 high{-.12f,.18f,-.12f},low{-.12f,-.02f,-.12f};
 auto tick=[&](Vec3 p,bool grip,bool usable=true,float dt=.02f,std::uint64_t epoch=1){return g.Update(p,grip,usable,dt,epoch);};
 tick(high,false);tick(high,false);tick(high,true);
 for(int n=0;n<8;++n)Check(!tick(high,true),"grip near head without travel never toggles");
 Check(tick(low,true),"downward pull toggles once");
 for(int n=0;n<10;++n)Check(!tick(low,true),"held stroke cannot retrigger");
 tick(low,false);tick(low,true);for(int n=0;n<7;++n)tick(low,true);
 Check(tick(high,true),"upward lift toggles");
 g={};tick(high,true);for(int n=0;n<12;++n)Check(!tick(low,true),"held on entry disarmed");
 tick(high,false);tick(high,true);tick(high,true,false);tick(high,true);
 for(int n=0;n<10;++n)Check(!tick(low,true),"tracking loss needs release");
 tick(high,false);tick(high,true);tick(low,true,true,.4f);
 for(int n=0;n<10;++n)Check(!tick(low,true),"stall invalidates stroke");
 tick(high,false);tick(high,true);tick(low,true,true,.02f,2);
 for(int n=0;n<10;++n)Check(!tick(low,true,true,.02f,2),"epoch transition invalidates stroke");
 g={};tick(high,false);tick(high,false);tick(high,true);
 Check(!tick({NAN,0,0},true),"nonfinite pose refuses");
}
int main(){MenuChecks();SettingsChecks();KindChecks();LayoutChecks();PresenceChecks();GestureChecks();std::cout<<"VR options ownership, settings, layout, persistence and gesture contracts passed\n";}
