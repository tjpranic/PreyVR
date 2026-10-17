#include "VrOptionsUi.h"
#include "D3D11SavedState.h"

#include <windows.h>

#include "imgui.h"
#include "imgui_impl_dx11.h"
// Hovered and active item ids, and the scrollbar rect: ImGui is pinned (CMake), so these hold.
#include "imgui_internal.h"

#include <algorithm>
#include <cfloat>
#include <cmath>

namespace preyvr::dll {
namespace {
constexpr float kWidth = static_cast<float>(options::Width);
constexpr float kHeight = static_cast<float>(options::Height);
// ImGui's style is designed around a 13 px font; this is the same look at a
// size that reads at the panel's 1.5 m.
constexpr float kBodySize = 30.0f, kHelpSize = 24.0f, kStyleScale = 2.25f;
constexpr float kFooterButton = 72.0f;
constexpr float kScrollSpeed = 900.0f; // pixels per second at full stick
constexpr float kAdjustDelay = 0.4f, kAdjustRepeat = 0.1f; // left/right held: first repeat, then each

// Every format the presentation can use maps to one typeless family; the view
// is the family's UNORM spelling.
DXGI_FORMAT Typeless(DXGI_FORMAT format)
{
    switch (format) {
    case DXGI_FORMAT_B8G8R8A8_UNORM: case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: case DXGI_FORMAT_B8G8R8A8_TYPELESS:
        return DXGI_FORMAT_B8G8R8A8_TYPELESS;
    case DXGI_FORMAT_R8G8B8A8_UNORM: case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: case DXGI_FORMAT_R8G8B8A8_TYPELESS:
        return DXGI_FORMAT_R8G8B8A8_TYPELESS;
    default:
        return DXGI_FORMAT_UNKNOWN;
    }
}
DXGI_FORMAT Unorm(DXGI_FORMAT typeless)
{
    return typeless == DXGI_FORMAT_B8G8R8A8_TYPELESS ? DXGI_FORMAT_B8G8R8A8_UNORM : DXGI_FORMAT_R8G8B8A8_UNORM;
}

std::string Utf8(std::wstring_view text)
{
    if (text.empty()) { return {}; }
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string out(size > 0 ? static_cast<std::size_t>(size) : 0u, '\0');
    if (size > 0) {
        WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size, nullptr, nullptr);
    }
    return out;
}
// A slider's format string shows a finished label: '%' must not start a conversion.
std::string Literal(const std::string& text)
{
    std::string out;
    for (const char c : text) { out += c; if (c == '%') { out += '%'; } }
    return out;
}

// The panel's buttons (options::Button): the rows that act rather than set.
struct ActionText { const char* label; const char* button; const char* help; };
ActionText ActionFor(int id)
{
    switch (id) {
    case options::RecenterView: return {"RESET VR VIEW", "Recenter",
                     "Look straight ahead and select to reset the view and controller reference together."};
    case options::ClearHolsterSlots: return {"CLEAR HOLSTER ASSIGNMENTS", "Clear",
                     "Forget both session holsters. Your weapons stay in the native inventory."};
    case options::CalibratePosture: return {"CALIBRATE CURRENT POSTURE", "Calibrate",
                     "Sit or stand comfortably, look straight ahead, then select. Sets this posture as your height "
                     "baseline and resets the view. Repeat after changing posture."};
    default: return {"", "", ""};
    }
}

void ApplyStyle(float scale)
{
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.ScaleAllSizes(kStyleScale * scale);
    style.FontScaleMain = scale; // every font size below is the size at scale 1
    style.WindowRounding = 0;
    style.WindowBorderSize = 0;
    style.ScrollbarSize = 44 * scale;   // a laser target, not a mouse one
    style.GrabMinSize = 40 * scale;
    style.Colors[ImGuiCol_WindowBg].w = 1; // the panel is opaque; nothing behind it to blend with
    style.Colors[ImGuiCol_PopupBg].w = 1;
}

// Builds one frame. Everything clickable is also recorded as a target, so a
// test can find a control without knowing the layout.
class Builder {
public:
    Builder(VrOptionsUiResult& out, unsigned& page, std::map<int, int>& dragging, const options::Values& values,
            bool forcedPage, float scale, const std::vector<VrOptionsControlRow>& controls)
        : out_(out), page_(page), dragging_(dragging), values_(values), forced_(forcedPage), scale_(scale),
          controls_(controls) {}

