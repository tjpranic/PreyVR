#pragma once

#include <algorithm>
#include <cmath>

// **The wrist status's fixed frame** (docs/WRIST-HUD-SCOPE-2026-11-11.md). The
// meters are no longer fitted to whatever Prey shows: for the wrist's replay
// alone, quadrant_SW is placed and scaled so one fixed frame of it lands in a
// known rectangle of the capture, and that rectangle becomes the card. Status
// effects, an equipped power and longer bars after an upgrade appear inside it
// as Prey lays them out, and nothing changes size. Only Wrist Display Size does.
// Pure, so every rule is tested offline.
namespace preyvr::wrist {

// A box in quadrant_SW's own units, y down as in Flash. Its origin is the
// quadrant's bottom-left corner as Prey places it.
struct Box {
    double left = 0, top = 0, right = 0, bottom = 0;
    constexpr double Width() const { return right - left; }
    constexpr double Height() const { return bottom - top; }
    constexpr double CentreX() const { return (left + right) * 0.5; }
    constexpr double CentreY() const { return (top + bottom) * 0.5; }
};

// What the frame holds, read from the patched Steam HUD movie (authored
// placements and shape bounds, 2026-11-11):
//   the meters' icons from x 89, their bars from x 130-158; y -112 to 0
//   (health on top, psi at the bottom);
//   each bar (100 + max) x its scale long, health up to 360 at max health 300,
//   so its end reaches x 618 with a power equipped, before its number;
//   the equipped power's icon (iconSize 100 at 0.9) about x 1 to 91 and
//   y -91 to -1, its noise overlay x -28 to 120, y -114 to 22, with the meters
//   moved 100 right while it shows (EquippedPower.powerOpen);
//   status effects in a row just above the meters, y -212 to -111, one icon
//   every 50 units to the right (StatusEffects.xOffset) as more are active.
// **Wide margins on every side** (2026-11-11, PreyVR-151): the first frame,
// -40,-220 to 640,30, left only 8 to 22 units past those -- icons pop in and
// glow past their resting size, numbers follow the bars, and effects stack. The
// frame is invisible, so a wide one costs only texture; nothing in it changes
// size. (Most of what the wearer saw cut off was Place's stretch across, fixed
// in PreyVR-152: see CoverScale.)
// wrist.report's content= is what a capture really held, in these units.
inline constexpr Box kFrame{-200.0, -380.0, 900.0, 100.0};
// The meters' middle with no power and no effect: the point the placement puts
// on the forearm (Wrist Display Position and Height).
inline constexpr double kAnchorX = 200.0, kAnchorY = -56.0;
// Half the meters' length along the reading direction, for the occlusion test.
inline constexpr double kMetersHalfSpan = 130.0;
// The card's size at Wrist Display Size 100%: about the meters' size when the
// card was fitted to them, with no power and no effect.
inline constexpr double kMetresPerUnit = 0.00032;
// The card's texture: the frame at 2 pixels per unit, the frame's aspect
// (LayerSwapchain needs at least 640 x 480). On a 2644 eye target the frame is
// captured at the same density, an exact copy.
inline constexpr unsigned kTextureWidth = 2200, kTextureHeight = 960;
static_assert(kTextureWidth * kFrame.Height() == kTextureHeight * kFrame.Width(), "the texture keeps the frame's aspect");

// The HUD movie as the replay finds it, and the capture it draws into.
struct Stage {
    double halfWidth = 0, halfHeight = 0;     // _root.cam.m_halfStageWidth / Height, stage units
    double safeScaleX = 100, safeScaleY = 100; // _root.safe._xscale / _yscale, percent
    unsigned width = 0, height = 0;            // the capture, pixels
};

// What the replay writes, and where the frame then is.
struct Layout {
    bool valid = false;
    double quadrantScaleX = 0, quadrantScaleY = 0; // quadrant_SW._xscale / _yscale, percent
    double quadrantX = 0, quadrantY = 0;           // quadrant_SW._x / _y, in safe's units
    double safeX = 0, safeY = 0;                   // _root.safe._x / _y: the stage centre, so no HUD sway
    float rect[4]{};                               // the frame in the capture: x0, y0, x1, y1, pixels
    double pixelsPerUnit = 0;
};

// **The stage is scaled uniformly to COVER the capture, and centred** (R-118,
// R-122, F-017 in RESEARCH_LOG.md; aim::ViewportToHudCanvas): the HUD's canvas,
// 1920 x 1080 (Stage.width / height, so twice m_halfStageWidth / Height), takes
// whichever axis ratio is larger, and the other axis overflows the capture. On a
// square eye target that is 2644 / 1080 in both axes. (PreyVR-150 and 151 took
// each axis's own ratio, 2644 / 1920 across: the status came out 1.78x too wide,
// stretched and cut off at the sides.) The frame is centred in the capture, at
// the texture's density, or less when the capture is too small to hold it.
inline double CoverScale(const Stage& s)
{
    return std::max(s.width / (2.0 * s.halfWidth), s.height / (2.0 * s.halfHeight));
}
inline Layout Place(const Stage& s, const Box& frame = kFrame)
{
    Layout out{};
    const bool usable = std::isfinite(s.halfWidth) && std::isfinite(s.halfHeight) && s.halfWidth > 1.0 &&
                        s.halfHeight > 1.0 && std::isfinite(s.safeScaleX) && std::isfinite(s.safeScaleY) &&
                        s.safeScaleX > 1.0 && s.safeScaleY > 1.0 && s.width >= 16 && s.height >= 16 &&
                        frame.Width() > 0.0 && frame.Height() > 0.0;
    if (!usable) { return out; }
    const double width = s.width, height = s.height;
    const double cover = CoverScale(s);   // capture pixels per stage unit, both axes
    double p = std::min(kTextureWidth / frame.Width(), kTextureHeight / frame.Height());
    p = std::min({p, 0.95 * width / frame.Width(), 0.95 * height / frame.Height()});
    out.quadrantScaleX = 100.0 * p / (cover * s.safeScaleX * 0.01);
    out.quadrantScaleY = 100.0 * p / (cover * s.safeScaleY * 0.01);
    out.quadrantX = -frame.CentreX() * out.quadrantScaleX * 0.01;
    out.quadrantY = -frame.CentreY() * out.quadrantScaleY * 0.01;
    out.safeX = s.halfWidth;
    out.safeY = s.halfHeight;
    out.rect[0] = static_cast<float>(width * 0.5 - frame.Width() * 0.5 * p);
    out.rect[1] = static_cast<float>(height * 0.5 - frame.Height() * 0.5 * p);
    out.rect[2] = static_cast<float>(width * 0.5 + frame.Width() * 0.5 * p);
    out.rect[3] = static_cast<float>(height * 0.5 + frame.Height() * 0.5 * p);
    out.pixelsPerUnit = p;
    out.valid = true;
    return out;
}

// A box in capture pixels (x0, y0, x1, y1), through the frame's rectangle, in
// quadrant units: what wrist.report's content= shows.
inline Box ToFrameUnits(const float (&rect)[4], const float (&pixels)[4], const Box& frame = kFrame)
{
    const double w = rect[2] - rect[0], h = rect[3] - rect[1];
    if (!(w > 0.0) || !(h > 0.0)) { return {}; }
    const auto x = [&](float v) { return frame.left + (v - rect[0]) / w * frame.Width(); };
    const auto y = [&](float v) { return frame.top + (v - rect[1]) / h * frame.Height(); };
    return {x(pixels[0]), y(pixels[1]), x(pixels[2]), y(pixels[3])};
}

// Whether `content` reaches past the frame by more than `margin` units.
inline bool Outside(const Box& content, const Box& frame = kFrame, double margin = 1.0)
{
    return content.left < frame.left - margin || content.top < frame.top - margin ||
           content.right > frame.right + margin || content.bottom > frame.bottom + margin;
}

// The card, metres, at Wrist Display Size `percent`: its size, and where its
// centre sits from the anchor's point, along its reading direction (+X) and its
// up (+Y, the reverse of Flash's y).
struct CardSize {
    float width = 0, height = 0, offsetX = 0, offsetY = 0;
};
inline CardSize Size(double percent, const Box& frame = kFrame)
{
    const double k = (std::isfinite(percent) ? std::clamp(percent, 10.0, 400.0) : 100.0) * 0.01 * kMetresPerUnit;
    return {static_cast<float>(frame.Width() * k), static_cast<float>(frame.Height() * k),
            static_cast<float>((frame.CentreX() - kAnchorX) * k), static_cast<float>(-(frame.CentreY() - kAnchorY) * k)};
}
// Half the meters' length, metres, at `percent`.
inline float MetersHalfSpan(double percent)
{
    return static_cast<float>((std::isfinite(percent) ? std::clamp(percent, 10.0, 400.0) : 100.0) * 0.01 *
                              kMetresPerUnit * kMetersHalfSpan);
}

}  // namespace preyvr::wrist
