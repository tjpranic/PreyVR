#include "VrOptionsRuntime.h"
#include "SceneQuery.h"
#include "PhysicalInteractions.h"
#include "ContactLane.h"
#include "SurfaceStopLane.h"
#include "SwingLane.h"
#include "PsiMedkit.h"
#include "Haptics.h"
#include "ReticleFollow.h"
#include "MarkerStereo.h"
#include "AimTakeover.h"
#include "BodyEquipment.h"
#include "ControllerPrompts.h"
#include "HeadTrackingHook.h"
#include "UiMode.h"
#include "MoveLane.h"
#include "InputPost.h"
#include "preyvr/MenuNavigator.h"
#include "preyvr/LatestSnapshot.h"
#include "XrSessionHost.h"
#include "DebugOverlay.h"
#include "ShotRay.h"
#include "AnimIkTakeover.h"
#include "CarryLane.h"
#include "GrenadeLane.h"
#include "HudLayer.h"
#include "InteractionLane.h"
#include "WristLane.h"
#include "preyvr/DebugOverlayScene.h"
#include "preyvr/MonotonicClock.h"
#include "Logger.h"
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <algorithm>
#include <cmath>
#include <mutex>
#include <ranges>
namespace preyvr::dll {
namespace {
options::Menu menu; // XR thread only
std::array<std::atomic<int>,options::Count> values{};
std::atomic<bool> ready{false},open{false},owned{false},saveFailed{false},scope{false};
std::atomic<bool> holsters{false},wrist{true}; // wrist: options::Defaults()
std::atomic<bool> beltHints{true};
std::atomic<unsigned> wristSize{100};
// One changed-flag per setting: no cap on how many there are.
std::array<std::atomic<bool>,options::Count> dirty{};
// **What the save file holds**: values chosen in the menu, or read from the
// file -- never the live lane values, so a tuner change stays in its session
// even after something else is saved. Only `present` settings are written and
// applied; the rest keep their built-in values.
std::mutex savedMutex;
options::Values saved=options::Defaults();
options::Present present{};
std::atomic<int> request{0};
// Latest explicit request wins. Busy reference locks retry for at most one
// second, within the same tracking session; a stale request never fires later.
struct ViewRequest { int action=0;std::uint64_t epoch=0,deadline=0,generation=0; };
std::mutex viewMutex;
std::optional<ViewRequest> viewPending;
std::uint64_t viewGeneration=0;
std::atomic<int> viewResult{-1}; // 1 reset, 2 posture calibration
void QueueViewRequest(int action,std::uint64_t epoch){
 std::lock_guard lock(viewMutex);
 viewPending=ViewRequest{action,epoch,MonotonicNanoseconds()+1000000000ull,++viewGeneration};
 viewResult=-2;
}
std::optional<ui::Surface> pointerSurface;
std::uint64_t surfaceReference=0;
long long previousTime=0;
// The panel (VrOptionsUi) draws and navigates; the menu state machine opens,
// closes and owns the controls.
VrOptionsUiInput uiInput;      // XR thread: this frame's controls, for the panel
TrackingFrame uiFrame{};       // XR thread: the sample they came from (haptics)
bool stickUp=false,stickDown=false,stickLeft=false,stickRight=false;
input::ReleaseTap previousTab,nextTab;
bool grabbing=false;unsigned grabSerial=0;float grabPush=0; // XR thread: VrOptionsGrab
// One change from the panel: buttons queue their action, a setting takes its
// value and saves on the command worker, and a changed value pulses `hand`.
void HandleChange(const options::Change& change,const TrackingFrame& f,Hand hand){
 if(change.recenter||change.calibrate){
  QueueViewRequest(change.calibrate?2:1,f.epoch);
 }
 if(change.clearHolsters)ClearHolsters();
 if(change.setting>=0){
  const auto id=static_cast<unsigned>(change.setting);
  // A value outside the setting's range is refused: not applied, not saved.
  if(!options::Valid(id,change.value))return;
  const auto before=VrOptionsValues()[id];
  values[id]=change.value;dirty[id]=true;
  if(before!=change.value)QueueHaptic(hand,haptics::Event::MenuChange,f);
 }
}
// The panel's navigation (MenuInput::navigate) as four held directions,
// with the navigator's hysteresis (MenuNavigatorPolicy), so a resting stick
// cannot chatter. ImGui does the repeat. Buttons stay up until
// the menu is armed (the opening hold, or a press carried in from the game
// menu, must not also act on the panel).
void CaptureUiInput(const TrackingFrame& f,const std::optional<ui::RayHit>& hit,bool armed,float dt,bool trigger){
 VrOptionsUiInput in;in.dt=dt;
 if(hit&&hit->inside){in.pointer=true;in.u=hit->u;in.v=hit->v;}
 const auto held=[](bool& state,float value){
  constexpr input::MenuNavigatorPolicy nav{};state=state?value>nav.release:value>nav.engage;return state;};
 const bool up=held(stickUp,f.menu.navigate.y),down=held(stickDown,-f.menu.navigate.y);
 const bool left=held(stickLeft,-f.menu.navigate.x),right=held(stickRight,f.menu.navigate.x);
 // The game menus' tab buttons (the Steam Frame's bumpers, else the grips) step
 // through the tabs when let go, and not when both grips or both tab buttons
 // were held: a two-handed hold, or the recenter chord (ReleaseTap, as in the
 // game's menus). A tab button let go as the panel arms is no tab.
 const bool spoiled=f.menu.bothGrips||(f.menu.previousTab&&f.menu.nextTab);
 const bool previous=previousTab.Update(f.menu.previousTab,spoiled),next=nextTab.Update(f.menu.nextTab,spoiled);
 if(!armed){previousTab.Reset();nextTab.Reset();}
 if(armed){
  in.click=trigger;in.accept=f.menu.confirm;in.up=up;in.down=down;in.left=left;in.right=right;
  in.previousPage=previous;in.nextPage=next;
  // The scroll stick (the left one) scrolls the list; it moves nothing else
  // while the panel is open.
  const float scroll=f.menu.scroll.y;
  if(std::isfinite(scroll)&&std::fabs(scroll)>.2f)in.scroll=scroll;
 }
 // **While the panel is held**, the trigger holds it rather than clicking, and
 // the laser hand's stick pushes it away or pulls it in; the sticks navigate
 // and scroll nothing meanwhile. Letting go of the trigger drops it.
 const auto hand=UiPointerHand();
 if(grabbing&&(!armed||!trigger||hand>=2)) {
  grabbing=false;
  if(hand<2)QueueHaptic(static_cast<Hand>(hand),haptics::Event::MenuChange,f);
 }
 grabPush=0;
 if(grabbing) {
  in.click=false;in.up=in.down=in.left=in.right=false;in.scroll=0;in.held=true;
  const float y=f.hands[hand].thumbstickY;
  if(std::isfinite(y)&&std::fabs(y)>.2f)grabPush=std::clamp(y,-1.f,1.f);
 }
 uiInput=in;uiFrame=f;
}
std::filesystem::path SettingsPath(){
 wchar_t path[32768]{};const auto n=GetEnvironmentVariableW(L"LOCALAPPDATA",path,32768);
 if(!n||n>=32768)return {};
 return std::filesystem::path(path)/L"PreyVR"/L"vr-options.ini";
}
// Most settings are one lane setter and its getter.
template<class R,class P> P ParameterOf(R(*)(P));
template<auto Setter> void Set(int v){Setter(static_cast<decltype(ParameterOf(Setter))>(v));}
template<auto Getter> int Get(){return static_cast<int>(Getter());}
// **How each setting reaches its lane, and is read back from it**, in Setting
// order. Read back from the lane rather than from what the menu last set, so
// the panel shows a command's change (Tune Prey VR.cmd) as it stands.
struct Wiring { options::Setting id; void (*apply)(int); int (*read)(); };
constexpr Wiring wiring[]={
 {options::Turn,Set<SetSnapTurnDegrees>,Get<SnapTurnDegrees>},
 {options::HeadRelative,Set<SetHeadRelativeMovement>,Get<HeadRelativeMovementEnabled>},
 {options::TurnSpeed,Set<SetTurnLaneScale>,Get<TurnLaneScalePercent>},
 {options::TwoHand,Set<SetTwoHandedAim>,Get<TwoHandedAimEnabled>},
 {options::GripToggle,Set<SetTwoHandGripToggle>,Get<TwoHandGripToggle>},
 {options::SupportSnap,Set<SetTwoHandSupportSnap>,Get<TwoHandSupportSnap>},
 {options::Psychoscope,[](int v){scope.store(v!=0);},[]{return static_cast<int>(scope.load());}},
 {options::UiScale,Set<SetUiScalePercent>,Get<UiScalePercent>},
 {options::UiMargin,Set<SetUiFitMarginPercent>,Get<UiFitMarginPercent>},
 {options::UiCurve,Set<SetUiCurveDegrees>,Get<UiCurveDegrees>},
 {options::UiGuide,Set<SetUiGuideEnabled>,Get<UiGuideEnabled>},
 // A change of mind clears the slots, so no weapon is left in a disabled one.
 {options::Holsters,[](int v){holsters.store(v!=0);ClearHolsters();},[]{return static_cast<int>(holsters.load());}},
 {options::Wrist,[](int v){wrist.store(v!=0);},[]{return static_cast<int>(wrist.load());}},
 {options::WristSize,[](int v){wristSize=static_cast<unsigned>(v);},[]{return static_cast<int>(wristSize.load());}},
 {options::ReticleWorld,Set<SetReticleInWorld>,Get<ReticleInWorld>},
 {options::ReticleSize,Set<SetReticleSizePercent>,Get<ReticleSizePercent>},
 {options::ReticleFallback,Set<SetReticleConvergenceMillimetres>,Get<ReticleConvergenceMillimetres>},
 {options::MarkerSize,Set<SetMarkerSizePercent>,Get<MarkerSizePercent>},
 {options::Haptics,Set<SetHapticsEnabled>,Get<HapticsEnabled>},
 {options::HapticStrength,Set<SetHapticStrength>,Get<HapticStrength>},
 {options::FreeSwing,Set<SetFreeSwing>,Get<FreeSwingEnabled>},
 {options::SwingSpeed,Set<SetSwingSpeed>,Get<SwingSpeed>},
 {options::HeavySwingSpeed,Set<SetHeavySwingSpeed>,Get<HeavySwingSpeed>},
 {options::PhysicalContact,Set<SetPhysicalContact>,Get<PhysicalContactEnabled>},
 {options::PushStrength,Set<SetPushStrength>,Get<PushStrength>},
 {options::PsiTarget,Set<SetPsiTargetMode>,Get<PsiTargetMode>},
 {options::Medkit,Set<SetMedkitSlot>,Get<MedkitSlotEnabled>},
 {options::BeltHints,[](int v){beltHints=v!=0;},[]{return static_cast<int>(beltHints.load());}},
 // On is every layer; the menu's row cannot pick layers (dbg.draw can).
 {options::DebugOverlay,[](int v){SetDebugOverlay(v?debugdraw::kAllLayers:0u);},
  []{return static_cast<int>(DebugOverlayMask()!=0);}},
 {options::MuzzleAim,Set<SetShotRay>,Get<ShotRayEnabled>},
 {options::UiDistance,Set<SetUiDistanceCm>,Get<UiDistanceCm>},
 // The offsets are set as a pair: each keeps the other's current value.
 {options::UiOffsetRight,[](int v){SetUiOffsetDegrees(v,UiOffsetUpDegrees());},Get<UiOffsetRightDegrees>},
 {options::UiOffsetUp,[](int v){SetUiOffsetDegrees(UiOffsetRightDegrees(),v);},Get<UiOffsetUpDegrees>},
 {options::UiText,Set<SetTextPanelDistanceCm>,Get<TextPanelDistanceCm>},
 {options::PointerHand,Set<SetUiPointerHand>,Get<UiPointerHand>},
 {options::HudLayer,Set<SetHudLayerEnabled>,Get<HudLayerEnabled>},
 {options::MoveDeadzone,Set<SetMoveLaneDeadzone>,Get<MoveLaneDeadzone>},
 {options::TurnDeadzone,Set<SetTurnLaneDeadzone>,Get<TurnLaneDeadzone>},
 // Sideways and backward speed are set as a pair, like the offsets.
 {options::StrafeSpeed,[](int v){SetMovementAxisScales(static_cast<unsigned>(v),MovementBackwardPercent());},
  Get<MovementStrafePercent>},
 {options::BackwardSpeed,[](int v){SetMovementAxisScales(MovementStrafePercent(),static_cast<unsigned>(v));},
  Get<MovementBackwardPercent>},
 {options::ArmsFree,Set<SetAnimIkFreeArms>,Get<AnimIkFreeArms>},
 {options::CarryHold,Set<SetCarryHoldToHold>,Get<CarryHoldPreference>},
 {options::GrenadeHand,Set<SetGrenadeHand>,Get<GrenadeHandEnabled>},
 {options::CarryThrowStrength,Set<SetCarryThrowGainPercent>,Get<CarryThrowGainPercent>},
 {options::GrenadeThrowStrength,Set<SetGrenadeThrowStrengthPercent>,Get<GrenadeThrowStrengthPercent>},
 {options::WristOnHud,Set<SetWristStatusOnHud>,Get<WristStatusOnHud>},
 {options::WristTick,Set<SetWristTick>,Get<WristTickEnabled>},
 {options::WristAlong,Set<SetWristAlongMm>,Get<WristAlongMm>},
 {options::WristHover,Set<SetWristHoverMm>,Get<WristHoverMm>},
 {options::WristTilt,Set<SetWristTiltDegrees>,Get<WristTiltDegrees>},
 // Carrying in the hand rides on the left hand's use, as VR start arms it.
 {options::CarryHand,[](int v){if(!v||UseHandLeft())SetCarryHand(static_cast<unsigned>(v));},Get<CarryHandEnabled>},
 {options::GrenadeDeploy,Set<SetGrenadeDeploy>,Get<GrenadeDeployEnabled>},
 {options::LongReach,Set<SetAnimIkLongReachEnabled>,Get<AnimIkLongReachEnabled>},
 {options::InventoryLayer,Set<SetInventoryCaptureEnabled>,Get<InventoryCaptureEnabled>},
 {options::InventoryStereo,Set<SetInventoryStereoEnabled>,Get<InventoryStereoEnabled>},
 {options::InventoryDepth,Set<SetInventoryDepthPercent>,Get<InventoryDepthPercent>},
 {options::FingerCurl,Set<SetAnimIkFingerCurl>,Get<AnimIkFingerCurl>},
 {options::AimLasers,Set<SetAimLasers>,Get<AimLasersEnabled>},
 {options::ControllerPrompts,Set<SetControllerPrompts>,Get<ControllerPromptsEnabled>},
 {options::ArmLength,Set<SetArmLengthCm>,Get<ArmLengthCm>},
 {options::SurfaceStop,Set<SetSurfaceStop>,Get<SurfaceStopEnabled>},
 {options::HeavyMass,Set<SetSurfaceHeavyMass>,Get<SurfaceHeavyMass>},
};
constexpr bool InSettingOrder(){
 for(unsigned i=0;i<std::size(wiring);++i)if(wiring[i].id!=i)return false;
 return std::size(wiring)==options::Count;
}
static_assert(InSettingOrder(),"one wiring per setting, in Setting order");
void Apply(unsigned id,int v){if(id<options::Count)wiring[id].apply(v);}
void Save(){
 try{
  const auto path=SettingsPath();if(path.empty()){saveFailed=true;return;}
  std::filesystem::create_directories(path.parent_path());
  auto temp=path;temp+=L".tmp";
  std::string text;
  {std::lock_guard lock(savedMutex);text=options::Serialize(saved,&present);}
  {std::ofstream out(temp,std::ios::binary|std::ios::trunc);out<<text;out.flush();if(!out){saveFailed=true;return;}}
  saveFailed=!MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);
 }catch(...){saveFailed=true;}
 if(saveFailed.load())lifecycle::Log("preyvr_options save=failed settings_remain_active=1");
}
}
options::Values VrOptionsValues(){
 // The lanes' own values, including changes made through the command channel;
 // a menu edit not yet applied stands in for its own setting until it is.
 options::Values v{};
 for(unsigned i=0;i<options::Count;++i)v[i]=dirty[i].load()?values[i].load():wiring[i].read();
 return v;
}
void LoadVrOptions(){
 if(ready.load())return;
 // Read only: the values apply when VR starts (ApplySavedVrOptions), after VR
 // start's own defaults, so a saved choice is the one in force.
 try{
  std::ifstream in(SettingsPath(),std::ios::binary);
  if(in){std::string data;char chunk[8193]{};in.read(chunk,sizeof(chunk));data.assign(chunk,static_cast<size_t>(in.gcount()));
   options::Values parsed{};options::Present marked{};
   if(options::Parse(data,parsed,&marked)){std::lock_guard lock(savedMutex);saved=parsed;present=marked;}
   else lifecycle::Log("preyvr_options load=invalid defaults_retained=1");
  }
 }catch(...){lifecycle::Log("preyvr_options load=failed defaults_retained=1");}
 ready.store(true);
}
// Applies the saved value of each of `ids` the file holds, and logs the count.
template<class Ids> void ApplySaved(const Ids& ids,const char* counter){
 options::Values v;options::Present p;
 {std::lock_guard lock(savedMutex);v=saved;p=present;}
 unsigned count=0;
 for(const unsigned id:ids){
  if(!p[id])continue;
  Apply(id,v[id]);++count;
 }
 lifecycle::Log(std::string("preyvr_options ")+counter+"="+std::to_string(count));
}
void ApplySavedVrOptions(){ApplySaved(std::views::iota(0u,static_cast<unsigned>(options::Count)),"applied");}
// What the menu panel looks like: its size, fit, curvature, guide card,
// distance and place, the text panel's distance and the laser's hand. VR start
// sets none of them itself, and each setter only stores a value (a panel refit
// waits for the frame thread), so they are safe before the session exists.
constexpr options::Setting kPanelSettings[]={options::UiScale,options::UiMargin,options::UiCurve,
 options::UiGuide,options::UiDistance,options::UiOffsetRight,options::UiOffsetUp,options::UiText,
 options::PointerHand};