    // `adjust`: a left/right step this frame (-1, 0, 1). `adjustKey`: the
    // control the held stick is adjusting, kept until it is let go.
    // `held`: the panel is held by the laser; `blank`: last frame the laser was
    // on it but on no control, where the trigger would grab it.
    void Frame(float scroll, float dt, int adjust, int& adjustKey, bool saveFailed, std::wstring_view setupMessage,
               bool held, bool blank)
    {
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(kWidth, kHeight));
        constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;
        ImGui::Begin("##vr_options", nullptr, flags);
        Tabs();
        // The settings scroll between the fixed tabs and the fixed footer, by
        // the left stick, the scrollbar, or the stick moving to a hidden row.
        ImGui::PushFont(nullptr, kHelpSize);
        const float status = ImGui::GetTextLineHeight();
        ImGui::PopFont();
        const float footer = status + kFooterButton * scale_ + ImGui::GetStyle().ItemSpacing.y * 2;
        ImGui::BeginChild("##settings", ImVec2(0, -footer), ImGuiChildFlags_NavFlattened);
        if (scroll != 0) {
            ImGui::SetScrollY(std::clamp(ImGui::GetScrollY() - scroll * kScrollSpeed * dt, 0.0f, ImGui::GetScrollMaxY()));
        }
        Settings();
        Adjust(adjust, adjustKey);
        out_.scrollY = ImGui::GetScrollY();
        out_.scrollMaxY = ImGui::GetScrollMaxY();
        if (out_.scrollMaxY > 0) {
            const ImRect bar = ImGui::GetWindowScrollbarRect(ImGui::GetCurrentWindow(), ImGuiAxis_Y);
            out_.targets.push_back({Kind::scrollbar, kNoKey, 0, bar.Min.x, bar.Min.y, bar.Max.x, bar.Max.y});
        }
        ImGui::EndChild();
        Status(saveFailed, setupMessage, held, blank);
        Footer();
        ImGui::End();
    }

private:
    using Kind = VrOptionsUiTarget::Kind;

