#pragma once

// The assertion every test executable shares. Each test is one translation
// unit, and ctest reads only its exit code. No project includes: some tests
// compile DLL sources directly and have no include path to preyvr/ (the
// geometry builders are in TestGeometry.h).
//
// Tolerance helpers (Near and friends) deliberately stay in each test: the
// epsilon, and whether the bound is strict, is part of what that test checks.
#include <cstdlib>
#include <iostream>

namespace preyvr::test {

inline void Require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}
inline void Check(bool condition, const char* message) { Require(condition, message); }

} // namespace preyvr::test
