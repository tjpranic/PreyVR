#pragma once

// What tests that compile DLL sources share (TestSupport.h has the assertion
// every test shares): the captured log and its searches, and stubs for the
// DLL's boundary functions that each test opts into.
//
// Each stub is a macro placed at file scope, after the DLL source is included:
// it expands to a qualified definition, so it compiles only where that source
// already declared the function (Logger.h, MinHookInit.h). A test that needs a
// stub to behave differently writes its own instead.
//
// No project includes, for the same reason as TestSupport.h.
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace preyvr::test {

// Every line PREYVR_TEST_CAPTURE_LOG received, oldest first.
inline std::vector<std::string> gLines;

// Whether one captured line contains a, b and c (b and c match any line).
inline bool Logged(const char* a, const char* b = "", const char* c = "")
{
    for (const auto& line : gLines) {
        if (line.find(a) != std::string::npos && line.find(b) != std::string::npos &&
            line.find(c) != std::string::npos) {
            return true;
        }
    }
    return false;
}

// How many captured lines contain a, b and c.
inline std::size_t Count(const char* a, const char* b = "", const char* c = "")
{
    std::size_t n = 0;
    for (const auto& line : gLines) {
        if (line.find(a) != std::string::npos && line.find(b) != std::string::npos &&
            line.find(c) != std::string::npos) {
            ++n;
        }
    }
    return n;
}

// What PREYVR_TEST_STUB_MINHOOK answers, and how many times it was asked.
inline bool gMinHookReady = false;
inline unsigned gMinHookCalls = 0;

} // namespace preyvr::test

// preyvr::lifecycle::Log appends each line to preyvr::test::gLines.
#define PREYVR_TEST_CAPTURE_LOG() \
    void preyvr::lifecycle::Log(std::string_view line) { ::preyvr::test::gLines.emplace_back(line); }

// preyvr::lifecycle::Log drops every line.
#define PREYVR_TEST_DROP_LOG() \
    void preyvr::lifecycle::Log(std::string_view) {}

// preyvr::dll::EnsureMinHook never initialises MinHook: it counts the call and
// answers gMinHookReady (false unless the test sets it).
#define PREYVR_TEST_STUB_MINHOOK() \
    bool preyvr::dll::EnsureMinHook() \
    { \
        ++::preyvr::test::gMinHookCalls; \
        return ::preyvr::test::gMinHookReady; \
    }