    void Tabs()
    {
        // Half the usual side padding, so all nine names fit across the panel
        // rather than the last, EXPERIMENTAL, being cut short.
        // Tab widths are measured at each BeginTabItem, so it holds to EndTabBar.
        const ImGuiStyle& style = ImGui::GetStyle();
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(style.FramePadding.x * 0.5f, style.FramePadding.y));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2(style.ItemInnerSpacing.x * 0.5f, style.ItemInnerSpacing.y));
        if (!ImGui::BeginTabBar("##pages", ImGuiTabBarFlags_FittingPolicyShrink | ImGuiTabBarFlags_NoTooltip)) {
            ImGui::PopStyleVar(2);
            return;
        }
        int shown = -1;
        for (unsigned p = 0; p < options::MenuPages(); ++p) {
            // A grip step selects the tab; otherwise ImGui's own selection leads.
            const ImGuiTabItemFlags flags = forced_ && p == page_ ? ImGuiTabItemFlags_SetSelected : 0;
            const bool open = ImGui::BeginTabItem(Utf8(options::MenuPageName(p)).c_str(), nullptr, flags);
            Record(Kind::tab, static_cast<int>(p));
            if (open) {
                shown = static_cast<int>(p);
                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();
        ImGui::PopStyleVar(2);
        // The list follows page_ in this frame, so a grip step never shows the
        // old page's rows for a frame.
        if (!forced_ && shown >= 0) { page_ = static_cast<unsigned>(shown); }
    }

    void Settings()
    {
        if (page_ == options::ControlsPage()) {
            Controls();
            return;
        }
        constexpr ImGuiTableFlags flags = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerH |
            ImGuiTableFlags_PadOuterX;
        if (!ImGui::BeginTable("##rows", 2, flags)) { return; }
        ImGui::TableSetupColumn("setting", ImGuiTableColumnFlags_WidthStretch, 0.5f);
        ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch, 0.5f);
        for (const int key : options::MenuEntries(page_)) {
            ImGui::TableNextRow();
            ImGui::PushID(key);
            ImGui::TableSetColumnIndex(0);
            const bool action = key < 0;
            ImGui::TextUnformatted(action ? ActionFor(key).label
                : Utf8(options::Describe(static_cast<unsigned>(key)).label).c_str());
            ImGui::PushFont(nullptr, kHelpSize);
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            ImGui::TextWrapped("%s", action ? ActionFor(key).help
                : Utf8(options::Describe(static_cast<unsigned>(key)).help).c_str());
            ImGui::PopStyleColor();
            ImGui::PopFont();
            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (action) { Action(key); }
            else { Setting(static_cast<unsigned>(key)); }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    // **The Controls tab**: each action and what it is bound to now. Read
    // only -- nothing here is a control, so the trigger anywhere grabs the
    // panel and A has nothing to act on; rebinding is SteamVR's.
    void Controls()
    {
        if (controls_.empty()) {
            ImGui::TextWrapped("%s", "Waiting for the controllers: what each button does appears once the runtime "
                                     "reports them.");
            return;
        }
        constexpr ImGuiTableFlags flags = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerH |
            ImGuiTableFlags_PadOuterX;
        if (!ImGui::BeginTable("##controls", 2, flags)) { return; }
        ImGui::TableSetupColumn("action", ImGuiTableColumnFlags_WidthStretch, 0.5f);
        ImGui::TableSetupColumn("buttons", ImGuiTableColumnFlags_WidthStretch, 0.5f);
        for (const auto& row : controls_) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            if (row.heading) {
                ImGui::TextColored(ImVec4(0.56f, 0.85f, 0.97f, 1.0f), "%s", row.action.c_str());
                continue;
            }
            ImGui::TextWrapped("%s", row.action.c_str());
            ImGui::TableSetColumnIndex(1);
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(row.buttons.empty() ? ImGuiCol_TextDisabled
                                                                                               : ImGuiCol_Text));
            ImGui::TextWrapped("%s", row.buttons.empty() ? "Not bound" : row.buttons.c_str());
            ImGui::PopStyleColor();
        }
        ImGui::EndTable();
    }

    void Setting(unsigned id)
    {
        const auto& spec = options::Describe(id);
        const int value = values_[id];
        const auto kind = options::KindOf(id);
        if (kind == options::Kind::toggle) {
            bool on = value != 0;
            // "###": the ON/OFF shown changes, the control's ID must not. With
            // "##" the whole label is the ID, so ticking it made a new control
            // and the stick's selection -- and A -- lost it.
            const std::string label = Utf8(options::Display(id, value)) + "###value";
            if (ImGui::Checkbox(label.c_str(), &on)) { Push(Set(static_cast<int>(id), on ? 1 : 0)); }
            Record(Kind::toggle, static_cast<int>(id), value ? 0 : 1);
            return;
        }
        if (kind == options::Kind::choice) {
            const auto choices = options::Choices(id);
            const std::string preview = Utf8(options::Display(id, value));
            if (ImGui::BeginCombo("##value", preview.c_str(), ImGuiComboFlags_HeightLargest)) {
                for (const int choice : choices) {
                    const std::string item = Utf8(options::Display(id, choice));
                    if (ImGui::Selectable(item.c_str(), choice == value)) { Push(Set(static_cast<int>(id), choice)); }
                    Record(Kind::choiceItem, static_cast<int>(id), choice);
                }
                ImGui::EndCombo();
            } else {
                Record(Kind::choice, static_cast<int>(id));
                Offer({static_cast<int>(id), value, 0, 0, 0, choices});
            }
            return;
        }
        int v = Held(static_cast<int>(id), value);
        const std::string format = Literal(Utf8(options::Display(id, v)));
        ImGui::SliderInt("##value", &v, spec.min, spec.max, format.c_str(),
                         ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoInput);
        Record(Kind::range, static_cast<int>(id), v);
        Offer({static_cast<int>(id), v, spec.min, spec.max, spec.step, {}});
        if (Released(static_cast<int>(id), v)) { Push(Set(static_cast<int>(id), v)); }
    }

    // **A left/right step**, for the control the held stick is already
    // adjusting, else the one under the laser, else the one last selected
    // (by the stick or a click). Kept until the stick is let go, so a panel
    // that moves as menu placement changes cannot hand the steps to another row.
    void Adjust(int direction, int& lockedKey)
    {
        if (direction == 0) { return; }
        const auto find = [&](int key) -> const Adjustable* {
            for (const auto& a : adjustables_) { if (a.key == key && key != kNoKey) { return &a; } }
            return nullptr;
        };
        const Adjustable* target = find(lockedKey);
        if (target == nullptr) { target = find(hovered_); }
        if (target == nullptr) { target = find(focused_); }
        if (target == nullptr || dragging_.count(target->key) != 0) { return; }
        lockedKey = target->key;
        int next = target->value;
        if (!target->choices.empty()) {
            const auto& c = target->choices;
            auto at = std::find(c.begin(), c.end(), target->value);
            const auto index = at == c.end() ? 0 : static_cast<int>(at - c.begin());
            next = c[static_cast<std::size_t>(std::clamp(index + direction, 0, static_cast<int>(c.size()) - 1))];
        } else {
            next = std::clamp(target->value + direction * target->step, target->min, target->max);
        }
        if (next == target->value) { return; }
        Push(Set(target->key, next));
    }

    void Action(int id)
    {
        if (ImGui::Button(ActionFor(id).button, ImVec2(-FLT_MIN, 0))) {
            options::Change change;
            change.recenter = id == options::RecenterView;
            change.clearHolsters = id == options::ClearHolsterSlots;
            change.calibrate = id == options::CalibratePosture;
            Push(change);
        }
        Record(Kind::action, id);
    }

    void Status(bool saveFailed, std::wstring_view setupMessage, bool held, bool blank)
    {
        // The page with recenter and calibrate is where their result belongs.
        const auto entries = options::MenuEntries(page_);
        const bool setup = !setupMessage.empty() &&
            std::find(entries.begin(), entries.end(), options::RecenterView) != entries.end();
        ImGui::PushFont(nullptr, kHelpSize);
        if (saveFailed) {
            ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "Settings active, but could not be saved to disk.");
        } else if (held) {
            ImGui::TextUnformatted("Moving: aim to place it, stick to push it away or pull it in, let go of the trigger");
        } else if (setup) {
            ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "%s", Utf8(setupMessage).c_str());
        } else if (blank) {
            ImGui::TextUnformatted("Hold the trigger here to drag the panel");
        } else {
            ImGui::TextDisabled("Trigger or A: select   Right stick: move, left/right adjust   Left stick: scroll   Grips: tabs");
        }
        ImGui::PopFont();
    }

    void Footer()
    {
        if (ImGui::Button("Close (B)", ImVec2(-FLT_MIN, kFooterButton * scale_))) { out_.close = true; }
        Record(Kind::close);
    }

    // A slider shows the value it is being dragged to, and reports it once,
    // when let go: a drag would otherwise save to disk every frame.
    int Held(int key, int value) const
    {
        const auto held = dragging_.find(key);
        return held != dragging_.end() ? held->second : value;
    }
    bool Released(int key, int value)
    {
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            dragging_.erase(key);
            return true;
        }
        if (ImGui::IsItemActive()) { dragging_[key] = value; } else { dragging_.erase(key); }
        return false;
    }

    static options::Change Set(int id, int value)
    {
        options::Change change;
        change.setting = id;
        change.value = value;
        return change;
    }
    void Push(const options::Change& change) { out_.changes.push_back(change); }
    void Record(Kind kind, int key = kNoKey, int value = 0)
    {
        const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
        out_.targets.push_back({kind, key, value, a.x, a.y, b.x, b.y});
        if (LaserOn()) { out_.controlHovered = true; }
    }
    // The laser is on the last control, this frame. Not ImGui's "any item
    // hovered", which also counts the previous frame, and not the stick's
    // selection, which ImGui otherwise reports as hovered.
    static bool LaserOn() { return ImGui::IsItemHovered(ImGuiHoveredFlags_NoNavOverride); }

    // A control left/right can step: a range (min, max, step) or the choices.
    struct Adjustable { int key; int value; int min, max, step; std::vector<int> choices; };
    // Called just after the control, so hover and focus are its own.
    void Offer(Adjustable adjustable)
    {
        if (LaserOn()) { hovered_ = adjustable.key; }
        if (ImGui::IsItemFocused()) { focused_ = adjustable.key; }
        adjustables_.push_back(std::move(adjustable));
    }

    VrOptionsUiResult& out_;
    unsigned& page_;
    std::map<int, int>& dragging_;
    const options::Values& values_;
    bool forced_;
    float scale_;
    const std::vector<VrOptionsControlRow>& controls_;
    std::vector<Adjustable> adjustables_;
    int hovered_ = kNoKey, focused_ = kNoKey;
};
} // namespace

