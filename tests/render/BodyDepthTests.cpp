// The body drawn in the world (render/stereo/BodyDepth.h): which slot renders are
// taken out of the near pass, the words they read cleared of exactly the near
// bits, and those bits, and only those, put back after the render.
#include "render/stereo/BodyDepth.h"
#include "render/stereo/BodyDepthNative.h"
#include "engine/NativeContract.h"
#include "render/stereo/NearParticles.h"
#include "TestSupport.h"

#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <string>

using namespace preyvr;
using namespace preyvr::bodydepth;
using preyvr::test::Require;
namespace bn = preyvr::bodydepth::native;

static_assert(bn::ObjectNearest == nearfx::kObjectNearest, "FOB_NEAREST is one bit, the near particles' too");
static_assert(bn::ObjectNearest == 1u << 23, "FOB_NEAREST is bit 23 (RenderCHR: bt rax,0x17)");
static_assert(bn::SlotRenderNearest == 1u << 1, "ENTITY_SLOT_RENDER_NEAREST is bit 1 (shr r12b,1; and r12b,1)");
static_assert(engine::CharacterInstanceLayout::drawNear == 0x2, "CS_FLAG_DRAW_NEAR (test byte [+0xAC8],2)");
static_assert(engine::PassInfoLayout::generalPassFlags == 0x2E5DF, "CSystem::Render's mov r8d,0x2E5DF");
static_assert(contract::Gated(bn::Contracts, bn::SlotRender), "the hooked slot render is the gated one");

namespace {

constexpr std::uintptr_t kPlayer = 0x1000, kOther = 0x2000;
constexpr std::uint32_t kGeneral = engine::PassInfoLayout::generalPassFlags;

// The player's slot as Prey leaves it: rendered (bit 0) and nearest, its object
// flags with FOB_NEAREST and others, the params' flags without it, and the
// character drawing its model (CS_FLAG_DRAW_MODEL, bit 0).
NearState PlayerSlot()
{
    NearState s{};
    s.slotFlags = 0x0003;
    s.slotObjectFlags = bn::ObjectNearest | 0x4000007u;
    s.paramsObjectFlags = 0x2000000000000100ull;
    s.characterFlags = 0x1;
    return s;
}

void TestDecide()
{
    Require(Decide(true, kPlayer, kPlayer, kGeneral) == Decision::world, "the player's slot in the main view");
    Require(Decide(false, kPlayer, kPlayer, kGeneral) == Decision::native, "off, every slot is Prey's");
    Require(Decide(true, kOther, kPlayer, kGeneral) == Decision::notPlayer, "another character's slot is Prey's");
    Require(Decide(true, 0, 0, kGeneral) == Decision::notPlayer, "no player known: nothing is taken");
    Require(Decide(true, kPlayer, 0, kGeneral) == Decision::notPlayer, "nor is a slot, with no player known");
    Require(Decide(true, kPlayer, kPlayer, 0x1234) == Decision::otherPass, "a shadow or other pass keeps the near pass");
    Require(Decide(true, kPlayer, kPlayer, kGeneral | 1u << 31) == Decision::otherPass, "the flags must be the main view's");
    Require(NearSlot(0x0002) && NearSlot(0x0003) && !NearSlot(0x0001) && !NearSlot(0xFFFD), "the near bit alone");
}

void TestInWorld()
{
    const NearState before = PlayerSlot();
    const NearState world = InWorld(before);
    Require(world.slotFlags == 0x0001, "the slot keeps ENTITY_SLOT_RENDER, loses only the nearest bit");
    Require(world.slotObjectFlags == 0x4000007u, "the object flags lose only FOB_NEAREST");
    Require(world.paramsObjectFlags == before.paramsObjectFlags, "params without FOB_NEAREST are left as they are");
    Require(world.characterFlags == 0x1, "the character keeps drawing its model");

    NearState all = before;
    all.paramsObjectFlags |= bn::ObjectNearest;
    all.characterFlags |= engine::CharacterInstanceLayout::drawNear;
    all.slotFlags = 0xFFFF;
    all.slotObjectFlags = 0xFFFFFFFFu;
    const NearState cleared = InWorld(all);
    Require(cleared.slotFlags == 0xFFFD && cleared.slotObjectFlags == 0xFF7FFFFFu, "every other slot bit kept");
    Require(cleared.paramsObjectFlags == (all.paramsObjectFlags & ~std::uint64_t{bn::ObjectNearest}),
            "the params' FOB_NEAREST cleared when they carry it");
    Require(cleared.characterFlags == 0x1, "CS_FLAG_DRAW_NEAR cleared when the character carries it");
    Require(!NearSlot(cleared.slotFlags), "nothing left that reads as near");
}

void TestRestored()
{
    const NearState before = PlayerSlot();
    // The render wrote meanwhile: a slot bit of its own and the params' flags
    // put back to the incoming value (0x9748A7).
    NearState now = InWorld(before);
    now.slotFlags |= 0x0100;
    now.paramsObjectFlags = before.paramsObjectFlags;
    const NearState back = Restored(now, before);
    Require(back.slotFlags == 0x0103, "the nearest bit back, the render's own bit kept");
    Require(back.slotObjectFlags == before.slotObjectFlags, "FOB_NEAREST back on the slot's object flags");
    Require(back.paramsObjectFlags == before.paramsObjectFlags, "params left without FOB_NEAREST, as they came");
    Require(back.characterFlags == before.characterFlags, "the character as it was");

    NearState all = before;
    all.paramsObjectFlags |= bn::ObjectNearest;
    all.characterFlags |= engine::CharacterInstanceLayout::drawNear;
    const NearState round = Restored(InWorld(all), all);
    Require(round.slotFlags == all.slotFlags && round.slotObjectFlags == all.slotObjectFlags &&
                round.paramsObjectFlags == all.paramsObjectFlags && round.characterFlags == all.characterFlags,
            "cleared then restored is what it was");

    // A bit that was clear before is never set.
    NearState plain{};
    plain.slotFlags = 0x0001;
    const NearState untouched = Restored(plain, plain);
    Require(untouched.slotFlags == 0x0001 && untouched.slotObjectFlags == 0 && untouched.paramsObjectFlags == 0 &&
                untouched.characterFlags == 0,
            "nothing added that was not there");
}

// --- the near pass at the world's depth ---------------------------------------------

using Matrix = std::array<float, 16>;   // row-major, row vectors, translation in row 3

Matrix Multiply(const Matrix& a, const Matrix& b)
{
    Matrix m{};
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            for (int k = 0; k < 4; ++k) { m[4 * i + j] += a[4 * i + k] * b[4 * k + j]; }
        }
    }
    return m;
}

