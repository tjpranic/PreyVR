// Near Clip Distance (render/stereo/NearClip.h): the view's near plane written while VR
// drives the view, a near plane Prey set itself kept, ours given back after, and
// the VR Options setting's range the one the view takes.
#include "render/stereo/NearClip.h"
#include "ui/options/VrOptions.h"
#include "TestSupport.h"

#include <cmath>
#include <iostream>

using namespace preyvr::nearclip;
using preyvr::test::Require;

namespace {

bool Near(float a, float b) { return std::fabs(a - b) < 1e-6f; }

void TestRange()
{
    Require(ClampCm(0) == 1 && ClampCm(1) == 1 && ClampCm(3) == 3 && ClampCm(10) == 10 && ClampCm(11) == 10 &&
                ClampCm(1000) == 10,
            "1 to 10 cm, anything else taken as the nearer end");
    Require(Near(Metres(3), 0.03f) && Near(Metres(10), 0.1f) && Near(Metres(0), 0.01f), "in metres, as the view takes it");
    Require(Metres(kMinCm) >= kEngineMinimum, "the smallest is one CView::Update honours, not its default");
    const auto& spec = preyvr::options::Describe(preyvr::options::NearClip);
    Require(spec.min == static_cast<int>(kMinCm) && spec.max == static_cast<int>(kMaxCm) &&
                spec.initial == static_cast<int>(kDefaultCm),
            "VR Options' Near Clip Distance is the range and default the view takes");
}

void TestNext()
{
    // What the field holds after a write: exactly the value written.
    const float three = Metres(3), six = Metres(6);
    // Prey's player view never sets one: 0, the view system's 10 cm.
    Step s = Next(0.0f, 0.0f, true, 3);
    Require(s.action == Action::write && s.value == three && Near(three, 0.03f), "VR drives the view: its near plane is ours");
    // Next frame the field still holds ours; a new setting replaces it.
    s = Next(three, three, true, 3);
    Require(s.action == Action::write && s.value == three, "ours stays ours");
    s = Next(three, three, true, 6);
    Require(s.action == Action::write && s.value == six, "changed in the menu: the new one at once");
    // A value below the engine's floor is no near plane: the default was in use.
    s = Next(0.005f, three, true, 3);
    Require(s.action == Action::write && s.value == three, "under 1 cm the view was at its default: ours");
    // A near plane Prey set itself.
    s = Next(0.24f, three, true, 3);
    Require(s.action == Action::keep && s.value == 0.24f, "Prey's own near plane is kept");
    s = Next(0.24f, 0.0f, false, 3);
    Require(s.action == Action::keep, "and left with VR off");
    // VR stops driving the view: ours is given back, nothing else is touched.
    s = Next(three, three, false, 3);
    Require(s.action == Action::clear && s.value == 0.0f, "VR off: the view system's default again");
    s = Next(0.0f, 0.0f, false, 3);
    Require(s.action == Action::none, "nothing of ours there: nothing to do");
    s = Next(0.0f, three, false, 3);
    Require(s.action == Action::none, "a view at its default is left at it");
}

} // namespace

int main()
{
    TestRange();
    TestNext();
    std::cout << "near_clip: the view's near plane ours while VR drives it, Prey's own kept, the default given back\n";
    return 0;
}
