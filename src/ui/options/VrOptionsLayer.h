#pragma once
#include "xr/LayerSwapchain.h"
#include "ui/options/VrOptionsUi.h"
#include "ui/screens/UiPanel.h"

#include <cstdint>
#include <memory>
#include <optional>

namespace preyvr::dll {
// **The VR Options panel's composition layer** (XrSessionHost; XR frame thread
// only). Each frame the panel is open, it:
//  - places it from the shared menu anchor straight ahead, in front of the game
//    menu (ui::kOptionsDepthRatio of ui.distance, which it follows), or where
//    it was dragged this session, relative to that anchor (ui::PanelPlacement)
//    -- never by ui.offset, which is the game menus';
//  - moves it with the laser while it is held (VrOptionsGrab, ui::DragPanel);
//  - draws it with VrOptionsUi into its own swapchain, and yields the quad.
// Its size is its own (scale 1, margin 0.68): the same angle at any ui.distance,
// and once dragged, the physical size it was dragged at, wherever it is put. A
// recenter re-places it and drops a grab in progress.
class VrOptionsLayer {
public:
    struct Frame {
        bool active = false;       // placed and drawn: submit `layer`
        bool sessionFault = false; // a swapchain wait or release failed: the session must stop
        XrCompositionLayerQuad layer{XR_TYPE_COMPOSITION_LAYER_QUAD};
        ui::Panel surface{};       // where the laser meets it
    };
    // Placed this opening; until then, the next Update anchors it afresh.
    bool Placed() const { return panel_.has_value(); }
    // `menuDistance`: the game menu's, metres (ui.distance).
    Frame Update(XrSession session, XrSpace space, ID3D11Device* device, const ui::MenuAnchor& anchor,
                 float menuDistance, std::uint64_t reference, const Pose& head, XrTime displayTime);
    // Closed: this opening's place goes. One let go of -- or closed -- while
    // held is kept for the session, relative to `anchor`.
    void Close(const ui::MenuAnchor* anchor);
    // XR teardown: the swapchain, the ImGui context and its D3D objects, and the
    // session's placement.
    void Reset();

private:
    void Drag(const ui::MenuAnchor& anchor, const Pose& head, XrTime displayTime);
    // Where it is and its size, kept for the session, relative to `anchor`.
    void Keep(const ui::MenuAnchor& anchor);

    DXGI_FORMAT format_ = DXGI_FORMAT_UNKNOWN;
    std::unique_ptr<LayerSwapchain> swapchain_;
    std::unique_ptr<VrOptionsUi> ui_;
    unsigned controlsGeneration_ = 0;   // the bindings the Controls tab shows (XrInputBoundControls)
    std::optional<ui::Panel> panel_;
    std::uint64_t reference_ = 0;
    float menuDistance_ = 0; // the ui.distance it was placed in front of
    // The grab, the press it belongs to (VrOptionsGrab::serial), and the last
    // frame's time, for the stick's push.
    std::optional<ui::PanelGrab> grab_;
    unsigned grabSerial_ = 0;
    XrTime dragTime_ = 0;
    std::optional<ui::PanelPlacement> placement_;
    float placedWidth_ = 0, placedHeight_ = 0;
};
} // namespace preyvr::dll
