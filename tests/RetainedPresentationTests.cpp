#include "Test.h"
#include "fvr/runtime/RetainedPresentation.h"
using namespace fvr;
int main(){
    runtime::PresentationRequirements req{100,120,29,19,4};runtime::TrackingFrame source{};
    source.generation=5;source.spaceGeneration=7;source.predictedNs=1000000000;source.focused=source.headValid=true;
    source.eyes[0].position.x=-.032f;source.eyes[1].position.x=.032f;
    source.fov={math::FovTangents{-1.2f,.8f,.9f,-1.1f},math::FovTangents{-.8f,1.2f,.9f,-1.1f}};
    graphics::TextureDescriptor d{};d.width=req.width;d.height=req.height;d.format=req.format;d.adapterLow=req.adapterLow;d.adapterHigh=req.adapterHigh;d.resourceEpoch=6;d.session[0]=1;
    graphics::PairTicket t{};t.resourceEpoch=d.resourceEpoch;t.session=d.session;t.sequence=1;t.frameId=4;t.spaceGeneration=source.spaceGeneration;t.trackingGeneration=source.generation;t.predictedNs=source.predictedNs;
    runtime::RetainedPresentation retained;CHECK(!retained.Select(req,source));CHECK(retained.Commit(req,source,d,t));
    auto current=source;++current.generation;current.predictedNs+=11000000;current.eyes[0].position.x+=.4f;current.fov[0].left=-.7f;
    const auto* held=retained.Select(req,current);CHECK(held);CHECK(held->generation==source.generation);
    CHECK(held->eyes[0].position.x==source.eyes[0].position.x&&held->fov[0].left==source.fov[0].left);
    // Repeated misses keep both original eyes, never the current head pose.
    for(int frame=2;frame<=22;++frame){current.predictedNs=source.predictedNs+frame*11000000;CHECK(retained.Select(req,current));}
    current.predictedNs=source.predictedNs+runtime::RetainedPresentation::MaxAgeNs+1;CHECK(!retained.Select(req,current));
    current=source;CHECK(!retained.Select(req,current)); // Expiry cannot resurrect.
    for(unsigned transition=0;transition<8;++transition){
        CHECK(retained.Commit(req,source,d,t));current=source;auto changed=req;bool shouldRender=true;
        switch(transition){case 0:current.focused=false;break;case 1:current.headValid=false;break;
          case 2:++current.spaceGeneration;break;case 3:--current.predictedNs;break;case 4:++changed.width;break;
          case 5:++changed.adapterLow;break;case 6:current.eyes[1].orientation.w=NAN;break;case 7:shouldRender=false;break;}
        CHECK(!retained.Select(changed,current,shouldRender));CHECK(!retained.Select(req,source));
    }
    CHECK(retained.Commit(req,source,d,t));auto bad=t;++bad.trackingGeneration;
    CHECK(!retained.Commit(req,source,d,bad));CHECK(retained.Select(req,source));
    retained.Reset();CHECK(!retained.Select(req,source));return 0;
}
