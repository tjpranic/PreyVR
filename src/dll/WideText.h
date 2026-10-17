#pragma once

// Windows wide text as UTF-8: paths for the log, labels for ImGui.
//
// Header-only, with no preyvr/ includes, because some tests compile a lane's
// .cpp directly.
#include <windows.h>

#include <string>
#include <string_view>

namespace preyvr::dll {

// Empty when `text` is empty or cannot be converted.
inline std::string Utf8(std::wstring_view text)
{
    if (text.empty()) {
        return {};
    }
    const int needed = WideCharToMultiByte(
        CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (needed <= 0) {
        return {};
    }
    std::string result(static_cast<std::size_t>(needed), '\0');
    WideCharToMultiByte(
        CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), needed, nullptr, nullptr);
    return result;
}

} // namespace preyvr::dll
