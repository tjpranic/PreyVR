// Per-eye temporal AA (TemporalAaLane.h) with Prey replaced at its boundary: a
// fake PreyDll image (the post-processing counter, the AA mode, the history
// CTextures, the pipeline), a renderer holding a WARP device, the AA stage with
// its previous-camera ring, a render view with its previous camera, and the two
// history textures as real WARP textures, read back to see what each resolve
// blended. Each simulated submission does what the bytes say Prey's does
// (R-136): the resolve picks its history and ring slots from the counter, writes
// its output and stamp, and post-processing copies the view-projection to
// "previous", stores this run's view as its viewport's previous view (what
// moving objects' motion vectors are taken against) and advances the counter.
// The views are real ones, translated by a tag, because the second eye's world
// motion is worked out from them (render/taa/WorldMotion.h).
#include "render/taa/TemporalAaLane.cpp"
#include "DllTestSupport.h"
#include "TestSupport.h"
#include <d3d11.h>
#include <cmath>
#include <cstring>
#include <iostream>
#include <set>
#include <string>
#include <vector>

PREYVR_TEST_CAPTURE_LOG()

namespace lane_test {
using namespace preyvr::dll;
using preyvr::test::Check;
using preyvr::test::Logged;
namespace tn = preyvr::taa::native;
namespace taa = preyvr::taa;
using Microsoft::WRL::ComPtr;

// --- the fake PreyDll ---------------------------------------------------------------
constexpr std::size_t kImageSize = 0x2C00000;
std::uintptr_t image = 0;
DWORD readiness = ERROR_SUCCESS;
DWORD FakeResolve(std::uintptr_t& base)
{
    base = readiness == ERROR_SUCCESS ? image : 0;
    return readiness;
}

template <class T>
void Put(std::uintptr_t at, T value)
{
    std::memcpy(reinterpret_cast<void*>(at), &value, sizeof(T));
}
template <class T>
T Get(std::uintptr_t at)
{
    T value{};
    std::memcpy(&value, reinterpret_cast<const void*>(at), sizeof(T));
    return value;
}
std::uintptr_t Address(const void* p) { return reinterpret_cast<std::uintptr_t>(p); }

std::vector<std::uint8_t> renderer(0xB000), pipeline(0x400), stage(0x1000), view(0x1700);
std::uintptr_t Stage(std::size_t offset) { return Address(stage.data()) + offset; }
std::uintptr_t Renderer(std::size_t offset) { return Address(renderer.data()) + offset; }
std::uint32_t Counter() { return Get<std::uint32_t>(image + tn::FrameCounter); }
void Mode(int mode) { Put<int>(image + tn::ModeEffective, mode); }
// The viewport Prey draws the eyes in; any of the 8.
constexpr int kViewport = 2;
std::uintptr_t PrevView() { return Renderer(tn::RendererPrevViews + kViewport * tn::PrevViewSize); }
// Multiplies the rotation of the views Prey stores: 2 makes them not views.
float viewScale = 1.0f;
void StoreView(float tag)
{
    const float stored[16] = {viewScale, 0, 0, 0, 0, viewScale, 0, 0, 0, 0, viewScale, 0, tag, 0, 0, 1};
    std::memcpy(reinterpret_cast<void*>(PrevView()), stored, sizeof(stored));
}
float ViewTag() { return Get<float>(PrevView() + 12 * sizeof(float)); }

// CRenderView::SetPreviousFrameCamera: the camera copied to the view's +0x13E0.
unsigned previousCameraCalls = 0;
void FakeSetPreviousCamera(void* target, const void* camera)
{
    ++previousCameraCalls;
    std::memcpy(static_cast<std::uint8_t*>(target) + tn::ViewPreviousCamera, camera, tn::CameraSize);
}

// Cameras carry an int tag first; outputs are small integers, exact in half floats.
struct Camera {
    alignas(16) std::uint8_t bytes[tn::CameraSize]{};
    explicit Camera(int tag) { std::memcpy(bytes, &tag, sizeof(tag)); }
};
int CameraTag(int eye, int frame) { return 1000 + frame * 2 + eye; }
constexpr int kCentreTag = 7;
float Tag(int eye, int frame) { return static_cast<float>(1 + frame * 2 + eye); }

// --- the history textures, on WARP --------------------------------------------------
ComPtr<ID3D11Device> device;
ComPtr<ID3D11DeviceContext> context;
std::array<ComPtr<ID3D11Texture2D>, 2> history;
std::array<ComPtr<ID3D11RenderTargetView>, 2> target;
ComPtr<ID3D11Texture2D> staging;
// CTexture (+0x30 device texture) and device textures (+0x00 the D3D texture).
std::uint8_t ctexture[4][0x40]{};
std::uintptr_t deviceTexture[4][2]{};
// An "object" whose vtable is this executable's: the lane must never call it.
[[noreturn]] void Called()
{
    std::cerr << "FAIL: the lane called into an object that is not d3d11.dll's\n";
    std::exit(1);
}
void* const kTrapVtable[] = {reinterpret_cast<void*>(&Called), reinterpret_cast<void*>(&Called),
                             reinterpret_cast<void*>(&Called)};
const void* trapObject = kTrapVtable;

void Fill(unsigned slot, float value)
{
    const float colour[4] = {value, 0, 0, 1};
    context->ClearRenderTargetView(target[slot].Get(), colour);
}
float HalfToFloat(std::uint16_t h)
{
    const int exponent = (h >> 10) & 0x1F, mantissa = h & 0x3FF;
    const float magnitude = exponent == 0 ? std::ldexp(static_cast<float>(mantissa), -24)
                                          : std::ldexp(static_cast<float>(1024 + mantissa), exponent - 25);
    return (h & 0x8000) != 0 ? -magnitude : magnitude;
}
float Pixel(unsigned slot)
{
    context->CopyResource(staging.Get(), history[slot].Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    Check(SUCCEEDED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)), "the history reads back");
    std::uint16_t half = 0;
    std::memcpy(&half, mapped.pData, sizeof(half));
    context->Unmap(staging.Get(), 0);
    return HalfToFloat(half);
}