void ApplySavedPanelOptions(){ApplySaved(kPanelSettings,"panel_applied");}
void ServiceVrOptions(){
 if(!ready.load())return;
 std::optional<ViewRequest> view;
 {std::lock_guard lock(viewMutex);view=viewPending;viewPending.reset();}
 if(view) {
  TrackingFrame f{};
  const bool current=MonotonicNanoseconds()<view->deadline&&TryGetTrackingFrame(f)&&f.epoch==view->epoch;
  const auto result=current?RecenterHeadTracking(view->action==2):DWORD{1};
  std::lock_guard lock(viewMutex);
  // A newer click or teardown must not inherit the old action's retry/status.
  if(viewGeneration==view->generation){
   if(result==3&&MonotonicNanoseconds()<view->deadline)viewPending=view;
   else {
    viewResult=static_cast<int>(result)+(view->action==2?10:0);
    lifecycle::Log("preyvr_options view_action="+std::to_string(view->action)+" result="+std::to_string(result));
   }
  }
 }
 bool changed=false;
 for(unsigned i=0;i<options::Count;++i){
  if(!dirty[i].exchange(false))continue;
  const int v=values[i].load();
  Apply(i,v);
  {std::lock_guard lock(savedMutex);saved[i]=v;present[i]=true;}
  changed=true;
 }
 if(changed)Save();
}
bool ProcessVrOptionsInput(const TrackingFrame& f){
 options::Input i;
 i.valid=ready.load()&&FreshSample(MonotonicNanoseconds(),f.publishedNs)&&IsPoseUsable(f.head,f.headValidity,kMaxPoseAgeNs);
 i.modal=HudMenuStateKnown()&&HudMenuIsOpen();i.epoch=f.epoch;
 i.start=f.system.pause;i.accept=f.menu.confirm;i.cancel=f.menu.back;
 i.previous=f.menu.previousTab;i.next=f.menu.nextTab;i.chord=f.menu.bothGrips;
 // Navigation held is not "at rest" when the panel arms.
 i.x=f.menu.navigate.x;i.y=f.menu.navigate.y;
 const auto hand=UiPointerHand();i.trigger=hand<2?f.hands[hand].triggerPressed:false;
 i.dt=StepSeconds(previousTime,f.displayTime);
 previousTime=f.displayTime;
 // Intersect the current controller sample with the anchored panel; never use
 // a previous frame's hover location to decide a new trigger press.
 std::optional<ui::RayHit> hit;
 if(pointerSurface&&surfaceReference==HeadTrackingReferenceGeneration()&&hand<2&&
    IsPoseUsable(f.hands[hand].aimPose,f.hands[hand].aimValidity,kMaxPoseAgeNs)){
  hit=ui::Intersect(*pointerSurface,f.hands[hand].aimPose);
 }
 const auto wanted=request.exchange(0);
 if(wanted>0)menu.RequestOpen();else if(wanted<0)menu.Close();
 const bool wasOwned=menu.OwnsInput();
 menu.Update(i);
 if(menu.PauseTap())PostMenuAction(static_cast<unsigned>(input::MenuAction::Start),0);
 if(menu.Open())CaptureUiInput(f,hit,menu.Armed(),i.dt,i.trigger);
 else {uiInput={};grabbing=false;grabPush=0;}
 open=menu.Open();owned=menu.OwnsInput();
 if(wasOwned!=menu.OwnsInput())ClearUiPointer();
 return wasOwned||menu.OwnsInput(); // closing frame belongs to this menu too
}
UiPointerVisual OptionsPointer(const ui::Surface& surface){
 UiPointerVisual visual;TrackingFrame f{};const auto hand=UiPointerHand();
 if(!open.load()||hand>=2||!TryGetTrackingFrame(f)||!FreshSample(MonotonicNanoseconds(),f.publishedNs))return visual;
 const auto& state=f.hands[hand];
 if(!IsPoseUsable(state.aimPose,state.aimValidity,kMaxPoseAgeNs))return visual;
 visual.active=true;visual.aim=state.aimPose;visual.pressed=state.triggerPressed;
 visual.hit=ui::Intersect(surface,state.aimPose);
 return visual;
}
void SetVrOptionsSurface(const ui::Surface* surface,std::uint64_t reference){
 if(surface)pointerSurface=*surface;else pointerSurface.reset();
 surfaceReference=reference;
}
VrOptionsUiInput VrOptionsUiFrameInput(){return uiInput;}
std::vector<VrOptionsControlRow> VrOptionsControlRows(const std::vector<XrBoundControl>& bound){
 std::vector<VrOptionsControlRow> rows;
 const auto names=[&](input::Role role){
  std::string text;
  for(const auto& control:bound)if(control.role==role)for(const auto& source:control.sources)
   text+=(text.empty()?"":" / ")+source;
  return text;
 };
 // An optional action nothing binds (the Frame's D-pad on Touch) is left out;
 // a required one shows as not bound.
 const auto add=[&](input::ActionSet set){
  for(const auto& action:input::Actions()){
   if(action.set!=set||action.kind==input::Kind::pose||action.kind==input::Kind::haptic||
      action.kind==input::Kind::touch||action.role==input::Role::trigger||action.role==input::Role::squeeze)continue;
   std::string buttons=names(action.role);
   if(buttons.empty()&&!action.required)continue;
   rows.push_back({action.label,std::move(buttons),false});
  }
 };
 rows.push_back({"PLAY","",true});
 add(input::ActionSet::gameplay);
 rows.push_back({"Two-Hand Aim","Left grip, at a long weapon's foregrip",false});
 rows.push_back({"MENUS","",true});
 add(input::ActionSet::menu);
 // A controller without tab buttons (the Steam Frame's bumpers) has them on its grips.
 if(names(input::Role::previousTab).empty()&&names(input::Role::nextTab).empty())
  rows.push_back({"Tabs","Grips, on release",false});
 rows.push_back({"Pages","Also the trigger of the hand without the laser, on release",false});
 rows.push_back({"ANY TIME","",true});
 add(input::ActionSet::common);
 const std::string pause=names(input::Role::pause);
 rows.push_back({"Recenter",(pause.empty()?std::string("Menu"):pause)+" + both grips",false});
 rows.push_back({"To change any of these: SteamVR > Settings > Controllers > Manage Controller Bindings > Prey.",
  "",true});
 return rows;
}
VrOptionsGrab VrOptionsGrabState(){return {grabbing&&menu.Open(),grabSerial,grabPush};}
void ApplyVrOptionsUi(const VrOptionsUiResult& result){
 if(!menu.Open())return; // a frame drawn after closing changes nothing
 const auto hand=UiPointerHand();
 const Hand pulse=uiInput.click&&hand<2?static_cast<Hand>(hand):Hand::right;
 for(const auto& change:result.changes)HandleChange(change,uiFrame,pulse);
 if(result.grabStarted&&!grabbing&&!result.close&&hand<2) {
  grabbing=true;++grabSerial;
  QueueHaptic(static_cast<Hand>(hand),haptics::Event::MenuChange,uiFrame);
 }
 if(result.close){
  grabbing=false;grabPush=0;
  // Closed here rather than by request, so no further frame acts on it. The
  // menu keeps input until the controls are neutral, as a B press would.
  menu.Close();open=false;owned=menu.OwnsInput();uiInput={};ClearUiPointer();
 }
}
bool VrOptionsOpen(){return open.load();}
bool VrOptionsReady(){return ready.load();}
bool VrOptionsInputOwned(){return owned.load();}
void RequestVrOptions(bool on){request=on?1:-1;}
void ResetVrOptionsSession(){menu={};open=false;owned=false;request=0;{std::lock_guard lock(viewMutex);viewPending.reset();++viewGeneration;viewResult=-1;}pointerSurface.reset();previousTime=0;
 uiInput={};uiFrame={};stickUp=stickDown=stickLeft=stickRight=false;previousTab.Reset();nextTab.Reset();
 grabbing=false;grabPush=0;ClearUiPointer();}
