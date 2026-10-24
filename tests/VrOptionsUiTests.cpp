// The VR Options panel (VrOptionsUi), drawn by the real Dear ImGui DX11
// backend on WARP: no game, no headset. Checks the target the swapchain copies,
// the pixels, that the caller's pipeline state survives, and what each kind of
// control sends when the laser uses it: checkbox, choice list, slider, button,
// tabs by grip, scrolling and back -- and that every tab draws all its rows.
#include "VrOptionsUi.h"
#include "TestSupport.h"

#include <d3d11.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>

using Microsoft::WRL::ComPtr;
using namespace preyvr;
using namespace preyvr::dll;
using Kind = VrOptionsUiTarget::Kind;

namespace {
using preyvr::test::Check;

std::array<unsigned char, 4> Pixel(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11Texture2D* texture,
                                   unsigned x, unsigned y)
{
    D3D11_TEXTURE2D_DESC desc{};
    texture->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;
    ComPtr<ID3D11Texture2D> staging;
    Check(SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &staging)), "staging copy");
    context->CopyResource(staging.Get(), texture);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    Check(SUCCEEDED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)), "map staging copy");
    const auto* row = static_cast<const unsigned char*>(mapped.pData) + static_cast<std::size_t>(y) * mapped.RowPitch;
    std::array<unsigned char, 4> out{row[x * 4], row[x * 4 + 1], row[x * 4 + 2], row[x * 4 + 3]};
    context->Unmap(staging.Get(), 0);
    return out;
}

std::optional<VrOptionsUiTarget> Find(const VrOptionsUiResult& result, Kind kind, int key = kNoKey, std::optional<int> value = {})
{
    for (const auto& target : result.targets) {
        if (target.kind == kind && (kind == Kind::close || target.key == key) && (!value || target.value == *value)) {
            return target;
        }
    }
    return {};
}

// The laser at a fraction of a target's width, at its middle height.
VrOptionsUiInput At(const VrOptionsUiTarget& target, bool click, float across = 0.5f)
{
    VrOptionsUiInput input;
    input.pointer = true;
    input.u = (target.x0 + (target.x1 - target.x0) * across) / static_cast<float>(options::Width);
    input.v = (target.y0 + target.y1) * 0.5f / static_cast<float>(options::Height);
    input.click = click;
    input.dt = 1.0f / 90.0f;
    return input;
}

struct Panel {
    Panel(ID3D11Device* d, ID3D11DeviceContext* c, float textScale = 1.0f) : device(d), context(c), ui(textScale) {}
    ID3D11Device* device;
    ID3D11DeviceContext* context;
    VrOptionsUi ui;
    options::Values values = options::Defaults();
    DXGI_FORMAT format = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;

    VrOptionsUiResult Frame(const VrOptionsUiInput& input)
    {
        return ui.Render(device, context, format, input, values, false, {});
    }
    VrOptionsUiResult Idle()
    {
        VrOptionsUiInput input;
        input.dt = 1.0f / 90.0f;
        return Frame(input);
    }
    VrOptionsUiResult Grip(bool next)
    {
        VrOptionsUiInput input;
        input.dt = 1.0f / 90.0f;
        (next ? input.nextPage : input.previousPage) = true;
        return Frame(input);
    }
    // Hover, press, release: what a trigger pull does. Returns the release
    // frame, carrying every change, close and grab any of the three sent.
    VrOptionsUiResult Click(const VrOptionsUiTarget& target, float across = 0.5f)
    {
        std::vector<options::Change> sent;
        bool close = false, grabbed = false;
        VrOptionsUiResult frame;
        for (const bool down : {false, true, false}) {
            frame = Frame(At(target, down, across));
            sent.insert(sent.end(), frame.changes.begin(), frame.changes.end());
            close = close || frame.close;
            grabbed = grabbed || frame.grabStarted;
        }
        frame.changes = sent;
        frame.close = close;
        frame.grabStarted = grabbed;
        return frame;
    }
    // The one change a frame sent for `setting`, taken into the values the
    // next frame shows, as the runtime would.
    std::optional<int> Took(const VrOptionsUiResult& frame, int setting)
    {
        std::optional<int> took;
        for (const auto& change : frame.changes) {
            if (change.setting == setting) { took = change.value; values[static_cast<unsigned>(setting)] = change.value; }
        }
        return took;
    }
};
} // namespace

