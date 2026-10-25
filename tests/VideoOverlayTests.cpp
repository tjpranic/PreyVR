// The fullscreen-video lane: the production detour with Prey replaced at its ABI
// boundary -- the video draw's original, GetViewport, the movie and its Bink
// handle, the gate and the installer -- and the held-eye copy and overlay
// window stubbed where XrSessionHost and DualOverlay provide them.
#include "../src/dll/VideoOverlay.cpp"
#include "DllTestSupport.h"
#include "TestSupport.h"
#include <cstring>
#include <iostream>
#include <string>

PREYVR_TEST_CAPTURE_LOG()
PREYVR_TEST_STUB_MINHOOK()

namespace {
using namespace preyvr::dll;
using preyvr::test::Check;
using preyvr::test::Count;
using preyvr::test::Logged;

// " video={hook=1 ... lastRefusal=none}" with each field between spaces.
std::string Report()
{
    std::string report = VideoOverlayReport() + " ";
    for (char& c : report) {
        if (c == '{' || c == '}') { c = ' '; }
    }
    return report;
}
bool Reports(const char* field) { return Report().find(std::string(" ") + field + " ") != std::string::npos; }

// dual.overlay and the overlay window, as DualOverlay and XrSessionHost answer.
bool overlayOn = true, windowOpen = true;
// What the copy answers, and what happened, in order: v (viewport asked), d
// (the draw), c (the copy).
bool copyAnswer = true;
const char* copyRefusal = nullptr;
std::string order;
} // namespace

bool preyvr::dll::DualOverlayRedrawEnabled() { return overlayOn; }
bool preyvr::dll::DualOverlayWindowOpen() { return windowOpen; }
bool preyvr::dll::CopyBackBufferIntoHeldEye(const char** refusal)
{
    order += 'c';
    if (!copyAnswer && refusal != nullptr) { *refusal = copyRefusal; }
    return copyAnswer;
}

namespace {
// A playing movie: its Bink handle at +0x08, which starts Width, Height.
struct Bink { std::uint32_t width, height, frames, frame; };
Bink bink{1920, 1080, 90, 0};
struct Movie { std::uint8_t bytes[0x40]; };
Movie movie{};
void SetBink(const void* handle) { std::memcpy(movie.bytes + vn::MovieBink, &handle, sizeof(handle)); }

int rendererObject = 0;   // the static renderer GetViewport is asked on
int viewport[4] = {0, 0, 2644, 2644};
const void* askedRenderer = nullptr;
void __fastcall FakeGetViewport(void* renderer, int* x, int* y, int* width, int* height)
{
    order += 'v';
    askedRenderer = renderer;
    *x = viewport[0];
    *y = viewport[1];
    *width = viewport[2];
    *height = viewport[3];
}

struct Drawn {
    void* backend = nullptr;
    void* movie = nullptr;
    unsigned handedOn = 0;
    void* passThrough = nullptr;
};
Drawn drawn{};
unsigned draws = 0;
void __fastcall FakeDraw(void* backend, void* movieDrawn, unsigned int handedOn, void* passThrough)
{
    order += 'd';
    ++draws;
    drawn = {backend, movieDrawn, handedOn, passThrough};
}
void DrawTarget() {}   // the address the fake gate resolves; never called

unsigned resolves = 0, installs = 0;
DWORD readiness = ERROR_SUCCESS;
DWORD FakeResolve(Engine& engine)
{
    ++resolves;
    engine = {};
    if (readiness == ERROR_SUCCESS) {
        engine.draw = reinterpret_cast<std::uintptr_t>(&DrawTarget);
        engine.getViewport = FakeGetViewport;
        engine.renderer = &rendererObject;
    }
    return readiness;
}
bool FakeInstall(std::uintptr_t target)
{
    ++installs;
    Check(target == reinterpret_cast<std::uintptr_t>(&DrawTarget), "the hook goes on the resolved draw");
    Check(gGetViewport.load() == FakeGetViewport && gRenderer.load() == &rendererObject,
          "GetViewport and the renderer are published before the hook is live");
    gOriginal = FakeDraw;
    return true;
}
void Retry()
{
    gInstallTried = false;
    gGate = "unchecked";
}

// One draw through the detour, with what it hands Prey's draw checked.
void Draw()
{
    order.clear();
    int backend = 0;
    int passThrough = 0;
    const unsigned before = draws;
    FullscreenDrawDetour(&backend, &movie, 0x2Eu, &passThrough);
    Check(draws == before + 1 && drawn.backend == &backend && drawn.movie == &movie && drawn.handedOn == 0x2Eu &&
              drawn.passThrough == &passThrough,
          "Prey's draw runs once, with its own four registers");
}
} // namespace

