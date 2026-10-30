// Exercise the production cached-ray hook with a fake player buffer and external
// tracking/muzzle providers. No game module is loaded and no hook is installed.
#include <windows.h>
HMODULE WINAPI ReviewModule(LPCWSTR);
#define GetModuleHandleW ReviewModule
// The aim hook and the two files its detour calls into: the frame build and the
// two-handed hold. One translation unit, so their file-local names are distinct.
#include "../../../src/dll/AimTakeover.cpp"
#include "../../../src/dll/GameplayFrame.cpp"
#include "../../../src/dll/TwoHandHold.cpp"
#undef GetModuleHandleW
#include <iostream>

preyvr::dll::TrackingFrame tracked;
preyvr::Vec3 drawnOrigin, drawnDirection;
bool focused = true, calibrated = false, examiningScreen = false;
const preyvr::Vec3 eye{10, 20, 30}, muzzle{10.3f, 20.5f, 29.8f};
int failures = 0;
std::array<std::uint8_t, preyvr::engine::SystemLayout::viewCamera +
    preyvr::engine::CameraLayout::size> fakeSystem{};
std::uint8_t* systemSlot = fakeSystem.data();
HMODULE WINAPI ReviewModule(LPCWSTR) {
    // Stub only the module lookup. Production GameCameraYaw still walks the
    // actual slot/camera offsets and reads the supplied matrix.
    return reinterpret_cast<HMODULE>(reinterpret_cast<std::uintptr_t>(&systemSlot) -
        preyvr::engine::SystemLayout::pointerRva);
}
preyvr::dll::ReticleAimContext drawnContext{};

