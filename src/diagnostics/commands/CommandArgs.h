#pragma once

// **One rule for a number in a command** (docs/TUNING-COMMANDS.md), shared by
// the command channel and the lanes' own dispatchers (carry., grenade., arms.,
// mem., wrist.): decimal, the whole word, or it is malformed. A malformed value
// is the verb's default, so a typo cannot set something to whatever `atoi`
// makes of it; a verb with no default to fall back to refuses it.
#include <charconv>
#include <cmath>
#include <cstddef>
#include <string>
#include <system_error>
#include <vector>

namespace preyvr::dll {

// True, with `out` set, when all of `text` is a decimal integer that fits an
// int. `0310` is 310, and `0x96` is malformed.
inline bool ParseInt(const std::string& text, int& out)
{
    try {
        std::size_t consumed = 0;
        const int value = std::stoi(text, &consumed, 10);
        if (consumed != text.size()) {
            return false;
        }
        out = value;
        return true;
    } catch (...) {
        return false;
    }
}

// The same for a finite decimal number with a fraction or an exponent.
inline bool ParseNumber(const std::string& text, double& out)
{
    double value = 0;
    const char* const end = text.data() + text.size();
    const auto [stop, error] = std::from_chars(text.data(), end, value);
    if (error != std::errc{} || stop != end || !std::isfinite(value)) {
        return false;
    }
    out = value;
    return true;
}

// Word `i` as an integer, or `fallback` when it is missing or malformed.
inline int Arg(const std::vector<std::string>& args, std::size_t i, int fallback)
{
    int value = fallback;
    if (i < args.size()) {
        ParseInt(args[i], value);
    }
    return value;
}

} // namespace preyvr::dll
