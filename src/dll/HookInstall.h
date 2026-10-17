#pragma once

// One way to put a MinHook detour on an engine function, so every installer gets
// the same ordering and the same failure handling:
//
//  * The trampoline is published before the hook is enabled: the detour can run
//    the moment it is live, and it calls through that pointer.
//  * A hook whose enable fails is removed again, so a retry creates it afresh
//    instead of failing for ever with MH_ERROR_ALREADY_CREATED.
//  * A set of hooks that only work together is enabled in one MH_ApplyQueued,
//    and on any failure every one of them is disabled and removed.
//  * Nothing here calls MH_Uninitialize: that would remove every other
//    feature's hooks while EnsureMinHook still reported ready.
//
// Callers keep their own byte gates and their own log lines; these helpers
// report which step failed and MinHook's status. Header-only, with no preyvr/
// includes, because some tests compile a lane's .cpp directly.
#include "MinHookInit.h"

#include <MinHook.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>

namespace preyvr::dll::hook {

enum class Step { ok, prologue, create, enable };

struct Result {
    Step step = Step::ok;
    MH_STATUS status = MH_OK;
    std::size_t index = 0;   // which hook of a set failed
    explicit operator bool() const { return step == Step::ok; }
};

// "prologue_mismatch", "create_hook" or "enable_hook": the words the installers
// already log.
inline const char* StepName(Step step)
{
    switch (step) {
    case Step::prologue: return "prologue_mismatch";
    case Step::create: return "create_hook";
    case Step::enable: return "enable_hook";
    case Step::ok: break;
    }
    return "ok";
}

namespace detail {
template <class Fn>
void Publish(std::atomic<Fn>& slot, void* trampoline)
{
    slot.store(reinterpret_cast<Fn>(trampoline), std::memory_order_release);
}
template <class Fn>
void Publish(Fn& slot, void* trampoline)
{
    slot = reinterpret_cast<Fn>(trampoline);
}
} // namespace detail

// Creates the hook and publishes its trampoline, without enabling it: for the
// installers that keep the trampoline for the life of the process and enable it
// separately. `original` is a function pointer or a std::atomic of one.
template <class Slot>
Result Create(void* target, void* detour, Slot& original)
{
    EnsureMinHook();   // idempotent; without it MH_CreateHook is NOT_INITIALIZED
    void* trampoline = nullptr;
    const MH_STATUS status = MH_CreateHook(target, detour, &trampoline);
    if (status != MH_OK) { return {Step::create, status}; }
    detail::Publish(original, trampoline);
    return {};
}

// Creates, publishes and enables; removes the hook again if enabling fails.
template <class Slot>
Result Install(void* target, void* detour, Slot& original)
{
    Result created = Create(target, detour, original);
    if (!created) { return created; }
    const MH_STATUS status = MH_EnableHook(target);
    if (status != MH_OK) {
        MH_RemoveHook(target);
        detail::Publish(original, nullptr);
        return {Step::enable, status};
    }
    return {};
}

// Install after checking that `target` starts with `prologue`. An empty
// prologue never matches, so a missing landmark cannot pass as "nothing to check".
template <class Slot>
Result InstallVerified(void* target, std::span<const std::uint8_t> prologue, void* detour, Slot& original)
{
    if (prologue.empty() || std::memcmp(target, prologue.data(), prologue.size()) != 0) {
        return {Step::prologue};
    }
    return Install(target, detour, original);
}

// InstallVerified with the lanes' shared log lines:
//   result=unavailable detail=prologue_mismatch target=<name>
//   result=failed detail=create_hook target=<name>   (and enable_hook)
template <class Slot>
bool HookAt(void* target, std::span<const std::uint8_t> prologue, void* detour, Slot& original, const char* name,
            void (*log)(const std::string&))
{
    const Result result = InstallVerified(target, prologue, detour, original);
    if (!result) {
        log(std::string(result.step == Step::prologue ? "result=unavailable detail=" : "result=failed detail=") +
            StepName(result.step) + " target=" + name);
    }
    return static_cast<bool>(result);
}

// One hook of a set that only works whole.
struct Spec {
    void* target;
    void* detour;
    void** original;   // filled by MH_CreateHook
};

// Creates every hook, calls `publish()` (the originals are filled in, nothing is
// live yet), then enables them all in one MH_ApplyQueued. On any failure every
// hook of the set is disabled and removed, and `index` names the one that failed.
template <class Publish>
Result InstallAll(std::span<const Spec> hooks, Publish&& publish)
{
    EnsureMinHook();
    std::size_t made = 0;
    auto undo = [&](Result failure) {
        for (std::size_t i = 0; i < made; ++i) {
            MH_DisableHook(hooks[i].target);
            MH_RemoveHook(hooks[i].target);
        }
        return failure;
    };
    for (; made < hooks.size(); ++made) {
        const MH_STATUS status = MH_CreateHook(hooks[made].target, hooks[made].detour, hooks[made].original);
        if (status != MH_OK) { return undo({Step::create, status, made}); }
    }
    publish();
    for (std::size_t i = 0; i < hooks.size(); ++i) {
        const MH_STATUS status = MH_QueueEnableHook(hooks[i].target);
        if (status != MH_OK) { return undo({Step::enable, status, i}); }
    }
    const MH_STATUS status = MH_ApplyQueued();
    if (status != MH_OK) { return undo({Step::enable, status, hooks.size()}); }
    return {};
}

inline Result InstallAll(std::span<const Spec> hooks)
{
    return InstallAll(hooks, [] {});
}

} // namespace preyvr::dll::hook