namespace preyvr::lifecycle { void Log(std::string_view) {} }
namespace preyvr::dll {
bool EnsureMinHook() { return false; }
bool TryGetTrackingFrame(TrackingFrame& out) {
    if (!focused) return false;
    out = tracked;
    return true;
}
unsigned long long HeadTrackingReferenceGeneration() { return 2; }
unsigned long long HeadTrackingRecenterEpoch() { return 1; }
float HeadTrackingReferenceYaw() { return 0; }
// Level play: no zero-G, so the frame's tilt stays the identity.
bool PlayerInZeroG() { return false; }
AppliedView LastAppliedView() { return {}; }
std::uint64_t WeaponEquipGeneration() { return 7; }
bool TryGetMuzzleFromGrip(const Pose&, std::uint64_t, Vec3& out) {
    if (!calibrated) return false;
    out = muzzle;
    return true;
}
bool WriteReticleScreenPosition(void*, const Vec3& origin, const Vec3& direction,
                               const ReticleAimContext* context) {
    drawnOrigin = origin; drawnDirection = direction;
    if (context) drawnContext = *context;
    return true;
}
// What the aim frame reads, at rest: no rig, no two-handed hold, no shot ray or
// scene hit, so the published ray is the takeover's own.
bool HudGameplayInputAllowed() { return true; }
bool MedkitOwnsGrip() { return false; }
bool TryGetEquippedRig(std::uintptr_t, EquippedRig&) { return false; }
bool ShotRayFrame(const GameplayPoseFrame&, aim::Sample&) { return false; }
void QueryAimScene(const GameplayPoseFrame&, aim::Sample&) {}
void PublishReticleTarget(const GameplayPoseFrame&, const aim::Sample&) {}
void QueueHaptic(Hand, haptics::Event, const TrackingFrame&) {}
void DebugOverlayAim(const GameplayPoseFrame&, const aim::Sample&) {}
void DebugOverlayForegrip(const GameplayPoseFrame&, const twohand::Input&, const twohand::Output&, bool) {}
// Whether a screen is examined (ExamineCamera.h), and the getter hook the world
// UI's watcher would register with (ReticleRayHook.h): none here.
bool ExaminingScreen(std::uintptr_t) { return examiningScreen; }
bool EnsureReticleRayHook() { return false; }
void SetReticleRayFilter(ReticleRayClient, ReticleRayFilter) {}
// The gameplay schedule the frame is handed to (FrameSchedule.h).
namespace schedule {
void PrepareGameplayFrame(GameplayPoseFrame&, bool) {}
void TickGameplayLanes(const GameplayPoseFrame&, bool, bool) {}
}
}
void __fastcall NativeRay(void* player) {
    auto* bytes = static_cast<std::uint8_t*>(player);
    const preyvr::Vec3 forward{0, 1, 0};
    std::memcpy(bytes + preyvr::engine::ArkPlayerLayout::cachedReticleOrigin, &eye, sizeof(eye));
    std::memcpy(bytes + preyvr::engine::ArkPlayerLayout::cachedReticleDirection, &forward, sizeof(forward));
}
bool Same(preyvr::Vec3 a, preyvr::Vec3 b) {
    return std::fabs(a.x-b.x) < 1e-5f && std::fabs(a.y-b.y) < 1e-5f && std::fabs(a.z-b.z) < 1e-5f;
}
void Check(bool ok, const std::string& name) {
    std::cout << (ok ? "PASS " : "FAIL ") << name << '\n';
    failures += !ok;
}
int main() {
    using namespace preyvr;
    using namespace preyvr::dll;
    std::array<std::uint8_t, 0x1900> player{};
    const auto camera = std::span<std::uint8_t>(fakeSystem).subspan(
        engine::SystemLayout::viewCamera, engine::CameraLayout::size);
    stereo::WriteMatrix(camera, stereo::MatrixFromPose({}));
    tracked.headValidity = {true, true, true, true, 0};
    tracked.epoch = 1; tracked.sequence = 3;
    auto& hand = tracked.hands[1];
    hand.aimValidity = hand.gripValidity = tracked.headValidity;
    hand.aimPose.position = {0.2f, -0.1f, -0.4f};
    hand.gripPose.position = hand.aimPose.position;
    gOriginal = &NativeRay;
    gEnabled = true;
    for (int hasCalibration = 0; hasCalibration <= 1; ++hasCalibration) {
        calibrated = hasCalibration != 0;
        for (unsigned mode = 0; mode <= 2; ++mode) {
            gOriginMode = mode;
            tracked.publishedNs = MonotonicNanoseconds();
            UpdateCachedRayWithTakeover(player.data());
            UpdateAimReticleForRender();
            Vec3 installed{};
            std::memcpy(&installed, player.data() + engine::ArkPlayerLayout::cachedReticleOrigin, sizeof(installed));
            aim::Sample sample;
            const std::string label = "origin mode=" + std::to_string(mode) +
                                      " calibration=" + std::to_string(hasCalibration);
            Check(TryGetAimSample(sample) && Same(sample.origin, installed) && Same(drawnOrigin, installed), label);
            Check(sample.confidence == aim::Confidence::origin, label + " does not claim a calibrated barrel direction");
            Check(Same(Rotate(sample.orientation, {0, 1, 0}), sample.direction), label + " orientation and direction share world space");
        }
    }
    // Hold a nontrivial controller orientation in LOCAL space, turn the head
    // through a 60-degree arc, and let the production producer derive play yaw
    // from the matching camera. A camera-relative implementation must fail.
    const float bodyYaw = 0.71f;
    hand.aimPose.orientation = Normalize({0.13f, -0.21f, 0.07f, 0.95f});
    gOriginMode = 1;
    Vec3 heldDirection{};
    int firstCameraMdeg = 0, lastCameraMdeg = 0;
    for (int degrees = -30; degrees <= 30; degrees += 5) {
        const float yaw = degrees * 0.0174532925199433f;
        tracked.head.orientation = {0, std::sin(yaw / 2), 0, std::cos(yaw / 2)};
        stereo::WriteMatrix(camera, stereo::MatrixFromPose(
            {stereo::YawQuaternion(bodyYaw + yaw), eye}));
        ++tracked.sequence;
        tracked.publishedNs = MonotonicNanoseconds();
        UpdateCachedRayWithTakeover(player.data());
        UpdateAimReticleForRender();
        aim::Sample swept{};
        const bool available = TryGetAimSample(swept);
        if (degrees == -30) { heldDirection = swept.direction; firstCameraMdeg = AimCameraYawMilliDegrees(); }
        lastCameraMdeg = AimCameraYawMilliDegrees();
        Check(available && Same(swept.direction, heldDirection) &&
            Same(drawnDirection, heldDirection), "fixed LOCAL controller, head yaw=" + std::to_string(degrees));
        Check(drawnContext.trackingSequence == tracked.sequence &&
              std::fabs(drawnContext.playYaw - bodyYaw) < 1e-5f &&
              drawnContext.controller.orientation.w == hand.aimPose.orientation.w,
              "render provenance belongs to selected aim publication");
    }
    // Positive control: the sweep turned the camera through 60 degrees, so a
    // camera-relative composition would have turned the aim with it.
    Check(std::abs(lastCameraMdeg - firstCameraMdeg - 60000) < 100,
        "positive control: the camera turned through the sweep");
    // A screen being examined: the cached ray -- the one Prey casts the screen's
    // cursor from -- starts on the hand's line level with the eye, placed against
    // the camera centre, whatever aim.origin says (still 1 here).
    examiningScreen = true;
    ++tracked.sequence;
    tracked.publishedNs = MonotonicNanoseconds();
    UpdateCachedRayWithTakeover(player.data());
    Vec3 atScreen{};
    std::memcpy(&atScreen, player.data() + engine::ArkPlayerLayout::cachedReticleOrigin, sizeof(atScreen));
    aim::Sample screenSample{};
    GameplayPoseFrame screenFrame{};
    Pose screenHand{};
    const bool screenRead = TryGetAimSample(screenSample) && TryGetGameplayPoseFrame(screenFrame) &&
        HandWorld(screenFrame, Hand::right, ControllerPose::aim, Anchor::camera, Placement::controller, screenHand);
    Check(screenRead && Same(atScreen, examine::ScreenRayOrigin(screenHand.position, screenFrame.cameraCentre,
                                                                screenSample.direction)),
          "a screen: the cached ray starts on the hand's line, level with the eye");
    Check(screenRead && std::fabs(Dot(Sub(atScreen, screenFrame.cameraCentre), screenSample.direction)) < 1e-4f &&
              !Same(atScreen, screenHand.position) && !Same(atScreen, eye),
          "a screen: neither the hand aim.origin would use nor the eye");
    Check(screenRead && Same(screenSample.origin, atScreen) && screenSample.cameraAnchored,
          "a screen: the sample starts there too, drawn against the camera centre");
    // The world UI's cursor raycast is handed the crosshair's ray (the sample).
    // At an examined screen that is the cached ray already: nothing moves.
    gWorldUiReturn = 0x301911;
    float read[6]{};
    std::memcpy(read, &atScreen, sizeof(atScreen));
    std::memcpy(read + 3, &screenSample.direction, sizeof(screenSample.direction));
    WorldUiRay(0, read, read, 0x301911);
    Vec3 handed{}, handedDirection{};
    std::memcpy(&handed, read, sizeof(handed));
    std::memcpy(&handedDirection, read + 3, sizeof(handedDirection));
    const std::string steered = ScreenAimReport();
    Check(Same(handed, atScreen) && steered.find("frames=1 ") != std::string::npos &&
              steered.find("steered=1}") != std::string::npos,
          "an examined screen reads the hand's ray, as cached (" + steered + ")");
    // Prey's ray from the eye -- a gun's crosshair starts at the muzzle -- is
    // replaced by the sample's.
    std::memcpy(read, &eye, sizeof(eye));
    WorldUiRay(0, read, read, 0x301911);
    std::memcpy(&handed, read, sizeof(handed));
    std::memcpy(&handedDirection, read + 3, sizeof(handedDirection));
    Check(Same(handed, screenSample.origin) && Same(handedDirection, screenSample.direction) &&
              ScreenAimReport().find("steered=2}") != std::string::npos,
          "an eye-origin ray is handed the crosshair's");
    // Any other reader of the ray keeps what it read.
    std::memcpy(read, &eye, sizeof(eye));
    WorldUiRay(0, read, read, 0x159A70A);
    Vec3 kept{};
    std::memcpy(&kept, read, sizeof(kept));
    Check(Same(kept, eye) && ScreenAimReport().find("steered=2}") != std::string::npos,
          "another caller (the target selector) is left alone");
    // Done examining: aim.origin's hand again, and the next rebuild got Prey's own.
    examiningScreen = false;
    ++tracked.sequence;
    tracked.publishedNs = MonotonicNanoseconds();
    UpdateCachedRayWithTakeover(player.data());
    aim::Sample afterScreen{};
    Check(TryGetAimSample(afterScreen) && !afterScreen.cameraAnchored && !Same(afterScreen.origin, atScreen),
          "after the screen: the ray aim.origin asks for");
    gWorldUiReturn = 0;

    focused = false;
    UpdateCachedRayWithTakeover(player.data());
    aim::Sample sample;
    Check(!TryGetAimSample(sample), "failed tracking invalidates previous aim publication");
    // No usable sample: the screen keeps Prey's ray.
    gWorldUiReturn = 0x301911;
    std::memcpy(read, &eye, sizeof(eye));
    WorldUiRay(0, read, read, 0x301911);
    std::memcpy(&kept, read, sizeof(kept));
    Check(Same(kept, eye) && ScreenAimReport().find("steered=2}") != std::string::npos,
          "with no aim sample the screen's ray is Prey's");
    gWorldUiReturn = 0;
    return failures ? 1 : 0;
}
