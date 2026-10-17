// Exercise the production focus hold, its gate, import slot and cvar handshake
// without loading Prey. Including this TU exposes its internals to this test only.
#include "../src/dll/FocusHold.cpp"
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>
#include "TestSupport.h"

namespace preyvr::lifecycle {
std::vector<std::string> gLines;
void Log(std::string_view message) { gLines.emplace_back(message); }
} // namespace preyvr::lifecycle
namespace preyvr::dll {
DWORD gPinned = 1;
DWORD ModulePinStatus() { return gPinned; }
bool gMinHook = false;
bool EnsureMinHook() { return gMinHook; }
DWORD gObserver = static_cast<DWORD>(FrameObserverRuntimeStatus::enabled);
DWORD FrameObserverStatus() { return gObserver; }
std::vector<std::string> gQueued;
DWORD gQueueResult = 0;
unsigned long long gVerifiedCount = 0;
DWORD gVerifiedResult = 0;
DWORD QueueVerifiedRendererSetting(const char* command)
{
    if (gQueueResult == 0) { gQueued.emplace_back(command); }
    return gQueueResult;
}
unsigned long long VerifiedRendererSettingCount() { return gVerifiedCount; }
DWORD VerifiedRendererSettingResult() { return gVerifiedResult; }
} // namespace preyvr::dll

namespace {
using namespace preyvr::dll;
using preyvr::lifecycle::gLines;
using Layout = preyvr::engine::GlobalEnvironmentLayout;

using preyvr::test::Require;
bool Logged(const char* text)
{
    for (const auto& line : gLines) {
        if (line.find(text) != std::string::npos) return true;
    }
    return false;
}
std::size_t Count(const char* text)
{
    std::size_t n = 0;
    for (const auto& line : gLines) {
        if (line.find(text) != std::string::npos) ++n;
    }
    return n;
}
bool Reports(const char* text) { return FocusHoldReport().find(text) != std::string::npos; }

std::uintptr_t RealGetActiveWindow()
{
    return reinterpret_cast<std::uintptr_t>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetActiveWindow"));
}

// A PreyDll-sized reservation with only the touched pages committed: the gate's
// page, the import slot's page (read-only, as .rdata is) and gEnv's.
struct Image {
    static constexpr std::size_t kSize = 0x2300000;
    std::uintptr_t base = 0;
    std::vector<std::uint8_t> renderer = std::vector<std::uint8_t>(kRendererWindow + sizeof(HWND), 0);

    Image()
    {
        base = reinterpret_cast<std::uintptr_t>(VirtualAlloc(nullptr, kSize, MEM_RESERVE, PAGE_NOACCESS));
        Require(base != 0, "the fake image is reserved");
        for (const std::uintptr_t rva : {kThrottleRva, kActiveWindowSlotRva, Layout::baseRva, kAudioManagerRva,
                                         kAudioDispatchRva}) {
            Require(VirtualAlloc(reinterpret_cast<void*>((base + rva) & ~std::uintptr_t{0xFFF}), 0x1000, MEM_COMMIT,
                                 PAGE_READWRITE) != nullptr, "a fake image page is committed");
        }
        std::memcpy(reinterpret_cast<void*>(base + kThrottleRva), kThrottleCode, sizeof(kThrottleCode));
        std::memcpy(reinterpret_cast<void*>(base + kThrottleSleepRva), kThrottleSleepCode, sizeof(kThrottleSleepCode));
        const auto put = [this](std::uintptr_t rva, const std::uint8_t* bytes, std::size_t size) {
            std::memcpy(reinterpret_cast<void*>(base + rva), bytes, size);
        };
        put(kAudioDispatchRva, kAudioDispatchCode, sizeof(kAudioDispatchCode));
        put(kAudioManagerCallRva, kAudioManagerCallCode, sizeof(kAudioManagerCallCode));
        put(kAudioManagerRva, kAudioManagerCode, sizeof(kAudioManagerCode));
        put(kAudioLoseFocusRva, kAudioLoseFocusCode, sizeof(kAudioLoseFocusCode));
        put(kAudioGetFocusRva, kAudioGetFocusCode, sizeof(kAudioGetFocusCode));
        SetLoseCase(static_cast<std::uint32_t>(kAudioLoseFocusRva));
        const auto rendererAddress = reinterpret_cast<std::uintptr_t>(renderer.data());
        std::memcpy(reinterpret_cast<void*>(base + Layout::baseRva + Layout::renderer), &rendererAddress, sizeof(rendererAddress));
        SetSlot(RealGetActiveWindow());
    }
    ~Image() { VirtualFree(reinterpret_cast<void*>(base), 0, MEM_RELEASE); }
    Image(const Image&) = delete;
    Image& operator=(const Image&) = delete;

