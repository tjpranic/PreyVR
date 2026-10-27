#include "preyvr/UiPanel.h"
#include "preyvr/StereoCamera.h"
#include <algorithm>
#include <cmath>

namespace preyvr::ui {
Pose CylinderAxis(const Surface& s) {
    return s.angle>0 ? Compose(s.panel.pose,Pose{{},{0,0,s.panel.width/s.angle}}) : s.panel.pose;
}
Pose SurfacePoint(const Surface& s,float u,float v) {
    if(s.angle<=0)return Compose(s.panel.pose,Pose{{},{(u-.5f)*s.panel.width,(.5f-v)*s.panel.height,0}});
    const float a=(u-.5f)*s.angle,r=s.panel.width/s.angle;
    // The tangent faces the cylinder axis (the viewer is on the inside).
    return Compose(CylinderAxis(s),Pose{{0,-std::sin(a*.5f),0,std::cos(a*.5f)},
        {r*std::sin(a),(.5f-v)*s.panel.height,-r*std::cos(a)}});
}
int PointerButtons::Update(bool active,bool inside,bool pressed) {
    if(!active){armed=false;if(down){down=false;return -1;}return 0;}
    if(!pressed){armed=true;if(down){down=false;return -1;}return 0;}
    if(armed){armed=false;if(inside){down=true;return 1;}}
    return 0;
}
std::optional<std::array<float,2>> PanelFraction(float tanX,float tanY,float width,float height,float distance) {
    if(!std::isfinite(tanX)||!std::isfinite(tanY)||!std::isfinite(width)||!std::isfinite(height)||
       !std::isfinite(distance)||width<=0||height<=0||distance<=0)return {};
    const std::array<float,2> result{.5f+tanX*distance/width,.5f-tanY*distance/height};
    if(!std::isfinite(result[0])||!std::isfinite(result[1]))return {};
    return result;
}
std::optional<Panel> FacingQuad(const Vec3& center, const Vec3& eye, float width, float height) {
    const auto finite=[](const Vec3& v){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);};
    if(!finite(center)||!finite(eye)||!std::isfinite(width)||!std::isfinite(height)||!(width>0)||!(height>0))return {};
    Vec3 z{eye.x-center.x,eye.y-center.y,eye.z-center.z};
    const float length=std::sqrt(z.x*z.x+z.y*z.y+z.z*z.z);
    if(!(length>1e-4f))return {};
    z={z.x/length,z.y/length,z.z/length};
    // Right is level: across the floor, perpendicular to the line of sight.
    Vec3 x{z.z,0,-z.x}; // up (0,1,0) cross z
    float n=std::sqrt(x.x*x.x+x.z*x.z);
    if(!(n>1e-4f)){x={1,0,0};n=1;} // straight above or below: any level right will do
    x={x.x/n,0,x.z/n};
    const Vec3 y{z.y*x.z-z.z*x.y,z.z*x.x-z.x*x.z,z.x*x.y-z.y*x.x}; // z cross x
    return Panel{Pose{Normalize(stereo::QuaternionFromBasis(x,y,z)),center},width,height};
}
float AngularWidth(float distance, float panelWidth, float panelDistance, float scale) {
    if(!std::isfinite(distance)||!std::isfinite(panelWidth)||!std::isfinite(panelDistance)||!std::isfinite(scale)||
       !(distance>0)||!(panelWidth>0)||!(panelDistance>0)||!(scale>0))return 0;
    return distance*panelWidth/panelDistance*scale;
}
PointerQuads PointerQuadsFor(const Pose& aim, const std::optional<RayHit>& hit, bool pressed,
                             const Surface& surface, const Vec3& head) {
    PointerQuads quads;
    const bool onSurface=hit&&hit->inside;
    if(onSurface) {
        const float size=pressed?.020f:.013f;
        quads.cursor=Panel{Compose(SurfacePoint(surface,hit->u,hit->v),Pose{{},{0,0,.002f}}),size,size};
    }
    quads.fades=!onSurface;
    const float length=onSurface?hit->distance:kLaserFadeLength;
    const auto y=Rotate(aim.orientation,{0,0,-1});
    const Vec3 centre{aim.position.x+y.x*length*.5f,aim.position.y+y.y*length*.5f,aim.position.z+y.z*length*.5f};
    const Vec3 toHead{head.x-centre.x,head.y-centre.y,head.z-centre.z};
    Vec3 x{y.y*toHead.z-y.z*toHead.y,y.z*toHead.x-y.x*toHead.z,y.x*toHead.y-y.y*toHead.x};
    const float norm=std::sqrt(x.x*x.x+x.y*x.y+x.z*x.z);
    if(norm>.0001f) {
        x={x.x/norm,x.y/norm,x.z/norm};
        const Vec3 z{x.y*y.z-x.z*y.y,x.z*y.x-x.x*y.z,x.x*y.y-x.y*y.x};
        quads.beam=Panel{Pose{stereo::QuaternionFromBasis(x,y,z),centre},.002f,length};
    }
    return quads;
}
float LaserFade(float along) {
    if(!std::isfinite(along))return 0;
    return 1-SmoothStep((along-.1f)/.9f);
}
namespace {
bool Valid(const Pose& p) {
    PoseValidity v{true,true,true,true,0};
    return IsPoseUsable(p,v,1);
}
bool Valid(const Eye& e) {
    return Valid(e.pose) && std::isfinite(e.left) && std::isfinite(e.right) &&
        std::isfinite(e.up) && std::isfinite(e.down) &&
        e.left < 0 && e.left > -1.55f && e.right > 0 && e.right < 1.55f &&
        e.down < 0 && e.down > -1.55f && e.up > 0 && e.up < 1.55f;
}
}
std::optional<RayHit> Intersect(const Surface& s,const Pose& aim) {
    if(!Valid(s.panel.pose)||!Valid(aim)||!std::isfinite(s.panel.width)||!std::isfinite(s.panel.height)||
       s.panel.width<=0||s.panel.height<=0||!std::isfinite(s.angle)||s.angle<0||s.angle>1.2f)return {};
    const auto axis=CylinderAxis(s);
    const auto inv=stereo::Conjugate(axis.orientation);
    const auto o=Rotate(inv,{aim.position.x-axis.position.x,aim.position.y-axis.position.y,aim.position.z-axis.position.z});
    const auto d=Rotate(inv,Rotate(aim.orientation,{0,0,-1}));
    float t=0,u=0;
    if(s.angle==0) {
        if(d.z>=-.00001f||o.z<=0)return {};
        t=-o.z/d.z;u=.5f+(o.x+t*d.x)/s.panel.width;
    } else {
        const float r=s.panel.width/s.angle;
        const float a=d.x*d.x+d.z*d.z,b=o.x*d.x+o.z*d.z,c=o.x*o.x+o.z*o.z-r*r;
        const float discriminant=b*b-a*c;
        if(a<.000001f||discriminant<0)return {};
        // Exit intersection hits the inside face. The entry root is back-facing.
        t=(-b+std::sqrt(discriminant))/a;
        u=.5f+std::atan2(o.x+t*d.x,-(o.z+t*d.z))/s.angle;
    }
    if(!std::isfinite(t)||t<=.01f||t>10)return {};
    const float v=.5f-(o.y+t*d.y)/s.panel.height;
    const auto direction=Rotate(aim.orientation,{0,0,-1});
    return RayHit{u,v,t,{aim.position.x+t*direction.x,aim.position.y+t*direction.y,aim.position.z+t*direction.z},
        u>=0&&u<=1&&v>=0&&v<=1};
}
// A non-finite margin is refused rather than clamped: it means the caller lost
// track of the value, and silently substituting a default would hide that.
bool ClampMargin(float& margin) {
    if(!std::isfinite(margin))return false;
    if(margin>1)margin=1; if(margin<.3f)margin=.3f;
    return true;
}
bool SurfaceVisible(const Surface& s,const std::array<Eye,2>& eyes,float margin) {
    if(!ClampMargin(margin))return false;
    if(!Valid(s.panel.pose)||!std::isfinite(s.panel.width)||!std::isfinite(s.panel.height)||
       s.panel.width<=0||s.panel.height<=0||!std::isfinite(s.angle)||s.angle<0||s.angle>1.2f)return false;
    for(const auto& eye:eyes) {
        if(!Valid(eye))return false;
        for(int i=0;i<=16;++i)for(float v:{0.f,1.f}) {
            const auto p=SurfacePoint(s,static_cast<float>(i)/16,v).position;
            const auto q=Rotate(stereo::Conjugate(eye.pose.orientation),{p.x-eye.pose.position.x,p.y-eye.pose.position.y,p.z-eye.pose.position.z});
            if(q.z>=-.01f)return false;
            const float x=q.x/-q.z,y=q.y/-q.z;
            if(x<std::tan(eye.left)*margin||x>std::tan(eye.right)*margin||y<std::tan(eye.down)*margin||y>std::tan(eye.up)*margin)return false;
        }
    }
    return true;
}
bool CornersVisible(const Panel& panel, const std::array<Eye,2>& eyes, float margin) {
    if (!ClampMargin(margin)) return false;
    if (!Valid(panel.pose) || !std::isfinite(panel.width) || !std::isfinite(panel.height) ||
        panel.width <= 0 || panel.height <= 0) return false;
    for (const auto& eye : eyes) {
        if (!Valid(eye)) return false;
        for (float x : {-0.5f,0.5f}) for (float y : {-0.5f,0.5f}) {
            const auto world = Compose(panel.pose, Pose{{},{x*panel.width,y*panel.height,0}}).position;
            const Vec3 delta{world.x-eye.pose.position.x,world.y-eye.pose.position.y,
                             world.z-eye.pose.position.z};
            const auto local=Rotate(stereo::Conjugate(eye.pose.orientation),delta);
            if (local.z >= -.01f) return false;
            const float tx=local.x/-local.z, ty=local.y/-local.z;
            if (tx < std::tan(eye.left)*margin || tx > std::tan(eye.right)*margin ||
                ty < std::tan(eye.down)*margin || ty > std::tan(eye.up)*margin) return false;
        }
    }
    return true;
}
std::optional<Panel> FitPanel(const Pose& head, const std::array<Eye,2>& eyes,
                             float aspect, float distance, float scale, float margin) {
    if (!Valid(head) || !Valid(eyes[0]) || !Valid(eyes[1]) ||
        !std::isfinite(aspect) || aspect < .25f || aspect > 5 ||
        !std::isfinite(distance) || distance < 1 || distance > 4 ||
        !std::isfinite(scale) || !std::isfinite(margin)) return {};
    ClampMargin(margin);
    // **Above 1 is allowed on purpose.** The 60/40 caps are a comfort default,
    // not a safety limit -- the corner test below still refuses anything whose
    // corners leave the frustum, so a wearer asking for a bigger panel cannot
    // push it somewhere invisible. Clamping at 1 made the dial one-directional,
    // and at a portrait render aspect the vertical cap binds and forces the
    // panel narrow, which is exactly when a wearer needs to go the other way.
    if (scale > 2) scale = 2; if (scale < .2f) scale = .2f;
    Panel panel{};
    panel.pose=Compose(head,Pose{{},{0,0,-distance}});
    // At most 60 degrees horizontally / 40 vertically, times the wearer's scale.
    // The corner test below only shrinks until the corners fit the frustum the
    // runtime REPORTS, which is an optical bound rather than what the lenses
    // show -- so passing it is not the same as being visible.
    panel.width=scale*std::min(2*distance*std::tan(.523598776f),
                         2*distance*std::tan(.34906585f)*aspect);
    panel.height=panel.width/aspect;
    for (int attempt=0;attempt<80;++attempt) {
        if (CornersVisible(panel,eyes,margin)) return panel;
        panel.width*=.95f; panel.height*=.95f;
    }
    return {};
}
Pose UprightHead(const Pose& head) {
    if (!Valid(head)) return head;
    // The forward axis projected flat is immune to roll (it turns about that
    // axis) and to pitch. armpose::HeadYaw is not used: it mixes in the up
    // vector at every pitch, which roll tips sideways -- 13 degrees off at 40
    // down and 25 rolled. Only near straight down or up, where the projection
    // vanishes, does the up vector stand in (it points the way the face is
    // turned looking down, away from it looking up).
    const auto forward=Rotate(head.orientation,Vec3{0,0,-1});
    float x=forward.x,z=forward.z;
    if (x*x+z*z<.01f) {
        const auto up=Rotate(head.orientation,Vec3{0,1,0});
        const float sign=forward.y<0?1.f:-1.f;
        x=up.x*sign;z=up.z*sign;
    }
    if (x*x+z*z<1e-8f) return head;
    const float yaw=std::atan2(-x,-z);
    // About +Y: a yaw of t sends -Z to (-sin t, 0, -cos t), as armpose::YawOf reads it.
    return Pose{{0,std::sin(yaw*.5f),0,std::cos(yaw*.5f)},head.position};
}
std::array<Eye,2> LevelledEyes(const Pose& head, const Pose& upright,
                               const std::array<Eye,2>& eyes) {
    if (!Valid(head) || !Valid(upright)) return eyes;
    const auto level=Normalize(Multiply(upright.orientation,stereo::Conjugate(head.orientation)));
    std::array<Eye,2> result=eyes;
    for (auto& eye : result) {
        const Vec3 offset{eye.pose.position.x-head.position.x,eye.pose.position.y-head.position.y,
                          eye.pose.position.z-head.position.z};
        const auto turned=Rotate(level,offset);
        eye.pose.orientation=Normalize(Multiply(level,eye.pose.orientation));
        eye.pose.position={upright.position.x+turned.x,upright.position.y+turned.y,upright.position.z+turned.z};
    }
    return result;
}
Pose AroundHead(const Pose& upright, const Pose& pose, float right, float up) {
    if (!std::isfinite(right) || !std::isfinite(up) || std::fabs(up) >= 1.55f ||
        !Valid(upright) || !Valid(pose)) return pose;
    // On the one cylinder every panel is placed on (PanelPlacement): turned
    // round the upright head's vertical, then slid straight up by its distance
    // across the floor times tan(up). Raising by rotating about X tipped the
    // panel back to face the eyes; sliding keeps it upright, facing the line.
    const MenuAnchor anchor{upright,{}};
    auto placement=PlacementOf(anchor,Panel{pose,1,1});
    placement.right+=right;
    placement.height+=placement.radius*std::tan(up);
    return PlacedPose(anchor,placement);
}
MenuAnchor AnchorMenu(const Pose& head, const std::array<Eye,2>& eyes) {
    const auto upright=UprightHead(head);
    return {upright,LevelledEyes(head,upright,eyes)};
}
std::optional<Panel> PlaceMenu(const MenuAnchor& anchor, float aspect, float distance, float scale,
                               float margin, float right, float up) {
    auto panel=FitPanel(anchor.upright,anchor.eyes,aspect,distance,scale,margin);
    if(panel)panel->pose=AroundHead(anchor.upright,panel->pose,right,up);
    return panel;
}
std::optional<MenuStack> FitMenuStack(Panel layout, float aspect, float guideRatio, float gapRatio, bool guide,
                                      float curve, const std::array<Eye,2>& eyes, float margin) {
    for(int attempt=0;attempt<80;++attempt) {
        auto menu=layout,card=layout;
        menu.height=menu.width/aspect;
        card.height=card.width*guideRatio;
        const float gap=menu.width*gapRatio;
        if(guide) {
            menu.pose=Compose(layout.pose,Pose{{},{0,(card.height+gap)*.5f,0}});
            card.pose=Compose(layout.pose,Pose{{},{0,-(menu.height+gap)*.5f,0}});
        } else {
            menu.pose=layout.pose;
        }
        if(SurfaceVisible({menu,curve},eyes,margin)&&(!guide||SurfaceVisible({card,0},eyes,margin))) {
            MenuStack stack{menu};
            if(guide)stack.guide=card;
            return stack;
        }
        layout.width*=.95f;layout.height*=.95f;
    }
    return std::nullopt;
}
bool WantMenuPanel(unsigned mode, bool haveViews, bool menuKnown, bool menuOpen, bool wasShown) {
    return haveViews && (mode==2 || (mode==1 && (menuKnown?menuOpen:wasShown)));
}
Panel ScaleAbout(const Panel& panel, const Vec3& eye, float ratio) {
    if(!std::isfinite(ratio)||!(ratio>0))return panel;
    const auto& p=panel.pose.position;
    return Panel{Pose{panel.pose.orientation,{eye.x+(p.x-eye.x)*ratio,eye.y+(p.y-eye.y)*ratio,eye.z+(p.z-eye.z)*ratio}},
                 panel.width*ratio,panel.height*ratio};
}
Pose GazeHead(const Pose& head, float maxPitch) {
    if (!Valid(head) || !std::isfinite(maxPitch)) return head;
    // The pitch from the forward axis, which roll turns about and so cannot
    // change; then tipped about the upright head's own X, so no roll comes back.
    // About +X, a positive angle tips -Z up: (0, sin t, -cos t).
    const auto forward=Rotate(head.orientation,Vec3{0,0,-1});
    const float limit=std::clamp(maxPitch,0.f,1.5f);
    const float pitch=std::clamp(std::asin(std::clamp(forward.y,-1.f,1.f)),-limit,limit);
    const auto upright=UprightHead(head);
    return Pose{Normalize(Multiply(upright.orientation,Quaternion{std::sin(pitch*.5f),0,0,std::cos(pitch*.5f)})),
                head.position};
}
PopupAnchor AnchorPopup(const Pose& head, const std::array<Eye,2>& eyes) {
    const auto gaze=GazeHead(head,kPopupMaxPitch);
    return {gaze,LevelledEyes(head,gaze,eyes)};
}
const PopupAnchor* PopupHold::Update(bool shown, std::uint64_t now, const Pose& head,
                                     const std::array<Eye,2>& eyes, std::uint64_t reference) {
    if (!shown) {
        if (anchor_ && now>lastShownNs_ && now-lastShownNs_>kPopupGraceNs) anchor_.reset();
        return nullptr;
    }
    lastShownNs_=now;
    const bool recentred=!stereo::Recentering(reference) && reference!=reference_;
    if ((!anchor_ || recentred) && Valid(head) && Valid(eyes[0]) && Valid(eyes[1])) {
        anchor_=AnchorPopup(head,eyes);
        reference_=reference;
    }
    return anchor_ ? &*anchor_ : nullptr;
}
std::optional<Panel> PlacePopup(const PopupAnchor& anchor, float aspect, float distance, float scale,
                                float margin) {
    return FitPanel(anchor.gaze,anchor.eyes,aspect,distance,scale,margin);
}
namespace {
// Upright, its face (+Z) turned toward the vertical line through `axis`.
std::optional<Quaternion> FacingAxis(const Vec3& position,const Vec3& axis)
{
    const float x=axis.x-position.x,z=axis.z-position.z;
    if(!(x*x+z*z>1e-8f))return {};
    const float yaw=std::atan2(x,z);
    return Quaternion{0,std::sin(yaw*.5f),0,std::cos(yaw*.5f)};
}
// The anchor's forward and right across the floor.
bool AnchorAxes(const MenuAnchor& anchor,Vec3& forward,Vec3& right)
{
    if(!Valid(anchor.upright))return false;
    const auto f=Rotate(anchor.upright.orientation,Vec3{0,0,-1});
    const float n=std::sqrt(f.x*f.x+f.z*f.z);
    if(!(n>1e-4f))return false;
    forward={f.x/n,0,f.z/n};right={-forward.z,0,forward.x};
    return true;
}
}
std::optional<PanelGrab> GrabPanel(const Panel& panel,const RayHit& hit,const Vec3& head)
{
    if(!hit.inside||!Valid(panel.pose)||!Finite(hit.point)||!Finite(head))return {};
    const auto& c=panel.pose.position;
    const auto local=Rotate(stereo::Conjugate(panel.pose.orientation),
        Vec3{hit.point.x-c.x,hit.point.y-c.y,hit.point.z-c.z});
    const float x=hit.point.x-head.x,z=hit.point.z-head.z;
    return PanelGrab{panel,local.x,local.y,std::clamp(std::sqrt(x*x+z*z),kGrabRadiusMin,kGrabRadiusMax)};
}
std::optional<Panel> DragPanel(const PanelGrab& grab,const Pose& aim,const Vec3& head)
{
    if(!Valid(aim)||!Finite(head)||!(grab.radius>0)||!std::isfinite(grab.x)||!std::isfinite(grab.y))return {};
    const auto d=Rotate(aim.orientation,Vec3{0,0,-1});
    const float ox=aim.position.x-head.x,oz=aim.position.z-head.z;
    // A hand held out past the cylinder would start outside it: widen it to
    // just beyond the hand rather than lose the panel.
    const float r=std::max(grab.radius,std::sqrt(ox*ox+oz*oz)+.1f);
    const float a=d.x*d.x+d.z*d.z,b=ox*d.x+oz*d.z,c=ox*ox+oz*oz-r*r;
    if(a<1e-6f)return {};
    const float t=(-b+std::sqrt(b*b-a*c))/a; // the hand is inside: one root ahead
    Vec3 point{aim.position.x+d.x*t,aim.position.y+d.y*t,aim.position.z+d.z*t};
    const float limit=r*std::tan(1.05f);
    point.y=std::clamp(point.y,head.y-limit,head.y+limit);
    // Face the line from the centre, which sits off the grabbed spot: settle
    // the turn twice, so the spot stays exactly where the laser meets it.
    auto turn=FacingAxis(point,head);
    if(!turn)return {};
    Panel out=grab.panel;
    for(int pass=0;pass<2;++pass) {
        const auto offset=Rotate(*turn,Vec3{grab.x,grab.y,0});
        out.pose.position={point.x-offset.x,point.y-offset.y,point.z-offset.z};
        if(pass==0)if(const auto settled=FacingAxis(out.pose.position,head))turn=settled;
    }
    out.pose.orientation=*turn;
    return out;
}
PanelPlacement PlacementOf(const MenuAnchor& anchor,const Panel& panel)
{
    Vec3 f{},r{};
    PanelPlacement out;
    if(!AnchorAxes(anchor,f,r)||!Valid(panel.pose))return out;
    const auto& h=anchor.upright.position;const auto& p=panel.pose.position;
    const float x=p.x-h.x,z=p.z-h.z;
    out.right=std::atan2(x*r.x+z*r.z,x*f.x+z*f.z);
    out.height=p.y-h.y;
    out.radius=std::sqrt(x*x+z*z);
    return out;
}
Pose PlacedPose(const MenuAnchor& anchor,const PanelPlacement& placement)
{
    Vec3 f{},r{};
    if(!AnchorAxes(anchor,f,r))return anchor.upright;
    const float c=std::cos(placement.right),s=std::sin(placement.right);
    const auto& h=anchor.upright.position;
    const Vec3 position{h.x+(f.x*c+r.x*s)*placement.radius,h.y+placement.height,h.z+(f.z*c+r.z*s)*placement.radius};
    const auto facing=FacingAxis(position,h);
    return Pose{facing?*facing:anchor.upright.orientation,position};
}
FrameLayers ComposeFrame(const FrameShown& s)
{
    FrameLayers out;
    const auto add=[&](FrameLayer layer){out.layers[out.count++]=layer;};
    const bool guide=s.menu&&s.guide;
    add(!s.menu?FrameLayer::world:s.menuCylinder?FrameLayer::menuCylinder:FrameLayer::menu);
    if(s.menu&&s.menuRightEye)add(FrameLayer::menuRightEye);
    // The guide card belongs to the game menu; the HUD, reticle, wheel, note and text to the world.
    if(guide)add(FrameLayer::guide);
    else if(!s.options&&!s.menu&&s.hud)add(FrameLayer::hud);
    if(!s.options&&!s.menu&&s.reticle)add(FrameLayer::reticle);
    if(!s.options&&!s.menu&&s.wheel)add(FrameLayer::wheel);
    if(!s.options&&!s.menu&&s.note)add(FrameLayer::note);
    if(!s.options&&!s.menu&&s.text)add(FrameLayer::text);
    if(s.options)add(FrameLayer::options);
    if(s.beam)add(FrameLayer::beam);
    if(s.cursor)add(FrameLayer::cursor);
    if(s.wrist)add(FrameLayer::wrist);
    if(s.slot)add(FrameLayer::slot);
    if(s.fade)add(FrameLayer::fade);
    return out;
}
int StepFade(Fade& fade, long long now, bool wanted)
{
    const float dt=fade.time&&now>fade.time?std::min(.1f,static_cast<float>(now-fade.time)*1e-9f):0.f;
    fade.time=now;
    fade.alpha=wanted?std::min(1.f,fade.alpha+dt/.1f):std::max(0.f,fade.alpha-dt/.2f);
    return static_cast<int>(fade.alpha*kFadeLevels+.5f);
}
}