// An off-centre perspective projection; reverse depth maps `n` to 1 and `f` to 0.
Matrix Projection(float n, float f, bool reverse)
{
    const float a = reverse ? -n / (f - n) : f / (f - n);
    const float b = reverse ? n * f / (f - n) : -n * f / (f - n);
    return {1.2f, 0, 0, 0, 0, 1.1f, 0, 0, 0.08f, -0.05f, a, 1, 0, 0, b, 0};
}

// A view: rotated about two axes, at `t` (row 3).
Matrix View(float tx, float ty, float tz)
{
    const float c = std::cos(0.4f), s = std::sin(0.4f), c2 = std::cos(-0.25f), s2 = std::sin(-0.25f);
    const Matrix yaw{c, 0, -s, 0, 0, 1, 0, 0, s, 0, c, 0, 0, 0, 0, 1};
    const Matrix pitch{1, 0, 0, 0, 0, c2, s2, 0, 0, -s2, c2, 0, 0, 0, 0, 1};
    Matrix v = Multiply(yaw, pitch);
    v[12] = tx; v[13] = ty; v[14] = tz;
    return v;
}

// A point through `m`: its clip coordinates.
std::array<float, 4> Clip(const Matrix& m, float x, float y, float z)
{
    std::array<float, 4> c{};
    for (int j = 0; j < 4; ++j) { c[j] = x * m[j] + y * m[4 + j] + z * m[8 + j] + m[12 + j]; }
    return c;
}

bool Close(float a, float b, float tolerance) { return std::fabs(a - b) <= tolerance; }

void TestModes()
{
    Require(static_cast<unsigned>(Mode::native) == 0 && static_cast<unsigned>(Mode::nearWorldDepth) == 1 &&
                static_cast<unsigned>(Mode::world) == 2,
            "body.depth 0, 1, 2");
    Require(std::string(ModeName(Mode::native)) == "native" && std::string(ModeName(Mode::nearWorldDepth)) == "near" &&
                std::string(ModeName(Mode::world)) == "world",
            "the modes' report names");
}

