#include "Test.h"
#include "fvr/interaction/ControllerInput.h"
#include <cmath>
#include <limits>
using namespace fvr;
int main(){
    interaction::InputFrame f{};f.generation=1;f.spaceGeneration=1;f.predictedNs=1000000000;f.focused=f.headValid=true;
    for(auto& h:f.hands){h.gripTracked=h.aimTracked=true;h.active=interaction::Components;}
    CHECK(interaction::ValidInput(f));auto bad=f;bad.hands[1].trigger=std::numeric_limits<float>::quiet_NaN();CHECK(!interaction::ValidInput(bad));
    bad=f;bad.focused=false;CHECK(!interaction::ValidInput(bad));bad=f;bad.hands[0].active=0;bad.hands[0].held=interaction::Primary;CHECK(!interaction::ValidInput(bad));
    interaction::ControllerActions actions;interaction::InputOwner owner{123,1,true,true};
    auto step=[&](){++f.generation;f.predictedNs+=11000000;return actions.Update(f,owner,f.predictedNs);};
    step();step();step(); // Context transition, owner transition, neutral arming.
    f.hands[1].trigger=.9f;f.hands[0].stickX=f.hands[0].stickY=1;
    auto out=step();CHECK(out.pressed==interaction::Fire&&out.owner==123);CHECK(std::abs(std::hypot(out.forward,out.strafe)-1)<.0001f);
    out=actions.Update(f,owner,f.predictedNs);CHECK(out.held==interaction::Fire&&!out.pressed);
    out=actions.Update(f,owner,f.predictedNs+100000001);CHECK(out.released==interaction::Fire&&!out.held);
    step();step();CHECK(!step().held); // Holding through recovery must not re-fire.
    f.hands[1].trigger=0;f.hands[0].stickX=f.hands[0].stickY=0;step();f.hands[1].trigger=1;CHECK(step().pressed==interaction::Fire);
    ++owner.generation;out=step();CHECK(out.released==interaction::Fire);CHECK(!step().held);CHECK(!step().held);
    f.hands[1].trigger=0;step();f.hands[1].trigger=1;CHECK(step().pressed==interaction::Fire);
    ++f.spaceGeneration;CHECK(step().released==interaction::Fire);CHECK(!step().held);
    f.hands[1].trigger=0;step();step();f.hands[1].trigger=1;CHECK(step().held==interaction::Fire);
    f.focused=false;f.hands={};CHECK(step().released==interaction::Fire);
    // Physical left turn maps stick forward to native left strafe. Head pitch
    // cannot invent a vertical movement axis; stick drift stays neutral.
    f.focused=f.headValid=true;f.hands={};for(auto& h:f.hands){h.gripTracked=h.aimTracked=true;h.active=interaction::Components;}
    step();step();step();f.hands[0].stickY=.1f;out=step();CHECK(!out.forward&&!out.strafe);
    f.hands[0].stickY=1;f.head.orientation={0,std::sqrt(.5f),0,std::sqrt(.5f)};
    out=step();CHECK(std::abs(out.forward)<.0001f&&out.strafe<-.999f);
    f.hands[0].stickY=0;f.head={};step();
    f.hands[0].held=interaction::Primary|interaction::Secondary;f.hands[1].held=interaction::Primary|interaction::Secondary;
    out=step();CHECK(out.held==(interaction::Use|interaction::Crouch|interaction::Jump|interaction::Reload));
    // A fully pulled off-hand trigger is pose input, not ADS/AlternateFire.
    // It cannot hold the action policy unarmed or interfere with the gun hand.
    f.hands[0].held=f.hands[1].held=0;f.hands[0].trigger=1;f.hands[1].trigger=0;
    out=step();CHECK(!out.held&&!out.pressed&&f.hands[0].trigger==1);
    out=actions.Update(f,owner,f.predictedNs);CHECK(!out.held&&!out.pressed);
    f.hands[1].trigger=1;out=step();CHECK(out.held==interaction::Fire&&out.pressed==interaction::Fire);
    f.hands[1].trigger=0;f.hands[0].held=interaction::Primary;
    out=step();CHECK(out.held==interaction::Use&&out.pressed==interaction::Use);
    f.hands[0].gripTracked=false;CHECK(step().released==interaction::Use);
    f.hands[0].gripTracked=true;f.hands[0].held=0;step();step();
    out=step();CHECK(!out.held&&!out.pressed);
    f.hands[0].trigger=0;step();
    // Left tracking loss releases only left actions while the held gun trigger
    // continues. Recovery with a held left button requires that hand's neutral.
    f.hands[0].held=f.hands[1].held=0;f.hands[1].trigger=0;step();
    f.hands[1].trigger=1;CHECK(step().pressed==interaction::Fire);
    f.hands[0].held=interaction::Primary;CHECK(step().pressed==interaction::Use);
    f.hands[0].gripTracked=false;out=step();
    CHECK(out.active&&out.held==interaction::Fire&&out.released==interaction::Use&&!out.pressed);
    f.hands[0].gripTracked=true;step();step();out=step();CHECK(out.held==interaction::Fire);
    f.hands[0].held=0;step();f.hands[0].held=interaction::Primary;CHECK(step().pressed==interaction::Use);
    f.hands[0].held=0;f.hands[0].stickY=1;out=step();CHECK(out.forward>.99f);
    // Right tracking loss releases fire while left movement continues. A held
    // trigger cannot re-fire until released after right-hand recovery.
    f.hands[1].aimTracked=false;out=step();CHECK(out.active&&out.forward>.99f&&out.released==interaction::Fire&&!out.held);
    f.hands[1].aimTracked=true;step();step();out=step();CHECK(!out.held&&out.forward>.99f);
    f.hands[1].trigger=0;step();f.hands[1].trigger=1;CHECK(step().pressed==interaction::Fire);
    f.hands[0].gripTracked=false;f.hands[1].aimTracked=false;out=step();CHECK(!out.active&&!out.held&&!out.forward);
    f.hands={};
    // One vertical flick per return to center, including duplicate packets,
    // held-stick equip transitions, diagonal turns, and right tracking recovery.
    f.hands={};for(auto& h:f.hands){h.gripTracked=h.aimTracked=true;h.active=interaction::Components;}
    step();step();step();f.hands[1].stickY=1;out=step();CHECK(out.pressed==interaction::NextWeapon&&!out.turnDegrees);
    CHECK(!actions.Update(f,owner,f.predictedNs).pressed);CHECK(!step().pressed);
    f.hands[1].stickY=.6f;CHECK(step().held==interaction::NextWeapon);
    ++owner.generation;step();step();CHECK(!step().pressed);
    f.hands[1].stickY=0;step();step();f.hands[1].stickY=-1;CHECK(step().pressed==interaction::PreviousWeapon);
    f.hands[1].aimTracked=false;CHECK(step().released==interaction::PreviousWeapon);
    f.hands[1].aimTracked=true;step();step();CHECK(!step().pressed);
    f.hands[1].stickY=0;step();step();f.hands[1].stickX=f.hands[1].stickY=1;out=step();CHECK(!(out.held&(interaction::NextWeapon|interaction::PreviousWeapon))&&out.turnDegrees);
    f.hands[1].stickX=f.hands[1].stickY=0;step();f.hands[1].stickY=1;CHECK(step().pressed==interaction::NextWeapon);
    // Reference yaw, pitch-independent gravity, native eye scale is separate.
    math::Pose p{};p.position={.2f,0,-.5f};p.orientation={std::sin(.2f),0,0,std::cos(.2f)};
    const auto ref=interaction::UprightReference(p);CHECK(ref&&ref->position.y==0&&ref->orientation.x==0&&ref->orientation.w==1);
    p.orientation={0,std::sin(.4f),0,std::cos(.4f)};const auto yaw=interaction::UprightReference(p);CHECK(yaw&&std::abs(yaw->orientation.y-p.orientation.y)<.0001f);
    p.orientation={std::sqrt(.5f),0,0,std::sqrt(.5f)};CHECK(!interaction::UprightReference(p));
    // Gesture is not a repeated button edge; duplicate samples and tracking gaps cannot complete it.
    interaction::RecenterGesture gesture;f.focused=f.headValid=true;f.hands={};
    for(auto& h:f.hands){h.gripTracked=true;h.active=interaction::StickClick;}
    auto g=[&](std::int64_t dt=11000000){++f.generation;f.predictedNs+=dt;return gesture.Update(f);};
    CHECK(!g());CHECK(!g());for(auto& h:f.hands)h.held=interaction::StickClick;
    CHECK(!g());CHECK(!gesture.Update(f));for(int i=0;i<9;++i)CHECK(!g(100000000));CHECK(g(100000000));CHECK(!g(100000000));
    for(auto& h:f.hands)h.held=0;CHECK(!g());for(auto& h:f.hands)h.held=interaction::StickClick;CHECK(!g());CHECK(!g(2000000000));
    for(int i=0;i<12;++i)CHECK(!g(100000000));
    return 0;
}
