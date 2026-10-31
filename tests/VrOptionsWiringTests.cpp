// VR Options' wiring against the real lanes (vr_options_runtime has stubs in
// their place): each setting's lane starts at its Spec default, and
// takes the Spec's whole range -- its minimum, maximum and default each read
// back as applied. A lane writes its default and range again in its own terms
// (a bool, milli-degrees, metres), so this is what keeps them the menu's.
//
// Two lists say where that does not hold offline, and why:
//  - kStartOff: three lanes start off, and VR start sets them to their defaults
//    (VrMode.cpp, options::Default).
//  - kNeedsPrey: four setters install a hook or reach Prey to turn on, so here,
//    with no Prey, they take off and refuse anything else.
//
// Offline and inert, like command_channel: PreyDll is never loaded, MinHook is
// stubbed and never called, and the log is captured. Including the TU exposes
// the Wiring table.
#include "../src/dll/VrOptionsRuntime.cpp"
#include "MinHookInit.h"
#include "DllTestSupport.h"
#include "TestSupport.h"
#include <algorithm>
#include <iostream>
#include <iterator>

PREYVR_TEST_CAPTURE_LOG()
PREYVR_TEST_STUB_MINHOOK()
// The logger's other entry points.
std::filesystem::path preyvr::lifecycle::LogPath()
{
    return std::filesystem::temp_directory_path() / L"PreyVR-vr-options-wiring-tests" / L"PreyVR.log";
}
void preyvr::lifecycle::ResetLog() {}

namespace {
using namespace preyvr;
using namespace preyvr::dll;
using preyvr::test::Require;

constexpr options::Setting kStartOff[] = {options::GrenadeHand, options::CarryHand, options::AimLasers};
constexpr options::Setting kNeedsPrey[] = {options::PsiTarget, options::Medkit, options::GrenadeHand,
                                           options::CarryHand};

template <std::size_t N>
bool In(const options::Setting (&list)[N], unsigned id)
{
    return std::find(std::begin(list), std::end(list), id) != std::end(list);
}

void TestDefaults()
{
    const auto defaults = options::Defaults();
    for (unsigned id = 0; id < options::Count; ++id) {
        Require(options::Default(id) == options::Describe(id).initial && defaults[id] == options::Default(id),
                "Default is the Spec's own, as Defaults() has it");
    }
}

void TestLanesStartAtTheirDefaults()
{
    unsigned failures = 0;
    for (unsigned id = 0; id < options::Count; ++id) {
        const char* key = options::Describe(id).key;
        const int lane = wiring[id].read(), wanted = options::Default(id);
        const bool startsOff = In(kStartOff, id);
        if (startsOff ? !(lane == 0 && wanted != 0) : lane != wanted) {
            ++failures;
            std::cerr << key << ": the lane starts at " << lane << ", the Spec's default is " << wanted
                      << (startsOff ? " (listed as starting off)" : "") << '\n';
        }
    }
    Require(failures == 0, "every lane starts at its Spec default, but the three VR start turns on");
}

void TestLanesTakeTheWholeRange()
{
    unsigned failures = 0;
    for (unsigned id = 0; id < options::Count; ++id) {
        const auto& spec = options::Describe(id);
        const bool needsPrey = In(kNeedsPrey, id);
        // Off, then the top of the range, then the default; with no Prey, a
        // setter that needs it takes off and keeps it.
        const int steps[] = {spec.min, spec.max, spec.initial};
        for (const int value : steps) {
            wiring[id].apply(value);
            const int read = wiring[id].read(), wanted = needsPrey ? spec.min : value;
            if (read != wanted) {
                ++failures;
                std::cerr << spec.key << ": applied " << value << ", read back " << read << ", wanted " << wanted
                          << (needsPrey ? " (listed as needing Prey)" : "") << '\n';
            }
        }
    }
    Require(failures == 0, "every lane takes its Spec's minimum, maximum and default");
}

} // namespace

int main()
{
    Require(GetModuleHandleW(L"PreyDll.dll") == nullptr, "no Prey in this process");
    TestDefaults();
    TestLanesStartAtTheirDefaults();
    TestLanesTakeTheWholeRange();
    Require(preyvr::test::gMinHookCalls == 0, "no setting tried to install a hook");
    Require(GetModuleHandleW(L"openxr_loader.dll") == nullptr, "and none loaded the OpenXR loader");
    std::cout << "vr_options_wiring: " << options::Count << " settings against their lanes passed\n";
    return 0;
}
