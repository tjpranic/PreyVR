#include "preyvr/RoomScale.h"
#include "preyvr/StereoCamera.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

using namespace preyvr;
using namespace preyvr::roomscale;

namespace {

void Require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << "\n";
        std::exit(1);
    }
}

constexpr float kDeg = 3.14159265f / 180.0f;
Vec3 Add(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 Sub(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
float Length(Vec3 a) { return std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z); }
bool Near(Vec3 a, Vec3 b, float e = 1e-4f) { return Length(Sub(a, b)) <= e; }

void TestShiftCancelsTheMove()
{
    for (float yaw : {-150.0f, -20.0f, 0.0f, 75.0f, 180.0f}) {
        for (float scale : {1.0f, 0.8f, 1.5f}) {
            const Vec3 head{0.4f, 1.6f, -1.1f}, origin{0.1f, 1.5f, 0.2f};
            const Vec3 move{0.07f, -0.03f, 0.0f};
            const Vec3 before = HeadOffsetWorld(head, origin, yaw * kDeg, scale);
            const Vec3 after = HeadOffsetWorld(head, Add(origin, OriginShift(move, yaw * kDeg, scale)), yaw * kDeg, scale);
            Require(Near(Sub(before, after), move, 1e-5f), "the origin shift takes exactly the move off the head offset");
        }
    }
    // OpenXR up is engine up: a head 0.2 m above the origin is 0.2 m up.
    const Vec3 up = HeadOffsetWorld({0, 0.2f, 0}, {}, 33.0f * kDeg, 1.0f);
    Require(Near(up, {0, 0, 0.2f}, 1e-5f), "height stays height");
    // OpenXR forward (-Z) at yaw 0 is engine +Y.
    Require(Near(HeadOffsetWorld({0, 0, -1}, {}, 0.0f, 1.0f), {0, 1, 0}, 1e-5f), "forward is engine +Y");
}

// The lane reduced to its arithmetic: the view eye is the native eye plus the
// head offset, the follower moves the capsule and the origin together.
struct World {
    Vec3 capsule{10, 20, 0};
    Vec3 origin{};
    float yaw = 30.0f * kDeg;
    float scale = 1.0f;
    Follower follower;
    Settings settings;
    Vec3 eye(const Vec3& head) const { return Add(Add(capsule, Vec3{0, 0, 1.7f}), HeadOffsetWorld(head, origin, yaw, scale)); }
    Vec3 Step(const Vec3& head, float dt)
    {
        const Vec3 move = follower.Update(HeadOffsetWorld(head, origin, yaw, scale), dt, settings);
        capsule = Add(capsule, move);
        origin = Add(origin, OriginShift(move, yaw, scale));
        return move;
    }
};

void TestWalkingNeverMovesTheView()
{
    World w;
    const float dt = 1.0f / 90.0f;
    const Vec3 start{0.0f, 1.6f, 0.0f};
    const Vec3 eye0 = w.eye(start);
    // Two metres back and half a metre to the side over two seconds, then still.
    for (int i = 0; i <= 360; ++i) {
        const float t = std::fmin(i / 180.0f, 1.0f);
        const Vec3 head{0.5f * t, 1.6f, 2.0f * t};
        const Vec3 expected = Add(eye0, HeadOffsetWorld(head, start, w.yaw, w.scale));
        w.Step(head, dt);
        Require(Near(w.eye(head), expected, 1e-4f), "following never moves the view");
    }
    const Vec3 offset = HeadOffsetWorld({0.5f, 1.6f, 2.0f}, w.origin, w.yaw, w.scale);
    Require(std::hypot(offset.x, offset.y) <= w.settings.startRadius + 1e-4f, "the body ends under the head");
    Require(Length(Sub(w.capsule, World{}.capsule)) > 1.9f, "it walked with the player");
    Require(std::fabs(offset.z - 1.6f) < 1e-5f, "height is never followed");
}

void TestNativeMovesAreNotAbsorbed()
{
    World w;
    const Vec3 head{0.0f, 1.6f, 0.0f};
    w.Step(head, 1.0f / 90.0f);
    const Vec3 eye0 = w.eye(head);
    // Stick walking, a lift, a scripted move: the capsule moves natively.
    w.capsule = Add(w.capsule, Vec3{1.0f, -2.0f, 0.5f});
    for (int i = 0; i < 90; ++i) {
        Require(Length(w.Step(head, 1.0f / 90.0f)) == 0.0f, "the follower does not chase a native move");
    }
    Require(Near(w.eye(head), Add(eye0, Vec3{1.0f, -2.0f, 0.5f})), "a native move moves the view");
}

void TestDeadzoneSpeedAndJumps()
{
    Settings s;
    Follower f;
    const float dt = 1.0f / 90.0f;
    Require(Length(f.Update({0.025f, 0, 0}, dt, s)) == 0.0f && !f.Following(), "inside the deadzone: still");
    const Vec3 m = f.Update({0.04f, 0, 0}, dt, s);
    Require(f.Following() && m.x > 0 && std::fabs(Length(m) - 0.04f * dt / s.followTime) < 1e-6f,
            "past it: glides, closing the gap at the time constant");
    Follower capped;   // a fresh one: from 4 cm to 1.2 m in one frame is a jump
    const Vec3 far = capped.Update({1.2f, 0, 0}, dt, s);
    Require(std::fabs(Length(far) - s.maxSpeed * dt) < 1e-5f, "never faster than the speed cap");
    Require(Length(f.Update({0.004f, 0, 0}, dt, s)) == 0.0f && !f.Following(), "close enough: stops");
    Require(Length(f.Update({0.025f, 0, 0}, dt, s)) == 0.0f, "and stays stopped inside the deadzone");
    // The glide converges: a 4 cm gap is closed to the stop radius within 0.2 s.
    {
        Follower g;
        float gap = 0.04f;
        int frames = 0;
        for (; frames < 90 && (frames == 0 || g.Following()); ++frames) {
            gap -= Length(g.Update({gap, 0, 0}, dt, s));
        }
        Require(gap <= s.stopRadius && frames <= 18, "the glide settles quickly");
    }
    Settings snap = s;
    snap.followTime = 0.0f;
    Follower h;
    Require(std::fabs(Length(h.Update({0.04f, 0, 0}, dt, snap)) - snap.maxSpeed * dt) < 1e-6f,
            "no time constant: at the speed cap");
    // A tracking jump: refused for the frame, then followed normally.
    f.Reset();
    f.Update({0.0f, 0, 0}, dt, s);
    Require(Length(f.Update({1.5f, 0, 0}, dt, s)) == 0.0f && f.Jumped(), "a jump is refused");
    Require(Length(f.Update({1.5f, 0, 0}, dt, s)) > 0.0f && !f.Jumped(), "then followed at the speed cap");
    Require(Length(f.Update({0.2f, 0, 0}, 0.0f, s)) == 0.0f && Length(f.Update({0.2f, 0, 0}, 0.5f, s)) == 0.0f,
            "no move without a sane frame time");
    Require(Length(f.Update({NAN, 0, 0}, dt, s)) == 0.0f && !f.Following(), "non-finite input resets");
}

// Distance along a ray to the plane y = wallY (engine), or -1.
float HitWallY(const Ray& ray, float wallY)
{
    const Vec3 d = Sub(ray.to, ray.from);
    if (std::fabs(d.y) < 1e-6f) { return -1.0f; }
    const float t = (wallY - ray.from.y) / d.y;
    return t >= 0.0f && t <= 1.0f ? t * Length(d) : -1.0f;
}

void TestSweep()
{
    Settings s;
    Rays rays{};
    const Vec3 feet{0, 0, 0};
    Require(!SweepRays(feet, {0, 0, 0}, s, rays) && !SweepRays(feet, {0, 0.1f, 0.1f}, s, rays),
            "nothing to sweep for no move or a vertical one");
    Require(SweepRays(feet, {0, 0.3f, 0}, s, rays), "a forward move sweeps");
    for (const Ray& r : rays) {
        Require(r.from.z >= 0.5f && std::fabs(r.from.z - r.to.z) < 1e-6f, "rays are level, above a low step");
        Require(std::fabs(Length(Sub(r.to, r.from)) - (r.lead + 0.3f + s.skin)) < 1e-5f, "each reaches past the move");
    }
    // A wall 0.5 m ahead of the centre: the capsule's front stops a skin short.
    std::array<float, kRays> hits{};
    for (std::size_t i = 0; i < kRays; ++i) { hits[i] = HitWallY(rays[i], 0.5f); }
    Require(std::fabs(AllowedLength(0.3f, rays, hits, s) - (0.5f - s.radius - s.skin)) < 1e-4f, "stops at the wall");
    for (std::size_t i = 0; i < kRays; ++i) { hits[i] = HitWallY(rays[i], 5.0f); }
    Require(AllowedLength(0.3f, rays, hits, s) == 0.3f, "a far wall does not shorten the move");
    // A post in front of the right side only (+X is right of a +Y move).
    for (std::size_t i = 0; i < kRays; ++i) { hits[i] = rays[i].from.x > 0.1f ? 0.2f : -1.0f; }
    Require(std::fabs(AllowedLength(0.3f, rays, hits, s) - (0.2f - s.skin)) < 1e-4f, "a post at the side stops it too");
    // Already touching: no further, but never negative.
    for (std::size_t i = 0; i < kRays; ++i) { hits[i] = rays[i].lead > 0.3f ? 0.1f : -1.0f; }
    Require(AllowedLength(0.3f, rays, hits, s) == 0.0f, "touching: no move");
    hits.fill(-1.0f);
    hits[4] = NAN;
    Require(AllowedLength(0.3f, rays, hits, s) == 0.0f, "a broken hit refuses the move");
}

} // namespace

int main()
{
    TestShiftCancelsTheMove();
    TestWalkingNeverMovesTheView();
    TestNativeMovesAreNotAbsorbed();
    TestDeadzoneSpeedAndJumps();
    TestSweep();
    std::cout << "room_scale: all tests passed\n";
    return 0;
}
