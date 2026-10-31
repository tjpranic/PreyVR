#pragma once

#include <cmath>

// World motion vectors in the second eye (the second known issue in
// docs/TEMPORAL-AA-SCOPE-2026-10-30.md).
//
// Both eyes draw one render view, and its objects compile once, on the first
// eye's submission. Each object's constants hold its previous transform W in the
// first eye's previous view space, W x V1 (0xF001D0 into 0xFD4570). The velocity
// shader multiplies that by CV_PrevViewProjMatr, view-info +0x220, the projection
// P alone. The second eye wants W x V2 x P and gets W x V1 x P, so an object at
// rest has a motion vector the size of its stereo disparity, and temporal AA
// reads history that far away.
//
// In Prey's row-vector layout (translation in row 3),
//     W x V1 x (V1^-1 x V2 x P) = W x V2 x P,
// so moving the second eye's +0x220 to V1^-1 x V2 x P corrects every object with
// one matrix a submission. First-person objects go through +0x260 instead, and
// their constants drop the view's translation (render/taa/NearMotion.h).
namespace preyvr::worldmotion {

// A view: finite, column 3 is (0, 0, 0, 1), and a rotation of determinant +-1.
inline bool LooksLikeView(const float* m)
{
    for (int i = 0; i < 16; ++i) {
        if (!std::isfinite(m[i])) { return false; }
    }
    if (std::fabs(m[3]) > 1e-4f || std::fabs(m[7]) > 1e-4f || std::fabs(m[11]) > 1e-4f ||
        std::fabs(m[15] - 1.0f) > 1e-4f) {
        return false;
    }
    const double determinant = static_cast<double>(m[0]) * (static_cast<double>(m[5]) * m[10] - static_cast<double>(m[6]) * m[9]) -
                               static_cast<double>(m[1]) * (static_cast<double>(m[4]) * m[10] - static_cast<double>(m[6]) * m[8]) +
                               static_cast<double>(m[2]) * (static_cast<double>(m[4]) * m[9] - static_cast<double>(m[5]) * m[8]);
    return std::fabs(std::fabs(determinant) - 1.0) < 1e-2;
}

// V1^-1 x V2, or false unless both are views. In doubles, and with the true inverse
// of V1's float rotation rather than its transpose: a view's translation is the
// camera's distance from the world's origin, and a transpose leaves that distance
// times the rotation's rounding, about half a pixel at 1 m from 2.5 km out.
inline bool Between(const float* first, const float* second, float* out)
{
    if (!LooksLikeView(first) || !LooksLikeView(second)) { return false; }
    const double a = first[0], b = first[1], c = first[2];
    const double d = first[4], e = first[5], f = first[6];
    const double g = first[8], h = first[9], i = first[10];
    const double ei = e * i - f * h, fg = f * g - d * i, dh = d * h - e * g;
    const double determinant = a * ei + b * fg + c * dh;
    const double inverse[3][3] = {{ei / determinant, (c * h - b * i) / determinant, (b * f - c * e) / determinant},
                                  {fg / determinant, (a * i - c * g) / determinant, (c * d - a * f) / determinant},
                                  {dh / determinant, (b * g - a * h) / determinant, (a * e - b * d) / determinant}};
    double rotation[3][3]{};
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column) {
            for (int k = 0; k < 3; ++k) { rotation[row][column] += inverse[row][k] * second[4 * k + column]; }
            out[4 * row + column] = static_cast<float>(rotation[row][column]);
        }
        out[4 * row + 3] = 0.0f;
    }
    for (int column = 0; column < 3; ++column) {
        const double moved = first[12] * rotation[0][column] + first[13] * rotation[1][column] + first[14] * rotation[2][column];
        out[12 + column] = static_cast<float>(second[12 + column] - moved);
    }
    out[15] = 1.0f;
    return true;
}

// `relative` x `projection`, what the second eye's +0x220 becomes.
inline void Follow(const float* relative, const float* projection, float* out)
{
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            double sum = 0.0;
            for (int k = 0; k < 4; ++k) { sum += static_cast<double>(relative[4 * row + k]) * projection[4 * k + column]; }
            out[4 * row + column] = static_cast<float>(sum);
        }
    }
}

// How far apart the two views are, metres, from Between's result.
inline double Separation(const float* relative)
{
    return std::sqrt(static_cast<double>(relative[12]) * relative[12] + static_cast<double>(relative[13]) * relative[13] +
                     static_cast<double>(relative[14]) * relative[14]);
}

} // namespace preyvr::worldmotion
