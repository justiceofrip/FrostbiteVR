#include "Test.h"
#include "Bc2KnifeDrawReturnGate.h"
#include "Bc2OriginalFiringObserver.h"
#include <cstdio>
using namespace fvr::interaction;using namespace fvr::bc2;
KnifeCommand Command(std::uint64_t serial=1){KnifeCommand c;c.serial=serial;c.knife={0x52000,3};
    c.source={{1,2,3,4},8,1000000000,1100000000,1000000000,true,{true,true}};
    c.hand=InteractionHand::Right;return c;}
int main(){
    using Gate=private_knife::DrawReturnGate;
    {Gate gate;auto c=Command();CHECK(gate.Stage(c,c.source.nowNs));
        CHECK(!gate.Choose(0,c.source.nowNs,false));CHECK(!gate.Choose(2,c.source.nowNs,true));
        CHECK(gate.Choose(0,c.source.nowNs,true));CHECK(!gate.Choose(0,c.source.nowNs,true));
        CHECK(gate.ResolveBeforeSource(9,1010000000,1010000000)==KnifeCommit::Accepted);
        // Completion of accepted native sibling is not a new held input edge.
        CHECK(gate.Choose(1,1500000000,false));CHECK(!gate.Choose(1,1500000000,false));
        CHECK(gate.Complete(c.serial));CHECK(!gate.Stage(c,1010000000));
        c=Command(2);c.kind=KnifeCommandKind::Return;CHECK(gate.Stage(c,1010000000));}
    {Gate gate;auto c=Command();CHECK(gate.Stage(c,c.source.nowNs));
        CHECK(gate.ResolveBeforeSource(9,1010000000,1010000000)==KnifeCommit::RejectedNoEffect);
        CHECK(!gate.Choose(0,1010000000,true));CHECK(!gate.Complete(c.serial));
        c=Command(2);CHECK(gate.Stage(c,c.source.nowNs));CHECK(gate.Choose(1,c.source.nowNs,true));}
    {Gate gate;const auto c=Command();CHECK(gate.Stage(c,c.source.nowNs));
        CHECK(!gate.Choose(0,c.source.deadlineNs,true));
        CHECK(gate.ResolveBeforeSource(c.source.sequence,c.source.observedNs,c.source.deadlineNs)==KnifeCommit::RejectedNoEffect);}
    {Gate gate;const auto c=Command();CHECK(gate.Stage(c,c.source.nowNs));
        CHECK(!gate.ResolveBeforeSource(c.source.sequence,c.source.observedNs,c.source.nowNs));
        CHECK(gate.Choose(0,c.source.nowNs,true));gate.LifetimeEnded();
        CHECK(!gate.Choose(1,c.source.nowNs,true));CHECK(!gate.ResolveBeforeSource(9,1010000000,1010000000));
        CHECK(!gate.Stage(c,c.source.nowNs));}
    {Gate gate;auto c=Command();c.kind=KnifeCommandKind::Attack;CHECK(!gate.Stage(c,c.source.nowNs));
        c=Command();c.source.tracked[0]=false;CHECK(!gate.Stage(c,c.source.nowNs));
        c=Command();c.returnToEmptyHands=false;CHECK(!gate.Stage(c,c.source.nowNs));}
    {struct State{unsigned entries=0,exits=0;};State state;
        OriginalFiringObserver observer{&state,[](void* p,const OriginalFiringUpdate& c)noexcept{auto& s=*static_cast<State*>(p);++s.entries;
            OriginalFiringToken t;t.words[0]=c.firing;return t;},
            [](void* p,const OriginalFiringUpdate& c,const OriginalFiringToken& t)noexcept{
                auto& s=*static_cast<State*>(p);if(t.words[0]==c.firing)++s.exits;}};
        const auto call=[&](bool early){OriginalFiringScope scope(observer,{0x20000,0x30000,0x40000,0});if(early)return;};
        call(false);call(true);CHECK(state.entries==2&&state.exits==2);
        {OriginalFiringScope scope({},{});}CHECK(state.entries==2&&state.exits==2);}
    std::puts("Six focused native draw/return gate and shared observer groups PASS; first source, once per inventory, accepted completion, no replay, expiry/lifetime, all exits.");
}
