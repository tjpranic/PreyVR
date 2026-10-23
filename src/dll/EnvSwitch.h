#pragma once

// The launcher's process-wide kill switches (PREYVR_CULL_WATCH=0, -NearFx 0 and
// the like). Header-only so tests that compile a module's .cpp directly need
// nothing extra.
#include <windows.h>

namespace preyvr::dll {

// True when the environment variable is set and starts with '0'. Unset, empty,
// any other value, or one too long for the buffer: false.
inline bool EnvSwitchOff(const wchar_t* name)
{
    wchar_t value[8]{};
    return GetEnvironmentVariableW(name, value, 8) > 0 && value[0] == L'0';
}

} // namespace preyvr::dll
