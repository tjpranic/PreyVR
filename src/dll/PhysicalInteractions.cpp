#include "PhysicalInteractions.h"
#include "HookInstall.h"
#include "EngineAccess.h"
#include "AimTakeover.h"
#include "BodyFollow.h"
#include "DebugOverlay.h"
#include "HeadTrackingHook.h"
#include "HudBridge.h"
#include "InputPost.h"
#include "SceneQuery.h"
#include "WeaponAttachment.h"
#include "Haptics.h"
#include "Logger.h"
#include "MinHookInit.h"
#include "preyvr/LatestSnapshot.h"
#include "preyvr/TextFormat.h"
#include "preyvr/PhysicalInteractions.h"
#include "preyvr/NativeContract.h"
#include "preyvr/PhysicalNative.h"
#include "preyvr/StereoCamera.h"
#include <MinHook.h>
#include <atomic>
#include <cstring>

namespace preyvr::dll {
namespace {
using namespace physical;
std::atomic<unsigned> melee{0},speed{120};
std::atomic<std::uint64_t> settings{1},strikes{0},substitutions{0},suppressed{0},refusals{0};
std::atomic<std::uint64_t> publications{0},sweeps{0},found{0},admissionRefused{0};
std::atomic<DWORD> error{0};std::atomic<bool> fault{false},installed{false};
struct Geometry {Sample sample{};RigIdentity rig{};std::uint64_t published=0,settings=0;bool wrench=false;};
LatestSnapshot<Geometry> geometry;
std::mutex publicationMutex;
Swing swing; // input-drain thread only, including the native callbacks below
HitOwnership ownership;
std::uint64_t seenSettings=0;
std::uintptr_t module=0;
struct Vector {scene::Hit* begin=nullptr;scene::Hit* end=nullptr;scene::Hit* capacity=nullptr;};
using Attack=bool(__fastcall*)(std::uintptr_t,unsigned,const void*,int,float);
using Hit=void(__fastcall*)(std::uintptr_t,float);
using GetHits=Vector*(__fastcall*)(std::uintptr_t,Vector*,std::uintptr_t,float);
using WeaponImpulse=void(__fastcall*)(void*,void*,const Vec3*,int,std::uintptr_t,float,float,float);
std::atomic<Attack> originalAttack{nullptr};std::atomic<Hit> originalHit{nullptr};
std::atomic<GetHits> originalGetHits{nullptr};std::atomic<WeaponImpulse> originalImpulse{nullptr};
struct Request {std::uintptr_t weapon=0;scene::Hit hit{};Vec3 direction{};};
thread_local Request request;
bool EngineThread(){return InputDrainThreadId()!=0&&InputDrainThreadId()==GetCurrentThreadId();}
bool __fastcall AttackHook(std::uintptr_t w,unsigned id,const void* name,int activation,float value){
    if(ownership.Suppress(w)&&ownership.BlockAttack(w,*reinterpret_cast<int*>(w+0x520),activation))return false;
    return originalAttack.load()(w,id,name,activation,value);
}
void __fastcall HitHook(std::uintptr_t w,float angle){
    // Animation events may arrive on another native thread. Suppression is
    // atomic and independent of input-drain thread identity or option state.
    if(request.weapon!=w&&ownership.Suppress(w)){++suppressed;return;}
    originalHit.load()(w,angle);
}
Vector* __fastcall GetHitsHook(std::uintptr_t component,Vector* out,std::uintptr_t w,float angle){
    if(EngineThread()&&request.weapon==w&&w&&component==w+0x4c8){
        *out={};
        // The consumer frees the vector. Allocate through its own append,
        // never return stack storage or memory from the mod's CRT heap.
        reinterpret_cast<void(__fastcall*)(Vector*,const scene::Hit*)>(module+native::AppendHit)(out,&request.hit);
        ++substitutions;return out;
    }
    return originalGetHits.load()(component,out,w,angle);
}
void __fastcall ImpulseHook(void* entity,void* collider,const Vec3* direction,int part,std::uintptr_t w,float force,float minMass,float maxMass){
    // Native wrapper negates its direction. Retain native force/stats and
    // material response, replacing only the body-facing direction during our hit.
    const Vec3 incoming=Scale(request.direction,-1);
    originalImpulse.load()(entity,collider,EngineThread()&&request.weapon==w&&w?&incoming:direction,part,w,force,minMass,maxMass);
}
// Every function called through a named RVA is one the gate hashes (the
// vtables are checked by slot below instead).
static_assert(contract::Gated(native::Contracts,native::Attack)&&contract::Gated(native::Contracts,native::WeaponHit)&&
              contract::Gated(native::Contracts,native::GetHits)&&contract::Gated(native::Contracts,native::AppendHit)&&
              contract::Gated(native::Contracts,native::WeaponImpulse)&&contract::Gated(native::Contracts,native::CharacterAabb));
bool Gate(){
    if(module)return true;
    const auto base=PreyBase();
    if(!base)return false;
    if(!contract::BodiesMatch(base,native::Contracts)){error=ERROR_BAD_EXE_FORMAT;fault=true;return false;}
    if(*reinterpret_cast<std::uintptr_t*>(base+0x1d22200+0x98)!=base+native::CharacterAabb){error=ERROR_INVALID_ADDRESS;fault=true;return false;}
    module=base;return true;
}
bool Install(){
    if(installed.load())return true;
    if(!EnsureMinHook())return false;
    void* originals[4]{};
    const hook::Spec hooks[]={
        {reinterpret_cast<void*>(module+native::Attack),reinterpret_cast<void*>(AttackHook),&originals[0]},
        {reinterpret_cast<void*>(module+native::WeaponHit),reinterpret_cast<void*>(HitHook),&originals[1]},
        {reinterpret_cast<void*>(module+native::GetHits),reinterpret_cast<void*>(GetHitsHook),&originals[2]},
        {reinterpret_cast<void*>(module+native::WeaponImpulse),reinterpret_cast<void*>(ImpulseHook),&originals[3]}};
    const auto hooked=hook::InstallAll(hooks,[&]{
        originalAttack=reinterpret_cast<Attack>(originals[0]);originalHit=reinterpret_cast<Hit>(originals[1]);
        originalGetHits=reinterpret_cast<GetHits>(originals[2]);originalImpulse=reinterpret_cast<WeaponImpulse>(originals[3]);
    });
    if(!hooked){error=ERROR_INVALID_FUNCTION;fault=true;return false;}
    installed=true;return true;
}
// What one contact did, for its log line. Plain data: Act fills it inside __try.
enum class Result:unsigned char{none,struck,notWrench,notInstalled,busy,claimRefused,notAdmitted,fault};
const char* ResultName(Result r){
    switch(r){
    case Result::struck:return "struck";case Result::notWrench:return "not_wrench";
    case Result::notInstalled:return "no_hooks";case Result::busy:return "weapon_busy";
    case Result::claimRefused:return "claim_refused";case Result::notAdmitted:return "not_admitted";
    case Result::fault:return "fault";
    default:return "none";
    }
}
struct Outcome {Result result=Result::none;int state=-1;};
bool Strike(std::uintptr_t weapon,const Contact& c,Outcome* o=nullptr){
    Outcome scratch{};auto& out=o?*o:scratch;
    if(*reinterpret_cast<std::uintptr_t*>(weapon)!=module+native::WrenchVtable){out.result=Result::notWrench;return false;}
    if(!installed.load()){out.result=Result::notInstalled;return false;}
    out.state=*reinterpret_cast<int*>(weapon+0x520);
    if(out.state!=0){out.result=Result::busy;return false;}
    // R8/name, EDX/entity and the value argument are not consumed by this
    // concrete implementation. Supply a stable empty name object; do not
    // manufacture a CryName hash. State transition, NOT bool return, admits it.
    static const std::uintptr_t emptyName=0;
    // Also cover an animation callback dispatched synchronously by PlayAnim.
    if(!ownership.Claim(weapon)){++admissionRefused;out.result=Result::claimRefused;return false;}
    originalAttack.load()(weapon,0,&emptyName,1,1.f);
    const int state=*reinterpret_cast<int*>(weapon+0x520);
    out.state=state;
    if(state!=1&&state!=2){ownership.Release(weapon);++admissionRefused;out.result=Result::notAdmitted;return false;}
    request={weapon,c.hit,c.direction};request.hit.next=nullptr;
    originalHit.load()(weapon,0.f);request={};++strikes;out.result=Result::struck;return true;
}
void Act(const Geometry& g,const Motion& motion,const Contact& c,Outcome& out){
    __try {
        if(!Gate()||!g.wrench||!melee.load()||!motion.strike)return;
        swing.Contact();Strike(g.rig.weapon,c,&out);
    }__except(EXCEPTION_EXECUTE_HANDLER){request={};fault=true;error=GetExceptionCode();++refusals;out.result=Result::fault;}
}
// The struck collider's class, for the log: its vtable and IPhysicalEntity
// GetType (slot +0x08, as CarryLane reads it; 1 static, 2 rigid, 4 living,
// 6 articulated). -2: unreadable.
struct ColliderInfo {std::uintptr_t vtable=0;int type=-1;};
ColliderInfo Describe(const void* collider){
    ColliderInfo info{};
    __try {
        const auto object=reinterpret_cast<std::uintptr_t>(collider);
        info.vtable=*reinterpret_cast<const std::uintptr_t*>(object);
        info.type=reinterpret_cast<int(__fastcall*)(std::uintptr_t)>(*reinterpret_cast<const std::uintptr_t*>(info.vtable+8))(object);
    }__except(EXCEPTION_EXECUTE_HANDLER){info.type=-2;}
    return info;
}
// --- the log (input-drain thread, except the geometry line) -----------------
void Log(const std::string& line){lifecycle::Log("preyvr_physical "+line);}
std::atomic<const char*> lastGate{"none"};
std::atomic<std::uint64_t> loggedRig{0};
bool loggedGate=false,loggedHooks=false,loggedFault=false;
std::uint64_t lastSequence=0,reportNs=0,reportSum=0;
const char* reportGate=nullptr;
// One line per armed swing that came near the strike speed, made a query or
// found a contact: what the gesture saw, so a swing that did nothing says why.
struct SwingLog {bool active=false;unsigned frames=0,fast=0,sweeps=0,contacts=0;float peak=0;};
SwingLog swingLog;
void EndSwing(const char* why){
    if(!swingLog.active)return;
    const auto s=swingLog;swingLog={};
    if(!s.sweeps&&!s.contacts&&s.peak*100<speed.load()*.6f)return;
    Log("swing peakCms="+std::to_string(std::lround(s.peak*100))+" needCms="+std::to_string(speed.load())+
        " frames="+std::to_string(s.frames)+" fastFrames="+std::to_string(s.fast)+" sweeps="+std::to_string(s.sweeps)+
        " contacts="+std::to_string(s.contacts)+" end="+why);
}
void NoteState(){
    if(!loggedGate&&module){loggedGate=true;Log("gate result=0 base="+Hex(module));}
    if(!loggedHooks&&installed.load()){loggedHooks=true;Log("hooks result=0 count=4");}
    if(!loggedFault&&fault.load()){loggedFault=true;Log("fault error="+std::to_string(error.load())+" hooks="+std::to_string(installed.load()));}
}
void LogContact(const Geometry& g,const Motion& motion,const Contact& c,const Outcome& o){
    const auto info=Describe(c.hit.collider);
    const auto vt=module&&info.vtable>=module?info.vtable-module:info.vtable;
    std::string line="contact kind="+std::string(g.wrench?"wrench":"weapon")+" speedCms="+std::to_string(std::lround(c.speed*100))+
        " swingCms="+std::to_string(std::lround(motion.speed*100))+" strike="+std::to_string(motion.strike)+
        " at="+Fixed(c.fraction,2)+" type="+std::to_string(info.type)+" vt="+Hex(vt)+
        " surface="+std::to_string(c.hit.surface)+" partId="+std::to_string(c.hit.partId)+
        " point="+Point(c.hit.point)+" normal="+Point(c.hit.normal)+" result="+ResultName(o.result);
    if(o.state>=0)line+=" state="+std::to_string(o.state);
    if(o.result==Result::fault)line+=" error="+std::to_string(error.load());
    Log(line);
}
// Every 10 s, when a counter or the refusing gate changed: the `physical`
// report, so a run's log shows where the pipeline stopped without the tuner.
void MaybeReport(){
    const auto now=MonotonicNanoseconds();
    if(now-reportNs<10000000000ull)return;
    reportNs=now;
    const auto sum=publications.load()+sweeps.load()+found.load()+strikes.load()+suppressed.load()+
                   refusals.load()+admissionRefused.load()+substitutions.load();
    const char* gate=lastGate.load();
    if(sum==reportSum&&gate==reportGate)return;
    reportSum=sum;reportGate=gate;Log("report"+PhysicalInteractionsReport());
}
bool Prepare(bool wrench){
    __try{return Gate()&&(!wrench||!melee.load()||Install());}
    __except(EXCEPTION_EXECUTE_HANDLER){fault=true;error=GetExceptionCode();return false;}
}
}
void PublishPhysicalWeapon(const GameplayPoseFrame& frame,const RigIdentity& rig,const animik::Location& location,
                           Pose wrist,const weaponrig::ContactGeometry& shape){
    if(!PhysicalInteractionsEnabled()||!frame.cameraCentreValid||!frame.headYawUsable||
       !std::isfinite(frame.yaw)||(frame.referenceGeneration&1)||
       !IsPoseUsable(frame.tracking.head,frame.tracking.headValidity,100000000)||
       !IsPoseUsable(frame.tracking.hands[1].gripPose,frame.tracking.hands[1].gripValidity,100000000)||
       frame.tracking.displayTime<=0||!std::isfinite(location.s)||location.s<=0||location.s>2)return;
    const Pose worldFromReference{{0,0,std::sin(frame.yaw*.5f),std::cos(frame.yaw*.5f)}, {}};
    Geometry g{};g.rig=rig;g.wrench=shape.wrench;g.settings=settings.load();
    auto& s=g.sample;s.worldFromReference=worldFromReference;
    s.worldFromReference.position=Sub(frame.cameraCentre,Rotate(worldFromReference.orientation,stereo::ToEngineSpace(frame.tracking.head.position)));
    const auto referenceFromWorld=Inverse(s.worldFromReference);
    const Pose model=Compose(wrist,shape.weaponInWrist);
    const Pose world{Multiply(location.q,model.orientation),animik::ModelToWorld(location,model.position)};
    const auto local=Bounds(shape.minimum,shape.maximum);
    for(unsigned i=0;i<local.size();++i)s.points[i]=Transform(referenceFromWorld,Transform(world,Scale(local[i],location.s)));
    s.grip=stereo::ToEngineSpace(frame.tracking.hands[1].gripPose);
    s.sequence=frame.tracking.sequence;s.stamp=static_cast<std::uint64_t>(frame.tracking.displayTime);
    s.owner=rig.generation;s.epoch=frame.tracking.epoch;s.reference=frame.referenceGeneration;
    g.published=frame.tracking.publishedNs;
    // Once per equip: the bounds the sweep uses for this weapon.
    if(auto seen=loggedRig.load();seen!=rig.generation&&loggedRig.compare_exchange_strong(seen,rig.generation))
        Log("geometry rig="+std::to_string(rig.generation)+" wrench="+std::to_string(shape.wrench)+" min="+Point(shape.minimum)+
            " max="+Point(shape.maximum)+" scale="+Fixed(location.s,2));
    std::lock_guard lock(publicationMutex);
    Geometry previous{};
    if(geometry.TryRead(previous)&&previous.sample.owner==s.owner&&previous.sample.epoch==s.epoch&&
       previous.sample.reference==s.reference&&previous.settings==g.settings&&previous.sample.sequence>=s.sequence)return;
    geometry.Publish(g);++publications;
}
// `overlay` (null unless the debug overlay draws the sweep) records what this
// frame's sweep saw: the box, the swept paths and any contact, in world space.
void UpdatePhysicalInteractionsImpl(const GameplayPoseFrame& frame,bool allowed,PhysicalOverlayState* overlay){
    MaybeReport();
    Geometry g{};EquippedRig owner{};
    const auto generation=settings.load();
    if(generation!=seenSettings){
        EndSwing("settings");swing.Reset();seenSettings=generation;
        Log("settings melee="+std::to_string(melee.load())+" speedCms="+std::to_string(speed.load()));
    }
    // The first gate that refuses this frame, in the order they are checked.
    // No swings from a head through a wall (BodyFollow.h).
    const char* why=nullptr;
    if(!allowed)why="tracking";
    else if(!PhysicalInteractionsEnabled())why="off";
    else if(!HudGameplayInputAllowed())why="menu";
    else if(BodyHeadOut())why="head_out";
    else if(!frame.cameraCentreValid||!frame.headYawUsable)why="camera";
    else if(!FreshSample(MonotonicNanoseconds(),frame.tracking.publishedNs,100000000)||
            !IsPoseUsable(frame.tracking.head,frame.tracking.headValidity,100000000)||
            !IsPoseUsable(frame.tracking.hands[1].gripPose,frame.tracking.hands[1].gripValidity,100000000))why="pose";
    else if(!geometry.TryRead(g)||!FreshSample(MonotonicNanoseconds(),g.published,100000000))why="no_geometry";
    // From here a weapon box exists; the rest decide whether it may query.
    const bool boxed=!why;
    if(!why){
        if(g.settings!=generation)why="settings";
        else if(g.sample.epoch!=frame.tracking.epoch||g.sample.reference!=frame.referenceGeneration||
                frame.referenceGeneration!=HeadTrackingReferenceGeneration())why="reference";
        else if(!TryGetEquippedRig(frame.player,owner)||owner.generation!=g.rig.generation||
                !SameRigBinding(owner,g.rig,owner.itemId))why="rig";
    }
    lastGate=why?why:"open";
    if(overlay){
        overlay->gate=why?why:"open";overlay->open=!why;overlay->box=boxed;
        if(boxed)for(unsigned i=0;i<g.sample.points.size();++i)overlay->points[i]=Transform(g.sample.worldFromReference,g.sample.points[i]);
    }
    if(why){EndSwing(why);swing.Reset();return;}
    const bool fresh=g.sample.sequence!=lastSequence;lastSequence=g.sample.sequence;
    const auto motion=swing.Update(g.sample,speed.load()*.01f);
    if(!motion.query){if(fresh)EndSwing("stopped");return;}
    // Only a swing that can do something: the wrench striking.
    const bool acts=g.wrench&&melee.load();
    if(acts){
        swingLog.active=true;++swingLog.frames;swingLog.peak=std::max(swingLog.peak,motion.speed);
        if(motion.strike)++swingLog.fast;
    }
    if(!acts||!motion.strike)return;
    // Hook installation can suspend threads. Do it BEFORE obtaining any native
    // collider pointer; a contact never survives an install or a frame boundary.
    if(!Prepare(g.wrench)){NoteState();++refusals;EndSwing("prepare_refused");swing.Reset();return;}
    NoteState();
    ++sweeps;++swingLog.sweeps;
    if(overlay){overlay->swept=true;overlay->from=motion.from;overlay->to=motion.to;}
    Contact c{};
    if(!Sweep(motion,[&](Vec3 origin,Vec3 delta,scene::Hit& hit){return QueryPhysicalSegment(frame,origin,delta,hit);},c)){
        ++refusals;EndSwing("query_refused");swing.Reset();return;
    }
    if(!c.found)return;
    ++found;++swingLog.contacts;
    EquippedRig current{};
    if(!HudGameplayInputAllowed()||!TryGetEquippedRig(frame.player,current)||current.generation!=g.rig.generation||
       !SameRigBinding(current,g.rig,current.itemId)||HeadTrackingReferenceGeneration()!=frame.referenceGeneration){
        ++refusals;EndSwing("rig_changed");swing.Reset();return;
    }
    Outcome outcome{};
    const auto before=strikes.load();Act(g,motion,c,outcome);
    if(strikes.load()!=before)QueueHaptic(Hand::right,haptics::Event::WeaponContact,frame.tracking);
    LogContact(g,motion,c,outcome);NoteState();
    if(overlay){
        overlay->contact=true;overlay->acted=outcome.result==Result::struck;
        overlay->point=c.hit.point;overlay->normal=c.hit.normal;overlay->result=ResultName(outcome.result);
    }
}
void UpdatePhysicalInteractions(const GameplayPoseFrame& frame,bool allowed){
    thread_local bool busy=false;
    if(busy||!EngineThread())return;
    struct Guard{bool& value;explicit Guard(bool& v):value(v){value=true;}~Guard(){value=false;}} guard(busy);
    const bool drawn=DebugOverlayWantsPhysical();
    PhysicalOverlayState overlay{};
    UpdatePhysicalInteractionsImpl(frame,allowed,drawn?&overlay:nullptr);
    if(!drawn)return;
    overlay.status="wrench "+std::string(melee.load()?"on":"off")+"  gate "+overlay.gate+"  sweeps "+std::to_string(sweeps.load())+
        "  contacts "+std::to_string(found.load())+"  struck "+std::to_string(strikes.load());
    DebugOverlayPhysical(frame,overlay);
}
bool PhysicalInteractionsEnabled(){return !fault.load()&&melee.load();}
void SetPhysicalMelee(unsigned v){melee=v!=0;++settings;}unsigned PhysicalMeleeEnabled(){return melee.load();}
void SetSwingSpeed(unsigned v){speed=std::clamp(v,80u,250u);++settings;}unsigned SwingSpeed(){return speed.load();}
std::string PhysicalInteractionsReport(){return " melee="+std::to_string(melee.load())+
    " speedCms="+std::to_string(speed.load())+" hooks="+std::to_string(installed.load())+
    " fault="+std::to_string(fault.load())+" strikes="+std::to_string(strikes.load())+" substituted="+std::to_string(substitutions.load())+
    " poses="+std::to_string(publications.load())+" sweeps="+std::to_string(sweeps.load())+" contactsFound="+std::to_string(found.load())+
    " admissionRefused="+std::to_string(admissionRefused.load())+
    " suppressed="+std::to_string(suppressed.load())+" refused="+std::to_string(refusals.load())+
    " error="+std::to_string(error.load())+" gate="+lastGate.load();}
}
