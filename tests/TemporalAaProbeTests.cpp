// The temporal-AA probe (TemporalAaProbe.h) with Prey replaced at its boundary:
// a fake PreyDll image holding the post-processing counter, the AA mode, the
// history CTextures and the pipeline; a fake renderer, AA stage and view; and a
// real WARP texture behind one history slot. Each simulated submission does what
// the bytes say Prey's does: build a view-projection, resolve (in a temporal
// mode) with the counter, copy the view-projection to "previous" and advance the
// counter at the end of post-processing.
#include "../src/dll/TemporalAaProbe.cpp"
#include "DllTestSupport.h"
#include "TestSupport.h"
#include <d3d11.h>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

PREYVR_TEST_CAPTURE_LOG()

namespace taa_test {
using namespace preyvr::dll;
using preyvr::test::Check;
using preyvr::test::Count;
using preyvr::test::Logged;
namespace tn = preyvr::taa::native;

// --- the fake PreyDll ---------------------------------------------------------------
constexpr std::size_t kImageSize = 0x2C00000;
std::uintptr_t image = 0;
DWORD readiness = ERROR_NOT_READY;
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

std::vector<std::uint8_t> renderer(0x1000), pipeline(0x400), stage(0x100);
int viewObject = 0;
const void* const view = &viewObject;

// A "vtable" in this executable that must never be called: the probe asks only
// objects whose vtable is d3d11.dll's.
[[noreturn]] void Called()
{
    std::cerr << "FAIL: the probe called into an object that is not d3d11.dll's\n";
    std::exit(1);
}
void* const kTrapVtable[] = {reinterpret_cast<void*>(&Called), reinterpret_cast<void*>(&Called),
                             reinterpret_cast<void*>(&Called)};
const void* trapObject = kTrapVtable;   // an "object" whose first pointer is the trap table

// CTexture (+0x30 device texture) and device textures, as the engine lays them out.
std::uint8_t ctexture[3][0x40]{};
std::uintptr_t deviceTexture[3][3]{};

// "first={n=3 delta=...}" -> "n=3 delta=..."; whole report for an empty name.
std::string Section(const char* name)
{
    const std::string report = TaaProbeReport();
    const std::string open = std::string(" ") + name + "={";
    const std::size_t at = report.find(open);
    if (at == std::string::npos) { return {}; }
    const std::size_t from = at + open.size();
    return " " + report.substr(from, report.find('}', from) - from) + " ";
}
bool Has(const char* section, const char* field)
{
    return Section(section).find(std::string(" ") + field + " ") != std::string::npos;
}
bool Reports(const char* field)
{
    std::string report = " " + TaaProbeReport() + " ";
    for (char& c : report) {
        if (c == '{' || c == '}') { c = ' '; }
    }
    return report.find(std::string(" ") + field + " ") != std::string::npos;
}

std::uint32_t Counter() { return Get<std::uint32_t>(image + tn::FrameCounter); }

// One submission. `post`: post-processing runs (copy, then +1); `resolve`: a
// temporal mode, so the AA stage stamps the counter it ran with.
void Submission(TaaSubmission kind, bool post, bool resolve, float jitterX = 0, float jitterY = 0,
                bool viewOnPipeline = true)
{
    TaaProbeBefore(renderer.data(), view, kind);
    const std::uint32_t c = Counter();
    auto* r = renderer.data();
    Put<float>(Address(r + tn::RendererViewProj), static_cast<float>(c) + 0.5f);   // this run's camera
    if (resolve) { Put<std::uint32_t>(Address(stage.data()) + tn::StageLastResolve, c); }
    if (post) {
        std::memcpy(r + tn::RendererPrevViewProj, r + tn::RendererViewProj, tn::ViewProjSize);
        Put<std::uint32_t>(image + tn::FrameCounter, (c + 1) & 0xFFFFFF);
    }
    Put<std::uintptr_t>(Address(pipeline.data()) + tn::PipelineView, viewOnPipeline ? Address(view) : 0);
    Put<float>(Address(r + tn::RendererJitter), jitterX);
    Put<float>(Address(r + tn::RendererJitter + 4), jitterY);
    TaaProbeAfter(renderer.data(), view, kind);
}
void BothEyes(bool resolve = false, float jitter = 0)
{
    Submission(TaaSubmission::first, true, resolve, jitter, -jitter);
    Submission(TaaSubmission::second, true, resolve, jitter, -jitter);
}

ID3D11Texture2D* WarpTexture(ID3D11Device** device)
{
    const D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
    Check(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, &level, 1, D3D11_SDK_VERSION,
                                      device, nullptr, nullptr)),
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
    ID3D11Texture2D* texture = nullptr;
    Check(SUCCEEDED((*device)->CreateTexture2D(&desc, nullptr, &texture)) && texture != nullptr, "a WARP texture");
    return texture;
}

