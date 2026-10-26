// The examination camera lane: the production detour with Prey replaced at its
// ABI boundary -- the fit's original, the examination, the view camera, the
// examined entity's position, the gate and the installer.
#include "../src/dll/ExamineCamera.cpp"
#include "DllTestSupport.h"
#include "TestSupport.h"
#include <cmath>
#include <cstring>
#include <iostream>
#include <string>

PREYVR_TEST_CAPTURE_LOG()
PREYVR_TEST_STUB_MINHOOK()
namespace {
using namespace preyvr;
using namespace preyvr::dll;
using preyvr::test::Check;
using preyvr::test::Logged;
namespace en = preyvr::examine::native;

// "examine={pullbackCm=30 ... last={screenMm=...}}" with each field between spaces.
bool Reports(const char* field)
{
    std::string report = " " + ExamineCameraReport() + " ";
    for (char& c : report) {
        if (c == '{' || c == '}') { c = ' '; }
    }
    return report.find(std::string(" ") + field + " ") != std::string::npos;
}

template <class T> void Put(void* base, std::size_t offset, const T& value)
{
    std::memcpy(static_cast<std::uint8_t*>(base) + offset, &value, sizeof(value));
}
bool Near(float a, float b) { return std::fabs(a - b) < 1e-5f; }

// The examination as Prey's begin leaves it: a world UI, entity 7, a 5 cm
// viewingBuffer. The screen's corner at the origin, in the plane y = 0; Prey's
// fit puts the camera 10 cm in front of it, facing it (looking along -Y).
alignas(16) std::uint8_t examination[0x120]{};
constexpr Vec3 kFit{0.2f, 0.1f, 1.5f};
constexpr Quaternion kFacing{0.0f, 0.0f, 1.0f, 0.0f};
float* Target() { return reinterpret_cast<float*>(examination + en::ExamTarget); }
Vec3 TargetNow()
{
    Vec3 v{};
    std::memcpy(&v, Target(), sizeof(v));
    return v;
}
bool At(Vec3 p) { const Vec3 t = TargetNow(); return Near(t.x, p.x) && Near(t.y, p.y) && Near(t.z, p.z); }
Quaternion facing = kFacing;
void Examine(int type, std::uint32_t entity)
{
    Put(examination, en::ExamType, type);
    Put(examination, en::ExamEntity, entity);
    Put(examination, en::ExamViewingBuffer, 0.05f);
    Put(examination, en::ExamTarget, kFit);   // what Prey's fit wrote, every call
}

// The view camera: the eye, and the vertical FOV (117.5 degrees).
alignas(16) std::uint8_t camera[engine::CameraLayout::size]{};
void Eye(Vec3 at)
{
    float matrix[12] = {1, 0, 0, at.x, 0, 1, 0, at.y, 0, 0, 1, at.z};
    std::memcpy(camera + engine::CameraLayout::matrix, matrix, sizeof(matrix));
    Put(camera, engine::CameraLayout::fov, 117.5f * kRadiansPerDegree);
}
const std::uint8_t* FakeViewCamera() { return camera; }
int entityToken = 0;
bool FakeEntityOrigin(void* entity, Vec3& out)
{
    Check(entity == &entityToken, "the examined entity's position is the one asked for");
    out = {};
    return true;
}

// Prey's fit: writes the camera's target and facing, AL 1.
unsigned fits = 0;
std::uintptr_t __fastcall FakeFit(void*, float* target, float* facingOut, void*)
{
    ++fits;
    std::memcpy(target, &kFit, sizeof(kFit));
    std::memcpy(facingOut, &kFacing, sizeof(kFacing));
    return 0x1234500000000001ull;
}

void Begin() { AfterFit(examination, Target(), &facing.x, &entityToken, true, en::BeginReturn); }
void Frame() { AfterFit(examination, Target(), &facing.x, &entityToken, true, en::FrameReturn); }

unsigned resolves = 0, installs = 0;
DWORD readiness = ERROR_SUCCESS;
constexpr std::uintptr_t kFakeBase = 0x10000000;
DWORD FakeResolve(std::uintptr_t& base)
{
    ++resolves;
    base = readiness ? 0 : kFakeBase;
    return readiness;
}
bool FakeInstall(std::uintptr_t target)
{
    ++installs;
    Check(target == kFakeBase + en::FitView, "the hook goes on the fit");
    gOriginal = FakeFit;
    return true;
}
void Retry()
{
    gInstallTried = false;
    gGate = "unchecked";
}
} // namespace