    void* SlotAddress() const { return reinterpret_cast<void*>(base + kActiveWindowSlotRva); }
    std::uintptr_t Slot() const { return *reinterpret_cast<const std::uintptr_t*>(SlotAddress()); }
    void SetSlot(std::uintptr_t value)
    {
        DWORD old = 0;
        Require(VirtualProtect(SlotAddress(), sizeof(value), PAGE_READWRITE, &old) != FALSE, "slot writable");
        std::memcpy(SlotAddress(), &value, sizeof(value));
        Require(VirtualProtect(SlotAddress(), sizeof(value), PAGE_READONLY, &old) != FALSE, "slot read-only");
    }
    DWORD SlotProtection() const
    {
        MEMORY_BASIC_INFORMATION info{};
        VirtualQuery(SlotAddress(), &info, sizeof(info));
        return info.Protect;
    }
    void SetWindow(HWND window) { std::memcpy(renderer.data() + kRendererWindow, &window, sizeof(window)); }
    void SetLoseCase(std::uint32_t rva)
    {
        std::memcpy(reinterpret_cast<void*>(base + kAudioManagerTableRva + kLoseFocus * 4), &rva, sizeof(rva));
    }
    HWND CallSlot() const { return reinterpret_cast<HWND(WINAPI*)()>(Slot())(); }
};

std::uintptr_t gFakeBase = 0;
std::uintptr_t FakeBase() { return gFakeBase; }

HWND MakeWindow(const char* className, DWORD style = WS_OVERLAPPEDWINDOW)
{
    const HWND window = CreateWindowExA(0, className, "focus hold test", style, 0, 0, 64, 64, nullptr, nullptr,
                                        GetModuleHandleW(nullptr), nullptr);
    Require(window != nullptr, "test window created");
    return window;
}

void RegisterClasses()
{
    for (const char* name : {"CryENGINE", "NotCryENGINE"}) {
        WNDCLASSA type{};
        type.lpfnWndProc = DefWindowProcA;
        type.hInstance = GetModuleHandleW(nullptr);
        type.lpszClassName = name;
        Require(RegisterClassA(&type) != 0, "test window class registered");
    }
}

// Everything but the once-per-process install.
void Reset()
{
    gWanted = false;
    gHeld.store(false);
    gWindow.store(nullptr);
    gRefusal = kOk;
    gPendingLogged = false;
    gPauseWanted = gPauseApplied = gPauseSending = gPauseFailed = -1;
    gStateKnown = false;
    gQueued.clear();
    gLines.clear();
    gQueueResult = 0;
    gVerifiedResult = 0;
    gObserver = static_cast<DWORD>(FrameObserverRuntimeStatus::enabled);
    gPinned = 1;
}

void TestBadValue()
{
    Require(SetFocusHold(2) == kBadValue, "a value other than 0 or 1 is refused");
}

void TestCodeMismatchRefuses()
{
    Reset();
    Image image;
    gFakeBase = image.base;
    reinterpret_cast<std::uint8_t*>(image.base + kThrottleSleepRva)[1] = 0x06; // Sleep(6)
    Require(SetFocusHold(1) == kCodeMismatch, "a changed throttle loop is refused");
    Require(Logged("state=refused code=4 detail=code_mismatch"), "the refusal is logged with its reason");
    Require(image.Slot() == RealGetActiveWindow(), "the slot is untouched");
    Require(Reports("state=refused") && Reports("installed=0"), "the report says refused");
    ServiceFocusHold();
    Require(Count("state=refused") == 1, "a refusal is not retried");
}

void TestForeignSlotRefuses()
{
    Reset();
    Image image;
    gFakeBase = image.base;
    const auto foreign = reinterpret_cast<std::uintptr_t>(&FakeBase);
    image.SetSlot(foreign);
    Require(SetFocusHold(1) == kSlotMismatch, "a slot that is not USER32!GetActiveWindow is refused");
    Require(Logged("detail=import_not_getactivewindow"), "and says why");
    Require(image.Slot() == foreign, "another tool's pointer is left alone");
}

void TestNotPinnedWaits()
{
    Reset();
    Image image;
    gFakeBase = image.base;
    gPinned = 0;
    Require(SetFocusHold(1) == kOk, "an unpinned module is a wait, not a refusal");
    Require(Logged("state=pending detail=module_not_pinned"), "the wait is logged");
    Require(image.Slot() == RealGetActiveWindow(), "nothing is installed before the pin");
    Require(Reports("state=pending"), "the report says pending");
    SetFocusHold(0);
}

// The render window may not exist yet; the hold waits for it, and only a live
// CryENGINE window of this process is ever the answer.
void TestHoldWaitsForTheRenderWindowThenAnswers(Image& image, HWND window)
{
    Reset();
    gFakeBase = image.base;
    // No frames yet, so the cvar waits for the next test.
    gObserver = static_cast<DWORD>(FrameObserverRuntimeStatus::ready);
    Require(SetFocusHold(1) == kOk, "a hold before the window exists is accepted");
    Require(Logged("state=pending detail=no_render_window"), "and waits for the window");
    Require(image.Slot() == reinterpret_cast<std::uintptr_t>(&AnswerGetActiveWindow), "the slot is answered");
    Require(image.SlotProtection() == PAGE_READONLY, "the slot's protection is put back");
    Require(image.CallSlot() == nullptr, "not yet held: the real answer passes through");

    const HWND other = MakeWindow("NotCryENGINE");
    image.SetWindow(other);
    ServiceFocusHold();
    Require(!gHeld.load(), "a window of another class is not the render window");
    Require(Count("state=pending") == 1, "the wait is logged once");
    DestroyWindow(other);

    image.SetWindow(window);
    ServiceFocusHold();
    Require(gHeld.load() && gWindow.load() == window, "held once the renderer's window is live");
    Require(Logged("state=held window=0x"), "state=held is logged for the launcher");
    Require(Logged("audio result=refused code=8 detail=hook_failed") && Logged("audio=native"),
            "an audio hook that cannot be created is reported, and does not stop the hold");
    Require(Logged("window foreground=0 minimized=0"), "the window's state is logged");
    const auto answered = gAnswered.load();
    Require(image.CallSlot() == window, "the throttle sees the render window as active");
    Require(gAnswered.load() == answered + 1, "and the answer is counted");
    ServiceFocusHold();
    Require(Count("window foreground=") == 1, "an unchanged state is not logged again");
    Require(Reports("state=held") && Reports("installed=1"), "the report says held");
}

void TestPauseIsSetOnceFramesDrain()
{
    gObserver = static_cast<DWORD>(FrameObserverRuntimeStatus::ready);
    gQueued.clear();
    ServiceFocusHold();
    Require(gQueued.empty(), "nothing is queued while no frame drains the console");
    gObserver = static_cast<DWORD>(FrameObserverRuntimeStatus::enabled);
    gQueueResult = static_cast<DWORD>(ConsoleBridgeResult::busy);
    ServiceFocusHold();
    Require(gQueued.empty() && !Logged("queue_refused"), "a busy queue is retried, not failed");
    gQueueResult = 0;
    ServiceFocusHold();
    Require(gQueued.size() == 1 && gQueued[0] == "g_pauseOnLoseFocus 0", "pause on focus loss is switched off");
    ServiceFocusHold();
    Require(gQueued.size() == 1, "nothing more is queued while it is verified");
    ++gVerifiedCount;
    ServiceFocusHold();
    Require(Logged("pause_on_lose_focus=0 result=0 detail=verified_readback"), "the readback is logged");
    Require(Reports("pauseOnLoseFocus=0"), "the report shows it applied");
    ServiceFocusHold();
    Require(gQueued.size() == 1, "an applied value is not sent again");
}

void TestReleaseRestoresThePause(Image& image, HWND window)
{
    gQueued.clear();
    Require(SetFocusHold(0) == kOk, "a release is accepted");
    Require(Logged("state=released") && Logged("pause_on_lose_focus=restoring"), "the release is logged");
    Require(image.CallSlot() != window, "released: the real answer passes through");
    ServiceFocusHold();
    Require(gQueued.size() == 1 && gQueued[0] == "g_pauseOnLoseFocus 1", "the game's default is put back");
    ++gVerifiedCount;
    ServiceFocusHold();
    Require(Logged("pause_on_lose_focus=1 result=0"), "and verified");
    Require(Reports("state=off") && Reports("pauseOnLoseFocus=1"), "the report says off");
}

void TestFailedPauseIsNotRetried()
{
    Reset();
    Require(SetFocusHold(1) == kOk && gHeld.load(), "held again");
    ServiceFocusHold();
    Require(gQueued.size() == 1, "the cvar is queued");
    gVerifiedResult = static_cast<DWORD>(ConsoleBridgeResult::unavailable);
    ++gVerifiedCount;
    ServiceFocusHold();
    Require(Logged("pause_on_lose_focus=0 result=failed"), "a failed readback is logged");
    ServiceFocusHold();
    Require(gQueued.size() == 1, "and not retried every tick");
    Require(Reports("pauseFailed=0"), "the report shows the failure");
    gVerifiedResult = 0;
    SetFocusHold(0);
    ServiceFocusHold();
    Require(gQueued.size() == 2 && gQueued[1] == "g_pauseOnLoseFocus 1", "a value that may have landed is put back");
    ++gVerifiedCount;
    ServiceFocusHold();
}

void TestUntouchedPauseIsLeftAlone()
{
    Reset();
    gObserver = static_cast<DWORD>(FrameObserverRuntimeStatus::ready);
    Require(SetFocusHold(1) == kOk && gHeld.load(), "held without frames");
    ServiceFocusHold();
    SetFocusHold(0);
    gObserver = static_cast<DWORD>(FrameObserverRuntimeStatus::enabled);
    ServiceFocusHold();
    Require(gQueued.empty(), "a cvar the hold never set is not touched on release");
    Require(Logged("pause_on_lose_focus=native"), "and the release says so");
}

// The audio gate: the dispatcher, its manager call, the handler's switch, both
// focus cases and the table entry that routes type 0x10.
void TestAudioGateRefuses()
{
    Image image;
    Require(InstallAudio(image.base, true) == kHookFailed, "intact audio code reaches the hook (no MinHook here)");
    image.SetLoseCase(0xD5AD83);
    Require(InstallAudio(image.base, true) == kCodeMismatch, "a table that routes 0x10 elsewhere is refused");
    image.SetLoseCase(static_cast<std::uint32_t>(kAudioLoseFocusRva));
    reinterpret_cast<std::uint8_t*>(image.base + kAudioGetFocusRva)[0x1A] = 0x30; // OnLoseFocus's slot
    Require(InstallAudio(image.base, true) == kCodeMismatch, "a changed get-focus case is refused");
    Require(!gAudioHooked, "nothing was hooked");
}

std::vector<std::uint32_t> gDispatched;
std::vector<std::uint32_t> gManaged;
void __fastcall FakeDispatch(void* atl, void* request)
{
    (void)atl;
    std::uint32_t type = 0;
    gDispatched.push_back(ReadManagerType(request, type) ? type : 0);
    std::memcpy(static_cast<std::uint8_t*>(request) + kRequestStatus, &kStatusSuccess, sizeof(kStatusSuccess));
}
std::uint32_t __fastcall FakeManager(void* atl, void* request)
{
    std::uint32_t type = 0;
    Require(atl == reinterpret_cast<void*>(0x1234), "an issued request goes to the dispatcher's ATL");
    Require(ReadManagerType(request, type), "an issued request is a manager request");
    gManaged.push_back(type);
    return kStatusSuccess;
}
struct AudioRequest {
    alignas(16) std::uint8_t data[0x20]{};
    alignas(16) std::uint8_t request[0x30]{};
    AudioRequest(std::uint32_t requestClass, std::uint32_t type)
    {
        const auto address = reinterpret_cast<std::uintptr_t>(data);
        const std::uint32_t pending = 4;
        std::memcpy(data + kDataClass, &requestClass, sizeof(requestClass));
        std::memcpy(data + kDataManagerType, &type, sizeof(type));
        std::memcpy(request + kRequestData, &address, sizeof(address));
        std::memcpy(request + kRequestStatus, &pending, sizeof(pending));
    }
    std::uint32_t Status() const
    {
        std::uint32_t status = 0;
        std::memcpy(&status, request + kRequestStatus, sizeof(status));
        return status;
    }
};

void TestAudioKeepsFocusWhileHeld()
{
    Reset();
    gAudioOriginal.store(&FakeDispatch);
    gAudioManager.store(&FakeManager);
    gAudioNative.store(1);
    gAudioApplied.store(1);
    void* const atl = reinterpret_cast<void*>(0x1234);
    AudioRequest object(4, 0);

    AudioRequest lost(kClassManager, kLoseFocus);
    AnswerAudioRequest(atl, lost.request);
    Require(gDispatched.size() == 1 && gDispatched[0] == kLoseFocus, "not held: a loss runs");
    Require(gAudioNative.load() == 0 && gAudioApplied.load() == 0, "and is recorded as the audio's state");
    AnswerAudioRequest(atl, object.request);
    Require(gManaged.empty(), "not held: nothing is issued");

    gHeld.store(true);
    AnswerAudioRequest(atl, object.request);
    Require(gManaged.size() == 1 && gManaged[0] == kGetFocus, "held after a loss: focus is given back once");
    Require(gAudioApplied.load() == 1 && gAudioIssued.load() == 1, "and recorded");
    AnswerAudioRequest(atl, object.request);
    Require(gManaged.size() == 1, "only once");

    const auto dispatched = gDispatched.size();
    AudioRequest held(kClassManager, kLoseFocus);
    AnswerAudioRequest(atl, held.request);
    Require(gDispatched.size() == dispatched, "held: a loss is not run");
    Require(held.Status() == kStatusSuccess, "but is marked done for the engine");
    Require(gAudioSwallowed.load() == 1 && gAudioNative.load() == 0 && gAudioApplied.load() == 1,
            "and remembered as what the engine asked for");
    AudioRequest regained(kClassManager, kGetFocus);
    AnswerAudioRequest(atl, regained.request);
    Require(gDispatched.back() == kGetFocus && gAudioNative.load() == 1, "a get-focus runs as usual");
    AudioRequest mute(kClassManager, 0x40);
    AnswerAudioRequest(atl, mute.request);
    Require(gDispatched.back() == 0x40 && gManaged.size() == 1, "other manager requests pass untouched");

    AudioRequest lostAgain(kClassManager, kLoseFocus);
    AnswerAudioRequest(atl, lostAgain.request);
    gHeld.store(false);
    AnswerAudioRequest(atl, object.request);
    Require(gManaged.size() == 2 && gManaged[1] == kLoseFocus,
            "released while unfocused: the audio mutes as the game would");
    Require(gAudioApplied.load() == 0, "and is recorded");
    Require(Reports("audioSwallowed=2") && Reports("audioIssued=2"), "the report counts both");
}

// A renderer that recreates its window is held again from the field.
void TestWindowGoneIsHeldAgain(Image& image, HWND& window)
{
    Reset();
    Require(SetFocusHold(1) == kOk && gHeld.load(), "held");
    DestroyWindow(window);
    ServiceFocusHold();
    Require(!gHeld.load() && Logged("state=pending detail=window_gone"), "a destroyed window is let go");
    ServiceFocusHold();
    Require(!gHeld.load(), "the stale field is not held");
    window = MakeWindow("CryENGINE", WS_OVERLAPPEDWINDOW | WS_MINIMIZE);
    image.SetWindow(window);
    ServiceFocusHold();
    Require(gHeld.load() && gWindow.load() == window, "the new window is held");
    Require(Logged("window foreground=0 minimized=1"), "a minimised window is logged as minimised");
    SetFocusHold(0);
}
} // namespace

int main()
{
    gModuleBase = &FakeBase;
    RegisterClasses();
    TestBadValue();
    TestCodeMismatchRefuses();
    TestForeignSlotRefuses();
    TestNotPinnedWaits();
    Image image;
    HWND window = MakeWindow("CryENGINE");
    TestHoldWaitsForTheRenderWindowThenAnswers(image, window);
    TestPauseIsSetOnceFramesDrain();
    TestReleaseRestoresThePause(image, window);
    image.SetWindow(window);
    TestFailedPauseIsNotRetried();
    TestUntouchedPauseIsLeftAlone();
    TestWindowGoneIsHeldAgain(image, window);
    TestAudioGateRefuses();
    TestAudioKeepsFocusWhileHeld();
    DestroyWindow(window);
    std::cout << "focus hold: gate, slot, pending, answer, pause cvar, release, window replacement and audio focus passed\n";
    return 0;
}
