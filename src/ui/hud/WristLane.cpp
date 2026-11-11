#include "ui/hud/WristLane.h"

#include "diagnostics/commands/CommandArgs.h"
#include "aim/GameplayFrame.h"
#include "diagnostics/overlay/DebugOverlay.h"
#include "controls/input/Haptics.h"
#include "physics/contact/SceneQueryLane.h"
#include "ui/options/VrOptionsRuntime.h"
#include "controls/input/XrInput.h"
#include "base/LatestSnapshot.h"
#include "render/camera/StereoCamera.h"
#include "ui/hud/WristHolo.h"
#include "ui/hud/WristFrame.h"
#include "ui/hud/NativeWristNative.h"
#include "engine/EngineAccess.h"
#include "engine/NativeContract.h"
#include "controls/input/InputPost.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <mutex>

namespace preyvr::dll {
namespace {

using wrist::Card;


// ---- settings (command channel; read on every thread) ----------------------
std::atomic<unsigned> gAnchor{1};          // 1 the drawn forearm, 0 Funk's grip card
std::atomic<int> gAlongMm{75}, gHoverMm{42}, gTiltDeg{40};
std::atomic<int> gEnterFacing{500}, gExitFacing{220}, gEnterViewing{820}, gExitViewing{620};  // permille
std::atomic<int> gDwellMs{80};
std::atomic<unsigned> gForce{0};           // 1: skip the look test (captures from any pose)
std::atomic<unsigned> gOcclusion{3};       // 1 scene segments, 2 the right hand and its weapon
std::atomic<unsigned> gHaptic{1};          // a faint tick on the left controller as it appears

wrist::Placement ReadPlacement()
{
    wrist::Placement p{};
    p.along = gAlongMm.load() * 0.001f;
    p.hover = gHoverMm.load() * 0.001f;
    p.maxTiltDeg = static_cast<float>(gTiltDeg.load());
    return p;
}
wrist::Thresholds ReadThresholds()
{
    wrist::Thresholds t{};
    t.enterFacing = gEnterFacing.load() * 0.001f;
    t.exitFacing = gExitFacing.load() * 0.001f;
    t.enterViewing = gEnterViewing.load() * 0.001f;
    t.exitViewing = gExitViewing.load() * 0.001f;
    t.dwell = gDwellMs.load() * 0.001f;
    return t;
}

// ---- the arm, per rendered frame (animation thread -> XR / game) -----------
std::mutex gRingMutex;
std::array<WristArmFrame, 16> gRing{};
std::size_t gRingNext = 0;
std::atomic<unsigned long long> gPublished{0};

// Exact frame first: the eye images were rendered for `time`. Otherwise the
// newest older one within 60 ms (the animation ran ahead or a frame was
// skipped), else nothing: a card from some other frame is not on the sleeve.
enum class Match { exact, older, none };
Match FindArm(long long time, WristArmFrame& out)
{
    std::lock_guard lock(gRingMutex);
    const WristArmFrame* best = nullptr;
    for (const auto& f : gRing) {
        if (f.displayTime == 0) { continue; }
        if (f.displayTime == time) { out = f; return Match::exact; }
        if (f.displayTime < time && time - f.displayTime <= 60000000ll &&
            (!best || f.displayTime > best->displayTime)) { best = &f; }
    }
    if (!best) { return Match::none; }
    out = *best;
    return Match::older;
}
bool NewestArm(WristArmFrame& out)
{
    std::lock_guard lock(gRingMutex);
    const WristArmFrame* best = nullptr;
    for (const auto& f : gRing) {
        if (f.displayTime != 0 && f.left && (!best || f.displayTime > best->displayTime)) { best = &f; }
    }
    if (!best) { return false; }
    out = *best;
    return true;
}

wrist::Arm ToArm(const WristArmFrame& f) { return {f.wrist, f.elbow, f.palm}; }

// ---- scene occlusion (main thread) -----------------------------------------
struct Occlusion {
    bool occluded = false;
    std::uint64_t ns = 0;
};
LatestSnapshot<Occlusion> gScene;
std::atomic<bool> gCandidate{false};
std::atomic<unsigned long long> gSceneQueries{0}, gSceneHits{0};

// ---- XR thread state ---------------------------------------------------------
wrist::Presenter gPresenter;
long long gLastFrameTime = 0;
bool gHaveLastCentre = false;
Vec3 gLastCentre{};
std::atomic<unsigned long long> gExact{0}, gOlder{0}, gMissing{0}, gShownFrames{0};
std::atomic<unsigned> gGateBits{0};
std::atomic<unsigned long long> gOpenings{0};
bool gWasShowing = false;

struct Last {
    bool eligible = false, armValid = false, cardValid = false, sceneOccluded = false, handOccluded = false;
    wrist::View view{};
    float alpha = 0, tilt = 0, roll = 0, speed = 0;
    Vec3 centre{}, axis{}, dorsal{};
    long long armTime = 0, submitted = 0;
};
std::mutex gLastMutex;
Last gLast;

// ---- what a capture held (XR thread -> report) --------------------------------
struct ContentSeen {
    bool valid = false, empty = true;
    wrist::Box box{};
    unsigned long long samples = 0, outside = 0, serial = 0;
};
std::mutex gContentMutex;
ContentSeen gContent;

// ---- whether the wrist shows the psi meter (main thread) ---------------------
// Prey's own predicate (NativeWristNative.h PsiShown) on the player's psi owner,
// a few times a second: it walks the 16 power slots and builds a list.
namespace native = hud::native;
static_assert(contract::Gated(native::PsiShownContracts, native::GetPlayer) &&
              contract::Gated(native::PsiShownContracts, native::PsiShown));
std::atomic<int> gPsiShown{-1};
std::atomic<unsigned> gPsiGate{0};   // 0 unchecked, 1 bodies match, 2 refused (or faulted)
unsigned gPsiTick = 0;
bool PsiBodiesMatch(std::uintptr_t base)
{
    __try { return base && contract::BodiesMatch(base, native::PsiShownContracts); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
// 1 shown, 0 not, -1 no player yet, -2 faulted.
int ReadPsiShown(std::uintptr_t base)
{
    __try {
        const auto player = reinterpret_cast<std::uintptr_t(__fastcall*)()>(base + native::GetPlayer)();
        if (!player) { return -1; }
        const auto owner = *reinterpret_cast<const std::uintptr_t*>(player + native::PlayerPsiOwner);
        if (!owner) { return -1; }
        return reinterpret_cast<bool(__fastcall*)(std::uintptr_t)>(base + native::PsiShown)(owner) ? 1 : 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return -2; }
}
void UpdatePsiShown()
{
    if (!WristDisplayEnabled() || !OnEngineMainThread() || gPsiTick++ % 15 != 0) { return; }
    const auto base = PreyBase();
    if (!base) { return; }
    if (gPsiGate.load() == 0) { gPsiGate.store(PsiBodiesMatch(base) ? 1u : 2u); }
    if (gPsiGate.load() != 1) { gPsiShown.store(-1); return; }
    const int shown = ReadPsiShown(base);
    if (shown == -2) { gPsiGate.store(2); }   // never again this session
    gPsiShown.store(shown < 0 ? -1 : shown);
}

}  // namespace

void PublishWristArm(const WristArmFrame& frame)
{
    std::lock_guard lock(gRingMutex);
    gRing[gRingNext] = frame;
    gRingNext = (gRingNext + 1) % gRing.size();
    gPublished.fetch_add(1, std::memory_order_relaxed);
}

void UpdateWristLane(const GameplayPoseFrame& frame, bool tracking)
{
    UpdatePsiShown();
    if (!(gOcclusion.load() & 1) || !gCandidate.load() || !tracking || !frame.cameraCentreValid) {
        gScene.Publish({false, MonotonicNanoseconds()});
        return;
    }
    WristArmFrame arm{};
    if (!NewestArm(arm)) { return; }
    const Card card = wrist::PlaceCard(ToArm(arm), frame.tracking.head, ReadPlacement());
    if (!card.valid) { return; }
    const Vec3 eye = frame.cameraCentre;
    const float half = wrist::MetersHalfSpan(WristSizePercent());
    bool occluded = false;
    // The centre and both ends along the forearm: an arm half in a wall shows
    // the card over the wall from one end.
    for (const float s : {0.0f, -half, half}) {
        const Vec3 target = stereo::TrackingToEngine(Add(card.pose.position, Scale(card.axis, s)), frame.cameraCentre, PlaySpaceRotation(frame), frame.tracking.head);
        const Vec3 d = Sub(target, eye);
        const float length = Length(d);
        if (!(length > 0.12f) || length > 2.0f) { continue; }
        const Vec3 unit = Scale(d, 1.0f / length);
        // From just ahead of the eye to just short of the card: the card is not
        // physical, and the start must not be inside the head's own geometry.
        const Vec3 a = Add(eye, Scale(unit, 0.05f));
        const Vec3 b = Sub(target, Scale(unit, 0.015f));
        scene::Hit hit{};
        gSceneQueries.fetch_add(1, std::memory_order_relaxed);
        if (QuerySegment(frame, a, b, hit) && hit.distance >= 0.0f) {
            occluded = true;
            gSceneHits.fetch_add(1, std::memory_order_relaxed);
            break;
        }
    }
    gScene.Publish({occluded, MonotonicNanoseconds()});
}

WristDecision DecideWrist(bool eligible, const TrackingFrame& latest, long long submittedTime,
                          long long frameTime)
{
    WristDecision decision{};
    float dt = gLastFrameTime ? static_cast<float>(frameTime - gLastFrameTime) * 1e-9f : 0.0f;
    gLastFrameTime = frameTime;
    if (!std::isfinite(dt) || dt < 0.0f || dt > 0.1f) { dt = 0.0f; }

    WristArmFrame arm{};
    const Match match = FindArm(submittedTime, arm);
    (match == Match::exact ? gExact : match == Match::older ? gOlder : gMissing).fetch_add(1, std::memory_order_relaxed);
    const bool armValid = match != Match::none && arm.left;
    Card card{};
    if (armValid) { card = wrist::PlaceCard(ToArm(arm), latest.head, ReadPlacement()); }
    wrist::View view = card.valid ? wrist::Measure(card, latest.head) : wrist::View{};
    if (gForce.load() && card.valid) { view.facing = 1.0f; view.viewing = 1.0f; }

    // The right hand and its weapon between the eye and the card.
    bool handOccluded = false;
    if ((gOcclusion.load() & 2) && card.valid && arm.right) {
        const Vec3 eye = latest.head.position, c = card.pose.position;
        handOccluded = wrist::SegmentNearPoint(eye, c, arm.rightWrist, 0.05f) ||
                       wrist::SegmentNearPoint(eye, c, arm.rightGrip, 0.06f);
        if (!handOccluded && arm.weapon) {
            handOccluded = wrist::SegmentNearSegment(eye, c, Sub(arm.rightGrip, Scale(arm.rightAim, 0.12f)),
                                                     Add(arm.rightGrip, Scale(arm.rightAim, 0.40f)), 0.05f);
        }
    }
    Occlusion scene{};
    const bool sceneFresh = gScene.TryRead(scene) &&
        MonotonicNanoseconds() - scene.ns < 150000000ull;
    const bool sceneOccluded = sceneFresh && scene.occluded;

    float speed = 0.0f;
    const auto& left = latest.hands[static_cast<unsigned>(Hand::left)];
    if (left.gripVelocityValid) {
        speed = Length(left.gripLinearVelocity);
    } else if (card.valid && gHaveLastCentre && dt > 0.0f) {
        speed = Length(Sub(card.pose.position, gLastCentre)) / dt;
    }
    gHaveLastCentre = card.valid;
    if (card.valid) { gLastCentre = card.pose.position; }

    wrist::Conditions conditions{};
    conditions.eligible = eligible && card.valid;
    conditions.occluded = sceneOccluded || handOccluded;
    conditions.handSpeed = speed;
    gPresenter.Update(view, conditions, dt, ReadThresholds());
    if (gPresenter.Showing() && !gWasShowing) {
        gOpenings.fetch_add(1, std::memory_order_relaxed);
        if (gHaptic.load()) { QueueHaptic(Hand::left, haptics::Event::WristOpened, latest); }
    }
    gWasShowing = gPresenter.Showing();
    gCandidate.store(gPresenter.Candidate() || gPresenter.Alpha() > 0.0f);

    // The frame's size at Wrist Display Size, and nothing else: no fitting, no
    // unfold. Its centre is moved off the placed point so the meters, not the
    // frame, sit where Wrist Display Position and Height put them.
    const wrist::CardSize size = wrist::Size(WristSizePercent());
    decision.active = card.valid && gPresenter.Alpha() > 0.004f;
    decision.alpha = gPresenter.Alpha();
    decision.pose = card.pose;
    if (card.valid) {
        const Vec3 across = Rotate(card.pose.orientation, {1.0f, 0.0f, 0.0f});
        const Vec3 up = Rotate(card.pose.orientation, {0.0f, 1.0f, 0.0f});
        decision.pose.position = Add(card.pose.position, Add(Scale(across, size.offsetX), Scale(up, size.offsetY)));
    }
    decision.width = size.width;
    decision.height = size.height;
    // Warm the native capture while the look is forming, so the first faded-in
    // frame already has this frame's meters.
    decision.capture = eligible && card.valid && (gPresenter.Candidate() || decision.active);
    if (decision.active) { gShownFrames.fetch_add(1, std::memory_order_relaxed); }
    if (card.valid) {
        const auto t = ReadThresholds();
        const bool shown = gPresenter.Showing();
        DebugOverlayWristCard(conditions.eligible && !conditions.occluded, decision.active, decision.pose, decision.width,
                              decision.height, view.distance, view.facing, view.viewing,
                              shown ? t.exitFacing : t.enterFacing, shown ? t.exitViewing : t.enterViewing);
    }

    {
        std::lock_guard lock(gLastMutex);
        gLast.eligible = eligible;
        gLast.armValid = armValid;
        gLast.cardValid = card.valid;
        gLast.sceneOccluded = sceneOccluded;
        gLast.handOccluded = handOccluded;
        gLast.view = view;
        gLast.alpha = gPresenter.Alpha();
        gLast.tilt = card.tiltDeg;
        gLast.roll = card.rollDeg;
        gLast.speed = speed;
        gLast.centre = card.pose.position;
        gLast.axis = card.axis;
        gLast.dorsal = card.dorsal;
        gLast.armTime = arm.displayTime;
        gLast.submitted = submittedTime;
    }
    return decision;
}

bool WristLegacyAnchor() { return gAnchor.load() == 0; }
void NoteWristGate(unsigned bits) { gGateBits.store(bits, std::memory_order_relaxed); }
void NoteWristContent(bool empty, const float (&box)[4], const float (&frame)[4], unsigned long long serial)
{
    std::lock_guard lock(gContentMutex);
    if (serial == gContent.serial) { return; }
    gContent.serial = serial;
    gContent.valid = true;
    gContent.empty = empty;
    ++gContent.samples;
    if (empty) { return; }
    gContent.box = wrist::ToFrameUnits(frame, box);
    if (wrist::Outside(gContent.box)) { ++gContent.outside; }
}
int WristPsiShown() { return gPsiShown.load(std::memory_order_relaxed); }
bool WristMovesStatusOffFace() { return WristDisplayEnabled(); }
DWORD SetWristTick(unsigned enabled)
{
    if (enabled > 1) { return ERROR_INVALID_PARAMETER; }
    gHaptic.store(enabled);
    return 0;
}
unsigned WristTickEnabled() { return gHaptic.load() ? 1u : 0u; }
DWORD SetWristAlongMm(int mm)
{
    if (mm < -50 || mm > 200) { return ERROR_INVALID_PARAMETER; }
    gAlongMm.store(mm);
    return 0;
}
DWORD SetWristHoverMm(int mm)
{
    if (mm < 0 || mm > 150) { return ERROR_INVALID_PARAMETER; }
    gHoverMm.store(mm);
    return 0;
}
DWORD SetWristTiltDegrees(int degrees)
{
    if (degrees < 0 || degrees > 90) { return ERROR_INVALID_PARAMETER; }
    gTiltDeg.store(degrees);
    return 0;
}
int WristAlongMm() { return gAlongMm.load(); }
int WristHoverMm() { return gHoverMm.load(); }
int WristTiltDegrees() { return gTiltDeg.load(); }

void ResetWristLane()
{
    gPresenter.Reset();
    gWasShowing = false;
    gLastFrameTime = 0;
    gHaveLastCentre = false;
    gCandidate = false;
}

std::string WristReport()
{
    Last l{};
    {
        std::lock_guard lock(gLastMutex);
        l = gLast;
    }
    ContentSeen c{};
    {
        std::lock_guard lock(gContentMutex);
        c = gContent;
    }
    // content=: what the last sampled capture held, in quadrant units, against
    // frame= (wrist::kFrame); outside= counts samples that reached past it.
    char content[96] = "none";
    if (c.valid && c.empty) { std::snprintf(content, sizeof(content), "empty"); }
    else if (c.valid) {
        std::snprintf(content, sizeof(content), "%.0f,%.0f,%.0f,%.0f", c.box.left, c.box.top, c.box.right, c.box.bottom);
    }
    const auto size = wrist::Size(WristSizePercent());
    char b[1280];
    std::snprintf(b, sizeof(b),
        " enabled=%d anchor=%u place=%d,%d,%d gate=%d,%d,%d,%d dwell=%d force=%u occlusion=%u psi=%d"
        " sizeMm=%.0f,%.0f frame=%.0f,%.0f,%.0f,%.0f content=%s samples=%llu outside=%llu"
        " | gate=%u eligible=%d arm=%d card=%d alpha=%.2f facing=%.2f viewing=%.2f dist=%.3f tilt=%.1f roll=%.1f speed=%.2f"
        " sceneOcc=%d handOcc=%d | centre=%.4f,%.4f,%.4f axis=%.3f,%.3f,%.3f dorsal=%.3f,%.3f,%.3f"
        " | armLagMs=%.1f published=%llu exact=%llu older=%llu missing=%llu shown=%llu openings=%llu sceneQ=%llu sceneHits=%llu",
        WristDisplayEnabled() ? 1 : 0, gAnchor.load(), gAlongMm.load(), gHoverMm.load(), gTiltDeg.load(),
        gEnterFacing.load(), gExitFacing.load(), gEnterViewing.load(), gExitViewing.load(), gDwellMs.load(),
        gForce.load(), gOcclusion.load(), gPsiShown.load(), size.width * 1000.0f, size.height * 1000.0f,
        wrist::kFrame.left, wrist::kFrame.top, wrist::kFrame.right, wrist::kFrame.bottom, content, c.samples, c.outside,
        gGateBits.load(), l.eligible ? 1 : 0, l.armValid ? 1 : 0, l.cardValid ? 1 : 0, l.alpha, l.view.facing, l.view.viewing,
        l.view.distance, l.tilt, l.roll, l.speed, l.sceneOccluded ? 1 : 0, l.handOccluded ? 1 : 0,
        l.centre.x, l.centre.y, l.centre.z, l.axis.x, l.axis.y, l.axis.z, l.dorsal.x, l.dorsal.y, l.dorsal.z,
        l.armTime ? (l.submitted - l.armTime) * 1e-6 : -1.0, gPublished.load(), gExact.load(), gOlder.load(),
        gMissing.load(), gShownFrames.load(), gOpenings.load(), gSceneQueries.load(), gSceneHits.load());
    return b;
}

bool ExecuteWristCommand(const std::vector<std::string>& args, std::ostringstream& out)
{
    if (args.empty() || args[0].rfind("wrist.", 0) != 0) { return false; }
    const std::string& verb = args[0];
    const auto num = [&](std::size_t i, int fallback) { return Arg(args, i, fallback); };
    if (verb == "wrist.report") {
        out << "wrist.report result=0" << WristReport();
    } else if (verb == "wrist.anchor") {
        // These report when bare (docs/TUNING-COMMANDS.md): a value sets.
        if (args.size() > 1) { gAnchor = num(1, 1) ? 1u : 0u; }
        out << "wrist.anchor result=0 anchor=" << gAnchor.load();
    } else if (verb == "wrist.place") {
        // wrist.place <along mm> <hover mm> <max tilt deg>: the size is Wrist Display Size's.
        gAlongMm = std::clamp(num(1, gAlongMm.load()), -50, 200);
        gHoverMm = std::clamp(num(2, gHoverMm.load()), 0, 150);
        gTiltDeg = std::clamp(num(3, gTiltDeg.load()), 0, 90);
        out << "wrist.place result=0" << WristReport();
    } else if (verb == "wrist.gate") {
        // wrist.gate <enter facing> <exit facing> <enter viewing> <exit viewing> [dwell ms]   (permille)
        gEnterFacing = std::clamp(num(1, gEnterFacing.load()), -1000, 1000);
        gExitFacing = std::clamp(num(2, gExitFacing.load()), -1000, 1000);
        gEnterViewing = std::clamp(num(3, gEnterViewing.load()), -1000, 1000);
        gExitViewing = std::clamp(num(4, gExitViewing.load()), -1000, 1000);
        gDwellMs = std::clamp(num(5, gDwellMs.load()), 0, 2000);
        out << "wrist.gate result=0" << WristReport();
    } else if (verb == "wrist.force") {
        if (args.size() > 1) { gForce = num(1, 1) ? 1u : 0u; }
        out << "wrist.force result=0 force=" << gForce.load();
    } else if (verb == "wrist.haptic") {
        if (args.size() > 1) { gHaptic = num(1, 1) ? 1u : 0u; }
        out << "wrist.haptic result=0 haptic=" << gHaptic.load();
    } else if (verb == "wrist.occlusion") {
        if (args.size() > 1) { gOcclusion = static_cast<unsigned>(std::clamp(num(1, 3), 0, 3)); }
        out << "wrist.occlusion result=0 mask=" << gOcclusion.load();
    } else {
        return false;
    }
    return true;
}

}  // namespace preyvr::dll
