#include "LoadingScreen.h"
#include "SafeMemory.h"
#include "HookInstall.h"
#include "Bootstrap.h"
#include "Logger.h"
#include "XrSessionHost.h"
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>

namespace preyvr::dll {
namespace {
// Steam PreyDll SHA256 7d6e322f...05311a7. tools/re/verify_loading_screen.py
// checks the gate and what it relies on offline.
//
// With gfx_loadtimethread 1 (the default), FlashUI (0x2D8810) hands its loading
// screen to the renderer, and SRenderThread::Run (0xFE96F0) loops on Prey's render
// thread until the load ends: the loadtime update, then, holding SRenderThread+0x18
// (which RenderLoadingThread takes for some of its commands), the loadtime render
// and this present: renderer vtable +0x8E0, void(CD3D9Renderer*, int frameId).
// Meanwhile ProcessCommands defers every RT_EndFrame and runs one after the loop.
//
// The present's entry through its swapchain Present (+0xAE88, slot 0x40), the one
// RT_EndFrame presents: the backbuffer holds the loading screen until that call.
constexpr std::uintptr_t kPresentRva = 0xF7E820;
constexpr std::uint8_t kPresentCode[] = {
    0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x40,0x48,0x8B,0xD9,0x8B,0xFA,0x48,
    0x8B,0x89,0x38,0xA1,0x00,0x00,0x48,0x85,0xC9,0x74,0x05,0xE8,0xD0,0xB6,0x04,0x00,
    0x48,0x8D,0x8B,0xE0,0x46,0x00,0x00,0x8B,0xD7,0xE8,0x62,0xF5,0x0F,0x00,0x48,0x8B,
    0xCB,0xE8,0xCA,0x7C,0x78,0x00,0x83,0xBB,0xEC,0xB1,0x00,0x00,0x00,0x44,0x8B,0x83,
    0xC4,0xB1,0x00,0x00,0x8B,0x93,0xC0,0xB1,0x00,0x00,0x0F,0x95,0xC1,0xE8,0x6E,0x98,
    0xFF,0xFF,0x48,0x8B,0x8B,0x88,0xAE,0x00,0x00,0x8B,0xD0,0x44,0x8B,0x83,0xF0,0xB1,
    0x00,0x00,0x4C,0x8B,0x09,0x41,0xFF,0x51,0x40};
// The loop's only call of it: lea rcx,[rdi+0x18]; call EnterCriticalSection;
// renderer vtable +0x8E8 (0); the callback's LoadtimeRender (+0x08); vtable
// +0x8E0 (frameId); lea rcx,[rdi+0x18]; call LeaveCriticalSection.
constexpr std::uintptr_t kLoopRva = 0xFE9981;
constexpr std::uint8_t kLoopCode[] = {
    0x48,0x8D,0x4F,0x18,0xE8,0xA6,0x1C,0x0B,0xFF,0x48,0x8B,0x0D,0x4F,0x4F,0xB5,0x01,
    0x33,0xD2,0x48,0x8B,0x01,0xFF,0x90,0xE8,0x08,0x00,0x00,0x48,0x8B,0x4F,0x10,0x48,
    0x85,0xC9,0x74,0x06,0x48,0x8B,0x01,0xFF,0x50,0x08,0x48,0x8B,0x0D,0x2E,0x4F,0xB5,
    0x01,0x8B,0xD6,0x48,0x8B,0x01,0xFF,0x90,0xE0,0x08,0x00,0x00,0x48,0x8D,0x4F,0x18,
    0xE8,0x6A,0x60,0xB5,0x00};
// CD3D9Renderer's vtable and the slot the loop calls, which must hold the target.
constexpr std::uintptr_t kRendererVtableRva = 0x1DD2E08, kPresentSlot = 0x8E0;

using PresentFn = void(__fastcall*)(void* renderer, int frameId);
std::atomic<PresentFn> gOriginal{nullptr};
std::atomic<unsigned long long> gPresents{0};
std::mutex gInstallMutex;
bool gInstalled = false;
DWORD gRefusal = 0;

void Log(const std::string& line) { lifecycle::Log("preyvr_loading_screen " + line); }

// Prey's render thread, in the loading loop, holding the loop's lock.
void __fastcall PresentLoadingScreen(void* renderer, int frameId)
{
    gPresents.fetch_add(1, std::memory_order_relaxed);
    // Before the original: its Present hands the backbuffer to the window.
    CaptureXrLoadingScreen(renderer);
    if (const auto original = gOriginal.load(std::memory_order_acquire)) original(renderer, frameId);
}

enum : DWORD { kOk = 0, kNotPinned = 2, kNoModule = 3, kCodeMismatch = 4, kSlotMismatch = 5, kHookFailed = 8 };
} // namespace

DWORD EnsureLoadingScreenHook()
{
    std::lock_guard lock(gInstallMutex);
    if (gInstalled) return kOk;
    // A refused gate stays refused: the bytes will not change in this process.
    if (gRefusal == kCodeMismatch || gRefusal == kSlotMismatch) return gRefusal;
    if (ModulePinStatus() == 0) return kNotPinned;
    const auto module = GetModuleHandleW(L"PreyDll.dll");
    if (module == nullptr) return kNoModule;
    const auto base = reinterpret_cast<std::uintptr_t>(module);
    auto* const target = reinterpret_cast<void*>(base + kPresentRva);
    if (!SafeEqual(target, kPresentCode, sizeof(kPresentCode)) ||
        !SafeEqual(reinterpret_cast<const void*>(base + kLoopRva), kLoopCode, sizeof(kLoopCode))) {
        Log("result=refused detail=code_mismatch loading_screen=black");
        return gRefusal = kCodeMismatch;
    }
    std::uintptr_t slot = 0;
    if (!SafeRead(base + kRendererVtableRva + kPresentSlot, slot) || slot != base + kPresentRva) {
        Log("result=refused detail=slot_mismatch loading_screen=black");
        return gRefusal = kSlotMismatch;
    }
    const auto result = hook::Install(target, reinterpret_cast<void*>(&PresentLoadingScreen), gOriginal);
    if (!result) {
        Log(std::string("result=failed detail=") + hook::StepName(result.step) + " minhook=" +
            MH_StatusToString(result.status) + " loading_screen=black");
        return gRefusal = kHookFailed;
    }
    gInstalled = true;
    gRefusal = kOk;
    Log("result=0 detail=hook_installed target=loadtime_present rva=0xF7E820");
    return kOk;
}

unsigned long long LoadingScreenPresentCount() { return gPresents.load(std::memory_order_relaxed); }
} // namespace preyvr::dll
