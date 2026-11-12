#include "equipment/psi/PsiMedkit.h"
#include "engine/hooks/HookInstall.h"
#include "engine/EngineAccess.h"
#include "aim/GameplayFrame.h"
#include "render/camera/HeadTrackingHook.h"
#include "ui/screens/UiMode.h"
#include "controls/input/InputPost.h"
#include "engine/hooks/MinHookInit.h"
#include "aim/ReticleRayHook.h"
#include "equipment/psi/PsiTargeting.h"
#include "render/camera/StereoCamera.h"
#include "engine/NativeContract.h"
#include "equipment/psi/PsiMedkitNative.h"
#include "equipment/psi/PsychoscopeNative.h"
#include "base/MonotonicClock.h"
#include <MinHook.h>
#include <atomic>
#include <cmath>
#include <iterator>
#include <mutex>

namespace preyvr::dll {
namespace {
namespace native=abilities::native;
std::atomic<unsigned> leftHand{0}; // Psi Aim Hand is Left
std::atomic<std::uint64_t> generation{1};
std::atomic<bool> installed{false},fault{false};
std::atomic<unsigned long long> previews{0},rayReads{0},casts{0},rejectedCasts{0},currentRayCasts{0};
// When Prey's targeting last read a VR ray for a selected power (PsiAimingLeftHand).
std::atomic<std::uint64_t> aimingNs{0};
std::atomic<DWORD> lastError{0};
std::mutex installMutex;
using UpdateFn=void(__fastcall*)(std::uintptr_t,float);
using ActivateFn=bool(__fastcall*)(std::uintptr_t);
using SelectFn=bool(__fastcall*)(std::uintptr_t,int);
using CandidateFn=bool(__fastcall*)(std::uintptr_t,std::uintptr_t,bool);
UpdateFn originalUpdate=nullptr;
ActivateFn originalActivate=nullptr;
SelectFn originalSelect=nullptr;
CandidateFn originalCandidate=nullptr;
thread_local psi::Context* context=nullptr;
thread_local psi::Sample preview{};

struct Owner {std::uintptr_t player=0,power=0;int selected=-1;std::uintptr_t selectedPower=0;};
DWORD ReadOwner(Owner* out){
 __try {
    if(!OnEngineMainThread())return ERROR_INVALID_THREAD_ID;
    const auto base=PreyBase();
    if(!base)return ERROR_NOT_READY;
    out->player=reinterpret_cast<std::uintptr_t(__fastcall*)()>(base+native::GetPlayer)();
    if(!out->player)return ERROR_NOT_READY;
    const auto psiOwner=*reinterpret_cast<const std::uintptr_t*>(out->player+0x678);
    if(!psiOwner)return ERROR_NOT_READY;
    out->power=reinterpret_cast<std::uintptr_t(__fastcall*)(std::uintptr_t)>(base+native::GetPower)(out->player);
    if(out->power!=psiOwner+8)return ERROR_INVALID_ADDRESS;
    out->selected=*reinterpret_cast<const int*>(out->power+0x230);
    if(out->selected>=0&&out->selected<16)
        out->selectedPower=*reinterpret_cast<const std::uintptr_t*>(out->power+0x40+8*out->selected);
    return ERROR_SUCCESS;
 }__except(EXCEPTION_EXECUTE_HANDLER){return GetExceptionCode();}
}
bool MakeContext(std::uintptr_t component,psi::Context& ctx){
    GameplayPoseFrame f{};Owner owner{};
    if(!leftHand.load()||fault.load()||!HudGameplayInputAllowed()||ReadOwner(&owner)||owner.power!=component||
       !TryGetGameplayPoseFrame(f)||f.player!=owner.player||!f.cameraCentreValid||!f.headYawUsable||
       !FreshSample(MonotonicNanoseconds(),f.tracking.publishedNs,100000000)||
       !IsPoseUsable(f.tracking.head,f.tracking.headValidity,100000000)||
       !stereo::ReferenceCurrent(f.referenceGeneration,HeadTrackingReferenceGeneration()))return false;
    const auto& left=f.tracking.hands[0];
    auto ray=psi::HandRay(PlaySpaceRotation(f),f.cameraCentre,f.tracking.head,FingerPose(f,Hand::left),left.aimValidity);
    if(!ray)return false;
    // From the drawn hand: held back by a wall (SurfaceStopLane.h), it casts from there.
    ray->origin=Add(ray->origin,SurfaceOffset(f,Hand::left));
    ctx.sample={*ray,f.player,component,owner.selectedPower,owner.selected,f.tracking.epoch,
        f.referenceGeneration,generation.load(),f.tracking.publishedNs,HudMenuEpoch()};
    ctx.active=true;return true;
}
// finally restores even when native code raises an exception. No native state
// is written, and no native simulation/targeting callback is repeated.
void InvokeUpdate(std::uintptr_t self,float dt,psi::Context* ctx){
    auto* saved=context;context=ctx;
    __try {originalUpdate(self,dt);} __finally {context=saved;}
}
bool InvokeActivate(std::uintptr_t self,psi::Context* ctx){
    auto* saved=context;context=ctx;bool result=false;
    __try {result=originalActivate(self);} __finally {context=saved;}
    return result;
}
bool InvokeSelect(std::uintptr_t self,int id,psi::Context* ctx){
    auto* saved=context;context=ctx;bool result=false;
    __try {result=originalSelect(self,id);} __finally {context=saved;}
    return result;
}
// Runs after the native reticle getter (ReticleRayHook owns that detour).
void RayFilter(std::uintptr_t receiver,void*,void* result,std::uintptr_t){
    auto* out=static_cast<psi::Ray*>(result);
    if(context&&!fault.load()&&leftHand.load()&&context->sample.generation==generation.load()&&
       context->sample.reference==HeadTrackingReferenceGeneration()&&
       context->sample.menu==HudMenuEpoch()&&
       FreshSample(MonotonicNanoseconds(),context->sample.stamp,100000000)&&
       psi::Override(context,receiver,out))++rayReads;
}
bool __fastcall CandidateHook(std::uintptr_t parameters,std::uintptr_t entity,bool hudPreferred){
    // The native HUD candidate normally bypasses the angle check. It still
    // has to pass the native VR ray cone, distance, target type and LOS checks.
    if(context&&context->active&&context->reads&&leftHand.load()&&!fault.load()&&
       context->sample.generation==generation.load()&&
       context->sample.reference==HeadTrackingReferenceGeneration()&&
       context->sample.menu==HudMenuEpoch()&&
       FreshSample(MonotonicNanoseconds(),context->sample.stamp,100000000))hudPreferred=false;
    return originalCandidate(parameters,entity,hudPreferred);
}
void __fastcall UpdateHook(std::uintptr_t self,float dt){
    if(!leftHand.load()||fault.load()){originalUpdate(self,dt);return;}
    psi::Context ctx{};const bool valid=MakeContext(self,ctx);
    // This player's component: another's update leaves its preview and aim alone.
    const bool mine=valid||preview.component==self;
    if(mine)preview={};
    InvokeUpdate(self,dt,valid?&ctx:nullptr);
    // A selected power is one being aimed: Prey selects it as the psi button
    // goes down and clears it once cast or let go.
    const bool aiming=valid&&ctx.sample.power&&ctx.sample.generation==generation.load();
    if(aiming){preview=ctx.sample;++previews;}
    if(mine)aimingNs.store(aiming?MonotonicNanoseconds():0,std::memory_order_relaxed);
}
bool __fastcall SelectHook(std::uintptr_t self,int id){
    if(!leftHand.load()||fault.load())return originalSelect(self,id);
    psi::Context ctx{};const bool valid=MakeContext(self,ctx);
    if(valid||preview.component==self)preview={};
    return InvokeSelect(self,id,valid?&ctx:nullptr);
}
bool __fastcall ActivateHook(std::uintptr_t self){
    if(!leftHand.load()||fault.load())return originalActivate(self);
    Owner owner{};const auto ownerError=ReadOwner(&owner);
    if(ownerError==ERROR_INVALID_THREAD_ID||(!ownerError&&owner.power!=self))return originalActivate(self);
    // No VR ray now (tracking lost, a recenter the frame has not caught up
    // with): no cast, rather than one along the other hand.
    psi::Context ctx{};
    if(!MakeContext(self,ctx)){++rejectedCasts;return false;}
    // Native powers have already cached the preview target. Preserve its ray
    // for any activation-time readers rather than substituting a newer ray.
    // Prey decides when to cast, so without that aim's preview (a snap turn
    // since, or no targeting update between select and cast) the cast takes
    // the ray as it is now, never a spent one.
    if(psi::Castable(preview,ctx.sample,MonotonicNanoseconds()))ctx.sample=preview;
    else ++currentRayCasts;
    const bool result=InvokeActivate(self,&ctx);preview={};
    if(result)++casts;
    return result;
}
// Every function called through a named RVA is one the gate hashes.
template <std::size_t N>
constexpr bool AllGated(const std::uintptr_t (&rvas)[N]){
    for(const auto rva:rvas)if(!contract::Gated(native::Contracts,rva))return false;
    return true;
}
constexpr std::uintptr_t kCalled[]={native::Update,native::Activate,native::Select,native::Reticle,
    native::GetPower,native::GetPlayer,native::Candidate};
static_assert(AllGated(kCalled));
DWORD Gate(){
 __try {
    const auto base=PreyBase();
    if(!base)return ERROR_NOT_READY;
    for(const auto& c:native::Contracts){
        // The shared hook checked this body against the same bytes before
        // patching it; hashing it now would hash the detour's jump.
        if(c.rva==native::Reticle&&ReticleRayHookInstalled())continue;
        if(!contract::BodyMatches(base,c))return ERROR_BAD_EXE_FORMAT;
    }
    return ERROR_SUCCESS;
 }__except(EXCEPTION_EXECUTE_HANDLER){return GetExceptionCode();}
}
DWORD Install(){
    std::lock_guard lock(installMutex);
    if(installed.load())return 0;
    const auto gated=Gate();if(gated)return gated;
    if(!EnsureGameplayPoseObservation())return ERROR_NOT_READY;
    EnsureMinHook();
    if(!EnsureReticleRayHook())return ERROR_INVALID_FUNCTION;
    const auto base=PreyBase();
    const hook::Spec hooks[]={
      {reinterpret_cast<void*>(base+native::Update),reinterpret_cast<void*>(&UpdateHook),reinterpret_cast<void**>(&originalUpdate)},
      {reinterpret_cast<void*>(base+native::Activate),reinterpret_cast<void*>(&ActivateHook),reinterpret_cast<void**>(&originalActivate)},
      {reinterpret_cast<void*>(base+native::Select),reinterpret_cast<void*>(&SelectHook),reinterpret_cast<void**>(&originalSelect)},
      {reinterpret_cast<void*>(base+native::Candidate),reinterpret_cast<void*>(&CandidateHook),reinterpret_cast<void**>(&originalCandidate)}};
    if(!hook::InstallAll(hooks))return ERROR_INVALID_FUNCTION;
    SetReticleRayFilter(ReticleRayClient::psi,&RayFilter);
    installed=true;return 0;
}

void Error(DWORD result){
    if(result)lastError=result;
    if(result&&result!=ERROR_NOT_READY&&result!=ERROR_INVALID_THREAD_ID)fault=true;
}
}
DWORD SetPsiAimHand(unsigned hand){
    if(hand>1)return ERROR_INVALID_PARAMETER;
    if(hand){const auto result=Install();Error(result);if(result)return result;}
    leftHand=hand;generation.fetch_add(1);return 0;
}
unsigned PsiAimHand(){return leftHand.load();}
bool PsiAimingLeftHand(){
    return PsiAimHand()&&FreshSample(MonotonicNanoseconds(),aimingNs.load(std::memory_order_relaxed),100000000);
}
std::string PsiMedkitReport(){return " aimHand="+std::string(leftHand.load()?"left":"right")+
    " installed="+std::to_string(installed.load())+" previews="+std::to_string(previews.load())+
    " rayReads="+std::to_string(rayReads.load())+" casts="+std::to_string(casts.load())+
    " castsCurrentRay="+std::to_string(currentRayCasts.load())+" aimingLeft="+std::to_string(PsiAimingLeftHand())+
    " castRefused="+std::to_string(rejectedCasts.load())+" fault="+std::to_string(fault.load())+" error="+std::to_string(lastError.load());}
}
