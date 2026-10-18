// Real runtime adapter, with native/XR calls replaced at its boundary.
#include "../src/dll/VrOptionsRuntime.cpp"
#include <iostream>
#include <cstdlib>
#include "TestSupport.h"
namespace {
preyvr::options::Values applied=preyvr::options::Defaults();
bool nativeModal=true;unsigned pauseTaps=0,recenters=0,calibrations=0,clears=0,holsterClears=0;
DWORD viewResultForTest=0;
bool supersedeViewForTest=false;
preyvr::dll::TrackingFrame currentFrame{};
using preyvr::test::Check;
}
namespace preyvr::lifecycle { void Log(std::string_view){} }
namespace preyvr::dll {
DWORD SetPsiTargetMode(unsigned v){applied[options::PsiTarget]=v;return 0;}unsigned PsiTargetMode(){return applied[options::PsiTarget];}
DWORD SetMedkitSlot(unsigned v){applied[options::Medkit]=v;return 0;}unsigned MedkitSlotEnabled(){return applied[options::Medkit];}
void SetPhysicalMelee(unsigned v){applied[options::PhysicalMelee]=v;}unsigned PhysicalMeleeEnabled(){return applied[options::PhysicalMelee];}
void SetPhysicalContacts(unsigned v){applied[options::PhysicalContacts]=v;}unsigned PhysicalContactsEnabled(){return applied[options::PhysicalContacts];}
void SetSwingSpeed(unsigned v){applied[options::SwingSpeed]=v;}unsigned SwingSpeed(){return applied[options::SwingSpeed];}
void SetContactStrength(unsigned v){applied[options::ContactStrength]=v;}unsigned ContactStrength(){return applied[options::ContactStrength];}
void QueueHaptic(Hand,haptics::Event,const TrackingFrame&){}
DWORD SetReticleInWorld(unsigned v){applied[options::ReticleWorld]=v;return 0;}
unsigned ReticleInWorld(){return applied[options::ReticleWorld];}
DWORD SetReticleSizePercent(unsigned v){applied[options::ReticleSize]=v;return 0;}
unsigned ReticleSizePercent(){return applied[options::ReticleSize];}
void SetHapticsEnabled(unsigned v){applied[options::Haptics]=v;}
unsigned HapticsEnabled(){return applied[options::Haptics];}
void SetHapticStrength(unsigned v){applied[options::HapticStrength]=v;}
unsigned HapticStrength(){return applied[options::HapticStrength];}
DWORD SetReticleConvergenceMillimetres(unsigned v){applied[options::ReticleFallback]=v;return 0;}
DWORD ReticleConvergenceMillimetres(){return applied[options::ReticleFallback];}
void ClearHolsters(){++holsterClears;}
DWORD SetDebugOverlay(unsigned v){applied[options::DebugOverlay]=v?1:0;return 0;}
DWORD SetShotRay(unsigned v){applied[options::MuzzleAim]=v;return 0;}
unsigned ShotRayEnabled(){return applied[options::MuzzleAim];}
DWORD SetBodyFollowMode(unsigned v){applied[options::BodyFollow]=v;return 0;}
unsigned BodyFollowMode(){return applied[options::BodyFollow];}
unsigned DebugOverlayMask(){return applied[options::DebugOverlay];}
DWORD SetSnapTurnDegrees(unsigned v){applied[options::Turn]=v;return 0;}
unsigned SnapTurnDegrees(){return applied[options::Turn];}
DWORD SetHeadRelativeMovement(unsigned v){applied[options::HeadRelative]=v;return 0;}
bool HeadRelativeMovementEnabled(){return applied[options::HeadRelative]!=0;}
DWORD SetTurnLaneScale(unsigned v){applied[options::TurnSpeed]=v;return 0;}
unsigned TurnLaneScalePercent(){return applied[options::TurnSpeed];}
DWORD SetTwoHandedAim(unsigned v){applied[options::TwoHand]=v;return 0;}
unsigned TwoHandedAimEnabled(){return applied[options::TwoHand];}
DWORD SetTwoHandGripToggle(unsigned v){applied[options::GripToggle]=v;return 0;}
bool TwoHandGripToggle(){return applied[options::GripToggle]!=0;}
DWORD SetTwoHandSupportSnap(unsigned v){applied[options::SupportSnap]=v;return 0;}
bool TwoHandSupportSnap(){return applied[options::SupportSnap]!=0;}
DWORD SetUiScalePercent(unsigned v){applied[options::UiScale]=v;return 0;}
DWORD UiScalePercent(){return applied[options::UiScale];}
DWORD SetUiFitMarginPercent(unsigned v){applied[options::UiMargin]=v;return 0;}
DWORD UiFitMarginPercent(){return applied[options::UiMargin];}
DWORD SetUiCurveDegrees(unsigned v){applied[options::UiCurve]=v;return 0;}
unsigned UiCurveDegrees(){return applied[options::UiCurve];}
DWORD SetUiGuideEnabled(unsigned v){applied[options::UiGuide]=v;return 0;}
DWORD UiGuideEnabled(){return applied[options::UiGuide];}
unsigned menuDistance=200,distanceSets=0,offsetSets=0;int menuRight=0,menuUp=0;
DWORD SetUiDistanceCm(unsigned v){if(v<100||v>400)return 1;menuDistance=v;++distanceSets;return 0;}
DWORD UiDistanceCm(){return menuDistance;}
DWORD SetUiOffsetDegrees(int r,int u){if(r<-90||r>90||u<-45||u>45)return 1;menuRight=r;menuUp=u;++offsetSets;return 0;}
int UiOffsetRightDegrees(){return menuRight;}
int UiOffsetUpDegrees(){return menuUp;}
// The rest of the settings: each stores into `applied`, as the lanes do, and
// reads back from it.
#define PREYVR_STUB(SET,GET,ID,T) DWORD SET(T v){applied[options::ID]=static_cast<int>(v);return 0;} \
 T GET(){return static_cast<T>(applied[options::ID]);}
PREYVR_STUB(SetTextPanelDistanceCm,TextPanelDistanceCmValue,UiText,unsigned)
PREYVR_STUB(SetMoveLaneDeadzone,MoveLaneDeadzone,MoveDeadzone,unsigned)
PREYVR_STUB(SetTurnLaneDeadzone,TurnLaneDeadzone,TurnDeadzone,unsigned)
PREYVR_STUB(SetTorsoDeadzone,TorsoDeadzoneDegrees,BodyTurnDeadzone,int)
PREYVR_STUB(SetAnimIkFreeArms,AnimIkFreeArms,ArmsFree,unsigned)
PREYVR_STUB(SetAnimIkFingerCurl,AnimIkFingerCurl,FingerCurl,unsigned)
PREYVR_STUB(SetAimLasers,AimLasersEnabled,AimLasers,unsigned)
PREYVR_STUB(SetControllerPrompts,ControllerPromptsEnabled,ControllerPrompts,unsigned)
PREYVR_STUB(SetCarryHoldToHold,CarryHoldPreference,CarryHold,unsigned)
PREYVR_STUB(SetGrenadeHand,GrenadeHandEnabled,GrenadeHand,unsigned)
PREYVR_STUB(SetCarryThrowGainPercent,CarryThrowGainPercent,CarryThrowStrength,unsigned)
PREYVR_STUB(SetGrenadeThrowStrengthPercent,GrenadeThrowStrengthPercent,GrenadeThrowStrength,unsigned)
PREYVR_STUB(SetWristStatusOnHud,WristStatusOnHud,WristOnHud,unsigned)
PREYVR_STUB(SetWristTick,WristTickEnabled,WristTick,unsigned)
PREYVR_STUB(SetWristAlongMm,WristAlongMm,WristAlong,int)
PREYVR_STUB(SetWristHoverMm,WristHoverMm,WristHover,int)
PREYVR_STUB(SetWristTiltDegrees,WristTiltDegrees,WristTilt,int)
PREYVR_STUB(SetCarryHand,CarryHandEnabled,CarryHand,unsigned)
PREYVR_STUB(SetGrenadeDeploy,GrenadeDeployEnabled,GrenadeDeploy,unsigned)
PREYVR_STUB(SetAnimIkLongReachEnabled,AnimIkLongReachEnabled,LongReach,unsigned)
PREYVR_STUB(SetInventoryDepthPercent,InventoryDepthPercent,InventoryDepth,unsigned)
PREYVR_STUB(SetUiPointerHand,UiPointerHand,PointerHand,unsigned)
#undef PREYVR_STUB
DWORD TextPanelDistanceCm(){return TextPanelDistanceCmValue();}
DWORD SetHudLayerEnabled(unsigned v){applied[options::HudLayer]=v!=0;return 0;}
bool HudLayerEnabled(){return applied[options::HudLayer]!=0;}
DWORD SetInventoryCaptureEnabled(unsigned v){applied[options::InventoryLayer]=v!=0;return 0;}
bool InventoryCaptureEnabled(){return applied[options::InventoryLayer]!=0;}
DWORD SetInventoryStereoEnabled(unsigned v){applied[options::InventoryStereo]=v!=0;return 0;}
bool InventoryStereoEnabled(){return applied[options::InventoryStereo]!=0;}
DWORD SetMovementAxisScales(unsigned strafe,unsigned backward){
 applied[options::StrafeSpeed]=static_cast<int>(strafe);applied[options::BackwardSpeed]=static_cast<int>(backward);return 0;}
unsigned MovementStrafePercent(){return static_cast<unsigned>(applied[options::StrafeSpeed]);}
unsigned MovementBackwardPercent(){return static_cast<unsigned>(applied[options::BackwardSpeed]);}
// The real zone's rule: the stop distance stays below the start.
int roomStop=5;
DWORD SetRoomScaleZone(int start,int stop,int follow){
 if(start<5||start>500||stop<0||stop>=start||follow>1000)return ERROR_INVALID_PARAMETER;
 applied[options::BodyStep]=start;roomStop=stop;if(follow>=0)applied[options::BodyGlide]=follow;return 0;}
int RoomScaleStartMm(){return applied[options::BodyStep];}
int RoomScaleStopMm(){return roomStop;}
int RoomScaleFollowMs(){return applied[options::BodyGlide];}
unsigned useLeft=1;
unsigned UseHandLeft(){return useLeft;}
bool HudMenuIsOpen(){return nativeModal;}
bool HudMenuStateKnown(){return true;}
bool TryGetTrackingFrame(TrackingFrame& f){f=currentFrame;return true;}
unsigned long long HeadTrackingReferenceGeneration(){return 2;}
DWORD RecenterHeadTracking(bool height){if(height)++calibrations;else ++recenters;
 if(supersedeViewForTest){supersedeViewForTest=false;QueueViewRequest(2,currentFrame.epoch);}
 return viewResultForTest;}
void ClearUiPointer(){++clears;}
DWORD PostMenuAction(unsigned id,unsigned long long){Check(id==static_cast<unsigned>(input::MenuAction::Start),"only pause posted");++pauseTaps;return 0;}
}
int main(){
 using namespace preyvr;using namespace preyvr::dll;
 wchar_t temp[MAX_PATH]{};Check(GetTempPathW(MAX_PATH,temp)!=0,"temporary directory");
 const auto sandbox=std::filesystem::path(temp)/(L"PreyVR-options-test-"+std::to_wstring(GetCurrentProcessId()));
 Check(!std::filesystem::exists(sandbox),"test sandbox must be new");
 Check(SetEnvironmentVariableW(L"LOCALAPPDATA",sandbox.c_str())!=0,"process-only settings destination");
 LoadVrOptions();Check(VrOptionsReady()&&!PsychoscopeGestureEnabled(),"startup gesture off");
 SetSnapTurnDegrees(75);SetUiFitMarginPercent(95);
 Check(VrOptionsValues()[options::Turn]==75&&VrOptionsValues()[options::UiMargin]==95,"UI reflects live command-channel changes");
 currentFrame.epoch=1;currentFrame.headValidity={true,true,true,true,0};
 currentFrame.hands[1].aimValidity={true,true,true,true,0};
 auto tick=[&]{currentFrame.displayTime+=10000000;currentFrame.publishedNs=MonotonicNanoseconds();return ProcessVrOptionsInput(currentFrame);};
 // What the panel (VrOptionsUi) does with a frame: give a setting a value, or
 // press one of its buttons.
 const auto set=[](unsigned id,int value){VrOptionsUiResult result;options::Change change;
  change.setting=static_cast<int>(id);change.value=value;result.changes.push_back(change);
  ApplyVrOptionsUi(result);ServiceVrOptions();};
 const auto press=[](options::Button button){VrOptionsUiResult result;options::Change change;
  change.recenter=button==options::RecenterView;change.clearHolsters=button==options::ClearHolsterSlots;
  change.calibrate=button==options::CalibratePosture;result.changes.push_back(change);ApplyVrOptionsUi(result);};
 const auto readFile=[&]{std::ifstream file(sandbox/L"PreyVR"/L"vr-options.ini");
  std::string text((std::istreambuf_iterator<char>(file)),{});return text;};
 tick();tick();RequestVrOptions(true);tick();tick();
 Check(VrOptionsOpen()&&VrOptionsInputOwned(),"adapter claims input");
 auto& r=currentFrame.hands[1];
 currentFrame.menu.confirm=true;tick();Check(VrOptionsUiFrameInput().accept,"armed: A reaches the panel");
 const auto before=VrOptionsValues();
 currentFrame.menu.confirm=false;tick();ServiceVrOptions();
 Check(VrOptionsValues()==before,"the state machine itself changes nothing");
 // XrInput publishes the tab buttons: the grips on Touch and Index, the
 // Steam Frame's bumpers.
 auto& m=currentFrame.menu;
 m.nextTab=true;tick();Check(!VrOptionsUiFrameInput().nextPage,"a tab button steps the tab when let go");
 m.nextTab=false;tick();Check(VrOptionsUiFrameInput().nextPage,"a tab button goes to the panel");
 tick();Check(!VrOptionsUiFrameInput().nextPage,"one page step per press");
 {// Both grips, in either order -- a two-handed hold or the recenter chord --
  // step no tab, as in the game's menus, whichever buttons the tabs are on.
  m.previousTab=true;tick();m.nextTab=true;m.bothGrips=true;tick();m.previousTab=false;tick();
  Check(!VrOptionsUiFrameInput().previousPage,"a grip let go from both steps no tab");
  m.nextTab=false;m.bothGrips=false;tick();
  Check(!VrOptionsUiFrameInput().previousPage&&!VrOptionsUiFrameInput().nextPage,"nor does the other");
  m.nextTab=true;tick();m.bothGrips=true;tick();m.bothGrips=false;tick();m.nextTab=false;tick();
  Check(!VrOptionsUiFrameInput().nextPage,"both grips spoil a bumper's tab held meanwhile");
  r.gripPressed=true;tick();r.gripPressed=false;tick();
  Check(!VrOptionsUiFrameInput().nextPage,"a grip alone is no tab once XrInput says the tabs are elsewhere");}
 set(options::GripToggle,1);
 Check(applied[options::GripToggle]==1,"menu applies toggle preference");
 {const auto data=readFile();
  auto persisted=options::Defaults();Check(options::Parse(data,persisted)&&persisted[options::GripToggle]==1,"menu choice persisted");
  Check(data.find("\nturn=")==std::string::npos&&data.find("ui_margin")==std::string::npos,
        "a tuner change is not saved by the next menu save");}
 set(options::Psychoscope,1);Check(PsychoscopeGestureEnabled(),"gesture opt-in through menu reaches consumer");
 set(options::Holsters,1);Check(HolstersEnabled(),"holster opt-in reaches input consumer");
 set(options::Wrist,0);Check(!WristDisplayEnabled()&&WristSizePercent()==100,"wrist toggle (on by default) reaches layer consumer");
 const auto clearBefore=holsterClears;press(options::ClearHolsterSlots);
 Check(holsterClears==clearBefore+1,"clear holsters invokes its own action");
 set(options::ReticleWorld,0);Check(ReticleInWorld()==0,"the reticle on the HUD reaches the reticle lane");
 set(options::ReticleSize,150);Check(ReticleSizePercent()==150,"the reticle's size reaches the reticle lane");
 set(options::ReticleFallback,10500);Check(ReticleConvergenceMillimetres()==10500,"fallback setting retains sub-metre precision");
 set(options::Haptics,0);Check(!HapticsEnabled(),"haptics disable reaches XR adapter");
 Check(!PhysicalMeleeEnabled()&&!PhysicalContactsEnabled(),"physical features independently off by default");
 set(options::PhysicalMelee,1);
 Check(PhysicalMeleeEnabled()&&!PhysicalContactsEnabled(),"physical wrench opt-in reaches consumer independently");
 set(options::PhysicalContacts,1);Check(PhysicalContactsEnabled(),"object nudge opt-in reaches consumer");
 Check(PsiTargetMode()==0&&!MedkitSlotEnabled(),"abilities start with native behavior");
 set(options::PsiTarget,1);Check(PsiTargetMode()==1,"head-directed psi reaches adapter");
 set(options::PsiTarget,2);Check(PsiTargetMode()==2,"controller-directed psi reaches adapter");
 set(options::Medkit,1);Check(MedkitSlotEnabled(),"medkit opt-in reaches adapter");
 press(options::CalibratePosture);
 viewResultForTest=3;ServiceVrOptions();
 Check(calibrations==1&&recenters==0&&VrSetupMessage()==L"WAITING FOR CURRENT HEADSET POSE","height calibration retries a busy reference");
 viewResultForTest=0;ServiceVrOptions();ServiceVrOptions();
 Check(calibrations==2&&recenters==0&&VrSetupMessage()==L"CURRENT POSTURE CALIBRATED / VR VIEW RESET","successful calibration reports completion and stops retrying");
 press(options::RecenterView);ServiceVrOptions();
 Check(recenters==1&&calibrations==2&&VrSetupMessage()==L"VR VIEW RESET / HEIGHT BASELINE PRESERVED","ordinary reset preserves the posture baseline");
 press(options::RecenterView);viewResultForTest=2;ServiceVrOptions();ServiceVrOptions();
 Check(recenters==2&&VrSetupMessage()==L"LOOK STRAIGHT AHEAD AND TRY AGAIN","vertical view refusal never automatically retries");
 press(options::RecenterView);++currentFrame.epoch;ServiceVrOptions();
 Check(recenters==2&&VrSetupMessage()==L"NO CURRENT HEADSET POSE / TRY AGAIN","request from an old tracking session never executes");
 // A new request arriving while the worker calls the native boundary must keep
 // its own action/epoch/deadline and must not receive the older result.
 QueueViewRequest(1,currentFrame.epoch);supersedeViewForTest=true;viewResultForTest=3;
 ServiceVrOptions();Check(VrSetupMessage()==L"WAITING FOR CURRENT HEADSET POSE","superseded reset cannot publish a result");
 viewResultForTest=0;ServiceVrOptions();
 Check(recenters==3&&calibrations==3&&VrSetupMessage()==L"CURRENT POSTURE CALIBRATED / VR VIEW RESET","new calibration never inherits older reset retry");
 QueueViewRequest(1,currentFrame.epoch);
 {std::lock_guard lock(viewMutex);viewPending->deadline=0;}
 ServiceVrOptions();Check(recenters==3,"expired request never reaches native backend");
 // Native menu changes behind us: held confirm cannot leak to the game.
 currentFrame.menu.confirm=true;nativeModal=false;Check(tick()&&!VrOptionsOpen(),"native menu closes options");
 Check(tick(),"held input quarantined");currentFrame.menu.confirm=false;tick();Check(!tick(),"neutral releases ownership");
 ResetVrOptionsSession();Check(!VrOptionsInputOwned(),"teardown clears ownership");
 Check(pauseTaps==0,"opening from native menu posts no redundant pause");
 nativeModal=true;
 tick();tick();RequestVrOptions(true);tick();
 Check(VrOptionsOpen()&&VrOptionsInputOwned(),"reopened after teardown");
 {const auto swing=SwingSpeed(),turn=SnapTurnDegrees();
  VrOptionsUiResult result;options::Change change;change.setting=options::HapticStrength;change.value=35;
  result.changes.push_back(change);change.setting=options::SwingSpeed;change.value=5000;result.changes.push_back(change);
  change.setting=options::Turn;change.value=7;result.changes.push_back(change);
  ApplyVrOptionsUi(result);ServiceVrOptions();
  Check(HapticStrength()==35,"a value is taken as it is");
  Check(SwingSpeed()==swing&&SnapTurnDegrees()==turn,"a value the setting does not take changes nothing");
  const auto text=readFile();auto stored=options::Defaults();
  Check(options::Parse(text,stored)&&stored[options::HapticStrength]==35,"and the one taken is saved");
  Check(text.find("swing_speed")==std::string::npos,"the refused one is not saved as a choice");}
 {// Every setting reaches its lane, reads back, and is saved like the rest.
  const std::pair<unsigned,int> chosen[]={
   {options::UiDistance,150},{options::UiOffsetRight,20},{options::UiOffsetUp,-10},{options::UiText,180},
   {options::HudLayer,0},{options::MoveDeadzone,20},{options::TurnDeadzone,25},{options::StrafeSpeed,80},
   {options::BackwardSpeed,60},{options::BodyTurnDeadzone,50},{options::BodyStep,100},{options::BodyGlide,200},
   {options::ArmsFree,0},{options::CarryHold,0},{options::GrenadeHand,0},{options::CarryThrowStrength,200},
   {options::GrenadeThrowStrength,150},{options::WristOnHud,1},{options::WristTick,0},{options::WristAlong,100},
   {options::WristHover,50},{options::WristTilt,30},{options::CarryHand,0},{options::GrenadeDeploy,1},
   {options::LongReach,0},{options::InventoryLayer,1},{options::InventoryStereo,1},{options::InventoryDepth,50},
   {options::UiCurve,50},{options::DebugOverlay,1},{options::FingerCurl,0},{options::AimLasers,0},
   {options::ControllerPrompts,0}};
  VrOptionsUiResult result;
  for(const auto& [id,v]:chosen){options::Change change;change.setting=static_cast<int>(id);change.value=v;
   result.changes.push_back(change);}
  ApplyVrOptionsUi(result);ServiceVrOptions();
  const auto live=VrOptionsValues();
  for(const auto& [id,v]:chosen)Check(live[id]==v,"each setting reaches its lane and reads back");
  Check(menuDistance==150&&menuRight==20&&menuUp==-10,"menu placement reaches its setters, both axes kept");
  Check(roomStop<100,"a body step keeps the zone's stop below it");
  const auto text=readFile();
  auto stored=options::Defaults();options::Present marked{};
  Check(options::Parse(text,stored,&marked),"the file stays readable");
  for(const auto& [id,v]:chosen)Check(marked[id]&&stored[id]==v,"and each is saved");
  // Carrying in the hand rides on the left hand's use; without it, it waits.
  useLeft=0;result.changes.clear();
  {options::Change change;change.setting=options::CarryHand;change.value=1;result.changes.push_back(change);}
  ApplyVrOptionsUi(result);ServiceVrOptions();
  Check(CarryHandEnabled()==0,"physical carrying is not armed without the left hand's use");
  useLeft=1;ApplyVrOptionsUi(result);ServiceVrOptions();
  Check(CarryHandEnabled()==1,"and is with it");}
 r.thumbstickY=0;currentFrame.menu.scroll.y=.8f;tick();
 Check(VrOptionsUiFrameInput().scroll>.7f,"the left stick scrolls the panel");
 currentFrame.menu.scroll.y=.1f;tick();Check(VrOptionsUiFrameInput().scroll==0,"a resting left stick does not");
 currentFrame.menu.scroll.y=0;tick();
 // **Navigation is device-neutral** (TrackingFrame::navigate: XrInput composes
 // it from the right stick and any controller's own navigation buttons). It is
 // pressed past .6 and let go below .35.
 auto& nav=currentFrame.menu.navigate;
 nav.x=1;tick();Check(VrOptionsUiFrameInput().right&&!VrOptionsUiFrameInput().left,"navigating right adjusts up");
 nav.x=-1;tick();Check(VrOptionsUiFrameInput().left&&!VrOptionsUiFrameInput().right,"left adjusts down");
 nav.x=0;nav.y=-1;tick();Check(VrOptionsUiFrameInput().down,"down moves down");
 nav.y=1;tick();Check(VrOptionsUiFrameInput().up&&!VrOptionsUiFrameInput().down,"up moves up");
 nav.y=.5f;tick();Check(VrOptionsUiFrameInput().up,"once pressed, it stays pressed past .35");
 nav.y=.3f;tick();Check(!VrOptionsUiFrameInput().up,"and is let go below it");
 nav.y=.5f;tick();Check(!VrOptionsUiFrameInput().up,"a fresh press needs .6");
 nav.y=0;tick();Check(!VrOptionsUiFrameInput().up&&!VrOptionsUiFrameInput().left,"at rest it does nothing");
 {VrOptionsUiResult result;result.close=true;ApplyVrOptionsUi(result);
  Check(!VrOptionsOpen()&&VrOptionsInputOwned()&&!VrOptionsUiFrameInput().accept,"back closes at once and keeps input until neutral");
  options::Change change;change.setting=options::HapticStrength;change.value=50;result.close=false;result.changes.push_back(change);
  const auto strength=HapticStrength();ApplyVrOptionsUi(result);ServiceVrOptions();
  Check(HapticStrength()==strength,"a frame drawn after closing changes nothing");}
 tick();Check(!tick()&&!VrOptionsInputOwned(),"neutral gives input back after the panel");
 // Navigation held as the panel opens is not at rest: nothing acts until it is let go.
 nav.x=1;RequestVrOptions(true);tick();tick();
 Check(VrOptionsOpen()&&!VrOptionsUiFrameInput().right,"navigation held while opening does not adjust");
 nav.x=0;tick();nav.x=1;tick();
 Check(VrOptionsUiFrameInput().right,"pressed again after release, it does");
 nav.x=0;tick();
 {VrOptionsUiResult result;result.close=true;ApplyVrOptionsUi(result);tick();tick();}
 {// Over the running game: no game menu needed, no pause posted, and a game
  // menu appearing under it closes it.
  nativeModal=false;const auto taps=pauseTaps;tick();tick();
  RequestVrOptions(true);tick();tick();
  Check(VrOptionsOpen()&&VrOptionsInputOwned()&&pauseTaps==taps,"a request opens it over the game, posting no pause");
  nativeModal=true;tick();Check(!VrOptionsOpen(),"a game menu opening under it closes it");
  nativeModal=false;tick();tick();Check(!VrOptionsInputOwned(),"input goes back once the controls are neutral");
  currentFrame.system.pause=true;
  for(int n=0;n<70;++n)tick();
  Check(VrOptionsOpen()&&pauseTaps==taps,"the long hold opens it over the game too, without pausing");
  currentFrame.system.pause=false;tick();Check(pauseTaps==taps&&VrOptionsOpen(),"and letting go is no pause tap");
  currentFrame.menu.back=true;tick();currentFrame.menu.back=false;tick();tick();
  Check(!VrOptionsOpen()&&!VrOptionsInputOwned(),"B closes it over the game as over a menu");
  nativeModal=true;tick();tick();}
 {// **Held by the laser** (a press on no control): the trigger holds it and
  // reaches no control, the laser hand's stick pushes it, letting go drops it.
  RequestVrOptions(true);tick();tick();
  Check(VrOptionsOpen()&&!VrOptionsGrabState().active,"premise: open, nothing held");
  r.triggerPressed=true;tick();
  Check(VrOptionsUiFrameInput().click,"premise: the trigger reaches the panel");
  VrOptionsUiResult grabbed;grabbed.grabStarted=true;ApplyVrOptionsUi(grabbed);
  const auto first=VrOptionsGrabState();
  Check(first.active&&first.push==0,"a press on no control holds the panel");
  r.thumbstickY=.8f;nav.y=.8f;tick();
  Check(!VrOptionsUiFrameInput().click&&!VrOptionsUiFrameInput().up&&VrOptionsGrabState().push>.7f,
        "held: no click, no navigation; the laser hand's stick pushes it away");
  r.thumbstickY=-.8f;nav.y=-.8f;tick();Check(VrOptionsGrabState().push<-.7f,"or pulls it in");
  r.thumbstickY=.1f;nav.y=.1f;tick();Check(VrOptionsGrabState().active&&VrOptionsGrabState().push==0,"a resting stick moves it neither way");
  r.thumbstickY=0;nav.y=0;r.triggerPressed=false;tick();
  Check(!VrOptionsGrabState().active,"letting go of the trigger drops it");
  r.triggerPressed=true;tick();ApplyVrOptionsUi(grabbed);
  Check(VrOptionsGrabState().active&&VrOptionsGrabState().serial==first.serial+1,"each grab is its own");
  {VrOptionsUiResult closing;closing.close=true;ApplyVrOptionsUi(closing);}
  Check(!VrOptionsGrabState().active,"closing the panel drops it");
  r.triggerPressed=false;tick();tick();
  ApplyVrOptionsUi(grabbed);Check(!VrOptionsGrabState().active,"a closed panel cannot be grabbed");}
 {// Switch the debug overlay off in the menu; it was last saved on.
  RequestVrOptions(true);tick();tick();
  Check(VrOptionsOpen(),"reopened for the overlay change");
  set(options::DebugOverlay,0);
  VrOptionsUiResult result;result.close=true;ApplyVrOptionsUi(result);tick();tick();
  Check(DebugOverlayMask()==0,"the debug overlay is off");}
 {// A new process: the file is read at load and applied only at VR start, after
  // VR start's own defaults and any command sent before it (ApplySavedVrOptions).
  const auto restart=[]{
   preyvr::dll::ready=false;
   {std::lock_guard lock(preyvr::dll::savedMutex);preyvr::dll::saved=options::Defaults();preyvr::dll::present={};}
   LoadVrOptions();};
  // What VR start and earlier commands set before the saved choices apply.
  applied[options::Turn]=45;applied[options::UiMargin]=72;applied[options::UiScale]=130;applied[options::UiCurve]=20;
  applied[options::HapticStrength]=70;applied[options::DebugOverlay]=0;applied[options::GrenadeHand]=1;
  applied[options::AimLasers]=1;applied[options::ControllerPrompts]=1;
  restart();
  Check(HapticStrength()==70&&UiCurveDegrees()==20,"loading the file applies nothing");
  // The menu panel's settings first, before the session's first menu frame.
  menuDistance=200;menuRight=menuUp=0;
  ApplySavedPanelOptions();
  Check(UiCurveDegrees()==50&&menuDistance==150&&menuRight==20&&menuUp==-10,
        "the saved panel settings apply before the rest of VR start");
  Check(HapticStrength()==70&&GrenadeHandEnabled()==1&&AimLasersEnabled()==1&&applied[options::UiScale]==130,
        "and nothing else, nor a panel setting never chosen in the menu");
  ApplySavedVrOptions();
  Check(HapticStrength()==35&&UiCurveDegrees()==50&&GrenadeHandEnabled()==0&&menuDistance==150,
        "saved choices win over VR start's defaults and earlier commands");
  Check(applied[options::Turn]==45&&applied[options::UiMargin]==72&&applied[options::UiScale]==130,
        "settings never chosen in the menu keep their values; tuner values were never saved");
  Check(DebugOverlayMask()==0,"a saved \"off\" debug overlay stays off after a restart");
  Check(AimLasersEnabled()==0,"saved \"off\" aim lasers stay off though VR start switches them on");
  Check(ControllerPromptsEnabled()==0,"and saved \"off\" controller prompts likewise");
  applied[options::DebugOverlay]=1;ApplySavedVrOptions();
  Check(DebugOverlayMask()==0,"and a dbg.draw does not outlast the next VR start");
  // F11: a command's curvature lasts until the next VR start, then the saved one is back.
  SetUiCurveDegrees(20);ApplySavedVrOptions();
  Check(UiCurveDegrees()==50,"an F11 re-enable restores the saved curvature");}
 ResetVrOptionsSession();
 std::filesystem::remove(sandbox/L"PreyVR"/L"vr-options.ini");
 std::filesystem::remove(sandbox/L"PreyVR");std::filesystem::remove(sandbox);
 {// **The Controls tab's rows**: each action beside the runtime's names for
  // its buttons, by section; an optional action nothing binds left out, a
  // required one shown unbound; the recenter chord on whatever pauses.
  std::vector<XrBoundControl> bound;
  for(const auto& action:input::Actions())bound.push_back({action.role,{}});
  const auto at=[&](input::Role role)->std::vector<std::string>&{return bound[static_cast<std::size_t>(role)].sources;};
  at(input::Role::jump)={"Right Hand A Button"};
  at(input::Role::pause)={"Right Hand Menu Button"};
  at(input::Role::secondary)={"Left Hand X Button","Right Hand X Button"};
  at(input::Role::trigger)={"Left Hand Trigger"};
  const auto rows=VrOptionsControlRows(bound);
  const auto find=[&](const std::string& action){
   for(const auto& row:rows)if(row.action==action)return &row;
   return static_cast<const VrOptionsControlRow*>(nullptr);};
  Check(find("PLAY")&&find("PLAY")->heading&&find("MENUS")&&find("ANY TIME"),"three sections");
  Check(find("Jump")&&find("Jump")->buttons=="Right Hand A Button","an action beside its button");
  Check(find("Crouch")&&find("Crouch")->buttons.empty(),"a required action nothing binds shows unbound");
  Check(!find("Sprint")&&!find("Navigate Up")&&!find("Close Menu"),"an optional one nothing binds is left out");
  Check(find(input::ActionOf(input::Role::secondary).label)->buttons=="Left Hand X Button / Right Hand X Button",
        "several buttons, all named");
  Check(!find(input::ActionOf(input::Role::trigger).label)&&!find(input::ActionOf(input::Role::gripPose).label),
        "the hands' own trigger, grip and poses are not buttons to list");
  Check(find("Recenter")&&find("Recenter")->buttons=="Right Hand Menu Button + both grips","recenter on what pauses");
  Check(find("Tabs")&&find("Tabs")->buttons=="Grips, on release","no tab buttons: the grips are the tabs");
  Check(find("Pages")!=nullptr,"the trigger of the hand without the laser pages on every controller");
  {// The Steam Frame: its bumpers are the tabs, listed by name, and the grips' row goes.
   auto frame=bound;
   frame[static_cast<std::size_t>(input::Role::previousTab)].sources={"Left Hand Bumper"};
   frame[static_cast<std::size_t>(input::Role::nextTab)].sources={"Right Hand Bumper"};
   const auto frameRows=VrOptionsControlRows(frame);
   bool grips=false,bumper=false;
   for(const auto& row:frameRows){
    grips=grips||row.action=="Tabs";
    bumper=bumper||(row.action==input::ActionOf(input::Role::previousTab).label&&row.buttons=="Left Hand Bumper");
   }
   Check(bumper&&!grips,"a controller with tab buttons lists them, not the grips");}
  std::size_t play=0,menus=0;
  for(std::size_t i=0;i<rows.size();++i){if(rows[i].action=="PLAY")play=i;if(rows[i].action=="MENUS")menus=i;}
  std::size_t jump=0;for(std::size_t i=0;i<rows.size();++i)if(rows[i].action=="Jump")jump=i;
  Check(play<jump&&jump<menus,"play's actions under PLAY");}
 std::cout<<"Runtime menu, native-input ownership, settings application and disk roundtrip passed\n";
}
