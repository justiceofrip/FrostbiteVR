#include "Test.h"
#include "fvr/interaction/HandInteraction.h"
using namespace fvr::interaction;
namespace {
constexpr std::int64_t ms=1000000;
constexpr InteractionHand left=InteractionHand::Left,right=InteractionHand::Right;
InteractionHand Other(InteractionHand hand){return hand==left?right:left;}
struct Fixture {
    HandInteraction hands;
    HandInteractionSample sample{{10,1,1,1},1,1000*ms,1150*ms,1000*ms,true,{true,true},{false,false}};
    std::array<std::uint64_t,2> intents{};
    HandClaimToken gun{},companion{};
    HandClaimRequest Request(InteractionHand hand,HandClaimKind kind,std::uint64_t contact,std::uint64_t parent=0){
        return {sample.owner,hand,kind,{100,1},{{contact,1},sample.sequence,sample.deadlineNs,true},++intents[static_cast<std::size_t>(hand)],parent};
    }
    explicit Fixture(InteractionHand gunHand=right){
        auto g=hands.Acquire(sample,Request(gunHand,HandClaimKind::GunHold,1));if(g.claim)gun=g.claim->token;
        auto c=hands.Acquire(sample,Request(Other(gunHand),HandClaimKind::WeaponSupport,2,gun.id));if(c.claim)companion=c.claim->token;
    }
    void Next(){sample.nowNs+=10*ms;sample.observedNs=sample.nowNs;sample.deadlineNs=sample.nowNs+150*ms;++sample.sequence;}
    HandGunCustodyTransfer Transfer(bool nextCompanion=true,HandClaimKind kind=HandClaimKind::Mechanism){
        HandGunCustodyTransfer t{gun,companion,{Request(Other(gun.hand),HandClaimKind::GunHold,20),sample},std::nullopt};
        if(nextCompanion)t.nextCompanion=HandCustodyTarget{Request(gun.hand,kind,30),sample};
        return t;
    }
    bool Unchanged()const {const auto g=hands.Current(gun.hand),c=hands.Current(companion.hand);return g&&c&&g->token==gun&&c->token==companion;}
};
unsigned Releases(const HandInteractionResult& result){unsigned n=0;for(const auto& r:result.released)if(r)++n;return n;}
bool Released(const HandInteractionResult& result,HandClaimToken token,HandInteractionReason reason){for(const auto& r:result.released)if(r&&r->token==token&&r->reason==reason)return true;return false;}

int PairExchangeBothDirections(){
    for(auto side:{left,right}){
        Fixture f(side);CHECK(f.gun.id&&f.companion.id);const auto oldGun=f.gun,oldSupport=f.companion;
        auto result=f.hands.TransferGunCustody(f.sample,f.Transfer());const auto& out=result.transaction;
        CHECK(out.accepted&&out.claim&&result.companion&&Releases(out)==2);
        CHECK(Released(out,oldGun,HandInteractionReason::Transferred)&&Released(out,oldSupport,HandInteractionReason::Transferred));
        CHECK(out.claim->token.hand==Other(side)&&out.claim->token.kind==HandClaimKind::GunHold&&out.claim->token.prerequisiteClaim==0);
        CHECK(result.companion->token.hand==side&&result.companion->token.kind==HandClaimKind::Mechanism&&result.companion->token.prerequisiteClaim==out.claim->token.id);
        CHECK(out.claim->token.id>oldSupport.id&&result.companion->token.id>out.claim->token.id);
        CHECK(!f.hands.Release(f.sample,oldGun).accepted&&!f.hands.Release(f.sample,oldSupport).accepted);
        f.gun=out.claim->token;f.companion=result.companion->token;f.Next();
        result=f.hands.TransferGunCustody(f.sample,f.Transfer(true,HandClaimKind::WeaponSupport));
        CHECK(result.transaction.accepted&&result.transaction.claim&&result.companion&&result.transaction.claim->token.hand==side);
        CHECK(result.companion->token.prerequisiteClaim==result.transaction.claim->token.id);
        const auto release=f.hands.Release(f.sample,result.transaction.claim->token);
        CHECK(release.accepted&&Releases(release)==2&&!f.hands.Current(left)&&!f.hands.Current(right));
        CHECK(Released(release,result.companion->token,HandInteractionReason::DependencyLost));
    }return 0;
}
int ReleaseTravelRegripAndReturn(){
    Fixture f;auto request=f.Transfer(false);const auto oldRightEvidence=f.sample;
    auto result=f.hands.TransferGunCustody(f.sample,request);CHECK(result.transaction.accepted&&result.transaction.claim&&!result.companion);
    const auto leftGun=result.transaction.claim->token;CHECK(!f.hands.Current(right)&&f.hands.Current(left)->token==leftGun);
    // Empty right hand cannot reuse the geometry preceding its custody release.
    auto mechanism=f.Request(right,HandClaimKind::Mechanism,31,leftGun.id);
    auto rejected=f.hands.AcquireFrom(f.sample,oldRightEvidence,mechanism);CHECK(!rejected.accepted&&rejected.reason==HandInteractionReason::ContactMismatch);
    f.Next();f.sample.released[1]=true;CHECK(f.hands.Update(f.sample).inputValid);const auto neutral=f.sample;
    CHECK(f.hands.Current(left)->token==leftGun&&!f.hands.Current(right));
    f.Next();f.sample.released[1]=false;mechanism=f.Request(right,HandClaimKind::Mechanism,31,leftGun.id);
    mechanism.contact.inputSequence=neutral.sequence;mechanism.contact.deadlineNs=neutral.deadlineNs;
    auto acquired=f.hands.AcquireFrom(f.sample,neutral,mechanism);CHECK(acquired.accepted&&acquired.claim&&acquired.claim->inputSequence==neutral.sequence);
    f.Next();f.sample.released[1]=true;auto released=f.hands.Update(f.sample);
    CHECK(Releases(released)==1&&f.hands.Current(left)->token==leftGun&&!f.hands.Current(right));
    f.Next();f.sample.released[1]=false;f.gun=leftGun;request=f.Transfer(true,HandClaimKind::WeaponSupport);request.companion.reset();
    result=f.hands.TransferGunCustody(f.sample,request);
    CHECK(result.transaction.accepted&&result.transaction.claim&&result.companion&&Releases(result.transaction)==1);
    CHECK(result.transaction.claim->token.hand==right&&result.companion->token.hand==left);
    CHECK(result.companion->token.prerequisiteClaim==result.transaction.claim->token.id);
    f.Next();f.sample.released[0]=true;f.hands.Update(f.sample);
    CHECK(!f.hands.Current(left)&&f.hands.Current(right)->token==result.transaction.claim->token);return 0;
}
int SeparateHistoricalProofsStayImmutable(){
    Fixture f;const auto leftEvidence=f.sample;f.Next();f.hands.Update(f.sample);const auto rightEvidence=f.sample;
    f.Next();auto request=f.Transfer();request.nextGun.evidence=leftEvidence;
    request.nextGun.request.contact.inputSequence=leftEvidence.sequence;request.nextGun.request.contact.deadlineNs=leftEvidence.deadlineNs;
    request.nextCompanion->evidence=rightEvidence;request.nextCompanion->request.contact.inputSequence=rightEvidence.sequence;request.nextCompanion->request.contact.deadlineNs=rightEvidence.deadlineNs;
    const auto result=f.hands.TransferGunCustody(f.sample,request);CHECK(result.transaction.accepted&&result.transaction.claim&&result.companion);
    CHECK(result.transaction.claim->inputSequence==leftEvidence.sequence&&result.transaction.claim->deadlineNs==leftEvidence.deadlineNs);
    CHECK(result.companion->inputSequence==rightEvidence.sequence&&result.companion->deadlineNs==rightEvidence.deadlineNs);
    // Child outlives its geometry lease only in time, never its parent custody.
    f.sample.nowNs=leftEvidence.deadlineNs;f.sample.observedNs=f.sample.nowNs;f.sample.deadlineNs=f.sample.nowNs+150*ms;++f.sample.sequence;
    const auto expired=f.hands.Update(f.sample);CHECK(Releases(expired)==2&&!f.hands.Current(left)&&!f.hands.Current(right));
    CHECK(Released(expired,result.companion->token,HandInteractionReason::DependencyLost));return 0;
}
int FailedRequestsPreserveBothClaimsAndConsumeIntents(){
    for(unsigned variant=0;variant<13;++variant){
        Fixture f;auto request=f.Transfer();
        if(variant==0)request.nextGun.request.contact.eligible=false;
        if(variant==1)request.nextCompanion->request.contact.eligible=false;
        if(variant==2)++request.nextCompanion->request.contact.deadlineNs;
        if(variant==3)request.nextCompanion->request.contact.key=request.nextGun.request.contact.key;
        if(variant==4)request.nextGun.request.item.generation++;
        if(variant==5)request.nextCompanion->request.owner.space++;
        if(variant==6)request.nextCompanion->request.kind=HandClaimKind::AmmoObject;
        if(variant==7)request.nextGun.request.prerequisiteClaim=f.gun.id;
        if(variant==8)request.nextCompanion->request.prerequisiteClaim=f.gun.id;
        if(variant==9)request.nextGun.evidence.tracked[1]=false;
        if(variant==10)request.nextCompanion->evidence.deadlineNs--;
        if(variant==11)request.companion.reset();
        if(variant==12)request.nextGun.request.kind=HandClaimKind::WeaponSupport;
        const auto result=f.hands.TransferGunCustody(f.sample,request);CHECK(!result.transaction.accepted&&Releases(result.transaction)==0&&!result.companion&&f.Unchanged());
        // Fresh data cannot make either failed destination intent win later.
        auto retry=f.Transfer();retry.nextGun.request.intent=request.nextGun.request.intent;retry.nextCompanion->request.intent=request.nextCompanion->request.intent;
        const auto repeated=f.hands.TransferGunCustody(f.sample,retry);CHECK(!repeated.transaction.accepted&&repeated.transaction.reason==HandInteractionReason::StaleIntent&&f.Unchanged());
    }return 0;
}
int UnrelatedClaimAndStaleTokensCannotParticipate(){
    Fixture f;auto request=f.Transfer();request.gun.contact.generation++;
    auto result=f.hands.TransferGunCustody(f.sample,request);CHECK(!result.transaction.accepted&&result.transaction.reason==HandInteractionReason::StaleToken&&f.Unchanged());
    request=f.Transfer();request.companion->id++;result=f.hands.TransferGunCustody(f.sample,request);CHECK(!result.transaction.accepted&&f.Unchanged());
    CHECK(f.hands.Release(f.sample,f.companion).accepted);f.Next();
    auto ammo=f.Request(left,HandClaimKind::AmmoObject,88);ammo.item={999,1};const auto a=f.hands.Acquire(f.sample,ammo);CHECK(a.accepted&&a.claim);
    request=f.Transfer();request.companion=a.claim->token;result=f.hands.TransferGunCustody(f.sample,request);
    CHECK(!result.transaction.accepted&&result.transaction.reason==HandInteractionReason::MissingPrerequisite);
    CHECK(f.hands.Current(left)->token==a.claim->token&&f.hands.Current(right)->token==f.gun);return 0;
}
int LifecycleLossCannotResurrectCustody(){
    for(unsigned variant=0;variant<5;++variant){
        Fixture f;auto request=f.Transfer();f.Next();
        if(variant==0)f.sample.released[1]=true;
        if(variant==1)f.sample.tracked[1]=false;
        if(variant==2)f.sample.focused=false;
        if(variant==3)f.sample.owner.equipGeneration++;
        if(variant==4){f.sample.nowNs=1150*ms;f.sample.observedNs=f.sample.nowNs;f.sample.deadlineNs=f.sample.nowNs+150*ms;}
        const auto result=f.hands.TransferGunCustody(f.sample,request);
        CHECK(!result.transaction.accepted&&Releases(result.transaction)==2&&!result.companion&&!f.hands.Current(left)&&!f.hands.Current(right));
    }
    Fixture f;auto request=f.Transfer();auto old=f.sample;f.Next();f.hands.Update(f.sample);
    const auto result=f.hands.TransferGunCustody(old,request);CHECK(!result.transaction.accepted&&result.transaction.reason==HandInteractionReason::StaleInput&&f.Unchanged());return 0;
}
int HistoricalReleaseAndTrackingBarriersRemainEffective(){
    for(bool tracking:{false,true}){
        Fixture f;const auto old=f.sample;f.Next();
        if(tracking)f.sample.tracked[0]=false;else f.sample.released[0]=true;
        f.hands.Update(f.sample);CHECK(f.hands.Current(right)&&!f.hands.Current(left));
        f.Next();f.sample.tracked[0]=true;f.sample.released[0]=false;
        const auto support=f.hands.Acquire(f.sample,f.Request(left,HandClaimKind::WeaponSupport,2,f.gun.id));CHECK(support.accepted&&support.claim);f.companion=support.claim->token;
        auto request=f.Transfer();request.nextGun.evidence=old;request.nextGun.request.contact.inputSequence=old.sequence;request.nextGun.request.contact.deadlineNs=old.deadlineNs;
        const auto result=f.hands.TransferGunCustody(f.sample,request);CHECK(!result.transaction.accepted&&result.transaction.reason==HandInteractionReason::ContactMismatch&&f.Unchanged());
    }return 0;
}
int EvictedAndForgedSourceCannotExtendLease(){
    for(unsigned variant=0;variant<3;++variant){
        Fixture f;const auto source=f.sample;
        if(variant==0){
            for(unsigned i=0;i<33;++i){++f.sample.sequence;f.sample.nowNs+=ms;f.sample.observedNs=f.sample.nowNs;f.sample.deadlineNs=f.sample.nowNs+150*ms;f.hands.Update(f.sample);}
        }else {f.Next();f.hands.Update(f.sample);}
        auto request=f.Transfer();request.nextGun.evidence=source;
        request.nextGun.request.contact.inputSequence=source.sequence;request.nextGun.request.contact.deadlineNs=source.deadlineNs;
        if(variant==1){request.nextGun.evidence.deadlineNs+=ms;request.nextGun.request.contact.deadlineNs+=ms;}
        if(variant==2){request.nextGun.evidence.observedNs+=ms;}
        const auto result=f.hands.TransferGunCustody(f.sample,request);
        CHECK(!result.transaction.accepted&&f.Unchanged());
        CHECK(result.transaction.reason==(variant==0?HandInteractionReason::MissingEvidence:HandInteractionReason::ContactMismatch));
    }return 0;
}
int SustainedExchangesNeverExposeOldDependency(){
    Fixture f;std::uint64_t highest=f.companion.id;
    for(unsigned cycle=0;cycle<512;++cycle){
        f.Next();const auto result=f.hands.TransferGunCustody(f.sample,f.Transfer(true,cycle%2?HandClaimKind::WeaponSupport:HandClaimKind::Mechanism));
        CHECK(result.transaction.accepted&&result.transaction.claim&&result.companion&&Releases(result.transaction)==2);
        CHECK(result.transaction.claim->token.id>highest&&result.companion->token.id>result.transaction.claim->token.id);
        CHECK(result.companion->token.prerequisiteClaim==result.transaction.claim->token.id);
        f.gun=result.transaction.claim->token;f.companion=result.companion->token;highest=f.companion.id;
        CHECK(f.Unchanged());
    }return 0;
}
}
int main(){
    if(PairExchangeBothDirections()||ReleaseTravelRegripAndReturn()||SeparateHistoricalProofsStayImmutable()||
       FailedRequestsPreserveBothClaimsAndConsumeIntents()||UnrelatedClaimAndStaleTokensCannotParticipate()||
       LifecycleLossCannotResurrectCustody()||HistoricalReleaseAndTrackingBarriersRemainEffective()||EvictedAndForgedSourceCannotExtendLease()||SustainedExchangesNeverExposeOldDependency())return 1;
    std::puts("Hand gun custody: 9 groups passed, including 512 sustained exchanges and real empty-hand release/regrip.");return 0;
}
