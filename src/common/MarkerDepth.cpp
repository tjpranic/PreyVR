#include "preyvr/MarkerDepth.h"

#include <cmath>

namespace preyvr::markers {

bool ScaleOf(const HudMatrix& m, HudScale& out)
{
    for (float v : m) {
        if (!std::isfinite(v)) {
            return false;
        }
    }
    const float p00 = std::sqrt(m[0] * m[0] + m[4] * m[4] + m[8] * m[8]);
    const float p11 = std::sqrt(m[1] * m[1] + m[5] * m[5] + m[9] * m[9]);
    const float forward = std::sqrt(m[3] * m[3] + m[7] * m[7] + m[11] * m[11]);
    // A perspective's scales are positive and finite; the depth row is a unit axis.
    if (!(p00 > 1e-3f && p00 < 1e3f && p11 > 1e-3f && p11 < 1e3f) || std::fabs(forward - 1.0f) > 0.01f) {
        return false;
    }
    out = {p00, p11};
    return true;
}

Vec3 ViewPoint(const HudMatrix& m, const HudScale& scale, Vec3 world)
{
    const float x = m[0] * world.x + m[4] * world.y + m[8] * world.z + m[12];
    const float y = m[1] * world.x + m[5] * world.y + m[9] * world.z + m[13];
    const float w = m[3] * world.x + m[7] * world.y + m[11] * world.z + m[15];
    return {x / scale.p00, y / scale.p11, w};
}

bool DisplayedDepth(const HudScale& scale, Vec3 view, float nx, float ny, float& depth)
{
    const float distance = Length(view);
    if (!std::isfinite(distance) || !std::isfinite(nx) || !std::isfinite(ny) || scale.p00 <= 0 || scale.p11 <= 0) {
        return false;
    }
    // The tangents of the direction the marker is drawn in.
    const float tx = (2.0f * nx - 1.0f) / scale.p00;
    const float ty = (1.0f - 2.0f * ny) / scale.p11;
    const float along = distance / std::sqrt(1.0f + tx * tx + ty * ty);
    depth = along > kNearestMetres ? along : kNearestMetres;
    return true;
}

float CanvasShiftPerMetre(const HudScale& scale, float depth, float canvasPerUnit)
{
    // An eye `o` metres right of the head sees the point `o` further left:
    // nx moves by -o * P00 / (2 * depth).
    return -scale.p00 * canvasPerUnit / (2.0f * depth);
}

} // namespace preyvr::markers
