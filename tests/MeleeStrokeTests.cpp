#include "Test.h"
#include "fvr/interaction/MeleeStroke.h"
#include <limits>
using namespace fvr::interaction;
namespace {
constexpr std::int64_t ms=1000000;
struct Fixture {
    MeleeStroke gesture;HandInteraction hands;MeleeStrokeSample s{};
    std::uint64_t claims=0;unsigned emitted=0;InteractionHand hand;
    explicit Fixture(InteractionHand selected=InteractionHand::Right):hand(selected){s.source={{1,2,3,4},1,1000*ms,1100*ms,1000*ms,true,{true,true},{false,false}};
        s.attackAvailable=true;Refresh();gesture.Update(s,hands);}
    void Refresh(){
        hands.Update(s.source);
        const HandContactProof contact{{11,1},s.source.sequence,s.source.deadlineNs,true};
        auto c=hands.Current(hand);
        if(c){auto r=hands.Renew(s.source,c->token,contact);if(r.claim)s.hold=r.claim->token;}
        else {auto r=hands.Acquire(s.source,{s.source.owner,hand,HandClaimKind::GunHold,
            {42,1},contact,++claims,0});if(r.claim)s.hold=r.claim->token;}
    }
    MeleeStrokeResult Step(MeleePoint tip,std::int64_t dt=10*ms){
        ++s.source.sequence;s.source.observedNs+=dt;s.source.nowNs=s.source.observedNs;
        s.source.deadlineNs=s.source.nowNs+100*ms;s.tip=tip;Refresh();
        auto out=gesture.Update(s,hands);emitted+=unsigned(out.intent.has_value());return out;
    }
    void Rest(unsigned count=20){for(unsigned i=0;i<count;++i)Step(s.tip);}
    MeleeStrokeResult Stab(){MeleeStrokeResult out;for(unsigned i=0;i<7;++i)out=Step({s.tip.x,s.tip.y,s.tip.z+.012f});return out;}
};
int OneIntentAndNativeBusy(){
    Fixture f;f.Rest();CHECK(f.gesture.Phase()==MeleeStrokePhase::Ready);
    f.Stab();CHECK(f.emitted==1&&f.gesture.Phase()==MeleeStrokePhase::Spent);
    f.Stab();CHECK(f.emitted==1);f.s.attackAvailable=false;f.Rest();f.Stab();CHECK(f.emitted==1);
    f.s.attackAvailable=true;f.Stab();CHECK(f.emitted==1);f.Rest();f.Stab();CHECK(f.emitted==2);return 0;
}
int DrawMotionCannotStab(){Fixture f;f.Stab();CHECK(!f.emitted);f.Rest();f.Stab();CHECK(f.emitted==1);return 0;}
int DeceleratingJabAndAbortedStroke(){
    // A purposeful start followed by a soft contact should still be one jab.
    // Exercise common controller sampling rates without changing thresholds.
    for(const auto hz:{72,90,120}){
        Fixture f;f.Rest();const auto dt=1000000000ll/hz;
        f.Step({0,0,.025f},dt);f.Step({0,0,.045f},dt);
        for(unsigned n=1;n<=5;++n)f.Step({0,0,.045f+.004f*n},dt);
        CHECK(f.emitted==1&&f.gesture.Phase()==MeleeStrokePhase::Spent);
        f.Rest(5);f.Stab();CHECK(f.emitted==1); // No repeat while held forward.
    }
    Fixture stopped;stopped.Rest();stopped.Step({0,0,.025f});
    stopped.Rest(36);stopped.Step({0,0,.07f});CHECK(!stopped.emitted); // Timeout cannot accumulate travel.
    Fixture reversed;reversed.Rest();reversed.Step({0,0,.04f});reversed.Step({0,0,.03f});
    reversed.Step({0,0,.08f});CHECK(!reversed.emitted);
    Fixture drift;drift.Rest();for(unsigned n=1;n<30;++n)drift.Step({0,0,n*.003f});CHECK(!drift.emitted);
    return 0;
}
int RestAndDirection(){
    Fixture slow;slow.Rest();for(unsigned i=0;i<30;++i)slow.Step({0,0,slow.s.tip.z+.003f});CHECK(!slow.emitted);
    Fixture side;side.Rest();for(unsigned i=0;i<20;++i)side.Step({side.s.tip.x+.015f,0,0});CHECK(!side.emitted);
    Fixture back;back.Rest();for(unsigned i=0;i<20;++i)back.Step({0,0,back.s.tip.z-.015f});CHECK(!back.emitted);
    Fixture turn;turn.Rest();turn.Step({0,0,.02f});turn.s.direction={1,0,0};turn.Step({0,0,.05f});CHECK(!turn.emitted);
    return 0;
}
int PacketAndPoseBoundaries(){
    for(unsigned fault=0;fault<10;++fault){
        Fixture f;f.Rest();f.Step({0,0,.02f});
        if(fault==0)f.Step({0,0,.09f},80*ms); // A tracking hole is not a long stroke.
        if(fault==1)f.Step({0,0,.5f}); // Pose jump.
        if(fault==2){f.s.source.focused=false;f.Step({0,0,.09f});}
        if(fault==3){f.s.source.tracked[1]=false;f.Step({0,0,.09f});}
        if(fault==4){f.s.source.released[1]=true;f.Step({0,0,.09f});}
        if(fault==5){++f.s.source.owner.space;f.Step({0,0,.09f});}
        if(fault==6){++f.s.source.owner.equipGeneration;f.Step({0,0,.09f});}
        if(fault==7){f.s.direction.z=std::numeric_limits<float>::quiet_NaN();f.Step({0,0,.09f});}
        if(fault==8)f.Step({0,0,.09f},0);
        if(fault==9)f.Step({0,0,.09f},-1*ms);
        CHECK(!f.emitted);
    }return 0;
}
int DuplicatesAndNoForgedCustody(){
    Fixture f;f.Rest();f.Step({0,0,.02f});
    for(unsigned i=0;i<200;++i)CHECK(!f.gesture.Update(f.s,f.hands).intent);
    f.s.tip.z=.09f;CHECK(!f.gesture.Update(f.s,f.hands).intent);CHECK(f.gesture.Phase()==MeleeStrokePhase::Unavailable);
    f.Rest();f.Stab();CHECK(f.emitted==1);
    Fixture stale;stale.Rest();++stale.s.hold.id;CHECK(!stale.gesture.Update(stale.s,stale.hands).intent);
    Fixture expired;expired.Rest();expired.s.source.nowNs=expired.s.source.deadlineNs;
    CHECK(!expired.gesture.Update(expired.s,expired.hands).intent);
    return 0;
}
int IntentProvenance(){
    Fixture f;f.Rest();std::optional<MeleeStrokeIntent> intent;
    for(unsigned i=0;i<8;++i){auto out=f.Step({0,0,f.s.tip.z+.012f});if(out.intent)intent=out.intent;}
    CHECK(intent&&intent->serial==1&&intent->hold==f.s.hold&&intent->sourceSequence<=f.s.source.sequence);
    CHECK(intent->deadlineNs-intent->observedNs==100*ms&&Near(intent->start.z,0));
    CHECK(intent->end.z>=.06f&&intent->direction==MeleePoint(0,0,1));
    f.gesture.Invalidate();f.Rest();f.Stab();CHECK(f.emitted==2);return 0;
}
int InvalidConfiguration(){
    Fixture f;f.Rest();MeleeStrokeConfig c;c.minimumSpeed=std::numeric_limits<float>::infinity();
    MeleeStroke bad(c);CHECK(bad.Update(f.s,f.hands).phase==MeleeStrokePhase::Unavailable);return 0;
}
int EitherHandAndFreshCustody(){
    Fixture f(InteractionHand::Left);f.Rest();f.Stab();CHECK(f.emitted==1);
    CHECK(f.s.hold.hand==InteractionHand::Left&&!f.hands.Current(InteractionHand::Right));
    f.Rest();f.Step({0,0,f.s.tip.z+.02f});
    f.hands.Release(f.s.source,f.s.hold);f.Refresh();
    f.Step({0,0,f.s.tip.z+.07f});CHECK(f.emitted==1); // A new hold must settle again.
    f.Rest();f.Stab();CHECK(f.emitted==2);return 0;
}
}
int main(){return OneIntentAndNativeBusy()||DrawMotionCannotStab()||DeceleratingJabAndAbortedStroke()||RestAndDirection()||PacketAndPoseBoundaries()||
    DuplicatesAndNoForgedCustody()||IntentProvenance()||InvalidConfiguration()||EitherHandAndFreshCustody();}
