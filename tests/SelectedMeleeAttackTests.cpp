#include "Test.h"
#include "fvr/interaction/SelectedMeleeAttack.h"
using namespace fvr::interaction;
namespace {
constexpr std::int64_t ms=1000000;
struct Fixture {
    SelectedMeleeAttack policy;HandInteraction hands;SelectedMeleeSample s{};
    std::uint64_t claims=0;unsigned emitted=0;std::optional<SelectedMeleeIntent> last;
    Fixture(){s.motion.source={{1,2,3,4},1,1000*ms,1100*ms,1000*ms,true,{true,true},{false,false}};
        s.identity={s.motion.source.owner,{42,1},{53,1},1};s.selected=s.geometryCurrent=s.motion.attackAvailable=true;
        Refresh();(void)policy.Update(s,hands);}
    void Refresh(){
        s.motion.source.owner=s.identity.owner;hands.Update(s.motion.source);
        HandContactProof contact{{11,1},s.motion.source.sequence,s.motion.source.deadlineNs,true};
        auto held=hands.Current(InteractionHand::Right);
        if(held){auto r=hands.Renew(s.motion.source,held->token,contact);if(r.claim)s.motion.hold=r.claim->token;}
        else {auto r=hands.Acquire(s.motion.source,{s.identity.owner,InteractionHand::Right,HandClaimKind::GunHold,
            s.identity.item,contact,++claims,0});if(r.claim)s.motion.hold=r.claim->token;}
    }
    SelectedMeleeResult Step(float dz=0,float trigger=0){
        ++s.motion.source.sequence;s.motion.source.observedNs+=10*ms;s.motion.source.nowNs=s.motion.source.observedNs;
        s.motion.source.deadlineNs=s.motion.source.nowNs+100*ms;s.motion.tip.z+=dz;s.trigger=trigger;Refresh();
        auto r=policy.Update(s,hands);if(r.intent){++emitted;last=r.intent;}return r;
    }
    void Rest(unsigned n=20){while(n--)Step();}
    void Stab(){for(unsigned i=0;i<8;++i)Step(.012f);}
    bool Outcome(SelectedMeleeOutcome value){return last&&policy.Resolve(last->serial,last->identity,value);}
};
int OneMotionIntentAndExactOutcome(){
    Fixture f;f.Rest();f.Stab();CHECK(f.emitted==1&&f.last->cause==SelectedMeleeCause::Stroke&&f.last->stroke);
    CHECK(f.last->hold==f.s.motion.hold&&f.last->identity==f.s.identity);
    f.Rest();f.Stab();CHECK(f.emitted==1&&f.policy.PendingState()==SelectedMeleePending::Staged);
    CHECK(!f.policy.Resolve(f.last->serial+1,f.last->identity,SelectedMeleeOutcome::Submitted));
    CHECK(!f.Outcome(SelectedMeleeOutcome::Completed));CHECK(f.Outcome(SelectedMeleeOutcome::Submitted));
    CHECK(!f.Outcome(SelectedMeleeOutcome::RejectedNoEffect));CHECK(f.Outcome(SelectedMeleeOutcome::Completed));
    f.Stab();CHECK(f.emitted==1);f.Rest();f.Stab();CHECK(f.emitted==2);return 0;
}
int TriggerAndMotionShareOneLatch(){
    Fixture f;f.Rest();for(unsigned i=0;i<4;++i)f.Step(.012f);
    f.Step(.02f,1);CHECK(f.emitted==1&&f.last->cause==SelectedMeleeCause::Trigger&&!f.last->stroke);
    for(unsigned i=0;i<30;++i)f.Step(.01f,1);CHECK(f.emitted==1);
    CHECK(f.Outcome(SelectedMeleeOutcome::RejectedNoEffect));f.Step(0,1);CHECK(f.emitted==1);
    f.Step(0,0);f.Step(0,1);CHECK(f.emitted==2);return 0;
}
int TriggerFallbackWithoutGeometry(){
    Fixture f;f.s.geometryCurrent=false;f.Rest();f.Stab();CHECK(!f.emitted);
    f.Step(0,1);CHECK(f.emitted==1&&f.last->cause==SelectedMeleeCause::Trigger);return 0;
}
int DrawRegripReconnectCannotAttack(){
    Fixture draw;draw.Stab();CHECK(!draw.emitted);draw.Rest();draw.Stab();CHECK(draw.emitted==1);
    Fixture f;f.Rest();f.hands.Release(f.s.motion.source,f.s.motion.hold);f.Step(0,1);CHECK(!f.emitted);
    for(unsigned i=0;i<20;++i)f.Step(0,1);CHECK(!f.emitted);f.Step();f.Step(0,1);CHECK(f.emitted==1);
    Fixture lost;lost.Rest();lost.s.motion.source.focused=false;lost.Step(0,1);CHECK(!lost.emitted);
    lost.s.motion.source.focused=true;for(unsigned i=0;i<20;++i)lost.Step(0,1);CHECK(!lost.emitted);
    lost.Step();lost.Step(0,1);CHECK(lost.emitted==1);return 0;
}
int PendingSurvivesInvalidationAndNeverOwnsHands(){
    Fixture f;f.Rest();f.Step(0,1);CHECK(f.emitted==1);CHECK(f.Outcome(SelectedMeleeOutcome::Submitted));
    const auto old=*f.last;const auto held=f.hands.Current(InteractionHand::Right);
    f.policy.InvalidateMotion();CHECK(f.policy.Pending()&&f.hands.Current(InteractionHand::Right)->token==held->token);
    CHECK(f.Outcome(SelectedMeleeOutcome::Unknown));++f.s.identity.binding.generation;++f.s.identity.inputEpoch;
    f.Rest();f.Stab();CHECK(f.emitted==1&&f.policy.Pending()->identity==old.identity);
    CHECK(!f.policy.Resolve(old.serial,f.s.identity,SelectedMeleeOutcome::Completed));
    CHECK(f.policy.Resolve(old.serial,old.identity,SelectedMeleeOutcome::Completed));
    f.Rest();f.Stab();CHECK(f.emitted==2);return 0;
}
int DuplicateStaleAndChangedInputCannotRepeat(){
    Fixture f;f.Rest();f.Step(0,1);const auto serial=f.last->serial;
    for(unsigned i=0;i<100;++i)CHECK(!f.policy.Update(f.s,f.hands).intent);
    CHECK(f.Outcome(SelectedMeleeOutcome::RejectedNoEffect));CHECK(!f.policy.Update(f.s,f.hands).intent);
    f.s.trigger=0;CHECK(!f.policy.Update(f.s,f.hands).intent);f.Step(0,1);CHECK(f.emitted==1);
    f.Step();f.Step(0,1);CHECK(f.emitted==2&&f.last->serial>serial);
    Fixture expiry;expiry.Rest();expiry.s.motion.source.nowNs=expiry.s.motion.source.deadlineNs;expiry.s.trigger=1;
    CHECK(!expiry.policy.Update(expiry.s,expiry.hands).intent);return 0;
}
int NativeReadinessNeverCompletesAnAttack(){
    Fixture f;f.Rest();f.Step(0,1);f.s.motion.attackAvailable=false;f.Rest();
    f.s.motion.attackAvailable=true;f.Rest();f.Stab();CHECK(f.emitted==1&&f.policy.Pending());
    CHECK(f.Outcome(SelectedMeleeOutcome::OwnerRetired));CHECK(!f.policy.Pending());
    Fixture unavailable;unavailable.Rest();unavailable.s.motion.attackAvailable=false;unavailable.Step(0,1);
    unavailable.s.motion.attackAvailable=true;unavailable.Step(0,1);CHECK(!unavailable.emitted);
    unavailable.Step();unavailable.Step(0,1);CHECK(unavailable.emitted==1);return 0;
}
int SharedHoldAndIdentityRequired(){
    for(unsigned fault=0;fault<5;++fault){Fixture f;f.Rest();
        if(fault==0)++f.s.motion.hold.id;
        if(fault==1)++f.s.identity.item.generation;
        if(fault==2)f.s.selected=false;
        if(fault==3)f.s.identity.binding={};
        if(fault==4)f.s.motion.hold.prerequisiteClaim=7;
        f.s.trigger=1;CHECK(!f.policy.Update(f.s,f.hands).intent);
    }
    for(unsigned field=0;field<4;++field){Fixture f;f.Rest();
        if(field==0)++f.s.identity.inputEpoch;
        if(field==1)++f.s.identity.binding.generation;
        if(field==2)++f.s.identity.owner.space;
        if(field==3)++f.s.identity.owner.equipGeneration;
        f.Step(0,1);CHECK(!f.emitted);f.Step(0,1);CHECK(!f.emitted);
        f.Step();f.Step(0,1);CHECK(f.emitted==1);
    }
    Fixture clock;clock.Rest();++clock.s.motion.source.sequence;clock.s.trigger=1;clock.Refresh();
    CHECK(clock.policy.Update(clock.s,clock.hands).reason==SelectedMeleeReason::Order);
    CHECK(!clock.policy.Pending());return 0;
}
}
int main(){return OneMotionIntentAndExactOutcome()||TriggerAndMotionShareOneLatch()||TriggerFallbackWithoutGeometry()||
    DrawRegripReconnectCannotAttack()||PendingSurvivesInvalidationAndNeverOwnsHands()||DuplicateStaleAndChangedInputCannotRepeat()||
    NativeReadinessNeverCompletesAnAttack()||SharedHoldAndIdentityRequired();}