void MakeDevice()
{
    const D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
    Check(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, &level, 1, D3D11_SDK_VERSION,
                                      &device, nullptr, &context)),
          "a WARP device");
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = 64;
    desc.Height = 32;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    for (unsigned i = 0; i < 2; ++i) {
        Check(SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &history[i])), "a history texture");
        Check(SUCCEEDED(device->CreateRenderTargetView(history[i].Get(), nullptr, &target[i])), "its target");
        Fill(i, 0.0f);
    }
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    Check(SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &staging)), "a staging texture");
}

// --- the engine's submission and the both-eyes frame -----------------------------------

struct Seen {
    bool fresh = false;
    float history = -1;       // what the resolve blended: its history, or this frame
    double ring = -1;         // the previous camera it reprojected with
    int viewPrevious = -1;    // the view's previous camera, for motion vectors
    float prevViewProj = -1;  // the renderer's previous view-projection
    float prevView = -1;      // the viewport's previous view, for moving objects' motion vectors
    bool motion = false;      // offered a move from the compiled view to this eye's
    float motionTag = 0;      // that move, as a difference of view tags
    std::uint32_t counter = 0;
};

Seen Engine(int eye, int frame, bool temporal)
{
    Seen seen;
    const std::uint32_t c = Counter();
    seen.counter = c;
    seen.viewPrevious = Get<int>(Address(view.data()) + tn::ViewPreviousCamera);
    seen.prevViewProj = Get<float>(Renderer(tn::RendererPrevViewProj));
    seen.prevView = ViewTag();
    float motion[16]{};
    seen.motion = TaaLaneSecondEyeMotion(motion);
    seen.motionTag = motion[12];
    Put<float>(Renderer(tn::RendererViewProj), Tag(eye, frame));
    if (temporal) {
        const std::uintptr_t stamp = Stage(tn::StageLastResolve);
        seen.fresh = taa::HistoryFresh(c, Get<std::uint32_t>(stamp));
        seen.history = seen.fresh ? Pixel(taa::HistoryIn(c)) : Tag(eye, frame);
        seen.ring = Get<double>(Stage(0xBA8 + taa::RingIn(c) * 0x80));
        Put<double>(Stage(0xBA8 + taa::RingOut(c) * 0x80), Tag(eye, frame));
        Fill(taa::HistoryOut(c), Tag(eye, frame));
        Put<std::uint32_t>(stamp, c);
    }
    StoreView(Tag(eye, frame));
    std::memcpy(renderer.data() + tn::RendererPrevViewProj, renderer.data() + tn::RendererViewProj, tn::ViewProjSize);
    Put<std::uint32_t>(image + tn::FrameCounter, taa::Advance(c));
    return seen;
}

