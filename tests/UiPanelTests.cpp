#include "preyvr/UiPanel.h"
#include <cstdlib>
#include <cmath>
#include <initializer_list>
#include <iostream>
using namespace preyvr;
void Require(bool b,const char* why) { if(!b){std::cerr<<why<<'\n';std::exit(1);} }
int main() {
    const auto near=[](float a,float b){return std::abs(a-b)<.0001f;};
    const ui::Surface flat{{Pose{{},{0,0,-2}},2,1},0};
    auto hit=ui::Intersect(flat,Pose{{},{.4f,.2f,0}});
    Require(hit&&hit->inside&&near(hit->u,.7f)&&near(hit->v,.3f)&&near(hit->distance,2),"translated ray maps to exact screen pixels");
    Require(!ui::Intersect(flat,Pose{{0,1,0,0},{}}),"ray pointing away cannot select");
    Require(!ui::Intersect(flat,Pose{{},{0,0,-3}}),"back face rejected");
    Require(!ui::Intersect(flat,Pose{{0,.70710678f,0,.70710678f},{}}),"parallel ray rejected");
    hit=ui::Intersect(flat,Pose{{},{1.2f,0,0}});
    Require(hit&&!hit->inside&&hit->u>1,"dragging offscreen must not clamp onto a button");
    const ui::Surface curved{flat.panel,.6f};
    const auto axis=ui::CylinderAxis(curved);
    Require(near(axis.position.z,2/.6f-2),"cylinder axis differs from visible centre by radius");
    Require(near(ui::SurfacePoint(curved,.5f,.5f).position.z,-2),"curvature preserves centre distance");
    for(float u:{.02f,.25f,.5f,.75f,.98f})for(float v:{.02f,.5f,.98f}) {
        const float theta=(u-.5f)*.6f,r=2/.6f;
        const Vec3 target{r*std::sin(theta),.5f-v,r-2-r*std::cos(theta)};
        const Vec3 origin{.25f,-.15f,.1f};
        Vec3 d{target.x-origin.x,target.y-origin.y,target.z-origin.z};
        const float len=std::sqrt(d.x*d.x+d.y*d.y+d.z*d.z);d={d.x/len,d.y/len,d.z/len};
        Pose ray{Normalize({d.y,-d.x,0,1-d.z}),origin};
        auto h=ui::Intersect(curved,ray);
        Require(h&&h->inside&&near(h->u,u)&&near(h->v,v),"curved intersection agrees with independent surface points");
        const Pose room{{0,.4f,0,std::sqrt(.84f)},{10,1,-3}};
        auto transformed=curved;transformed.panel.pose=Compose(room,curved.panel.pose);
        h=ui::Intersect(transformed,Compose(room,ray));
        Require(h&&near(h->u,u)&&near(h->v,v),"recenter and room transforms preserve curved UVs");
    }
    ui::PointerButtons buttons;
    Require(buttons.Update(true,true,true)==0,"held trigger on menu entry cannot click");
    Require(buttons.Update(true,true,false)==0,"release arms pointer");
    Require(buttons.Update(true,true,true)==1,"fresh squeeze presses");
    Require(buttons.Update(true,false,true)==0&&buttons.down,"drag remains captured beyond panel");
    Require(buttons.Update(false,false,true)==-1&&!buttons.down,"focus loss releases capture");
    Require(buttons.Update(true,true,true)==0,"focus regain while held cannot click");
    buttons.Update(true,false,false);
    Require(buttons.Update(true,false,true)==0,"press outside never captures");
    Require(buttons.Update(true,true,true)==0,"held ray entering button never presses");
    buttons.Update(true,true,false);
    Require(buttons.Update(true,true,true)==1&&buttons.Update(true,true,false)==-1,"ordinary press/release");
    for(float angle:{-.9f,-.2f,0.f,.3f,.9f}) {
        const auto f=ui::PanelFraction(std::tan(angle),.1f,2.3f,1.3f,2);
        Require(f.has_value(),"valid HUD projection");
        const Vec3 point{((*f)[0]-.5f)*2.3f,(.5f-(*f)[1])*1.3f,-2};
        Require(std::abs(std::atan2(point.x,-point.z)-angle)<.0001f,"HUD pixel reconstructs original aim direction");
    }
    const auto outside=ui::PanelFraction(2,0,2,1,2);
    Require(outside && (*outside)[0]>1,"off-panel aim must leave the texture, not label its edge");
    Require(!ui::PanelFraction(0,0,0,1,2),"degenerate HUD plane rejected");
    std::array<ui::Eye,2> eyes{{{Pose{{},{-.032f,0,0}},-.95f,.75f,.8f,-.85f},
                              {Pose{{},{.032f,0,0}},-.75f,.95f,.8f,-.85f}}};
    for(float aspect : {.5f,1.f,16.f/9,3440.f/1440,4.f}) {
        const auto panel=ui::FitPanel({},eyes,aspect);
        Require(panel && ui::CornersVisible(*panel,eyes),"both eyes must contain every corner");
        Require(std::abs(panel->width/panel->height-aspect)<.0001f,"no aspect distortion");
        auto s=ui::Surface{*panel,.6f};
        for(int n=0;n<80&&!ui::SurfaceVisible(s,eyes);++n){s.panel.width*=.95f;s.panel.height*=.95f;}
        Require(ui::SurfaceVisible(s,eyes),"curved canvas fits both optical frusta");
    }
    // A narrow asymmetric/canted runtime must shrink the panel, never crop it.
    auto invalidSurface=curved;
    invalidSurface.panel.width=std::nanf("");
    Require(!ui::SurfaceVisible(invalidSurface,eyes)&&!ui::Intersect(invalidSurface,{}),"non-finite surface rejected");
    invalidSurface=curved;invalidSurface.panel.height=INFINITY;
    Require(!ui::SurfaceVisible(invalidSurface,eyes)&&!ui::Intersect(invalidSurface,{}),"infinite surface rejected");
    eyes[0].right=.2f; eyes[1].left=-.23f;
    eyes[0].pose.orientation={0,.05f,0,std::sqrt(1-.05f*.05f)};
    auto narrow=ui::FitPanel({},eyes,16.f/9);
    Require(narrow && ui::CornersVisible(*narrow,eyes),"canted narrow frusta");
    const Pose room{{0,.4f,0,std::sqrt(.84f)},{10,1,-3}};
    for(auto& eye:eyes) eye.pose=Compose(room,eye.pose);
    auto moved=ui::FitPanel(room,eyes,16.f/9);
    Require(moved && ui::CornersVisible(*moved,eyes),"invariant under common room transform");
    eyes[0].left=std::nanf("");
    Require(!ui::FitPanel({},eyes,1),"reject bad FOV");
    Require(!ui::FitPanel({},eyes,0),"reject zero aspect");
    // --- the wearer's scale dial ------------------------------------------
    // A clean pair of eyes: the ones above were deliberately damaged by the
    // rejection cases, so reusing them would test the wrong thing.
    std::array<ui::Eye,2> plain{};
    for(int e=0;e<2;++e){plain[e].pose={{0,0,0,1},{e?.032f:-.032f,0,0}};
        plain[e].left=-.9f;plain[e].right=.9f;plain[e].up=.9f;plain[e].down=-.9f;}

    auto base=ui::FitPanel({},plain,16.f/9);
    auto same=ui::FitPanel({},plain,16.f/9,2.0f,1.0f);
    Require(base && same,"premise: both fit");
    Require(std::abs(base->width-same->width)<.0001f,"scale 1.0 is the default");

    auto half=ui::FitPanel({},plain,16.f/9,2.0f,.5f);
    Require(half && std::abs(half->width-base->width*.5f)<.0001f,"0.5 halves the width");
    Require(std::abs(half->height-base->height*.5f)<.0001f,"and the height");
    Require(std::abs(half->width/half->height-16.f/9)<.0001f,"aspect survives scaling");

    // **Above 1 must actually grow.** The 60/40 caps are a comfort default, and
    // at a portrait aspect the vertical one binds and forces the panel narrow --
    // the case a wearer hit at 2688x2880, where it came out around 37.5 degrees:
    // too small, and notably NOT clipping anywhere, which is what proves the cap
    // was binding rather than the corner test.
    auto big=ui::FitPanel({},plain,16.f/9,2.0f,1.5f);
    Require(big && big->width>base->width,"1.5 grows the panel past the default cap");
    Require(std::abs(big->width-base->width*1.5f)<.0001f,"by exactly the factor asked for");

    // Clamped at both ends rather than refused, so a fat-fingered value degrades
    // to the nearest sane panel instead of removing the UI entirely.
    auto huge=ui::FitPanel({},plain,16.f/9,2.0f,50.f);
    auto twice=ui::FitPanel({},plain,16.f/9,2.0f,2.0f);
    Require(huge && twice && std::abs(huge->width-twice->width)<.0001f,"clamped at 2.0");
    auto tiny=ui::FitPanel({},plain,16.f/9,2.0f,.001f);
    auto floorPanel=ui::FitPanel({},plain,16.f/9,2.0f,.2f);
    Require(tiny && floorPanel && std::abs(tiny->width-floorPanel->width)<.0001f,"clamped at 0.2");
    Require(!ui::FitPanel({},plain,16.f/9,2.0f,std::nanf("")),"a non-finite scale is refused");

    // The portrait aspect is the case that motivated the dial.
    auto wide=ui::FitPanel({},plain,2560.f/1440);
    auto square=ui::FitPanel({},plain,2688.f/2880);
    Require(wide && square,"both aspects fit");
    Require(square->width<wide->width,"the portrait aspect yields a NARROWER panel");

    // --- the fit margin, and why ui.scale looked capped ---------------------
    //
    // A wearer at 2688x2880 reported that ui.scale stopped doing anything above
    // about 130 and read that as its maximum. It is not: FitPanel shrinks the
    // panel in 0.95 steps until it sits inside `margin` of the frustum the
    // runtime reports, so once that binds, every further increase in scale is
    // shrunk straight back off. These eyes reproduce it -- a 40-degree vertical
    // half-angle, which is the bound that binds at a portrait render aspect.
    std::array<ui::Eye,2> tight{};
    for(int e=0;e<2;++e){tight[e].pose={{0,0,0,1},{e?.032f:-.032f,0,0}};
        tight[e].left=-.9f;tight[e].right=.9f;tight[e].up=.7f;tight[e].down=-.7f;}
    const float portrait=2688.f/2880;

    auto s18=ui::FitPanel({},tight,portrait,2.0f,1.8f);
    auto s20=ui::FitPanel({},tight,portrait,2.0f,2.0f);
    Require(s18&&s20,"both scales fit once shrunk");
    // Asking for 11 percent more delivers under 1 percent: the defect itself.
    Require(s20->height/s18->height<1.01f,
        "past the margin, raising the scale does almost nothing -- the reported cap");

    // The margin is the dial that actually moves it.
    auto wideM=ui::FitPanel({},tight,portrait,2.0f,2.0f,.95f);
    Require(wideM.has_value(),"a looser margin still fits");
    Require(wideM->height>s20->height*1.15f,
        "loosening the margin grows the panel where scale could not");
    Require(std::abs(wideM->width/wideM->height-portrait)<.0001f,"aspect survives the margin change");

    // The default must stay the default: passing it explicitly changes nothing.
    auto implicitM=ui::FitPanel({},tight,portrait,2.0f,2.0f);
    auto explicitM=ui::FitPanel({},tight,portrait,2.0f,2.0f,ui::kDefaultFitMargin);
    Require(implicitM&&explicitM&&
        std::abs(implicitM->height-explicitM->height)<.0001f,"0.72 is the default margin");

    // Clamped at both ends rather than refused, so a fat-fingered value degrades
    // to the nearest sane panel -- but a non-finite one is refused outright,
    // because it means the caller lost the value rather than mistyped it.
    auto overM=ui::FitPanel({},tight,portrait,2.0f,2.0f,4.f);
    auto atOneM=ui::FitPanel({},tight,portrait,2.0f,2.0f,1.f);
    Require(overM&&atOneM&&std::abs(overM->height-atOneM->height)<.0001f,"margin clamped at 1.0");
    auto underM=ui::FitPanel({},tight,portrait,2.0f,2.0f,.01f);
    auto floorM=ui::FitPanel({},tight,portrait,2.0f,2.0f,.3f);
    Require(underM&&floorM&&std::abs(underM->height-floorM->height)<.0001f,"margin clamped at 0.3");
    Require(!ui::FitPanel({},tight,portrait,2.0f,2.0f,std::nanf("")),"a non-finite margin is refused");
    Require(!ui::CornersVisible(*atOneM,tight,std::nanf("")),"and refused by the corner test");
    Require(!ui::SurfaceVisible({*atOneM,0},tight,std::nanf("")),"and by the surface test");

    // A tighter margin must never admit what a looser one rejects.
    for(float m:{.3f,.5f,.72f,.9f,1.f}) {
        auto pm=ui::FitPanel({},tight,portrait,2.0f,2.0f,m);
        Require(pm&&ui::CornersVisible(*pm,tight,m),"a fitted panel passes its own margin");
        if(m>.3f)Require(ui::CornersVisible(*pm,tight,1.f),"and passes every looser one");
    }

    // --- menus open upright, at eye level, facing the wearer ----------------
    //
    // Fitted from the head itself, a menu opened while looking down came out
    // low and tilted, and rolled with the head. The upright head keeps only the
    // way the face is turned; levelling the eyes with it sizes the panel for a
    // level gaze, as though the wearer had looked straight ahead.
    const auto about=[](float x,float y,float z,float radians){
        const float s=std::sin(radians*.5f);return Quaternion{x*s,y*s,z*s,std::cos(radians*.5f)};};
    const auto eyesOf=[&](const Pose& head,const std::array<ui::Eye,2>& local){
        auto out=local;for(auto& eye:out)eye.pose=Compose(head,eye.pose);return out;};
    const float yaw=.52f;
    const Pose tilted{Normalize(Multiply(Multiply(about(0,1,0,yaw),about(1,0,0,-.7f)),about(0,0,1,.44f))),
                      {.3f,1.6f,-.2f}};
    const auto upright=ui::UprightHead(tilted);
    Require(near(upright.position.x,.3f)&&near(upright.position.y,1.6f)&&near(upright.position.z,-.2f),
        "the upright head stays where the head is");
    const auto uprightForward=Rotate(upright.orientation,{0,0,-1});
    Require(near(uprightForward.y,0)&&near(std::atan2(-uprightForward.x,-uprightForward.z),yaw),
        "and keeps only the way the face is turned");
    const auto levelled=ui::LevelledEyes(tilted,upright,eyesOf(tilted,plain));
    for(float distance:{1.f,1.5f,2.f,4.f}) {
        const auto menu=ui::FitPanel(upright,levelled,16.f/9,distance);
        Require(menu.has_value(),"a levelled fit places the menu");
        Require(near(menu->pose.position.y,1.6f),"at eye level");
        const float dx=menu->pose.position.x-.3f,dz=menu->pose.position.z+.2f;
        Require(near(std::sqrt(dx*dx+dz*dz),distance),"at the distance asked for");
        Require(near(std::atan2(-dx,-dz),yaw),"straight ahead of where the face is turned");
        const auto normal=Rotate(menu->pose.orientation,{0,0,1});
        const auto across=Rotate(menu->pose.orientation,{1,0,0});
        Require(near(normal.y,0)&&near(across.y,0),"upright: neither tilted nor rolled");
        Require(near(normal.x*dx+normal.z*dz,-distance),"facing the wearer square-on");
        Require(near(menu->width/distance,base->width/2),
            "the same apparent size at every distance, and as a level gaze gets");
    }
    const Pose lookingDown{Multiply(about(0,1,0,yaw),about(1,0,0,-1.05f)),{0,1.6f,0}};
    const auto levelHead=ui::UprightHead(lookingDown);
    Require(!ui::FitPanel(levelHead,eyesOf(lookingDown,plain),16.f/9),
        "premise: the live eyes, looking well down, refuse a level menu outright");
    Require(ui::FitPanel(levelHead,ui::LevelledEyes(lookingDown,levelHead,eyesOf(lookingDown,plain)),16.f/9).has_value(),
        "the levelled eyes place it");
    const Pose straightDown{Multiply(about(0,1,0,yaw),about(1,0,0,-1.5707963f)),{0,1.6f,0}};
    const auto downForward=Rotate(ui::UprightHead(straightDown).orientation,{0,0,-1});
    Require(near(std::atan2(-downForward.x,-downForward.z),yaw),
        "looking straight down still faces the way the face is turned");

    // --- ui.offset: around a cylinder about the head, never pitched or resized
    const auto ahead=ui::FitPanel(upright,levelled,16.f/9,2.f);
    Require(ahead.has_value(),"premise: the straight-ahead menu");
    const auto unmoved=ui::AroundHead(upright,ahead->pose,0,0);
    Require(near(unmoved.position.x,ahead->pose.position.x)&&near(unmoved.position.y,ahead->pose.position.y)&&
        near(unmoved.position.z,ahead->pose.position.z),"no offset, no move");
    for(float right:{-1.2f,-.35f,0.f,.5f,1.5f})for(float up:{-.9f,-.2f,0.f,.3f,1.f}) {
        const auto aside=ui::AroundHead(upright,ahead->pose,right,up);
        const float dx=aside.position.x-.3f,dy=aside.position.y-1.6f,dz=aside.position.z+.2f;
        const float across=std::sqrt(dx*dx+dz*dz);
        Require(near(across,2.f),"kept at the horizontal distance asked for");
        Require(near(dy,2.f*std::tan(up)),"slid straight up to the height of that angle");
        Require(near(std::atan2(dy,across),up),"so its centre is seen that far above eye level");
        // Turned right of where the face points: yaw decreases to the right.
        Require(near(std::atan2(-dx,-dz),yaw-right),"turned by the angle asked for");
        const auto normal=Rotate(aside.orientation,{0,0,1});
        Require(near(normal.y,0),"upright: never pitched to face the eyes");
        Require(near(Rotate(aside.orientation,{1,0,0}).y,0),"its edges stay level");
        Require(near(normal.x*dx+normal.z*dz,-across),"facing the head's vertical line square-on");
    }
    const auto vertical=ui::AroundHead(upright,ahead->pose,0,1.5707963f);
    Require(near(vertical.position.y,ahead->pose.position.y),"straight up has no height, so it is refused");

    // --- the shared menu anchor -------------------------------------------
    // Taken once from the tilted head; every menu placed from it, before or
    // after the wearer turns, lands where the anchor says.
    const auto anchor=ui::AnchorMenu(tilted,eyesOf(tilted,plain));
    const auto placed=ui::PlaceMenu(anchor,16.f/9,2.f,1.f,ui::kDefaultFitMargin,.5f,-.2f);
    Require(placed.has_value(),"a menu is placed from the anchor");
    const auto expected=ui::AroundHead(upright,ahead->pose,.5f,-.2f);
    Require(near(placed->pose.position.x,expected.position.x)&&near(placed->pose.position.y,expected.position.y)&&
        near(placed->pose.position.z,expected.position.z)&&near(placed->width,ahead->width),
        "placing is fitting straight ahead of the upright head, then carrying it around the cylinder");
    // The wearer has turned to look at that menu, 0.5 rad right. Re-placing it
    // from the anchor leaves it where it is; anchoring afresh where they now
    // look would add the offset a second time.
    const Pose lookingAtIt{Multiply(about(0,1,0,yaw-.5f),about(1,0,0,-.2f)),tilted.position};
    const auto again=ui::PlaceMenu(anchor,16.f/9,2.f,1.f,ui::kDefaultFitMargin,.5f,-.2f);
    Require(again&&near(again->pose.position.x,placed->pose.position.x)&&near(again->pose.position.z,placed->pose.position.z),
        "re-placed from its anchor, a menu stays put");
    const auto compounded=ui::PlaceMenu(ui::AnchorMenu(lookingAtIt,eyesOf(lookingAtIt,plain)),16.f/9,2.f,1.f,
        ui::kDefaultFitMargin,.5f,-.2f);
    Require(compounded&&!near(compounded->pose.position.x,placed->pose.position.x),
        "premise: anchored afresh where the wearer now looks, it would move by the offset again");
    // Only the distance changes: the same direction from the anchor, further away.
    const auto further=ui::PlaceMenu(anchor,16.f/9,3.f,1.f,ui::kDefaultFitMargin,.5f,-.2f);
    Require(further.has_value(),"a further menu is placed");
    const float px=placed->pose.position.x-.3f,pz=placed->pose.position.z+.2f;
    const float fx=further->pose.position.x-.3f,fz=further->pose.position.z+.2f;
    Require(near(std::atan2(-fx,-fz),std::atan2(-px,-pz))&&near(std::sqrt(fx*fx+fz*fz),3.f),
        "a new distance keeps its direction from the anchor");
    Require(!ui::PlaceMenu(anchor,0,2.f,1.f,ui::kDefaultFitMargin,0,0),"a refused fit places nothing");
    const auto toRight=ui::AroundHead(upright,ahead->pose,.4f,0);
    const auto rightOfFace=Rotate(upright.orientation,{1,0,0});
    Require((toRight.position.x-ahead->pose.position.x)*rightOfFace.x+
            (toRight.position.z-ahead->pose.position.z)*rightOfFace.z>0,"positive x is the wearer's right");
    Require(ui::AroundHead(upright,ahead->pose,.2f,-.3f).position.y<1.6f,"negative y is below eye level");
    // The guide card rides with the menu: both offsets keep their spacing.
    const Pose above=Compose(ahead->pose,Pose{{},{0,.3f,0}});
    const auto a=ui::AroundHead(upright,ahead->pose,.6f,.4f),b=ui::AroundHead(upright,above,.6f,.4f);
    const float gx=b.position.x-a.position.x,gy=b.position.y-a.position.y,gz=b.position.z-a.position.z;
    Require(near(std::sqrt(gx*gx+gy*gy+gz*gz),.3f),"a stacked card keeps its place beside the menu");
    const auto kept=ui::AroundHead(upright,ahead->pose,std::nanf(""),0);
    Require(near(kept.position.z,ahead->pose.position.z),"a non-finite offset leaves the menu where it was");
    const Pose lost{{std::nanf(""),0,0,1},{}};
    Require(!ui::FitPanel(ui::UprightHead(lost),ui::LevelledEyes(lost,ui::UprightHead(lost),plain),16.f/9),
        "a non-finite head is refused, not levelled into a plausible pose");

    // What a frame submits, bottom to top.
    using L=ui::FrameLayer;
    const auto is=[](const ui::FrameLayers& got,std::initializer_list<L> want){
        if(got.count!=want.size())return false;
        unsigned n=0;for(const L l:want)if(got.layers[n++]!=l)return false;
        return true;
    };
    ui::FrameShown play;play.hud=play.text=play.beam=play.cursor=play.wrist=play.slot=play.fade=true;
    Require(is(ui::ComposeFrame(play),{L::world,L::hud,L::text,L::beam,L::cursor,L::wrist,L::slot,L::fade}),
        "gameplay: the world, then the HUD and text, the laser, wrist, slot and fade");
    auto over=play;over.options=true;over.wrist=over.slot=over.fade=false; // paused by their callers meanwhile
    Require(is(ui::ComposeFrame(over),{L::world,L::options,L::beam,L::cursor}),
        "VR Options over the running game: the world under it, the laser over it, the HUD and text out");
    over.wrist=over.slot=over.fade=true;
    Require(is(ui::ComposeFrame(over),{L::world,L::options,L::beam,L::cursor,L::wrist,L::slot,L::fade}),
        "the wrist, slot and fade are their callers' to gate: what is shown is drawn");
    ui::FrameShown menu;menu.menu=menu.menuCylinder=menu.menuRightEye=menu.guide=menu.beam=menu.cursor=true;
    Require(is(ui::ComposeFrame(menu),{L::menuCylinder,L::menuRightEye,L::guide,L::beam,L::cursor}),
        "a curved game menu with its right eye and guide card, under the laser");
    auto options=menu;options.options=true;
    Require(is(ui::ComposeFrame(options),{L::menuCylinder,L::menuRightEye,L::guide,L::options,L::beam,L::cursor}),
        "VR Options over a game menu: the whole menu stays under it");
    ui::FrameShown flatMenu;flatMenu.menu=flatMenu.options=flatMenu.hud=flatMenu.text=true;
    Require(is(ui::ComposeFrame(flatMenu),{L::menu,L::options}),"a flat game menu is the quad, VR Options over it, no HUD or text");
    ui::FrameShown everything;
    everything.menuRightEye=everything.guide=everything.hud=everything.text=true;
    everything.beam=everything.cursor=everything.wrist=everything.slot=everything.fade=true;
    Require(is(ui::ComposeFrame(everything),{L::world,L::hud,L::text,L::beam,L::cursor,L::wrist,L::slot,L::fade}),
        "the menu's right eye and guide card never show over the world");

    // **VR Options dragged by the laser**, round the inside of a cylinder about the head.
    {
        const auto close=[](float a,float b){return std::abs(a-b)<.002f;};
        // OpenXR looks down -Z: a positive yaw turns left, a positive pitch up.
        const auto aim=[](Vec3 from,float turnBy,float tiltBy){
            const Quaternion turn{0,std::sin(turnBy*.5f),0,std::cos(turnBy*.5f)},tilt{std::sin(tiltBy*.5f),0,0,std::cos(tiltBy*.5f)};
            return Pose{Normalize(Multiply(turn,tilt)),from};
        };
        const auto across=[](Vec3 a,Vec3 b){return std::hypot(a.x-b.x,a.z-b.z);};
        const auto isUpright=[&](const Quaternion& q){return close(q.x,0)&&close(q.z,0);};
        const auto facing=[&](const ui::Panel& panel,Vec3 head){
            const auto n=Rotate(panel.pose.orientation,Vec3{0,0,1});
            const float x=head.x-panel.pose.position.x,z=head.z-panel.pose.position.z,length=std::hypot(x,z);
            return close(n.y,0)&&close(n.x,x/length)&&close(n.z,z/length);
        };
        const Vec3 head{0,1.6f,0},hand{.2f,1.3f,-.3f},spot{.3f,1.8f,-2};
        const ui::Panel start{Pose{{},{0,1.6f,-2}},1.2f,.9f};
        const float dx=spot.x-hand.x,dy=spot.y-hand.y,dz=spot.z-hand.z;
        const float laserYaw=std::atan2(-dx,-dz),laserPitch=std::asin(dy/std::sqrt(dx*dx+dy*dy+dz*dz));
        const auto laser=aim(hand,laserYaw,laserPitch);
        const auto panelHit=ui::Intersect(ui::Surface{start,0},laser);
        Require(panelHit&&panelHit->inside,"premise: the laser is on the panel, right of and above its centre");
        const auto grab=ui::GrabPanel(start,*panelHit,head);
        Require(grab&&close(grab->x,.3f)&&close(grab->y,.2f)&&close(grab->radius,across(spot,head)),
            "a grab keeps the spot it took and its distance across the floor");
        Require(!ui::GrabPanel(start,ui::RayHit{},head),"a laser off the panel grabs nothing");
        const auto spotOf=[&](const ui::Panel& panel){
            const auto o=Rotate(panel.pose.orientation,Vec3{grab->x,grab->y,0});
            return Vec3{panel.pose.position.x+o.x,panel.pose.position.y+o.y,panel.pose.position.z+o.z};
        };
        const auto onLaser=[&](Vec3 point,const Pose& ray){
            const auto d=Rotate(ray.orientation,Vec3{0,0,-1});
            const Vec3 v{point.x-ray.position.x,point.y-ray.position.y,point.z-ray.position.z};
            const float along=v.x*d.x+v.y*d.y+v.z*d.z;
            return along>0&&std::hypot(std::hypot(v.x-d.x*along,v.y-d.y*along),v.z-d.z*along)<.002f;
        };
        const auto still=ui::DragPanel(*grab,laser,head);
        Require(still&&close(still->pose.position.x,0)&&close(still->pose.position.y,1.6f)&&close(still->pose.position.z,-2),
            "held without moving, it stays put");
        const auto swungLaser=aim(hand,laserYaw-.5f,laserPitch);
        const auto swung=ui::DragPanel(*grab,swungLaser,head);
        Require(swung&&swung->pose.position.x>.5f&&onLaser(spotOf(*swung),swungLaser)&&close(across(spotOf(*swung),head),grab->radius)&&
            isUpright(swung->pose.orientation)&&facing(*swung,head),
            "swung right: round the cylinder, the grabbed spot under the laser, upright and facing the head");
        const auto raisedLaser=aim(hand,laserYaw,laserPitch+.4f);
        const auto raised=ui::DragPanel(*grab,raisedLaser,head);
        Require(raised&&spotOf(*raised).y>spot.y+.5f&&onLaser(spotOf(*raised),raisedLaser)&&
            close(across(spotOf(*raised),head),grab->radius)&&isUpright(raised->pose.orientation)&&facing(*raised,head),
            "raised: it slides up the cylinder at the same distance across the floor, never tilting toward the eyes");
        auto pushed=*grab;pushed.radius+=1;
        const auto pushedPanel=ui::DragPanel(pushed,laser,head);
        Require(pushedPanel&&close(across(spotOf(*pushedPanel),head),grab->radius+1)&&onLaser(spotOf(*pushedPanel),laser)&&
            pushedPanel->width==start.width&&pushedPanel->height==start.height,
            "pushed: a metre further along the laser, the same physical size, so it reads smaller");
        Require(!ui::DragPanel(*grab,aim(hand,0,1.5707963f),head),"a laser straight up meets no wall of the cylinder");
        const auto steep=ui::DragPanel(*grab,aim(hand,laserYaw,1.45f),head);
        Require(steep&&spotOf(*steep).y-head.y<=grab->radius*std::tan(1.05f)+.002f,"no higher than 60 degrees from the head");
        const ui::PanelGrab small{start,0,0,.6f};
        const auto beyond=ui::DragPanel(small,aim(Vec3{0,1.3f,-1.2f},0,0),head);
        Require(beyond&&close(across(beyond->pose.position,head),1.3f),"a hand held out past the cylinder widens it, rather than losing the panel");
        // Placement relative to an anchor: kept across anchors.
        const ui::MenuAnchor anchorAhead{Pose{{},{0,1.6f,0}},{}};
        const auto leftAt=ui::PlacementOf(anchorAhead,*swung);
        const auto back=ui::PlacedPose(anchorAhead,leftAt);
        Require(close(back.position.x,swung->pose.position.x)&&close(back.position.y,swung->pose.position.y)&&
            close(back.position.z,swung->pose.position.z)&&close(back.orientation.y,swung->pose.orientation.y)&&
            close(back.orientation.w,swung->pose.orientation.w),"a placement puts a panel back where it was");
        const ui::MenuAnchor turned{Pose{{0,std::sin(.7853982f),0,std::cos(.7853982f)},{1,1.5f,1}},{}};
        const auto aside=ui::PlacedPose(turned,ui::PanelPlacement{});
        const ui::Panel asidePanel{aside,1,1};
        Require(close(aside.position.x,-1)&&close(aside.position.y,1.5f)&&close(aside.position.z,1)&&isUpright(aside.orientation)&&
            facing(asidePanel,turned.upright.position),"the default placement is 2 m ahead of an anchor turned left, facing it");
        const auto reopened=ui::PlacementOf(turned,ui::Panel{ui::PlacedPose(turned,leftAt),1,1});
        Require(close(reopened.right,leftAt.right)&&close(reopened.height,leftAt.height)&&close(reopened.radius,leftAt.radius),
            "and from a new anchor it is the same place relative to the wearer");
    }

    std::cout<<"UI panel geometry, frame layers and panel dragging passed\n";
}
