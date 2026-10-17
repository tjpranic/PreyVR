#pragma once

// Number formatting for log and report lines, one copy for every module.
#include "preyvr/VrMath.h"

#include <cstdio>
#include <string>

namespace preyvr {

// printf's %.*f.
inline std::string Fixed(float value, int decimals = 3)
{
    char text[32];
    std::snprintf(text, sizeof(text), "%.*f", decimals, static_cast<double>(value));
    return text;
}

// "x,y,z", three decimals each.
inline std::string Point(Vec3 v) { return Fixed(v.x) + "," + Fixed(v.y) + "," + Fixed(v.z); }

// Lower case with a 0x prefix and no leading zeros: 0x0, 0x7ff6a1c0.
inline std::string Hex(unsigned long long value)
{
    static const char* const digits = "0123456789abcdef";
    std::string out = "0x";
    bool leading = true;
    for (int shift = 60; shift >= 0; shift -= 4) {
        const unsigned int nibble = static_cast<unsigned int>((value >> shift) & 0xFull);
        if (nibble == 0 && leading && shift != 0) {
            continue;
        }
        leading = false;
        out.push_back(digits[nibble]);
    }
    return out;
}

} // namespace preyvr
