#include "../tools/debug/SceneDrawPayloadPlan.h"
#include "Test.h"
#include <limits>
using namespace fvr::debug;
using namespace fvr::graphics;
int main(){
    PayloadSelection p;p.count=3;p.ordinal={1234,1238};
    for(auto& draw:p.expected){draw.kind=SceneWitnessDrawKind::IndexedIndirect;draw.argumentOffset=16;}
    p.ranges[0]={PayloadSource::Vertex,PayloadStage::Vertex,3,128,64};
    p.ranges[1]={PayloadSource::ShaderResource,PayloadStage::Vertex,7,64,128};
    p.ranges[2]={PayloadSource::Indirect,PayloadStage::Vertex,0,0,20};
    CHECK(PayloadPlanValid(p));CHECK(PayloadPairBytes==1024*1024);
    CHECK(PayloadRangeFits(1024,128,512,64,128));
    CHECK(PayloadRangeFits(1024,0,1024,1020,4));
    CHECK(!PayloadRangeFits(1024,0,1024,1024,4));
    CHECK(!PayloadRangeFits(1024,1025,0,0,4));
    CHECK(!PayloadRangeFits(1024,0,1025,0,4));
    CHECK(!PayloadRangeFits(1024,2,1022,0,4));
    CHECK(!PayloadRangeFits(1024,0,1024,0,0));
    CHECK(!PayloadRangeFits(1024,0,1024,0,3));
    CHECK(!PayloadRangeFits(1024,std::numeric_limits<std::uint64_t>::max(),4,0,4));
    CHECK(!PayloadRangeFits(1024,0,1024,std::numeric_limits<unsigned>::max(),4));
    // A small requested range does not authorize bytes outside the SRV's view.
    CHECK(!PayloadRangeFits(4096,128,64,60,8));
    for(unsigned n=0;n<11;++n){auto bad=p;
        switch(n){case 0:bad.count=0;break;case 1:bad.count=9;break;
        case 2:bad.ordinal[0]=100001;break;case 3:bad.ranges[0].slot=32;break;
        case 4:bad.ranges[1].slot=128;break;case 5:bad.ranges[0].offset=2;break;
        case 6:bad.ranges[0].bytes=PayloadRangeBytes+4;break;
        case 7:bad.ranges[0].stage=PayloadStage::Pixel;break;
        case 8:bad.ranges[2].offset=4;break;case 9:bad.ranges[2].bytes=16;break;
        case 10:bad.ranges[1]=bad.ranges[0];break;}
        CHECK(!PayloadPlanValid(bad));
    }
    auto indirect=p;for(auto& d:indirect.expected)d.kind=SceneWitnessDrawKind::Indirect;
    indirect.ranges[2].bytes=16;CHECK(PayloadPlanValid(indirect));
    indirect.expected[1].kind=SceneWitnessDrawKind::IndexedIndirect;CHECK(!PayloadPlanValid(indirect));
    auto shape=p.expected[0];shape.argumentBuffer=123;CHECK(PayloadDrawShape(shape,p.expected[0]));
    ++shape.argumentOffset;CHECK(!PayloadDrawShape(shape,p.expected[0]));
    for(unsigned n=0;n<7;++n){auto bad=p.expected[0];
        switch(n){case 0:bad.kind=SceneWitnessDrawKind::Indexed;break;case 1:++bad.count;break;
        case 2:++bad.instances;break;case 3:++bad.start;break;case 4:++bad.firstInstance;break;
        case 5:++bad.base;break;case 6:++bad.argumentOffset;break;}
        CHECK(!PayloadDrawShape(bad,p.expected[0]));
    }
    SceneWitnessKey k{1,2,3,4,5,6,7,8,9,10,0,1920,1080,11,12};CHECK(PayloadKeyValid(k));
    for(unsigned n=0;n<4;++n){auto bad=k;
        switch(n){case 0:bad.nativeOwner=0;break;case 1:bad.nativeRequest=0;break;
        case 2:bad.predictedNs=0;break;case 3:bad.rightView=bad.mainView;break;}
        CHECK(!PayloadKeyValid(bad));
    }
    return 0;
}
