#include "PassCreationHook.h"
#include "HookInstall.h"
#include "MinHookInit.h"

#include "FrameProbe.h"
#include "Logger.h"
#include "preyvr/EngineMap.h"

#include <MinHook.h>
#include <intrin.h>

#include <windows.h>

#include <atomic>
#include <cstring>
#include <string>

namespace preyvr::dll {
namespace {

void Log(const std::string& line)
{
    lifecycle::Log("preyvr_pass_camera " + line);
}

// R-071 CreateGeneralPassRenderingInfo(out, const CCamera*, flags, bAuxWindow).
// RCX is a caller-supplied 64-byte stack buffer, RDX the camera.
using CreateGeneralPassFn =
    void*(__fastcall*)(void* out, const std::uint8_t* camera, std::uint32_t flags, std::uint32_t aux);

void* gTarget = nullptr;
std::atomic<CreateGeneralPassFn> gOriginal{nullptr};
bool gHookCreated = false;

void* __fastcall CreateGeneralPassObserved(
    void* out, const std::uint8_t* camera, std::uint32_t flags, std::uint32_t aux)
{
    const CreateGeneralPassFn original = gOriginal.load(std::memory_order_acquire);
    void* const result = original != nullptr ? original(out, camera, flags, aux) : nullptr;
    // After the original, which is what writes the frame ids; a no-op unless
    // the frame probe is recording.
    ProbePassCreated(out, flags, _ReturnAddress());
    return result;
}

} // namespace

bool EnsurePassCreationHook()
{
    if (gHookCreated) {
        return true;
    }
    // Resolved from the landmark table, so this inherits the fail-closed byte gate
    // rather than trusting a hardcoded address.
    const engine::Landmark* const landmark = engine::FindLandmark("pass.create_general");
    if (landmark == nullptr) {
        Log("result=unavailable detail=landmark_missing");
        return false;
    }
    const HMODULE preyDll = GetModuleHandleW(L"PreyDll.dll");
    if (preyDll == nullptr) {
        return false;
    }
    const auto target = reinterpret_cast<std::uintptr_t>(preyDll) + landmark->rva;
    if (std::memcmp(reinterpret_cast<const void*>(target),
                    landmark->expected.data(), landmark->expected.size()) != 0) {
        Log("result=unavailable detail=prologue_mismatch");
        return false;
    }

    gTarget = reinterpret_cast<void*>(target);
    const auto installed = hook::Install(gTarget, reinterpret_cast<void*>(&CreateGeneralPassObserved), gOriginal);
    if (!installed) {
        Log(std::string("result=failed detail=") + hook::StepName(installed.step) + " minhook=" +
            MH_StatusToString(installed.status));
        return false;
    }
    gHookCreated = true;
    Log("result=0 detail=hook_installed target=CreateGeneralPassRenderingInfo");
    return true;
}

} // namespace preyvr::dll