void TestDepthMap()
{
    // The near pass's projection (0.03 to 40 m) and the world's (0.03 to 1024 m),
    // both from the same eye, translation-free as the near VP is.
    const Matrix view = View(0, 0, 0);
    const Matrix nearVp = Multiply(view, Projection(0.03f, 40.0f, true));
    const Matrix worldZero = Multiply(view, Projection(0.03f, 1024.0f, true));
    DepthMap nearMap{}, worldMap{};
    Require(ReadDepthMap(nearVp.data(), nearMap) && Close(nearMap.a, -0.03f / 39.97f, 1e-7f) &&
                Close(nearMap.b, 0.03f * 40.0f / 39.97f, 1e-6f),
            "the near pass's depth read from its view-projection");
    Require(ReadDepthMap(worldZero.data(), worldMap), "and the world's");
    Require(!SameDepthMap(nearMap, worldMap) && SameDepthMap(nearMap, nearMap) && SameDepthMap(worldMap, worldMap),
            "the two told apart, each the same as itself");
    float n = 0, f = 0;
    DepthPlanes(nearMap, n, f);
    Require(Close(n, 0.03f, 1e-5f) && Close(f, 40.0f, 0.01f), "the near pass's planes, 0.03 and 40 m");
    DepthPlanes(worldMap, n, f);
    Require(Close(n, 0.03f, 1e-5f) && Close(f, 1024.0f, 1.0f), "the world's, 0.03 and 1024 m");

    // The world's VP with the camera's translation reads the same depth.
    const Matrix worldVp = Multiply(View(105.0f, -42.0f, 31.0f), Projection(0.03f, 1024.0f, true));
    DepthMap translated{};
    Require(ReadDepthMap(worldVp.data(), translated) && SameDepthMap(translated, worldMap),
            "a translation does not change the depth read");
    // Without reverse depth too.
    DepthMap forward{};
    Require(ReadDepthMap(Multiply(view, Projection(0.05f, 300.0f, false)).data(), forward), "forward depth reads");
    DepthPlanes(forward, n, f);
    Require(Close(n, 0.05f, 1e-5f) && Close(f, 300.0f, 0.1f), "and its planes come out the same way round");

    // The near VP given the world's depth: every point lands at the world's depth,
    // where the near pass already drew it (x, y, w unchanged).
    Matrix remapped = nearVp;
    WriteDepthMap(remapped.data(), worldMap);
    const float points[][3] = {{0.1f, -0.3f, 0.4f}, {-0.2f, 0.1f, 0.8f}, {0.05f, 0.02f, 0.06f}, {3, 1, -12}};
    for (const auto& p : points) {
        const auto before = Clip(nearVp, p[0], p[1], p[2]);
        const auto after = Clip(remapped, p[0], p[1], p[2]);
        const auto world = Clip(worldZero, p[0], p[1], p[2]);
        Require(after[0] == before[0] && after[1] == before[1] && after[3] == before[3], "x, y and w untouched");
        Require(Close(after[2] / after[3], world[2] / world[3], 1e-6f), "the depth is the world's");
    }
    {
        // At arm's length the near pass's own depth was about 1% off the world's.
        const auto p = points[0];
        const auto before = Clip(nearVp, p[0], p[1], p[2]), world = Clip(worldZero, p[0], p[1], p[2]);
        Require(!Close(before[2] / before[3], world[2] / world[3], 1e-6f), "and was not before");
    }
    DepthMap again{};
    Require(ReadDepthMap(remapped.data(), again) && SameDepthMap(again, worldMap), "rewritten, it reads as the world's");
    Matrix twice = remapped;
    WriteDepthMap(twice.data(), worldMap);
    Require(twice == remapped, "rewriting it again changes nothing");

    // NearViewStereo's eye offset after it (row 3 less d times rows 0..2) keeps
    // the world's depth: the offset matrix still reads as the world's, so an
    // already-edited buffer reads back the same.
    Matrix offset = remapped;
    const float d[3] = {0.032f, 0.0f, -0.001f};
    for (int j = 0; j < 4; ++j) {
        offset[12 + j] = remapped[12 + j] - d[0] * remapped[j] - d[1] * remapped[4 + j] - d[2] * remapped[8 + j];
    }
    DepthMap offsetMap{};
    Require(ReadDepthMap(offset.data(), offsetMap) && SameDepthMap(offsetMap, worldMap), "the eye offset keeps it");
    Matrix offsetAgain = offset;
    WriteDepthMap(offsetAgain.data(), worldMap);
    for (int i = 0; i < 16; ++i) {
        Require(Close(offsetAgain[i], offset[i], 1e-7f), "and rewriting the offset matrix is a no-op");
    }

    // Nothing is read from a matrix that is not a perspective view-projection.
    DepthMap refused{};
    const Matrix ortho{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0.01f, 0, 0, 0, 0.5f, 1};
    Require(!ReadDepthMap(ortho.data(), refused), "an orthographic projection (no w column) is refused");
    Matrix bent = nearVp;
    bent[2] += 0.25f;
    Require(!ReadDepthMap(bent.data(), refused), "a depth column with more in it than depth is refused");
    Matrix flat = nearVp;
    flat[14] = flat[15] * nearMap.a;   // b = 0
    Require(!ReadDepthMap(flat.data(), refused), "a depth that does not depend on distance is refused");
    Matrix broken = nearVp;
    broken[5] = std::numeric_limits<float>::quiet_NaN();
    broken[7] = std::numeric_limits<float>::quiet_NaN();
    Require(!ReadDepthMap(broken.data(), refused), "a non-finite matrix is refused");
    Require(refused.a == 0.0f && refused.b == 0.0f, "and nothing is written out");
}

} // namespace

int main()
{
    TestDecide();
    TestInWorld();
    TestRestored();
    TestModes();
    TestDepthMap();
    std::cout << "body_depth: the player's slot in the main view drawn without the near bits, and they come back; "
                 "the near pass's depth rebuilt as the world's\n";
    return 0;
}
