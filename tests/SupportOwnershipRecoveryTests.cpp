#include "Test.h"
#include "fvr/interaction/SupportOwnershipRecovery.h"
#include <cmath>
using namespace fvr;
using namespace fvr::interaction;
namespace {
constexpr std::int64_t ms=1000000,base=1000000000;
constexpr auto left=InteractionHand::Left,right=InteractionHand::Right;
constexpr HandInteractionOwner owner{1,2,3,4};
constexpr HandInteractionKey item{5,6},supportContact{2,7};
constexpr SupportGripOwner gripOwner{1,2,7};
HandInteractionSample Sample(std::uint64_t generation,std::int64_t offset){
    return {owner,generation,base+offset,base+offset+100*ms,base+offset,true,{true,true}};
}
InputFrame Input(const HandInteractionSample& sample,float squeeze){
    InputFrame in{};in.generation=sample.sequence;in.spaceGeneration=owner.space;
    in.predictedNs=5000000000+sample.nowNs-base;in.focused=in.headValid=true;
    for(auto& hand:in.hands){hand.gripTracked=hand.aimTracked=true;hand.active=Components;}
    in.hands[0].grip.position.z=-.4f;in.hands[0].squeeze=squeeze;return in;
}
HandContactProof Proof(const HandInteractionSample& s,HandInteractionKey key=supportContact){return {key,s.sequence,s.deadlineNs,true};}
struct Fixture {
    HandInteraction arbiter;SupportGrip policy;
    HandInteractionSample current{},source{};
    HandInteractionResult lifecycle{};
    std::optional<HandClaim> previous{};
    SupportGripResult proposed{},before{};
    std::uint64_t intent=0,grasp=0;HandClaimToken originalGun{};
    SupportGripContact geometry{true,.03f,{}};
    HandClaimRequest Request(HandClaimKind kind,HandContactProof proof,std::uint64_t parent=0){
        return {owner,kind==HandClaimKind::GunHold?right:left,kind,item,proof,++intent,parent};
    }
    bool Begin(bool expireGun=false,bool dependency=false,std::int64_t after=115*ms){
        auto first=Sample(1,0);first.released[0]=true;
        policy.Update(gripOwner,Input(first,0),geometry);arbiter.Update(first);
        auto gun=arbiter.Acquire(first,Request(HandClaimKind::GunHold,Proof(first,{1,6})));
        if(!gun.claim)return false;
        auto press=Sample(2,10*ms);before=policy.Update(gripOwner,Input(press,.9f),geometry);
        if(!before.holding||!before.engaged)return false;
        grasp=before.token;arbiter.Update(press);
        gun=arbiter.Renew(press,gun.claim->token,Proof(press,{1,6}));if(!gun.claim)return false;
        originalGun=gun.claim->token;
        const auto support=arbiter.AcquireFrom(press,press,Request(HandClaimKind::WeaponSupport,Proof(press),gun.claim->token.id));
        if(!support.claim)return false;
        // Raw packet 3 is observed and later rendered. The support claim still
        // uses packet 2, modelling the adapter's real publication lag.
        source=Sample(3,20*ms);auto moved=Input(source,.9f);moved.hands[0].grip.position.x=.1f;
        before=policy.Update(gripOwner,moved,geometry);arbiter.Update(source);
        if(!expireGun&&!arbiter.Renew(source,gun.claim->token,Proof(source,{1,6})).accepted)return false;
        if(dependency&&!arbiter.RenewFrom(source,source,support.claim->token,Proof(source)).accepted)return false;
        previous=arbiter.Current(left);
        current=Sample(10,after);auto resumed=Input(current,.9f);resumed.hands[0].grip.position.x=.1f;
        // A real 95ms gather gap (115-20) expires packet-2's 110ms lease.
        // Packet-3 geometry remains valid until 120ms, with no retimestamping.
        lifecycle=arbiter.Update(current);
        auto nowGun=arbiter.Current(right);
        if(nowGun)arbiter.Renew(current,nowGun->token,Proof(current,{1,6}));
        else arbiter.Acquire(current,Request(HandClaimKind::GunHold,Proof(current,{1,6})));
        proposed=policy.Update(gripOwner,resumed,geometry);
        return previous.has_value()&&arbiter.Current(right).has_value();
    }
    bool Eligible(const HandInteractionSample& evidence,HandContactProof proof,bool cancel=false)const{
        return SupportLeaseContinuationEligible(previous,lifecycle,current,item,grasp,proposed,
            arbiter.Current(left),arbiter.Current(right),evidence,proof,cancel);
    }
    HandInteractionResult Continue(const HandInteractionSample& evidence,HandContactProof proof){
        if(!Eligible(evidence,proof))return {};
        return arbiter.AcquireFrom(current,evidence,Request(HandClaimKind::WeaponSupport,proof,arbiter.Current(right)->token.id));
    }
};
int FreshGeometryPreservesGrasp(){
    Fixture f;CHECK(f.Begin());CHECK(!f.arbiter.Current(left));
    CHECK(f.proposed.holding&&!f.proposed.engaged&&!f.proposed.released&&f.proposed.token==f.grasp);
    CHECK(Near(f.proposed.correctionRadians,f.before.correctionRadians,.00001f));
    const auto out=f.Continue(f.source,Proof(f.source));CHECK(out.accepted&&out.claim);
    CHECK(out.claim->token.id!=f.previous->token.id&&out.claim->inputSequence==f.source.sequence);
    CHECK(out.claim->deadlineNs==f.source.deadlineNs&&out.claim->deadlineNs<f.current.deadlineNs);
    CHECK(f.arbiter.Current(right)->token==f.originalGun);
    CHECK(!f.arbiter.Renew(f.current,f.previous->token,Proof(f.current)).accepted);
    CHECK(f.proposed.token==f.grasp);return 0;
}
int SimultaneousExpiryAndDependencyBarrier(){
    Fixture both;CHECK(both.Begin(true));
    CHECK(both.arbiter.Current(right)->token.id!=both.originalGun.id);
    auto out=both.Continue(both.source,Proof(both.source));CHECK(out.accepted&&out.claim);
    CHECK(out.claim->token.prerequisiteClaim==both.arbiter.Current(right)->token.id);
    // If the left lease survived but its parent expired, Update installs a
    // strict dependency barrier. Geometry continuation cannot bypass it.
    Fixture dependent;CHECK(dependent.Begin(true,true));
    CHECK(!dependent.Eligible(dependent.source,Proof(dependent.source)));
    CHECK(!dependent.Eligible(dependent.current,Proof(dependent.current)));
    auto request=dependent.Request(HandClaimKind::WeaponSupport,Proof(dependent.current),dependent.arbiter.Current(right)->token.id);
    out=dependent.arbiter.AcquireFrom(dependent.current,dependent.current,request);
    CHECK(!out.accepted&&out.reason==HandInteractionReason::ContactMismatch);return 0;
}
int StaleAndInventedEvidenceRejected(){
    Fixture f;CHECK(f.Begin());auto old=Sample(2,10*ms);
    CHECK(!f.Eligible(old,Proof(old)));
    auto stale=f.source;stale.deadlineNs=f.current.nowNs;
    CHECK(!f.Eligible(stale,Proof(stale)));
    auto wrong=f.source;++wrong.owner.space;CHECK(!f.Eligible(wrong,Proof(wrong)));
    wrong=f.source;wrong.released[0]=true;CHECK(!f.Eligible(wrong,Proof(wrong)));
    wrong=f.source;wrong.tracked[0]=false;CHECK(!f.Eligible(wrong,Proof(wrong)));
    auto proof=Proof(f.source);++proof.key.generation;CHECK(!f.Eligible(f.source,proof));
    proof=Proof(f.source);++proof.deadlineNs;CHECK(!f.Eligible(f.source,proof));
    // The predicate cannot replace arbiter history: a plausible unobserved
    // packet remains rejected by the required final AcquireFrom.
    auto absent=Sample(4,25*ms);CHECK(f.Eligible(absent,Proof(absent)));
    CHECK(!f.Continue(absent,Proof(absent)).accepted&&!f.arbiter.Current(left));
    Fixture noFresh;CHECK(noFresh.Begin(false,false,130*ms));
    CHECK(noFresh.proposed.holding);CHECK(!noFresh.Eligible(noFresh.source,Proof(noFresh.source)));
    return 0;
}
int PolicyAndCurrentSafetyCannotBeBypassed(){
    for(unsigned mode=0;mode<7;++mode){
        Fixture f;CHECK(f.Begin());auto input=f.proposed.input;auto ownerNow=gripOwner;auto contact=f.geometry;
        if(mode==0)input.hands[0].squeeze=0;
        if(mode==1)input.hands[0].gripTracked=false;
        if(mode==2)input.focused=false;
        if(mode==3)++ownerNow.equipped;
        if(mode==4)contact.valid=false;
        if(mode==5)input.predictedNs+=300*ms;
        f.proposed=f.policy.Update(ownerNow,input,contact,mode==6);
        CHECK(!f.proposed.holding&&!f.Eligible(f.source,Proof(f.source)));
    }
    Fixture f;CHECK(f.Begin());CHECK(!f.Eligible(f.source,Proof(f.source),true));
    auto good=f.current;
    f.current.released[0]=true;CHECK(!f.Eligible(f.source,Proof(f.source)));f.current=good;
    f.current.tracked[1]=false;CHECK(!f.Eligible(f.source,Proof(f.source)));f.current=good;
    ++f.current.owner.equipGeneration;CHECK(!f.Eligible(f.source,Proof(f.source)));f.current=good;
    ++f.grasp;CHECK(!f.Eligible(f.source,Proof(f.source)));return 0;
}
int ExactGunLineageRequired(){
    Fixture liveParent;CHECK(liveParent.Begin());
    auto gun=liveParent.arbiter.Current(right);CHECK(gun);++gun->token.id;
    CHECK(!SupportLeaseContinuationEligible(liveParent.previous,liveParent.lifecycle,liveParent.current,item,
        liveParent.grasp,liveParent.proposed,std::nullopt,gun,liveParent.source,Proof(liveParent.source)));
    Fixture expiredParent;CHECK(expiredParent.Begin(true));gun=expiredParent.arbiter.Current(right);CHECK(gun);
    gun->token.id=expiredParent.originalGun.id;
    CHECK(!SupportLeaseContinuationEligible(expiredParent.previous,expiredParent.lifecycle,expiredParent.current,item,
        expiredParent.grasp,expiredParent.proposed,std::nullopt,gun,expiredParent.source,Proof(expiredParent.source)));
    return 0;
}
int OccupiedAndLifecycleLossRejected(){
    Fixture f;CHECK(f.Begin());
    auto request=f.Request(HandClaimKind::Sight,Proof(f.current,{3,6}),f.arbiter.Current(right)->token.id);
    const auto sight=f.arbiter.Acquire(f.current,request);CHECK(sight.claim);
    CHECK(!f.Eligible(f.source,Proof(f.source))&&f.arbiter.Current(left)->token==sight.claim->token);
    for(const auto reason:{HandInteractionReason::TrackingLost,HandInteractionReason::FocusLost,
        HandInteractionReason::Released,HandInteractionReason::IdentityChanged,HandInteractionReason::Reset}){
        Fixture loss;CHECK(loss.Begin());
        for(auto& event:loss.lifecycle.released)if(event&&event->token==loss.previous->token)event->reason=reason;
        CHECK(!loss.Eligible(loss.source,Proof(loss.source)));
    }
    // Exact historical barriers still win even when a caller has retained a
    // once-valid eligibility snapshot; neither the helper nor a new intent
    // can erase an explicit release at the current packet.
    Fixture barrier;CHECK(barrier.Begin());auto held=barrier.Continue(barrier.source,Proof(barrier.source));CHECK(held.claim);
    CHECK(barrier.arbiter.Release(barrier.current,held.claim->token).accepted);
    CHECK(!barrier.Continue(barrier.source,Proof(barrier.source)).accepted);
    CHECK(!barrier.arbiter.Current(left));return 0;
}
}
int main(){
    return FreshGeometryPreservesGrasp()||SimultaneousExpiryAndDependencyBarrier()||
        StaleAndInventedEvidenceRejected()||PolicyAndCurrentSafetyCannotBeBypassed()||ExactGunLineageRequired()||OccupiedAndLifecycleLossRejected();
}

