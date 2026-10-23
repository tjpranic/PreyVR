#include "preyvr/InventoryDepth.h"

#include <cmath>

namespace preyvr::inventory {

bool ApplyStereoParallax(std::array<float,16>& clip, float cameraUnits,
                         float panelWidth, float eyeOnPanel, float depthScale)
{
    if (!std::isfinite(cameraUnits) || cameraUnits <= 1.f ||
        !std::isfinite(panelWidth) || panelWidth < .1f || panelWidth > 20.f ||
        !std::isfinite(eyeOnPanel) || std::abs(eyeOnPanel) > .1f ||
        !std::isfinite(depthScale) || depthScale < 0.f || depthScale > 1.f) return false;
    for (float v:clip) if (!std::isfinite(v)) return false;
    if (depthScale==0.f || eyeOnPanel==0.f) return true;
    auto result=clip;
    const float factor=2.f*eyeOnPanel/panelWidth*depthScale;
    for (unsigned i=0;i<4;++i)
        result[i]+=factor*(clip[12+i]-(i==3?cameraUnits:0.f));
    for (float v:result) if (!std::isfinite(v)) return false;
    clip=result;
    return true;
}

} // namespace preyvr::inventory