// Opt-in preview: PREYVR_UI_PREVIEW=<directory> writes every page as a PPM,
// then the interface page scrolled to its end and the turning list open.
void WritePreviews(ID3D11Device* device, ID3D11DeviceContext* context, const std::string& directory)
{
    D3D11_TEXTURE2D_DESC desc{};
        auto write = [&](const VrOptionsUiResult& shot, const std::string& name) {
            shot.texture->GetDesc(&desc);
            desc.Usage = D3D11_USAGE_STAGING;
            desc.BindFlags = 0;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            ComPtr<ID3D11Texture2D> staging;
            Check(SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &staging)), "preview staging");
            context->CopyResource(staging.Get(), shot.texture);
            D3D11_MAPPED_SUBRESOURCE mapped{};
            Check(SUCCEEDED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)), "preview map");
            const std::string path = directory + "\\vr-options-imgui-" + name + ".ppm";
            FILE* file = nullptr;
            Check(fopen_s(&file, path.c_str(), "wb") == 0 && file != nullptr, "preview file");
            std::fprintf(file, "P6\n%u %u\n255\n", desc.Width, desc.Height);
            for (unsigned y = 0; y < desc.Height; ++y) {
                const auto* row = static_cast<const unsigned char*>(mapped.pData) + static_cast<std::size_t>(y) * mapped.RowPitch;
                for (unsigned x = 0; x < desc.Width; ++x) {
                    const unsigned char rgb[3] = {row[x * 4 + 2], row[x * 4 + 1], row[x * 4]};
                    std::fwrite(rgb, 1, 3, file);
                }
            }
            std::fclose(file);
            context->Unmap(staging.Get(), 0);
        };
        Panel shots{device, context};
        auto shot = shots.Idle();
        for (unsigned page = 0; page < options::MenuPages(); ++page) {
            shot = page == 0 ? shots.Idle() : shots.Grip(true);
            shot = shots.Idle();
            write(shot, std::to_string(page));
        }
        shots.Grip(true);
        shots.Grip(true);
        shots.Grip(true);
        VrOptionsUiInput down;
        down.dt = 1.0f / 90.0f;
        down.scroll = -1.0f;
        for (int n = 0; n < 120; ++n) { shot = shots.Frame(down); }
        shot = shots.Idle();
        write(shot, "2-scrolled");
        shots.Grip(false);
        shots.Grip(false);
        shot = shots.Idle();
        shots.Frame(At(*Find(shot, Kind::choice, options::Turn), false));
        shots.Frame(At(*Find(shot, Kind::choice, options::Turn), true));
        shot = shots.Frame(At(*Find(shot, Kind::choice, options::Turn), false));
        shot = shots.Idle();
        write(shot, "0-turning-list");
}

