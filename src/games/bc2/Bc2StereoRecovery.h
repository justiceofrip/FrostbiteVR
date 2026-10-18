#pragma once
#include <array>
#include <cstdint>

namespace fvr::bc2 {
struct RetiredStereoView {
    std::uint32_t address=0,request=0,parent=0,references=0;
    bool nativeTypeVerified=false;
    bool operator==(const RetiredStereoView&)const=default;
};
struct StereoChildHolder {
    std::uint32_t object=0,control=0,owners=0,first=0,second=0;
    bool operator==(const StereoChildHolder&)const=default;
};
// Same-world shutdown must retire only the private eye subtree. Native original
// children can be re-registered while a session runs. Snapshot their CURRENT
// ordered generation before Release, then compare that exact order afterwards.
struct LiveStereoRetirementGraph {
    std::uint32_t world=0,request=0,main=0,eye=0,requestReferences=0,count=0;
    std::uint32_t mainChildren=0,eyeChildren=0;
    bool nativeOwnersVerified=false,requestRegistered=false,callbacksVerified=false;
    std::array<RetiredStereoView,64> views{};
    // A new inactive view retains its children in an owned pending queue until
    // native update consumes it. Its destructor releases both holders and queue.
    // Additional references are admissible only with those exact owners proved.
    bool pendingEyeOwnershipVerified=false;
    std::uint32_t pendingEyeCount=0;
    // Pending entries can span retired child generations. Use the same bound
    // as the registered-view snapshot, not the current holder-count bound.
    std::array<std::uint32_t,64> pendingEye{};
    std::array<StereoChildHolder,8> eyeHolders{};
    bool operator==(const LiveStereoRetirementGraph&)const=default;
};
struct StereoSurvivors {
    std::uint32_t count=0,removed=0,referencesAfter=0;
    std::array<std::uint32_t,64> ordered{};
    bool operator==(const StereoSurvivors&)const=default;
};
// The same native subtree destructor owns these references during session exit
// and after scene replacement. Neither path may infer ownership from refcount2.
template<class Graph> bool OwnsPendingStereoChildren(const Graph& g,unsigned children)noexcept {
    if(g.pendingEyeCount>g.pendingEye.size()||children>g.eyeHolders.size()||g.count>g.views.size())return false;
    if(g.pendingEyeCount){
        if(!g.pendingEyeOwnershipVerified||!children)return false;
        for(unsigned i=0;i<children;++i){const auto& h=g.eyeHolders[i];
            if(!h.object||!h.control||h.owners!=1||!h.first||!h.second||h.first==h.second)return false;
            for(unsigned j=0;j<i;++j){const auto& other=g.eyeHolders[j];
                if(h.object==other.object||h.control==other.control||h.first==other.first||h.first==other.second||
                   h.second==other.first||h.second==other.second)return false;
            }
            for(auto child:{h.first,h.second}){
                unsigned matches=0;
                for(unsigned n=0;n<g.count;++n)if(g.views[n].address==child&&g.views[n].parent==g.eye)++matches;
                if(matches!=1)return false;
            }
        }
        for(unsigned i=0;i<g.pendingEyeCount;++i){
            unsigned registered=0;
            for(unsigned n=0;n<g.count;++n)registered+=g.views[n].address==g.pendingEye[i]&&g.views[n].parent==g.eye;
            if(registered!=1)return false;
            for(unsigned j=0;j<i;++j)if(g.pendingEye[j]==g.pendingEye[i])return false;
        }
    }
    return true;
}
// A native child generation can be replaced while its previous generation still
// waits in the same owned queue. Current children have holder+queue refs; older
// queued children have queue refs only. Count the verified union, never infer a
// permanent holder reference for a child solely because it is registered.
template<class Graph> unsigned OwnedStereoChildCount(const Graph& g,unsigned children)noexcept {
    unsigned count=children*2;
    for(unsigned i=0;i<g.pendingEyeCount;++i){bool held=false;
        for(unsigned h=0;h<children;++h)held|=g.eyeHolders[h].first==g.pendingEye[i]||g.eyeHolders[h].second==g.pendingEye[i];
        count+=!held;
    }
    return count;
}
template<class Graph> unsigned OwnedStereoViewReferences(const Graph& g,std::uint32_t address,unsigned children)noexcept {
    if(!g.pendingEyeCount||address==g.eye)return 1;
    bool child=false;for(unsigned n=0;n<g.count;++n)child|=g.views[n].address==address&&g.views[n].parent==g.eye;
    if(!child)return 1;
    unsigned references=0;
    for(unsigned h=0;h<children;++h)references+=(g.eyeHolders[h].first==address)+(g.eyeHolders[h].second==address);
    for(unsigned i=0;i<g.pendingEyeCount;++i)references+=g.pendingEye[i]==address;
    return references;
}
inline bool CaptureLiveStereoSurvivors(const LiveStereoRetirementGraph& g,StereoSurvivors& result)noexcept {
    if(!g.world||!g.request||!g.main||!g.eye||g.main==g.eye||!g.nativeOwnersVerified||
       !g.requestRegistered||!g.callbacksVerified||!g.mainChildren||!g.eyeChildren||
       g.mainChildren>8||g.eyeChildren>8||g.count>g.views.size()||
       g.requestReferences!=g.count+1||!OwnsPendingStereoChildren(g,g.eyeChildren)||
       g.count!=2+2*g.mainChildren+OwnedStereoChildCount(g,g.eyeChildren))return false;
    StereoSurvivors out{};unsigned mains=0,eyes=0;
    for(unsigned n=0;n<g.count;++n){const auto& v=g.views[n];
        if(!v.address||!v.nativeTypeVerified||v.request!=g.request||v.references!=OwnedStereoViewReferences(g,v.address,g.eyeChildren))return false;
        for(unsigned j=0;j<n;++j)if(v.address==g.views[j].address)return false;
        if(v.address==g.main){if(v.parent)return false;++mains;}
        else if(v.address==g.eye){if(v.parent)return false;++eyes;}
        else if(v.parent!=g.main&&v.parent!=g.eye)return false;
        if(v.address==g.eye||v.parent==g.eye)++out.removed;
        else out.ordered[out.count++]=v.address;
    }
    if(mains!=1||eyes!=1||out.count!=1+2*g.mainChildren||out.removed!=1+OwnedStereoChildCount(g,g.eyeChildren))return false;
    out.referencesAfter=g.requestReferences-out.removed;result=out;return true;
}
struct RetiredStereoGraph {
    std::uint32_t world=0,request=0,eye=0,currentWorld=0,currentRequest=0;
    std::uint32_t requestReferences=0,worldReferences=0,mainCount=0,mainEntry=0,count=0,children=0;
    bool nativeOwnersVerified=false,requestRegistered=false,callbacksVerified=false;
    std::array<RetiredStereoView,64> views{};
    bool pendingEyeOwnershipVerified=false;
    std::uint32_t pendingEyeCount=0;
    std::array<std::uint32_t,64> pendingEye{};
    std::array<StereoChildHolder,8> eyeHolders{};
    bool operator==(const RetiredStereoGraph&)const=default;
};
struct RetiredViewCache {
    std::uint32_t callback=0,type=0,references=0,scene=0,begin=0,end=0,capacity=0;
    bool operator==(const RetiredViewCache&)const=default;
};
// A live terrain cache borrows its first pointer from the registered-view list.
// Native root Draw can release an older queued child generation after Rebuild
// populated that cache. Compare pointer values only: stale cached views must
// never be dereferenced to decide whether they need replacing.
struct LiveViewCache {
    std::uint32_t world=0,request=0,callback=0,type=0,context=0,scene=0,references=0;
    std::uint32_t registeredBegin=0,registeredEnd=0,registeredCapacity=0;
    std::uint32_t cacheBegin=0,cacheEnd=0,cacheCapacity=0;
    std::uint32_t registeredCount=0,cacheCount=0;
    std::uint8_t dirty=0;
    bool ownerVerified=false,boundaryVerified=false,registeredViewsVerified=false;
    std::array<std::uint32_t,64> registered{},cached{};
    bool operator==(const LiveViewCache&)const=default;
};
enum class LiveViewCacheResult {Unchanged,Refreshed,Disabled,Rejected};
inline bool ValidLiveViewVector(std::uint32_t begin,std::uint32_t end,std::uint32_t capacity,
    std::uint32_t stride,std::uint32_t count)noexcept {
    if(stride!=4&&stride!=20)return false;
    if(!begin)return !end&&!capacity&&!count;
    return !(begin&3u)&&!(end&3u)&&!(capacity&3u)&&begin<=end&&end<=capacity&&
        count<=64&&end-begin==count*stride&&(capacity-begin)%stride==0&&capacity-begin<=4096u*stride;
}
inline bool ValidLiveViewCache(const LiveViewCache& c)noexcept {
    if(!c.world||!c.request||!c.callback||!c.type||!c.context||!c.references||
       !c.ownerVerified||!c.boundaryVerified||!c.registeredViewsVerified||c.dirty>1||
       !c.registeredCount||c.registeredCount>c.registered.size()||c.cacheCount>c.cached.size()||
       !ValidLiveViewVector(c.registeredBegin,c.registeredEnd,c.registeredCapacity,4,c.registeredCount)||
       !ValidLiveViewVector(c.cacheBegin,c.cacheEnd,c.cacheCapacity,20,c.cacheCount))return false;
    for(unsigned i=0;i<c.registeredCount;++i){
        if(!c.registered[i])return false;
        for(unsigned j=0;j<i;++j)if(c.registered[i]==c.registered[j])return false;
    }
    return true;
}
inline bool LiveViewCacheMatches(const LiveViewCache& c)noexcept {
    if(c.cacheCount!=c.registeredCount)return false;
    for(unsigned i=0;i<c.registeredCount;++i)if(c.cached[i]!=c.registered[i])return false;
    return true;
}
inline bool SameLiveViewCacheOwner(const LiveViewCache& a,const LiveViewCache& b)noexcept {
    return a.world==b.world&&a.request==b.request&&a.callback==b.callback&&a.type==b.type&&
        a.context==b.context&&a.references==b.references&&a.registeredBegin==b.registeredBegin&&
        a.registeredEnd==b.registeredEnd&&a.registeredCapacity==b.registeredCapacity&&
        a.registeredCount==b.registeredCount&&a.registered==b.registered;
}
// Exactly one native refresh, only for a dirty, mismatched, verified live owner.
// No ownership/refcount correction, retries, or refresh merely because a frame
// elapsed. A missing terrain entity disables its consumer but does NOT prove
// that the old cache was rebuilt or cleared.
template<class Calls> LiveViewCacheResult RefreshLiveViewCache(Calls& calls)noexcept {
    LiveViewCache before{},again{},after{};
    if(!calls.Capture(before)||!ValidLiveViewCache(before))return LiveViewCacheResult::Rejected;
    if(LiveViewCacheMatches(before))return LiveViewCacheResult::Unchanged;
    if(!before.dirty)return !before.scene?LiveViewCacheResult::Disabled:LiveViewCacheResult::Rejected;
    if(!calls.Capture(again)||again!=before||!calls.Refresh(before)||
       !calls.Capture(after)||!ValidLiveViewCache(after)||!SameLiveViewCacheOwner(before,after)||after.dirty)
        return LiveViewCacheResult::Rejected;
    if(!after.scene)return LiveViewCacheResult::Disabled;
    if(!LiveViewCacheMatches(after))return LiveViewCacheResult::Rejected;
    return LiveViewCacheResult::Refreshed;
}
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
       g.requestReferences!=g.count||g.children>8||!OwnsPendingStereoChildren(g,g.children)||
       g.count!=1+OwnedStereoChildCount(g,g.children))return false;
    unsigned roots=0;
    for(unsigned i=0;i<g.count;++i){const auto& v=g.views[i];
        if(!v.address||v.request!=g.request||v.references!=OwnedStereoViewReferences(g,v.address,g.children)||!v.nativeTypeVerified)return false;
        for(unsigned j=0;j<i;++j)if(g.views[j].address==v.address)return false;
        if(v.address==g.eye){if(v.parent)return false;++roots;}
        else if(v.parent!=g.eye)return false;
    }
    return roots==1;
}
}
