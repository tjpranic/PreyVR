#include "SceneQuery.h"
#include "EngineAccess.h"
#include "AimTakeover.h"
#include "HeadTrackingHook.h"
#include "UiMode.h"
#include "InputPost.h"
#include "ReticleFollow.h"
#include "preyvr/SceneQuery.h"
#include "preyvr/EngineMap.h"
#include "preyvr/NativeContract.h"
#include "preyvr/SceneQueryNative.h"
#include "preyvr/LatestSnapshot.h"
#include <atomic>
namespace preyvr::dll {
namespace {
std::atomic<bool> enabled{false},fault{false};
std::atomic<unsigned long long> hits{0},misses{0},refused{0};
std::atomic<DWORD> lastError{0};
// Every function called through a named RVA is one the gate hashes.
static_assert(contract::Gated(scene::native::Contracts,scene::native::Query)&&
              contract::Gated(scene::native::Contracts,scene::native::EntityPhysics)&&
              contract::Gated(scene::native::Contracts,engine::ArkPlayerLayout::getInstanceRva));
bool Gate(std::uintptr_t base){return contract::BodiesMatch(base,scene::native::Contracts);}
// The physical world and the player's own physics (what every query skips),
// after the thread, player and vtable checks. Faults propagate to the caller's __try.
DWORD World(const GameplayPoseFrame& frame,std::uintptr_t& base,std::uintptr_t& world,void*& player){
    if(!OnEngineMainThread())return ERROR_INVALID_THREAD_ID;
    base=PreyBase();
    if(!base||!frame.player)return ERROR_NOT_READY;
    static bool gated=false;
    if(!gated){if(!Gate(base))return ERROR_BAD_EXE_FORMAT;gated=true;}
    const auto instance=reinterpret_cast<std::uintptr_t(__fastcall*)()>(base+engine::ArkPlayerLayout::getInstanceRva)();
    if(instance!=frame.player)return ERROR_NOT_READY;
    const auto entity=*reinterpret_cast<const std::uintptr_t*>(instance+scene::native::PlayerEntityOffset);
    if(!entity)return ERROR_NOT_READY;
    const auto entityTable=*reinterpret_cast<const std::uintptr_t*>(entity);
    if(!entityTable||*reinterpret_cast<const std::uintptr_t*>(entityTable+scene::native::PhysicsSlot)!=base+scene::native::EntityPhysics)
        return ERROR_INVALID_ADDRESS;
    player=reinterpret_cast<void*(__fastcall*)(std::uintptr_t)>(base+scene::native::EntityPhysics)(entity);
    if(!player)return ERROR_NOT_READY;
    world=*reinterpret_cast<const std::uintptr_t*>(base+scene::native::WorldPointer);
    if(!world)return ERROR_NOT_READY;
    const auto table=*reinterpret_cast<const std::uintptr_t*>(world);
    if(table!=base+scene::native::WorldVtable||*reinterpret_cast<const std::uintptr_t*>(table+0x118)!=base+scene::native::Query)
        return ERROR_INVALID_ADDRESS;
    return ERROR_SUCCESS;
}
// `also` (optional) is skipped as well as the player; `objectTypes` the classes asked for.
DWORD Query(const GameplayPoseFrame& frame,Vec3 origin,Vec3 direction,float range,scene::Hit& hit,
            int objectTypes=scene::AllObjects,void* also=nullptr){
    hit={};hit.distance=-1;
    __try {
        std::uintptr_t base=0,world=0;void* skip[2]={nullptr,also};
        if(const DWORD ready=World(frame,base,world,skip[0]))return ready;
        scene::Params params{};
        if(!scene::Build(params,origin,direction,hit,skip,also?2:1,range,objectTypes))return ERROR_INVALID_DATA;
        // Synchronous flags only. Stack skip/hit storage cannot escape. Native
        // caller=4 acquires the physics caller lock; never call from render/XR.
        using Ray=int(__fastcall*)(std::uintptr_t,const scene::Params*,const char*,int);
        const int count=reinterpret_cast<Ray>(base+scene::native::Query)(world,&params,"RayWorldIntersection(PreyVR)",4);
        if(count==0){hit={};hit.distance=-1;++misses;return ERROR_SUCCESS;}
        if(scene::Distance(count,hit)<0||hit.distance>range+.001f)return ERROR_INVALID_DATA;
        ++hits;return ERROR_SUCCESS;
    }__except(EXCEPTION_EXECUTE_HANDLER){return GetExceptionCode();}
}
}
void QueryAimScene(const GameplayPoseFrame& frame,aim::Sample& sample){
    sample.sceneDistance=-1;
    // aim.scene, or the world reticle, whose depth is this hit.
    if(!(enabled.load()||ReticleInWorld())||fault.load())return;
    if(!HudGameplayInputAllowed()||!FreshSample(MonotonicNanoseconds(),frame.tracking.publishedNs)||
       frame.referenceGeneration!=HeadTrackingReferenceGeneration()){++refused;return;}
    thread_local bool busy=false;
    if(busy){++refused;return;}
    scene::Hit hit{};
    busy=true;const auto result=Query(frame,sample.origin,sample.direction,scene::Range,hit);busy=false;
    sample.sceneDistance=hit.distance;
    lastError=result;
    if(result){sample.sceneDistance=-1;++refused;
        // Transient lack of player/physics or wrong thread can recover. Bad ABI,
        // malformed output or access faults remain disarmed for this process.
        if(!TransientEngineError(result))fault=true;
    }
}
bool QueryDebugRay(const GameplayPoseFrame& frame,Vec3 origin,Vec3 direction,scene::Hit& hit){
    hit={};hit.distance=-1;
    if(fault.load()||!HudGameplayInputAllowed()||!FreshSample(MonotonicNanoseconds(),frame.tracking.publishedNs)||
       frame.referenceGeneration!=HeadTrackingReferenceGeneration())return false;
    thread_local bool busy=false;
    if(busy)return false;
    busy=true;const auto result=Query(frame,origin,direction,scene::Range,hit);busy=false;
    if(result){lastError=result;++refused;
        if(!TransientEngineError(result))fault=true;
        hit={};hit.distance=-1;
    }
    return result==ERROR_SUCCESS;
}
bool QuerySegment(const GameplayPoseFrame& frame,Vec3 a,Vec3 b,scene::Hit& hit){
    hit={};hit.distance=-1;
    const Vec3 d{b.x-a.x,b.y-a.y,b.z-a.z};
    const float length=std::sqrt(d.x*d.x+d.y*d.y+d.z*d.z);
    if(fault.load()||!std::isfinite(length)||length<.001f||length>2.f||!HudGameplayInputAllowed()||
       !FreshSample(MonotonicNanoseconds(),frame.tracking.publishedNs)||
       frame.referenceGeneration!=HeadTrackingReferenceGeneration())return false;
    thread_local bool busy=false;
    if(busy)return false;
    busy=true;const auto result=Query(frame,a,{d.x/length,d.y/length,d.z/length},length,hit);busy=false;
    if(result){lastError=result;++refused;
        if(!TransientEngineError(result))fault=true;
        hit={};hit.distance=-1;
    }
    return result==ERROR_SUCCESS;
}
bool QuerySceneryBetween(const GameplayPoseFrame& frame,Vec3 a,Vec3 b,void* skip,scene::Hit& hit){
    return QueryObjectsBetween(frame,a,b,skip,scene::SceneryObjects,hit);
}
bool QueryObjectsBetween(const GameplayPoseFrame& frame,Vec3 a,Vec3 b,void* skip,int objectTypes,scene::Hit& hit){
    hit={};hit.distance=-1;
    const Vec3 d{b.x-a.x,b.y-a.y,b.z-a.z};
    const float length=std::sqrt(d.x*d.x+d.y*d.y+d.z*d.z);
    if(fault.load()||!std::isfinite(length)||length<.001f||length>2.f||!HudGameplayInputAllowed()||
       !FreshSample(MonotonicNanoseconds(),frame.tracking.publishedNs)||
       frame.referenceGeneration!=HeadTrackingReferenceGeneration())return false;
    thread_local bool busy=false;
    if(busy)return false;
    busy=true;
    const auto result=Query(frame,a,{d.x/length,d.y/length,d.z/length},length,hit,objectTypes,skip);
    busy=false;
    if(result){lastError=result;++refused;
        if(!TransientEngineError(result))fault=true;
        hit={};hit.distance=-1;
    }
    return result==ERROR_SUCCESS;
}
DWORD SceneQueryWorld(const GameplayPoseFrame& frame,std::uintptr_t& world,void*& player){
    world=0;player=nullptr;std::uintptr_t base=0;
    __try{return World(frame,base,world,player);}
    __except(EXCEPTION_EXECUTE_HANDLER){world=0;player=nullptr;return GetExceptionCode();}
}
bool SceneQueryFaulted(){return fault.load();}
void SetSceneReticle(unsigned on){enabled=on!=0;}
unsigned SceneReticleEnabled(){return enabled.load()?1u:0u;}
std::string SceneQueryReport(){return " enabled="+std::to_string(enabled.load())+" fault="+std::to_string(fault.load())+
    " hits="+std::to_string(hits.load())+" misses="+std::to_string(misses.load())+
    " refused="+std::to_string(refused.load())+" error="+std::to_string(lastError.load());}
}
