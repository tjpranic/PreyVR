#pragma once

// One console setting held at a wanted value through the verified
// renderer-setting slot (ConsoleBridgeWin32.h QueueVerifiedRendererSetting): it
// is queued once the frame observer runs, since console commands drain on
// observed frames, confirmed by the slot's readback, and a value that failed is
// not tried again until another is wanted. ControllerPrompts and FocusHold each
// hold one. Header-only so the tests that compile them need nothing more.
#include "ConsoleBridgeWin32.h"
#include "FrameObserverHook.h"

#include <windows.h>

#include <string>

namespace preyvr::dll {

struct VerifiedSetting {
    // The value asked for, confirmed, in flight and refused; -1 for none.
    int wanted = -1, applied = -1, sending = -1, failed = -1;
    // The slot's completion count when ours was queued: the next one is ours.
    unsigned long long count = 0;

    // One step of the handshake. `key` names the setting in the log lines
    // (`key=value result=...`); `command(value)` is the console line for a value.
    template <class Command, class Log>
    void Advance(const char* key, Command&& command, Log&& log)
    {
        if (sending >= 0) {
            if (VerifiedRendererSettingCount() == count) { return; }
            const bool verified = VerifiedRendererSettingResult() == 0;
            log(std::string(key) + "=" + std::to_string(sending) +
                (verified ? " result=0 detail=verified_readback" : " result=failed detail=readback_timeout_or_mismatch"));
            if (verified) { applied = sending; }
            else { failed = sending; }
            sending = -1;
        }
        if (wanted < 0 || wanted == applied || wanted == failed) { return; }
        // Console commands drain on observed frames; queued before that, this
        // would hold the shared slot for nothing.
        if (FrameObserverStatus() != static_cast<DWORD>(FrameObserverRuntimeStatus::enabled)) { return; }
        const auto before = VerifiedRendererSettingCount();
        const DWORD queued = QueueVerifiedRendererSetting(command(wanted));
        if (queued == 0) {
            sending = wanted;
            count = before;
        } else if (queued != static_cast<DWORD>(ConsoleBridgeResult::busy)) {
            failed = wanted;
            log(std::string(key) + "=" + std::to_string(wanted) + " result=" + std::to_string(queued) +
                " detail=queue_refused");
        }
    }
};

} // namespace preyvr::dll