VrOptionsUi::VrOptionsUi(float textScale)
    : scale_(std::isfinite(textScale) ? std::clamp(textScale, 0.5f, 3.0f) : 1.0f)
{
}

bool VrOptionsUi::Ensure(ID3D11Device* device, ID3D11DeviceContext* context, DXGI_FORMAT format)
{
    const DXGI_FORMAT family = Typeless(format);
    if (device == nullptr || context == nullptr || family == DXGI_FORMAT_UNKNOWN) {
        error_ = "no_device_or_unsupported_format";
        return false;
    }
    if (imgui_ != nullptr && backend_ && device_.Get() == device && context_.Get() == context && family_ == family) {
        return true;
    }
    Reset();
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = options::Width;
    desc.Height = options::Height;
    desc.MipLevels = desc.ArraySize = 1;
    desc.Format = family;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(device->CreateTexture2D(&desc, nullptr, &target_))) {
        error_ = "create_target";
        Reset();
        return false;
    }
    D3D11_RENDER_TARGET_VIEW_DESC view{};
    view.Format = Unorm(family);
    view.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    if (FAILED(device->CreateRenderTargetView(target_.Get(), &view, &view_))) {
        error_ = "create_view";
        Reset();
        return false;
    }
    device_ = device;
    context_ = context;
    family_ = family;

    imgui_ = ImGui::CreateContext();
    ImGui::SetCurrentContext(imgui_);
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr; // never write imgui.ini into the game's directory
    io.LogFilename = nullptr;
    io.BackendPlatformName = "preyvr_xr";
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad | ImGuiConfigFlags_NoMouseCursorChange;
    io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
    ApplyStyle(scale_);
    ImFontConfig font;
    font.SizePixels = kBodySize;
    io.Fonts->AddFontDefaultVector(&font); // ImGui's own face, scalable
    backend_ = ImGui_ImplDX11_Init(device, context);
    if (!backend_) {
        error_ = "imgui_dx11_init";
        Reset();
        return false;
    }
    error_.clear();
    return true;
}

