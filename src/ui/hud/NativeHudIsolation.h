#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
namespace preyvr::hud {
// Named, non-mask siblings in the patched Steam movie. No native visibility,
// transforms, ActionScript variables or animation playlists are modified.
inline constexpr std::array ExcludedFromStatus{
    "_root.damage", "_root.ar_mc", "_root.safe.damage_mc", "_root.safe.awareness_mc",
    "_root.safe.reticle_mc", "_root.safe.quadrant_W", "_root.safe.quadrant_N",
    "_root.safe.quadrant_NE", "_root.safe.quadrant_E", "_root.safe.wheel_mc",
    "_root.safe.quadrant_NW", "_root.safe.quadrant_SE", "_root.safe.quadrant_S",
    "_root.safe.quadrant_C", "_root.safe.quadrant_N_2", "_root.safe.quadrant_SW.power_mc",
    "_root.safe.quadrant_SW.climb_mc", "_root.safe.quadrant_SW.flashlight_mc",
    "_root.safe.quadrant_SW.stealth", "_root.safe.quadrant_SW.pickup"};
inline constexpr std::array StatusMeters{"_root.safe.quadrant_SW.health_mc",
    "_root.safe.quadrant_SW.psi_mc", "_root.safe.quadrant_SW.armor_mc"};

// **What one replay draws.** Every path in `excluded` is left out with all it
// holds; one of `observed` drawn proves the replay reached what it is for. Not
// reaching it is not a fault: native visibility may have hidden all of them.
struct Profile { std::span<const char* const> excluded, observed; };
inline constexpr std::size_t MaxProfilePaths=32;
// The wrist's: the status meters, everything else in the movie left out.
inline constexpr Profile StatusProfile{ExcludedFromStatus, StatusMeters};

// **The movie's named layout above its widgets** (HUD-PARTS-2026-10-15.md,
// read offline from the patched Steam movie): _root places these three clips
// and nothing else; safe places these fourteen and nothing else, and at run
// time SpiralWheel's constructor attaches two more to it (SafeAttached). A
// replay that keeps one of safe's children leaves out every other one, and
// _root's two; one that keeps a clip inside a quadrant leaves out that
// quadrant's other children. The quadrants a part reaches into place
// these children and nothing else, and nothing is attached to them at run time.
inline constexpr std::array RootChildren{"_root.damage", "_root.ar_mc", "_root.safe"};
inline constexpr std::array SafeChildren{
    "_root.safe.damage_mc", "_root.safe.awareness_mc", "_root.safe.reticle_mc",
    "_root.safe.quadrant_W", "_root.safe.quadrant_N", "_root.safe.quadrant_NE",
    "_root.safe.quadrant_E", "_root.safe.quadrant_SW", "_root.safe.wheel_mc",
    "_root.safe.quadrant_NW", "_root.safe.quadrant_SE", "_root.safe.quadrant_S",
    "_root.safe.quadrant_C", "_root.safe.quadrant_N_2"};
// safe.attachMovie("assign_power", "quick_mc", 800) and ("equip_power",
// "assign_mc", 900): the wheel's assign-to-hotbar and equip-power flourishes.
inline constexpr std::array SafeAttached{"_root.safe.quick_mc", "_root.safe.assign_mc"};
inline constexpr std::array QuadrantSChildren{"_root.safe.quadrant_S.tutHint_mc",
    "_root.safe.quadrant_S.stamina_mc", "_root.safe.quadrant_S.quickSelect_mc"};
inline constexpr std::array QuadrantSEChildren{"_root.safe.quadrant_SE.ammo_mc",
    "_root.safe.quadrant_SE.speedometer_mc", "_root.safe.quadrant_SE.oxygen",
    "_root.safe.quadrant_SE.throw_mc", "_root.safe.quadrant_SE.quickSelect_mc"};
inline constexpr std::array QuadrantCChildren{"_root.safe.quadrant_C.interact",
    "_root.safe.quadrant_C.quickLoot", "_root.safe.quadrant_C.mash_mc"};
inline constexpr std::array QuadrantWChildren{"_root.safe.quadrant_W.pickup",
    "_root.safe.quadrant_W.tutorial", "_root.safe.quadrant_W.pickupPrompt"};

// **A part of the HUD with a layer of its own** (HudLayer RequestHudPart): the
// subtrees lifted off the HUD's own draw together (`roots`, left out as one
// group or not at all), the replay that draws them alone, and the boolean
// `gates` read in the same callback. A gated part is lifted while any of its
// gates reads true, and briefly after (PartLift), so it costs a replay only
// while it is up; an ungated one whenever it is requested. `probes` are
// diagnostic paths: on sampled frames, whether each was reached by the HUD's
// own draw or by the replay (hud.layer), to see where a piece really draws.
// `overrides` are numeric ActionScript properties set for the replay alone:
// each is read, replaced, and put back right after the replay's draw, so the
// game and the HUD's own draw never see the change. The value set is `value`,
// plus the number at `base` when one is named (read in the same callback).
struct Override { const char* path; double value; const char* base = nullptr; };
struct Part {
    const char* name;
    std::span<const char* const> roots;
    Profile replay;
    std::span<const char* const> gates;
    std::span<const char* const> probes;
    std::span<const Override> overrides;
};
inline constexpr std::size_t MaxGates=4, MaxProbes=4, MaxOverrides=10;

// **The Favorites Wheel and what Prey shows with it.** The wheel, its details
// panel and quick-use prompts sit under wheel_mc. Opening it
// (Visibility.openAbilities) fades out the rest of the HUD (PsiHideArray) except,
// among others, the PC hotbar (QuickSelectPC, opened with @ui_Assign) and the
// gamepad quick-select (QuickSelect), and it shows the controls box,
// quadrant_W.tutorial (_visible = true): that box carries the wheel's
// "[Mouse 1] Equip  [Mouse 2] Cancel" (the pad's glyphs on a controller), the
// engine's HTML line written by ControlsPrompt.tutorialUpdate into
// tutorial.inner_mc.help_txt, which itself checks vis.focusIsOpen. With the two
// flourishes on safe, they travel with the wheel. Proved by reaching any of
// them, so it stays lifted while the slowest still fades out.
inline constexpr std::array WheelRoots{"_root.safe.wheel_mc", "_root.safe.quadrant_S.quickSelect_mc",
    "_root.safe.quadrant_SE.quickSelect_mc", "_root.safe.quadrant_W.tutorial",
    "_root.safe.quick_mc", "_root.safe.assign_mc"};
// The wheel (SpiralWheel), its details panel (SpiralWheelDetails) and its
// quick-use prompts (SpiralWheelPrompts) each keep an `isOpen`. PreyDll closes
// them in one routine (wheelDetailsClose, wheelClose, wheelItemPromptsClose), so
// none outlives the others. All three are root-timeline variables, so members of
// _root. Lifted while any is open.
inline constexpr std::array WheelGates{"_root.spiral.isOpen", "_root.spiralDetails.isOpen",
    "_root.spiralPrompts.isOpen"};
inline constexpr std::array WheelProbes{"_root.safe.quadrant_W.tutorial",
    "_root.safe.wheel_mc.details_mc", "_root.safe.wheel_mc.prompts_mc"};
inline constexpr std::array WheelExcluded{"_root.damage", "_root.ar_mc",
    "_root.safe.damage_mc", "_root.safe.awareness_mc", "_root.safe.reticle_mc",
    "_root.safe.quadrant_N", "_root.safe.quadrant_NE", "_root.safe.quadrant_E",
    "_root.safe.quadrant_SW", "_root.safe.quadrant_NW", "_root.safe.quadrant_C",
    "_root.safe.quadrant_N_2",
    "_root.safe.quadrant_W.pickup", "_root.safe.quadrant_W.pickupPrompt",
    "_root.safe.quadrant_S.tutHint_mc", "_root.safe.quadrant_S.stamina_mc",
    "_root.safe.quadrant_SE.ammo_mc", "_root.safe.quadrant_SE.speedometer_mc",
    "_root.safe.quadrant_SE.oxygen", "_root.safe.quadrant_SE.throw_mc"};
// No overrides: nothing the wheel carries moves with the crosshair. (The
// interaction prompt does -- reticleXOffset/YOffset move all of quadrant_C --
// and it goes with the reticle, step 3; it is not drawn while the wheel is up.)
inline constexpr Part Wheel{"wheel", WheelRoots, {WheelExcluded, WheelRoots}, WheelGates, WheelProbes, {}};

// **The reticle and what follows it.** reticle_mc holds every crosshair (the
// frames of reticle_master), the hit markers, charge and stun-lock indicators,
// the range readout and the interact icons. quadrant_C holds the interaction
// prompt, quick loot and the mash prompt, which Prey moves with the crosshair
// (Reticles.reticleXOffset/YOffset set both). Ungated: lifted whenever its
// consumer asks (the world reticle), drawn whenever Prey shows it.
inline constexpr std::array ReticleRoots{"_root.safe.reticle_mc", "_root.safe.quadrant_C"};
// Everything else in the movie, including the wheel's two flourishes on safe.
inline constexpr std::array ReticleExcluded{"_root.damage", "_root.ar_mc",
    "_root.safe.damage_mc", "_root.safe.awareness_mc", "_root.safe.quadrant_W",
    "_root.safe.quadrant_N", "_root.safe.quadrant_NE", "_root.safe.quadrant_E",
    "_root.safe.quadrant_SW", "_root.safe.wheel_mc", "_root.safe.quadrant_NW",
    "_root.safe.quadrant_SE", "_root.safe.quadrant_S", "_root.safe.quadrant_N_2",
    "_root.safe.quick_mc", "_root.safe.assign_mc"};
inline constexpr std::array ReticleProbes{"_root.safe.reticle_mc.reticle_master",
    "_root.safe.quadrant_C.interact", "_root.safe.quadrant_C.quickLoot"};
// **Centred for its replay**, so its texture's centre is the crosshair and the
// world reticle places it by its quad: reticle_mc and quadrant_C where a centred
// crosshair (0.5, 0.5) puts them, 0 and 0 (round(StageWidth * x) -
// round(StageWidth / 2)), and safe where HUDPos(0, 0) puts it, at half the stage
// (Camera.HUDPos sets safe to X + m_halfStageWidth, Y + m_halfStageHeight; the
// engine's HUD sway and shake move it from there).
//
// **And the crosshair and interact icons where they are authored in reticle_mc,
// 0 and 0.** Reticles.reticlePosition(X, Y) moves those two children by X minus
// the last reticleXOffset/YOffset value, and Prey sends it every frame of an
// examination and once on leaving one, from ArkPlayer +0x17EC/+0x17F0 (0x15AB280).
// Flat, that field is the value the offsets were sent, so the children return to
// 0. Here it is the eye's viewport fraction while the offsets are the HUD canvas
// fraction (ReticleFollow), so they kept the difference and the crosshair sat off
// the laser after every keypad (docs/RETICLE-DRIFT-SCOPE-2026-10-27.md).
inline constexpr std::array<Override, 10> ReticleOverrides{{
    {"_root.safe.reticle_mc._x", 0.0}, {"_root.safe.reticle_mc._y", 0.0},
    {"_root.safe.quadrant_C._x", 0.0}, {"_root.safe.quadrant_C._y", 0.0},
    {"_root.safe._x", 0.0, "_root.cam.m_halfStageWidth"}, {"_root.safe._y", 0.0, "_root.cam.m_halfStageHeight"},
    {"_root.safe.reticle_mc.reticle_master._x", 0.0}, {"_root.safe.reticle_mc.reticle_master._y", 0.0},
    {"_root.safe.reticle_mc.interact_icons._x", 0.0}, {"_root.safe.reticle_mc.interact_icons._y", 0.0}}};
inline constexpr Part Reticle{"reticle", ReticleRoots, {ReticleExcluded, ReticleRoots}, {}, ReticleProbes,
                              ReticleOverrides};

// **A part's gates as one**: open (1) when any reads open, closed (0) when none
// does and at least one reads closed, unreadable (-1) when none can be read. A
// gate that does not resolve neither opens nor keeps the part closed.
inline int CombineGates(std::span<const int> gates) {
    int combined = -1;
    for (int gate : gates) {
        if (gate == 1) return 1;
        if (gate == 0) combined = 0;
    }
    return combined;
}

// **When a part is lifted.** Once per HUD draw: requested by its consumer, and
// its gates read open (1). After they close it stays lifted for the close
// animation while the replay still draws something, at most PartTrailNs; an
// unreadable gate (-1) never lifts, so the part stays where Prey draws it.
inline constexpr std::uint64_t PartTrailNs=1'000'000'000;
class PartLift {
public:
    bool Decide(bool requested, int open, std::uint64_t now) {
        if (open == 1) openNs_ = now;
        const bool trailing = lifted_ && drewLast_ && open == 0 && openNs_ != 0 && now >= openNs_ &&
                              now - openNs_ <= PartTrailNs;
        lifted_ = requested && (open == 1 || trailing);
        if (!lifted_) drewLast_ = false;
        return lifted_;
    }
    // After the frame's replay (or its refusal): whether it drew the part.
    void Drew(bool drawn) { drewLast_ = drawn; }
    void Reset() { *this = {}; }
private:
    std::uint64_t openNs_ = 0;
    bool lifted_ = false, drewLast_ = false;
};

// Retained handles live for exactly one replay, inside the native render locks.
// All references, including a partial failed lookup, are released on failure.
template<class Api> class Isolation {
public:
    explicit Isolation(Api& api, const Profile& profile = StatusProfile) : api_(api), profile_(profile) {}
    Isolation(const Isolation&)=delete;
    Isolation& operator=(const Isolation&)=delete;
    bool Begin(){
        if(attempted_)return false;
        attempted_=true;
        const std::size_t total=profile_.excluded.size()+profile_.observed.size();
        if(total>MaxProfilePaths)return false;
        for(std::size_t i=0;i<total;++i){
            const auto path=i<profile_.excluded.size()?profile_.excluded[i]:profile_.observed[i-profile_.excluded.size()];
            if(!api_.Resolve(path,values_[i],objects_[i])||!objects_[i])return false;
            for(std::size_t j=0;j<i;++j)if(objects_[j]==objects_[i])return false;
        }
        ready_=true;return true;
    }
    bool Excludes(const void* object)const{
        if(!ready_)return false;
        for(std::size_t i=0;i<profile_.excluded.size();++i)if(objects_[i]==object)return true;
        return false;
    }
    void Observe(const void* object){
        if(!ready_)return;
        for(std::size_t i=0;i<profile_.observed.size();++i)
            if(objects_[profile_.excluded.size()+i]==object)observed_[i]=true;
    }
    bool Complete()const{
        if(!ready_)return false;
        // One observed object proves traversal. For the wrist, one meter below
        // the common SW ancestor: requiring all three would reject legitimate
        // native visibility (e.g. locked psi).
        for(std::size_t i=0;i<profile_.observed.size();++i)if(observed_[i])return true;
        return false;
    }
    bool Release(){
        ready_=false;bool ok=true;
        for(auto& value:values_)ok=api_.Release(value)&&ok;
        return ok;
    }
    ~Isolation(){Release();}
private:
    Api& api_;
    Profile profile_;
    std::array<typename Api::Value,MaxProfilePaths> values_{};
    std::array<void*,MaxProfilePaths> objects_{};
    std::array<bool,MaxProfilePaths> observed_{};
    bool attempted_=false,ready_=false;
};

// **What the HUD's own draw leaves out**: groups of paths, each left out whole
// or not at all. A group that does not resolve stays in the draw without
// costing the others (the wrist's meters, a lifted part). Every handle taken is
// released, a failed group's included; handles live for this one draw.
template<class Api, std::size_t N = 32> class Exclusion {
public:
    explicit Exclusion(Api& api) : api_(api) {}
    Exclusion(const Exclusion&)=delete;
    Exclusion& operator=(const Exclusion&)=delete;
    bool Add(std::span<const char* const> paths){
        const std::size_t first=count_;
        for(const char* path:paths){
            if(taken_==N){count_=first;return false;}
            void* object=nullptr;
            if(!api_.Resolve(path,values_[taken_++],object)||!object){count_=first;return false;}
            objects_[count_++]=object;
        }
        return true;
    }
    bool Excludes(const void* object)const{
        for(std::size_t i=0;i<count_;++i)if(objects_[i]==object)return true;
        return false;
    }
    std::size_t Count()const{return count_;}
    bool Release(){
        bool ok=true;
        for(std::size_t i=0;i<taken_;++i)ok=api_.Release(values_[i])&&ok;
        taken_=count_=0;return ok;
    }
    ~Exclusion(){Release();}
private:
    Api& api_;
    std::array<typename Api::Value,N> values_{};
    std::array<void*,N> objects_{};
    std::size_t taken_=0,count_=0;
};
}
