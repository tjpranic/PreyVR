#pragma once

#include "preyvr/VrOptions.h"

#include <d3d11.h>
#include <wrl/client.h>

#include <limits>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

struct ImGuiContext;

namespace preyvr::dll {

// No setting, button or tab: a target, or a control in hand, that is none.
inline constexpr int kNoKey = std::numeric_limits<int>::min();

// What the controls did to the options panel this frame, in panel terms. Built
// on the XR frame thread from the tracking sample (VrOptionsRuntime); every
// button is false until the controls have been at rest since the panel opened.
struct VrOptionsUiInput {
    bool pointer = false;          // the laser is on the panel...
    float u = 0, v = 0;            // ...here, 0..1 from the top-left
    bool click = false;            // the laser hand's trigger is held
    // Navigation held that way (TrackingFrame::navigate: the right stick, or a
    // controller's navigation buttons): up/down move between controls,
    // left/right adjust one a step.
    bool up = false, down = false;
    bool left = false, right = false;
    bool accept = false;           // A held: the control under the laser, else the selected one
    bool previousPage = false, nextPage = false; // a grip pressed this frame
    float scroll = 0;              // left stick, -1 (down the list) .. 1 (up)
    float dt = 0;                  // seconds since the previous frame
    bool held = false;             // the panel is held by the laser: the hint says how to place it
};

// **One row of the Controls tab**: an action and the buttons it is bound to
// now, by the runtime's own names (XrInputBoundControls), or a heading.
struct VrOptionsControlRow {
    std::string action, buttons;
    bool heading = false;
};

// A control drawn this frame, in panel pixels: what it is and which setting.
struct VrOptionsUiTarget {
    enum class Kind { tab, toggle, choice, choiceItem, range, action, close, scrollbar };
    Kind kind = Kind::tab;
    int key = kNoKey; // options::Setting, options::Button, or the page for a tab
    int value = 0;  // a choice item's value; a toggle's value if clicked
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
};

struct VrOptionsUiResult {
    ID3D11Texture2D* texture = nullptr;      // borrowed; null when the frame was refused
    std::vector<options::Change> changes;    // the same currency as options::Menu
    bool close = false;                      // "Close (B)" was activated
    unsigned page = 0;
    float scrollY = 0, scrollMaxY = 0;       // the settings list's scroll position and range
    bool controlHovered = false;             // the laser is on a control this frame
    // The trigger went down with the laser on the panel but on nothing ImGui
    // answers -- help text, a label, empty space: the runtime drags the panel.
    bool grabStarted = false;
    std::vector<VrOptionsUiTarget> targets;  // every control drawn (tests, diagnostics)
};

// **The VR Options panel**, drawn with Dear ImGui: its own dark style and
// widgets, sized for a headset. A tab per category, then that category's
// settings as a scrolling list (options::MenuEntries, any number of rows) --
// a checkbox, a choice list or a slider by what the setting is
// (options::KindOf) -- each with its help. Navigation left/right adjusts a
// slider or choice list one step: the one under the laser, else the one last
// selected. A trigger press on no control grabs the panel (grabStarted;
// VrOptionsLayer drags it). Draws into its own options::Width x
// options::Height target on the game's D3D11 device. Settings leave as
// options::Change, so applying, saving and haptics stay in one place
// (VrOptionsRuntime). A slider reports once, when it is let go.
//
// XR frame thread only: the immediate context is the game's, and every piece of
// pipeline state a draw touches is restored (D3D11SavedState). The target is
// typeless with a UNORM view, so the copy into the sRGB swapchain converts
// nothing. Nothing is written to disk: no imgui.ini, no log file.
class VrOptionsUi {
public:
    // `textScale` sizes text and spacing together, 0.5..3; 1 is the size the
    // panel is designed at. Larger text makes a page longer, and it scrolls.
    explicit VrOptionsUi(float textScale = 1.0f);
    ~VrOptionsUi() { Reset(); }
    VrOptionsUi(const VrOptionsUi&) = delete;
    VrOptionsUi& operator=(const VrOptionsUi&) = delete;

    // `format` is the presentation format; its family (B8G8R8A8 or R8G8B8A8)
    // decides the target's. A refused frame returns a null texture and leaves
    // the reason in LastError().
    VrOptionsUiResult Render(ID3D11Device* device, ID3D11DeviceContext* context, DXGI_FORMAT format,
                             const VrOptionsUiInput& input, const options::Values& values,
                             bool saveFailed, std::wstring_view setupMessage);
    // Releases the ImGui context and every D3D object; the next Render starts afresh.
    void Reset();
    // What the Controls tab lists (options::ControlsPage), until set again.
    void SetControls(std::vector<VrOptionsControlRow> rows) { controls_ = std::move(rows); }
    const std::string& LastError() const { return error_; }

private:
    bool Ensure(ID3D11Device* device, ID3D11DeviceContext* context, DXGI_FORMAT format);

    ImGuiContext* imgui_ = nullptr;
    bool backend_ = false;
    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> target_;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> view_;
    DXGI_FORMAT family_ = DXGI_FORMAT_UNKNOWN;
    float scale_ = 1.0f;
    unsigned page_ = 0;
    std::vector<VrOptionsControlRow> controls_;
    std::map<int, int> dragging_; // a slider's value while it is held, by key
    // Left/right adjustment: the direction held, for how long (repeat), and the
    // control it is adjusting, kept until the stick is let go.
    int adjustHeld_ = 0;
    float adjustTime_ = 0;
    int adjustKey_ = kNoKey;
    // A: a click where the laser is when it is on a control (as the trigger),
    // else ImGui's activate for the selected control. Chosen as A goes down,
    // kept until it comes up, from whether a control was hovered last frame.
    bool acceptWas_ = false, acceptClicks_ = false, controlHovered_ = false;
    bool clickWas_ = false, popupWas_ = false; // the trigger, and an open choice list, last frame
    bool blank_ = false;                       // last frame the laser was on the panel but on no control
    std::string error_;
};

} // namespace preyvr::dll
