#pragma once
#include "preyvr/VrMath.h"
#include <array>
#include <cstdint>
#include <optional>

namespace preyvr::ui {
struct Eye { Pose pose{}; float left=0, right=0, up=0, down=0; };
struct Panel { Pose pose{}; float width=0, height=0; };
// Fits all four corners inside BOTH eye frusta, including asymmetric/canted views.
// The runtime FOV is an optical bound, not a visibility-mask guarantee; the
// tangent margin and angular caps deliberately reserve room around the panel.
// `scale` shrinks the fit below the angular caps. It exists because the caps
// are 60 deg horizontal / 40 vertical and the corner test only shrinks until the
// corners fit the frustum the RUNTIME REPORTS -- which on a Quest 3 is wider
// than the lenses actually show, so a panel that passes the test is still
// cropped at the edges by the optics. A wearer is the only instrument that can
// settle where the real edge is, so this is a dial rather than a constant.
// Clamped to [0.2, 2.0]. Above 1 exceeds the default caps deliberately: they are
// a comfort choice, not a limit, and the corner test still refuses a panel whose
// corners leave the frustum. At a portrait render aspect the vertical cap binds
// and forces the panel narrow, which is precisely when a wearer needs more.
//
// `margin` is the fraction of the reported frustum a panel must stay inside,
// and it is the REAL ceiling on size -- not `scale`. Both fit loops shrink
// until every sampled point sits within it, so once the margin binds, raising
// `scale` changes nothing at all: a wearer at 2688x2880 found the panel stopped
// growing at about 130 percent and reasonably read that as the maximum.
// Default 0.72, clamped to [0.3, 1.0]. Raising it trades the safety comment
// above for size, which is the wearer's trade to make and is reversible.
inline constexpr float kDefaultFitMargin=.72f;
std::optional<Panel> FitPanel(const Pose& head, const std::array<Eye,2>& eyes,
                             float aspect, float distance=2.0f, float scale=1.0f,
                             float margin=kDefaultFitMargin);
bool CornersVisible(const Panel& panel, const std::array<Eye,2>& eyes,
                    float margin=kDefaultFitMargin);
// The head turned only about the vertical (OpenXR +Y): its position and the way
// the face is turned, without pitch or roll -- unaffected by either, and still
// defined looking straight down or up. A menu fitted from it sits at eye level and
// faces the wearer square-on, however the head was tilted when it opened --
// fitted from the head itself it came out low, tilted and rolled with it.
// A non-finite head is returned unchanged, for FitPanel to refuse.
Pose UprightHead(const Pose& head);
// The eyes as they would be with the head upright: each carried by the rotation
// that takes `head` to `upright`, about the head. Fitting with these sizes the
// panel for a level gaze; with the live eyes, a level panel opened while the
// wearer looked down failed the corner test and was refused outright.
std::array<Eye,2> LevelledEyes(const Pose& head, const Pose& upright,
                               const std::array<Eye,2>& eyes);
// `pose` carried around the inside of a cylinder about the head's vertical:
// turned by `right` (positive to the wearer's right), then slid straight up
// until its centre sits `up` above eye level as seen from the head (positive
// up, height = horizontal distance x tan(up)). Radians, from the upright head.
// The horizontal distance is kept and the panel stays upright -- never pitched
// or rolled -- facing the head's vertical line. Fit first, then offset: the fit
// sizes for straight ahead, so the offset moves a panel without resizing it.
// The same cylinder as PanelPlacement, which it is worked through. Non-finite angles, or `up` of 1.55 rad (about 89 degrees) or more either way,
// leave `pose` unchanged.
Pose AroundHead(const Pose& upright, const Pose& pose, float right, float up);
// **Where menus are placed from**: the upright head and the levelled eyes,
// taken when a menu opens or the view is recentred, and shared by every menu.
// Re-placing one after a setting changes keeps the anchor, so an offset is
// measured from where the wearer faced on opening -- never from where they
// look now, which, while looking at an offset menu, would add the offset again.
struct MenuAnchor { Pose upright{}; std::array<Eye,2> eyes{}; };
MenuAnchor AnchorMenu(const Pose& head, const std::array<Eye,2>& eyes);
// A menu fitted from an anchor: sized for a level gaze straight ahead at
// `distance` (FitPanel), then carried by `right`/`up` around the cylinder
// (AroundHead). Nothing when the fit refuses.
std::optional<Panel> PlaceMenu(const MenuAnchor& anchor, float aspect, float distance, float scale,
                               float margin, float right, float up);
// **`panel` scaled about `eye` by `ratio`**: its centre `ratio` times as far
// along the line from the eye, its width and height `ratio` times as big, facing
// the same way. From `eye` it covers the same angle, so it looks the same size.
// Unchanged for a non-finite or non-positive ratio.
Panel ScaleAbout(const Panel& panel, const Vec3& eye, float ratio);
// **VR Options sits in front of the game menu** (VrOptionsLayer): fitted where
// the menu is (ui.distance), then scaled about the anchor to this fraction of
// that distance. At the menu's own depth the two clashed: the curve brings a
// menu's sides nearer than a flat panel there, so it stood in front of the panel
// drawn over it. Every menu is fitted to an angle, so that bulge grows with its
// distance, and one fraction clears it at every ui.distance, curve and size
// (UiPanelTests).
inline constexpr float kOptionsDepthRatio=.85f;
// The head without roll, its pitch held within +-`maxPitch` radians: where a
// popup opens, in front of the gaze. Looking down at a note on a desk puts its
// panel down there, tilted back to face the eyes, where a menu would go to eye
// level; past the limit it stops, so a note opened looking at the floor is not
// laid flat on it. The way the face is turned is UprightHead's, so a look
// straight down or up is handled as it is there. A non-finite head is returned
// unchanged, for FitPanel to refuse.
Pose GazeHead(const Pose& head, float maxPitch);
// **A popup held where it opened** (the note panel: Prey's notes and books).
// The anchor -- the gaze head and the eyes carried with it -- is taken on the
// first frame the popup is shown and kept while it stays up, so turning the
// head looks around the page instead of carrying it along. It is let go once
// the popup has been gone for longer than kPopupGraceNs, which a frame or two
// the capture misses never is, so the next opening is placed afresh in front of
// the wearer. A completed recenter (an even reference generation other than the
// anchor's; odd is one in progress) places it afresh too, as it does a menu.
inline constexpr float kPopupMaxPitch=.6108652f; // 35 degrees
inline constexpr std::uint64_t kPopupGraceNs=300'000'000;
struct PopupAnchor { Pose gaze{}; std::array<Eye,2> eyes{}; };
PopupAnchor AnchorPopup(const Pose& head, const std::array<Eye,2>& eyes);
class PopupHold {
public:
    // Once per frame. `shown`: the popup has an image this frame. The anchor to
    // place it from, or nothing when it is not shown -- or when it has never
    // been seen with a usable head, which is not taken as an anchor.
    const PopupAnchor* Update(bool shown, std::uint64_t nowNs, const Pose& head,
                              const std::array<Eye,2>& eyes, std::uint64_t reference);
    bool Held() const { return anchor_.has_value(); }
    void Reset() { anchor_.reset(); }
private:
    std::optional<PopupAnchor> anchor_;
    std::uint64_t lastShownNs_=0, reference_=0;
};
// A popup fitted from its anchor: straight ahead of the gaze at `distance`,
// facing it (FitPanel). The same anchor gives the same panel, so re-placing it
// every frame keeps it still; a changed size or distance re-places it from where
// it opened, not from where the wearer now looks.
std::optional<Panel> PlacePopup(const PopupAnchor& anchor, float aspect, float distance, float scale,
                                float margin);
// angle=0 is a plane; otherwise width is arc length. The panel pose always
// denotes the visible surface centre, while OpenXR's cylinder pose is its axis.
struct Surface { Panel panel{}; float angle=0; };
struct RayHit { float u=0,v=0,distance=0; Vec3 point{}; bool inside=false; };
// **A panel dragged by the laser** (VR Options, XrSessionHost). It moves round
// the inside of a cylinder about the head's vertical line, as the menus'
// offsets do: the laser's point on that cylinder carries the spot it was
// grabbed by; the panel stays upright, turned to face the line -- raised or
// lowered, it slides, never tilting toward the eyes -- and keeps its physical
// size, so nearer reads larger. `radius` is the cylinder's, the stick's push
// and pull; x and y are the grabbed spot from the panel's centre, in its plane.
struct PanelGrab { Panel panel{}; float x=0,y=0,radius=0; };
inline constexpr float kGrabRadiusMin=.5f,kGrabRadiusMax=5.f;
// From where the laser hit the panel, and the head it is seen from. Nothing
// for a hit off the panel.
std::optional<PanelGrab> GrabPanel(const Panel& panel,const RayHit& hit,const Vec3& head);
// The panel where `aim` now meets the cylinder about `head`. No higher or
// lower than 60 degrees from the head. Nothing when the laser runs along the
// axis (straight up or down); a hand outside the cylinder widens it.
std::optional<Panel> DragPanel(const PanelGrab& grab,const Pose& aim,const Vec3& head);
// **Where a panel sits relative to an anchor**, so a later anchor -- the next
// opening, a recenter -- puts it back in the same place relative to the
// wearer: turned `right` radians round the anchor's vertical (positive to the
// right), `height` metres above its eye level, `radius` metres away across the
// floor, upright and facing it.
struct PanelPlacement { float right=0,height=0,radius=2; };
PanelPlacement PlacementOf(const MenuAnchor& anchor,const Panel& panel);
Pose PlacedPose(const MenuAnchor& anchor,const PanelPlacement& placement);
Pose CylinderAxis(const Surface& surface);
Pose SurfacePoint(const Surface& surface,float u,float v);
std::optional<RayHit> Intersect(const Surface& surface,const Pose& aim);
bool SurfaceVisible(const Surface& surface,const std::array<Eye,2>& eyes,
                    float margin=kDefaultFitMargin);
// Neutral on entry; losing the target releases outside it, never clicks the
// last hovered item. A captured drag can travel beyond the screen boundary.
struct PointerButtons {
    bool armed=false,down=false;
    int Update(bool active,bool inside,bool pressed); // -1 release, +1 press
};
// Unclipped canvas position of a head-relative ray on a front-facing HUD plane.
std::optional<std::array<float,2>> PanelFraction(float tanX,float tanY,float width,float height,float distance);
// **A quad centred on `center`, facing `eye` square-on and upright**: its face
// (+Z) toward the eye, its edges level with the floor (no roll), `width` by
// `height`. The world reticle (XrSessionHost) at the aim's hit point. Nothing
// for a non-finite input, a non-positive size, or the eye at the centre; seen
// from straight above or below, its right is +X.
std::optional<Panel> FacingQuad(const Vec3& center, const Vec3& eye, float width, float height);
// The width that keeps a texture's angular size when it is moved from the HUD
// panel (`panelWidth` wide at `panelDistance`) to `distance` away, times
// `scale`: what the world reticle is drawn at, so it looks the size it did on
// the HUD. Zero for a non-finite or non-positive input.
float AngularWidth(float distance, float panelWidth, float panelDistance, float scale);

// **The laser's two quads**, from the controller's aim and what it hit. The beam
// is 2 mm wide, from the aim along its forward (-Z) to the hit, solid -- or,
// hitting nothing, kLaserFadeLength long and `fades` (LaserFade) -- turned
// about its own axis to face `head`. The cursor is 13 mm square (20 mm
// while `pressed`), 2 mm in front of `surface` -- the one `hit` lies on -- at
// the hit's (u, v), only when it is inside. Drawn the same in Prey's frames and in hold frames
// (XrSessionHost), so a laser during a load looks like the live one. No beam
// when the head is on its axis.
struct PointerQuads { std::optional<Panel> beam, cursor; bool fades=false; };
PointerQuads PointerQuadsFor(const Pose& aim, const std::optional<RayHit>& hit, bool pressed,
                             const Surface& surface, const Vec3& head);
// **A beam that hits nothing fades out along its length**, as the headset's own
// laser does outside the game, rather than stopping short: its opacity at
// `along`, 0 at the controller to 1 at the tip -- solid for the first tenth, then
// easing to nothing. Without depth testing between layers, a longer solid beam
// would be drawn over a menu it passes behind; faded, its far end is too faint
// to matter.
inline constexpr float kLaserFadeLength=1.f;
float LaserFade(float along);

// **What one XR frame submits, bottom to top** (XrSessionHost). The game's menu
// panel and the world are exclusive. VR Options goes over either -- the running
// game or a game menu, its stereo half and guide card -- under the laser. While
// it is open the HUD, wheel, note and text panels stay out (they are still
// captured, so they do not fall back into the world), as they would sit in front
// of it unseen. The reticle is over the HUD it came out of and under the wheel,
// which hides it anyway; the note panel under the text panel: a subtitle reads
// over an open book.
// The wrist, slot and fade layers are their callers' to gate: they pause while
// VR Options has the controls, which is more than not being drawn.
enum class FrameLayer : unsigned char {
    world, menu, menuCylinder, menuRightEye, guide, hud, reticle, wheel, note, text, options, beam, cursor, wrist,
    slot, fade };
struct FrameShown {
    bool menu=false,menuCylinder=false,menuRightEye=false,guide=false,hud=false,reticle=false,wheel=false,note=false;
    bool text=false,options=false,beam=false,cursor=false,wrist=false,slot=false,fade=false;
};
struct FrameLayers { std::array<FrameLayer,12> layers{}; unsigned count=0; };
FrameLayers ComposeFrame(const FrameShown& shown);
}
