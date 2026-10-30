#include "preyvr/VrOptions.h"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <iterator>
#include <sstream>
namespace preyvr::options {
namespace {
constexpr int turnChoices[]={0,15,30,45,60,90};
constexpr const wchar_t* headRelativeNames[]={L"BODY RELATIVE",L"HEAD RELATIVE"};
constexpr const wchar_t* gripToggleNames[]={L"HOLD",L"TOGGLE"};
constexpr const wchar_t* supportSnapNames[]={L"FREE",L"SNAPPED"};
constexpr const wchar_t* psiNames[]={L"ORIGINAL",L"HEAD",L"LEFT CONTROLLER"};
constexpr const wchar_t* pointerNames[]={L"LEFT",L"RIGHT",L"BUTTONS ONLY"};
constexpr const wchar_t* hudNames[]={L"IN THE WORLD",L"FLOATING PANEL"};
constexpr const wchar_t* reticleNames[]={L"ON THE HUD",L"IN THE WORLD"};
constexpr const wchar_t* carryHoldNames[]={L"PRESS TO TOGGLE",L"HOLD TO CARRY"};
constexpr const wchar_t* grenadeNames[]={L"GAME THROW",L"HAND THROW"};
constexpr const wchar_t* antialiasingNames[]={L"SMAA",L"TEMPORAL"};
constexpr const wchar_t* helmetNames[]={L"HIDDEN",L"SHOWN"};
using U=Unit;
constexpr Spec specs[]={
 {"turn",L"TURNING",L"Smooth turning or fixed angular steps.",45,0,90,15,U::degrees,{},turnChoices,L"SMOOTH"},
 {"head_relative",L"MOVEMENT DIRECTION",L"Follow your view, or the character's forward direction.",1,0,1,1,U::none,headRelativeNames},
 {"turn_speed",L"SMOOTH TURN SPEED",L"Scales the game's turn rate. Applies in Smooth mode.",100,10,200,25,U::percent},
 {"two_hand",L"TWO-HAND AIM",L"Grip a long weapon's foregrip to aim it, or a pistol's grip to brace it.",1,0,1,1},
 {"grip_toggle",L"FOREGRIP INPUT",L"Hold the grip, or squeeze once to attach and again to release.",0,0,1,1,U::none,gripToggleNames},
 {"support_snap",L"SUPPORT HAND",L"Snap the displayed hand to the weapon, or follow your controller.",1,0,1,1,U::none,supportSnapNames},
 {"psychoscope",L"PSYCHOSCOPE GESTURE",L"Left grip at forehead: pull down or lift up. Native unlock rules apply.",0,0,1,1},
 {"ui_scale",L"INTERFACE SIZE",L"Scales game panels and HUD within the visible-area limit.",100,20,200,10,U::percent},
 {"ui_margin",L"VISIBLE-AREA LIMIT",L"Larger values approach the lens edges. Tune while wearing the headset.",72,30,100,5,U::percent},
 {"ui_curve",L"PANEL CURVATURE",L"Curves supported mono menus. Experimental stereo PDA stays planar.",35,0,60,5,U::degrees,{},{},L"FLAT"},
 {"ui_guide",L"CONTROLS GUIDE",L"Shows a help card below native menus; uses some panel space.",0,0,1,1},
 {"holsters",L"BODY HOLSTERS",L"Right grip at right hip or left chest: store current weapon, then draw/stow. Assignments are not saved.",0,0,1,1},
 {"wrist",L"WRIST STATUS",L"Raise your left forearm and look at its back, like a watch: health, suit and psi.",1,0,1,1},
 {"wrist_size",L"WRIST DISPLAY SIZE",L"Adjust the status hologram size. Hidden in menus and during two-hand aiming.",100,80,140,10,U::percent},
 {"reticle_world",L"RETICLE",L"In the world: the crosshair sits on what you aim at, so it stays sharp when you look there. On the HUD: on the floating HUD panel. Needs the HUD on its floating panel.",1,0,1,1,U::none,reticleNames},
 {"reticle_size",L"RETICLE SIZE",L"The crosshair in the world, against its size on the HUD.",100,50,200,10,U::percent},
 {"reticle_fallback_mm",L"RETICLE FALLBACK DISTANCE",L"How far the crosshair sits when your aim hits nothing within 200 m.",10000,500,200000,500,U::metresInMm},
 {"marker_size",L"MARKER SIZE",L"The markers in the world: objectives, repair and danger icons, enemy awareness and points of interest.",100,50,200,10,U::percent},
 {"haptics",L"CONTROLLER FEEDBACK",L"Short pulses for VR menu changes and attaching the support hand.",1,0,1,1},
 {"haptic_strength",L"FEEDBACK STRENGTH",L"Scale controller feedback intensity. Zero silences all pulses.",70,0,100,10,U::percent},
 {"free_swing",L"FREE-SWING WRENCH",L"Swing the wrench to strike what it hits: harder swings hit harder, and Prey's damage, knockback and stamina apply. The attack button does nothing with the wrench out.",1,0,1,1},
 {"swing_speed",L"MINIMUM SWING SPEED",L"How fast the wrench must move where it hits to strike. Slower touches only push. At or above the heavy swing speed, every strike is heavy.",120,100,700,10,U::cmPerSecond},
 {"heavy_swing_speed",L"HEAVY SWING SPEED",L"From this speed where the wrench hits, a swing lands Prey's charged hit: more damage and knockback, more stamina. Raise it to keep heavy hits for your hardest swings.",700,300,1500,50,U::cmPerSecond},
 {"physical_contact",L"PHYSICAL CONTACT",L"Your weapon and free hands push loose objects and bodies: light ones keep up, heavy ones resist. Shown in the debug overlay.",1,0,1,1},
 {"push_strength",L"PUSH STRENGTH",L"How hard your weapon and hands push. Light objects keep up either way; heavier ones move sooner with more.",100,25,200,25,U::percent},
 {"psi_target",L"PSYCHIC TARGETING",L"The left trigger casts the equipped power. Original aims it as Prey does; Head or Left Controller aim it with your head or left hand.",0,0,2,1,U::none,psiNames},
 {"medkit_slot",L"LEFT-HIP MEDKIT SLOT",L"Squeeze left grip at left hip, then press left trigger while holding grip. One medkit per grab; native use rules apply.",0,0,1,1},
 {"belt_hints",L"BODY SLOT FEEDBACK",L"Brief feedback for holster requests and medkit selection/use. Requires the corresponding body slot feature.",1,0,1,1},
 {"debug_overlay",L"DEBUG OVERLAY (TESTING)",L"Draws each hand's ray and target, body slots, the wrist test, the foregrip region and the weapon's contact sweep in the world.",0,0,1,1},
 {"muzzle_aim",L"SHOTS FROM THE MUZZLE",L"Shots leave the weapon's muzzle along your controller's aim, so they land where the barrel points at any distance.",0,0,1,1},
 {"ui_distance",L"MENU DISTANCE",L"How far away the game's menus float. They keep their apparent size.",200,100,400,5,U::cm},
 {"ui_offset_right",L"MENU LEFT / RIGHT",L"Turns the game's menus around you, left or right of where you faced when they opened.",0,-90,90,1,U::degreesRightLeft,{},{},L"CENTRE"},
 {"ui_offset_up",L"MENU UP / DOWN",L"Raises or lowers the game's menus; they stay upright.",0,-45,45,1,U::degreesUpDown,{},{},L"CENTRE"},
 {"ui_text",L"TEXT PANEL DISTANCE",L"How far notes, books, subtitles and tutorial cards float. Nearer is larger and sharper.",140,100,200,5,U::cm},
 {"pointer_hand",L"LASER HAND",L"Which hand points at menus. Buttons only turns the laser off.",1,0,2,1,U::none,pointerNames},
 {"hud_layer",L"HUD STYLE",L"The game's HUD on a panel floating in front of you, or drawn in the world as Prey draws it.",1,0,1,1,U::none,hudNames},
 {"move_deadzone",L"MOVE STICK DEADZONE",L"How far the left stick moves before you walk.",15,0,60,1,U::percent},
 {"turn_deadzone",L"TURN STICK DEADZONE",L"How far the right stick moves before you turn.",15,0,60,1,U::percent},
 {"strafe_speed",L"SIDEWAYS SPEED",L"Walking speed to the side, relative to walking forward.",100,10,200,5,U::percent},
 {"backward_speed",L"BACKWARD SPEED",L"Walking speed backward, relative to walking forward.",100,10,200,5,U::percent},
 {"arms_free",L"HANDS WITHOUT A WEAPON",L"Keep both hands at the controllers with no weapon drawn: holstered or carrying.",1,0,1,1},
 {"carry_hold",L"CARRY",L"Press X once to pick up and again to drop, as Prey does, or hold X to keep holding.",0,0,1,1,U::none,carryHoldNames},
 {"grenade_hand",L"GRENADE THROW",L"Throw grenades with your hand's motion, or with the game's charged throw along your view.",1,0,1,1,U::none,grenadeNames},
 {"carry_throw_strength",L"OBJECT THROW STRENGTH",L"How hard carried objects fly when you throw them.",140,50,300,10,U::percent},
 {"grenade_throw_strength",L"GRENADE THROW STRENGTH",L"How hard grenades fly when you throw them.",130,50,400,10,U::percent},
 {"wrist_on_hud",L"STATUS ON HUD TOO",L"Keep health, suit and psi in front of you as well as on the wrist.",0,0,1,1},
 {"wrist_tick",L"WRIST TICK",L"A faint pulse on the left controller as the wrist display unfolds.",1,0,1,1},
 {"wrist_along",L"WRIST DISPLAY POSITION",L"Where the display sits along the forearm, from the wrist toward the elbow.",75,-50,200,5,U::mm},
 {"wrist_hover",L"WRIST DISPLAY HEIGHT",L"How far the display floats above the forearm.",42,0,150,2,U::mm},
 {"wrist_tilt",L"WRIST DISPLAY TILT",L"How far the display turns toward your eyes.",40,0,90,5,U::degrees},
 {"carry_hand",L"PHYSICAL CARRYING",L"Carried objects sit in your left hand and are thrown with its motion. Off: the game's own carry in front of you.",1,0,1,1},
 {"grenade_deploy",L"STICK CHARGES TO SURFACES",L"With hand throwing, keep the game's sticking of charges to the surface you look at.",0,0,1,1},
 {"long_reach",L"LONG-WEAPON HOLD-BACK",L"Long weapons are held back toward the body, with a wider foregrip region and torso twist.",1,0,1,1},
 {"inventory_layer",L"INVENTORY LAYER",L"The inventory on a see-through layer of its own instead of the whole flat screen.",0,0,1,1},
 {"inventory_stereo",L"STEREO INVENTORY",L"Gives the inventory's layers depth. Needs Inventory Layer.",0,0,1,1},
 {"inventory_depth",L"INVENTORY DEPTH",L"How much depth Stereo Inventory gives.",25,0,100,5,U::percent},
 {"finger_curl",L"FINGERS FOLLOW THE CONTROLLER",L"A free hand's fingers follow yours: the thumb on a button, the index on the trigger, the rest with the grip.",1,0,1,1},
 {"aim_lasers",L"AIM LASERS",L"A laser from each hand to what it points at: where the right hand aims, and what the left hand can grab or use.",1,0,1,1},
 {"controller_prompts",L"FORCE CONTROLLER PROMPTS",L"Game menus show your controller's buttons while the laser points at them. Off: the laser switches them to mouse and keyboard.",1,0,1,1},
 // The arm lane takes the same 40-70 (AnimIkTakeover's arm.length).
 {"arm_length",L"ARM LENGTH",L"Your arm from shoulder to wrist. Lower it if your arms look bent when yours are straight; raise it if your hands stop short of the controllers.",56,40,70,1,U::cm},
 {"surface_stop",L"HANDS STOP AT SURFACES",L"Your drawn hands and weapon stop against walls, floors, desks and glass, though your real hands go on. Loose objects are still pushed.",1,0,1,1},
 {"heavy_mass",L"HEAVY OBJECTS",L"Loose objects heavier than this stop your hands and weapon as walls do, and slide when you press into them. Lighter ones are pushed as before. Needs Hands Stop at Surfaces.",10,1,100,1,U::kg},
 // The room-scale and body lanes take the same 0-50 and 0-20 (RoomScaleLane.h, AnimIkTakeover.h).
 {"lean_zone",L"LEAN ZONE",L"How far your head can lean out before your body steps after it. Within it your torso bends to follow you; stand still and upright and the body catches up under you. Off: it follows your head at once.",25,0,50,5,U::cm,{},{},L"OFF"},
 {"hips_back",L"HIPS AND LEGS BACK",L"Moves your drawn hips and legs back, under your head rather than under your eyes. Raise it if your body seems to lean back when you look down; lower it if your legs seem to be behind you.",8,0,20,1,U::cm},
 // The view takes the same 1-10, default 3 (preyvr/NearClip.h; near_clip pins it).
 {"near_clip",L"NEAR CLIP DISTANCE",L"How close to your eyes things are still drawn. Lower it if you can see through your own body when you look down; raise it if distant surfaces flicker. Prey's own is 10 cm.",3,1,10,1,U::cm},
 // The examination camera takes the same 0-100, default 30 (preyvr/ExaminePullback.h; examine_pullback pins it).
 {"screen_pullback",L"SCREEN DISTANCE",L"How far your view stays back from keypads, terminals and other screens you use. Prey brings a screen close enough to fill your whole view, which in the headset is a hand's width from your eyes. Never further back than you were standing.",30,0,100,5,U::cm,{},{},L"PREY'S OWN"},
 // Temporal is TSAA with each eye's own history (TemporalAaLane.h); SMAA is SMAA 1X.
 {"antialiasing",L"ANTI-ALIASING",L"Temporal smooths the jagged, crawling edges of distant objects by blending each eye's recent frames, at some cost in frame rate. SMAA smooths each frame on its own: faster, but distant edges shimmer as you move.",1,0,1,1,U::none,antialiasingNames},
 // Shown sits it on your head (Helmet.h); Hidden takes it out of the render alone.
 {"helmet",L"SPACE HELMET",L"The helmet Morgan wears in zero gravity and outside the station. Shown: it sits on your head and moves with it. Hidden: it is not drawn.",1,0,1,1,U::none,helmetNames},
 // The helmet lane takes the same -100..100 mm and 50-200% (Helmet.h): moved along your eyes' own axes, sized
 // around its centre.
 {"helmet_height",L"HELMET HEIGHT",L"Raises or lowers the helmet around your eyes. Lower it if you can see out of its bottom edge.",0,-100,100,5,U::mmUpDown,{},{},L"PREY'S OWN"},
 {"helmet_forward",L"HELMET FORWARD / BACK",L"Moves the helmet forward or back around your eyes. Back brings its edges further round you.",0,-100,100,5,U::mmForwardBack,{},{},L"PREY'S OWN"},
 {"helmet_size",L"HELMET SIZE",L"Scales the helmet around its centre. Larger looks further away; it covers more or less of your view only once it has been moved.",100,50,200,5,U::percent},
 // The move lane takes the same 0 or 15-90 as Turning (MoveLane.h).
 {"zerog_roll",L"ZERO-G ROLL",L"In zero gravity the bumpers roll you left and right, and both together brake. Smooth rolls while held, as the game does; or one fixed step a press.",0,0,90,15,U::degrees,{},turnChoices,L"SMOOTH"},
 {"zerog_pitch",L"ZERO-G PITCH",L"In zero gravity the right stick up and down tips you forward and back. Smooth tips while held, as the game does; or one fixed step a push.",0,0,90,15,U::degrees,{},turnChoices,L"SMOOTH"}
};
static_assert(std::size(specs)==Count,"one Spec per Setting, in order");
// Each default, name and choice belongs to its setting's range, one name per value.
constexpr bool Consistent(){
 for(const auto& s:specs){
  if(s.initial<s.min||s.initial>s.max||s.step<=0)return false;
  if(!s.names.empty()&&static_cast<int>(s.names.size())!=(s.max-s.min)/s.step+1)return false;
  for(const int c:s.choices)if(c<s.min||c>s.max)return false;
 }
 return true;
}
static_assert(Consistent(),"every Spec describes its own range");

// The panel's tabs. Each setting appears once, each button once.
constexpr int comfort[]={Turn,TurnSpeed,HeadRelative,MoveDeadzone,TurnDeadzone,StrafeSpeed,BackwardSpeed,ZeroGRoll,
    ZeroGPitch};
constexpr int hands[]={AimLasers,TwoHand,GripToggle,SupportSnap,Psychoscope,ArmsFree,FingerCurl,ArmLength,CarryHold,GrenadeHand,
    CarryThrowStrength,GrenadeThrowStrength,SurfaceStop,HeavyMass,PhysicalContact,PushStrength,FreeSwing,SwingSpeed,HeavySwingSpeed};
constexpr int interface_[]={UiScale,UiMargin,UiCurve,UiGuide,UiDistance,UiOffsetRight,UiOffsetUp,UiText,ScreenPullback,
    PointerHand,ControllerPrompts,HudLayer,ReticleWorld,ReticleSize,ReticleFallback,MarkerSize,Helmet,HelmetHeight,HelmetForward,HelmetSize};
constexpr int equipment[]={Holsters,ClearHolsterSlots,Wrist,WristSize,WristOnHud,WristTick,WristAlong,WristHover,
    WristTilt};
constexpr int feedback[]={Haptics,HapticStrength};
constexpr int abilities[]={PsiTarget,Medkit,BeltHints};
constexpr int setup[]={CalibratePosture,RecenterView,LeanZone,HipsBack,NearClip,Antialiasing,DebugOverlay};
// Features still being proven, together with the settings that tune them.
constexpr int experimental[]={CarryHand,GrenadeDeploy,LongReach,MuzzleAim,
    InventoryLayer,InventoryStereo,InventoryDepth};
// CONTROLS holds no setting: the panel draws what each action is bound to there.
constexpr unsigned kControlsPage=7;
constexpr std::span<const int> menuPages[]={comfort,hands,interface_,equipment,feedback,abilities,setup,
    std::span<const int>{},experimental};
constexpr const wchar_t* menuNames[]={L"COMFORT",L"HANDS",L"INTERFACE",L"EQUIPMENT",L"FEEDBACK",L"ABILITIES",
    L"SETUP",L"CONTROLS",L"EXPERIMENTAL"};
static_assert(kControlsPage<std::size(menuPages)&&menuPages[kControlsPage].empty());
static_assert(std::size(menuPages)==std::size(menuNames));
}
unsigned MenuPages(){return static_cast<unsigned>(std::size(menuPages));}
unsigned ControlsPage(){return kControlsPage;}
const wchar_t* MenuPageName(unsigned page){return menuNames[page%MenuPages()];}
std::span<const int> MenuEntries(unsigned page){return page<MenuPages()?menuPages[page]:std::span<const int>{};}
const Spec& Describe(unsigned id){return specs[id<Count?id:0];}
bool Valid(unsigned id,int v){
 if(id>=Count||v<specs[id].min||v>specs[id].max)return false;
 const auto& c=specs[id].choices;
 return c.size()<2||v==c[0]||v>=c[1];
}
Kind KindOf(unsigned id){
 const auto& s=Describe(id);
 if(!s.names.empty()||!s.choices.empty())return Kind::choice;
 return s.min==0&&s.max==1&&s.unit==Unit::none?Kind::toggle:Kind::range;
}
std::vector<int> Choices(unsigned id){
 const auto& s=Describe(id);
 if(!s.choices.empty())return {s.choices.begin(),s.choices.end()};
 std::vector<int> out;
 if(!s.names.empty())for(int v=s.min;v<=s.max;v+=s.step)out.push_back(v);
 return out;
}
std::wstring Display(unsigned id,int v){
 const auto& s=Describe(id);
 if(!s.names.empty()){
  const int n=(v-s.min)/s.step;
  return v>=s.min&&n<static_cast<int>(s.names.size())?s.names[static_cast<std::size_t>(n)]:std::to_wstring(v);
 }
 if(v==0&&s.zero)return s.zero;
 const auto number=std::to_wstring(v);
 switch(s.unit){
 case U::percent:return number+L"%";
 case U::cm:return number+L" CM";
 case U::mm:return number+L" MM";
 case U::metresInMm:return std::to_wstring(v/1000)+L"."+std::to_wstring((v%1000)/100)+L" M";
 case U::degrees:return number+L" DEGREES";
 case U::degreesRightLeft:return std::to_wstring(std::abs(v))+(v>0?L" DEGREES RIGHT":L" DEGREES LEFT");
 case U::degreesUpDown:return std::to_wstring(std::abs(v))+(v>0?L" DEGREES UP":L" DEGREES DOWN");
 case U::mmUpDown:return std::to_wstring(std::abs(v))+(v>0?L" MM UP":L" MM DOWN");
 case U::mmForwardBack:return std::to_wstring(std::abs(v))+(v>0?L" MM FORWARD":L" MM BACK");
 case U::cmPerSecond:return number+L" CM/S";
 case U::kg:return number+L" KG";
 case U::none:break;
 }
 return v?L"ON":L"OFF";
}
Values Defaults(){Values v{};for(unsigned i=0;i<Count;++i)v[i]=specs[i].initial;return v;}
int Default(unsigned id){return Describe(id).initial;}
std::string Serialize(const Values& v,const Present* present){
 std::string s="version=1\n";
 for(unsigned i=0;i<Count;++i)if(!present||(*present)[i])s+=std::string(specs[i].key)+"="+std::to_string(v[i])+"\n";
 return s;
}
bool Parse(std::string_view text,Values& result,Present* present){
 if(text.size()>8192)return false;
 Values parsed=Defaults();std::array<bool,Count> seen{};bool version=false;
 std::istringstream in{std::string(text)};std::string line;
 while(std::getline(in,line)){
  if(!line.empty()&&line.back()=='\r')line.pop_back();
  if(line.empty())continue;
  const auto sep=line.find('=');if(sep==std::string::npos)return false;
  int value=0;const auto first=line.data()+sep+1,last=line.data()+line.size();
  const auto [end,err]=std::from_chars(first,last,value);
  if(err!=std::errc{}||end!=last)return false;
  const auto key=line.substr(0,sep);
  if(key=="version"){if(version||value!=1)return false;version=true;continue;}
  // Retired settings: skipped, so a file saved before they went still loads.
  // The next save leaves them out. scene_reticle: the world reticle runs the
  // scene query itself (reticle_world). physical_contacts, contact_strength:
  // Object Nudges, removed 2026-10-18 for a redesign. physical_melee: the
  // Physical Wrench, replaced by the free-swing wrench (free_swing) 2026-10-18.
  // body_follow, body_turn_deadzone, body_step, body_glide: Body Follows You,
  // removed 2026-10-24 (smooth turning jittered with it on).
  if(key=="scene_reticle"||key=="physical_contacts"||key=="contact_strength"||key=="physical_melee"||
     key=="body_follow"||key=="body_turn_deadzone"||key=="body_step"||key=="body_glide")continue;
  // swing_speed took 80-250 until 2026-10-18; a file saved at 80 or 90 rises to the new minimum.
  if(key=="swing_speed"&&value>=80&&value<100)value=100;
  unsigned i=0;for(;i<Count&&key!=specs[i].key;++i){}
  if(i==Count||seen[i]||!Valid(i,value))return false;
  parsed[i]=value;seen[i]=true;
 }
 if(!version)return false;
 // Partial files preserve explicit defaults; future versions must bump version.
 result=parsed;if(present)*present=seen;return true;
}
void Menu::Close(){open_=false;quarantine_=true;armed_=false;requested_=false;}
void Menu::Update(const Input& i){
 pauseTap_=false;
 const bool valid=i.valid&&std::isfinite(i.x)&&std::isfinite(i.y)&&std::isfinite(i.dt)&&i.dt>0&&i.dt<=.2f;
 const bool neutral=!i.start&&!i.accept&&!i.cancel&&!i.previous&&!i.next&&!i.trigger&&std::fabs(i.x)<.25f&&std::fabs(i.y)<.25f;
 if(!valid||i.epoch!=epoch_){
  epoch_=i.epoch;Close();startArmed_=false;startUsed_=false;startWas_=false;hold_=0;return;
 }
 // Menu with both grips held is the recenter chord: this press is spent.
 if(i.start&&i.chord)startUsed_=true;
 if(!i.start){
  if(startWas_&&startArmed_&&!startUsed_&&!open_&&!quarantine_)pauseTap_=true;
  hold_=0;startArmed_=true;startUsed_=false;
 }
 else if(startArmed_&&!startUsed_)hold_+=i.dt;
 startWas_=i.start;
 // The same hold in play and in a game menu: open over what is shown, without
 // pausing the game. Opening uses the hold, so its release is no pause tap.
 if(!open_&&((hold_>=.65f&&!startUsed_&&!quarantine_)||requested_)){
  open_=true;quarantine_=true;armed_=false;requested_=false;startUsed_=true;overMenu_=i.modal;
 }
 // A game menu opening under it, or the one it was opened over closing.
 if(open_&&i.modal!=overMenu_)Close();
 if(!open_){if(neutral)quarantine_=false;return;}
 // Armed at rest, so B is a fresh press.
 if(!armed_){if(neutral)armed_=true;return;}
 if(i.cancel)Close();
}
}
