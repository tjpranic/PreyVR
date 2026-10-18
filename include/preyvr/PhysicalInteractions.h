#pragma once
#include "preyvr/SceneQuery.h"
#include <array>
#include <cstdint>

// The weapon box: the drawn weapon's bounds, placed where the hand holds it, as
// the contact lane (ContactLane.cpp) and the free-swing wrench (SwingLane.cpp)
// query them. Pure geometry here; the DLL half publishes it (PhysicalInteractions.cpp).
namespace preyvr::physical {
using preyvr::Add;
using preyvr::Length;
using preyvr::Scale;
using preyvr::Sub;
inline Pose Inverse(Pose p){auto q=Conjugate(p.orientation);return {q,Rotate(q,Scale(p.position,-1))};}
inline Vec3 Transform(Pose p,Vec3 v){return Add(p.position,Rotate(p.orientation,v));}
using Points=std::array<Vec3,9>;
// A box's centre, then corner i+1 whose bits x/y/z pick `hi` on that axis.
inline Points Bounds(Vec3 lo,Vec3 hi){
    Points p{};p[0]=Scale(Add(lo,hi),.5f);
    for(unsigned i=0;i<8;++i)p[i+1]={i&1?hi.x:lo.x,i&2?hi.y:lo.y,i&4?hi.z:lo.z};
    return p;
}
// One published pose of the box: its points and the grip in tracking reference
// space (engine axes), and the map to the world at that sample.
struct Sample {
    Points points{}; // engine axes, XR reference space; never world history
    Pose grip{},worldFromReference{};
    std::uint64_t sequence=0,stamp=0,owner=0,epoch=0,reference=0;
};
}