unsigned long long frameNumber = 0;
// One both-eyes frame as DualSceneStereo runs it. The game thread has put the
// view's own previous camera on it. `skip`: frames the renderer counted between.
std::array<Seen, 2> Frame(int frame, bool temporal, bool stopAfterFirst = false, unsigned long long skip = 0)
{
    const Camera centre(kCentreTag);
    std::memcpy(view.data() + tn::ViewPreviousCamera, centre.bytes, tn::CameraSize);
    frameNumber += 1 + skip;
    std::array<Seen, 2> seen{};
    TaaLaneFrameBegin(renderer.data(), view.data(), frameNumber);
    for (int eye = 0; eye < 2; ++eye) {
        const Camera camera(CameraTag(eye, frame));
        TaaLaneEyeBegin(renderer.data(), view.data(), eye, camera.bytes);
        seen[eye] = Engine(eye, frame, temporal);
        TaaLaneEyeEnd(renderer.data(), eye, camera.bytes);
        if (stopAfterFirst) {
            TaaLaneFrameEnd(view.data(), false);
            return seen;
        }
    }
    TaaLaneFrameEnd(view.data(), true);
    Check(Get<int>(Address(view.data()) + tn::ViewPreviousCamera) == kCentreTag,
          "the view's own previous camera is back after the frame");
    return seen;
}

bool Reports(const char* field)
{
    std::string report = " " + TaaLaneReport() + " ";
    for (char& c : report) {
        if (c == '{' || c == '}') { c = ' '; }
    }
    return report.find(std::string(" ") + field + " ") != std::string::npos;
}

// Each eye blends its own last output and reprojects with its own last cameras.
void CheckOwn(const std::array<Seen, 2>& seen, int frame, const char* what)
{
    for (int eye = 0; eye < 2; ++eye) {
        Check(seen[eye].fresh && seen[eye].history == Tag(eye, frame - 1), what);
        Check(seen[eye].ring == Tag(eye, frame - 1), "its own previous camera in the ring");
        Check(seen[eye].viewPrevious == CameraTag(eye, frame - 1), "its own previous camera on the view");
        Check(seen[eye].prevViewProj == Tag(eye, frame - 1), "its own previous view-projection");
        Check(seen[eye].prevView == Tag(eye, frame - 1), "its own previous view for moving objects");
    }
    Check(!seen[0].motion, "the first eye's objects compiled against its own view");
    // From last frame's first eye: the view the objects compiled against, not the
    // one its submission has since left.
    Check(seen[1].motion && seen[1].motionTag == Tag(1, frame - 1) - Tag(0, frame - 1),
          "the second eye gets the move from the compiled view to its own");
    Check(seen[1].counter == taa::Advance(seen[0].counter, taa::kEyeLead), "the second eye two counts ahead");
}
void CheckStart(const std::array<Seen, 2>& seen, int frame, const char* what)
{
    for (int eye = 0; eye < 2; ++eye) {
        Check(!seen[eye].fresh && seen[eye].history == Tag(eye, frame), what);
        Check(seen[eye].viewPrevious == CameraTag(eye, frame), "a start: the eye's own camera as its previous one");
        Check(!seen[eye].motion, "a start: no previous views, so no move");
    }
}

