#include "HudLayer.h"
#include "HookInstall.h"
#include "EngineAccess.h"
#include "DualOverlay.h"
#include "Bootstrap.h"
#include "MinHookInit.h"
#include "Logger.h"
#include "UiMode.h"
#include "XrSessionHost.h"
#include "InventorySwapchain.h"
#include "MarkerStereo.h"
#include "WristLane.h"
#include "preyvr/EngineMap.h"
#include "preyvr/LatestSnapshot.h"
#include "preyvr/UiPanel.h"
#include "preyvr/InventoryDepth.h"
#include "preyvr/NativeHudIsolation.h"
#include "preyvr/StereoCamera.h"
#include "NativeHudValue.h"
#include <MinHook.h>
#include <wrl/client.h>
#include <array>
#include <atomic>
#include <cstring>
#include <cmath>
#include <mutex>
#include <string>
#include <vector>
#include <cstdio>

namespace preyvr::dll {
namespace {
using Microsoft::WRL::ComPtr;
using ElementRender=void(__fastcall*)(void*);
using FlashRender=void(__fastcall*)(void*,bool);
using SetTargets=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,UINT,ID3D11RenderTargetView*const*,ID3D11DepthStencilView*);
ElementRender originalElement=nullptr;
FlashRender originalFlash=nullptr;
SetTargets originalTargets=nullptr;
std::mutex installMutex;
bool installed=false;
std::atomic<bool> enabled{false};
// Opt-in pending headset acceptance. Redirection consumes the movie's native
// draw, so it also requires a live, compatible swapchain consumer lease.
std::atomic<bool> inventoryCapture{false};
struct InventoryConsumer {D3D11_TEXTURE2D_DESC description{};std::uint64_t stamp=0;
    float panelWidth=0,eyeSeparation=0;bool stereo=false;};
LatestSnapshot<InventoryConsumer> inventoryConsumer;
std::atomic<unsigned long long> refused{0};

// **Generalised by purpose, not by widening a name check.**
//
// This captured only DanielleHUD, and only while gameplay input was allowed --
// which is precisely when the inventory is NOT up, so the inventory could never
// be extracted (INVENTORY-HOLOGRAM-READINESS-2026-09-11, "Already available").
// The two movies want opposite gates and must not share a texture, so identity,
// target lifetime and capture freshness are kept separate per movie rather than
// one being made to stand in for the other.
enum class Movie:unsigned {hud=0,pda=1};
constexpr unsigned kMovies=2;
constexpr const char* kMovieNames[kMovies]={"DanielleHUD","DaniellePDA"};

struct Identity {std::uintptr_t proxy=0;std::uint64_t stamp=0;};
struct Presentation {float width=0,height=0,distance=2;bool active=false;std::uint64_t stamp=0;};
LatestSnapshot<Identity> seen[kMovies];
LatestSnapshot<Presentation> presentation;
std::atomic<unsigned long long> captured[kMovies]{};
std::atomic<unsigned long long> identified[kMovies]{};
// **The observed proxy vtable when it did not match**, kept because it is the
// one value that turns a silent "the inventory never captures" into a fix.
// RE-INVENTORY-DEPTH-2026-09-11 confirmed the PDA shares the element vtable
// 0x1CAB358 and player vtable 0x1DB56D8 with the HUD, but nothing has yet
// proved its render proxy at player+8 carries 0x1DB58B8 as the HUD's does. If
// it does not, this reports the real RVA instead of leaving us to guess.
std::atomic<std::uintptr_t> proxyMismatch[kMovies]{};

struct Capture {
    ComPtr<ID3D11Texture2D> texture,depth;
    ComPtr<ID3D11RenderTargetView> target;
    ComPtr<ID3D11DepthStencilView> depthView;
    std::uint64_t stamp=0;
    DWORD thread=0;
};
Capture captures[kMovies];

// **The text movies.** Notes and books, subtitles and tutorial cards are
// separate elements from the gameplay HUD, so they stayed in the scene -- and
// in a both-eyes frame reached only the second eye. Each panel's movies draw
// into one shared capture (the first each frame clears it, the rest draw over
// it), shown by XrSessionHost above the HUD: notes and books on the note panel,
// held where they opened; subtitles and tutorial cards on the text panel, which
// follows the head. Kept apart so that a subtitle playing when a note opens
// cannot place the note, and a held note cannot leave subtitles behind.
enum class TextPanel:unsigned {text=0,note=1};
constexpr unsigned kTextPanels=2;
constexpr const char* kTextPanelNames[kTextPanels]={"text","note"};
struct TextMovie {const char* name;TextPanel panel;};
constexpr TextMovie kTextMovies[]={{"DanielleTranscribeNote",TextPanel::note},
    {"DanielleSubtitles",TextPanel::text},{"DanielleTutorialCard",TextPanel::text}};
constexpr unsigned kTextMovieCount=sizeof(kTextMovies)/sizeof(kTextMovies[0]);
LatestSnapshot<Identity> seenText[kTextMovieCount];
std::atomic<unsigned long long> textIdentified[kTextMovieCount]{};
struct TextCapture {
    Capture capture;
    std::atomic<bool> fault{false};
    std::atomic<unsigned long long> captured{0},refused{0};
};
TextCapture textPanels[kTextPanels];

// Every Flash element's render proxy and name, kept for reference so it
// can say which movie it drew into the held eye (DualOverlayReport).
struct Named {std::uintptr_t proxy=0;char name[40]{};};
std::mutex namedMutex;
std::array<Named,24> named{};
std::size_t namedNext=0;
void CopyName(char (&out)[40],const char* name) {
    std::size_t i=0;for(;i+1<sizeof(out)&&name[i];++i)out[i]=name[i];out[i]=0;
}
void Remember(std::uintptr_t proxy,const char* name) {
    std::lock_guard lock(namedMutex);
    for(auto& entry:named)if(entry.proxy==proxy){CopyName(entry.name,name);return;}
    auto& entry=named[namedNext++%named.size()];
    entry.proxy=proxy;CopyName(entry.name,name);
}
std::string NameOf(std::uintptr_t proxy) {
    std::lock_guard lock(namedMutex);
    for(const auto& entry:named)if(entry.proxy==proxy)return entry.name;
    return "unnamed";
}

// A movie left in the scene, drawn once more into the held first eye
// inside its one native callback. Set by RedrawIntoHeldEye for
// the duration of that callback and consumed by StereoDisplay.
thread_local void* redrawRoot=nullptr;
thread_local ID3D11DeviceContext* redrawContext=nullptr;
thread_local ID3D11RenderTargetView* redrawFrom=nullptr;
thread_local ID3D11DepthStencilView* redrawFromDepth=nullptr;
thread_local ID3D11RenderTargetView* redrawTarget=nullptr;
thread_local ID3D11DepthStencilView* redrawDepthView=nullptr;
thread_local bool redrawn=false;
// The movie being redrawn is DanielleMarkers: its markers move per eye (MarkerStereo.h).
thread_local bool redrawMarkers=false;
Capture redrawDepth; // the stencil the native draw started from; depth fields only
Capture inventoryRight;
Capture nativeWrist;
std::atomic<std::uint64_t> wristRequest{0};
std::atomic<unsigned long long> wristCaptured{0},wristRefused{0};
std::atomic<bool> wristFault{false};
bool displayInstalled=false,wristInstallAttempted=false;
thread_local void* wristRoot=nullptr;
using SpriteDisplay=void(__fastcall*)(void*,void*);
SpriteDisplay originalSpriteDisplay=nullptr;
bool spriteInstalled=false;
// The replay in flight (the wrist's status, or a HUD part): what it leaves out
// and what proves it drew. Without one, the HUD's own draw and its exclusions.
thread_local hud::Isolation<NativeHudApi>* replayFilter=nullptr;
bool FaceExcludes(const void* sprite);
bool DrawStatusGuarded(void* root);
// **A part's probes** (hud::Part::probes), on a sampled frame: whether each was
// reached by a draw (its display called) and drawn (not left out), in the HUD's
// own draw (phase 0) and in the part's replay (phase 1). Diagnostics only.
struct ProbeSet {
    std::array<NativeHudValue,hud::MaxProbes> values{};
    std::array<void*,hud::MaxProbes> objects{};
    std::size_t count=0;
    int phase=-1;
    bool reached[2][hud::MaxProbes]{},drawn[2][hud::MaxProbes]{};
    void Saw(const void* sprite,bool excluded){
        if(phase<0)return;
        for(std::size_t i=0;i<count;++i)
            if(objects[i]==sprite){reached[phase][i]=true;if(!excluded)drawn[phase][i]=true;}
    }
};
thread_local ProbeSet* probeSet=nullptr;
void __fastcall FilterStatusSprite(void* sprite,void* context){
    const bool excluded=replayFilter?replayFilter->Excludes(sprite):FaceExcludes(sprite);
    if(probeSet)probeSet->Saw(sprite,excluded);
    if(excluded)return;
    if(replayFilter)replayFilter->Observe(sprite);
    originalSpriteDisplay(sprite,context);
}

// **HUD parts** (HudLayer.h RequestHudPart): one named subtree each, lifted off
// the gameplay HUD's own draw and replayed alone into a texture of its own, for
// its consumer to place on a layer of its own. The wrist's status replay, made
// general: the same sprite filter and the same one-callback transaction, inside
// the HUD's one native callback. Render thread throughout.
struct PartState {
    const hud::Part* profile;
    Capture capture;
    std::atomic<std::uint64_t> request{0};
    std::atomic<bool> fault{false};
    hud::PartLift lift;
    bool drawn=false;              // this frame's replay drew the part
    std::atomic<int> open{-1};     // the gates as last read: 1 open, 0 closed, -1 unread
    std::atomic<unsigned long long> lifted{0},drawnFrames{0},refused{0};
    // Frames each gate read open; and, over sampled lifted frames, how often
    // each probe was reached / drawn by the HUD's own draw and by the replay.
    std::array<std::atomic<unsigned long long>,hud::MaxGates> gateFrames{};
    std::array<std::atomic<unsigned long long>,hud::MaxProbes> probeReached[2]{},probeDrawn[2]{};
    std::atomic<unsigned long long> probeSamples{0};
    // Replays whose overrides were applied, and those drawn without them.
    std::atomic<unsigned long long> overridden{0},overrideRefused{0};
    unsigned sampleCounter=0;
};
PartState parts[]={{&hud::Wheel},{&hud::Reticle}};
constexpr unsigned kParts=sizeof(parts)/sizeof(parts[0]);
static_assert(kParts==static_cast<unsigned>(HudPart::count));
bool partsInstallAttempted=false;
bool WantPart(const PartState& part){
    return !part.fault.load() && FreshSample(MonotonicNanoseconds(),part.request.load());
}
bool WantAnyPart(){for(const auto& part:parts)if(WantPart(part))return true;return false;}
std::atomic<bool> inventoryStereo{false};
std::atomic<unsigned> inventoryDepthPercent{25};
std::atomic<unsigned long long> stereoPairs{0},stereoMatrices{0},stereoRefused{0};
using MovieDisplay=void(__fastcall*)(void*);
using CalcMatrix=void(__fastcall*)(void*,const void*,float*);
MovieDisplay originalDisplay=nullptr;
CalcMatrix originalCalc=nullptr;
bool stereoInstalled=false;
thread_local void* stereoRoot=nullptr;
thread_local int stereoEye=-1;
thread_local bool stereoProduced=false;
thread_local InventoryConsumer stereoConsumer{};
thread_local float stereoScale=0;

// The redirection is scoped to the native callback on this thread. Offscreen
// Scaleform filter targets stay native; only its original destination changes.
thread_local ID3D11RenderTargetView* originalDestination=nullptr;
thread_local ID3D11DepthStencilView* originalDepth=nullptr;
thread_local ID3D11DeviceContext* activeContext=nullptr;
// Which capture the in-flight callback is writing into. Thread-local with the
// rest: two movies never render on top of each other, but the substitute must
// follow the movie rather than a file-scope single target.
thread_local ID3D11RenderTargetView* activeTarget=nullptr;
thread_local ID3D11DepthStencilView* activeDepthView=nullptr;

bool WantWrist(){return !wristFault.load()&&FreshSample(MonotonicNanoseconds(),wristRequest.load());}

// hud.var / hud.setvar: read or write ActionScript properties of the gameplay
// HUD, run inside its display callback (the native locks Resolve also runs in).
// SetVariable is the movie root's slot +0x80 (0x18ABCC0): path, value, type;
// its value converter is 0x183F4F0, the pair of GetVariable's 0x183F680.
struct VarOp {std::string path;bool set=false;double value=0;};
std::mutex varMutex;
std::vector<VarOp> varPending;
std::string varLast="none";
std::atomic<bool> varWanted{false};
thread_local void* hudRoot=nullptr;
using SetVar=bool(__fastcall*)(void*,const char*,const NativeHudValue*,int);
bool SetNumberGuarded(void* root,std::uintptr_t base,const char* path,double value){
    __try {
        NativeHudValue v{};v.type=3;std::memcpy(&v.payload,&value,sizeof(value));
        return reinterpret_cast<SetVar>(base+0x18ABCC0)(root,path,&v,0);
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool GetValueGuarded(void* root,std::uintptr_t base,const char* path,NativeHudValue& v){
    __try {return reinterpret_cast<NativeHudApi::Get>(base+0x18ABE10)(root,&v,path);}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
std::string DescribeValue(const NativeHudValue& v){
    char b[96];const unsigned t=v.type&0x3F;
    double d=0;std::memcpy(&d,&v.payload,sizeof(d));
    if(t==3)std::snprintf(b,sizeof(b),"number %.4f",d);
    else if(t==2)std::snprintf(b,sizeof(b),"bool %u",static_cast<unsigned>(v.payload&0xFF));
    else if(t==8)std::snprintf(b,sizeof(b),"object 0x%llX",static_cast<unsigned long long>(v.payload));
    else std::snprintf(b,sizeof(b),"type 0x%X",v.type);
    return b;
}
bool SetValueGuarded(void* root,std::uintptr_t base,const char* path,const NativeHudValue& v){
    __try {return reinterpret_cast<SetVar>(base+0x18ABCC0)(root,path,&v,0);}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}

// **The wrist's own presentation of the native status, for its replay only.**
// The quadrant is authored tilted (-15/-15 deg, Health's init) at half scale,
// and the game closes the meters (_visible false) a few seconds after a change
// when "player state" is dynamic -- the profile default here. For the one
// filtered replay the wrist draws, the quadrant is flattened, enlarged and the
// meters shown; every value is read first and written back right after, so the
// gameplay HUD and the movie's own logic never see the difference.
constexpr const char* kStatusOverride[]={
    "_root.safe.quadrant_SW._xrotation","_root.safe.quadrant_SW._yrotation",
    "_root.safe.quadrant_SW._xscale","_root.safe.quadrant_SW._yscale",
    "_root.safe.quadrant_SW.health_mc._visible","_root.safe.quadrant_SW.psi_mc._visible",
    "_root.safe.quadrant_SW.armor_mc._visible","_root.safe.quadrant_SW.bg_mc._visible",
    "_root.safe.quadrant_SW.bg2_mc._visible","_root.safe.quadrant_SW.line_mc._visible"};
constexpr std::size_t kStatusOverrides=sizeof(kStatusOverride)/sizeof(kStatusOverride[0]);
std::atomic<unsigned long long> overrideApplied{0},overrideRefused{0};
struct StatusOverride {
    std::array<NativeHudValue,kStatusOverrides> original{};
    std::size_t written=0;
    // `background`: keep the meters' own backdrop and top line (bg_mc, bg2_mc,
    // line_mc); off, the hologram's glass stands in for them and the framing
    // closes on the meters.
    bool Apply(void* root,std::uintptr_t base,float scale,bool background){
        for(std::size_t i=0;i<kStatusOverrides;++i){
            NativeHudValue v{};
            if(!GetValueGuarded(root,base,kStatusOverride[i],v))return false;
            // Plain numbers and booleans only: nothing to release, safe to replay.
            if(v.type!=(i<4?3u:2u))return false;
            original[i]=v;
        }
        for(std::size_t i=0;i<kStatusOverrides;++i){
            NativeHudValue v{};
            if(i<2){v.type=3;const double zero=0;std::memcpy(&v.payload,&zero,sizeof(zero));}
            else if(i<4){v.type=3;double s=0;std::memcpy(&s,&original[i].payload,sizeof(s));s*=scale;std::memcpy(&v.payload,&s,sizeof(s));}
            else {v.type=2;v.payload=i<7||background?1:0;}
            if(!SetValueGuarded(root,base,kStatusOverride[i],v))return false;
            written=i+1;
        }
        return true;
    }
    void Restore(void* root,std::uintptr_t base){
        for(std::size_t i=written;i-->0;)SetValueGuarded(root,base,kStatusOverride[i],original[i]);
        written=0;
    }
};

// **The status meters leave the face** when the wrist shows them (wrist.face):
// excluded from the gameplay HUD's normal draw, the rest of it unchanged.
constexpr const char* kFaceExcluded[]={
    "_root.safe.quadrant_SW.health_mc","_root.safe.quadrant_SW.psi_mc","_root.safe.quadrant_SW.armor_mc",
    "_root.safe.quadrant_SW.bg_mc","_root.safe.quadrant_SW.bg2_mc","_root.safe.quadrant_SW.line_mc",
    "_root.safe.quadrant_SW.status_effects"};
using FaceFilter=hud::Exclusion<NativeHudApi>;
thread_local FaceFilter* faceFilter=nullptr;
std::atomic<unsigned long long> faceFrames{0},faceRefused{0};
bool FaceExcludes(const void* sprite){return faceFilter&&faceFilter->Excludes(sprite);}
// The normal draw with the status meters left out (`status`) and each lifted
// part (`lift`). Resolved for this one draw (handles never outlive it) and
// released whatever happens. Each group resolves on its own: one that does not
// stays in the draw, and a part that stayed is unmarked in `lift` so it is not
// replayed as well. False when nothing was left out: the caller draws it whole.
bool DrawExcluding(NativeHudApi& api,void* root,bool status,std::array<bool,kParts>& lift){
    FaceFilter filter(api);
    if(status){
        if(filter.Add(kFaceExcluded))faceFrames.fetch_add(1,std::memory_order_relaxed);
        else faceRefused.fetch_add(1,std::memory_order_relaxed);
    }
    for(unsigned i=0;i<kParts;++i){
        if(!lift[i])continue;
        if(!filter.Add(parts[i].profile->roots)){lift[i]=false;parts[i].refused.fetch_add(1,std::memory_order_relaxed);}
    }
    if(!filter.Count())return false;
    faceFilter=&filter;
    DrawStatusGuarded(root);
    faceFilter=nullptr;
    return true; // drawn, or faulted inside the draw: either way not to be redrawn
}

void RunVarOps(void* root){
    std::vector<VarOp> ops;
    {std::lock_guard lock(varMutex);ops.swap(varPending);varWanted=false;}
    NativeHudApi api{root,PreyBase()};
    std::string out;
    if(!api.Valid())out="root_invalid";
    else for(const auto& op:ops){
        out+=op.path+"=";
        if(op.set){out+=SetNumberGuarded(root,api.base,op.path.c_str(),op.value)?"set ":"set_failed ";}
        NativeHudValue v{};
        if(GetValueGuarded(root,api.base,op.path.c_str(),v)){out+=DescribeValue(v);api.Release(v);}
        else out+="unresolved";
        out+="; ";
    }
    std::lock_guard lock(varMutex);varLast=out;
}
bool DrawStatusGuarded(void* root){
    __try {originalDisplay(root);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
void CaptureNativeStatus(void* root){
    NativeHudApi api{root,PreyBase()};
    if(!api.Valid()){++wristRefused;return;}
    hud::Isolation isolation(api);
    bool produced=false,coverage=false;
    if(isolation.Begin()){
        const auto previousTarget=activeTarget;
        const auto previousDepth=activeDepthView;
        activeTarget=nativeWrist.target.Get();activeDepthView=nativeWrist.depthView.Get();
        const float clear[4]{};activeContext->ClearRenderTargetView(activeTarget,clear);
        originalTargets(activeContext,1,&activeTarget,activeDepthView);
        StatusOverride layout;
        if(layout.Apply(root,api.base,WristNativeScalePercent()*.01f,WristNativeBackground()))overrideApplied.fetch_add(1,std::memory_order_relaxed);
        else {layout.Restore(root,api.base);overrideRefused.fetch_add(1,std::memory_order_relaxed);}
        replayFilter=&isolation;
        produced=DrawStatusGuarded(root);
        replayFilter=nullptr;
        layout.Restore(root,api.base);
        // Require traversal below the common SW ancestor; cached ancestors
        // could otherwise contribute pixels without passing the sprite filter.
        coverage=isolation.Complete();
        activeTarget=previousTarget;activeDepthView=previousDepth;
        originalTargets(activeContext,1,&activeTarget,activeDepthView);
    }
    const bool restored=isolation.Release();
    if(!restored||!produced){
        ++wristRefused;
        // A failed transaction disables replay until the XR session restarts.
        // Release attempts ALL handles. A faulting native release is never retried.
        wristFault=true;
        lifecycle::Log(std::string("preyvr_native_wrist refused restored=")+std::to_string(restored));
        return;
    }
    // Native visibility may hide every meter. Retry on a future requested frame;
    // missing traversal is not a process fault or proof that the HUD is empty.
    if(!coverage){++wristRefused;return;}
    nativeWrist.stamp=MonotonicNanoseconds();nativeWrist.thread=GetCurrentThreadId();++wristCaptured;
}

// A part's boolean gate, read in this callback: 1, 0, or -1 when it does not
// resolve to a boolean (which never lifts the part).
int ReadGate(NativeHudApi& api,void* root,const char* path){
    NativeHudValue v{};
    if(!GetValueGuarded(root,api.base,path,v))return -1;
    const int open=(v.type&0x3F)==2?((v.payload&0xFF)?1:0):-1;
    api.Release(v);
    return open;
}
bool EnsureTexture(ID3D11Device* device,ID3D11RenderTargetView* original,ID3D11DepthStencilView* ds,Capture& capture);
// The part's own target, sized like the HUD's, and the stencil the native draw
// starts from (Flash clips its viewport with it), copied before the HUD's own
// draw can touch it -- which it does when the HUD layer is off and the HUD
// draws natively.
bool PreparePart(PartState& part){
    if(!activeContext || !originalDestination)return false;
    ComPtr<ID3D11Device> device;activeContext->GetDevice(&device);
    if(!device || !EnsureTexture(device.Get(),originalDestination,originalDepth,part.capture))return false;
    if(originalDepth){ComPtr<ID3D11Resource> source;originalDepth->GetResource(&source);activeContext->CopyResource(part.capture.depth.Get(),source.Get());}
    return true;
}
// The part alone, into its target, inside the HUD's one native callback: as the
// wrist's replay, with the part's own profile. A failed transaction disables
// the part until the XR session restarts, and it goes back to the HUD's draw.
// A part's overrides (hud::Part::overrides) for its replay: each number read,
// replaced (by its value, plus its base's number when it names one), and
// written back right after the draw, as the wrist's layout is.
// Plain numbers only, so nothing to release. Refused whole if any is not a
// number; then whatever was written is put back and the replay draws unmoved.
struct PartOverrides {
    std::array<NativeHudValue,hud::MaxOverrides> original{};
    std::size_t written=0;
    bool Apply(void* root,std::uintptr_t base,std::span<const hud::Override> list){
        if(list.size()>hud::MaxOverrides)return false;
        for(std::size_t i=0;i<list.size();++i){
            NativeHudValue v{};
            if(!GetValueGuarded(root,base,list[i].path,v)||v.type!=3)return false;
            original[i]=v;
        }
        std::array<double,hud::MaxOverrides> target{};
        for(std::size_t i=0;i<list.size();++i){
            target[i]=list[i].value;
            if(!list[i].base)continue;
            NativeHudValue b{};
            if(!GetValueGuarded(root,base,list[i].base,b)||b.type!=3)return false;
            double add=0;std::memcpy(&add,&b.payload,sizeof(double));
            if(!std::isfinite(add))return false;
            target[i]+=add;
        }
        for(std::size_t i=0;i<list.size();++i){
            NativeHudValue v{};v.type=3;std::memcpy(&v.payload,&target[i],sizeof(double));
            if(!SetValueGuarded(root,base,list[i].path,v))return false;
            written=i+1;
        }
        return true;
    }
    void Restore(void* root,std::uintptr_t base,std::span<const hud::Override> list){
        for(std::size_t i=written;i-->0;)SetValueGuarded(root,base,list[i].path,original[i]);
        written=0;
    }
};
void ReplayPart(NativeHudApi& api,void* root,PartState& part){
    hud::Isolation isolation(api,part.profile->replay);
    bool produced=false,drawn=false;
    if(isolation.Begin()){
        const auto previousTarget=activeTarget;
        const auto previousDepth=activeDepthView;
        activeTarget=part.capture.target.Get();activeDepthView=part.capture.depthView.Get();
        const float clear[4]{};activeContext->ClearRenderTargetView(activeTarget,clear);
        originalTargets(activeContext,1,&activeTarget,activeDepthView);
        const auto& list=part.profile->overrides;
        PartOverrides moved;
        if(!list.empty()){
            if(moved.Apply(root,api.base,list))part.overridden.fetch_add(1,std::memory_order_relaxed);
            else {moved.Restore(root,api.base,list);part.overrideRefused.fetch_add(1,std::memory_order_relaxed);}
        }
        replayFilter=&isolation;
        produced=DrawStatusGuarded(root);
        replayFilter=nullptr;
        moved.Restore(root,api.base,list);
        drawn=isolation.Complete();
        activeTarget=previousTarget;activeDepthView=previousDepth;
        originalTargets(activeContext,1,&activeTarget,activeDepthView);
    }
    const bool restored=isolation.Release();
    part.lift.Drew(produced&&drawn);
    if(!restored||!produced){
        part.refused.fetch_add(1,std::memory_order_relaxed);
        part.fault=true;
        lifecycle::Log(std::string("preyvr_hud_part refused part=")+part.profile->name+
                       " restored="+std::to_string(restored));
        return;
    }
    part.drawn=drawn;
    part.capture.stamp=MonotonicNanoseconds();part.capture.thread=GetCurrentThreadId();
    part.lifted.fetch_add(1,std::memory_order_relaxed);
    if(drawn)part.drawnFrames.fetch_add(1,std::memory_order_relaxed);
}
// **The gameplay HUD's capture draw**, when anything leaves it: the wrist's
// meters off the face, and each part its consumer asked for whose gate is open.
// Then the wrist's replay and each lifted part's, all inside this one callback.
void DrawHudCapture(void* root,bool face){
    NativeHudApi api{root,PreyBase()};
    const bool valid=api.Valid();
    const auto now=MonotonicNanoseconds();
    std::array<bool,kParts> lift{};
    for(unsigned i=0;i<kParts;++i){
        auto& part=parts[i];
        const bool requested=valid && spriteInstalled && WantPart(part);
        int open=-1;
        if(requested){
            const auto& gates=part.profile->gates;
            const std::size_t n=std::min(gates.size(),hud::MaxGates);
            std::array<int,hud::MaxGates> read{};
            for(std::size_t g=0;g<n;++g){
                read[g]=ReadGate(api,root,gates[g]);
                if(read[g]==1)part.gateFrames[g].fetch_add(1,std::memory_order_relaxed);
            }
            open=n?hud::CombineGates(std::span<const int>(read.data(),n)):1;
        }
        part.open=open;
        lift[i]=part.lift.Decide(requested,open,now);
        if(lift[i] && !PreparePart(part)){
            lift[i]=false;part.lift.Drew(false);part.refused.fetch_add(1,std::memory_order_relaxed);
        }
    }
    // Every 16th lifted frame, one part's probes: reached and drawn by the HUD's
    // own draw, and by its replay. Resolved and released within this callback;
    // a probe that does not resolve is never seen.
    ProbeSet probes{};
    PartState* probed=nullptr;
    for(unsigned i=0;i<kParts && !probed;++i)
        if(lift[i] && !parts[i].profile->probes.empty() && parts[i].sampleCounter++%16==0)probed=&parts[i];
    if(probed){
        for(const char* path:probed->profile->probes){
            if(probes.count==hud::MaxProbes)break;
            void* object=nullptr;
            if(!api.Resolve(path,probes.values[probes.count],object))object=nullptr;
            probes.objects[probes.count++]=object;
        }
        probeSet=&probes;probes.phase=0;
    }
    if(!valid){
        if(face)faceRefused.fetch_add(1,std::memory_order_relaxed);
        originalDisplay(root);
    } else if(!DrawExcluding(api,root,face,lift))originalDisplay(root);
    probes.phase=-1;
    if(root==wristRoot)CaptureNativeStatus(root);
    for(unsigned i=0;i<kParts;++i){
        if(lift[i]){
            if(&parts[i]==probed)probes.phase=1;
            ReplayPart(api,root,parts[i]);
            probes.phase=-1;
        }
        else parts[i].lift.Drew(false); // left in the HUD's draw: nothing of its own to trail
    }
    if(probed){
        probeSet=nullptr;
        for(std::size_t k=0;k<probes.count;++k){
            for(int phase=0;phase<2;++phase){
                if(probes.reached[phase][k])probed->probeReached[phase][k].fetch_add(1,std::memory_order_relaxed);
                if(probes.drawn[phase][k])probed->probeDrawn[phase][k].fetch_add(1,std::memory_order_relaxed);
            }
            api.Release(probes.values[k]);
        }
        probed->probeSamples.fetch_add(1,std::memory_order_relaxed);
    }
}

void __fastcall StereoMatrix(void* renderer,const void* source,float* output) {
    originalCalc(renderer,source,output);
    if(stereoEye<0 || !activeContext || !output)return;
    // CalcTransMat3D on the supported Steam image consumes view at +0x1F8.
    // The recorded view has m22=-1 and translation Z=-36882.855; retain the
    // native viewpoint and use its actual homogeneous reference depth.
    const auto view=reinterpret_cast<const float*>(static_cast<const char*>(renderer)+0x1F8);
    if(std::abs(view[10]+1.f)>.001f || std::abs(view[11])>.001f ||
       std::abs(view[15]-1.f)>.001f || !std::isfinite(view[14]) || view[14]>=-1.f) {
        stereoRefused.fetch_add(1,std::memory_order_relaxed);return;
    }
    std::array<float,16> matrix;std::memcpy(matrix.data(),output,sizeof(matrix));
    const float eye=stereo::EyeSign(stereoEye)*.5f*stereoConsumer.eyeSeparation;
    if(inventory::ApplyStereoParallax(matrix,-view[14],stereoConsumer.panelWidth,eye,stereoScale)) {
        std::memcpy(output,matrix.data(),sizeof(matrix));
        stereoMatrices.fetch_add(1,std::memory_order_relaxed);
    } else stereoRefused.fetch_add(1,std::memory_order_relaxed);
}
// The native draw, into the frame being finished (the second eye), then
// the same movie, same frame, into the held first eye -- starting from the
// stencil the native draw started from, which Flash clips its viewport with.
void RedrawDisplay(void* root) {
    if(redrawFromDepth && redrawDepthView) {
        ComPtr<ID3D11Resource> source;redrawFromDepth->GetResource(&source);
        redrawContext->CopyResource(redrawDepth.depth.Get(),source.Get());
    }
    // The native draw is the frame's own backbuffer: the second submission's eye.
    if(redrawMarkers){BeginMarkerEyes(root);ApplyMarkerEye(root,1);}
    originalDisplay(root);
    activeContext=redrawContext;originalDestination=redrawFrom;originalDepth=redrawFromDepth;
    activeTarget=redrawTarget;activeDepthView=redrawDepthView;
    originalTargets(redrawContext,1,&activeTarget,activeDepthView);
    if(redrawMarkers)ApplyMarkerEye(root,0);
    originalDisplay(root);
    if(redrawMarkers)RestoreMarkerEyes(root);
    activeContext=nullptr;originalDestination=nullptr;originalDepth=nullptr;
    activeTarget=nullptr;activeDepthView=nullptr;
    originalTargets(redrawContext,1,&redrawFrom,redrawFromDepth);
    redrawn=true;
}
void __fastcall StereoDisplay(void* root) {
    if(root==hudRoot && varWanted.load())RunVarOps(root);
    if(redrawRoot && root==redrawRoot){RedrawDisplay(root);return;}
    const bool face=root==hudRoot && activeContext && spriteInstalled && WristMovesStatusOffFace();
    // The wrist's root is the HUD's own (both taken in the same capture).
    if(root==hudRoot && activeContext && (face || root==wristRoot || (spriteInstalled&&WantAnyPart()))){
        // First the normal draw, minus what leaves it. Replays stay inside the
        // ONE native proxy callback and its locks; no extra Advance or release.
        DrawHudCapture(root,face);return;
    }
    if(root!=stereoRoot || !activeContext || stereoEye>=0) {originalDisplay(root);return;}
    // Entered from the ONE native Flash callback, inside both native locks.
    // Each Display builds/destroys its own draw context. No Advance, input,
    // outer callback replay, or extra release of the player occurs here.
    for(int eye=0;eye<2;++eye) {
        auto& target=eye==0?captures[1]:inventoryRight;
        if(originalDepth) {
            ComPtr<ID3D11Resource> source;originalDepth->GetResource(&source);
            activeContext->CopyResource(target.depth.Get(),source.Get());
        }
        const float clear[4]{};activeContext->ClearRenderTargetView(target.target.Get(),clear);
        activeTarget=target.target.Get();activeDepthView=target.depthView.Get();
        originalTargets(activeContext,1,&activeTarget,activeDepthView);
        stereoEye=eye;originalDisplay(root);stereoEye=-1;
    }
    stereoProduced=true;
}

// Any Flash UI element: its name, and its player's render proxy. 0 when it is
// not one; 1 with the proxy; 2 when the proxy's vtable is not the expected one,
// with `*proxy` holding that vtable's RVA instead.
int Inspect(void* element,std::uintptr_t base,std::uintptr_t* proxy,char (&name)[40]) {
    __try {
        auto e=static_cast<std::uint8_t*>(element);
        if(!e || *reinterpret_cast<std::uintptr_t*>(e)!=base+0x1CAB358)return 0;
        const char* text=*reinterpret_cast<const char**>(e+0x30);
        if(!text)return 0;
        std::size_t i=0;for(;i+1<sizeof(name)&&text[i];++i)name[i]=text[i];name[i]=0;
        auto p=*reinterpret_cast<std::uintptr_t*>(e+0x58);
        if(!p || *reinterpret_cast<std::uintptr_t*>(p)!=base+0x1DB56D8)return 0;
        const auto proxyVtable=*reinterpret_cast<std::uintptr_t*>(p+8);
        if(proxyVtable!=base+0x1DB58B8){*proxy=proxyVtable>base?proxyVtable-base:proxyVtable;return 2;}
        *proxy=p+8;
        return 1;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return 0;}
}
void __fastcall ObserveElement(void* element) {
    std::uintptr_t proxy=0;char name[40]{};
    const int found=Inspect(element,PreyBase(),&proxy,name);
    if(found!=0) {
        const auto now=MonotonicNanoseconds();
        for(unsigned i=0;i<kMovies;++i) {
            if(std::strcmp(name,kMovieNames[i])!=0)continue;
            // Fail closed, but say what was actually there. Recorded as an RVA
            // so it can be read straight against the module.
            if(found==2)proxyMismatch[i].store(proxy,std::memory_order_relaxed);
            else {identified[i].fetch_add(1,std::memory_order_relaxed);seen[i].Publish({proxy,now});}
        }
        if(found==1) {
            for(unsigned i=0;i<kTextMovieCount;++i) {
                if(std::strcmp(name,kTextMovies[i].name)!=0)continue;
                textIdentified[i].fetch_add(1,std::memory_order_relaxed);seenText[i].Publish({proxy,now});
            }
            Remember(proxy,name);
        }
    }
    originalElement(element);
}
void STDMETHODCALLTYPE RedirectTargets(ID3D11DeviceContext* context,UINT count,
        ID3D11RenderTargetView*const* targets,ID3D11DepthStencilView* ds) {
    if(context==activeContext && count==1 && targets && targets[0]==originalDestination && activeTarget) {
        auto substitute=activeTarget;
        originalTargets(context,1,&substitute,ds==originalDepth?activeDepthView:ds);
    } else originalTargets(context,count,targets,ds);
}
bool EnsureTexture(ID3D11Device* device,ID3D11RenderTargetView* original,
                   ID3D11DepthStencilView* ds,Capture& capture) {
    ComPtr<ID3D11Resource> resource;
    original->GetResource(&resource);
    ComPtr<ID3D11Texture2D> source;
    if(FAILED(resource.As(&source)))return false;
    D3D11_TEXTURE2D_DESC d{};source->GetDesc(&d);
    D3D11_RENDER_TARGET_VIEW_DESC view{};original->GetDesc(&view);
    if(d.SampleDesc.Count!=1 || d.ArraySize!=1 || d.MipLevels!=1 ||
       d.Width<640 || d.Height<480 || d.Width>8192 || d.Height>8192)return false;
    if(view.Format!=DXGI_FORMAT_R8G8B8A8_UNORM && view.Format!=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB &&
       view.Format!=DXGI_FORMAT_B8G8R8A8_UNORM && view.Format!=DXGI_FORMAT_B8G8R8A8_UNORM_SRGB)return false;
    D3D11_TEXTURE2D_DESC old{};if(capture.texture)capture.texture->GetDesc(&old);
    if(!capture.texture || old.Width!=d.Width || old.Height!=d.Height || old.Format!=view.Format) {
        capture.texture.Reset();capture.target.Reset();capture.depth.Reset();capture.depthView.Reset();
        d.Format=view.Format;d.Usage=D3D11_USAGE_DEFAULT;d.CPUAccessFlags=0;d.MiscFlags=0;
        d.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
        // A half-built capture would match the size check next time and be
        // returned with no view.
        if(FAILED(device->CreateTexture2D(&d,nullptr,&capture.texture)) ||
           FAILED(device->CreateRenderTargetView(capture.texture.Get(),&view,&capture.target))) {
            capture.texture.Reset();capture.target.Reset();return false;
        }
    }
    // Preserve the native stencil contents used to clip Flash into its viewport.
    if(ds) {
        ComPtr<ID3D11Resource> dsResource;ds->GetResource(&dsResource);
        ComPtr<ID3D11Texture2D> dsTexture;if(FAILED(dsResource.As(&dsTexture)))return false;
        D3D11_TEXTURE2D_DESC dd{},previous{};dsTexture->GetDesc(&dd);if(capture.depth)capture.depth->GetDesc(&previous);
        if(dd.Width!=d.Width || dd.Height!=d.Height || dd.SampleDesc.Count!=1)return false;
        if(!capture.depth || previous.Width!=dd.Width || previous.Height!=dd.Height || previous.Format!=dd.Format) {
            capture.depth.Reset();capture.depthView.Reset();
            D3D11_DEPTH_STENCIL_VIEW_DESC vd{};ds->GetDesc(&vd);
            if(FAILED(device->CreateTexture2D(&dd,nullptr,&capture.depth)) ||
               FAILED(device->CreateDepthStencilView(capture.depth.Get(),&vd,&capture.depthView))) {
                capture.depth.Reset();capture.depthView.Reset();return false;
            }
        }
    } else {capture.depth.Reset();capture.depthView.Reset();}
    return true;
}
// **The gate differs by movie, and that is the whole point.** The gameplay HUD
// is only meaningful while the game shows gameplay; the PDA is only on screen
// while it does not. Sharing one gate is what kept the inventory uncapturable.
// Gameplay *shown*, not gameplay input: VR Options owns the controllers over the
// running game, and gating on that dropped the HUD into the world behind it.
bool WantMovie(Movie movie) {
    switch(movie) {
        case Movie::hud: return (enabled.load()||WantWrist()||(spriteInstalled&&WristMovesStatusOffFace())||
                                 (spriteInstalled&&WantAnyPart())) && HudGameplayShown();
        case Movie::pda: return inventoryCapture.load(std::memory_order_acquire) &&
                                HudInventoryIsOpen();
    }
    return false;
}
void* InventoryRoot(void* proxy,std::uintptr_t base) {
    __try {
        auto root=*reinterpret_cast<void**>(static_cast<char*>(proxy)+0xA8);
        return root && *reinterpret_cast<std::uintptr_t*>(root)==base+0x1EB4B60?root:nullptr;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return nullptr;}
}
bool EnsureDepthCopy(ID3D11Device* device,ID3D11DepthStencilView* ds,Capture& capture) {
    ComPtr<ID3D11Resource> resource;ds->GetResource(&resource);
    ComPtr<ID3D11Texture2D> texture;if(FAILED(resource.As(&texture)))return false;
    D3D11_TEXTURE2D_DESC d{},previous{};texture->GetDesc(&d);if(capture.depth)capture.depth->GetDesc(&previous);
    if(d.SampleDesc.Count!=1)return false;
    if(!capture.depth || previous.Width!=d.Width || previous.Height!=d.Height || previous.Format!=d.Format) {
        capture.depth.Reset();capture.depthView.Reset();
        D3D11_DEPTH_STENCIL_VIEW_DESC view{};ds->GetDesc(&view);
        if(FAILED(device->CreateTexture2D(&d,nullptr,&capture.depth)) ||
           FAILED(device->CreateDepthStencilView(capture.depth.Get(),&view,&capture.depthView))) {
            capture.depth.Reset();capture.depthView.Reset();return false;
        }
    }
    return true;
}
struct NativeTargets {
    ComPtr<ID3D11DeviceContext> context;ID3D11Device* device=nullptr;
    std::array<ID3D11RenderTargetView*,8> saved{};ID3D11DepthStencilView* ds=nullptr;
    bool single=false;
    NativeTargets() {
        const auto base=PreyBase();
        auto renderer=*reinterpret_cast<std::uintptr_t*>(base+engine::RendererLayout::singletonPointerRva);
        device=*reinterpret_cast<ID3D11Device**>(renderer+engine::RendererLayout::device);
        device->GetImmediateContext(&context);
        context->OMGetRenderTargets(8,saved.data(),&ds);
        single=saved[0]!=nullptr;for(unsigned i=1;i<8;++i)single=single && !saved[i];
    }
    ~NativeTargets(){for(auto p:saved)if(p)p->Release();if(ds)ds->Release();}
    NativeTargets(const NativeTargets&)=delete;
    NativeTargets& operator=(const NativeTargets&)=delete;
};
// **The text panels' capture.** Each text movie's one native callback, redirected
// into its panel's shared texture. The first movie of a frame on a panel clears
// it; the frame-boundary drain (TakeText) is what makes the next frame's first
// movie first again. False leaves the movie to draw natively. Gated as the HUD
// is (WantMovie): under VR Options a book stays captured and held, not drawn
// into the game's image behind the panel.
bool CaptureText(void* proxy,bool release,std::uintptr_t address,std::uint64_t now) {
    if(!enabled.load() || activeContext || XrSessionStatusValue()!=1 || !HudGameplayShown())return false;
    TextCapture* panel=nullptr;
    for(unsigned i=0;i<kTextMovieCount;++i){
        Identity id{};
        if(seenText[i].TryRead(id) && id.proxy==address && FreshSample(now,id.stamp)){
            panel=&textPanels[static_cast<unsigned>(kTextMovies[i].panel)];break;
        }
    }
    if(!panel || panel->fault.load())return false;
    Capture& capture=panel->capture;
    NativeTargets native;
    if(!native.single || !EnsureTexture(native.device,native.saved[0],native.ds,capture)) {
        panel->refused.fetch_add(1,std::memory_order_relaxed);return false;
    }
    auto* context=native.context.Get();
    if(!capture.stamp || capture.thread!=GetCurrentThreadId()) {
        const float clear[4]{};context->ClearRenderTargetView(capture.target.Get(),clear);
    }
    if(native.ds) {ComPtr<ID3D11Resource> source;native.ds->GetResource(&source);context->CopyResource(capture.depth.Get(),source.Get());}
    originalDestination=native.saved[0];originalDepth=native.ds;activeContext=context;
    activeTarget=capture.target.Get();activeDepthView=capture.depthView.Get();
    originalTargets(context,1,&activeTarget,activeDepthView);
    originalFlash(proxy,release);
    activeContext=nullptr;originalDestination=nullptr;originalDepth=nullptr;
    activeTarget=nullptr;activeDepthView=nullptr;
    originalTargets(context,8,native.saved.data(),native.ds);
    capture.stamp=MonotonicNanoseconds();capture.thread=GetCurrentThreadId();
    panel->captured.fetch_add(1,std::memory_order_relaxed);
    return true;
}
ID3D11Texture2D* TakeText(TextPanel which) {
    auto& panel=textPanels[static_cast<unsigned>(which)];
    const bool fresh=panel.capture.thread==GetCurrentThreadId() && FreshSample(MonotonicNanoseconds(),panel.capture.stamp);
    panel.capture.stamp=0; // the next frame's first movie on this panel clears it
    return fresh && !panel.fault.load()?panel.capture.texture.Get():nullptr;
}
void RefuseText(TextPanel which,const char* reason) {
    if(!textPanels[static_cast<unsigned>(which)].fault.exchange(true))
        lifecycle::Log(std::string("preyvr_hud_layer ")+kTextPanelNames[static_cast<unsigned>(which)]+"_disabled reason="+reason);
}
// A movie still drawing into the frame's backbuffer while a both-eyes frame
// holds its first eye: its one native callback runs with the movie marked,
// and StereoDisplay draws it twice -- natively, then into the held eye.
// The stereo inventory's pattern: no extra outer callback, no extra Advance.
// False leaves the movie to draw natively (and says why in the census).
bool RedrawIntoHeldEye(void* proxy,bool release,std::uintptr_t address) {
    if(!DualOverlayRedrawEnabled() || !DualOverlayWindowOpen() || activeContext || !displayInstalled)return false;
    NativeTargets native;
    const char* refusal=nullptr;
    ID3D11RenderTargetView* held=native.single?HeldFirstEyeTarget(native.saved[0],&refusal):nullptr;
    if(!native.single)refusal="multiple_targets";
    void* root=held?InventoryRoot(proxy,PreyBase()):nullptr;
    if(held && !root)refusal="movie_root";
    if(root && native.ds && !EnsureDepthCopy(native.device,native.ds,redrawDepth)){root=nullptr;refusal="depth_copy";}
    const std::string name=NameOf(address);
    if(!root) {NoteFlashOverlay(name.c_str(),false,refusal);return false;}
    redrawRoot=root;redrawContext=native.context.Get();redrawFrom=native.saved[0];redrawFromDepth=native.ds;
    redrawTarget=held;redrawDepthView=native.ds?redrawDepth.depthView.Get():nullptr;redrawn=false;
    redrawMarkers=name=="DanielleMarkers";
    originalFlash(proxy,release);
    const bool drew=redrawn;
    redrawRoot=nullptr;redrawContext=nullptr;redrawFrom=nullptr;redrawFromDepth=nullptr;
    redrawTarget=nullptr;redrawDepthView=nullptr;redrawn=false;redrawMarkers=false;
    // As the captures do: the callback ends with the targets it was entered with.
    originalTargets(native.context.Get(),8,native.saved.data(),native.ds);
    NoteFlashOverlay(name.c_str(),drew,drew?nullptr:"no_display");
    return true;
}
void __fastcall CaptureFlash(void* proxy,bool release) {
    const auto address=reinterpret_cast<std::uintptr_t>(proxy);
    const auto now=MonotonicNanoseconds();
    unsigned which=kMovies;
    if((enabled.load() || inventoryCapture.load() || WantWrist() || (spriteInstalled&&WristMovesStatusOffFace()) ||
        (spriteInstalled&&WantAnyPart())) && XrSessionStatusValue()==1 && !activeContext) {
        for(unsigned i=0;i<kMovies;++i) {
            Identity id{};
            if(seen[i].TryRead(id) && id.proxy==address && FreshSample(now,id.stamp)) {
                captures[i].stamp=0; // This callback supersedes any older capture, including on refusal.
                if(i==1)inventoryRight.stamp=0;
                if(i==0&&WantWrist())nativeWrist.stamp=0;
                if(i==0)for(auto& part:parts)part.capture.stamp=0;
                if(WantMovie(static_cast<Movie>(i))) {which=i;break;}
            }
        }
    }
    if(which==kMovies) {
        if(CaptureText(proxy,release,address,now) || RedrawIntoHeldEye(proxy,release,address))return;
        originalFlash(proxy,release);return;
    }
    InventoryConsumer consumer{};
    if(which==static_cast<unsigned>(Movie::pda) &&
       (!inventoryConsumer.TryRead(consumer) || !FreshSample(now,consumer.stamp))) {
        originalFlash(proxy,release);return;
    }
    Capture& capture=captures[which];
    const bool captureNormal=which!=0||enabled.load();
    const auto base=PreyBase();
    auto renderer=*reinterpret_cast<std::uintptr_t*>(base+engine::RendererLayout::singletonPointerRva);
    auto device=*reinterpret_cast<ID3D11Device**>(renderer+engine::RendererLayout::device);
    ComPtr<ID3D11DeviceContext> context;device->GetImmediateContext(&context);
    std::array<ID3D11RenderTargetView*,8> saved{};ID3D11DepthStencilView* ds=nullptr;
    context->OMGetRenderTargets(8,saved.data(),&ds);
    bool single=saved[0]!=nullptr;for(unsigned i=1;i<8;++i)single=single && !saved[i];
    bool ready=single && (!captureNormal||EnsureTexture(device,saved[0],ds,capture));
    if(ready&&which==0&&WantWrist()){
        wristRequest=0; // At most one extra HUD draw per XR consumer request.
        if(EnsureTexture(device,saved[0],ds,nativeWrist)){
            wristRoot=InventoryRoot(proxy,base);
            if(ds){ComPtr<ID3D11Resource> source;ds->GetResource(&source);context->CopyResource(nativeWrist.depth.Get(),source.Get());}
        }
        if(!wristRoot)++wristRefused;
    }
    if(ready&&which==0)hudRoot=InventoryRoot(proxy,base);
    if(ready && which==static_cast<unsigned>(Movie::pda)) {
        D3D11_TEXTURE2D_DESC d{};capture.texture->GetDesc(&d);
        ready=InventoryTextureCompatible(consumer.description,d);
    }
    stereoProduced=false;
    if(ready && which==1 && inventoryStereo.load() && consumer.stereo &&
       consumer.panelWidth>=.1f && consumer.eyeSeparation>=.04f && consumer.eyeSeparation<=.09f &&
       EnsureTexture(device,saved[0],ds,inventoryRight)) {
        stereoRoot=InventoryRoot(proxy,base);
        stereoConsumer=consumer;
        stereoScale=inventoryDepthPercent.load()*.01f;
    }
    if(ready) {
        if(captureNormal){
            if(ds) {ComPtr<ID3D11Resource> source;ds->GetResource(&source);context->CopyResource(capture.depth.Get(),source.Get());}
            const float clear[4]={0,0,0,0};context->ClearRenderTargetView(capture.target.Get(),clear);
        }
        originalDestination=saved[0];originalDepth=ds;activeContext=context.Get();
        activeTarget=captureNormal?capture.target.Get():saved[0];activeDepthView=captureNormal?capture.depthView.Get():ds;
        originalTargets(context.Get(),1,&activeTarget,activeDepthView);
    } else refused.fetch_add(1);
    // Exactly one outer call. The opt-in Display hook draws inside its locks.
    originalFlash(proxy,release);
    stereoRoot=nullptr;wristRoot=nullptr;hudRoot=nullptr;
    if(ready) {
        activeContext=nullptr;originalDestination=nullptr;originalDepth=nullptr;
        activeTarget=nullptr;activeDepthView=nullptr;
        originalTargets(context.Get(),8,saved.data(),ds);
        if(captureNormal){capture.stamp=MonotonicNanoseconds();capture.thread=GetCurrentThreadId();captured[which].fetch_add(1);}
        if(which==1 && stereoProduced) {
            inventoryRight.stamp=capture.stamp;inventoryRight.thread=capture.thread;
            stereoPairs.fetch_add(1);
        }
    }
    for(auto p:saved)if(p)p->Release();if(ds)ds->Release();
}
bool Install() {
    if(installed)return true;
    if(!ModulePinStatus() || !EnsureMinHook())return false;
    auto base=PreyBase();
    if(!base)return false;
    constexpr std::uint8_t elementBytes[]={0x41,0x56,0x48,0x83,0xEC,0x20,0x48,0x83,0x79,0x58,0,0x4C,0x8B,0xF1,0x74,0x6A};
    constexpr std::uint8_t flashBytes[]={0x48,0x89,0x5C,0x24,8,0x48,0x89,0x6C,0x24,0x18,0x48,0x89,0x74,0x24,0x20,0x57};
    if(std::memcmp(reinterpret_cast<void*>(base+0x2FEBC0),elementBytes,sizeof(elementBytes)) ||
       std::memcmp(reinterpret_cast<void*>(base+0xE8C5E0),flashBytes,sizeof(flashBytes)))return false;
    auto renderer=*reinterpret_cast<std::uintptr_t*>(base+engine::RendererLayout::singletonPointerRva);
    if(!renderer)return false;
    auto device=*reinterpret_cast<ID3D11Device**>(renderer+engine::RendererLayout::device);if(!device)return false;
    ComPtr<ID3D11DeviceContext> context;device->GetImmediateContext(&context);if(!context)return false;
    auto om=(*reinterpret_cast<void***>(context.Get()))[33];
    // One transaction: all three live or none.
    const hook::Spec hooks[]={
        {om,reinterpret_cast<void*>(RedirectTargets),reinterpret_cast<void**>(&originalTargets)},
        {reinterpret_cast<void*>(base+0x2FEBC0),reinterpret_cast<void*>(ObserveElement),reinterpret_cast<void**>(&originalElement)},
        {reinterpret_cast<void*>(base+0xE8C5E0),reinterpret_cast<void*>(CaptureFlash),reinterpret_cast<void**>(&originalFlash)}};
    if(!hook::InstallAll(hooks))return false;
    installed=true;return true;
}
bool InstallDisplay() {
    if(displayInstalled)return true;
    if(SmokeStatus()!=2)return false; // Complete supported-module gate, not only a DLL pin.
    if(!Install())return false;
    const auto base=PreyBase();
    constexpr unsigned char displayBytes[]={0x40,0x55,0x56,0x48,0x81,0xEC,0x68,0x03,0,0};
    const auto display=reinterpret_cast<void*>(base+0x18AB710);
    if(std::memcmp(display,displayBytes,sizeof(displayBytes)))return false;
    if(!hook::Install(display,reinterpret_cast<void*>(StereoDisplay),originalDisplay))return false;
    displayInstalled=true;return true;
}
bool InstallStereo() {
    if(stereoInstalled)return true;
    if(!InstallDisplay())return false;
    const auto base=PreyBase();
    constexpr unsigned char calcBytes[]={0x48,0x8B,0xC4,0x55,0x53,0x56,0x57,0x48,0x8D,0xA8,0x48,0xFF,0xFF,0xFF};
    const auto calc=reinterpret_cast<void*>(base+0xDB9620);
    if(std::memcmp(calc,calcBytes,sizeof(calcBytes)))return false;
    if(!hook::Install(calc,reinterpret_cast<void*>(StereoMatrix),originalCalc))return false;
    stereoInstalled=true;return true;
}
bool InstallStatus(){
    if(spriteInstalled)return true;
    if(!InstallDisplay())return false;
    const auto base=PreyBase();
    if(!VerifyNativeWrist(base))return false;
    if(*reinterpret_cast<std::uintptr_t*>(base+0x1EB57E8)!=base+0x18BABA0)return false;
    auto at=reinterpret_cast<void*>(base+0x18BABA0);
    if(!hook::Install(at,reinterpret_cast<void*>(FilterStatusSprite),originalSpriteDisplay))return false;
    spriteInstalled=true;return true;
}
ID3D11Texture2D* BorrowTexture(Movie movie) {
    Capture& capture=captures[static_cast<unsigned>(movie)];
    return capture.thread==GetCurrentThreadId() && FreshSample(MonotonicNanoseconds(),capture.stamp)
        ? capture.texture.Get() : nullptr;
}
}
DWORD SetHudLayerEnabled(unsigned value) {
    if(value>1)return 1;
    std::lock_guard lock(installMutex);
    if(value && !Install())return 2;
    if(value)for(auto& panel:textPanels)panel.fault=false; // a refused text or note panel tries again with the layer
    enabled.store(value!=0);return 0;
}
bool HudLayerEnabled(){return enabled.load();}
DWORD SetInventoryCaptureEnabled(unsigned value) {
    if(value>1)return 1;
    std::lock_guard lock(installMutex);
    if(value && !Install())return 2;
    if(!value)inventoryConsumer.Clear();
    inventoryCapture.store(value!=0,std::memory_order_release);
    lifecycle::Log(std::string("preyvr_hud_layer inventory_capture=")+(value?"1":"0")+
                   (value?" note=waiting_for_inventory_swapchain_consumer":""));
    return 0;
}
bool InventoryCaptureEnabled(){return inventoryCapture.load(std::memory_order_acquire);}
void RefuseInventoryCapture(const char* reason){
    inventoryConsumer.Clear();
    if(inventoryCapture.exchange(false))lifecycle::Log(std::string("preyvr_hud_layer inventory_disabled reason=")+reason);
}
void RefuseHudLayer(const char* reason){
    if(enabled.exchange(false))lifecycle::Log(std::string("preyvr_hud_layer disabled reason=")+reason);
}
std::string HudLayerReport(){
    // Per movie, because "captured=0" on its own never said which movie was
    // missing, nor whether identification or the gate was the reason.
    InventoryConsumer consumer{};
    const bool consumerReady=inventoryConsumer.TryRead(consumer) && FreshSample(MonotonicNanoseconds(),consumer.stamp);
    std::string out="hudLayer="+std::to_string(enabled.load())+
        " inventoryCapture="+std::to_string(inventoryCapture.load())+
        " inventoryConsumer="+std::to_string(consumerReady)+
        " inventoryLayerFrames="+std::to_string(InventoryLayerFrameCount())+
        " refused="+std::to_string(refused.load());
    out+=" nativeWrist={captured="+std::to_string(wristCaptured.load())+" refused="+
        std::to_string(wristRefused.load())+" fault="+std::to_string(wristFault.load())+
        " layout="+std::to_string(overrideApplied.load())+"/"+std::to_string(overrideRefused.load())+
        " faceFrames="+std::to_string(faceFrames.load())+" faceRefused="+std::to_string(faceRefused.load())+"}";
    // Per part: requested now, frames lifted and replayed, frames the replay
    // drew it, the gate as last read (1 open, 0 closed, -1 unread or unreadable).
    out+=" parts={";
    for(unsigned i=0;i<kParts;++i){
        const auto& part=parts[i];
        out+=std::string(i?" ":"")+part.profile->name+"={requested="+std::to_string(WantPart(part))+
             " lifted="+std::to_string(part.lifted.load())+" drawn="+std::to_string(part.drawnFrames.load())+
             " open="+std::to_string(part.open.load())+" refused="+std::to_string(part.refused.load())+
             " fault="+std::to_string(part.fault.load());
        // gates=frames each gate read open, in profile order. probes: name=
        // hud reached/drawn,replay reached/drawn, over `samples` sampled frames.
        out+=" gates=";
        for(std::size_t g=0;g<std::min(part.profile->gates.size(),hud::MaxGates);++g)
            out+=(g?"/":"")+std::to_string(part.gateFrames[g].load());
        if(!part.profile->overrides.empty())
            out+=" overrides="+std::to_string(part.overridden.load())+"/"+std::to_string(part.overrideRefused.load());
        out+=" samples="+std::to_string(part.probeSamples.load())+" probes={";
        for(std::size_t k=0;k<std::min(part.profile->probes.size(),hud::MaxProbes);++k){
            const std::string path=part.profile->probes[k];
            out+=(k?" ":"")+path.substr(path.rfind('.')+1)+"="+
                 std::to_string(part.probeReached[0][k].load())+"/"+std::to_string(part.probeDrawn[0][k].load())+","+
                 std::to_string(part.probeReached[1][k].load())+"/"+std::to_string(part.probeDrawn[1][k].load());
        }
        out+="}}";
    }
    out+="}";
    for(unsigned i=0;i<kMovies;++i) {
        out+=std::string(" ")+kMovieNames[i]+"={identified="+std::to_string(identified[i].load())+
             " captured="+std::to_string(captured[i].load());
        const auto mismatch=proxyMismatch[i].load();
        if(mismatch)out+=" proxyVtableRva=0x"+[&]{char b[32];std::snprintf(b,sizeof(b),"%llX",
            static_cast<unsigned long long>(mismatch));return std::string(b);}();
        out+="}";
    }
    for(unsigned p=0;p<kTextPanels;++p) {
        const auto& panel=textPanels[p];
        out+=std::string(" ")+kTextPanelNames[p]+"={captured="+std::to_string(panel.captured.load())+
             " refused="+std::to_string(panel.refused.load())+" fault="+std::to_string(panel.fault.load());
        for(unsigned i=0;i<kTextMovieCount;++i)
            if(static_cast<unsigned>(kTextMovies[i].panel)==p)
                out+=std::string(" ")+kTextMovies[i].name+"="+std::to_string(textIdentified[i].load());
        out+="}";
    }
    return out;
}
ID3D11Texture2D* HudLayerTexture(){
    auto texture=BorrowTexture(Movie::hud);
    captures[static_cast<unsigned>(Movie::hud)].stamp=0;
    return texture;
}
ID3D11Texture2D* TextLayerTexture(){return TakeText(TextPanel::text);}
void RefuseTextLayer(const char* reason){RefuseText(TextPanel::text,reason);}
ID3D11Texture2D* NoteLayerTexture(){return TakeText(TextPanel::note);}
void RefuseNoteLayer(const char* reason){RefuseText(TextPanel::note,reason);}
bool EnsureOverlayHooks(){
    std::lock_guard lock(installMutex);
    return InstallDisplay();
}
bool DrawRedirected(ID3D11DeviceContext* context,ID3D11RenderTargetView* from,ID3D11DepthStencilView* fromDepth,
                    ID3D11RenderTargetView* to,ID3D11DepthStencilView* toDepth,void(*draw)(void*),void* argument){
    // Never inside a capture already in flight: its substitution owns the state.
    if(!originalTargets || activeContext || !context || !from || !to)return false;
    activeContext=context;originalDestination=from;originalDepth=fromDepth;activeTarget=to;activeDepthView=toDepth;
    originalTargets(context,1,&to,toDepth);
    draw(argument);
    activeContext=nullptr;originalDestination=nullptr;originalDepth=nullptr;activeTarget=nullptr;activeDepthView=nullptr;
    originalTargets(context,1,&from,fromDepth);
    return true;
}
void RequestNativeWristCapture(bool active){
    // The face filter rides the same sprite hook, so it is installed as soon as
    // the wrist owns the status in gameplay, not only once the card is first
    // looked at. Not before gameplay: a refused install latches the fault for
    // the whole XR session, and the module gates may still be settling.
    const bool install=active||(WristMovesStatusOffFace()&&HudGameplayInputAllowed());
    if(!active&&(wristInstallAttempted||!install)){wristRequest=0;return;}
    if(!wristInstallAttempted){
        std::lock_guard lock(installMutex);wristInstallAttempted=true;
        if(!InstallStatus()){wristFault=true;lifecycle::Log("preyvr_native_wrist install=refused");}
    }
    if(!active){wristRequest=0;return;}
    if(!wristFault.load())wristRequest=MonotonicNanoseconds();
}
ID3D11Texture2D* NativeWristLayerTexture(){
    const bool fresh=nativeWrist.thread==GetCurrentThreadId()&&FreshSample(MonotonicNanoseconds(),nativeWrist.stamp);
    nativeWrist.stamp=0;
    return fresh&&!wristFault.load()?nativeWrist.texture.Get():nullptr;
}
DWORD QueueHudVariable(const std::string& path,bool set,double value){
    if(path.empty()||path.size()>200||(set&&!std::isfinite(value)))return 1;
    {std::lock_guard lock(installMutex);if(!InstallDisplay())return 2;}
    std::lock_guard lock(varMutex);
    if(varPending.size()>=16)return 3;
    varPending.push_back({path,set,value});varWanted=true;return 0;
}
std::string HudVariableReport(){std::lock_guard lock(varMutex);return varLast;}
bool HudMovieNumber(void* root,const char* path,double& value){
    NativeHudValue v{};
    NativeHudApi api{root,PreyBase()};
    if(!root || !GetValueGuarded(root,api.base,path,v))return false;
    if((v.type&0x3F)!=3){api.Release(v);return false;} // only a plain number has nothing to release
    std::memcpy(&value,&v.payload,sizeof(value));return std::isfinite(value);
}
bool SetHudMovieNumber(void* root,const char* path,double value){
    return root && std::isfinite(value) && SetNumberGuarded(root,PreyBase(),path,value);
}
void ResetNativeWristCapture(){
    wristRequest=0;nativeWrist={};wristFault=false;wristInstallAttempted=false;
}
void RequestHudPart(HudPart which,bool active){
    auto& part=parts[static_cast<unsigned>(which)];
    if(!active){part.request=0;return;}
    // As the wrist: the sprite hook is installed once, and not before gameplay,
    // since a refused install latches the fault for the whole XR session.
    if(!partsInstallAttempted){
        if(!HudGameplayShown()){part.request=0;return;}
        std::lock_guard lock(installMutex);partsInstallAttempted=true;
        if(!InstallStatus()){
            for(auto& each:parts)each.fault=true;
            lifecycle::Log("preyvr_hud_part install=refused");
        }
    }
    if(!part.fault.load())part.request=MonotonicNanoseconds();
}
HudPartImage TakeHudPart(HudPart which){
    auto& part=parts[static_cast<unsigned>(which)];
    const bool fresh=part.capture.thread==GetCurrentThreadId()&&FreshSample(MonotonicNanoseconds(),part.capture.stamp);
    part.capture.stamp=0; // the next HUD callback replays it afresh, or not at all
    HudPartImage out;
    out.open=part.open.load();
    if(fresh&&!part.fault.load()){out.texture=part.capture.texture.Get();out.drawn=part.drawn;}
    return out;
}
void RefuseHudPart(HudPart which,const char* reason){
    auto& part=parts[static_cast<unsigned>(which)];
    part.request=0;
    if(!part.fault.exchange(true))
        lifecycle::Log(std::string("preyvr_hud_part disabled part=")+part.profile->name+" reason="+reason);
}
void ResetHudParts(){
    for(auto& part:parts){
        part.request=0;part.capture={};part.fault=false;part.lift.Reset();part.drawn=false;part.open=-1;
        part.sampleCounter=0;
    }
    partsInstallAttempted=false;
}
ID3D11Texture2D* InventoryLayerTexture(ID3D11Texture2D** rightEye){
    auto texture=BorrowTexture(Movie::pda);
    if(rightEye)*rightEye=texture && inventoryRight.stamp==captures[1].stamp &&
        inventoryRight.thread==GetCurrentThreadId()?inventoryRight.texture.Get():nullptr;
    // The host drains this at every render-frame boundary, including skips.
    captures[static_cast<unsigned>(Movie::pda)].stamp=0;
    inventoryRight.stamp=0;
    return texture;
}
bool PeekInventoryPair(ID3D11Texture2D** left,ID3D11Texture2D** right) {
    *left=BorrowTexture(Movie::pda);
    *right=*left && inventoryRight.stamp==captures[1].stamp &&
        inventoryRight.thread==GetCurrentThreadId()?inventoryRight.texture.Get():nullptr;
    return *left && *right;
}
void SetInventoryConsumerReady(const D3D11_TEXTURE2D_DESC* description,float width,float separation,bool stereo){
    if(description)inventoryConsumer.Publish({*description,MonotonicNanoseconds(),width,separation,stereo});
    else inventoryConsumer.Clear();
}
DWORD SetInventoryStereoEnabled(unsigned value) {
    if(value>1)return 1;
    std::lock_guard lock(installMutex);
    if(value && !InstallStereo())return 2;
    inventoryStereo.store(value!=0);return 0;
}
bool InventoryStereoEnabled(){return inventoryStereo.load();}
DWORD SetInventoryDepthPercent(unsigned value) {
    if(value>100)return 1;
    inventoryDepthPercent.store(value);return 0;
}
unsigned InventoryDepthPercent(){return inventoryDepthPercent.load();}
std::string InventoryStereoReport() {
    return "enabled="+std::to_string(inventoryStereo.load())+" depthPercent="+std::to_string(inventoryDepthPercent.load())+
        " capturedPairs="+std::to_string(stereoPairs.load())+" matrices="+std::to_string(stereoMatrices.load())+
        " refusedMatrices="+std::to_string(stereoRefused.load());
}
void SetHudLayerPresentation(bool active,float width,float height,float distance){presentation.Publish({width,height,distance,active,MonotonicNanoseconds()});}
bool HudLayerReticle(float tanX,float tanY,float& x,float& y){
    Presentation p{};if(!enabled.load() || !presentation.TryRead(p) || !p.active ||
        !FreshSample(MonotonicNanoseconds(),p.stamp) || p.width<=0 || p.height<=0)return false;
    const auto fraction=ui::PanelFraction(tanX,tanY,p.width,p.height,p.distance);
    if(!fraction)return false;x=(*fraction)[0];y=(*fraction)[1];return true;
}
}