int main()
{
    SetBink(&bink);
    Check(Reports("hook=0") && Reports("gate=unchecked") && Reports("draws=0"), "nothing hooked yet");

    // Not ready (no PreyDll yet): not tried, so tried again.
    gResolve = FakeResolve;
    gInstall = FakeInstall;
    readiness = ERROR_NOT_READY;
    Check(EnsureVideoOverlay() == ERROR_NOT_READY && installs == 0 && Reports("gate=unchecked") &&
              Logged("preyvr_dual_overlay", "video_no_preydll"),
          "no PreyDll: not yet");
    EnsureVideoOverlay();
    Check(resolves == 2, "asked again once PreyDll may be there");

    // Other bytes: refused for good.
    readiness = ERROR_BAD_EXE_FORMAT;
    Check(EnsureVideoOverlay() == ERROR_NOT_SUPPORTED && installs == 0 && Reports("gate=refused") &&
              Logged("result=unavailable", "video_bytes_mismatch", "FullscreenDraw"),
          "mismatched bytes refuse the hook");
    Check(EnsureVideoOverlay() == ERROR_NOT_SUPPORTED && resolves == 3, "and a refusal is not retried");

    // The real installer with MinHook never initialised: the hook cannot be made.
    Retry();
    readiness = ERROR_SUCCESS;
    gInstall = Install;
    Check(EnsureVideoOverlay() == ERROR_NOT_SUPPORTED && Reports("gate=hook_failed") && Reports("hook=0") &&
              Logged("result=failed", "video_create_hook", "FullscreenDraw"),
          "a hook that cannot be created is reported");

    Retry();
    gInstall = FakeInstall;
    Check(EnsureVideoOverlay() == 0 && installs == 1 && Reports("hook=1") && Reports("gate=ok") &&
              Logged("video_hook_installed", "FullscreenDraw", "rows=2"),
          "the draw is hooked");
    Check(EnsureVideoOverlay() == 0 && installs == 1, "once");

    // dual.overlay off, or no held first eye: Prey's draw alone.
    overlayOn = false;
    Draw();
    Check(order == "d" && Reports("draws=0"), "off: the draw only, not counted");
    overlayOn = true;
    windowOpen = false;
    Draw();
    Check(order == "d" && Reports("draws=0"), "window closed: the draw only, not counted");
    windowOpen = true;

    // A both-eyes frame in a headset: the viewport is asked first, as the draw
    // asks it, then the draw, then the copy.
    Draw();
    Check(order == "vdc" && askedRenderer == &rendererObject, "viewport on the static renderer, draw, then copy");
    Check(Reports("draws=1") && Reports("copied=1") && Reports("refused=0") && Reports("lastRefusal=none"),
          "a letterboxed video is copied into the held eye");
    Check(Logged("video size=1920x1080", "viewport=2644x2644", "copied=1 refusal=none"), "and the first is logged");

    // The draw does not clear: the world around the video is the second eye's.
    viewport[2] = 1920;
    viewport[3] = 1080;
    Draw();
    Check(order == "vd" && Reports("copied=1") && Reports("refused=1") && Reports("lastRefusal=not_letterboxed"),
          "the video's own size is not copied");
    viewport[2] = 3840;
    viewport[3] = 2160;
    Draw();
    Check(order == "vd" && Reports("refused=2"), "nor another size of its aspect");
    viewport[2] = viewport[3] = 2644;

    // The copy refuses (the target was not the frame's backbuffer, say).
    copyAnswer = false;
    copyRefusal = "not_backbuffer";
    Draw();
    Check(order == "vdc" && Reports("copied=1") && Reports("refused=3") && Reports("lastRefusal=not_backbuffer"),
          "a refused copy is counted with its reason");
    copyAnswer = true;

    // No Bink handle, or one that cannot be read: Prey's draw, nothing asked.
    SetBink(nullptr);
    Draw();
    Check(order == "d" && Reports("refused=4") && Reports("lastRefusal=unread"), "no handle: the draw only");
    SetBink(reinterpret_cast<const void*>(std::uintptr_t{0x10}));
    Draw();
    Check(order == "d" && Reports("refused=5"), "an unreadable handle: the draw only");
    SetBink(&bink);
    Draw();
    Check(order == "vdc" && Reports("copied=2") && Reports("draws=7"), "and copied again with a handle");

    Check(Count("preyvr_dual_overlay video size=") == 4, "only the first four draws are logged");

    std::cout << "video_overlay: a letterboxed fullscreen video copied into the held eye after Prey's draw\n";
}
