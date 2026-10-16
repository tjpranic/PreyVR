// HUD parts (preyvr/NativeHudIsolation.h): the wheel's replay profile against
// the movie's named layout, the HUD draw's group exclusions, and when a part
// is lifted. The native calls are faked; the transactions are the real ones.
#include "preyvr/NativeHudIsolation.h"
#include <cstdlib>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <initializer_list>
#include <vector>
using namespace preyvr;
void Require(bool b,const char* why){if(!b){std::cerr<<why<<'\n';std::exit(1);}}
namespace {
// One object per path, so a test can ask what a path resolved to; `missing`
// paths fail to resolve, and every handle taken must be released once.
struct Api {
    using Value=int;
    std::map<std::string,int> objects;
    std::set<std::string> missing;
    int next=0,live=0,released=0;
    bool Resolve(const char* path,Value& value,void*& object){
        value=++next;++live;
        auto& slot=objects[path];
        object=&slot;
        return !missing.count(path);
    }
    bool Release(Value& value){if(!value)return true;value=0;--live;++released;return true;}
    void* Of(const char* path){return &objects[path];}
};
std::set<std::string> Set(std::span<const char* const> paths){return {paths.begin(),paths.end()};}
}
// A part's replay against the mapped layout: it leaves out exactly every other
// child of each container its kept clips sit in; any probe or override touches
// only what it keeps or a container of it.
void CheckLayout(const hud::Part& part){
    std::map<std::string,std::set<std::string>> tree;
    tree["_root"]=Set(hud::RootChildren);
    tree["_root.safe"]=Set(hud::SafeChildren);
    for(const char* path:hud::SafeAttached)tree["_root.safe"].insert(path);
    tree["_root.safe.quadrant_S"]=Set(hud::QuadrantSChildren);
    tree["_root.safe.quadrant_SE"]=Set(hud::QuadrantSEChildren);
    tree["_root.safe.quadrant_C"]=Set(hud::QuadrantCChildren);
    tree["_root.safe.quadrant_W"]=Set(hud::QuadrantWChildren);
    const auto roots=Set(part.roots);
    Require(roots.size()==part.roots.size(),"each kept clip once");
    std::set<std::string> containers;
    for(const auto& root:roots)
        for(auto dot=root.rfind('.');dot!=std::string::npos&&dot>0;dot=root.rfind('.',dot-1))containers.insert(root.substr(0,dot));
    for(const auto& c:containers)Require(tree.count(c)==1,"a part reaches only into mapped containers");
    std::set<std::string> expected;
    for(const auto& c:containers)for(const auto& child:tree[c])if(!roots.count(child)&&!containers.count(child))expected.insert(child);
    Require(Set(part.replay.excluded)==expected&&part.replay.excluded.size()==expected.size(),
        "the replay leaves out every other clip beside what it keeps, each once");
    Require(Set(part.replay.observed)==roots,"proved by reaching what it keeps");
    Require(part.replay.excluded.size()+part.replay.observed.size()<=hud::MaxProfilePaths,"within capacity");
    const auto touches=[&](const std::string& clip){
        if(containers.count(clip))return true;
        for(const auto& root:roots)if(clip==root||clip.rfind(root+".",0)==0)return true;
        return false;
    };
    for(const char* probe:part.probes)Require(touches(probe),"each probe is something the part carries");
    Require(part.overrides.size()<=hud::MaxOverrides&&part.probes.size()<=hud::MaxProbes,"overrides and probes within capacity");
    for(const auto& o:part.overrides){
        const std::string path=o.path;
        Require(touches(path.substr(0,path.rfind('.'))),"each override moves what the part carries, or its container");
    }
}
int main(){
    // --- the reticle: the crosshair and the block that follows it ---------------
    {
        CheckLayout(hud::Reticle);
        CheckLayout(hud::Wheel);
        Require(Set(hud::Reticle.roots)==std::set<std::string>{"_root.safe.reticle_mc","_root.safe.quadrant_C"},
            "the crosshair and the interaction prompt, quick loot and mash prompt that follow it");
        Require(hud::Reticle.gates.empty(),"ungated: lifted whenever the world reticle asks");
        std::set<std::string> excluded=Set(hud::Reticle.replay.excluded);
        Require(excluded.count("_root.safe.quick_mc")&&excluded.count("_root.safe.assign_mc"),
            "the wheel's flourishes are not drawn with the crosshair");
        std::map<std::string,const hud::Override*> by;
        for(const auto& o:hud::Reticle.overrides)by[o.path]=&o;
        for(const char* clip:{"_root.safe.reticle_mc","_root.safe.quadrant_C"})
            for(const char* axis:{"._x","._y"}){
                const auto o=by.find(std::string(clip)+axis);
                Require(o!=by.end()&&o->second->value==0.0&&!o->second->base,"centred: where a centred crosshair puts it, 0");
            }
        const auto sx=by.find("_root.safe._x"),sy=by.find("_root.safe._y");
        Require(sx!=by.end()&&sy!=by.end()&&sx->second->value==0.0&&sy->second->value==0.0&&
            std::string(sx->second->base)=="_root.cam.m_halfStageWidth"&&std::string(sy->second->base)=="_root.cam.m_halfStageHeight",
            "and safe where HUDPos(0, 0) puts it, at half the stage, without the HUD's sway");
        Require(by.size()==6,"nothing else is moved");
        // The two parts never lift the same clip.
        for(const char* r:hud::Reticle.roots)for(const char* w:hud::Wheel.roots)
            Require(std::string(r)!=w,"the reticle and the wheel carry different clips");
    }
    // --- the wheel's profile is the layout less what it keeps ----------------
    {
        // The layout as a tree: every child of each container a part reaches into.
        std::map<std::string,std::set<std::string>> tree;
        tree["_root"]=Set(hud::RootChildren);
        tree["_root.safe"]=Set(hud::SafeChildren);
        for(const char* path:hud::SafeAttached)tree["_root.safe"].insert(path);
        tree["_root.safe.quadrant_S"]=Set(hud::QuadrantSChildren);
        tree["_root.safe.quadrant_SE"]=Set(hud::QuadrantSEChildren);
        tree["_root.safe.quadrant_C"]=Set(hud::QuadrantCChildren);
        tree["_root.safe.quadrant_W"]=Set(hud::QuadrantWChildren);
        const auto roots=Set(hud::Wheel.roots);
        Require(roots.size()==hud::Wheel.roots.size()&&roots.count("_root.safe.wheel_mc"),"the wheel and its companions, each once");
        const auto ancestors=[](const std::string& path){
            std::set<std::string> out;
            for(auto dot=path.rfind('.');dot!=std::string::npos&&dot>0;dot=path.rfind('.',dot-1))out.insert(path.substr(0,dot));
            return out;
        };
        // A root inside another (the details panel inside the wheel) is named only
        // so the HUD's draw leaves it out directly; it needs no container of its own.
        std::set<std::string> top;
        for(const auto& root:roots){
            bool nested=false;
            for(const auto& above:ancestors(root))nested=nested||roots.count(above);
            if(!nested)top.insert(root);
        }
        // Every proper ancestor of a top-level kept clip is a container whose children are all known...
        std::set<std::string> containers;
        for(const auto& root:top)for(const auto& above:ancestors(root))containers.insert(above);
        for(const auto& container:containers)Require(tree.count(container)==1,"a part reaches only into containers whose children are mapped");
        for(const auto& root:top){
            const auto parent=root.substr(0,root.rfind('.'));
            Require(tree[parent].count(root)==1,"each kept clip is one of its container's mapped children");
        }
        // ...so the replay must leave out exactly every other child of those containers.
        std::set<std::string> expected;
        for(const auto& container:containers)
            for(const auto& child:tree[container])
                if(!top.count(child)&&!containers.count(child))expected.insert(child);
        Require(Set(hud::Wheel.replay.excluded)==expected,"the wheel's replay leaves out every other clip beside what it keeps, and only those");
        Require(hud::Wheel.replay.excluded.size()==expected.size(),"each once");
        Require(Set(hud::Wheel.replay.observed)==roots,"and is proved by reaching any of what it keeps");
        Require(Set(hud::Wheel.gates)==std::set<std::string>{"_root.spiral.isOpen","_root.spiralDetails.isOpen",
            "_root.spiralPrompts.isOpen"},"gated on the wheel's, its details panel's and its prompts' isOpen");
        Require(hud::Wheel.gates.size()<=hud::MaxGates&&hud::Wheel.probes.size()<=hud::MaxProbes,"gates and probes within capacity");
        for(const char* probe:hud::Wheel.probes){
            bool inside=false;
            for(const auto& root:roots)inside=inside||std::string(probe).rfind(root,0)==0;
            Require(inside,"each probe is something the wheel part should carry");
        }
        Require(hud::Wheel.replay.excluded.size()+hud::Wheel.replay.observed.size()<=hud::MaxProfilePaths,"within capacity");
        // The controls box Prey shows with the wheel carries its Equip/Cancel prompt.
        Require(roots.count("_root.safe.quadrant_W.tutorial")==1,"the wheel carries the controls box");
        Require(!roots.count("_root.safe.quadrant_C.interact"),"and not the interaction prompt, which goes with the reticle");
        // Nothing the wheel carries moves with the crosshair, so no overrides; any a
        // part names must move something it carries.
        Require(hud::Wheel.overrides.empty()&&hud::Wheel.overrides.size()<=hud::MaxOverrides,"no overrides for the wheel");
        std::set<std::string> axes;
        for(const auto& o:hud::Wheel.overrides){
            const std::string path=o.path;
            const auto clip=path.substr(0,path.rfind('.'));
            bool carries=false;
            for(const auto& root:roots)carries=carries||root.rfind(clip+".",0)==0||root==clip;
            Require(carries,"each override moves something the wheel carries");
            Require(o.value==0.0,"to the centred crosshair's offset, 0");
            axes.insert(path.substr(path.rfind('.')+1));
        }
        Require(axes.empty(),"none");
        // The wrist's older list agrees with the layout read for the parts.
        std::set<std::string> status=Set(hud::ExcludedFromStatus);
        for(const char* path:hud::SafeChildren)
            if(std::string(path)!="_root.safe.quadrant_SW")Require(status.count(path)==1,"the wrist leaves out safe's other children");
    }
    // --- the wheel's replay ---------------------------------------------------
    {
        Api api;
        {
            hud::Isolation guard(api,hud::Wheel.replay);
            Require(guard.Begin(),"the wheel's replay resolves");
            for(const char* path:hud::Wheel.replay.excluded)Require(guard.Excludes(api.Of(path)),"every other clip left out");
            for(const char* path:hud::Wheel.roots)Require(!guard.Excludes(api.Of(path)),"the wheel and its companions drawn");
            Require(!guard.Complete(),"not yet reached");
            int unrelated=0;guard.Observe(&unrelated);
            Require(!guard.Complete(),"something else drawn proves nothing");
            guard.Observe(api.Of("_root.safe.quadrant_S.quickSelect_mc"));
            Require(guard.Complete(),"reaching any of them proves the replay drew the part (the hotbar fading after the wheel)");
        }
        Require(api.live==0,"every handle released");
        for(const char* missing:hud::Wheel.roots){
            Api broken;broken.missing.insert(missing);
            {hud::Isolation guard(broken,hud::Wheel.replay);Require(!guard.Begin(),"any kept clip that does not resolve refuses the replay");}
            Require(broken.live==0,"and releases what it took");
        }
        Api status;
        {hud::Isolation guard(status);Require(guard.Begin(),"the wrist's default profile is unchanged");
            Require(guard.Excludes(status.Of("_root.safe.wheel_mc"))&&!guard.Excludes(status.Of(hud::StatusMeters[0])),
                "it still leaves out the wheel and keeps the meters");}
    }
    // --- the HUD's own draw: groups left out whole or not at all ---------------
    {
        const char* const meters[]={"a","b","c"};
        Api api;api.missing.insert("b");
        {
            hud::Exclusion<Api> filter(api);
            Require(!filter.Add(meters),"a group with a missing path is refused");
            Require(filter.Count()==0&&!filter.Excludes(api.Of("a")),"and none of it is left out");
            Require(filter.Add(hud::Wheel.roots),"another group still is");
            Require(filter.Count()==hud::Wheel.roots.size(),"only that one");
            for(const char* path:hud::Wheel.roots)Require(filter.Excludes(api.Of(path)),"all of the wheel's group left out");
            Require(filter.Release()&&api.live==0,"every handle released, the failed group's too");
            Require(!filter.Excludes(api.Of(hud::Wheel.roots[0])),"and nothing left out after");
        }
        Api partial;partial.missing.insert("_root.safe.quadrant_W.tutorial");
        {
            hud::Exclusion<Api> filter(partial);
            Require(!filter.Add(hud::Wheel.roots)&&filter.Count()==0,"one missing companion keeps the whole wheel in the HUD's draw");
        }
        Require(partial.live==0,"and releases what it took");
        Api capacity;
        {
            hud::Exclusion<Api> filter(capacity);
            const char* const status[]={"s1","s2","s3","s4","s5","s6","s7"};
            Require(filter.Add(status)&&filter.Add(hud::Wheel.roots),"the wrist's seven meters and the wheel's group fit together");
        }
        Api full;
        {
            hud::Exclusion<Api,2> filter(full);
            const char* const three[]={"x","y","z"};
            Require(!filter.Add(three)&&filter.Count()==0,"a group past capacity is refused whole");
        }
        Require(full.live==0,"the destructor releases what was taken");
    }
    // --- a part's gates as one -----------------------------------------------------
    {
        const auto combine=[](std::initializer_list<int> gates){
            const std::vector<int> v(gates);return hud::CombineGates(std::span<const int>(v.data(),v.size()));};
        Require(combine({0,1,0})==1,"any gate open opens the part: the details panel outlasting the wheel");
        Require(combine({0,0,0})==0,"all closed: closed");
        Require(combine({-1,0,-1})==0,"an unreadable gate does not keep it open, a readable closed one closes it");
        Require(combine({-1,1,-1})==1,"nor does it stop another from opening it");
        Require(combine({-1,-1,-1})==-1,"none readable: unreadable, so never lifted");
        Require(combine({})==-1,"no gates read: unreadable");
    }
    // --- when a part is lifted --------------------------------------------------
    {
        constexpr std::uint64_t ms=1'000'000;
        hud::PartLift lift;
        Require(!lift.Decide(false,1,100*ms),"not requested: left where Prey draws it");
        Require(!lift.Decide(true,0,110*ms),"requested but closed: nothing to lift");
        Require(!lift.Decide(true,-1,120*ms),"an unreadable gate never lifts");
        Require(lift.Decide(true,1,130*ms),"open: lifted");
        lift.Drew(true);
        Require(lift.Decide(true,0,200*ms),"closing: kept through its animation while it still draws");
        lift.Drew(true);
        Require(lift.Decide(true,0,600*ms),"still drawing");
        lift.Drew(false);
        Require(!lift.Decide(true,0,610*ms),"drawn out: back to the HUD");
        Require(!lift.Decide(true,0,620*ms),"and stays there while closed");
        Require(lift.Decide(true,1,700*ms),"reopened: lifted again");
        lift.Drew(true);
        Require(lift.Decide(true,0,700*ms+hud::PartTrailNs),"trailing up to the limit");
        lift.Drew(true);
        Require(!lift.Decide(true,0,701*ms+hud::PartTrailNs),"never past it, even if it keeps drawing");
        Require(lift.Decide(true,1,2000*ms),"open again");
        lift.Drew(true);
        Require(!lift.Decide(false,0,2010*ms),"no longer requested: dropped at once");
        Require(!lift.Decide(true,0,2020*ms),"and not resumed as a trail");
        Require(lift.Decide(true,1,2030*ms),"open");
        lift.Drew(true);
        Require(!lift.Decide(true,-1,2040*ms),"a gate gone unreadable drops it");
        lift.Reset();
        Require(!lift.Decide(true,0,2050*ms),"reset: nothing to trail");
    }
    std::cout<<"HUD parts: wheel profile, exclusions and lifting passed\n";
}
