#include "fvr/graphics/SceneDrawWitness.h"
#include "Test.h"
#include <limits>
using namespace fvr::graphics;
int main(){
    CHECK(SceneWitnessDimensions(2784,3024));CHECK(SceneWitnessDimensions(320,240));
    CHECK(!SceneWitnessDimensions(0,3024));CHECK(!SceneWitnessDimensions(16384,16384));
    CHECK(!SceneWitnessDimensions(std::numeric_limits<unsigned>::max(),2));
    CHECK(SceneWitnessMaxPixels*4*14+SceneWitnessBufferBudget+SceneWitnessMetadataBudget<512ull*1024*1024);
    const SceneWitnessKey k{1,2,3,4,5,6,7,8,9,10,2,320,240};CHECK(SceneWitnessValid(k));
    SceneWitnessPairGate gate;CHECK(!gate.Begin(1,k,1,k.rightView));CHECK(!gate.Begin(2,k,0,k.mainView));
    CHECK(!gate.Begin(1,k,0,k.rightView));CHECK(!gate.Active());
    CHECK(gate.Begin(1,k,0,k.mainView));CHECK(gate.Active());CHECK(gate.End(k,0));CHECK(!gate.Complete());
    CHECK(gate.Begin(1,k,1,k.rightView));CHECK(gate.End(k,1));CHECK(gate.Complete());
    CHECK(!gate.Begin(1,k,0,k.mainView));CHECK(gate.Complete());
    auto next=k;++next.nativeFrame;++next.request;
    CHECK(gate.Begin(2,next,0,next.mainView));CHECK(gate.End(next,0));
    auto wrong=next;++wrong.tracking;CHECK(!gate.Begin(2,wrong,1,wrong.rightView));CHECK(gate.Failed());CHECK(!gate.Complete());
    CHECK(!gate.Begin(2,next,1,next.rightView));CHECK(gate.Begin(3,next,0,next.mainView));CHECK(gate.End(next,0));
    CHECK(gate.Begin(3,next,1,next.rightView));CHECK(gate.End(next,1));CHECK(gate.Complete());CHECK(!gate.Begin(4,next,0,next.mainView));
    // Every identity component is part of pair association. No stale source,
    // world or tracking packet can stand in for the missing second eye.
    for(unsigned field=0;field<13;++field){auto changed=k;
        switch(field){case 0:++changed.world;break;case 1:++changed.request;break;case 2:++changed.nativeFrame;break;
        case 3:++changed.deviceEpoch;break;case 4:++changed.channel;break;case 5:++changed.tracking;break;
        case 6:++changed.space;break;case 7:++changed.predictedNs;break;case 8:++changed.mainView;break;
        case 9:++changed.rightView;break;case 10:changed.sourceSlot=0;break;case 11:++changed.width;break;case 12:++changed.height;break;}
        SceneWitnessPairGate g;CHECK(g.Begin(1,k,0,k.mainView));CHECK(g.End(k,0));CHECK(!g.Begin(1,changed,1,changed.rightView));CHECK(g.Failed());
    }
    SceneWitnessPairGate nested;CHECK(nested.Begin(1,k,0,k.mainView));CHECK(!nested.Begin(1,k,0,k.mainView));CHECK(nested.Failed());CHECK(!nested.End(k,0));
    auto invalid=k;invalid.world=0;CHECK(!SceneWitnessValid(invalid));invalid=k;invalid.sourceSlot=3;CHECK(!SceneWitnessValid(invalid));
    return 0;
}
