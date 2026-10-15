#pragma once
#include "InventorySwapchain.h"
#include "VrOptionsUi.h"
#include "preyvr/UiPanel.h"

#include <cstdint>
#include <memory>
#include <optional>

namespace preyvr::dll {
// **The VR Options panel's composition layer** (XrSessionHost; XR frame thread
// only). Each frame the panel is open, it:
//  - places it from the shared menu anchor, 2 m straight ahead, or where it
//    was dragged this session, relative to that anchor (ui::PanelPlacement) --
//    never by ui.distance or ui.offset, which are the game menus';
//  - moves it with the laser while it is held (VrOptionsGrab, ui::DragPanel);
//  - draws it with VrOptionsUi into its own swapchain, and yields the quad.
// Its size is its own (scale 1, margin 0.68, fitted at 2 m) and stays its
// physical size wherever it is put. A recenter re-places it and drops a grab in
// progress.
class VrOptionsLayer {
public:
    struct Frame {
        bool active = false;       // placed and drawn: submit `layer`
        bool sessionFault = false; // a swapchain wait or release failed: the session must stop
        XrCompositionLayerQuad layer{XR_TYPE_COMPOSITION_LAYER_QUAD};
        ui::Surface surface{};     // where the laser meets it
    };
    // Placed this opening; until then, the next Update anchors it afresh.
    bool Placed() const { return panel_.has_value(); }
    Frame Update(XrSession session, XrSpace space, ID3D11Device* device, const ui::MenuAnchor& anchor,
                 std::uint64_t reference, const Pose& head, XrTime displayTime);
    // Closed: this opening's place goes. One let go of -- or closed -- while
    // held is kept for the session, relative to `anchor`.
    void Close(const ui::MenuAnchor* anchor);
    // XR teardown: the swapchain, the ImGui context and its D3D objects, and the
    // session's placement.
    void Reset();

private:
    void Drag(const ui::MenuAnchor& anchor, const Pose& head, XrTime displayTime);

    DXGI_FORMAT format_ = DXGI_FORMAT_UNKNOWN;
    std::unique_ptr<InventorySwapchain> swapchain_;
    std::unique_ptr<VrOptionsUi> ui_;
    std::optional<ui::Panel> panel_;
    std::uint64_t reference_ = 0;
    // The grab, the press it belongs to (VrOptionsGrab::serial), and the last
    // frame's time, for the stick's push.
    std::optional<ui::PanelGrab> grab_;
    unsigned grabSerial_ = 0;
    XrTime dragTime_ = 0;
    std::optional<ui::PanelPlacement> placement_;
};
} // namespace preyvr::dll