void TestProbe()
{
    gResolve = FakeResolve;
    image = Address(VirtualAlloc(nullptr, kImageSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    Check(image != 0, "the fake PreyDll is mapped");

    // Before PreyDll: nothing read, nothing counted, tried again later.
    Submission(TaaSubmission::single, true, false);
    Check(Reports("gate=untried") && Reports("frames=0") && Has("single", "n=0"), "no PreyDll: idle, untried");

    // Other bytes: refused once, then idle.
    readiness = ERROR_BAD_EXE_FORMAT;
    Submission(TaaSubmission::single, true, false);
    Submission(TaaSubmission::single, true, false);
    Check(Reports("gate=refused") && Has("single", "n=0") && Count("detail=bytes_mismatch") == 1,
          "other bytes: refused, logged once, nothing read");

    // The engine as the bytes describe it.
    gGate.store(0);
    readiness = ERROR_SUCCESS;
    Put<std::uint32_t>(image + tn::FrameCounter, 100);
    Put<int>(image + tn::ModeCvar, 1);
    Put<int>(image + tn::ModeEffective, 1);
    Put<std::uintptr_t>(image + tn::Pipeline, Address(pipeline.data()));
    Put<std::uintptr_t>(Address(pipeline.data()) + tn::PipelineAaStage, Address(stage.data()));

    ID3D11Device* device = nullptr;
    ID3D11Texture2D* texture = WarpTexture(&device);
    deviceTexture[0][0] = Address(texture);                 // the D3D texture first, as CDeviceTexture holds it
    deviceTexture[1][0] = deviceTexture[1][1] = deviceTexture[1][2] = Address(&trapObject);
    // ctexture[2] keeps a null device texture.
    for (int i = 0; i < 2; ++i) { Put<std::uintptr_t>(Address(ctexture[i]) + tn::TextureDevice, Address(deviceTexture[i])); }
    Put<std::uintptr_t>(image + tn::History, Address(ctexture[0]));
    Put<std::uintptr_t>(image + tn::History + 8, Address(ctexture[1]));

    // Three both-eyes frames, post-processing per eye, SMAA 1X (no resolve, no jitter).
    BothEyes();
    BothEyes();
    BothEyes();
    Check(Logged("preyvr_taa result=0 detail=gate_ok rows=15"), "the gate opens on the fifteen bodies");
    Check(Reports("gate=ok") && Reports("mode=1") && Reports("effective=1") && Reports("counter=106"),
          "mode, effective mode and the counter after three frames");
    Check(Reports("frames=3") && Reports("frameDelta=1:0,2:2,other:0"),
          "frame to frame the counter moves by 2: post-processing once per eye");
    Check(Has("first", "n=3") && Has("first", "delta=0:0,1:3,other:0") && Has("second", "delta=0:0,1:3,other:0"),
          "each submission moves it by 1");
    Check(Has("first", "prevViewProjChanged=3") && Has("second", "prevViewProjChanged=3"),
          "each eye's post-processing replaces the previous view-projection");
    Check(Has("first", "stampIsCounter=0") && Has("first", "jittered=0") && Has("first", "view=match:3,null:0,other:0"),
          "no resolve and no jitter in SMAA 1X; the pipeline draws this view");

    // The history textures, described once after a frame's last submission.
    Check(Count("preyvr_taa history inspection=") == 1, "described once");
    Check(Reports("hop=+0x00") && Reports("size=64x32") && Reports("format=10") && Reports("mips=1") &&
              Reports("array=1") && Reports("samples=1") && Reports("bind=0x28"),
          "tex0: the D3D texture at the device texture's +0x00, its description");
    Check(Section("tex0").find("+0x00=d3d11.dll") != std::string::npos, "tex0: the hop's vtable is d3d11.dll's");
    Check(Section("tex1").find("hop=none") != std::string::npos &&
              Section("tex1").find("+0x00=d3d11.dll") == std::string::npos,
          "tex1: no d3d11.dll object, and nothing called on another module's");
    Check(Reports("historyChanges=0"), "the history pointers held still");

    // A temporal mode: the stage stamps the counter each submission began with; jitter.
    BothEyes(true, 0.25f);
    BothEyes(true, 0.25f);
    Check(Has("first", "stampIsCounter=2") && Has("second", "stampIsCounter=2") && Reports("lastResolve=109"),
          "the AA stage's last resolve ran with each submission's counter");
    Check(Has("first", "jittered=2") && Has("first", "jitter=0.25,-0.25"), "the jitter is read");

    // A frame with the view gone from the pipeline, and a submission without post-processing.
    Submission(TaaSubmission::single, false, false, 0, 0, false);
    Check(Has("single", "delta=0:1,1:0,other:0") &&
              Has("single", "view=match:0,null:1,other:0") && Has("single", "prevViewProjChanged=0"),
          "no post-processing: the counter and the previous view-projection stay");

    // The 24-bit wrap is a move of 1.
    Put<std::uint32_t>(image + tn::FrameCounter, 0xFFFFFF);
    Submission(TaaSubmission::single, true, false);
    Check(Has("single", "delta=0:1,1:1,other:0") && Reports("counter=0"), "0xFFFFFF to 0 moves by 1");

    // New history pointers: counted, described again; a null slot and a null device texture.
    Put<std::uintptr_t>(image + tn::History + 8, Address(ctexture[2]));
    BothEyes();
    Check(Reports("historyChanges=1") && Count("preyvr_taa history inspection=") == 2 &&
              Section("tex1").find("device=null") != std::string::npos,
          "a new pointer: counted, described again; a null device texture says so");
    Put<std::uintptr_t>(image + tn::History, 0);
    BothEyes();
    Check(Section("tex0") == " null ", "a null slot says so");

    // Pointers that churn are described at most eight times.
    for (int i = 0; i < 20; ++i) {
        Put<std::uintptr_t>(image + tn::History + 8, Address(ctexture[i % 2 == 0 ? 1 : 2]));
        BothEyes();
    }
    Check(Count("preyvr_taa history inspection=") == 8, "at most eight descriptions");

    texture->Release();
    device->Release();
}

} // namespace taa_test

int main()
{
    taa_test::TestProbe();
    std::cout << "temporal_aa_probe: the post-processing counter, the AA stage and the history textures, read only\n";
}
