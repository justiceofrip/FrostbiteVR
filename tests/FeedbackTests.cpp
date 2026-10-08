#include "fvr/ipc/FeedbackProtocol.h"
#include "Test.h"
#include <iostream>
using namespace fvr::interaction;using namespace fvr::ipc;
namespace {
constexpr std::int64_t Now=1000000000;
InputFrame Input(){InputFrame i;i.generation=20;i.spaceGeneration=7;i.predictedNs=3000000000;
    i.focused=i.headValid=true;i.hands[0].gripTracked=i.hands[1].gripTracked=true;return i;}
FeedbackEvent Event(){return {1,19,7,Now-1000000,Now+99000000,FeedbackKind::ReloadCapture,0};}
int DistinctCaptureAndReceiptWithNoReplay(){HapticFeedbackPolicy p;auto i=Input();auto e=Event();
    auto r=p.Update(i,Now,&e);CHECK(r.pulse&&r.pulse->hand==0&&r.pulse->durationNs==35000000&&Near(r.pulse->amplitude,.55f));
    CHECK(!p.Update(i,Now+1,&e).pulse);e.id++;e.kind=FeedbackKind::ReloadApplied;
    r=p.Update(i,Now+2,&e);CHECK(r.pulse&&r.pulse->durationNs==75000000&&Near(r.pulse->amplitude,.9f));
    CHECK(!p.Update(i,Now+3,&e).pulse);return 0;}
int FocusAndMenuBarrier(){HapticFeedbackPolicy p;auto i=Input();p.Update(i,Now);i.focused=false;++i.generation;
    CHECK(p.Update(i,Now+1).stop);auto e=Event();e.inputSequence=i.generation;
    CHECK(!p.Update(i,Now+2,&e).pulse);i.focused=true;++i.generation;e.id++;
    CHECK(!p.Update(i,Now+3,&e).pulse);e.id++;e.inputSequence=i.generation;
    CHECK(p.Update(i,Now+4,&e).pulse);return 0;}
int SessionEventSuspendBlocksOldInput(){HapticFeedbackPolicy p;auto i=Input();p.Update(i,Now);p.Suspend();
    auto e=Event();e.inputSequence=i.generation;CHECK(!p.Update(i,Now+1,&e).pulse);
    ++i.generation;++e.id;e.inputSequence=i.generation;CHECK(p.Update(i,Now+2,&e).pulse);return 0;}
int SpaceTrackingClockAndExpiry(){for(unsigned bad=0;bad<8;++bad){HapticFeedbackPolicy p;auto i=Input();auto e=Event();p.Update(i,Now);
    auto at=Now+1;
    if(bad==0)++e.space;if(bad==1)i.hands[0].gripTracked=false;if(bad==2)i.headValid=false;
    if(bad==3)at=e.deadlineNs;if(bad==4)e.inputSequence=i.generation+1;if(bad==5)e.observedNs=at+1;
    if(bad==6)at=Now-1;if(bad==7){++i.spaceGeneration;e.inputSequence=i.generation;}
    CHECK(!p.Update(i,at,&e).pulse);
    CHECK(!p.Update(Input(),Now+2,&e).pulse); // Rejected event cannot replay after recovery.
    }return 0;}
int WireLayoutAndMalformedInputs(){auto e=Event();FeedbackPacket wire;CHECK(EncodeFeedback(e,Now,wire));FeedbackEvent out;
    CHECK(DecodeFeedback(wire,Now,out)&&out.id==e.id&&out.observedNs==e.observedNs&&out.deadlineNs==e.deadlineNs);
    for(unsigned bad=0;bad<8;++bad){auto p=wire;if(bad==0)p.magic=0;if(bad==1)++p.version;if(bad==2)--p.bytes;if(bad==3)p.reserved=1;
        if(bad==4)p.kind=999;if(bad==5)p.hand=2;if(bad==6)p.event=0;if(bad==7)p.deadlineNs=p.observedNs+100000001;
        CHECK(!DecodeFeedback(p,Now,out)&&out.id==0);}
    CHECK(!DecodeFeedback(wire,e.deadlineNs,out));return 0;}
int CaptureRateLimitDoesNotSuppressReceipt(){HapticFeedbackPolicy p;auto i=Input();auto e=Event();CHECK(p.Update(i,Now,&e).pulse);
    ++e.id;CHECK(!p.Update(i,Now+1000000,&e).pulse);++e.id;e.kind=FeedbackKind::ReloadApplied;
    CHECK(p.Update(i,Now+1000001,&e).pulse);return 0;}
}
int main(){if(DistinctCaptureAndReceiptWithNoReplay()||FocusAndMenuBarrier()||SessionEventSuspendBlocksOldInput()||SpaceTrackingClockAndExpiry()||WireLayoutAndMalformedInputs()||CaptureRateLimitDoesNotSuppressReceipt())return 1;
    std::cout<<"Feedback: six semantic/wire/safety groups passed\n";return 0;}
