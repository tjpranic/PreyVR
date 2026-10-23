#include "preyvr/InventoryDepth.h"
#include "TestSupport.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

using namespace preyvr::inventory;

namespace {
using preyvr::test::Require;
bool Near(float a, float b, float tolerance = .0001f) { return std::abs(a - b) < tolerance; }
} // namespace

int main()
{
    // Independent projection fixture: at a 2m-wide panel, eyes +/-32mm,
    // C=40000. A vertex at W=C has no texture disparity; W=C/2 receives
    // crossed disparity and W=2C uncrossed disparity. Check after divide.
    for(float w:{20000.f,40000.f,80000.f}) {
        std::array<float,16> native{1,0,0,12000,0,1,0,9000,0,0,0,0,0,0,0,w};
        auto left=native,right=native,control=native;
        Require(ApplyStereoParallax(left,40000,2,-.032f,1) &&
                ApplyStereoParallax(right,40000,2,.032f,1),"stereo projection accepted");
        const float disparity=left[3]/w-right[3]/w;
        Require(Near(disparity,.064f*(40000.f/w-1.f)),"disparity sign and magnitude after homogeneous divide");
        for(unsigned i=4;i<16;++i)Require(left[i]==native[i],"Y/Z/W and native clipping are unchanged");
        Require(ApplyStereoParallax(control,40000,2,-.032f,0) && control==native,"zero depth is bit-exact native output");
        Require(!ApplyStereoParallax(control,40000,0,-.032f,1) && control==native,"invalid geometry refuses without mutation");
    }
    // Tilted geometry has varying W across a triangle. Correct every column,
    // not only the translation; otherwise the triangle distorts incorrectly.
    std::array<float,16> tilted{1,0,0,0,0,1,0,0,0,0,0,0,.2f,-.3f,0,40000};
    auto eye=tilted;
    Require(ApplyStereoParallax(eye,40000,2,-.032f,.5f),"tilted transform accepted");
    Require(Near(eye[0],1.f-.016f*.2f) && Near(eye[1],.016f*.3f) && eye[3]==0,
        "tilted homogeneous depth contributes to the full X row");
    std::cout << "Inventory stereo projection passed\n";
    return 0;
}
