#pragma once

#include <cmath>

// First-person motion vectors under the near stereo (the known issue in
// docs/TEMPORAL-AA-SCOPE-2026-10-30.md).
//
// Prey writes a motion vector for every FOB_NEAREST pixel: its position through
// the near view-projection (view-info +0xA0) minus its previous position through
// CV_PrevViewProjNearestMatr (view-info +0x260, the near projection alone) times
// the object's previous transform in view space (0xFD4570: the renderer's
// previous view, its translation dropped). NearViewStereo moves the near VP by
// each eye's displacement `d`. Unless the previous projection moves with it,
// every near pixel's motion points half its stereo disparity away, and temporal
// AA blends in whatever is there.
//
// In Prey's row-vector layout (translation in row 3) the near VP is the
// translation-free view V times the near projection P. Moving it by `d`,
// Translation(-d) x V x P, is moving P by e = d x V in view space,
// V x Translation(-e) x P, since V's column 3 is (0, 0, 0, 1). Both are the same
// change to row 3: -(d.x row0 + d.y row1 + d.z row2) of the unedited near VP.
namespace preyvr::nearmotion {

// Row 3 of a row-vector matrix moved by -d against the rows 0..2 of `axes`:
// row[j] - d.x axes[0][j] - d.y axes[1][j] - d.z axes[2][j], in that order.
inline void ShiftTranslationRow(float* row, const float* axes, float dx, float dy, float dz)
{
    for (int j = 0; j < 4; ++j) {
        row[j] = row[j] - dx * axes[0 + j] - dy * axes[4 + j] - dz * axes[8 + j];
    }
}

// A perspective projection in that layout: all finite, and w the view's depth
// (column 3 is (0, 0, +-1, 0)). The previous near projection is left alone
// unless it is one.
inline bool LooksPerspective(const float* m)
{
    for (int i = 0; i < 16; ++i) {
        if (!std::isfinite(m[i])) { return false; }
    }
    return std::fabs(m[3]) < 1e-4f && std::fabs(m[7]) < 1e-4f && std::fabs(std::fabs(m[11]) - 1.0f) < 1e-3f &&
           std::fabs(m[15]) < 1e-4f;
}

} // namespace preyvr::nearmotion
