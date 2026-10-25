// The starfield's far block (preyvr/StarfieldDepth.h) and the geometry it is
// for: ArkStarsVS as Stars.cfx draws a star, through _pos_Common -- the sky
// object's position less the eye's, then a projection whose depth is the far
// plane -- against the eyes of a both-eyes frame.
#include "preyvr/NativeContract.h"
#include "preyvr/StarfieldDepth.h"
#include "preyvr/StarfieldNative.h"
#include "TestSupport.h"

#include <cmath>
#include <cstring>
#include <iostream>

using namespace preyvr;
using namespace preyvr::starfield;
using preyvr::test::Require;
namespace sn = preyvr::starfield::native;

namespace {

struct Vec3 { double x, y, z; };
Vec3 Add(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 Sub(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 Mul(Vec3 a, double s) { return {a.x * s, a.y * s, a.z * s}; }
double Dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
double Length(Vec3 a) { return std::sqrt(Dot(a, a)); }
double AngleDegrees(Vec3 a, Vec3 b)
{
    const double c = Dot(a, b) / (Length(a) * Length(b));
    return std::acos(c > 1 ? 1 : c < -1 ? -1 : c) * 180.0 / 3.14159265358979323846;
}

float BlockFloat(const StarBlock& block, std::size_t offset)
{
    float value = 0;
    std::memcpy(&value, block.data() + offset, sizeof(value));
    return value;
}
void PutFloat(StarBlock& block, std::size_t offset, float value) { std::memcpy(block.data() + offset, &value, sizeof(value)); }

// A block as 0xBB720 leaves it: a smart pointer, StarRotation (here a turn of
// 30 degrees about z, then 20 about x), the intensity, size and share of stars.
StarBlock SampleBlock()
{
    StarBlock block{};
    for (std::size_t i = 0; i < 8; ++i) { block[i] = static_cast<std::uint8_t>(0xA0 + i); }
    const double a = 30 * 3.14159265358979323846 / 180, b = 20 * 3.14159265358979323846 / 180;
    const double r[9] = {std::cos(a), -std::sin(a), 0,
                         std::sin(a) * std::cos(b), std::cos(a) * std::cos(b), -std::sin(b),
                         std::sin(a) * std::sin(b), std::cos(a) * std::sin(b), std::cos(b)};
    for (std::size_t i = 0; i < 9; ++i) { PutFloat(block, sn::BlockRotation + 4 * i, static_cast<float>(r[i])); }
    PutFloat(block, 0x2C, 0.7f);
    PutFloat(block, 0x30, 0.0025f);
    PutFloat(block, 0x34, 0.9f);
    return block;
}

// mul((float3x3)StarRotation, pos.xyz): the block's rows, as the draw sets them.
Vec3 Rotate(const StarBlock& block, Vec3 v)
{
    const auto row = [&](std::size_t r) {
        return Vec3{BlockFloat(block, sn::BlockRotation + 12 * r), BlockFloat(block, sn::BlockRotation + 12 * r + 4),
                    BlockFloat(block, sn::BlockRotation + 12 * r + 8)};
    };
    return {Dot(row(0), v), Dot(row(1), v), Dot(row(2), v)};
}

// Where the eye sees the star: _pos_Common puts it at the rotated position plus
// the sky object's position (the head the frame was culled from) less the eye's.
Vec3 Seen(const StarBlock& block, Vec3 star, Vec3 head, Vec3 eye) { return Add(Rotate(block, star), Sub(head, eye)); }

// A symmetric projection looking along +y (Prey's forward), x right, z up; the
// clip w is the depth. The quad's corner is added in clip space times w, as
// ArkStarsVS does, and z is w (xyww).
struct Ndc { double x, y, z; };
Ndc Project(Vec3 view, double cornerX, double size)
{
    const double p00 = 1.0 / std::tan(50 * 3.14159265358979323846 / 180), p11 = p00 * 0.95;
    const double w = view.y;
    const double cx = view.x * p00 + cornerX * size * w, cy = view.z * p11;
    return {cx / w, cy / w, w / w};
}

void TestTable()
{
    for (const auto& a : sn::Contracts) {
        unsigned seen = 0;
        for (const auto& b : sn::Contracts) { seen += a.rva == b.rva; }
        Require(seen == 1 && a.size > 0, "each body is one row");
    }
    Require(contract::Gated(sn::Contracts, sn::ArkStarsDraw), "the hooked draw is gated");
    // The fields the draw reads: element +0x40, +0x48, +0x50 and +0x54 (4 bytes);
    // block +0x08..+0x37.
    Require(sn::ElementStarBlock == 0x40 && sn::ElementSize >= 0x54 + 4, "the element copy holds every field read");
    Require(sn::BlockRotation == 0x08 && sn::BlockRotationFloats == 9 && sn::BlockSize >= 0x34 + 4,
            "the block copy holds every field read");
}

void TestFarBlock()
{
    const StarBlock block = SampleBlock();
    const StarBlock pushed = FarStarBlock(block);
    for (std::size_t i = 0; i < sn::BlockSize; ++i) {
        const bool rotation = i >= sn::BlockRotation && i < sn::BlockRotation + 4 * sn::BlockRotationFloats;
        if (!rotation) { Require(pushed[i] == block[i], "every byte but the rotation is the block's own"); }
    }
    for (std::size_t i = 0; i < sn::BlockRotationFloats; ++i) {
        const std::size_t at = sn::BlockRotation + 4 * i;
        Require(BlockFloat(pushed, at) == BlockFloat(block, at) * kStarDistanceMetres, "the rotation, scaled to the distance");
    }
    Require(FarStarBlock(block, 1.0f) == block, "a distance of 1 is Prey's block");
}

void TestGeometry()
{
    // A both-eyes frame (DualSceneStereo mode 3): culled from the head, each eye
    // half a 69 mm IPD to its side along the head's right (+x).
    const Vec3 head{12.0, -40.0, 3.5}, right{1, 0, 0};
    const double halfIpd = 0.069 / 2;
    const Vec3 left = Sub(head, Mul(right, halfIpd)), rightEye = Add(head, Mul(right, halfIpd));
    const StarBlock block = SampleBlock(), pushed = FarStarBlock(block);
    // Stars ahead, a little to each side, above: unit vectors as stars.dat gives them.
    const Vec3 stars[] = {{0, 1, 0}, {0.3, 0.95, 0.1}, {-0.4, 0.9, -0.2}, {0.1, 0.8, 0.59}};
    double worstNative = 0, worstFar = 0;
    for (Vec3 star : stars) {
        star = Mul(star, 1.0 / Length(star));
        const Vec3 truth = Rotate(block, star);   // the direction a star at infinity is in
        if (truth.y < 0.2) { continue; }   // only stars in view
        const double nativePair = AngleDegrees(Seen(block, star, head, left), Seen(block, star, head, rightEye));
        const double farPair = AngleDegrees(Seen(pushed, star, head, left), Seen(pushed, star, head, rightEye));
        worstNative = nativePair > worstNative ? nativePair : worstNative;
        worstFar = farPair > worstFar ? farPair : worstFar;
        Require(AngleDegrees(Seen(pushed, star, head, left), truth) < 1e-3, "the left eye sees the star where it is");
        Require(AngleDegrees(Seen(pushed, star, head, rightEye), truth) < 1e-3, "and so does the right");
        // A flat frame (the eye is the head): the same pixels, the same size and
        // the far plane either way.
        for (double corner : {-1.0, 1.0}) {
            const Ndc a = Project(Seen(block, star, head, head), corner, 0.0025);
            const Ndc b = Project(Seen(pushed, star, head, head), corner, 0.0025);
            Require(std::fabs(a.x - b.x) < 1e-6 && std::fabs(a.y - b.y) < 1e-6 && a.z == 1 && b.z == 1,
                    "flat, the far block draws the same pixels");
        }
    }
    // The bug: IPD / 1 m between the eyes, about 4 degrees. The fix: 0.0004.
    Require(worstNative > 3.5, "Prey's own block puts the stars 1 m from the head in a both-eyes frame");
    Require(worstFar < 1e-3, "the far block leaves the eyes in agreement");
}

} // namespace

int main()
{
    TestTable();
    TestFarBlock();
    TestGeometry();
    std::cout << "starfield_depth: the stars 10 km out, the same in a flat frame, together in both eyes\n";
}
