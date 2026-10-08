#include "Test.h"
#include "fvr/interaction/HandInteraction.h"
#include <type_traits>
using namespace fvr::interaction;
namespace {
constexpr std::int64_t ms=1000000;
constexpr InteractionHand left=InteractionHand::Left,right=InteractionHand::Right;
constexpr HandInteractionKey rifle{100,1},shell{200,1};
struct Fixture {
    HandInteraction arbiter;
    HandInteractionSample sample{{10,1,1,1},1,1000*ms,1150*ms,1000*ms,true,{true,true}};
    std::array<std::uint64_t,2> intent{};
    Fixture(){arbiter.Update(sample);}
    void Next(std::int64_t delta=10*ms){sample.nowNs+=delta;sample.observedNs=sample.nowNs;sample.deadlineNs=sample.nowNs+150*ms;++sample.sequence;}
    HandContactProof Proof(std::uint64_t id){return {{id,1},sample.sequence,sample.deadlineNs,true};}
    HandClaimRequest Request(InteractionHand hand,HandClaimKind kind,HandInteractionKey item=rifle,std::uint64_t contact=1,std::uint64_t parent=0){
        return {sample.owner,hand,kind,item,Proof(contact),++intent[static_cast<std::size_t>(hand)],parent};
    }
    HandInteractionResult Gun(){return arbiter.Acquire(sample,Request(right,HandClaimKind::GunHold));}
    HandInteractionResult Support(const HandClaimToken& gun){return arbiter.Acquire(sample,Request(left,HandClaimKind::WeaponSupport,rifle,2,gun.id));}
};
unsigned Releases(const HandInteractionResult& r){unsigned n=0;for(const auto& e:r.released)if(e)++n;return n;}
bool Released(const HandInteractionResult& r,const HandClaimToken& token,HandInteractionReason why){
    for(const auto& e:r.released)if(e&&e->token==token&&e->reason==why)return true;return false;
}
int FailedAndSuccessfulTransfers(){
    Fixture f;auto g=f.Gun();CHECK(g.accepted&&g.claim);const auto gun=g.claim->token;
    auto s=f.Support(gun);CHECK(s.accepted&&s.claim);const auto support=s.claim->token;
    // A failed handover never drops the old support or changes its token.
    for(unsigned failure=0;failure<6;++failure){
        auto r=f.Request(left,HandClaimKind::Sight,rifle,3,gun.id);
        if(failure==0)r.contact.eligible=false;
        if(failure==1)++r.contact.inputSequence;
        if(failure==2)r.kind=static_cast<HandClaimKind>(255);
        if(failure==3)++r.contact.deadlineNs;
        if(failure==4)r.item.generation=2;
        if(failure==5){r.hand=right;r.intent=++f.intent[1];r.kind=HandClaimKind::AmmoObject;r.item=shell;r.prerequisiteClaim=0;}
        const auto out=f.arbiter.Transfer(f.sample,support,r);
        CHECK(!out.accepted&&Releases(out)==0);
        CHECK(f.arbiter.Current(left)->token==support&&f.arbiter.Current(right)->token==gun);
        CHECK(f.arbiter.Renew(f.sample,support,f.Proof(2)).accepted);
    }
    auto busy=f.arbiter.Acquire(f.sample,f.Request(left,HandClaimKind::Sight,rifle,3,gun.id));
    CHECK(!busy.accepted&&busy.reason==HandInteractionReason::HandOccupied&&f.arbiter.Current(left)->token==support);
    auto request=f.Request(left,HandClaimKind::Sight,rifle,3,gun.id);
    auto out=f.arbiter.Transfer(f.sample,support,request);CHECK(out.accepted&&out.claim);
    const auto sight=out.claim->token;
    CHECK(sight.id>support.id&&Releases(out)==1&&Released(out,support,HandInteractionReason::Transferred));
    CHECK(f.arbiter.Current(left)->token==sight&&f.arbiter.Current(right)->token==gun);
    out=f.arbiter.Release(f.sample,support);CHECK(!out.accepted&&Releases(out)==0&&f.arbiter.Current(left)->token==sight);
    out=f.arbiter.Transfer(f.sample,support,request);CHECK(!out.accepted&&Releases(out)==0&&f.arbiter.Current(left)->token==sight);
    auto forged=sight;forged.contact.generation++;
    CHECK(!f.arbiter.Release(f.sample,forged).accepted&&f.arbiter.Current(left)->token==sight);
    out=f.arbiter.Release(f.sample,sight);CHECK(out.accepted&&Releases(out)==1);
    CHECK(!f.arbiter.Release(f.sample,sight).accepted&&f.arbiter.Current(right)->token==gun);
    return 0;
}
int ExplicitSharingAndCrossHandTransfer(){
    Fixture f;auto g=f.Gun();CHECK(g.claim);const auto gun=g.claim->token;
    for(unsigned bad=0;bad<6;++bad){
        auto r=f.Request(left,HandClaimKind::Sight,rifle,2,gun.id);
        if(bad==0)r.prerequisiteClaim=0;
        if(bad==1)++r.prerequisiteClaim;
        if(bad==2)r.kind=HandClaimKind::AmmoObject;
        if(bad==3)r.item.generation=2;
        if(bad==4)r.contact.key=gun.contact;
        if(bad==5)r.kind=HandClaimKind::GunHold;
        const auto out=f.arbiter.Acquire(f.sample,r);CHECK(!out.accepted&&Releases(out)==0&&f.arbiter.Current(right)->token==gun);
    }
    auto r=f.Request(left,HandClaimKind::WeaponSupport,rifle,2);
    CHECK(!f.arbiter.Acquire(f.sample,r).accepted); // Support always declares its gun.
    const auto oldSample=f.sample;
    auto out=f.arbiter.Transfer(f.sample,gun,f.Request(left,HandClaimKind::GunHold,rifle,4));
    CHECK(out.accepted&&out.claim&&Releases(out)==1&&!f.arbiter.Current(right));
    const auto moved=out.claim->token;CHECK(moved.id>gun.id);
    f.Next();f.arbiter.Update(f.sample);
    // Queued releases/transfers with both an old token and old input cannot
    // invalidate the newer destination claim through lifecycle processing.
    CHECK(!f.arbiter.Release(oldSample,gun).accepted&&f.arbiter.Current(left)->token==moved);
    auto stale=f.Request(right,HandClaimKind::GunHold);stale.contact.inputSequence=oldSample.sequence;
    CHECK(!f.arbiter.Transfer(oldSample,moved,stale).accepted&&f.arbiter.Current(left)->token==moved);
    // Different objects are independent; same object cannot be silently stolen.
    out=f.arbiter.Acquire(f.sample,f.Request(right,HandClaimKind::AmmoObject,shell,5));CHECK(out.accepted&&out.claim);
    const auto ammo=out.claim->token;
    out=f.arbiter.Transfer(f.sample,moved,f.Request(right,HandClaimKind::GunHold,rifle,4));
    CHECK(!out.accepted&&f.arbiter.Current(left)->token==moved&&f.arbiter.Current(right)->token==ammo);
    return 0;
}
int DependencyAndPerHandTracking(){
    Fixture f;auto g=f.Gun();CHECK(g.claim);const auto gun=g.claim->token;auto s=f.Support(gun);CHECK(s.claim);
    const auto support=s.claim->token;
    f.sample.tracked[0]=false;
    auto out=f.arbiter.Update(f.sample);
    CHECK(Releases(out)==1&&Released(out,support,HandInteractionReason::TrackingLost)&&f.arbiter.Current(right)->token==gun);
    f.sample.tracked[0]=true;auto r=f.Request(left,HandClaimKind::WeaponSupport,rifle,2,gun.id);
    CHECK(!f.arbiter.Acquire(f.sample,r).accepted); // Duplicate packet cannot recover tracking.
    f.Next();r=f.Request(left,HandClaimKind::WeaponSupport,rifle,2,gun.id);
    out=f.arbiter.Acquire(f.sample,r);CHECK(out.accepted&&out.claim);const auto replacement=out.claim->token;
    f.sample.tracked[1]=false;out=f.arbiter.Update(f.sample);
    CHECK(Releases(out)==2&&Released(out,gun,HandInteractionReason::TrackingLost));
    CHECK(Released(out,replacement,HandInteractionReason::DependencyLost));
    CHECK(!f.arbiter.Current(left)&&!f.arbiter.Current(right));
    Fixture independent;auto hold=independent.Gun();CHECK(hold.claim);
    auto a=independent.arbiter.Acquire(independent.sample,independent.Request(left,HandClaimKind::AmmoObject,shell,5));CHECK(a.claim);
    independent.sample.tracked[1]=false;out=independent.arbiter.Update(independent.sample);
    CHECK(Releases(out)==1&&independent.arbiter.Current(left)->token==a.claim->token);
    Fixture release;g=release.Gun();CHECK(g.claim);s=release.Support(g.claim->token);CHECK(s.claim);
    out=release.arbiter.Release(release.sample,g.claim->token);
    CHECK(out.accepted&&Releases(out)==2&&Released(out,s.claim->token,HandInteractionReason::DependencyLost));
    return 0;
}
int IdentityFocusResetAndFreshIntent(){
    for(unsigned change=0;change<6;++change){
        Fixture f;auto request=f.Request(right,HandClaimKind::GunHold);
        auto out=f.arbiter.Acquire(f.sample,request);CHECK(out.claim);const auto old=out.claim->token;
        const auto oldSample=f.sample;f.Next();
        if(change==0)++f.sample.owner.actor;
        if(change==1)++f.sample.owner.actorGeneration;
        if(change==2)++f.sample.owner.equipGeneration;
        if(change==3)++f.sample.owner.space;
        if(change==4)f.sample.focused=false;
        if(change==5)f.sample.tracked[1]=false;
        out=f.arbiter.Update(f.sample);CHECK(Releases(out)==1&&!f.arbiter.Current(right));
        CHECK(!f.arbiter.Acquire(oldSample,request).accepted&&!f.arbiter.Current(right));
        f.sample.focused=true;f.sample.tracked[1]=true;f.Next();
        request.owner=f.sample.owner;request.contact=f.Proof(1);
        // Even freshly rebound geometry cannot turn a previously consumed
        // held gesture into a new acquisition after lifecycle/tracking loss.
        out=f.arbiter.Acquire(f.sample,request);CHECK(!out.accepted&&out.reason==HandInteractionReason::StaleIntent);
        out=f.arbiter.Acquire(f.sample,f.Request(right,HandClaimKind::GunHold));CHECK(out.claim&&out.claim->token.id>old.id);
        const auto current=out.claim->token;
        CHECK(!f.arbiter.Release(oldSample,old).accepted&&f.arbiter.Current(right)->token==current);
    }
    Fixture f;auto g=f.Gun();CHECK(g.claim);const auto first=g.claim->token;
    auto out=f.arbiter.Reset();CHECK(Releases(out)==1&&Released(out,first,HandInteractionReason::Reset));
    CHECK(!f.arbiter.Acquire(f.sample,f.Request(right,HandClaimKind::GunHold)).accepted);
    f.Next();g=f.Gun();CHECK(g.claim&&g.claim->token.id>first.id);
    const auto second=g.claim->token;f.arbiter.Reset();CHECK(Releases(f.arbiter.Reset())==0);
    f.Next();g=f.Gun();CHECK(g.claim&&g.claim->token.id>second.id);
    return 0;
}
int LeasesRenewalAndContactIdentity(){
    Fixture f;auto request=f.Request(right,HandClaimKind::GunHold);request.contact.deadlineNs=f.sample.nowNs+30*ms;
    auto out=f.arbiter.Acquire(f.sample,request);CHECK(out.claim);const auto gun=out.claim->token;
    auto support=f.Support(gun);CHECK(support.claim);
    auto original=request.contact;
    CHECK(f.arbiter.Renew(f.sample,gun,original).accepted);
    auto extended=original;extended.deadlineNs=f.sample.deadlineNs;
    out=f.arbiter.Renew(f.sample,gun,extended);
    CHECK(!out.accepted&&out.reason==HandInteractionReason::ContactMismatch);
    CHECK(f.arbiter.Current(right)->deadlineNs==original.deadlineNs);
    f.Next();auto proof=f.Proof(1);out=f.arbiter.Renew(f.sample,gun,proof);CHECK(out.accepted&&out.claim->token==gun);
    CHECK(out.claim->deadlineNs==f.sample.deadlineNs);
    auto bad=proof;bad.key.generation++;CHECK(!f.arbiter.Renew(f.sample,gun,bad).accepted&&f.arbiter.Current(right)->token==gun);
    bad=proof;bad.inputSequence--;CHECK(!f.arbiter.Renew(f.sample,gun,bad).accepted);
    bad=proof;bad.deadlineNs++;CHECK(!f.arbiter.Renew(f.sample,gun,bad).accepted);
    // Renewing from the identical original packet cannot rejuvenate its lease.
    f.sample.nowNs=f.sample.deadlineNs;out=f.arbiter.Update(f.sample);
    CHECK(Releases(out)==2&&out.reason==HandInteractionReason::StaleInput);
    Fixture expiry;request=expiry.Request(right,HandClaimKind::GunHold);request.contact.deadlineNs=expiry.sample.nowNs+20*ms;
    auto g=expiry.arbiter.Acquire(expiry.sample,request);CHECK(g.claim);
    auto s=expiry.Support(g.claim->token);CHECK(s.claim);
    expiry.Next(20*ms);out=expiry.arbiter.Update(expiry.sample);
    CHECK(Releases(out)==2&&Released(out,g.claim->token,HandInteractionReason::LeaseExpired));
    CHECK(Released(out,s.claim->token,HandInteractionReason::DependencyLost));
    CHECK(!expiry.arbiter.Renew(expiry.sample,g.claim->token,expiry.Proof(1)).accepted);
    return 0;
}
int MalformedAndImmutableInput(){
    for(unsigned bad=0;bad<10;++bad){
        Fixture f;auto gun=f.Gun();CHECK(gun.claim);const auto token=gun.claim->token;
        auto r=f.Request(left,HandClaimKind::AmmoObject,shell,5);
        if(bad==0)r.hand=static_cast<InteractionHand>(255);
        if(bad==1)r.kind=HandClaimKind::None;
        if(bad==2)r.kind=static_cast<HandClaimKind>(255);
        if(bad==3)r.item.id=0;
        if(bad==4)r.item.generation=0;
        if(bad==5)r.contact.key.id=0;
        if(bad==6)r.contact.key.generation=0;
        if(bad==7)r.contact.eligible=false;
        if(bad==8)++r.owner.space;
        if(bad==9)r.intent=0;
        const auto out=f.arbiter.Acquire(f.sample,r);
        CHECK(!out.accepted&&Releases(out)==0&&f.arbiter.Current(right)->token==token);
    }
    Fixture duplicate;auto gun=duplicate.Gun();CHECK(gun.claim);
    duplicate.sample.deadlineNs++;auto out=duplicate.arbiter.Update(duplicate.sample);
    CHECK(!out.inputValid&&Releases(out)==1&&out.reason==HandInteractionReason::InvalidSample);
    Fixture clock;gun=clock.Gun();CHECK(gun.claim);clock.sample.nowNs++;clock.arbiter.Update(clock.sample);
    clock.sample.nowNs--;out=clock.arbiter.Update(clock.sample);
    CHECK(!out.inputValid&&Releases(out)==1&&out.reason==HandInteractionReason::ClockReversed);
    Fixture sequence;gun=sequence.Gun();CHECK(gun.claim);sequence.Next();sequence.arbiter.Update(sequence.sample);
    sequence.sample.sequence--;out=sequence.arbiter.Update(sequence.sample);
    CHECK(!out.inputValid&&Releases(out)==1&&out.reason==HandInteractionReason::SequenceRollback);
    HandInteraction invalid({0});Fixture source;out=invalid.Update(source.sample);
    CHECK(!out.inputValid&&out.reason==HandInteractionReason::InvalidConfig);
    return 0;
}
int UnavailableIntentCannotReplay(){
    for(unsigned lost=0;lost<2;++lost){
        Fixture f;
        if(lost==0)f.sample.tracked[1]=false;else f.sample.focused=false;
        auto request=f.Request(right,HandClaimKind::GunHold);
        auto out=f.arbiter.Acquire(f.sample,request);CHECK(!out.accepted&&!f.arbiter.Current(right));
        f.sample.tracked[1]=f.sample.focused=true;f.Next();
        request.contact=f.Proof(1); // Geometry recovers; original intent does not.
        out=f.arbiter.Acquire(f.sample,request);
        CHECK(!out.accepted&&out.reason==HandInteractionReason::StaleIntent);
        out=f.arbiter.Acquire(f.sample,f.Request(right,HandClaimKind::GunHold));CHECK(out.accepted&&out.claim);
    }
    Fixture f;const auto old=f.sample;f.Next();f.arbiter.Update(f.sample);
    auto queued=f.Request(right,HandClaimKind::GunHold);queued.intent=1000;
    queued.contact.inputSequence=old.sequence;queued.contact.deadlineNs=old.deadlineNs;
    CHECK(!f.arbiter.Acquire(old,queued).accepted);
    // A stale operation cannot consume the current stream's newer intent state.
    auto out=f.arbiter.Acquire(f.sample,f.Request(right,HandClaimKind::GunHold));
    CHECK(out.accepted&&out.claim);
    return 0;
}
HandContactProof From(const HandInteractionSample& source,std::uint64_t contact){
    return {{contact,1},source.sequence,source.deadlineNs,true};
}
int HistoricalTransferAndExactSource(){
    Fixture f;auto g=f.Gun();CHECK(g.claim);const auto gun=g.claim->token;
    auto held=f.Support(gun);CHECK(held.claim);const auto support=held.claim->token;
    const auto source=f.sample;f.Next();f.arbiter.Update(f.sample);
    auto request=f.Request(left,HandClaimKind::Sight,rifle,3,gun.id);request.contact=From(source,3);
    CHECK(!f.arbiter.Transfer(f.sample,support,request).accepted&&f.arbiter.Current(left)->token==support);
    for(unsigned bad=0;bad<8;++bad){
        auto evidence=source;request=f.Request(left,HandClaimKind::Sight,rifle,3,gun.id);
        if(bad==0)++evidence.owner.space;
        if(bad==1)++evidence.observedNs;
        if(bad==2)--evidence.deadlineNs;
        if(bad==3)evidence.tracked[1]=false;
        if(bad==4)evidence.focused=false;
        if(bad==5)evidence.sequence=f.sample.sequence+1;
        request.contact=From(evidence,3);
        if(bad==6)--request.contact.deadlineNs;
        if(bad==7)++request.contact.inputSequence;
        const auto out=f.arbiter.TransferFrom(f.sample,evidence,support,request);
        CHECK(!out.accepted&&Releases(out)==0&&f.arbiter.Current(left)->token==support);
    }
    request=f.Request(left,HandClaimKind::Sight,rifle,3,gun.id);request.contact=From(source,3);
    auto evidence=source;evidence.nowNs=f.sample.nowNs; // Processing time is not source metadata.
    auto out=f.arbiter.TransferFrom(f.sample,evidence,support,request);
    CHECK(out.accepted&&out.claim&&out.claim->inputSequence==source.sequence);
    CHECK(out.claim->deadlineNs==source.deadlineNs&&Releases(out)==1);
    CHECK(Released(out,support,HandInteractionReason::Transferred));
    const auto sight=out.claim->token;
    CHECK(!f.arbiter.Release(source,support).accepted&&f.arbiter.Current(left)->token==sight);
    return 0;
}
int HistoricalReleaseAndTrackingBarriers(){
    Fixture f;auto g=f.Gun();CHECK(g.claim);auto support=f.Support(g.claim->token);CHECK(support.claim);
    const auto beforeRelease=f.sample;
    f.Next();f.sample.released[0]=true;const auto neutral=f.sample;
    auto out=f.arbiter.Update(f.sample);CHECK(Releases(out)==1&&f.arbiter.Current(right));
    auto request=f.Request(left,HandClaimKind::WeaponSupport,rifle,2,g.claim->token.id);request.contact=From(beforeRelease,2);
    out=f.arbiter.AcquireFrom(f.sample,beforeRelease,request);
    CHECK(!out.accepted&&out.reason==HandInteractionReason::Released&&!f.arbiter.Current(left));
    f.Next();f.sample.released[0]=false;f.arbiter.Update(f.sample);
    request=f.Request(left,HandClaimKind::WeaponSupport,rifle,2,g.claim->token.id);request.contact=From(beforeRelease,2);
    CHECK(!f.arbiter.AcquireFrom(f.sample,beforeRelease,request).accepted);
    // Neutral itself remains valid geometry for a genuinely new current press.
    request=f.Request(left,HandClaimKind::WeaponSupport,rifle,2,g.claim->token.id);request.contact=From(neutral,2);
    out=f.arbiter.AcquireFrom(f.sample,neutral,request);
    CHECK(out.accepted&&out.claim&&out.claim->inputSequence==neutral.sequence);
    const auto active=out.claim->token;const auto beforeLoss=f.sample;
    f.Next();f.sample.tracked[0]=false;const auto lost=f.sample;out=f.arbiter.Update(f.sample);
    CHECK(Releases(out)==1&&Released(out,active,HandInteractionReason::TrackingLost));
    request=f.Request(left,HandClaimKind::Sight,rifle,3,g.claim->token.id);request.contact=From(beforeLoss,3);
    CHECK(!f.arbiter.AcquireFrom(f.sample,beforeLoss,request).accepted);
    f.Next();f.sample.tracked[0]=true;f.arbiter.Update(f.sample);
    request=f.Request(left,HandClaimKind::Sight,rifle,3,g.claim->token.id);request.contact=From(beforeLoss,3);
    CHECK(!f.arbiter.AcquireFrom(f.sample,beforeLoss,request).accepted);
    request=f.Request(left,HandClaimKind::Sight,rifle,3,g.claim->token.id);request.contact=From(lost,3);
    CHECK(!f.arbiter.AcquireFrom(f.sample,lost,request).accepted);
    return 0;
}
int DelayedGeometryWithinOneNeutralRun(){
    for(unsigned lag:{2u,3u})for(unsigned beforeSource:{0u,1u}){
        Fixture f;auto g=f.Gun();CHECK(g.claim);auto support=f.Support(g.claim->token);CHECK(support.claim);
        const auto held=f.sample;
        f.Next();f.sample.released[0]=true;f.arbiter.Update(f.sample);
        for(unsigned n=0;n<beforeSource;++n){f.Next();f.arbiter.Update(f.sample);}
        const auto neutral=f.sample;
        for(unsigned n=1;n<lag;++n){f.Next();f.arbiter.Update(f.sample);}
        f.Next();f.sample.released[0]=false;f.arbiter.Update(f.sample);
        CHECK(f.sample.sequence-neutral.sequence==lag&&!f.arbiter.Current(left));
        auto request=f.Request(left,HandClaimKind::Mechanism,rifle,3,g.claim->token.id);request.contact=From(held,3);
        auto out=f.arbiter.AcquireFrom(f.sample,held,request);
        CHECK(!out.accepted&&out.reason==HandInteractionReason::ContactMismatch&&!f.arbiter.Current(left));
        // Renderer N-2/N-3 contact remains in the same genuine neutral run.
        // Acquisition still uses a new current intent and the ORIGINAL expiry.
        request=f.Request(left,HandClaimKind::Mechanism,rifle,3,g.claim->token.id);request.contact=From(neutral,3);
        out=f.arbiter.AcquireFrom(f.sample,neutral,request);
        CHECK(out.accepted&&out.claim&&out.claim->inputSequence==neutral.sequence);
        CHECK(out.claim->deadlineNs==neutral.deadlineNs&&out.claim->token.prerequisiteClaim==g.claim->token.id);
        CHECK(f.arbiter.Current(right)->token==g.claim->token);
    }
    return 0;
}
int NewReleaseRunRejectsEarlierNeutralGeometry(){
    Fixture f;auto g=f.Gun();CHECK(g.claim);
    f.Next();f.sample.released[0]=true;f.arbiter.Update(f.sample);const auto oldNeutral=f.sample;
    f.Next();f.arbiter.Update(f.sample);
    f.Next();f.sample.released[0]=false;f.arbiter.Update(f.sample);const auto held=f.sample;
    f.Next();f.sample.released[0]=true;f.arbiter.Update(f.sample);const auto newNeutral=f.sample;
    f.Next();f.arbiter.Update(f.sample);
    f.Next();f.sample.released[0]=false;f.arbiter.Update(f.sample);
    // Even an unclaimed held packet ends the previous continuous neutral run.
    for(const auto& source:{oldNeutral,held}){
        auto request=f.Request(left,HandClaimKind::Mechanism,rifle,3,g.claim->token.id);request.contact=From(source,3);
        const auto out=f.arbiter.AcquireFrom(f.sample,source,request);
        CHECK(!out.accepted&&out.reason==HandInteractionReason::ContactMismatch&&!f.arbiter.Current(left));
    }
    auto request=f.Request(left,HandClaimKind::Mechanism,rifle,3,g.claim->token.id);request.contact=From(newNeutral,3);
    const auto out=f.arbiter.AcquireFrom(f.sample,newNeutral,request);
    CHECK(out.accepted&&out.claim&&out.claim->inputSequence==newNeutral.sequence&&out.claim->deadlineNs==newNeutral.deadlineNs);
    return 0;
}
int DelayedNeutralGeometryCannotCrossSafetyLoss(){
    for(unsigned loss=0;loss<5;++loss){
        Fixture f;auto g=f.Gun();CHECK(g.claim);
        f.Next();f.sample.released[0]=true;f.arbiter.Update(f.sample);const auto neutral=f.sample;
        f.Next();f.arbiter.Update(f.sample);
        f.Next();f.sample.released[0]=false;f.arbiter.Update(f.sample);
        auto support=f.Support(g.claim->token);CHECK(support.claim);
        f.Next();
        if(loss==0)f.sample.tracked[0]=false;
        if(loss==1)f.sample.focused=false;
        if(loss==2)++f.sample.owner.equipGeneration;
        if(loss==3)f.sample.tracked[1]=false;
        auto out=f.arbiter.Update(f.sample);
        if(loss==3)CHECK(Released(out,support.claim->token,HandInteractionReason::DependencyLost));
        if(loss==4)CHECK(f.arbiter.Release(f.sample,support.claim->token).accepted);
        CHECK(!f.arbiter.Current(left));
        f.Next();f.sample.tracked={true,true};f.sample.focused=true;f.arbiter.Update(f.sample);
        if(!f.arbiter.Current(right)){g=f.Gun();CHECK(g.claim);}
        const auto gun=f.arbiter.Current(right)->token;
        auto request=f.Request(left,HandClaimKind::Mechanism,rifle,3,gun.id);request.contact=From(neutral,3);
        out=f.arbiter.AcquireFrom(f.sample,neutral,request);
        CHECK(!out.accepted&&!f.arbiter.Current(left)&&f.arbiter.Current(right)->token==gun);
        CHECK(out.reason==(loss==2?HandInteractionReason::WrongOwner:HandInteractionReason::ContactMismatch));
        // A failed intent cannot be rebound to fresh geometry during the held grip.
        request.contact=From(f.sample,3);
        out=f.arbiter.AcquireFrom(f.sample,f.sample,request);
        CHECK(!out.accepted&&out.reason==HandInteractionReason::StaleIntent&&!f.arbiter.Current(left));
    }
    return 0;
}
int DelayedNeutralGeometryCannotStealOccupiedHand(){
    for(const auto kind:{HandClaimKind::WeaponSupport,HandClaimKind::Sight}){
        Fixture f;auto g=f.Gun();CHECK(g.claim);
        f.Next();f.sample.released[0]=true;f.arbiter.Update(f.sample);const auto neutral=f.sample;
        f.Next();f.arbiter.Update(f.sample);
        f.Next();f.sample.released[0]=false;f.arbiter.Update(f.sample);
        const auto occupied=f.arbiter.Acquire(f.sample,f.Request(left,kind,rifle,2,g.claim->token.id));CHECK(occupied.claim);
        auto request=f.Request(left,HandClaimKind::Mechanism,rifle,3,g.claim->token.id);request.contact=From(neutral,3);
        auto out=f.arbiter.AcquireFrom(f.sample,neutral,request);
        CHECK(!out.accepted&&out.reason==HandInteractionReason::HandOccupied&&Releases(out)==0);
        CHECK(f.arbiter.Current(left)->token==occupied.claim->token&&f.arbiter.Current(right)->token==g.claim->token);
        CHECK(f.arbiter.Release(f.sample,occupied.claim->token).accepted);
        request.contact=From(f.sample,3);
        out=f.arbiter.AcquireFrom(f.sample,f.sample,request);
        CHECK(!out.accepted&&out.reason==HandInteractionReason::StaleIntent&&!f.arbiter.Current(left));
    }
    return 0;
}
int DuplicateSafetyLossCannotBecomeNeutralEvidence(){
    for(unsigned loss=0;loss<4;++loss){
        Fixture f;auto g=f.Gun();CHECK(g.claim);auto held=f.Support(g.claim->token);CHECK(held.claim);
        const auto source=f.sample;
        if(loss==0){f.sample.tracked[0]=false;f.arbiter.Update(f.sample);}
        if(loss==1){f.sample.focused=false;f.arbiter.Update(f.sample);}
        if(loss==2)CHECK(f.arbiter.Release(f.sample,held.claim->token).accepted);
        if(loss==3){f.sample.released[0]=true;f.arbiter.Update(f.sample);}
        f.Next();f.sample.tracked[0]=f.sample.focused=true;f.sample.released[0]=false;f.arbiter.Update(f.sample);
        if(!f.arbiter.Current(right)){g=f.Gun();CHECK(g.claim);}
        const auto gun=f.arbiter.Current(right)->token;
        auto request=f.Request(left,HandClaimKind::WeaponSupport,rifle,2,gun.id);request.contact=From(source,2);
        const auto out=f.arbiter.AcquireFrom(f.sample,source,request);
        CHECK(!out.accepted&&!f.arbiter.Current(left)&&f.arbiter.Current(right)->token==gun);
    }
    // Even initially genuine neutral geometry becomes unusable after a later
    // tracking loss at that same sequence. Equal neutral is not a safety bypass.
    Fixture f;auto g=f.Gun();CHECK(g.claim);
    f.Next();f.sample.released[0]=true;f.arbiter.Update(f.sample);const auto neutral=f.sample;
    f.sample.tracked[0]=false;f.arbiter.Update(f.sample);
    f.Next();f.sample.tracked[0]=true;f.sample.released[0]=false;f.arbiter.Update(f.sample);
    auto request=f.Request(left,HandClaimKind::WeaponSupport,rifle,2,g.claim->token.id);request.contact=From(neutral,2);
    CHECK(!f.arbiter.AcquireFrom(f.sample,neutral,request).accepted&&!f.arbiter.Current(left));
    return 0;
}
int HistoricalRenewalIsMonotonic(){
    Fixture f;auto g=f.Gun();CHECK(g.claim);auto supported=f.Support(g.claim->token);CHECK(supported.claim);
    const auto support=supported.claim->token;const auto first=f.sample;
    f.Next();const auto second=f.sample;f.arbiter.Update(f.sample);
    f.Next();f.arbiter.Update(f.sample);
    auto out=f.arbiter.RenewFrom(f.sample,second,support,From(second,2));
    CHECK(out.accepted&&out.claim&&out.claim->inputSequence==second.sequence&&out.claim->deadlineNs==second.deadlineNs);
    CHECK(!f.arbiter.RenewFrom(f.sample,first,support,From(first,2)).accepted);
    CHECK(f.arbiter.Current(left)->deadlineNs==second.deadlineNs);
    out=f.arbiter.RenewFrom(f.sample,second,support,From(second,2));CHECK(out.accepted&&out.claim->token==support);
    auto changed=second;changed.deadlineNs+=ms;changed.observedNs+=ms;
    CHECK(!f.arbiter.RenewFrom(f.sample,changed,support,From(changed,2)).accepted);
    CHECK(f.arbiter.Current(left)->deadlineNs==second.deadlineNs);
    out=f.arbiter.RenewFrom(f.sample,f.sample,support,From(f.sample,2));
    CHECK(out.accepted&&out.claim->deadlineNs==f.sample.deadlineNs&&out.claim->token==support);
    return 0;
}
int MissingExpiredAndWrongOwnerEvidence(){
    Fixture f;auto g=f.Gun();CHECK(g.claim);auto held=f.Support(g.claim->token);CHECK(held.claim);
    const auto support=held.claim->token;const auto first=f.sample;
    f.Next();++f.sample.sequence;f.arbiter.Update(f.sample); // Sequence 2 was never observed.
    auto missing=first;missing.sequence=2;missing.observedNs+=5*ms;missing.deadlineNs+=5*ms;
    auto request=f.Request(left,HandClaimKind::Sight,rifle,3,g.claim->token.id);request.contact=From(missing,3);
    auto out=f.arbiter.TransferFrom(f.sample,missing,support,request);
    CHECK(!out.accepted&&out.reason==HandInteractionReason::MissingEvidence&&f.arbiter.Current(left)->token==support);
    for(unsigned n=0;n<32;++n){f.Next(ms);f.arbiter.Update(f.sample);}
    request=f.Request(left,HandClaimKind::Sight,rifle,3,g.claim->token.id);request.contact=From(first,3);
    out=f.arbiter.TransferFrom(f.sample,first,support,request);
    CHECK(!out.accepted&&out.reason==HandInteractionReason::MissingEvidence&&f.arbiter.Current(left)->token==support);
    // Expired old evidence cannot replace a separately valid current hold.
    f.Next(160*ms);f.arbiter.Update(f.sample);g=f.Gun();CHECK(g.claim);held=f.Support(g.claim->token);CHECK(held.claim);
    request=f.Request(left,HandClaimKind::Sight,rifle,3,g.claim->token.id);request.contact=From(first,3);
    out=f.arbiter.TransferFrom(f.sample,first,held.claim->token,request);
    CHECK(!out.accepted&&Releases(out)==0&&f.arbiter.Current(left)->token==held.claim->token);
    const auto oldOwner=f.sample;f.Next();++f.sample.owner.equipGeneration;f.arbiter.Update(f.sample);
    g=f.Gun();CHECK(g.claim);held=f.Support(g.claim->token);CHECK(held.claim);
    request=f.Request(left,HandClaimKind::Sight,rifle,3,g.claim->token.id);request.contact=From(oldOwner,3);
    out=f.arbiter.TransferFrom(f.sample,oldOwner,held.claim->token,request);
    CHECK(!out.accepted&&out.reason==HandInteractionReason::WrongOwner&&f.arbiter.Current(left)->token==held.claim->token);
    return 0;
}
int FailedIntentCannotLaterWin(){
    Fixture f;auto gun=f.Gun();CHECK(gun.claim);
    auto request=f.Request(left,HandClaimKind::AmmoObject,rifle,5);
    auto out=f.arbiter.Acquire(f.sample,request);CHECK(!out.accepted&&out.reason==HandInteractionReason::ItemOccupied);
    CHECK(f.arbiter.Release(f.sample,gun.claim->token).accepted);
    // Merely resolving the conflict does not replay yesterday's intent.
    out=f.arbiter.Acquire(f.sample,request);CHECK(!out.accepted&&out.reason==HandInteractionReason::StaleIntent);
    request=f.Request(left,HandClaimKind::AmmoObject,rifle,5);
    out=f.arbiter.Acquire(f.sample,request);CHECK(out.accepted&&out.claim);
    return 0;
}
}
static_assert(!std::is_copy_constructible_v<HandInteraction>);
int main(){
    if(FailedAndSuccessfulTransfers()||ExplicitSharingAndCrossHandTransfer()||DependencyAndPerHandTracking()||
       IdentityFocusResetAndFreshIntent()||LeasesRenewalAndContactIdentity()||MalformedAndImmutableInput()||
        UnavailableIntentCannotReplay()||HistoricalTransferAndExactSource()||HistoricalReleaseAndTrackingBarriers()||
        DelayedGeometryWithinOneNeutralRun()||NewReleaseRunRejectsEarlierNeutralGeometry()||
        DelayedNeutralGeometryCannotCrossSafetyLoss()||DelayedNeutralGeometryCannotStealOccupiedHand()||
       DuplicateSafetyLossCannotBecomeNeutralEvidence()||HistoricalRenewalIsMonotonic()||
       MissingExpiredAndWrongOwnerEvidence()||FailedIntentCannotLaterWin())return 1;
    return 0;
}
