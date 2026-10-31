#include "preyvr/TwoHandedAim.h"
#include "preyvr/MotionController.h"
#include "preyvr/WeaponRigAlignment.h"
#include "TestSupport.h"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
using namespace preyvr;
using namespace preyvr::twohand;
using preyvr::test::Check;
bool Near(Vec3 a,Vec3 b,float epsilon=.002f) { return std::fabs(a.x-b.x)<epsilon&&std::fabs(a.y-b.y)<epsilon&&std::fabs(a.z-b.z)<epsilon; }
Input Base() { Input i{};i.owner=1;i.epoch=1;i.reference=2;i.usable=true;i.dt=.01f;
    i.region.start={0,.25f,0};i.region.end={0,.45f,0};i.support.position={0,.35f,0};return i; }
Output Hold(Solver& s,Input& i) { s.Update(i);i.squeeze=1; Output out{};for(int n=0;n<60;++n)out=s.Update(i);return out; }
int main() {
    {
        auto toggle=Base();toggle.toggleGrip=true;Solver solver;
        Check(Hold(solver,toggle).held,"toggle grip acquires");
        toggle.squeeze=0;Check(solver.Update(toggle).held,"toggle remains attached after release");
        toggle.squeeze=1;Check(!solver.Update(toggle).held,"second squeeze detaches");
        Check(!solver.Update(toggle).held,"held detach cannot immediately reacquire");
        toggle.squeeze=0;solver.Update(toggle);toggle.squeeze=1;Check(solver.Update(toggle).held,"fresh squeeze reacquires");
        toggle.owner++;Check(!solver.Update(toggle).held,"weapon switch clears toggle");
        Check(!solver.Update(toggle).held,"switch with held grip cannot latch");
        toggle.squeeze=0;solver.Update(toggle);toggle.squeeze=1;Check(solver.Update(toggle).held,"new weapon fresh grip");
        toggle.toggleGrip=false;Check(!solver.Update(toggle).held,"switching grip preference releases");
        Check(!solver.Update(toggle).held,"mode switch needs release");
        toggle.usable=false;Check(!solver.Update(toggle).held,"disabled resets");
    }
    auto i=Base();Solver s;auto out=Hold(s,i);
    Check(out.held&&out.blend>.99f,"grip enters in region");
    Check(std::fabs(out.socket.y-.35f)<.001f,"nearest point locks within capsule");
    i.support.position={.35f,0,0};out=s.Update(i);
    Check(out.held,"leaving acquisition region does not release latch");
    Check(Near(Rotate(out.orientation,{0,1,0}),{1,0,0}),"raw offhand steers barrel");
    // Feed solved orientation through the real wrist alignment contract.
    weaponrig::Basis basis{};basis.source=weaponrig::Source::jointBind;
    basis.weaponInWrist=Normalize({.2f,.1f,.3f,.9f});basis.barrelInWeapon=Normalize({.1f,.3f,.1f,.8f});
    const Quaternion character=Normalize({.1f,.2f,.4f,.9f});Quaternion wrist{};
    Check(weaponrig::SolveWrist(character,out.orientation,basis,wrist),"wrist solve accepts two-hand aim");
    Check(Near(Rotate(Multiply(Multiply(Multiply(character,wrist),basis.weaponInWrist),basis.barrelInWeapon),{0,1,0}),
               Rotate(out.orientation,{0,1,0})),"model barrel and firing ray share solved direction");
    i.squeeze=0;out=s.Update(i);Check(!out.held&&out.blend>0,"release blends instead of snapping");
    for(int n=0;n<60;++n)out=s.Update(i);
    Check(Near(Rotate(out.orientation,{0,1,0}),{0,1,0}),"release restores one-hand aim");
    i=Base();s={};i.support.position={1,.35f,0};out=Hold(s,i);Check(!out.held,"empty-air squeeze cannot acquire");
    i.support.position={0,.35f,0};Check(!s.Update(i).held,"held failed squeeze cannot acquire by drifting in");
    i.squeeze=0;s.Update(i);i.squeeze=1;Check(s.Update(i).held,"fresh squeeze can acquire");
    i.owner=2;Check(!s.Update(i).held,"weapon switch clears hold");Check(!s.Update(i).held,"new weapon requires release");
    i.squeeze=0;s.Update(i);i.squeeze=.7f;Check(s.Update(i).held,"new owner reacquires on squeeze");
    i.squeeze=.5f;Check(s.Update(i).held,"squeeze hysteresis holds at half travel");
    i.reference+=2;Check(!s.Update(i).held,"recenter invalidates hold");
    i=Base();s={};Hold(s,i);i.usable=false;out=s.Update(i);Check(!out.held&&out.blend==0,"tracking/modal loss resets immediately");
    i.usable=true;Check(!s.Update(i).held,"held reacquired controller cannot auto-grab");
    i=Base();s={};Hold(s,i);i.epoch++;Check(!s.Update(i).held,"session epoch invalidates hold");
    i=Base();s={};Hold(s,i);i.support.position={0,0,.35f};out=s.Update(i);
    Check(out.held&&Near(Rotate(out.orientation,{0,1,0}),{0,0,1}),"vertical aiming has no up-vector singularity");
    i.support.position={0,-.35f,0};out=s.Update(i);Check(Near(Rotate(out.orientation,{0,1,0}),{0,-1,0}),"180 degree turn is finite");
    i.support.position={0,.01f,0};out=s.Update(i);Check(!out.held&&out.blend==0,"near-coincident hands cannot spin weapon");
    i=Base();s={};i.region.start=i.region.end={0,.05f,0};i.support.position={0,.05f,0};Check(!Hold(s,i).held,"short adjacent grips are excluded");
    i=Base();s={};i.visualPrimaryOffset={.1f,0,0};i.support.position={.1f,.35f,0};out=Hold(s,i);
    Check(out.held&&Near(Rotate(out.orientation,{0,1,0}),{0,1,0}),"acquisition and steering account for visible wrist reach compression");
    i=Base();s={};i.region.start=i.region.end={.06f,.35f,-.03f};i.support.position=i.region.start;out=Hold(s,i);
    Check(out.held&&Near(Rotate(out.orientation,{0,1,0}),{0,1,0}),"authored off-axis socket does not introduce aim offset on grab");
    i=Base();s={};Hold(s,i);i.squeeze=std::numeric_limits<float>::quiet_NaN();Check(!s.Update(i).held,"NaN squeeze refuses");
    i=Base();s={};Hold(s,i);i.support.position.x=std::numeric_limits<float>::infinity();Check(!s.Update(i).held,"nonfinite tracking refuses");
    i=Base();s={};Hold(s,i);i.dt=1;Check(!s.Update(i).held,"long stall resets smoothing");
    {
        // Q-Beam samples, metres in the barrel frame (2026-10-07).
        RegionLatch latch;Vec3 socket{};const Vec3 rest{.375f,.616f,.198f};
        Check(!latch.Update(5,true,rest,socket),"one sample does not latch");
        for(int n=0;n<RegionLatch::kSettleSamples-2;++n) Check(!latch.Update(5,true,{rest.x+(n%2)*.002f,rest.y,rest.z},socket),"settling");
        Check(latch.Update(5,true,rest,socket)&&Near(socket,rest),"a resting palm latches");
        Check(latch.Update(5,false,{},socket)&&Near(socket,rest),"out-of-bounds palm (firing animation) keeps the region");
        for(int n=0;n<200;++n) latch.Update(5,true,{.129f,.473f,.096f},socket);
        Check(Near(socket,rest),"an animated palm, even resting elsewhere, never moves a latched region");
        Check(!latch.Update(6,true,{.1f,.3f,0},socket),"re-equip starts over");
        Check(!latch.Update(0,true,rest,socket),"no owner, no region");
        RegionLatch moving;
        for(int n=0;n<100;++n) Check(!moving.Update(5,true,{.3f+n*.02f,.6f,.2f},socket),"an equip animation never settles");
        moving.Update(5,true,{std::numeric_limits<float>::quiet_NaN(),0,0},socket);
        for(int n=0;n<RegionLatch::kSettleSamples;++n) moving.Update(5,true,rest,socket);
        Check(moving.Latched()&&Near(socket,rest),"then latches once it rests");
    }
    {
        LongReach reach{};
        const Vec3 qbeam{-.355f,.576f,-.233f},gloo{-.103f,.424f,.013f};   // measured 2026-10-07
        Check(std::fabs(HoldBack(qbeam,reach)-.126f)<.001f,"Q-Beam foregrip comes back to 0.45 m ahead");
        Check(HoldBack(gloo,reach)==0,"a foregrip within reach is untouched");
        Check(HoldBack({0,1.5f,0},reach)==reach.maxHoldBack,"hold-back is bounded");
        Check(HoldBack({0,std::numeric_limits<float>::quiet_NaN(),0},reach)==0,"nonfinite socket refuses");
        const Region far=SupportRegion(qbeam,.126f,reach),near=SupportRegion(gloo,0,reach);
        Check(Near(far.start,{qbeam.x,qbeam.y-.14f,qbeam.z})&&Near(far.end,{qbeam.x,qbeam.y+.04f,qbeam.z}),"long weapon grabs behind its foregrip");
        Check(Near(near.start,{gloo.x,gloo.y-.04f,gloo.z})&&Near(near.end,{gloo.x,gloo.y+.04f,gloo.z}),"other weapons keep the native region");
        // A grab 12 cm behind the foregrip, inside the radius, latches there: steering starts without a jump.
        Input g=Base();g.region=far;g.support.position={qbeam.x,qbeam.y-.12f,qbeam.z};Solver s2;auto o=Hold(s2,g);
        Check(o.held&&Near(o.socket,g.support.position)&&Near(Rotate(o.orientation,{0,1,0}),{0,1,0}),"behind-foregrip grab holds without an aim jump");
    }
    {
        // Native support palms, metres in the barrel frame (2026-10-28): the
        // pistol and the Disruptor both cup the grip; the shotgun holds its pump.
        const Vec3 cup{-.044f,.073f,-.024f};Grip grip{};
        Check(ClassifySocket(cup,true,grip)&&grip==Grip::handgun,"a cupped support hand is a handgun grip");
        Check(ClassifySocket({.010f,.429f,.040f},true,grip)&&grip==Grip::foregrip,"a pump is a foregrip");
        Check(ClassifySocket({.375f,.616f,.198f},true,grip)&&grip==Grip::foregrip,"the Q-Beam's side handle is a wide foregrip");
        Check(!ClassifySocket({.375f,.616f,.198f},false,grip),"and refused without wide support");
        Check(!ClassifySocket({-.3f,.05f,-.3f},true,grip),"a hand off the weapon is refused");
        Check(!ClassifySocket({-.04f,-.06f,0},true,grip),"a hand behind the trigger wrist is not a cup");
        Check(!ClassifySocket({0,std::numeric_limits<float>::quiet_NaN(),0},true,grip),"nonfinite palm refuses");
        const Region r=SupportRegion(cup,0,LongReach{},Grip::handgun);
        Check(r.grip==Grip::handgun&&Near(r.start,cup)&&Near(r.end,cup)&&r.radius==kHandgunGrab,"a handgun is grabbed around its cup");
        // Two controllers cannot both be in the cup: the left one braces beside and below.
        Input h=Base();h.region=r;h.support.position={cup.x-.06f,cup.y,cup.z-.05f};Solver hs;auto o=Hold(hs,h);
        Check(o.held&&o.blend>.99f&&Near(o.socket,cup),"a braced controller grabs the cup");
        Check(Near(Rotate(o.orientation,{0,1,0}),{0,1,0}),"grabbing a handgun leaves its aim alone");
        h.support.position={cup.x-.10f,cup.y+.08f,cup.z+.04f};o=hs.Update(h);
        Check(o.held&&Near(Rotate(o.orientation,{0,1,0}),{0,1,0})&&Near(Rotate(o.correction,{0,1,0}),{0,1,0}),
              "the support hand never steers a handgun");
        h.support.position={-.02f,.03f,-.02f};Check(hs.Update(h).held,"hands close together keep a handgun");
        h.support.position={-.35f,.05f,0};o=hs.Update(h);Check(!o.held&&o.blend>0,"a hand gone from the gun lets go, blending");
        h.support.position={cup.x-.06f,cup.y,cup.z-.05f};Check(!hs.Update(h).held,"coming back still squeezed does not grab");
        h.squeeze=0;hs.Update(h);h.squeeze=1;Check(hs.Update(h).held,"a fresh squeeze grabs again");
        h=Base();h.region=r;h.support.position={cup.x,cup.y+.25f,cup.z};Solver far;Check(!Hold(far,h).held,"a squeeze away from the cup does nothing");
        Region off=r;off.start=off.end={-.3f,.05f,-.3f};h=Base();h.region=off;h.support.position=off.start;
        Solver offHand;Check(!Hold(offHand,h).held,"a handgun region off the weapon never grabs");
    }
    {
        // A snap turn turns both hands and the aim together and keeps the
        // reference: the hold carries on, the steered weapon turned with it.
        auto t=Base();Solver ts;Hold(ts,t);
        t.support.position={.1f,.35f,0};const auto before=ts.Update(t);
        const Quaternion turn{0,0,.3826834f,.9238795f};   // 45 degrees about engine up
        t.aim.orientation=Multiply(turn,t.aim.orientation);t.support.orientation=Multiply(turn,t.support.orientation);
        t.support.position=Rotate(turn,t.support.position);t.primary=Rotate(turn,t.primary);
        const auto after=ts.Update(t);
        Check(after.held&&Near(Rotate(after.orientation,{0,1,0}),Rotate(turn,Rotate(before.orientation,{0,1,0}))),
              "a snap turn keeps the hold and turns the steered aim with it");
    }
    {
        // Auto-snap: one more way to attach. Base's hand rests on the foregrip,
        // 35 cm ahead, and 10 ms a frame takes kAutoSnapDwell in 10 frames.
        constexpr int kDwell=12;
        const Vec3 on{0,.35f,0},away{1,.35f,0};
        const auto frames=[](Solver& s,Input& i,int n){Output o{};for(int k=0;k<n;++k)o=s.Update(i);return o;};
        {
            auto off=Base();Solver s0;
            Check(!frames(s0,off,100).held,"without the option a hand at rest on the foregrip never attaches");
        }
        auto a=Base();a.autoSnap=true;Solver as;
        a.support.position=away;frames(as,a,3);
        a.support.position=on;
        Check(!frames(as,a,7).held,"a hand that has just arrived has not attached");
        a.support.position=away;frames(as,a,2);a.support.position=on;
        Check(!frames(as,a,7).held,"passing through twice does not add up");
        auto o=frames(as,a,kDwell);
        Check(o.held&&o.snapped&&Near(o.socket,on),"a hand that stays attaches, with no squeeze");
        Check(frames(as,a,60).blend>.99f&&Near(Rotate(as.Update(a).orientation,{0,1,0}),{0,1,0}),"and blends in with no aim jump");
        // Hold mode: no grip to let go of, so the grip at rest does not release it.
        a.support.position={0,.35f,.2f};o=as.Update(a);
        Check(o.held&&o.snapped&&Rotate(o.orientation,{0,1,0}).z>.1f,"it steers, inside its range");
        a.support.position={0,.35f,.26f};o=as.Update(a);
        Check(!o.held&&!o.snapped&&o.blend>0,"a hand gone from where it attached lets go, blending");
        a.support.position=away;frames(as,a,3);a.support.position=on;
        Check(frames(as,a,kDwell).snapped,"it attaches again when the hand comes back");
        // Hold mode: a squeeze takes it over, and then it is an ordinary hold.
        a.squeeze=1;o=as.Update(a);Check(o.held&&!o.snapped,"a squeeze makes it an ordinary hold");
        a.support.position={.6f,.35f,0};Check(as.Update(a).held,"which, as ever, does not let go by distance");
        a.support.position=on;as.Update(a);
        a.squeeze=0;Check(!as.Update(a).held,"letting go releases it");
        Check(!frames(as,a,100).held,"and it does not snap back with the hand still there");
        a.squeeze=1;Check(as.Update(a).held&&!as.Update(a).snapped,"a squeeze still attaches there");
        a.squeeze=0;as.Update(a);
        a.support.position=away;frames(as,a,3);a.support.position=on;
        Check(frames(as,a,kDwell).snapped,"once the hand has left, it snaps again");
        // Toggle mode: a press releases, and the hand must leave before it snaps.
        auto t=Base();t.autoSnap=t.toggleGrip=true;Solver ts;
        t.support.position=away;frames(ts,t,3);t.support.position=on;
        o=frames(ts,t,kDwell);Check(o.held&&o.snapped,"toggle: it snaps too");
        t.squeeze=1;Check(!ts.Update(t).held,"toggle: a press releases");
        t.squeeze=0;Check(!frames(ts,t,100).held,"toggle: no snap with the hand still there");
        t.squeeze=1;o=ts.Update(t);Check(o.held&&!o.snapped,"toggle: a fresh press attaches by squeeze");
        t.squeeze=0;ts.Update(t);t.support.position={.6f,.35f,0};
        Check(ts.Update(t).held,"toggle: which does not let go by distance");
        // A squeezed hand is busy with something else.
        auto b=Base();b.autoSnap=true;Solver bs;
        b.support.position=away;frames(bs,b,3);b.squeeze=1;frames(bs,b,3);b.support.position=on;
        Check(!frames(bs,b,100).held,"a hand arriving squeezed neither grabs nor snaps");
        // What ends a hold ends a snapped one, and the option going off does too.
        auto c=Base();c.autoSnap=true;Solver cs;
        c.support.position=away;frames(cs,c,3);c.support.position=on;frames(cs,c,kDwell);
        c.owner++;Check(!cs.Update(c).held,"a weapon switch ends it");
        Check(frames(cs,c,kDwell).snapped,"and the new weapon snaps after the same wait");
        c.usable=false;Check(!cs.Update(c).held,"losing tracking or a menu ends it");
        c.usable=true;cs.Update(c);frames(cs,c,kDwell);
        c.autoSnap=false;Check(!cs.Update(c).held,"switching the option off ends it, having no grip to");
        c.autoSnap=true;c.support.position=away;frames(cs,c,3);
        c.support.position=on;frames(cs,c,kDwell);c.support.position={0,.01f,0};
        o=cs.Update(c);Check(!o.held&&o.blend==0,"hands together let go at once");
        // A handgun snaps from nearer than a squeeze reaches, and goes by the break distance.
        const Vec3 cup{-.044f,.073f,-.024f};
        auto h=Base();h.autoSnap=true;h.region=SupportRegion(cup,0,LongReach{},Grip::handgun);Solver hs;
        h.support.position=away;frames(hs,h,3);
        h.support.position={cup.x-.12f,cup.y,cup.z};
        Check(!frames(hs,h,100).held,"a hand 12 cm from a pistol's cup does not snap");
        h.squeeze=1;Check(hs.Update(h).held,"though a squeeze there grabs it");
        h.squeeze=0;hs.Update(h);h.support.position=away;frames(hs,h,3);
        h.support.position={cup.x-.08f,cup.y,cup.z};
        o=frames(hs,h,kDwell);Check(o.held&&o.snapped,"a hand 8 cm from it does");
        Check(Near(Rotate(frames(hs,h,60).orientation,{0,1,0}),{0,1,0}),"and leaves its aim alone");
        h.snapBreak=.4f;h.support.position={cup.x-.3f,cup.y,cup.z};
        Check(hs.Update(h).held,"a snapped handgun goes by the break distance, past a squeeze's 30 cm");
        h.snapBreak=.25f;Check(!hs.Update(h).held,"and lets go at it");
        // The wearer's distances.
        auto w=Base();w.autoSnap=true;Solver ws;
        const auto arrive=[&](Vec3 at){w.support.position=away;frames(ws,w,3);w.support.position=at;return frames(ws,w,kDwell);};
        Check(!arrive({.15f,.35f,0}).held,"15 cm off the foregrip is out of the default 10");
        w.snapAttach=.2f;Check(arrive({.15f,.35f,0}).held,"a wider attach distance reaches it");
        w.snapAttach=.05f;Check(!arrive({.08f,.35f,0}).held,"a narrower one does not reach 8 cm");
        w.snapAttach=25*.01f;Check(arrive({.24f,.35f,0}).held,"the widest the setting goes still attaches");
        w.snapAttach=.1f;w.snapBreak=.4f;arrive(on);w.support.position={.3f,.35f,0};
        Check(ws.Update(w).held,"a wider break distance keeps it past the default");
        w.snapBreak=.15f;arrive(on);w.support.position={.2f,.35f,0};
        Check(!ws.Update(w).held,"a narrower one lets go sooner");
        w.snapAttach=.2f;w.snapBreak=.1f;arrive({.18f,.35f,0});
        Check(frames(ws,w,10).held,"a break nearer than the attach distance never lets go as it attaches");
        w.support.position={.24f,.35f,0};Check(ws.Update(w).held,"it stays 5 cm past the attach distance");
        w.support.position={.26f,.35f,0};Check(!ws.Update(w).held,"and lets go there");
    }
    std::cout<<"Two-handed aim geometry, lifecycle and barrel integration passed\n";
}
