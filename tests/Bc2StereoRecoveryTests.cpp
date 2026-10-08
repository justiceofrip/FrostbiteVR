#include "Bc2StereoRecovery.h"
#include "fvr/runtime/RenderOwnerRecovery.h"
#include <cstdlib>
#include <iostream>
#include <vector>
using namespace fvr::bc2;
void Check(bool v){if(!v)std::abort();}
RetiredStereoGraph Graph(){RetiredStereoGraph g;
    g.world=410945504;g.request=120624304;g.eye=446119296;g.currentWorld=1001202704;g.currentRequest=120623888;
    g.requestReferences=g.count=7;g.worldReferences=1;g.mainCount=1;g.mainEntry=g.eye;g.children=3;
    g.nativeOwnersVerified=g.requestRegistered=g.callbacksVerified=true;
    const std::uint32_t ids[]={446119296,445748160,445389440,445596080,456360816,455552512,1098821968};
    for(unsigned i=0;i<7;++i)g.views[i]={ids[i],g.request,i?g.eye:0,1,true};return g;
}
struct CacheFixture {
    // Actual crash060816: empty native registration, 14 cached20-byte records,
    // surviving callback refs2, scene entity table already destroyed.
    RetiredViewCache value{0x187e0d30,0x144f90c,2,0x3a12ab60,0x3bddb9e0,0x3bddbaf8,0x3bddbaf8};
    unsigned cacheReferences=28,shrinkCalls=0,captures=0,sceneQueries=0;
    bool detached=true,changeBefore=false,changeAfter=false,failShrink=false;
    bool Capture(RetiredViewCache& out)noexcept{
        ++captures;out=value;
        if(changeBefore&&captures==2)++out.scene;
        if(changeAfter&&captures==3)out.capacity+=20;
        return true;
    }
    bool Detached()noexcept{return detached;}
    bool ShrinkToZero(const RetiredViewCache& before)noexcept {
        Check(before==value);++shrinkCalls;if(failShrink)return false;
        // Model the verified native shrink's resource ownership, not ammo or
        // scene reconstruction: release two refs per entry, retain allocation.
        cacheReferences-=2*(value.end-value.begin)/20;value.end=value.begin;return true;
    }
};
void UnloadedSceneCacheRetirement(){
    CacheFixture c;Check(RetireDetachedViewCache(c));
    Check(c.cacheReferences==0&&c.shrinkCalls==1&&c.sceneQueries==0&&c.value.end==c.value.begin);
    Check(c.value.begin==0x3bddb9e0&&c.value.capacity==0x3bddbaf8&&c.value.references==2);
    Check(RetireDetachedViewCache(c)&&c.shrinkCalls==1); // no double native release
}
void CacheFailurePreservesNativeOwnership(){
    {CacheFixture c;c.changeBefore=true;Check(!RetireDetachedViewCache(c)&&c.shrinkCalls==0&&c.cacheReferences==28);}
    {CacheFixture c;c.detached=false;Check(!RetireDetachedViewCache(c)&&c.shrinkCalls==0&&c.cacheReferences==28);}
    {CacheFixture c;c.failShrink=true;Check(!RetireDetachedViewCache(c)&&c.cacheReferences==28);}
    {CacheFixture c;c.changeAfter=true;Check(!RetireDetachedViewCache(c)&&c.cacheReferences==0);}
    for(unsigned index=0;index<5;++index){CacheFixture c;
        if(index==0)c.value.end+=4;
        if(index==1)c.value.end=c.value.begin-20;
        if(index==2)c.value.capacity=c.value.begin+4097*20;
        if(index==3)c.value.references=0;
        if(index==4)c.value.begin=0;
        Check(!RetireDetachedViewCache(c)&&c.shrinkCalls==0);
    }
    {CacheFixture c;c.value.begin=c.value.end=c.value.capacity=0;c.cacheReferences=0;Check(RetireDetachedViewCache(c)&&c.shrinkCalls==0);}
}
void ActualRetirementOrdersCacheBeforeOwner(){
    struct Calls {
        CacheFixture cache;unsigned refs=7;bool released=false,ownerDestroyed=false;std::vector<unsigned> order;
        bool RetainOwner()noexcept{order.push_back(0);++refs;return true;}
        bool ReleaseRoot()noexcept{order.push_back(1);released=true;refs-=7;return true;}
        bool Detached()noexcept{return released&&refs==1;}
        bool RetireRegistry()noexcept{order.push_back(2);return Detached()&&RetireDetachedViewCache(cache);}
        bool ReleaseOwner()noexcept{order.push_back(3);Check(cache.cacheReferences==0&&cache.sceneQueries==0);--refs;ownerDestroyed=true;return true;}
    } c;
    auto step=fvr::runtime::RetirementStep::Untouched;
    Check(fvr::runtime::RetireOwnedRenderRoot(c,step));
    Check(step==fvr::runtime::RetirementStep::Complete&&c.ownerDestroyed&&c.refs==0&&c.order==std::vector<unsigned>({0,1,2,3}));
    Calls failed;failed.cache.changeAfter=true;step=fvr::runtime::RetirementStep::Untouched;
    Check(!fvr::runtime::RetireOwnedRenderRoot(failed,step)&&!failed.ownerDestroyed&&failed.refs==1);
    Check(step==fvr::runtime::RetirementStep::RootReleased);
}
LiveStereoRetirementGraph LiveGraph(){
    LiveStereoRetirementGraph g;g.world=457149440;g.request=117478160;g.main=1016379152;g.eye=456363952;
    g.requestReferences=15;g.count=14;g.mainChildren=g.eyeChildren=3;
    g.nativeOwnersVerified=g.requestRegistered=g.callbacksVerified=true;
    // Actual post-detach064547 order differs from the stored initialization
    // order but has identical surviving pointers, owners, parents and refs.
    const unsigned original[]={1016379152,436894992,1248889472,444922816,1014900192,1014058272,1244983888};
    for(unsigned i=0;i<7;++i){g.views[i*2]={original[i],g.request,i?g.main:0,1,true};
        g.views[i*2+1]={i?0x30000000u+i*0x10000u:g.eye,g.request,i?g.eye:0,1,true};}
    return g;
}
void ShutdownUsesCurrentOrderedGeneration(){
    auto g=LiveGraph();StereoSurvivors s;Check(CaptureLiveStereoSurvivors(g,s));
    const std::array<unsigned,7> current{1016379152,436894992,1248889472,444922816,1014900192,1014058272,1244983888};
    const std::array<unsigned,7> historical{1016379152,436894992,1014900192,1014058272,1244983888,1248889472,444922816};
    Check(s.count==7&&s.removed==7&&s.referencesAfter==8);
    for(unsigned i=0;i<7;++i)Check(s.ordered[i]==current[i]);
    Check(current!=historical); // Old exact initialization comparison rejects this native record.
    for(unsigned i=7;i<s.ordered.size();++i)Check(s.ordered[i]==0);
    // The actual retirement must preserve this exact survivor order. A later
    // permutation is not authorized by accepting a fresh earlier generation.
    auto later=g;std::swap(later.views[4],later.views[6]);StereoSurvivors changed;
    Check(CaptureLiveStereoSurvivors(later,changed)&&changed!=s);
}
void ShutdownRejectsUnownedOrSharedSubtrees(){
    StereoSurvivors sentinel;sentinel.count=1;sentinel.removed=2;sentinel.referencesAfter=3;sentinel.ordered[0]=0x12345678;
    for(unsigned n=0;n<14;++n)for(unsigned fault=0;fault<4;++fault){auto g=LiveGraph();auto s=sentinel;
        if(fault==0)++g.views[n].request;if(fault==1)g.views[n].references=2;
        if(fault==2)g.views[n].nativeTypeVerified=false;if(fault==3)g.views[n].address=0;
        Check(!CaptureLiveStereoSurvivors(g,s)&&s==sentinel);}
    for(unsigned n=0;n<14;++n){auto g=LiveGraph();g.views[n].parent=0x12345678;StereoSurvivors s;Check(!CaptureLiveStereoSurvivors(g,s));}
    for(unsigned fault=0;fault<10;++fault){auto g=LiveGraph();StereoSurvivors s;
        if(fault==0)g.views[2].address=g.views[0].address;if(fault==1)--g.count;
        if(fault==2)--g.requestReferences;if(fault==3)--g.mainChildren;if(fault==4)--g.eyeChildren;
        if(fault==5)g.callbacksVerified=false;if(fault==6)g.requestRegistered=false;
        if(fault==7)g.nativeOwnersVerified=false;if(fault==8)g.eye=g.main;if(fault==9)g.world=0;
        Check(!CaptureLiveStereoSurvivors(g,s));}
}
int main(){
    Check(CanRetireStereoGraph(Graph()));
    for(unsigned i=0;i<7;++i){auto g=Graph();g.views[i].request=g.currentRequest;Check(!CanRetireStereoGraph(g));
        g=Graph();g.views[i].references=2;Check(!CanRetireStereoGraph(g));
        g=Graph();g.views[i].nativeTypeVerified=false;Check(!CanRetireStereoGraph(g));}
    {auto g=Graph();g.views[2].address=g.views[1].address;Check(!CanRetireStereoGraph(g));}
    {auto g=Graph();g.views[3].parent=1000040960;Check(!CanRetireStereoGraph(g));} // reused original main
    {auto g=Graph();g.views[0].parent=g.eye;Check(!CanRetireStereoGraph(g));}
    {auto g=Graph();g.mainEntry=1000040960;Check(!CanRetireStereoGraph(g));}
    {auto g=Graph();g.count=65;Check(!CanRetireStereoGraph(g));}
    {auto g=Graph();g.requestReferences=6;Check(!CanRetireStereoGraph(g));}
    {auto g=Graph();g.children=2;Check(!CanRetireStereoGraph(g));}
    {auto g=Graph();g.world=g.currentWorld;Check(!CanRetireStereoGraph(g));}
    {auto g=Graph();g.callbacksVerified=false;Check(!CanRetireStereoGraph(g));}
    {auto g=Graph();g.requestRegistered=false;Check(!CanRetireStereoGraph(g));}
    {auto g=Graph();g.nativeOwnersVerified=false;Check(!CanRetireStereoGraph(g));}
    {auto g=Graph();g.worldReferences=0;Check(!CanRetireStereoGraph(g));}
    UnloadedSceneCacheRetirement();CacheFailurePreservesNativeOwnership();ActualRetirementOrdersCacheBeforeOwner();
    ShutdownUsesCurrentOrderedGeneration();ShutdownRejectsUnownedOrSharedSubtrees();
    std::cout<<"19 retired stereo graph/cache and current shutdown generation groups passed\n";
}
