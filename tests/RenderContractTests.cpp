#include "preyvr/RenderContract.h"
#include "preyvr/SettingReadback.h"
#include "TestSupport.h"
#include <cstdlib>
#include <iostream>
#include <limits>
using namespace preyvr;
using preyvr::test::Require;
int main() {
    RenderContract a{};a.eye=0;a.valid=true;a.displayTime=100;a.tangents={-1,1.2f,1.1f,-.9f};
    RenderContract b=a;b.eye=1;b.displayTime=200;b.pose.position.x=3;
    RenderContractQueue queue;RenderContract out;
    Require(ValidRenderContract(a),"valid asymmetric camera rejected");
    Require(queue.Push(a)&&queue.Push(b),"publish");
    a.pose.position.x=999; // producer moves again before the consumer runs
    Require(queue.Pop(out)&&out.pose.position.x==0&&out.displayTime==100&&out.eye==0,"retimed older pixels");
    Require(queue.Pop(out)&&out.pose.position.x==3&&out.displayTime==200&&out.eye==1,"second frame mismatch");
    Require(!queue.Pop(out)&&!out.valid,"underflow reused metadata");
    b.tangents[0]=std::numeric_limits<float>::quiet_NaN();Require(!ValidRenderContract(b),"NaN FOV accepted");
    b=a;b.tangents[0]=.2f;Require(!ValidRenderContract(b),"inverted FOV accepted");
    b=a;b.valid=false;Require(queue.Push(b)&&queue.Push(a),"refused frame placeholders");
    Require(queue.Pop(out)&&!ValidRenderContract(out),"refusal lost");
    Require(queue.Pop(out)&&out.displayTime==100,"refusal shifted next frame");
    for(int i=0;i<64;++i)Require(queue.Push(a),"capacity");
    Require(!queue.Push(a)&&!queue.Pop(out),"overflow silently skipped records");
    queue.Reset();Require(queue.Push(a)&&queue.Pop(out),"reset failed");
    b=a;b.pose.orientation.w=0;Require(!ValidRenderContract(b),"invalid pose accepted");
    b=a;b.displayTime=0;Require(!ValidRenderContract(b),"missing sample time accepted");
    // Both eyes from one frame (rung 1b): the head pose plus two eye poses, all
    // checked, and carried through the queue as one record.
    RenderContract pair=a;pair.eye=kRenderContractBothEyes;
    pair.eyePoses[0].position.x=-.032f;pair.eyePoses[1].position.x=.032f;
    Require(ValidRenderContract(pair),"valid both-eyes record rejected");
    b=pair;b.eyePoses[1].orientation.w=0;Require(!ValidRenderContract(b),"invalid right eye pose accepted");
    b=pair;b.eyePoses[0].position.y=std::numeric_limits<float>::infinity();
    Require(!ValidRenderContract(b),"non-finite left eye pose accepted");
    b=pair;b.eye=3;Require(!ValidRenderContract(b),"out-of-range eye accepted");
    b=a;b.eye=-1;Require(!ValidRenderContract(b),"missing eye accepted");
    // A zoom (preyvr/ViewZoom.h): declared wider than rendered by its magnification,
    // the compositor keeping the real eyes and what is drawn in the image the
    // rendered ones.
    {
        RenderContract zoomed=pair;
        Require(DeclaredTangents(zoomed)==zoomed.tangents,"no zoom: declared exactly as rendered");
        Require(RenderedEyePose(zoomed,0).position.x==-.032f&&RenderedEyePose(zoomed,1).position.x==.032f,
                "no zoom: rendered from the real eyes");
        zoomed.zoom=4.f;zoomed.pose.position={1,2,3};
        zoomed.eyePoses[0].position={1-.032f,2,3};zoomed.eyePoses[1].position={1+.032f,2,3};
        Require(ValidRenderContract(zoomed),"zoomed record rejected");
        const auto declared=DeclaredTangents(zoomed);
        Require(declared[0]==-4.f&&declared[1]==4.8f&&declared[2]==4.4f&&declared[3]==-3.6f,"declared wider by the zoom");
        const Pose left=RenderedEyePose(zoomed,0),right=RenderedEyePose(zoomed,1);
        Require(std::abs(left.position.x-(1-.008f))<1e-5f&&std::abs(right.position.x-(1+.008f))<1e-5f&&
                left.position.y==2&&right.position.z==3,"rendered from eyes closer to the head by the zoom");
        Require(left.orientation.w==zoomed.eyePoses[0].orientation.w,"facing as the real eye");
        b=zoomed;b.zoom=.5f;Require(!ValidRenderContract(b),"a view declared narrower than rendered accepted");
        b=zoomed;b.zoom=std::numeric_limits<float>::infinity();Require(!ValidRenderContract(b),"non-finite zoom accepted");
    }
    queue.Reset();Require(queue.Push(pair)&&queue.Pop(out),"both-eyes publish");
    Require(out.eye==kRenderContractBothEyes&&out.eyePoses[0].position.x==-.032f&&
            out.eyePoses[1].position.x==.032f&&out.displayTime==pair.displayTime,"both-eyes record altered");
    // A level-transition load (2026-10-22): Prey's main loop rendered ~330 times
    // while its render thread ended the frame once. The first both-eyes frame
    // after it finds its own record by camera and drops the backlog; the old
    // queue stayed poisoned and the headset black until VR was restarted.
    {
        RenderContractQueue q;RenderContract got;std::size_t skipped=0;
        const auto frame=[&](int n){
            RenderContract c=pair;c.displayTime=1000+n;c.haveCamera=true;
            c.camera={1,0,0,100.f+n*.01f, 0,1,0,50, 0,0,1,2};
            return c;
        };
        Require(q.Push(frame(1))&&q.Push(frame(2)),"steady publish");
        Require(q.PopMatching(frame(1).camera,got,skipped)&&got.displayTime==1001&&skipped==0,
                "steady frame takes its own record");
        Require(q.Size()==1,"the next frame's record stays queued");
        bool dropped=false;
        for(int n=3;n<=332;++n) dropped|=!q.Push(frame(n));
        Require(dropped&&q.Size()==64,"a long load drops the oldest records and stays bounded");
        Require(!q.Pop(got)&&!got.valid,"position after an overflow is still refused");
        q.Push(frame(333));
        Require(q.PopMatching(frame(333).camera,got,skipped)&&got.displayTime==1333&&skipped==63,
                "first frame after the load finds its own record");
        Require(q.Size()==0,"the load's backlog is dropped");
        Require(q.Push(frame(334))&&q.Pop(got)&&got.displayTime==1334,"position works again after the resync");
        Require(q.Push(frame(335)),"publish");
        auto away=frame(335).camera;away[3]+=.001f;
        Require(!q.PopMatching(away,got,skipped)&&q.Size()==1&&!got.valid,
                "a frame no record matches changes nothing");
        const float nearest=q.NearestCameraDifference(away);
        Require(nearest>.0009f&&nearest<.0011f,"nearest difference reports the miss");
        auto copy=frame(335).camera;copy[7]+=5e-5f;
        Require(q.PopMatching(copy,got,skipped)&&got.displayTime==1335,"a by-value copy within noise matches");
        RenderContract bare=frame(336);bare.haveCamera=false;
        Require(q.Push(bare)&&q.Push(frame(337)),"publish a render without a camera");
        Require(!q.PopMatching(frame(336).camera,got,skipped)&&q.Size()==2,"a record without a camera never matches");
        Require(q.PopMatching(frame(337).camera,got,skipped)&&skipped==1&&got.displayTime==1337,
                "a record without a camera is dropped as backlog");
        Require(q.NearestCameraDifference(away)==-1.f,"no candidates reports -1");
    }
    // Submission a frame later: refused by a change of tracking space, never by
    // a snap or a recenter, which leave the eye poses where they were.
    RenderContract built=pair;built.reference=4;built.spaceEpoch=1;
    Require(SubmittableRenderContract(built,1),"a frame built before a snap was dropped");
    Require(!SubmittableRenderContract(built,2),"a frame from the previous tracking space submitted");
    b=built;b.spaceEpoch=0;Require(!SubmittableRenderContract(b,0),"a frame with no tracking space submitted");
    b=built;b.reference=5;Require(!SubmittableRenderContract(b,1),"an unsettled reference submitted");
    b=built;b.valid=false;Require(!SubmittableRenderContract(b,1),"a refused frame submitted");
    SettingReadback readback{1,1000};
    Require(readback.Observe(false,1,1)==ReadbackResult::pending,"unread value accepted");
    Require(readback.Observe(true,3,999)==ReadbackResult::pending,"queue acknowledgement accepted");
    Require(readback.Observe(true,1,500)==ReadbackResult::verified,"applied value rejected");
    Require(readback.Observe(true,3,1000)==ReadbackResult::failed,"ignored setting did not fail");
    Require(readback.Observe(false,1,1000)==ReadbackResult::failed,"missing CVar did not fail");
    std::cout<<"Render contract and CVar readback regressions passed\n";
}
