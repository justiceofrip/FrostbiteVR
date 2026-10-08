#pragma once
#include <array>
#include <cstdint>

namespace fvr::bc2 {
struct RetiredStereoView {
    std::uint32_t address=0,request=0,parent=0,references=0;
    bool nativeTypeVerified=false;
    bool operator==(const RetiredStereoView&)const=default;
};
// Same-world shutdown must retire only the private eye subtree. Native original
// children can be re-registered while a session runs. Snapshot their CURRENT
// ordered generation before Release, then compare that exact order afterwards.
struct LiveStereoRetirementGraph {
    std::uint32_t world=0,request=0,main=0,eye=0,requestReferences=0,count=0;
    std::uint32_t mainChildren=0,eyeChildren=0;
    bool nativeOwnersVerified=false,requestRegistered=false,callbacksVerified=false;
    std::array<RetiredStereoView,64> views{};
    bool operator==(const LiveStereoRetirementGraph&)const=default;
};
struct StereoSurvivors {
    std::uint32_t count=0,removed=0,referencesAfter=0;
    std::array<std::uint32_t,64> ordered{};
    bool operator==(const StereoSurvivors&)const=default;
};
inline bool CaptureLiveStereoSurvivors(const LiveStereoRetirementGraph& g,StereoSurvivors& result)noexcept {
    if(!g.world||!g.request||!g.main||!g.eye||g.main==g.eye||!g.nativeOwnersVerified||
       !g.requestRegistered||!g.callbacksVerified||!g.mainChildren||!g.eyeChildren||
       g.mainChildren>8||g.eyeChildren>8||g.count>g.views.size()||
       g.count!=2+2*g.mainChildren+2*g.eyeChildren||g.requestReferences!=g.count+1)return false;
    StereoSurvivors out{};unsigned mains=0,eyes=0;
    for(unsigned n=0;n<g.count;++n){const auto& v=g.views[n];
        if(!v.address||!v.nativeTypeVerified||v.request!=g.request||v.references!=1)return false;
        for(unsigned j=0;j<n;++j)if(v.address==g.views[j].address)return false;
        if(v.address==g.main){if(v.parent)return false;++mains;}
        else if(v.address==g.eye){if(v.parent)return false;++eyes;}
        else if(v.parent!=g.main&&v.parent!=g.eye)return false;
        if(v.address==g.eye||v.parent==g.eye)++out.removed;
        else out.ordered[out.count++]=v.address;
    }
    if(mains!=1||eyes!=1||out.count!=1+2*g.mainChildren||out.removed!=1+2*g.eyeChildren)return false;
    out.referencesAfter=g.requestReferences-out.removed;result=out;return true;
}
struct RetiredStereoGraph {
    std::uint32_t world=0,request=0,eye=0,currentWorld=0,currentRequest=0;
    std::uint32_t requestReferences=0,worldReferences=0,mainCount=0,mainEntry=0,count=0,children=0;
    bool nativeOwnersVerified=false,requestRegistered=false,callbacksVerified=false;
    std::array<RetiredStereoView,64> views{};
};
struct RetiredViewCache {
    std::uint32_t callback=0,type=0,references=0,scene=0,begin=0,end=0,capacity=0;
    bool operator==(const RetiredViewCache&)const=default;
};
inline bool ValidRetiredViewCache(const RetiredViewCache& c)noexcept {
    if(!c.callback||!c.type||!c.references)return false;
    if(!c.begin)return !c.end&&!c.capacity;
    return !(c.begin&3u)&&!(c.end&3u)&&!(c.capacity&3u)&&c.begin<=c.end&&c.end<=c.capacity&&
        (c.end-c.begin)%20u==0&&(c.capacity-c.begin)%20u==0&&c.capacity-c.begin<=4096u*20u;
}
// A retired world's callback may survive its scene entity-table teardown.
// Never rebuild its cache through the scene. The adapter supplies the exact
// native vector shrink operation used by the ordinary cache implementation.
// Preserve owner identity and native allocation while observing every release.
template<class Calls> bool RetireDetachedViewCache(Calls& calls)noexcept {
    RetiredViewCache before{},observed{},after{};
    if(!calls.Capture(before)||!ValidRetiredViewCache(before)||!calls.Detached()||
       !calls.Capture(observed)||observed!=before)return false;
    if(before.end!=before.begin&&!calls.ShrinkToZero(before))return false;
    if(!calls.Capture(after)||!ValidRetiredViewCache(after)||!calls.Detached())return false;
    auto expected=before;expected.end=before.begin;
    return after==expected;
}
// Only the orphaned extra-eye subtree is supported. An old world's normal views,
// shared child references, rebound nodes, duplicate entries or shared callback
// registries cannot authorize destruction. The adapter captures all fields at
// the verified update boundary and rechecks exact lists before the first call.
inline bool CanRetireStereoGraph(const RetiredStereoGraph& g)noexcept {
    if(!g.world||!g.request||!g.eye||!g.currentWorld||!g.currentRequest||
       g.world==g.currentWorld||g.request==g.currentRequest||!g.nativeOwnersVerified||
       !g.requestRegistered||!g.callbacksVerified||g.worldReferences!=1||
       g.mainCount!=1||g.mainEntry!=g.eye||!g.count||g.count>g.views.size()||
       g.requestReferences!=g.count||g.children>8||g.count!=1+g.children*2)return false;
    unsigned roots=0;
    for(unsigned i=0;i<g.count;++i){const auto& v=g.views[i];
        if(!v.address||v.request!=g.request||v.references!=1||!v.nativeTypeVerified)return false;
        for(unsigned j=0;j<i;++j)if(g.views[j].address==v.address)return false;
        if(v.address==g.eye){if(v.parent)return false;++roots;}
        else if(v.parent!=g.eye)return false;
    }
    return roots==1;
}
}