int main()
{
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    Check(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION,
                                      &device, nullptr, &context)), "WARP device");

    char preview[260]{};
    if (GetEnvironmentVariableA("PREYVR_UI_PREVIEW", preview, sizeof(preview)) > 0) {
        WritePreviews(device.Get(), context.Get(), preview);
    }

    // The game's state, as far as the panel can see it: a bound target and viewport.
    D3D11_TEXTURE2D_DESC gameDesc{};
    gameDesc.Width = gameDesc.Height = 64;
    gameDesc.MipLevels = gameDesc.ArraySize = 1;
    gameDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    gameDesc.SampleDesc.Count = 1;
    gameDesc.BindFlags = D3D11_BIND_RENDER_TARGET;
    ComPtr<ID3D11Texture2D> gameTexture;
    ComPtr<ID3D11RenderTargetView> gameView;
    Check(SUCCEEDED(device->CreateTexture2D(&gameDesc, nullptr, &gameTexture)) &&
          SUCCEEDED(device->CreateRenderTargetView(gameTexture.Get(), nullptr, &gameView)), "game target");
    ID3D11RenderTargetView* bound = gameView.Get();
    context->OMSetRenderTargets(1, &bound, nullptr);
    const D3D11_VIEWPORT gameViewport{1, 2, 30, 40, 0, 1};
    context->RSSetViewports(1, &gameViewport);

    Panel panel{device.Get(), context.Get()};
    auto frame = panel.Idle();
    frame = panel.Idle(); // ImGui lays a tab bar out on its second frame
    Check(frame.texture != nullptr, "a frame is drawn");
    D3D11_TEXTURE2D_DESC desc{};
    frame.texture->GetDesc(&desc);
    Check(desc.Width == options::Width && desc.Height == options::Height, "options::Width by Height, so fit and pointer mapping hold");
    Check(desc.Format == DXGI_FORMAT_B8G8R8A8_TYPELESS, "typeless in the presentation's family");

    ID3D11RenderTargetView* after = nullptr;
    context->OMGetRenderTargets(1, &after, nullptr);
    Check(after == gameView.Get(), "the game's render target is restored");
    if (after != nullptr) { after->Release(); }
    D3D11_VIEWPORT viewport{};
    UINT viewports = 1;
    context->RSGetViewports(&viewports, &viewport);
    Check(viewports == 1 && viewport.TopLeftX == 1 && viewport.TopLeftY == 2 && viewport.Width == 30 && viewport.Height == 40,
          "and its viewport");

    // ImGui's own dark window, opaque.
    const auto corner = Pixel(device.Get(), context.Get(), frame.texture, 4, options::Height - 4);
    Check(corner[0] == corner[1] && corner[1] == corner[2] && corner[0] < 24 && corner[3] == 255, "ImGui's dark, opaque background");

    // The selected tab is ImGui's blue: R and B differ, so the byte order shows.
    const auto tab = Find(frame, Kind::tab, 0);
    Check(tab.has_value(), "tabs are drawn");
    const auto tabX = static_cast<unsigned>(tab->x0 + 8), tabY = static_cast<unsigned>((tab->y0 + tab->y1) * 0.5f);
    const auto bgra = Pixel(device.Get(), context.Get(), frame.texture, tabX, tabY);
    Check(bgra[0] != bgra[2], "premise: the selected tab is coloured");

    // What the swapchain copy does: same bytes into an sRGB image, no conversion.
    D3D11_TEXTURE2D_DESC srgbDesc = desc;
    srgbDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
    srgbDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    ComPtr<ID3D11Texture2D> swapImage;
    Check(SUCCEEDED(device->CreateTexture2D(&srgbDesc, nullptr, &swapImage)), "sRGB swapchain stand-in");
    context->CopyResource(swapImage.Get(), frame.texture);
    Check(Pixel(device.Get(), context.Get(), swapImage.Get(), tabX, tabY) == bgra, "copied into an sRGB image byte for byte");

    // Comfort: each setting as the widget its values call for.
    Check(frame.page == 0 && Find(frame, Kind::close).has_value(), "comfort page and back");
    Check(Find(frame, Kind::choice, options::Turn).has_value(), "turning is a choice of angles");
    Check(Find(frame, Kind::choice, options::HeadRelative).has_value(), "head/body relative is a named choice, not a checkbox");
    Check(Find(frame, Kind::range, options::TurnSpeed).has_value(), "turn speed is a slider");

    // A choice list: open it, pick 30 degrees.
    frame = panel.Click(*Find(frame, Kind::choice, options::Turn));
    Check(frame.changes.empty(), "opening a choice list changes nothing");
    frame = panel.Idle(); // a popup is sized, hidden, on its first frame
    const auto thirty = Find(frame, Kind::choiceItem, options::Turn, 30);
    Check(thirty.has_value() && Find(frame, Kind::choiceItem, options::Turn, 0).has_value(), "the list offers the angles, smooth included");
    frame = panel.Click(*thirty);
    Check(frame.changes.size() == 1 && frame.changes[0].setting == options::Turn &&
          frame.changes[0].value == 30, "picking 30 degrees sends 30");

    // Hands: a checkbox.
    frame = panel.Grip(true);
    Check(frame.page == 1, "right grip: the hands page, drawn this frame");
    const auto twoHand = Find(frame, Kind::toggle, options::TwoHand);
    Check(twoHand.has_value() && Find(frame, Kind::choice, options::GripToggle).has_value(), "on/off is a checkbox, hold/toggle a choice");
    frame = panel.Click(*twoHand);
    Check(frame.changes.size() == 1 && frame.changes[0].setting == options::TwoHand &&
          frame.changes[0].value == 0, "unticking two-hand aim sends off");

    // Interface: a slider reports once, when let go, at where it was let go.
    panel.Grip(true);
    frame = panel.Idle();
    Check(frame.page == 2, "interface page");
    const auto scale = Find(frame, Kind::range, options::UiScale);
    Check(scale.has_value(), "interface size is a slider");
    panel.Frame(At(*scale, false, 0.1f));
    auto held = panel.Frame(At(*scale, true, 0.1f));
    held = panel.Frame(At(*scale, true, 0.9f));
    held = panel.Frame(At(*scale, true, 0.9f));
    const auto dragging = Find(held, Kind::range, options::UiScale);
    Check(held.changes.empty() && dragging && dragging->value > 160, "while held it shows the drag and sends nothing");
    frame = panel.Frame(At(*scale, false, 0.9f));
    Check(frame.changes.size() == 1 && frame.changes[0].setting == options::UiScale &&
          frame.changes[0].value > 160 && frame.changes[0].value <= 200, "let go: one change, at the drag's value");
    frame = panel.Idle();
    Check(frame.changes.empty(), "and nothing after");

    // The menu placement settings live on this page, as settings like any other.
    const auto distance = Find(frame, Kind::range, options::UiDistance);
    Check(distance && Find(frame, Kind::range, options::UiOffsetRight) && Find(frame, Kind::range, options::UiOffsetUp),
          "menu distance and offset are on the interface page");
    Check(Find(frame, Kind::choice, options::PointerHand) && Find(frame, Kind::choice, options::HudLayer),
          "laser hand and HUD style are named choices on it too");
    Check(frame.scrollMaxY > 0, "ten rows: the interface page scrolls at the designed size");
    frame = panel.Click(*distance, 0.98f);
    Check(frame.changes.size() == 1 && panel.Took(frame, options::UiDistance) >= 390,
          "the far end of menu distance sends about 400 cm, nothing else");
    frame = panel.Idle();
    frame = panel.Click(*Find(frame, Kind::range, options::UiOffsetRight), 0.02f);
    Check(frame.changes.size() == 1 && panel.Took(frame, options::UiOffsetRight) <= -85,
          "the near end of left/right sends about 90 degrees left, and only that");

    // Left/right on the right stick: one step of the control under the laser,
    // repeating while held, and kept on that control until let go.
    {
        Panel fine(device.Get(), context.Get());
        fine.Idle();
        fine.Grip(true);
        fine.Grip(true);
        frame = fine.Idle();
        const auto bar = *Find(frame, Kind::range, options::UiDistance);
        const auto curve = *Find(frame, Kind::range, options::UiCurve);
        auto hover = At(bar, false);
        hover.right = true;
        frame = fine.Frame(hover);
        Check(frame.changes.size() == 1 && fine.Took(frame, options::UiDistance) == 205, "right on the distance bar under the laser: 5 cm further");
        frame = fine.Frame(hover);
        Check(frame.changes.empty(), "held: nothing more before the repeat delay");
        int steps = 0;
        for (int n = 0; n < 45; ++n) {
            frame = fine.Frame(hover);
            if (fine.Took(frame, options::UiDistance)) { ++steps; }
        }
        Check(steps >= 2 && fine.values[options::UiDistance] == 205 + 5 * steps, "held past the delay: it repeats, a step at a time");
        hover.right = false;
        fine.Frame(hover);
        hover.left = true;
        frame = fine.Frame(hover);
        const int before = fine.values[options::UiDistance];
        Check(fine.Took(frame, options::UiDistance) == before - 5, "left: 5 cm nearer");
        hover.left = false;
        fine.Frame(hover);

        // The laser leaving the control mid-hold (as a moving panel would) does
        // not hand the steps to the control it lands on.
        hover.right = true;
        frame = fine.Frame(hover);
        fine.Took(frame, options::UiDistance);
        auto moved = At(curve, false);
        moved.right = true;
        bool curveChanged = false;
        int distanceSteps = 0;
        for (int n = 0; n < 45; ++n) {
            frame = fine.Frame(moved);
            curveChanged = curveChanged || fine.Took(frame, options::UiCurve).has_value();
            if (fine.Took(frame, options::UiDistance)) { ++distanceSteps; }
        }
        Check(!curveChanged && distanceSteps >= 2, "the held stick keeps adjusting the control it started on");
        moved.right = false;
        fine.Frame(moved);
        moved.right = true;
        frame = fine.Frame(moved);
        Check(frame.changes.size() == 1 && frame.changes[0].setting == options::UiCurve &&
              frame.changes[0].value == 40, "a new press takes the control now under the laser, one step of 5 degrees");
        moved.right = false;
        fine.Frame(moved);

        // No laser on a control: the one last selected (here by a click).
        frame = fine.Click(*Find(fine.Idle(), Kind::range, options::UiScale));
        Check(!frame.changes.empty() && frame.changes.back().setting == options::UiScale, "premise: a click selects interface size");
        fine.values[options::UiScale] = frame.changes.back().value;
        VrOptionsUiInput away;
        away.dt = 1.0f / 90.0f;
        away.right = true;
        frame = fine.Frame(away);
        Check(frame.changes.size() == 1 && frame.changes[0].setting == options::UiScale &&
              frame.changes[0].value == std::min(200, fine.values[options::UiScale] + 10), "without the laser, the selected slider steps by its own 10%");
        away.right = false;
        fine.Frame(away);
        // The laser outranks the selection.
        hover.right = false;
        fine.Frame(hover);
        hover.right = true;
        frame = fine.Frame(hover);
        Check(frame.changes.size() == 1 && frame.changes[0].setting == options::UiDistance,
              "the control under the laser wins over the one selected");
        hover.right = false;
        fine.Frame(hover);
        // Nothing past the end of a range.
        fine.values[options::UiDistance] = 400;
        fine.Frame(hover);
        hover.right = true;
        frame = fine.Frame(hover);
        Check(frame.changes.empty(), "at 400 cm, right does nothing");
        hover.right = false;
        fine.Frame(hover);

        // A choice list steps through its choices; a checkbox is left to A and the trigger.
        fine.Grip(false);
        fine.Grip(false);
        frame = fine.Idle();
        auto turning = At(*Find(frame, Kind::choice, options::Turn), false);
        turning.right = true;
        frame = fine.Frame(turning);
        Check(frame.changes.size() == 1 && frame.changes[0].setting == options::Turn && frame.changes[0].value == 60,
              "right on turning: 45 to 60 degrees");
        turning.right = false;
        fine.Frame(turning);
        fine.values[options::Turn] = 0;
        fine.Frame(turning);
        turning.left = true;
        frame = fine.Frame(turning);
        Check(frame.changes.empty(), "left from smooth: nothing before the first choice");
        turning.left = false;
        fine.Frame(turning);
        fine.Grip(true);
        frame = fine.Idle();
        auto tick = At(*Find(frame, Kind::toggle, options::TwoHand), false);
        tick.right = true;
        frame = fine.Frame(tick);
        Check(frame.changes.empty(), "left/right does not flip a checkbox");
    }

    // A: on the control under the laser it is a click there, as the trigger;
    // off a control it activates the selected one, never clicking empty panel.
    {
        Panel press(device.Get(), context.Get());
        press.Idle();
        press.Grip(true);
        frame = press.Idle();
        const auto box = *Find(frame, Kind::toggle, options::TwoHand);
        auto a = At(box, false);
        press.Frame(a); // the laser comes to rest on the checkbox
        a.accept = true;
        const auto down = press.Frame(a);
        a.accept = false;
        auto up = press.Frame(a);
        Check(down.changes.empty() && up.changes.size() == 1 && up.changes[0].setting == options::TwoHand &&
              up.changes[0].value == 0, "A on the checkbox under the laser unticks it, as the trigger would");
        press.values[options::TwoHand] = 0;
        // The checkbox is now the selected control. With the laser on empty
        // panel space, A activates it rather than clicking nothing.
        VrOptionsUiInput empty;
        empty.dt = 1.0f / 90.0f;
        empty.pointer = true;
        // In the names-and-help column, which holds no control whatever the
        // rows are (the value column, right of the middle, holds them all).
        empty.u = 0.2f;
        empty.v = 0.7f;
        frame = press.Frame(empty);
        Check(Find(frame, Kind::toggle, options::TwoHand)->y1 < 0.7f * options::Height, "premise: the checkbox is above");
        for (const auto& target : frame.targets) {
            const float x = empty.u * options::Width, y = empty.v * options::Height;
            Check(target.kind == Kind::tab || target.kind == Kind::scrollbar || !(x >= target.x0 && x <= target.x1 &&
                  y >= target.y0 && y <= target.y1), "premise: no control is under the laser");
        }
        empty.accept = true;
        std::vector<options::Change> sent;
        for (int n = 0; n < 2; ++n) { frame = press.Frame(empty); sent.insert(sent.end(), frame.changes.begin(), frame.changes.end()); }
        empty.accept = false;
        for (int n = 0; n < 2; ++n) { frame = press.Frame(empty); sent.insert(sent.end(), frame.changes.begin(), frame.changes.end()); }
        Check(sent.size() == 1 && sent[0].setting == options::TwoHand && sent[0].value == 1,
              "A with the laser off any control ticks the selected checkbox back");
        // An action button under the laser.
        for (int n = 0; n < 5; ++n) { press.Grip(true); }
        frame = press.Idle();
        Check(frame.page == 6, "setup page");
        const auto recenter = *Find(frame, Kind::action, options::RecenterView);
        a = At(recenter, false);
        press.Frame(a);
        a.accept = true;
        press.Frame(a);
        a.accept = false;
        up = press.Frame(a);
        Check(up.changes.size() == 1 && up.changes[0].recenter, "A on recenter under the laser sends it");
        // A held while the laser slides off the control does not click it.
        const auto box2 = *Find(up, Kind::toggle, options::DebugOverlay);
        a = At(box2, false);
        press.Frame(a);
        a.accept = true;
        press.Frame(a);
        empty.accept = true;
        frame = press.Frame(empty);
        empty.accept = false;
        frame = press.Frame(empty);
        Check(frame.changes.empty(), "pressed on a control and let go off it: no click");
    }

    // A page longer than the panel scrolls, by as much as it needs: larger text
    // makes the interface page outgrow it by more.
    {
        Panel tall(device.Get(), context.Get(), 1.6f);
        tall.Idle();
        tall.Grip(true);
        tall.Grip(true);
        frame = tall.Idle();
        Check(frame.page == 2 && frame.scrollMaxY > 0, "with larger text the interface page outgrows the panel");
        // The interface page ends with the reticle: its fallback distance is the last row.
        const auto last = Find(frame, Kind::range, options::ReticleFallback);
        const auto back = Find(frame, Kind::close);
        Check(last && back && last->y1 > back->y0, "its last row starts out of sight, under the footer");
        const float top = last->y0;
        VrOptionsUiInput scroll;
        scroll.dt = 1.0f / 90.0f;
        scroll.scroll = -1.0f;
        for (int n = 0; n < 300; ++n) { frame = tall.Frame(scroll); }
        Check(frame.scrollY > 0 && frame.scrollY == frame.scrollMaxY, "the left stick scrolls to the end, and stops there");
        const auto lowered = Find(frame, Kind::range, options::ReticleFallback);
        Check(lowered && lowered->y0 < top && lowered->y1 < Find(frame, Kind::close)->y0, "the last row comes into view");
        frame = tall.Click(*lowered, 0.98f);
        Check(tall.Took(frame, options::ReticleFallback) >= 190000, "a row reached by scrolling works: about 196 m");
        scroll.scroll = 1.0f;
        for (int n = 0; n < 300; ++n) { frame = tall.Frame(scroll); }
        Check(frame.scrollY == 0, "and back up to the top");
    }

    // **A trigger press on no control grabs the panel** (the runtime drags it):
    // on help text, a label or empty space it does; on a control, a tab or the
    // scrollbar it does not, nor while a choice list is open -- that press closes
    // the list.
    {
        Panel hold(device.Get(), context.Get());
        hold.Idle();
        frame = hold.Idle();
        const auto turning = *Find(frame, Kind::choice, options::Turn);
        auto label = turning;              // the same row's left column: its name and help
        label.x0 = 40;
        label.x1 = 60;
        {
            Panel fresh(device.Get(), context.Get());
            fresh.Idle();
            fresh.Idle();
            VrOptionsUiInput a = At(label, false);
            fresh.Frame(a);
            a.accept = true;
            Check(!fresh.Frame(a).grabStarted, "A never grabs: only the trigger does");
        }
        frame = hold.Click(label);
        Check(frame.grabStarted && frame.changes.empty(), "a press on a setting's name and help grabs the panel, changing nothing");
        Check(!hold.Click(turning).grabStarted, "a press on a control does not");
        frame = hold.Idle();
        Check(Find(frame, Kind::choiceItem, options::Turn, 30).has_value(), "premise: that press opened the turning list");
        frame = hold.Click(label);
        Check(!frame.grabStarted, "with a choice list open, the press closes it rather than grabbing");
        frame = hold.Idle();
        Check(!Find(frame, Kind::choiceItem, options::Turn, 30).has_value(), "and the list is closed");
        Check(hold.Click(label).grabStarted, "after which the same press grabs");
        Check(!hold.Click(*Find(hold.Idle(), Kind::tab, 1)).grabStarted, "a tab is a control");
        hold.Grip(true); // from the hands tab that click opened, to the interface tab
        frame = hold.Idle();
        const auto bar = Find(frame, Kind::scrollbar);
        Check(frame.page == 2 && bar.has_value(), "premise: the interface page has a scrollbar");
        Check(!hold.Click(*bar).grabStarted, "the scrollbar is a control too");
    }

    // Every tab draws every one of its rows (options::MenuEntries) as a control,
    // reachable by scrolling however many there are.
    {
        Panel all(device.Get(), context.Get());
        all.Idle();
        frame = all.Idle();
        for (unsigned page = 0; page < options::MenuPages(); ++page) {
            if (page > 0) { all.Grip(true); frame = all.Idle(); }
            Check(frame.page == page, "each tab in turn");
            for (const int key : options::MenuEntries(page)) {
                const bool drawn = key < 0 ? Find(frame, Kind::action, key).has_value()
                    : Find(frame, Kind::toggle, key) || Find(frame, Kind::choice, key) || Find(frame, Kind::range, key);
                Check(drawn, "every row of every tab is a control");
            }
        }
    }

    // Setup: an action is a button.
    for (int n = 0; n < 4; ++n) { panel.Grip(true); }
    frame = panel.Idle();
    Check(frame.page == 6, "setup page");
    const auto recenter = Find(frame, Kind::action, options::RecenterView);
    Check(recenter.has_value(), "recenter is offered");
    frame = panel.Click(*recenter);
    Check(frame.changes.size() == 1 && frame.changes[0].recenter && !frame.changes[0].calibrate, "recenter sends its action");
    // Controls: what each action is bound to, read only -- nothing on it is a
    // control but the tabs and Back, so the trigger anywhere on it grabs.
    frame = panel.Grip(true);
    Check(frame.page == options::ControlsPage(), "right grip: the Controls tab");
    for (const auto& target : frame.targets) {
        Check(target.kind == Kind::tab || target.kind == Kind::close || target.kind == Kind::scrollbar,
              "the Controls tab, waiting for the controllers, has no control");
    }
    {
        std::vector<VrOptionsControlRow> rows{{"PLAY", "", true}, {"Jump", "Right Hand A Button", false},
                                              {"Sprint", "", false}};
        for (int n = 0; n < 40; ++n) { rows.push_back({"Row", "Left Hand X Button / Right Hand Y Button", false}); }
        panel.ui.SetControls(rows);
        panel.Idle();   // ImGui sizes the scroll range from the frame before
        frame = panel.Idle();
        Check(frame.texture != nullptr && frame.page == options::ControlsPage() && frame.scrollMaxY > 0,
              "the bindings drawn, a long list scrolling");
        for (const auto& target : frame.targets) {
            Check(target.kind == Kind::tab || target.kind == Kind::close || target.kind == Kind::scrollbar,
                  "and still no control but the tabs, Back and the scrollbar");
        }
    }
    frame = panel.Grip(true);
    Check(frame.page == options::MenuPages() - 1 && Find(frame, Kind::toggle, options::CarryHand) &&
          Find(frame, Kind::toggle, options::MuzzleAim) && Find(frame, Kind::range, options::InventoryDepth),
          "right grip: the experimental tab, last");
    frame = panel.Grip(true);
    Check(frame.page == 0, "right grip wraps to the first tab");
    frame = panel.Grip(false);
    Check(frame.page == options::MenuPages() - 1, "left grip wraps to the last");

    // Back to the game menu.
    frame = panel.Click(*Find(frame, Kind::close));
    Check(frame.close && frame.changes.empty(), "back closes and changes nothing");

    // The other family: the same picture with R and B swapped.
    panel.format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    panel.Idle(); // a new family is a new target and context; tabs lay out on the second frame
    frame = panel.Idle();
    Check(frame.texture != nullptr && frame.page == 0, "R8G8B8A8 presentation, from the first page");
    frame.texture->GetDesc(&desc);
    Check(desc.Format == DXGI_FORMAT_R8G8B8A8_TYPELESS, "target follows the presentation family");
    const auto rgba = Pixel(device.Get(), context.Get(), frame.texture, tabX, tabY);
    Check(rgba[0] == bgra[2] && rgba[2] == bgra[0] && rgba[1] == bgra[1], "same colour in RGBA order");

    // Refusals leave a reason and no texture; Reset starts afresh.
    panel.format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    frame = panel.Idle();
    Check(frame.texture == nullptr && !panel.ui.LastError().empty(), "an unsupported format is refused with a reason");
    panel.format = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
    Check(panel.ui.Render(nullptr, context.Get(), panel.format, {}, panel.values, false, {}).texture == nullptr,
          "no device, no frame");
    panel.ui.Reset();
    frame = panel.ui.Render(device.Get(), context.Get(), panel.format, {}, panel.values, true, L"");
    Check(frame.texture != nullptr && frame.page == 0, "drawn again after a reset, from the first page");

    std::cout << "VR Options panel: target, pixels, state restore, widgets by kind, sliders on release, "
                 "menu placement, every tab's rows, scrolling, grabbing, grips, actions and back passed\n";
    return 0;
}
