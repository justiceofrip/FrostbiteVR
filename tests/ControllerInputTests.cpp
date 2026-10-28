#include "Test.h"
#include "fvr/interaction/ControllerInput.h"
#include <cmath>
#include <limits>
using namespace fvr;
namespace {
using namespace interaction;
struct CrouchFixture {
    InputFrame f{};InputOwner owner{123,1,true,true,9};ControllerActions actions;RecenterGesture recenter;
    bool recentered=false;
    CrouchFixture(){
        f.generation=f.spaceGeneration=1;f.predictedNs=1000000000;f.focused=f.headValid=true;
        for(auto& h:f.hands){h.gripTracked=h.aimTracked=true;h.active=Components;}
        f.menu={InputMenuOwner::Gameplay,1,1,f.predictedNs+100000000};
    }
    ActionOutput Step(std::int64_t dt=10000000,bool renewMenu=true){
        ++f.generation;f.predictedNs+=dt;
        if(renewMenu&&f.menu.owner!=InputMenuOwner::Unknown){++f.menu.sequence;f.menu.deadlineNs=f.predictedNs+100000000;}
        recentered=recenter.Update(f);
        return actions.Update(f,owner,f.predictedNs);
    }
    void Arm(){for(unsigned n=0;n<6;++n)Step();}
    ActionOutput Click(){
        f.hands[0].held=Secondary;Step();f.hands[0].held=0;return Step();
    }
};
int CrouchClickWhileMoving(){
    CrouchFixture p;p.Arm();p.f.hands[0].stickY=1;
    p.f.hands[0].held=Secondary;auto out=p.Step();
    CHECK(out.forward>.99f&&!(out.held&(Crouch|Sprint))&&!out.pressed);
    for(unsigned n=0;n<20;++n){
        out=p.Step();CHECK(out.forward>.99f&&!(out.held&(Crouch|Sprint)));
        out=p.actions.Update(p.f,p.owner,p.f.predictedNs);CHECK(!out.pressed&&!out.released);
    }
    p.f.hands[0].held=0;out=p.Step();CHECK(out.held==Crouch&&out.pressed==Crouch&&out.forward>.99f);
    out=p.actions.Update(p.f,p.owner,p.f.predictedNs);CHECK(out.held==Crouch&&!out.pressed&&!out.released);
    for(unsigned n=0;n<20;++n){out=p.Step();CHECK(out.held==Crouch&&!out.pressed&&out.forward>.99f);}
    p.f.hands[0].held=Secondary;out=p.Step();CHECK(out.held==Crouch&&!out.pressed);
    p.f.hands[0].held=0;out=p.Step();CHECK(!out.held&&out.released==Crouch&&out.forward>.99f);
    p.f.hands[0].held=StickClick;out=p.Step();CHECK(out.held==Sprint&&out.pressed==Sprint);
    p.f.hands[0].held=0;out=p.Step();CHECK(!out.held&&out.released==Sprint);return 0;
}
int CrouchIndependentMenuClock(){
    // Recorded approach43202 publications17..23: the synthetic XR clock is
    // about10seconds, while the actual QPC menu clock is about375849seconds.
    struct Row {std::uint64_t sequence,menuSequence;std::int64_t poseNs,menuNowNs,deadlineNs;bool clicked;};
    const Row rows[]{
        {17,67,10187000000,375848995002200,375849086636900,false},
        {18,72,10198000000,375849026028500,375849119987000,false},
        {19,75,10209000000,375849041260700,375849137535700,true},
        {20,77,10220000000,375849056432800,375849150221900,true},
        {21,80,10231000000,375849087997700,375849174107200,true},
        {22,83,10242000000,375849103336400,375849201202200,true},
        {23,84,10253000000,375849118585300,375849206017600,false}};
    CrouchFixture p;p.f.predictedNs=10011000000;p.f.spaceGeneration=8589934593;p.f.menu.epoch=2;p.Arm();
    auto wrongClock=p.actions;
    for(const auto& row:rows){
        p.f.generation=row.sequence;p.f.predictedNs=row.poseNs;
        p.f.menu={InputMenuOwner::Gameplay,row.menuSequence,2,row.deadlineNs};
        p.f.hands[0].held=row.clicked?Secondary:0u;p.f.hands[0].stickY=row.sequence==23?.5f:0;
        CHECK(GameplayOwnsInput(p.f.menu,row.menuNowNs)&&!GameplayOwnsInput(p.f.menu,row.poseNs));
        const auto out=p.actions.Update(p.f,p.owner,row.poseNs,row.menuNowNs);
        const auto wrong=wrongClock.Update(p.f,p.owner,row.poseNs,row.poseNs);
        CHECK(!(wrong.held&Crouch)&&bool(out.held&Crouch)==(row.sequence==23));
        if(row.sequence==23)CHECK(out.pressed==Crouch&&out.forward>0&&wrong.forward==out.forward);
    }
    // Expiry is judged only on the menu clock; pose/action arming is unchanged.
    ++p.f.generation;p.f.predictedNs+=11000000;
    const auto expired=p.actions.Update(p.f,p.owner,p.f.predictedNs,p.f.menu.deadlineNs+1);
    CHECK(expired.released==Crouch&&!(expired.held&Crouch)&&expired.forward>0);
    ++p.f.generation;p.f.predictedNs+=11000000;++p.f.menu.sequence;
    p.f.menu.deadlineNs+=100000000;
    const auto restored=p.actions.Update(p.f,p.owner,p.f.predictedNs,p.f.menu.deadlineNs-50000000);
    CHECK(restored.held==Crouch&&restored.forward>0);
    return 0;
}
int CrouchRecenterOwnsBothClicks(){
    for(unsigned order=0;order<3;++order)for(unsigned release=0;release<2;++release){
        CrouchFixture p;p.Arm();unsigned completions=0;
        if(order<2){p.f.hands[order].held=StickClick;const auto first=p.Step();CHECK(!(first.held&Crouch)&&bool(first.held&Sprint)==(order==0));}
        p.f.hands[0].held=p.f.hands[1].held=StickClick;
        for(unsigned n=0;n<12;++n){
            const auto out=p.Step(100000000);CHECK(!(out.held&(Crouch|Sprint)));
            if(p.recentered)++completions;
        }
        CHECK(completions==1);
        p.f.hands[release].held=0;CHECK(!(p.Step().held&(Crouch|Sprint)));
        p.f.hands[1-release].held=0;CHECK(!(p.Step().held&(Crouch|Sprint)));
        CHECK(p.Click().pressed==Crouch);
    }
    // Even a short, uncompleted recenter chord cancels a staggered left click.
    CrouchFixture p;p.Arm();p.f.hands[0].held=Secondary;p.Step();
    p.f.hands[1].held=StickClick;p.Step();p.f.hands[1].held=0;p.Step();
    p.f.hands[0].held=0;CHECK(!(p.Step().held&(Crouch|Sprint)));return 0;
}
int CrouchMenuChordDoesNotToggle(){
    for(unsigned posture=0;posture<2;++posture)for(unsigned order=0;order<2;++order)for(unsigned release=0;release<2;++release){
        CrouchFixture p;p.Arm();if(posture)CHECK(p.Click().pressed==Crouch);
        p.f.hands[0].held=order?Primary:Secondary;p.Step();
        p.f.hands[0].held=Primary|Secondary;p.Step();
        p.f.hands[0].held=release?Primary:Secondary;p.Step();
        p.f.hands[0].held=0;const auto out=p.Step();
        CHECK(bool(out.held&Crouch)==bool(posture)&&!(out.pressed&Crouch)&&!(out.released&Crouch));
        CHECK(posture?p.Click().released==Crouch:p.Click().pressed==Crouch);
    }
    return 0;
}
int CrouchLossSuspendsWithoutToggling(){
    for(unsigned fault=0;fault<14;++fault){
        CrouchFixture p;p.Arm();CHECK(p.Click().pressed==Crouch);
        p.f.hands[0].held=Secondary;p.Step(); // Interrupted attempt to stand.
        auto saved=p.f;const auto owner=p.owner;ActionOutput out;
        switch(fault){
        case 0:p.f.focused=false;p.f.hands={};break;
        case 1:p.f.headValid=false;break;
        case 2:p.f.hands[0].gripTracked=false;break;
        case 3:p.f.hands[0].active&=~Secondary;p.f.hands[0].held=0;break;
        case 4:p.f.hands[1].active&=~StickClick;break;
        case 5:++p.owner.owner;break;
        case 6:++p.owner.generation;break;
        case 7:++p.f.spaceGeneration;break;
        case 8:p.owner.playing=false;break;
        case 9:p.owner.bindingsVerified=false;break;
        case 10:p.f.menu.owner=InputMenuOwner::Pending;break;
        case 11:p.f.menu.owner=InputMenuOwner::Menu;break;
        case 12:p.f.menu={};break;
        case 13:++p.owner.actorGeneration;break;
        }
        out=p.Step();CHECK(!(out.held&Crouch)&&bool(out.released&Crouch));
        const auto sequence=p.f.generation;const auto time=p.f.predictedNs;
        p.f=saved;p.f.generation=sequence;p.f.predictedNs=time;p.owner=owner;
        const bool replaced=fault==5||fault==13;
        for(unsigned n=0;n<8;++n)out=p.Step();
        CHECK(bool(out.held&Crouch)!=replaced);
        p.f.hands[0].held=0;p.Arm();CHECK(bool(p.Step().held&Crouch)!=replaced);
        out=p.Click();CHECK(replaced?out.pressed==Crouch:out.released==Crouch);
    }
    return 0;
}
int CrouchPersistsAcrossRecenterAndPause(){
    CrouchFixture p;p.Arm();CHECK(p.Click().pressed==Crouch);
    p.f.hands[0].held=p.f.hands[1].held=StickClick;
    unsigned completions=0;
    for(unsigned n=0;n<12;++n){
        const auto out=p.Step(100000000);CHECK(out.held==Crouch&&!out.pressed&&!out.released);
        if(p.recentered)++completions;
    }
    CHECK(completions==1);
    // The runtime publishes the new reference space after completing the chord.
    ++p.f.spaceGeneration;CHECK(p.Step().released==Crouch);
    p.f.hands[1].held=0;p.Step();p.f.hands[0].held=0;p.Arm();
    auto out=p.Step();CHECK(out.held==Crouch&&!out.pressed&&!out.released);
    // A partial stand click cannot finish across the pause/resume boundary.
    p.f.hands[0].held=Secondary;p.Step();
    p.f.menu.owner=InputMenuOwner::Menu;++p.f.menu.epoch;
    CHECK(p.Step().released==Crouch);
    for(unsigned n=0;n<5;++n)CHECK(!(p.Step().held&Crouch));
    p.f.menu.owner=InputMenuOwner::Gameplay;++p.f.menu.epoch;++p.owner.generation;
    // Input rearming is independent of stable actor lifetime. A held stick
    // click cannot become a fresh click while the posture itself is restored.
    p.Arm();CHECK(p.Step().held==Crouch);
    p.f.hands[0].held=0;p.Arm();out=p.Step();
    CHECK(out.held==Crouch&&!out.pressed&&!out.released);
    CHECK(p.Click().released==Crouch);
    return 0;
}
int CrouchFreshnessAndSourceTransitions(){
    for(unsigned fault=0;fault<6;++fault){
        CrouchFixture p;p.Arm();CHECK(p.Click().pressed==Crouch);
        ActionOutput out;
        if(fault==0)out=p.actions.Update(p.f,p.owner,p.f.predictedNs+100000001);
        if(fault==1)out=p.Step(160000000);
        if(fault==2){++p.f.menu.epoch;out=p.Step();}
        if(fault==3){p.f.menu.deadlineNs=p.f.predictedNs;out=p.Step(10000000,false);}
        if(fault==4){p.f.generation-=2;p.f.predictedNs-=30000000;out=p.actions.Update(p.f,p.owner,p.f.predictedNs);}
        if(fault==5){p.f.hands[0].held=Secondary;out=p.Step(0);}
        CHECK(!(out.held&Crouch)&&bool(out.released&Crouch));
        p.f.hands[0].held=Secondary;for(unsigned n=0;n<8;++n)out=p.Step();
        CHECK(out.held&Crouch);
        p.f.hands[0].held=0;p.Arm();CHECK(p.Click().released==Crouch);
    }
    // Connecting with the button already down cannot count as a deliberate click.
    CrouchFixture p;p.f.hands[0].held=Secondary;p.Arm();
    p.f.hands[0].held=0;CHECK(!(p.Step().held&Crouch));p.Arm();CHECK(p.Click().pressed==Crouch);
    // Right-hand pose occlusion does not disable its available recenter button.
    p.f.hands[1].aimTracked=p.f.hands[1].gripTracked=false;
    p.f.hands[0].held=Secondary;p.Step();p.f.hands[1].held=StickClick;p.Step();
    p.f.hands[0].held=p.f.hands[1].held=0;CHECK(p.Step().held&Crouch);
    // A contradictory repeated packet cannot hide a recenter overlap and later
    // finish the earlier left-only click.
    CrouchFixture repeated;repeated.Arm();repeated.f.hands[0].held=Secondary;repeated.Step();
    repeated.f.hands[1].held=StickClick;
    CHECK(!(repeated.actions.Update(repeated.f,repeated.owner,repeated.f.predictedNs).held&Crouch));
    repeated.f.hands[0].held=repeated.f.hands[1].held=0;CHECK(!(repeated.Step().held&Crouch));
    CHECK(repeated.Click().pressed==Crouch);return 0;
}
}
int main(){
    CHECK(CrouchClickWhileMoving()==0);CHECK(CrouchIndependentMenuClock()==0);CHECK(CrouchRecenterOwnsBothClicks()==0);
    CHECK(CrouchLossSuspendsWithoutToggling()==0);CHECK(CrouchFreshnessAndSourceTransitions()==0);
    CHECK(CrouchPersistsAcrossRecenterAndPause()==0);
    CHECK(CrouchMenuChordDoesNotToggle()==0);
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
    out=step();CHECK(out.held==(interaction::Use|interaction::Jump|interaction::Reload));
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
    // Holsters own selection. Vertical sticks never select, including duplicate
    // packets, held-stick equip/recenter transitions and tracking recovery.
    f.hands={};for(auto& h:f.hands){h.gripTracked=h.aimTracked=true;h.active=interaction::Components;}
    const auto noSelection=[](const interaction::ActionOutput& value){
        return !((value.held|value.pressed|value.released)&(interaction::NextWeapon|interaction::PreviousWeapon));
    };
    step();step();step();
    for(const float vertical:{1.f,.6f,0.f,-1.f,0.f,1.f}){
        f.hands[1].stickY=vertical;out=step();CHECK(noSelection(out));CHECK(!out.turnDegrees);
        CHECK(noSelection(actions.Update(f,owner,f.predictedNs)));
    }
    ++owner.generation;for(int i=0;i<4;++i)CHECK(noSelection(step()));
    ++f.spaceGeneration;for(int i=0;i<4;++i)CHECK(noSelection(step()));
    f.hands[1].aimTracked=false;CHECK(noSelection(step()));
    f.hands[1].aimTracked=true;for(int i=0;i<4;++i)CHECK(noSelection(step()));
    // Vertical movement neither blocks firing nor consumes horizontal snap turn.
    f.hands[1].trigger=1;out=step();CHECK(noSelection(out));CHECK(out.pressed==interaction::Fire);
    f.hands[1].trigger=0;step();
    f.hands[1].stickX=1;out=step();CHECK(noSelection(out));CHECK(out.turnDegrees);
    out=step();CHECK(noSelection(out));CHECK(!out.turnDegrees);
    f.hands[1].stickX=0;step();f.hands[1].stickX=-1;out=step();CHECK(noSelection(out));CHECK(out.turnDegrees<0);
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