bool VrOptionsSaveFailed(){return saveFailed.load();}
std::wstring VrSetupMessage(){
 const auto result=viewResult.load();
 if(result==-1)return L"";
 if(result==-2)return L"WAITING FOR CURRENT HEADSET POSE";
 if(result==0)return L"VR VIEW RESET / HEIGHT BASELINE PRESERVED";
 if(result==10)return L"CURRENT POSTURE CALIBRATED / VR VIEW RESET";
 if(result%10==2)return L"LOOK STRAIGHT AHEAD AND TRY AGAIN";
 if(result%10==3)return L"REFERENCE BUSY / TRY AGAIN";
 return L"NO CURRENT HEADSET POSE / TRY AGAIN";
}
bool PsychoscopeGestureEnabled(){return scope.load();}
bool HolstersEnabled(){return holsters.load();}
bool WristDisplayEnabled(){return wrist.load();}
unsigned WristSizePercent(){return wristSize.load();}
bool BeltHintsEnabled(){return beltHints.load();}
std::string VrOptionsReport(){return std::string(" open=")+std::to_string(open.load())+" inputOwned="+std::to_string(owned.load())+
 " saved="+std::to_string(!saveFailed.load())+" psychoscope="+std::to_string(scope.load())+
 " holsters="+std::to_string(holsters.load())+" wrist="+std::to_string(wrist.load())+
 " wristSize="+std::to_string(wristSize.load());}
}