void TestLane()
{
    gResolve = FakeResolve;
    gSetPreviousCamera = FakeSetPreviousCamera;
    image = Address(VirtualAlloc(nullptr, kImageSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    Check(image != 0, "the fake PreyDll is mapped");
    MakeDevice();
    Put<ID3D11Device*>(Renderer(preyvr::engine::RendererLayout::device), device.Get());
    Put<std::uint32_t>(image + tn::FrameCounter, 100);
    Put<int>(Renderer(tn::RendererViewport), kViewport);
    Put<int>(Renderer(tn::RendererGraphicsPipeline), tn::CompiledOncePipeline);
    Put<std::uintptr_t>(image + tn::Pipeline, Address(pipeline.data()));
    Put<std::uintptr_t>(Address(pipeline.data()) + tn::PipelineAaStage, Address(stage.data()));
    for (unsigned i = 0; i < 2; ++i) { deviceTexture[i][0] = Address(history[i].Get()); }
    deviceTexture[2][0] = deviceTexture[1][0];            // another CTexture over the same texture
    deviceTexture[3][0] = Address(&trapObject);           // a device texture that is not d3d11.dll's
    for (unsigned i = 0; i < 4; ++i) {
        Put<std::uintptr_t>(Address(ctexture[i]) + tn::TextureDevice, Address(deviceTexture[i]));
    }
    Put<std::uintptr_t>(image + tn::History, Address(ctexture[0]));
    Put<std::uintptr_t>(image + tn::History + 8, Address(ctexture[1]));
    Mode(4);
    int frame = 0;

    // Off: Prey's own timeline, each eye blending the other's.
    Check(TaaLaneEnabled() && SetTaaLane(2) == ERROR_INVALID_PARAMETER, "on by default, as the menu is; 0 or 1 only");
    Check(SetTaaLane(0) == ERROR_SUCCESS && !TaaLaneEnabled(), "off");
    Frame(frame++, true);
    auto seen = Frame(frame++, true);
    Check(seen[0].history == Tag(1, frame - 2) && seen[1].history == Tag(0, frame - 1),
          "off: the first eye blends the second's last frame, the second the first's");
    Check(seen[1].counter == seen[0].counter + 1 && previousCameraCalls == 0 && Reports("active=0"),
          "off: the counter moves once per eye and nothing is touched");
    Check(seen[0].prevView == Tag(1, frame - 2) && seen[1].prevView == Tag(0, frame - 1),
          "off: each eye's moving objects take the other eye's view as their previous one");
    Check(!seen[1].motion && Reports("given=0"), "off: nothing to correct against");

    // Other bytes: refused, nothing touched.
    readiness = ERROR_BAD_EXE_FORMAT;
    Check(SetTaaLane(1) == ERROR_SUCCESS && TaaLaneEnabled(), "on");
    seen = Frame(frame++, true);
    Check(Reports("gate=refused") && Reports("active=0") && previousCameraCalls == 0 &&
              seen[1].counter == seen[0].counter + 1,
          "other bytes: the lane stays out");
    gGate.store(0);
    readiness = ERROR_SUCCESS;

    // SMAA 1X: no resolve, so nothing to separate.
    Mode(1);
    seen = Frame(frame++, false);
    Check(Reports("idleMode=1") && Reports("active=0") && previousCameraCalls == 0 &&
              seen[1].counter == seen[0].counter + 1,
          "SMAA 1X: idle");

    // TSAA: a start without history, then each eye its own.
    Mode(4);
    seen = Frame(frame++, true);
    CheckStart(seen, frame - 1, "the first TSAA frame starts both eyes afresh");
    Check(Logged("preyvr_taa_lane result=0 detail=gate_ok rows=15") && Logged("start reason=enable"),
          "the gate opens on the fifteen bodies; the start is the enable");
    std::array<std::set<unsigned>, 2> jitter;
    for (int n = 0; n < 6; ++n) {
        seen = Frame(frame++, true);
        CheckOwn(seen, frame - 1, "each eye blends its own last output");
        if (n < 4) {
            for (int eye = 0; eye < 2; ++eye) { jitter[eye].insert(seen[eye].counter & 3u); }
        }
    }
    Check(jitter[0].size() == 4 && jitter[1].size() == 4, "each eye walks the whole 4-sample jitter pattern");
    Check(Reports("active=1") && Reports("gate=ok") && Reports("frames=7") && Reports("copiesIn=12") &&
              Reports("copiesOut=14") && Reports("viewsIn=12") && Reports("freshStarts=2") && Reports("resolveMissed=0") &&
              Reports("counterOff=0") && Reports("history=64x32") && Reports("format=10"),
          "the report: frames, copies and views in, the history's size and format");
    Check(Reports("on=1") && Reports("pipeline=2") && Reports("given=6") && Reports("separationUm=1000000"),
          "the report: the second eye's move, the latched pipeline, the views' separation");

    // The second eye's world motion: off for an A/B, and left alone without its inputs.
    Check(SetTaaSecondEyeMotion(2) == ERROR_INVALID_PARAMETER, "0 or 1 only");
    Check(SetTaaSecondEyeMotion(0) == ERROR_SUCCESS && Reports("on=0"), "motion off");
    seen = Frame(frame++, true);
    Check(!seen[1].motion && seen[1].prevView == Tag(1, frame - 2), "off: as the objects compiled, the lane otherwise as it was");
    Check(SetTaaSecondEyeMotion(1) == ERROR_SUCCESS, "motion on");
    seen = Frame(frame++, true);
    CheckOwn(seen, frame - 1, "on again: at once");
    // Below 2, each pass compiles per draw, so each eye's objects have its own view.
    Put<int>(Renderer(tn::RendererGraphicsPipeline), 1);
    seen = Frame(frame++, true);
    Check(!seen[1].motion && Reports("perDraw=1") && Reports("pipeline=1"), "objects compiled per draw: nothing to correct");
    Put<int>(Renderer(tn::RendererGraphicsPipeline), tn::CompiledOncePipeline);
    seen = Frame(frame++, true);
    CheckOwn(seen, frame - 1, "compiled once again: corrected");
    // A previous view that isn't one, then an unreadable viewport.
    viewScale = 2.0f;
    Frame(frame++, true);
    viewScale = 1.0f;
    seen = Frame(frame++, true);
    Check(!seen[1].motion && Reports("badViews=1"), "a previous view that is not a view: left alone");
    seen = Frame(frame++, true);
    CheckOwn(seen, frame - 1, "good views again: corrected");
    Put<int>(Renderer(tn::RendererViewport), static_cast<int>(tn::PrevViewSlots));
    seen = Frame(frame++, true);
    Check(!seen[1].motion, "a viewport outside the slots: left alone");
    Put<int>(Renderer(tn::RendererViewport), kViewport);
    seen = Frame(frame++, true);
    CheckOwn(seen, frame - 1, "its viewport back: corrected");
    TaaLaneMotionPacked(true);
    TaaLaneMotionPacked(false);
    Check(Reports("packed=1") && Reports("unshaped=1"), "the packs' reports");

    // A frame the renderer counted between: both eyes start afresh.
    seen = Frame(frame++, true, false, 1);
    CheckStart(seen, frame - 1, "after a gap, a start");
    Check(Logged("start reason=gap"), "logged as a gap");
    seen = Frame(frame++, true);
    CheckOwn(seen, frame - 1, "and then their own again");

    // A frame that stopped after its first eye.
    Frame(frame++, true, true);
    seen = Frame(frame++, true);
    CheckStart(seen, frame - 1, "after an incomplete frame, a start");
    Check(Logged("start reason=incomplete"), "logged as incomplete");
    seen = Frame(frame++, true);
    CheckOwn(seen, frame - 1, "and then their own again");

    // New history CTextures: adopted, and a start.
    Put<std::uintptr_t>(image + tn::History + 8, Address(ctexture[2]));
    seen = Frame(frame++, true);
    CheckStart(seen, frame - 1, "new history textures: a start");
    Check(Logged("start reason=textures"), "logged as new textures");
    seen = Frame(frame++, true);
    CheckOwn(seen, frame - 1, "and then their own again");

    // A history texture that is not a D3D11 one: refused, nothing called on it, nothing touched.
    Put<std::uintptr_t>(image + tn::History + 8, Address(ctexture[3]));
    const unsigned callsBefore = previousCameraCalls;
    seen = Frame(frame++, true);
    Check(Reports("active=0") && previousCameraCalls == callsBefore && seen[1].counter == seen[0].counter + 1 &&
              Reports("refused=gate:1,stage:0,textures:1,device:0"),
          "a history texture that is not D3D11's: refused, the frame left as Prey's");
    Put<std::uintptr_t>(image + tn::History + 8, Address(ctexture[1]));
    seen = Frame(frame++, true);
    CheckStart(seen, frame - 1, "usable again: a start");
    seen = Frame(frame++, true);
    CheckOwn(seen, frame - 1, "and then their own again");

    // Off: its textures released, Prey's timeline again.
    Check(SetTaaLane(0) == ERROR_SUCCESS && !TaaLaneEnabled(), "off");
    const unsigned callsOff = previousCameraCalls;
    seen = Frame(frame++, true);
    Check(Reports("active=0") && Reports("history=none") && previousCameraCalls == callsOff &&
              seen[1].counter == seen[0].counter + 1,
          "off: released, nothing touched");
    Check(SetTaaLane(1) == ERROR_SUCCESS, "on again");
    seen = Frame(frame++, true);
    CheckStart(seen, frame - 1, "on again: a start");
    Check(Logged("start reason=enable", "starts=6"), "logged as an enable");
    seen = Frame(frame++, true);
    CheckOwn(seen, frame - 1, "and then their own again");
}

} // namespace lane_test

int main()
{
    lane_test::TestLane();
    std::cout << "temporal_aa_lane: each eye its own history, previous camera, view-projection, previous view and "
                 "jitter\n";
}
