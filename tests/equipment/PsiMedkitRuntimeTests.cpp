// Production hooks with mocks only at the native ABI and pose boundaries.
#include <windows.h>
#include <cstring>
HMODULE TestModule(const wchar_t*);
#define GetModuleHandleW TestModule
#include "equipment/psi/PsiMedkit.cpp"
#undef GetModuleHandleW
#include <cstdlib>
#include <iostream>
#include <thread>
#include "TestSupport.h"
namespace {
using namespace preyvr;using namespace preyvr::dll;
alignas(16) unsigned char playerBytes[0x2000]{},psiBytes[0x600]{};
std::uintptr_t moduleBase=0,player=0,power=0;
GameplayPoseFrame published{};bool modal=false,poseAvailable=true;
unsigned updates=0,activations=0,selections=0,foreignActivations=0;
unsigned long long reference=2,menuEpoch=1;
const DWORD drainThread=GetCurrentThreadId();psi::Ray observed{},castRay{};
using preyvr::test::Check;
bool Near(float a,float b){return std::fabs(a-b)<.0001f;}
template<class T> T& Field(std::uintptr_t p,unsigned offset){return *reinterpret_cast<T*>(p+offset);}
std::uintptr_t __fastcall GetPlayer(){return player;}
std::uintptr_t __fastcall GetPower(std::uintptr_t p){Check(p==player,"whole player receiver");return power;}
psi::Ray* __fastcall GetRay(std::uintptr_t,psi::Ray* out){*out={{7,8,9},{1,0,0}};return out;}
// The reticle getter as ReticleRayHook runs it: the native body, then psi's filter.
psi::Ray* NativeGetter(std::uintptr_t r,psi::Ray* out){auto* result=GetRay(r,out);RayFilter(r,out,result,0);return result;}
void __fastcall Update(std::uintptr_t p,float){Check(p==power,"psi update receiver");++updates;NativeGetter(player+0x40,&observed);}
bool __fastcall Activate(std::uintptr_t p){if(p!=power){++foreignActivations;return true;}++activations;NativeGetter(player+0x40,&castRay);return true;}
bool __fastcall Select(std::uintptr_t p,int id){Check(p==power,"psi select receiver");++selections;Field<int>(p,0x230)=id;return true;}
bool __fastcall Candidate(std::uintptr_t,std::uintptr_t,bool preferred){return preferred;}
void Jump(std::uintptr_t rva,const void* fn){
    unsigned char code[12]{0x48,0xb8};std::memcpy(code+2,&fn,8);code[10]=0xff;code[11]=0xe0;
    std::memcpy(reinterpret_cast<void*>(moduleBase+rva),code,sizeof(code));
}
// A fresh tracking sample, as the input drain publishes one. The left trigger
// is Prey's psi button (MoveLane), which calls the hooks below itself.
void Publish(float dt=.01f){
    published.tracking.displayTime+=static_cast<long long>(dt*1e9f);++published.tracking.sequence;
    published.tracking.publishedNs=MonotonicNanoseconds();
}
}
HMODULE TestModule(const wchar_t*){return reinterpret_cast<HMODULE>(moduleBase);}
namespace preyvr::dll {
bool EnsureMinHook(){return false;}
bool EnsureReticleRayHook(){return false;}
bool ReticleRayHookInstalled(){return false;}
void SetReticleRayFilter(ReticleRayClient,ReticleRayFilter){}
bool EnsureGameplayPoseObservation(){return true;}
DWORD InputDrainThreadId(){return drainThread;}
unsigned long long HeadTrackingReferenceGeneration(){return reference;}
bool HudGameplayInputAllowed(){return !modal;}
unsigned long long HudMenuEpoch(){return menuEpoch;}
bool TryGetGameplayPoseFrame(GameplayPoseFrame& out,bool){out=published;return poseAvailable;}
}
int main(){
    using namespace preyvr;using namespace preyvr::dll;
    moduleBase=reinterpret_cast<std::uintptr_t>(VirtualAlloc(nullptr,0x2000000,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
    Check(moduleBase!=0,"test ABI arena");player=reinterpret_cast<std::uintptr_t>(playerBytes);
    Check(Gate()==ERROR_BAD_EXE_FORMAT,"unsupported native bodies refused before installing hooks");
    power=reinterpret_cast<std::uintptr_t>(psiBytes)+8;
    Jump(native::GetPlayer,reinterpret_cast<const void*>(&GetPlayer));Jump(native::GetPower,reinterpret_cast<const void*>(&GetPower));
    FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(moduleBase),0x2000000);
    Field<std::uintptr_t>(player,0x678)=power-8;
    Field<int>(power,0x230)=2;Field<int>(power,0x234)=2;
    Field<std::uintptr_t>(power,0x40+16)=0xabcdef;
    Field<std::uintptr_t>(player+scope::InputOffset,0)=moduleBase+scope::InputVtableRva;
    Field<std::uintptr_t>(player+scope::InputOffset,0x70)=player;
    const psi::Ray weapon{{70,80,90},{0,1,0}};Field<psi::Ray>(player,0x17d4)=weapon;
    originalUpdate=Update;originalActivate=Activate;originalSelect=Select;originalCandidate=Candidate;installed=true;
    published.player=player;published.cameraCentreValid=published.headYawUsable=true;
    published.cameraCentre={12,23,3};published.referenceGeneration=2;published.tracking.epoch=1;
    published.tracking.head={{},{0,1.65f,0}};published.tracking.headValidity={true,true,true,true,0};
    auto& left=published.tracking.hands[0];left.aimPose={{},{-.3f,1.2f,-.4f}};left.gripPose={{},{-.25f,1.f,.02f}};
    left.aimValidity=left.gripValidity={true,true,true,true,0};Publish();
    psi::Ray outside{};NativeGetter(player+0x40,&outside);UpdateHook(power,.01f);
    Check(outside.origin.x==7&&observed.origin.x==7&&updates==1&&!PsiAimingLeftHand(),"right hand: Prey's own aim unchanged");
    Check(SetPsiAimHand(2)==ERROR_INVALID_PARAMETER,"two hands only");
    Check(SetPsiAimHand(1)==0&&PsiAimHand()==1,"enable left-hand aim");UpdateHook(power,.01f);
    Check(Near(observed.origin.x,11.7f)&&Near(observed.origin.y,23.4f)&&updates==2,"one native update with the left ray, anchored to its head offset");
    Check(PsiAimingLeftHand(),"a power aimed along the left hand shows on its laser");
    NativeGetter(player+0x40,&outside);Check(outside.origin.x==7,"weapon getter outside scope unchanged");
    psi::Context scoped{preview,true,0};context=&scoped;NativeGetter(player+0x48,&outside);context=nullptr;
    Check(outside.origin.x==7,"wrong secondary receiver stays native");
    scoped.reads=1;context=&scoped;Check(!CandidateHook(1,2,true),"HUD candidate must pass VR angle test");context=nullptr;
    Check(CandidateHook(1,2,true),"unscoped native candidate preference preserved");
    Check(ActivateHook(power+64)&&foreignActivations==1&&activations==0,"other components keep their native activation");
    published.cameraCentre.x=20;
    Check(ActivateHook(power)&&Near(castRay.origin.x,11.7f)&&activations==1,"cast uses preview ray rather than newer pose");
    // Prey decides when to cast: a second one takes the ray as it is now.
    Check(ActivateHook(power)&&Near(castRay.origin.x,19.7f)&&activations==2&&currentRayCasts==1,
          "a cast without its aim's preview takes the current ray, never the spent one");
    UpdateHook(power,.01f);++reference;Check(!ActivateHook(power)&&activations==2,"a recenter the frame lags refuses the cast");reference=2;
    std::thread other([&]{UpdateHook(power,.01f);Check(observed.origin.x==7,"foreign thread keeps native ray");});other.join();
    Check(Field<psi::Ray>(player,0x17d4).origin.x==weapon.origin.x&&Field<psi::Ray>(player,0x17d4).direction.y==1,"cached weapon fields unchanged");
    // **Prey's own psi input**, the left trigger whichever hand aims: select as
    // it goes down, targeting while it is held, cast as it comes up.
    Check(SetPsiAimHand(0)==0&&PsiAimHand()==0&&SetPsiAimHand(1)==0&&PsiAimHand()==1,"right, then left again");
    Field<int>(power,0x230)=-1;UpdateHook(power,.01f);Check(!PsiAimingLeftHand(),"no power selected, none aimed");
    Check(SelectHook(power,2)&&Field<int>(power,0x230)==2&&selections==1,"Prey's select runs in the scope");
    UpdateHook(power,.01f);const auto aimed=observed;
    Check(PsiAimingLeftHand()&&Near(aimed.origin.x,19.7f),"Prey's targeting reads the left ray");
    left.aimPose.position.x+=.5f;
    Check(ActivateHook(power)&&activations==3&&Near(castRay.origin.x,aimed.origin.x),"Prey's cast takes the aimed ray");
    UpdateHook(power,.01f);Check(Near(observed.origin.x,20.2f),"targeting follows the hand");
    left.aimPose.position.x-=.5f;reference+=2;published.referenceGeneration=reference;
    Check(ActivateHook(power)&&activations==4&&Near(castRay.origin.x,19.7f)&&currentRayCasts==2,
          "a snap turn mid-aim casts along the hand as it is now");
    reference=2;published.referenceGeneration=2;
    UpdateHook(power,.01f);left.aimValidity.orientationTracked=false;const auto refused=rejectedCasts.load();
    Check(!ActivateHook(power)&&activations==4&&rejectedCasts==refused+1,"lost left tracking refuses the cast rather than aim with the right hand");
    left.aimValidity.orientationTracked=true;
    Check(SetPsiAimHand(0)==0&&!PsiAimingLeftHand()&&ActivateHook(power)&&activations==5&&castRay.origin.x==7,
          "right hand: Prey's own cast along Prey's own aim");
    Check(SetPsiAimHand(3)==ERROR_INVALID_PARAMETER,"invalid settings refused");
    VirtualFree(reinterpret_cast<void*>(moduleBase),0,MEM_RELEASE);
    std::cout<<"Production psi scopes, preview provenance, and Prey's psi input order passed\n";
}
