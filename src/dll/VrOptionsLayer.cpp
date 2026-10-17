#include "VrOptionsLayer.h"

#include "Logger.h"
#include "VrOptionsRuntime.h"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace preyvr::dll {
namespace {
constexpr float kPushMetresPerSecond = 1.5f;

// The panel's own sRGB format, of the two the runtime may list.
DXGI_FORMAT PanelFormat(XrSession session)
{
    std::uint32_t count = 0;
    if (xrEnumerateSwapchainFormats(session, 0, &count, nullptr) != XR_SUCCESS || count == 0 || count >= 256) {
        return DXGI_FORMAT_UNKNOWN;
    }
    std::vector<std::int64_t> formats(count);
    if (xrEnumerateSwapchainFormats(session, count, &count, formats.data()) != XR_SUCCESS) {
        return DXGI_FORMAT_UNKNOWN;
    }
    for (const auto format : {DXGI_FORMAT_B8G8R8A8_UNORM_SRGB, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB}) {
        if (std::find(formats.begin(), formats.end(), format) != formats.end()) { return format; }
    }
    return DXGI_FORMAT_UNKNOWN;
}
} // namespace

VrOptionsLayer::Frame VrOptionsLayer::Update(XrSession session, XrSpace space, ID3D11Device* device,
                                             const ui::MenuAnchor& anchor, float menuDistance,
                                             std::uint64_t reference, const Pose& head, XrTime displayTime)
{
    Frame out;
    // In front of the game menu (ui::kOptionsDepthRatio), so re-placed when the
    // menu's distance changes -- by a slider on this panel, applied on release,
    // so it moves once and not under the laser. Not once dragged: then where it
    // is, and its size, are the wearer's.
    const bool menuMoved = menuDistance != menuDistance_ && !placement_ && !grab_;
    if (!panel_ || reference_ != reference || menuMoved) {
        panel_ = ui::PlaceMenu(anchor, static_cast<float>(options::Width) / options::Height, menuDistance,
                               1.0f, 0.68f, 0, 0);
        if (panel_) { panel_ = ui::ScaleAbout(*panel_, anchor.upright.position, ui::kOptionsDepthRatio); }
        if (panel_ && placement_) {
            panel_->pose = ui::PlacedPose(anchor, *placement_);
            panel_->width = placedWidth_;
            panel_->height = placedHeight_;
        }
        reference_ = reference;
        menuDistance_ = menuDistance;
        grab_.reset();
    }
    Drag(anchor, head, displayTime);

    // **Drawn on every frame it is open** -- hover and the navigation cursor are
    // live, so no frame is "unchanged" -- into its own target, then copied into
    // its swapchain. A refused frame closes the panel (XrSessionHost).
    if (format_ == DXGI_FORMAT_UNKNOWN) { format_ = PanelFormat(session); }
    bool drawn = false;
    ID3D11DeviceContext* context = nullptr;
    device->GetImmediateContext(&context);
    if (context != nullptr && format_ != DXGI_FORMAT_UNKNOWN) {
        if (!ui_) { ui_ = std::make_unique<VrOptionsUi>(); controlsGeneration_ = 0; }
        std::vector<XrBoundControl> bound;
        unsigned generation = 0;
        if (XrInputBoundControls(bound, generation) && generation != controlsGeneration_) {
            ui_->SetControls(VrOptionsControlRows(bound));
            controlsGeneration_ = generation;
        }
        const auto frame = ui_->Render(device, context, format_, VrOptionsUiFrameInput(), VrOptionsValues(),
                                       VrOptionsSaveFailed(), VrSetupMessage());
        if (frame.texture != nullptr) {
            D3D11_TEXTURE2D_DESC description{};
            frame.texture->GetDesc(&description);
            if (!swapchain_) {
                swapchain_ = std::make_unique<InventorySwapchain>(InventorySwapchainApi{
                    xrEnumerateSwapchainFormats, xrCreateSwapchain, xrEnumerateSwapchainImages,
                    xrAcquireSwapchainImage, xrWaitSwapchainImage, xrReleaseSwapchainImage, DestroyUnheldSwapchain});
            }
            if (swapchain_->Prepare(session, description, format_)) {
                const auto upload = swapchain_->Copy(context, frame.texture);
                drawn = upload == InventorySwapchain::Upload::ready;
                out.sessionFault = upload == InventorySwapchain::Upload::sessionFault;
            }
        } else {
            lifecycle::Log("preyvr_xr_session result=refused detail=vr_options_ui reason=" + ui_->LastError());
        }
        ApplyVrOptionsUi(frame);
    }
    if (context != nullptr) { context->Release(); }

    out.active = drawn && panel_.has_value();
    if (out.active) {
        const auto& p = *panel_;
        out.layer.space = space;
        out.layer.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
        out.layer.pose = {{p.pose.orientation.x, p.pose.orientation.y, p.pose.orientation.z, p.pose.orientation.w},
                          {p.pose.position.x, p.pose.position.y, p.pose.position.z}};
        out.layer.size = {p.width, p.height};
        out.layer.subImage.swapchain = swapchain_->Handle();
        out.layer.subImage.imageRect = {{0, 0}, {static_cast<std::int32_t>(options::Width),
                                                 static_cast<std::int32_t>(options::Height)}};
        out.surface = {p, 0};
    }
    return out;
}

// **Dragged by the laser** (VrOptionsGrab): the press's first frame takes hold
// where the laser is on the panel; after that the panel goes where the laser
// meets the cylinder round the head, the stick pushing it out or pulling it
// in. Let go, its place is kept for this session.
void VrOptionsLayer::Drag(const ui::MenuAnchor& anchor, const Pose& head, XrTime displayTime)
{
    const auto grab = VrOptionsGrabState();
    const float seconds = dragTime_ != 0 && displayTime > dragTime_
        ? std::min(static_cast<float>(displayTime - dragTime_) * 1e-9f, 0.1f) : 0.0f;
    dragTime_ = displayTime;
    if (grab.active && panel_) {
        const auto laser = OptionsPointer(ui::Surface{*panel_, 0});
        if (grab.serial != grabSerial_) {
            grabSerial_ = grab.serial;
            grab_.reset();
            if (laser.active && laser.hit) { grab_ = ui::GrabPanel(*panel_, *laser.hit, head.position); }
        } else if (grab_ && laser.active) {
            grab_->radius = std::clamp(grab_->radius + grab.push * kPushMetresPerSecond * seconds,
                                       ui::kGrabRadiusMin, ui::kGrabRadiusMax);
            if (const auto moved = ui::DragPanel(*grab_, laser.aim, head.position)) { panel_ = moved; }
        }
    } else if (grab_) {
        if (panel_) { Keep(anchor); }
        grab_.reset();
    }
}

void VrOptionsLayer::Keep(const ui::MenuAnchor& anchor)
{
    placement_ = ui::PlacementOf(anchor, *panel_);
    placedWidth_ = panel_->width;
    placedHeight_ = panel_->height;
}

void VrOptionsLayer::Close(const ui::MenuAnchor* anchor)
{
    if (grab_ && panel_ && anchor != nullptr) { Keep(*anchor); }
    grab_.reset();
    panel_.reset();
}

void VrOptionsLayer::Reset()
{
    swapchain_.reset(); // before the session that owns it
    ui_.reset();        // the ImGui context and its D3D objects on the game's device
    *this = VrOptionsLayer{};
}
} // namespace preyvr::dll
