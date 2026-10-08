#include "fvr/interaction/ControllerInput.h"
#include "Test.h"
#include <cmath>
#include <iostream>
#include <limits>
using namespace fvr;using namespace fvr::interaction;
namespace {
struct Fixture {
    InputFrame f{};RecenterGesture gesture;
    Fixture(){f.generation=f.spaceGeneration=1;f.predictedNs=1000000000;f.focused=f.headValid=true;
        for(auto& h:f.hands)h.active=StickClick;}
    bool Next(std::int64_t elapsed=100000000){++f.generation;f.predictedNs+=elapsed;return gesture.Update(f);}
    void Neutral(){for(auto& h:f.hands)h.held=0;Next();Next();}
    void Hold(){for(auto& h:f.hands)h.held=StickClick;}
};
int ButtonsWorkWhenControllerPosesAreOccluded(){
    Fixture p;p.Neutral();p.Hold();CHECK(!p.Next());
    for(unsigned n=0;n<9;++n)CHECK(!p.Next());CHECK(p.Next());
    CHECK(!p.Next()&&!p.gesture.Update(p.f));
    const auto& e=p.gesture.Evidence();CHECK(e.completed==1&&e.starts==1&&e.maxHoldNs==1000000000);
    CHECK(e.bothHeldWithoutHandPose==e.bothHeld&&e.bothHeld>0);return 0;
}
int MissingBindingDoesNotArmFromFalseNeutral(){
    Fixture p;p.f.hands[1].active=0;p.Next();p.Next();
    p.f.hands[1].active=StickClick;p.Hold();for(unsigned n=0;n<15;++n)CHECK(!p.Next());
    p.Neutral();p.Hold();CHECK(!p.Next());for(unsigned n=0;n<9;++n)CHECK(!p.Next());CHECK(p.Next());return 0;
}
int InputLossRequiresAReleaseBeforeRetry(){
    for(unsigned kind=0;kind<4;++kind){Fixture p;p.Neutral();p.Hold();CHECK(!p.Next());
        for(unsigned n=0;n<5;++n)CHECK(!p.Next());
        if(kind==0){p.f.headValid=false;CHECK(!p.Next());p.f.headValid=true;}
        if(kind==1){p.f.focused=false;p.f.hands={};CHECK(!p.Next());p.f.focused=true;for(auto& h:p.f.hands)h.active=StickClick;p.Hold();}
        if(kind==2){++p.f.spaceGeneration;CHECK(!p.Next());}
        if(kind==3)CHECK(!p.Next(200000000));
        for(unsigned n=0;n<15;++n)CHECK(!p.Next());
        p.Neutral();p.Hold();CHECK(!p.Next());for(unsigned n=0;n<9;++n)CHECK(!p.Next());CHECK(p.Next());
        CHECK(p.gesture.Evidence().completed==1&&p.gesture.Evidence().interruptions>=1);
    }return 0;
}
int SingleClickAndDuplicatePacketsCannotComplete(){
    Fixture p;p.Neutral();p.f.hands[0].held=StickClick;
    for(unsigned n=0;n<15;++n)CHECK(!p.Next());p.Hold();CHECK(!p.Next());
    for(unsigned n=0;n<100;++n)CHECK(!p.gesture.Update(p.f));
    for(unsigned n=0;n<9;++n)CHECK(!p.Next());CHECK(p.Next());return 0;
}
int LookDownRecenterKeepsYawButMovesOrigin(){
    math::Pose old{},head{};old.orientation={0,std::sin(.4f),0,std::cos(.4f)};
    old.position={7,8,9};head.position={1,.7f,3};head.orientation={std::sqrt(.5f),0,0,std::sqrt(.5f)};
    CHECK(!UprightReference(head));const auto result=ExplicitRecenterReference(head,old);
    CHECK(result&&result->position.x==1&&result->position.y==.7f&&result->position.z==3);
    CHECK(std::abs(result->orientation.y-old.orientation.y)<.0001f&&std::abs(result->orientation.w-old.orientation.w)<.0001f);
    head.orientation={0,std::sin(.2f),0,std::cos(.2f)};const auto facing=ExplicitRecenterReference(head,old);
    CHECK(facing&&std::abs(facing->orientation.y-head.orientation.y)<.0001f);
    head.position.x=std::numeric_limits<float>::quiet_NaN();CHECK(!ExplicitRecenterReference(head,old));return 0;
}
}
int main(){for(auto test:{ButtonsWorkWhenControllerPosesAreOccluded,MissingBindingDoesNotArmFromFalseNeutral,
    InputLossRequiresAReleaseBeforeRetry,SingleClickAndDuplicatePacketsCannotComplete,LookDownRecenterKeepsYawButMovesOrigin})if(test())return 1;
    std::cout<<"Recenter: 5 button/focus/pose/space/reference groups passed\n";}