VrOptionsUiResult VrOptionsUi::Render(ID3D11Device* device, ID3D11DeviceContext* context, DXGI_FORMAT format,
                                      const VrOptionsUiInput& input, const options::Values& values,
                                      bool saveFailed, std::wstring_view setupMessage)
{
    VrOptionsUiResult out;
    out.page = page_;
    if (!Ensure(device, context, format)) { return out; }
    ImGui::SetCurrentContext(imgui_);
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(kWidth, kHeight);
    const float dt = std::isfinite(input.dt) && input.dt > 0 ? std::min(input.dt, 0.1f) : 1.0f / 90.0f;
    io.DeltaTime = dt;
    const bool onPanel = input.pointer && std::isfinite(input.u) && std::isfinite(input.v);
    io.AddMousePosEvent(onPanel ? input.u * kWidth : -FLT_MAX, onPanel ? input.v * kHeight : -FLT_MAX);
    // **A follows the same rule as left/right**: the control under the laser,
    // else the selected one. On a control it is a click where the laser is,
    // exactly as the trigger; ImGui's own activate only reaches the control the
    // stick selected, so pointing at a checkbox and pressing A did nothing.
    // Off a control (or off the panel) it is that activate. The route is chosen
    // as A goes down and kept until it comes up, so a press and its release
    // always go the same way.
    const bool acceptDown = input.accept && !acceptWas_;
    if (acceptDown) { acceptClicks_ = onPanel && controlHovered_; }
    acceptWas_ = input.accept;
    const bool acceptClick = input.accept && acceptClicks_;
    const bool clickDown = input.click && !clickWas_;
    clickWas_ = input.click;
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, input.click || acceptClick);
    // Up/down move between controls. Left/right are not ImGui's: there is one
    // control per row, so sideways navigation has nothing to reach, and here
    // they step the targeted control instead (Builder::Adjust).
    io.AddKeyEvent(ImGuiKey_GamepadDpadUp, input.up);
    io.AddKeyEvent(ImGuiKey_GamepadDpadDown, input.down);
    io.AddKeyEvent(ImGuiKey_GamepadFaceDown, input.accept && !acceptClick);
    // ImGui activates the selected control only while its navigation cursor
    // shows, and a laser click selects a control but hides that cursor. Showing
    // it as A goes down lets A reach a control the laser selected, as left/right
    // already do, and frames what A acted on.
    if (acceptDown && !acceptClicks_) { ImGui::SetNavCursorVisible(true); }
    // The grips step through the tabs.
    const bool forced = input.previousPage != input.nextPage;
    if (forced) {
        page_ = (page_ + (input.nextPage ? 1u : options::MenuPages() - 1u)) % options::MenuPages();
        dragging_.clear();
    }
    const float scroll = std::isfinite(input.scroll) ? std::clamp(input.scroll, -1.0f, 1.0f) : 0.0f;
    // One step per press, then repeating while held, like the game menus'
    // navigator. A new press, or a let-go, picks the target afresh.
    const int held = input.left == input.right ? 0 : (input.right ? 1 : -1);
    int adjust = 0;
    if (held != adjustHeld_) {
        adjustHeld_ = held;
        adjustTime_ = 0;
        adjustKey_ = kNoKey;
        adjust = held;
    } else if (held != 0) {
        adjustTime_ += dt;
        if (adjustTime_ >= kAdjustDelay) {
            adjustTime_ -= kAdjustRepeat;
            adjust = held;
        }
    }

    ImGui_ImplDX11_NewFrame();
    ImGui::NewFrame();
    Builder(out, page_, dragging_, values, forced, scale_, controls_)
        .Frame(scroll, dt, adjust, adjustKey_, saveFailed, setupMessage, input.held, blank_);
    controlHovered_ = out.controlHovered; // routes the next A press
    // **A press on nothing ImGui answers grabs the panel.** Read from ImGui's own
    // hovered and active item this frame, not our list of controls, so the
    // scrollbar counts as one; and never while a choice list was open, since
    // that press is what closes it.
    const bool onNothing = onPanel && ImGui::GetHoveredID() == 0 && ImGui::GetActiveID() == 0;
    out.grabStarted = clickDown && onNothing && !popupWas_;
    popupWas_ = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId);
    blank_ = onNothing && !popupWas_ && !input.held; // the next frame's hint
    ImGui::Render();
    {
        D3D11SavedState saved(context);
        ID3D11RenderTargetView* view = view_.Get();
        context->OMSetRenderTargets(1, &view, nullptr);
        const ImVec4 background = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
        const float clear[4] = {background.x, background.y, background.z, 1.0f};
        context->ClearRenderTargetView(view, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    }
    out.texture = target_.Get();
    out.page = page_;
    return out;
}

void VrOptionsUi::Reset()
{
    if (imgui_ != nullptr) {
        ImGui::SetCurrentContext(imgui_);
        if (backend_) { ImGui_ImplDX11_Shutdown(); }
        ImGui::DestroyContext(imgui_);
        imgui_ = nullptr;
    }
    backend_ = false;
    view_.Reset();
    target_.Reset();
    context_.Reset();
    device_.Reset();
    family_ = DXGI_FORMAT_UNKNOWN;
    page_ = 0;
    dragging_.clear();
    adjustHeld_ = 0;
    adjustTime_ = 0;
    adjustKey_ = kNoKey;
    acceptWas_ = acceptClicks_ = controlHovered_ = false;
    clickWas_ = popupWas_ = blank_ = false;
}

} // namespace preyvr::dll