int main()
{
    gViewCamera = FakeViewCamera;
    gEntityOrigin = FakeEntityOrigin;
    Check(Reports("pullbackCm=30") && Reports("active=0") && Reports("hooks=0") && Reports("gate=unchecked"),
          "30 cm by default, nothing hooked, not moving the camera");
    Check(SetExaminePullbackCm(101) == ERROR_INVALID_PARAMETER && ExaminePullbackCm() == 30, "over 100 cm is refused");
    Check(SetExaminePullbackCm(100) == 0 && ExaminePullbackCm() == 100 && SetExaminePullbackCm(0) == 0 &&
              ExaminePullbackCm() == 0,
          "0 to 100 cm taken");
    SetExaminePullbackCm(30);
    Check(preyvr::test::gMinHookCalls == 0, "setting it hooks nothing");

    // Not ready (no PreyDll yet): not tried, so tried again.
    gResolve = FakeResolve;
    gInstall = FakeInstall;
    readiness = ERROR_NOT_READY;
    EnsureExamineCamera();
    Check(installs == 0 && Reports("gate=unchecked") && Reports("active=0") && Logged("preyvr_examine", "no_preydll"),
          "no PreyDll: not yet");
    EnsureExamineCamera();
    Check(resolves == 2, "asked again once PreyDll may be there");

    // Other bytes: refused for good.
    readiness = ERROR_BAD_EXE_FORMAT;
    EnsureExamineCamera();
    Check(installs == 0 && Reports("gate=refused") && Reports("active=0") &&
              Logged("result=unavailable", "bytes_mismatch", "ExamineFitView"),
          "mismatched bytes refuse the hook");
    EnsureExamineCamera();
    Check(resolves == 3, "and a refusal is not retried");

    // The real installer with MinHook never initialised: the hook cannot be made.
    Retry();
    readiness = ERROR_SUCCESS;
    gInstall = Install;
    EnsureExamineCamera();
    Check(Reports("gate=hook_failed") && Reports("hooks=0") && Reports("active=0") &&
              Logged("result=failed", "create_hook", "ExamineFitView"),
          "a hook that cannot be created is reported, and Prey's camera kept");

    // PREYVR_EXAMINE_CAMERA=0 at launch: unhooked.
    Retry();
    gInstall = FakeInstall;
    SetEnvironmentVariableW(L"PREYVR_EXAMINE_CAMERA", L"0");
    EnsureExamineCamera();
    SetEnvironmentVariableW(L"PREYVR_EXAMINE_CAMERA", nullptr);
    Check(installs == 0 && Reports("active=0") && Logged("disabled_by_environment"), "the launch switch leaves it unhooked");

    // VR start: hooked, and moving the camera.
    EnsureExamineCamera();
    Check(installs == 1 && Reports("hooks=1") && Reports("gate=ok") && Reports("active=1") &&
              Logged("hook_installed", "ExamineFitView", "rows=3"),
          "VR start hooks the fit");
    EnsureExamineCamera();
    Check(installs == 1, "once");

    // The begin: the eye 90 cm from the screen, 80 behind the fit. 30 cm back.
    Eye(Vec3{0.5f, 0.9f, 1.6f});
    Examine(en::TypeWorldUi, 7);
    Begin();
    Check(At(Vec3{0.2f, 0.4f, 1.5f}), "the begin's camera is 30 cm back along its view, centred on the screen");
    Check(Reports("begins=1") && Reports("moved=1") && Reports("screenMm=100") && Reports("eyeMm=800") &&
              Reports("pullMm=300") && Reports("fovDeci=1175") && Reports("bufferMm=50"),
          "the report: the fit 10 cm from the screen, the eye 80 cm behind it, Prey's FOV and buffer");
    // Every frame Prey's fit writes its own again; it is moved back again.
    Examine(en::TypeWorldUi, 7);
    Frame();
    Check(At(Vec3{0.2f, 0.4f, 1.5f}) && Reports("moved=2") && Reports("begins=1"), "and every frame after");

    // Never further back than the eye was.
    SetExaminePullbackCm(100);
    Examine(en::TypeWorldUi, 7);
    Frame();
    Check(At(Vec3{0.2f, 0.9f, 1.5f}) && Reports("capped=1") && Reports("pullMm=800"), "100 cm stops at the eye, 80 back");
    SetExaminePullbackCm(30);
    // Standing nearer the screen than Prey's camera: Prey's own.
    Eye(Vec3{0.2f, 0.05f, 1.5f});
    Examine(en::TypeWorldUi, 9);
    Begin();
    Check(At(kFit) && Reports("eyeMm=-50") && Reports("pullMm=0") && Reports("capped=2"),
          "an eye nearer the screen than the fit: not moved backwards");

    // An examination whose begin was not seen: its eye is not known.
    Examine(en::TypeWorldUi, 11);
    Frame();
    Check(At(kFit) && Reports("noEye=1"), "no begin seen for this entity: Prey's camera");

    // Other examinations, failed fits and unknown callers: untouched.
    Eye(Vec3{0.5f, 0.9f, 1.6f});
    Examine(2, 7);
    Begin();
    Check(At(kFit) && Reports("begins=2"), "a telescope is Prey's");
    Examine(en::TypeWorldUi, 7);
    AfterFit(examination, Target(), &facing.x, &entityToken, false, en::BeginReturn);
    Check(At(kFit) && Reports("begins=2"), "a fit that failed is Prey's");
    AfterFit(examination, Target(), &facing.x, &entityToken, true, 0x15AB000);
    Check(At(kFit) && Reports("otherCaller=1"), "an unknown caller is Prey's");
    AfterFit(examination, reinterpret_cast<float*>(std::uintptr_t{0x10}), &facing.x, &entityToken, true,
             en::FrameReturn);
    Check(Reports("unread=1"), "an unreadable target is left");

    // 0, or VR stopped: Prey's own camera. The begin still sees the eye.
    SetExaminePullbackCm(0);
    Begin();
    Check(At(kFit) && Reports("begins=3"), "0: Prey's own camera");
    SetExaminePullbackCm(30);
    StopExamineCamera();
    Examine(en::TypeWorldUi, 7);
    Begin();
    Check(At(kFit) && Reports("active=0") && Reports("begins=4"), "VR stopped: Prey's own camera");
    EnsureExamineCamera();
    Examine(en::TypeWorldUi, 7);
    Frame();
    Check(At(Vec3{0.2f, 0.4f, 1.5f}) && Reports("active=1"), "VR again: moved again, from the eye seen while stopped");

    // The detour itself: Prey's result handed back whole; this test is no caller it knows.
    Examine(en::TypeWorldUi, 7);
    const std::uintptr_t result = FitViewDetour(examination, Target(), &facing.x, &entityToken);
    Check(fits == 1 && result == 0x1234500000000001ull && At(kFit) && Reports("otherCaller=2"),
          "the detour runs Prey's fit, returns its RAX, and moves nothing for a caller it does not know");

    std::cout << "examine_camera: a world UI's camera moved back along its view, never past the eye\n";
}
