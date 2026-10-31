// The zoom lane: the production lane with Prey replaced at its boundary -- the
// final hfov, cl_hfov and the gate.
#include "render/camera/ZoomLane.cpp"
#include "DllTestSupport.h"
#include "TestSupport.h"
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>

PREYVR_TEST_CAPTURE_LOG()
namespace {
using namespace preyvr;
using namespace preyvr::dll;
using preyvr::test::Check;
using preyvr::test::Logged;
namespace zn = preyvr::viewzoom::native;

constexpr float kDegree = 3.14159265358979f / 180.0f;
float Tangent(float degrees) { return std::tan(degrees * 0.5f * kDegree); }
float ViewFov(float hfov) { return 2.0f * std::atan(Tangent(hfov)); }
bool Near(float a, float b) { return std::fabs(a - b) < 1e-3f; }

bool Reports(const std::string& field)
{
    std::string report = " " + ZoomLaneReport() + " ";
    for (char& c : report) {
        if (c == '{' || c == '}') { c = ' '; }
    }
    return report.find(" " + field + " ") != std::string::npos;
}

// The player the camera sits in, and Prey's values for this frame.
std::uint8_t player[0x1400]{};
void* Camera() { return player + zn::PlayerCamera; }
float prey = 0, slider = 110;
const std::uint8_t* readPlayer = nullptr;
bool readable = true;
bool Read(const std::uint8_t* from, float& hfov, float& cl)
{
    readPlayer = from;
    hfov = prey;
    cl = slider;
    return readable;
}
DWORD gateResult = ERROR_SUCCESS;
int gateCalls = 0;
DWORD ResolveFake(std::uintptr_t& base)
{
    ++gateCalls;
    base = gateResult == ERROR_SUCCESS ? 0x180000000 : 0;
    return gateResult;
}

// One frame of Prey at this hfov.
void Frame(float hfov, bool driving = true)
{
    prey = hfov;
    NoteViewZoom(Camera(), ViewFov(hfov), driving);
}

void TestGate()
{
    gRead = Read;
    gResolve = ResolveFake;
    gateResult = ERROR_NOT_READY;
    Frame(60);
    Check(gateCalls == 1 && ViewZoomOf(Tangent(60)) == 1.0f, "before PreyDll is loaded: no zoom, and asked again");
    gateResult = ERROR_BAD_EXE_FORMAT;
    Frame(60);
    Frame(60);
    Check(gateCalls == 2 && Reports("gate=refused") && Logged("preyvr_view_zoom result=unavailable detail=bytes_mismatch"),
          "other bytes: refused once, and for good");
    Check(ViewZoomOf(Tangent(60)) == 1.0f, "and frames declared as they are, as without the lane");
    // The rest of the tests run past the gate.
    gChecked = false;
    gateResult = ERROR_SUCCESS;
    Frame(110);
    Check(gBase != 0 && Reports("gate=ok"), "the body the one read");
    Check(readPlayer == player, "the player is the camera's ArkPlayer");
}

void TestZoom()
{
    Frame(110);
    Check(ViewZoomOf(Tangent(110)) == 1.0f, "no zoom: the frame is as it was");
    // Opening the scope, then the psychoscope's own stop, then the deepest scan
    // lock measured: each as deep as Prey asks.
    Frame(104.4f);
    Check(Near(ViewZoomOf(Tangent(104.4f)), Tangent(110) / Tangent(104.4f)), "magnified by the tangents' ratio");
    Frame(49.4f);
    Check(std::fabs(ViewZoomOf(Tangent(49.4f)) - 3.1f) < 0.01f, "the psychoscope's stop");
    Frame(24.8f);
    Check(Near(ViewZoomOf(Tangent(24.8f)), Tangent(110) / Tangent(24.8f)), "no limit of ours past it");
    Check(Reports("now=6.496") && Reports("zoomed=3"), "the report shows the zoom now");
    // The zoom ends: one line, and the frames are as they were.
    Check(!Logged("detail=zoom peak"), "no line during a zoom");
    Frame(110);
    Check(ViewZoomOf(Tangent(110)) == 1.0f, "zoomed out");
    Check(Logged("preyvr_view_zoom result=0 detail=zoom peak=6.50 frames=3"), "the zoom's line at its end");
    Check(Reports("zooms=1") && Reports("now=1.000"), "one zoom, and none now");
    // A sprint: wider than the slider, and declared as it is.
    Frame(115.4f);
    Check(ViewZoomOf(Tangent(115.4f)) == 1.0f && Reports("zooms=1"), "a wider view is no zoom");
}

void TestNotDriving()
{
    Frame(60);
    Check(ViewZoomOf(Tangent(60)) > 1.5f, "zoomed");
    Frame(24.8f, false);
    Check(ViewZoomOf(Tangent(24.8f)) == 1.0f, "VR not driving the view: declared as it is");
    NoteViewZoom(nullptr, 1.0f, true);
    Check(ViewZoomOf(0.5f) == 1.0f, "no camera: no zoom");
}

void TestUnreadable()
{
    Frame(60);
    readable = false;
    Frame(24.8f);
    Check(ViewZoomOf(Tangent(24.8f)) == 1.0f && Reports("unread=1"), "declared as it is, and counted");
    readable = true;
    slider = std::numeric_limits<float>::quiet_NaN();
    Frame(24.8f);
    Check(ViewZoomOf(Tangent(24.8f)) == 1.0f && Reports("unread=2"), "a slider that is no angle the same");
    slider = 110;
}

} // namespace

int main()
{
    TestGate();
    TestZoom();
    TestNotDriving();
    TestUnreadable();
    std::cout << "zoom_lane: zooms magnified as deep as Prey goes, unzoomed frames untouched, other bytes refused\n";
    return 0;
}
