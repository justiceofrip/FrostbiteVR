#include "Bc2FeedEquipmentAdapter.h"
#include "Test.h"
#include <cstdio>
#include <cstring>
using namespace fvr::bc2;
using namespace fvr::interaction;
namespace {
constexpr std::int64_t Ms=1000000;
template<std::size_t N,std::size_t M>void Text(std::array<char,N>& out,const char(&value)[M]){
    static_assert(M<=N);std::memcpy(out.data(),value,M);
}
fvr::math::Matrix4 Pose(float x=0){fvr::math::Matrix4 m{};
    for(unsigned n=0;n<4;++n)m.values[n][n]=1;m.values[3][0]=x;return m;}
Bc2AmmoReserveLease Replacement(const FeedEquipmentNativeView& view,std::int64_t now){
    Bc2AmmoReserveLease result;result.identity=view.identity;
    result.identity.owner.actorGeneration++;result.identity.owner.soldier+=0x100;
    result.identity.owner.weak+=0x100;result.identity.serverSoldier+=0x100;
    result.sequence=view.sequence+1;result.observedNs=now;result.deadlineNs=now+100*Ms;
    result.loaded=view.counts.loaded;result.reserve=view.counts.reserve;result.capacity=view.counts.capacity;
    result.verified=result.allThreeIdle=true;return result;
}
FeedEquipmentActorDrain Drain(const ReloadHoldIdentity& previous,std::int64_t now,std::uint64_t event=1){
    return {previous,event,event,now,now+90*Ms,true};
}
struct Fixture {
    FeedEquipmentAdapter adapter;
    FeedEquipmentNativeView view{};
    FeedEquipmentSubmission submission{};
    std::optional<FeedAmmunition> ledger;
    std::int64_t now=1000*Ms;
    bool Setup(FeedAmmunitionEffect effect=FeedAmmunitionEffect::RefillLoaded,
               FeedPhysicalOperation operation=FeedPhysicalOperation::CloseCover){
        view.identity={{0x10000,0x20000,0x30000,0x40000,1,2,3},{0x50000,0x60000,0x70000},0x80000,0x90000,0xa0000};
        view.binding={view.identity.owner,{{0x20000,1,0x40000,19},2,3},0xb0000,0xc0000,0xd0000,0xe0000,now,now+100*Ms};
        auto& config=view.config;config.weaponData=0xd0000;config.firingData=0xf0000;
        config.primaryFire=0x100000;config.ammoAddress=config.primaryFire+0x170;
        Text(config.assetName,"M60_sp");
        Text(config.assetPath,"Objects/Weapons/Handheld/BU_lmg_M60/SP_lmg_M60");
        config.fireLogicType=2;config.reloadType=1;config.fireInputAction=8;config.reloadInputAction=29;
        config.baseCapacity=100;config.numberOfMagazines=2;config.reloadTime=7.8f;config.reloadThreshold=.7f;
        view.counts={20,130,100};view.observedNs=now;view.deadlineNs=now+100*Ms;
        if(!adapter.Publish(view,now))return false;
        const auto source=adapter.Read(view.identity.owner,now);if(!source)return false;
        if(!adapter.PublishGate({source->binding.context.resource,10,1,false,true},now))return false;
        submission.source=*source;
        auto& g=submission.geometry;auto& p=g.profile;
        p.family=FeedFamily::Belt;p.id=11;p.revision=2;p.ammunitionKind=3;p.maxAgeNs=100*Ms;
        p.cover=p.container=true;p.contacts[0]={10,.03f,.1f};p.contacts[2]={12,.03f,.1f};p.contacts[3]={13,.03f,.1f};
        g.rigFingerprint=FeedEquipmentM60RigFingerprint;g.inputSequence=50;g.observedNs=now;g.deadlineNs=now+90*Ms;g.selectedRigVerified=true;
        auto& motion=g.motion;motion.operation=operation;motion.kind=FeedMotionKind::Hinge;motion.measured=true;
        motion.weaponFromMotion=Pose();motion.motionFromContact=Pose(.2f);motion.travel=-1.5f;
        motion.completeFraction=.95f;motion.releaseFraction=.8f;motion.pathToleranceMeters=.005f;
        motion.orientationToleranceRadians=.1f;motion.maxStepMeters=.03f;motion.maxStepRadians=.3f;
        motion.endpointDwellNs=20*Ms;motion.maximumGestureNs=4000*Ms;motion.maxInputGapNs=50*Ms;
        if(operation==FeedPhysicalOperation::AttachContainer){
            motion.kind=FeedMotionKind::FreePlacement;motion.travel=motion.completeFraction=motion.releaseFraction=0;
            motion.placement={Pose(.3f),Pose(),Pose()};
        }
        const auto owner=FeedEquipmentOwner(view.identity.owner);const FeedKey mechanism{30,1},binding{40,1};
        const FeedSupplyKey supply{{60,1},{61,1}};
        FeedAmmunitionSnapshot before{owner,mechanism,10,source->sequence,source->observedNs,source->deadlineNs,source->counts,supply,true};
        ledger.emplace(p,owner,mechanism,supply,10);if(!ledger->Bind(before,now))return false;
        const bool attach=operation==FeedPhysicalOperation::AttachContainer;
        FeedPhysicalCandidate candidate{owner,mechanism,attach?supply:FeedSupplyKey{},supply,p.id,p.revision,before.sequence,
            operation,attach?FeedPart::Container:FeedPart::Cover,p.contacts[attach?2:0].role};
        FeedReloadRequest request{1,10,binding,{1,g.inputSequence,now+10,candidate},now+20};
        FeedAmmunitionAuthority authority{{41,1},binding,owner,mechanism,10,p.id,p.revision,
            candidate.operation,effect,effect==FeedAmmunitionEffect::PhysicalOnly?FeedSupplyKey{}:supply,
            effect==FeedAmmunitionEffect::PhysicalOnly?0:80,true};
        const auto command=ledger->Dispatch(request,authority,before,now+20);if(!command)return false;
        submission.command=*command;now+=30;return true;
    }
    FeedEquipmentNativeCall Call(bool applied=true){return {submission.command,view.identity,100,now,now+10,
        applied,applied&&submission.command.mutationRequired,true,true};}
    AmmoResourceOwnUpdate Row(unsigned branch,const FeedEquipmentNativeCall& call){
        const auto counts=call.applied?call.command.after:call.command.before.counts;
        AmmoResourceOwnUpdate row;row.beforeOwner=row.afterOwner=call.identity.owner;row.firing=call.identity.firing[branch];
        row.serverPlayer=call.identity.serverPlayer;row.serverSoldier=call.identity.serverSoldier;row.serverItem=call.identity.serverItem;
        row.invocation=call.invocation+(branch==2?0:branch+1);row.beginNs=call.beganNs-1;
        row.endNs=call.completedNs+10+branch;row.branch=branch;row.depth=1;
        row.loadedBefore=call.command.before.counts.loaded;row.reserveBefore=call.command.before.counts.reserve;
        row.loadedAfter=counts.loaded;row.reserveAfter=counts.reserve;row.current=row.next=2;
        row.finished=row.identityRetained=row.neutralContext=true;return row;
    }
    bool Finish(const FeedEquipmentNativeCall& call){
        if(!adapter.Call(call))return false;
        if(adapter.Observe(Row(2,call),call.completedNs+20)||adapter.Observe(Row(0,call),call.completedNs+20))return false;
        return adapter.Observe(Row(1,call),call.completedNs+20);
    }
};
int RefillJoinsExactFeedCommandAndThreeUpdates(){
    Fixture f;CHECK(f.Setup());CHECK(FeedEquipmentSubmissionValid(f.submission,f.now));
    CHECK(f.adapter.Submit(f.submission,f.now));CHECK(!f.adapter.Submit(f.submission,f.now));
    const auto call=f.Call();CHECK(f.Finish(call));
    const auto receipt=f.adapter.Receipt(10,1);CHECK(receipt&&receipt->command==f.submission.command);
    CHECK(receipt->post.counts.loaded==100&&receipt->post.counts.reserve==50);
    CHECK(receipt->post.sequence>f.submission.command.before.sequence);
    CHECK(receipt->post.observedNs==call.completedNs+12);CHECK(receipt->authorityInvocation==100);
    CHECK(f.ledger->Complete(*receipt,call.completedNs+20));
    CHECK(!f.adapter.Submit(f.submission,call.completedNs+20));CHECK(!f.adapter.Receipt(11,1));
    CHECK(!f.adapter.Observe(f.Row(2,call),call.completedNs+30));return 0;
}
int PhysicalOnlyRequiresRealUnchangedUpdates(){
    Fixture f;CHECK(f.Setup(FeedAmmunitionEffect::PhysicalOnly));CHECK(f.adapter.Submit(f.submission,f.now));
    const auto call=f.Call();CHECK(!call.nativeEntered);CHECK(f.adapter.Call(call));
    CHECK(!f.adapter.Receipt(10,1));CHECK(!f.adapter.Observe(f.Row(0,call),call.completedNs+20));
    CHECK(!f.adapter.Observe(f.Row(1,call),call.completedNs+20));CHECK(!f.adapter.Receipt(10,1));
    CHECK(f.adapter.Observe(f.Row(2,call),call.completedNs+20));
    const auto receipt=f.adapter.Receipt(10,1);CHECK(receipt&&receipt->post.counts==f.submission.command.before.counts);
    CHECK(f.ledger->Complete(*receipt,call.completedNs+20));return 0;
}
int AttachmentRefillKeepsTheExactPartialSupply(){
    Fixture f;CHECK(f.Setup(FeedAmmunitionEffect::RefillLoaded,FeedPhysicalOperation::AttachContainer));
    const auto& command=f.submission.command;
    CHECK(command.before.counts.loaded==20&&command.authority.rounds==80);
    CHECK(command.loadedSupplyAfter==command.before.loadedSupply);
    CHECK(command.request.completion.candidate.supply==command.before.loadedSupply);
    CHECK(f.adapter.Submit(f.submission,f.now));const auto call=f.Call();CHECK(f.Finish(call));
    const auto receipt=f.adapter.Receipt(10,1);CHECK(receipt&&receipt->command==command);
    CHECK(receipt->post.loadedSupply==command.before.loadedSupply&&receipt->post.counts.loaded==100);
    CHECK(f.ledger->Complete(*receipt,call.completedNs+20));return 0;
}
int UnsupportedAndWrongProvenanceStayUnavailable(){
    Fixture f;CHECK(f.Setup());const auto original=f.submission;
    auto bad=original;bad.geometry.selectedRigVerified=false;CHECK(!f.adapter.Submit(bad,f.now));
    bad=original;bad.geometry.rigFingerprint^=1;CHECK(!f.adapter.Submit(bad,f.now));
    bad=original;bad.geometry.inputSequence++;CHECK(!f.adapter.Submit(bad,f.now));
    bad=original;bad.geometry.motion.measured=false;CHECK(!f.adapter.Submit(bad,f.now));
    bad=original;bad.geometry.deadlineNs=original.command.request.requestedNs;CHECK(!f.adapter.Submit(bad,f.now));
    bad=original;bad.command.authority.effect=FeedAmmunitionEffect::LoadSupply;CHECK(!f.adapter.Submit(bad,f.now));
    bad=original;bad.command.authority.operation=FeedPhysicalOperation::CycleAction;CHECK(!f.adapter.Submit(bad,f.now));
    bad=original;bad.command.authority.rounds--;CHECK(!f.adapter.Submit(bad,f.now));
    bad=original;bad.command.after.reserve--;CHECK(!f.adapter.Submit(bad,f.now));
    bad=original;bad.command.before.sequence++;CHECK(!f.adapter.Submit(bad,f.now));
    bad=original;bad.source.binding.context.resource.weaponGeneration++;CHECK(!f.adapter.Submit(bad,f.now));
    bad=original;bad.source.config.baseCapacity=99;CHECK(!f.adapter.Submit(bad,f.now));
    bad=original;Text(bad.source.config.assetPath,"scoped variant");CHECK(!f.adapter.Submit(bad,f.now));
    CHECK(f.adapter.Submit(original,f.now));return 0;
}
int SameCountsCannotReplaceOwnServerCallback(){
    Fixture f;CHECK(f.Setup());CHECK(f.adapter.Submit(f.submission,f.now));const auto call=f.Call();CHECK(f.adapter.Call(call));
    auto sibling=f.Row(2,call);sibling.invocation+=10;
    CHECK(!f.adapter.Observe(sibling,call.completedNs+20));
    CHECK(!f.adapter.Observe(f.Row(0,call),call.completedNs+20));
    CHECK(!f.adapter.Observe(f.Row(1,call),call.completedNs+20));CHECK(!f.adapter.Receipt(10,1));
    CHECK(f.adapter.Observe(f.Row(2,call),call.completedNs+20));return 0;
}
int WrongOwnerAndFailedCallsRetainUncertainty(){
    Fixture f;CHECK(f.Setup());CHECK(f.adapter.Submit(f.submission,f.now));const auto call=f.Call();CHECK(f.adapter.Call(call));
    auto wrong=f.Row(2,call);wrong.afterOwner.weapon+=4;
    CHECK(!f.adapter.Observe(wrong,call.completedNs+20));CHECK(f.adapter.Uncertain()&&f.adapter.Pending());
    CHECK(!f.adapter.Observe(f.Row(2,call),call.completedNs+20));CHECK(!f.adapter.Receipt(10,1));
    CHECK(!f.adapter.Submit(f.submission,f.now+200*Ms));
    Fixture failed;CHECK(failed.Setup());CHECK(failed.adapter.Submit(failed.submission,failed.now));
    auto failure=failed.Call();failure.exact=false;CHECK(!failed.adapter.Call(failure));
    CHECK(failed.adapter.Invoked()&&failed.adapter.Uncertain()&&failed.adapter.Pending());
    CHECK(!failed.adapter.Call(failed.Call()));CHECK(!failed.adapter.Receipt(10,1));return 0;
}
int ExpiryRequiresObservedNoEntryRejection(){
    Fixture f;CHECK(f.Setup());CHECK(f.adapter.Submit(f.submission,f.now));
    f.now=f.submission.command.before.deadlineNs+5*Ms;
    CHECK(f.adapter.Pending()&&!f.adapter.Receipt(10,1));const auto call=f.Call(false);CHECK(f.Finish(call));
    const auto receipt=f.adapter.Receipt(10,1);CHECK(receipt&&receipt->acknowledgement.status==FeedReloadAcknowledgement::Rejected);
    CHECK(receipt->noTransferEntry);
    CHECK(receipt->post.counts==f.submission.command.before.counts);CHECK(f.ledger->Complete(*receipt,call.completedNs+20));
    Fixture entered;CHECK(entered.Setup());CHECK(entered.adapter.Submit(entered.submission,entered.now));
    auto impossible=entered.Call(false);impossible.nativeEntered=true;CHECK(!entered.adapter.Call(impossible));
    CHECK(entered.adapter.Uncertain()&&entered.adapter.Pending());return 0;
}
int PartialReplicationAndHistoricalReceipts(){
    Fixture f;CHECK(f.Setup());CHECK(f.adapter.Submit(f.submission,f.now));const auto call=f.Call();CHECK(f.adapter.Call(call));
    CHECK(!f.adapter.Observe(f.Row(2,call),call.completedNs+20));
    auto old=f.Row(0,call);old.loadedAfter=call.command.before.counts.loaded;old.reserveAfter=call.command.before.counts.reserve;
    CHECK(!f.adapter.Observe(old,call.completedNs+20));CHECK(!f.adapter.Observe(f.Row(1,call),call.completedNs+20));
    CHECK(!f.adapter.Receipt(10,1));auto fresh=f.Row(0,call);fresh.endNs+=100;fresh.invocation+=10;
    CHECK(f.adapter.Observe(fresh,fresh.endNs+1));const auto receipt=f.adapter.Receipt(10,1);CHECK(receipt);
    f.now+=1000*Ms;CHECK(!f.adapter.Read(f.view.identity.owner,f.now));CHECK(f.adapter.Receipt(10,1)==receipt);
    CHECK(f.ledger->Complete(*receipt,f.now));return 0;
}
int ReadAtUsesActualEarlierCoherentView(){
    Fixture f;CHECK(f.Setup());const auto original=f.submission.source;
    auto later=original;later.observedNs+=20*Ms;later.binding.observedNs=later.observedNs;
    later.deadlineNs=later.binding.deadlineNs=later.observedNs+100*Ms;
    CHECK(f.adapter.Publish(later,later.observedNs));
    const auto historical=f.adapter.ReadAt(original.identity.owner,original.observedNs+5*Ms,later.observedNs+Ms);
    CHECK(historical&&*historical==original);
    CHECK(!f.adapter.ReadAt(original.identity.owner,original.observedNs-1,later.observedNs+Ms));
    CHECK(!f.adapter.ReadAt(original.identity.owner,original.observedNs+5*Ms,original.deadlineNs));
    auto replacement=later;replacement.binding.context.resource.weaponGeneration++;
    CHECK(f.adapter.Publish(replacement,later.observedNs));
    CHECK(!f.adapter.ReadAt(original.identity.owner,original.observedNs+5*Ms,later.observedNs+Ms));
    auto backdated=later;backdated.observedNs=original.observedNs;
    CHECK(!f.adapter.Publish(backdated,later.observedNs));return 0;
}
int NoEntryRejectionRequiresLaterOwnCallbacks(){
    Fixture f;CHECK(f.Setup());using Result=FeedEquipmentSubmitResult;
    CHECK(f.adapter.SubmitResult(f.submission,f.now,true)==Result::NoEntryRetained);
    CHECK(!f.adapter.Pending()&&!f.adapter.Invoked()&&!f.adapter.Receipt(10,1));
    auto observed=f.Call(false);observed.beganNs+=100;observed.completedNs+=100;
    auto old=f.Row(2,observed);old.beginNs=f.now-1;
    CHECK(!f.adapter.Observe(old,observed.completedNs+20));
    CHECK(!f.adapter.Observe(f.Row(0,observed),observed.completedNs+20));
    CHECK(!f.adapter.Observe(f.Row(1,observed),observed.completedNs+20));
    CHECK(!f.adapter.Receipt(10,1));
    CHECK(f.adapter.Observe(f.Row(2,observed),observed.completedNs+20));
    const auto receipt=f.adapter.Receipt(10,1);
    CHECK(receipt&&receipt->noTransferEntry&&receipt->authorityInvocation==observed.invocation);
    CHECK(receipt->beganNs==observed.beganNs-1&&receipt->command==f.submission.command);
    CHECK(receipt->acknowledgement.status==FeedReloadAcknowledgement::Rejected);
    CHECK(f.ledger->Complete(*receipt,observed.completedNs+20));
    CHECK(!f.adapter.Pending()&&!f.adapter.Invoked());return 0;
}
int RefusedUncertainCountsNeverReleaseDebt(){
    Fixture f;CHECK(f.Setup());using Result=FeedEquipmentSubmitResult;
    CHECK(f.adapter.SubmitResult(f.submission,f.now,true)==Result::NoEntryRetained);
    auto observed=f.Call(false);observed.beganNs+=100;observed.completedNs+=100;
    auto changed=f.Row(2,observed);changed.loadedAfter++;
    CHECK(!f.adapter.Observe(changed,observed.completedNs+20));
    for(unsigned n=0;n<3;++n)CHECK(!f.adapter.Observe(f.Row(n,observed),observed.completedNs+20));
    CHECK(!f.adapter.Receipt(10,1));auto gate=*f.adapter.ReadGate(f.view.binding.context.resource);
    gate.revision++;gate.blocksFire=false;gate.closedVerified=true;CHECK(!f.adapter.PublishGate(gate,f.now));
    CHECK(f.adapter.SubmitResult(f.submission,f.now,true)==Result::NoEntryRetained);
    CHECK(!f.adapter.Pending()&&!f.adapter.Invoked());return 0;
}
int BoundedRefusalPressureRetainsFrontendRetry(){
    Fixture f;CHECK(f.Setup());using Result=FeedEquipmentSubmitResult;auto command=f.submission;
    for(unsigned n=1;n<=16;++n){command.command.request.id=n;
        CHECK(f.adapter.SubmitResult(command,f.now,true)==Result::NoEntryRetained);}
    command.command.request.id=17;
    CHECK(f.adapter.SubmitResult(command,f.now,true)==Result::RetentionBusy);
    auto observed=f.Call(false);observed.beganNs+=100;observed.completedNs+=100;
    CHECK(!f.adapter.Observe(f.Row(0,observed),observed.completedNs+20));
    CHECK(!f.adapter.Observe(f.Row(1,observed),observed.completedNs+20));
    CHECK(f.adapter.Observe(f.Row(2,observed),observed.completedNs+20));
    CHECK(f.adapter.Receipt(10,1)&&f.adapter.Receipt(10,16));
    CHECK(f.adapter.SubmitResult(command,observed.completedNs+20,true)==Result::NoEntryRetained);
    CHECK(!f.adapter.Pending()&&!f.adapter.Invoked()&&!f.adapter.Receipt(10,17));
    CHECK(f.adapter.Receipt(10,1)&&f.adapter.Receipt(10,16));return 0;
}
int GatePersistsUntilVerifiedCloseAndResourceRetirement(){
    Fixture f;CHECK(f.Setup(FeedAmmunitionEffect::PhysicalOnly));const auto resource=f.view.binding.context.resource;
    const auto initial=f.adapter.ReadGate(resource);CHECK(initial&&!initial->blocksFire);
    CHECK(!f.adapter.PublishGate(*initial,f.now));
    CHECK(f.adapter.Submit(f.submission,f.now));auto close=*initial;close.revision++;
    CHECK(f.adapter.ReadGate(resource)->blocksFire);CHECK(!f.adapter.PublishGate(close,f.now));
    const auto call=f.Call();CHECK(f.Finish(call));CHECK(f.adapter.PublishGate(close,call.completedNs+20));
    auto next=close;next.lifetime++;next.revision++;next.blocksFire=true;next.closedVerified=false;
    CHECK(f.adapter.PublishGate(next,call.completedNs+20));
    CHECK(!f.adapter.Read(f.view.identity.owner,f.now+1000*Ms));CHECK(f.adapter.ReadGate(resource)->blocksFire);
    auto different=resource;different.weaponGeneration++;CHECK(!f.adapter.ReadGate(different));
    different=resource;different.actorGeneration++;CHECK(!f.adapter.ReadGate(different));
    auto changedContext=f.submission.source;changedContext.identity.owner.equipGeneration++;
    changedContext.identity.owner.space++;changedContext.binding.owner=changedContext.identity.owner;
    changedContext.binding.context.equipGeneration++;changedContext.binding.context.space++;
    CHECK(f.adapter.Publish(changedContext,f.now));CHECK(f.adapter.ReadGate(resource)->blocksFire);
    auto falseClose=next;falseClose.revision++;falseClose.blocksFire=false;falseClose.closedVerified=true;
    CHECK(!f.adapter.PublishGate(falseClose,f.now));
    auto replacement=Replacement(f.view,f.now);const auto drain=Drain(f.view.identity,f.now);
    replacement.identity=f.view.identity;CHECK(!f.adapter.RetireForReplacementActor(replacement,f.now,drain));
    replacement=Replacement(f.view,f.now);CHECK(f.adapter.RetireForReplacementActor(replacement,f.now,drain));
    CHECK(!f.adapter.ReadGate(resource));const auto retired=f.adapter.Retirement(0);
    CHECK(retired&&FeedEquipmentActorRetirementValid(*retired)&&retired->retiredItems==1);return 0;
}
int AdmittedCommandCannotBecomeRefusedNoEntry(){
    Fixture f;CHECK(f.Setup());CHECK(f.adapter.Submit(f.submission,f.now));
    CHECK(f.adapter.SubmitResult(f.submission,f.now,true)==FeedEquipmentSubmitResult::Invalid);
    const auto call=f.Call();CHECK(f.Finish(call));const auto receipt=f.adapter.Receipt(10,1);
    CHECK(receipt&&!receipt->noTransferEntry&&receipt->acknowledgement.status==FeedReloadAcknowledgement::Applied);return 0;
}
int IssuedNoEntrySurvivesExpiredObservationRollover(){
    Fixture f;CHECK(f.Setup());using Result=FeedEquipmentSubmitResult;auto retained=f.submission;
    for(unsigned n=1;n<=16;++n){retained.command.request.id=n;
        CHECK(f.adapter.SubmitResult(retained,f.now,true)==Result::NoEntryRetained);}
    retained.command.request.id=17;
    CHECK(f.adapter.SubmitResult(retained,f.now,true)==Result::RetentionBusy);
    const auto original=retained.source;
    for(unsigned n=1;n<=80;++n){auto fresh=f.view;f.now=original.deadlineNs+n*Ms;
        fresh.observedNs=fresh.binding.observedNs=f.now;
        fresh.deadlineNs=fresh.binding.deadlineNs=f.now+100*Ms;
        CHECK(f.adapter.Publish(fresh,f.now));}
    CHECK(!f.adapter.ReadAt(original.identity.owner,original.observedNs,f.now));
    CHECK(retained.source==original&&retained.source.deadlineNs==original.deadlineNs);
    CHECK(f.adapter.SubmitResult(retained,f.now)==Result::Invalid);
    CHECK(f.adapter.SubmitResult(retained,f.now,true)==Result::RetentionBusy);
    auto observed=f.Call(false);observed.beganNs+=100;observed.completedNs+=100;
    CHECK(!f.adapter.Observe(f.Row(0,observed),observed.completedNs+20));
    CHECK(!f.adapter.Observe(f.Row(1,observed),observed.completedNs+20));
    CHECK(f.adapter.Observe(f.Row(2,observed),observed.completedNs+20));
    f.now=observed.completedNs+20;
    CHECK(f.adapter.SubmitResult(retained,f.now,true)==Result::NoEntryRetained);
    CHECK(!f.adapter.Pending()&&!f.adapter.Invoked()&&!f.adapter.Receipt(10,17));
    CHECK(!f.adapter.Call(observed)); // No transfer call is admitted by retention.
    observed=f.Call(false);observed.beganNs+=100;observed.completedNs+=100;observed.invocation+=10;
    CHECK(!f.adapter.Observe(f.Row(0,observed),observed.completedNs+20));
    CHECK(!f.adapter.Observe(f.Row(2,observed),observed.completedNs+20));
    CHECK(!f.adapter.Receipt(10,17));
    CHECK(f.adapter.Observe(f.Row(1,observed),observed.completedNs+20));
    const auto receipt=f.adapter.Receipt(10,17);
    CHECK(receipt&&receipt->command==retained.command&&receipt->noTransferEntry);
    CHECK(receipt->acknowledgement.status==FeedReloadAcknowledgement::Rejected);
    CHECK(receipt->post.counts==original.counts&&receipt->authorityInvocation==observed.invocation);
    CHECK(!f.adapter.Pending()&&!f.adapter.Invoked());return 0;
}
int IssuedProofRejectsForgerySubstitutionAndNewAdapter(){
    Fixture f,other;CHECK(f.Setup());CHECK(other.Setup());using Result=FeedEquipmentSubmitResult;
    auto bad=f.submission;bad.source.issuedSource.reset();
    CHECK(f.adapter.SubmitResult(bad,f.now,true)==Result::Invalid);
    bad=f.submission;bad.source.issuedSource=other.submission.source.issuedSource;
    CHECK(f.adapter.SubmitResult(bad,f.now,true)==Result::Invalid);
    CHECK(other.adapter.SubmitResult(f.submission,other.now,true)==Result::Invalid);
    bad=f.submission;bad.source.sequence++;bad.command.before.sequence++;
    bad.command.request.completion.candidate.snapshotSequence++;
    CHECK(FeedEquipmentSubmissionValid(bad,f.now));
    CHECK(f.adapter.SubmitResult(bad,f.now,true)==Result::Invalid);
    CHECK(f.adapter.Publish(f.view,f.now));
    bad=f.submission;bad.source.issuedSource=f.adapter.Read(f.view.identity.owner,f.now)->issuedSource;
    CHECK(f.adapter.SubmitResult(bad,f.now,true)==Result::Invalid);
    std::optional<Fixture> sameStorage{std::in_place};CHECK(sameStorage->Setup());
    const auto old=sameStorage->submission;sameStorage.emplace();CHECK(sameStorage->Setup());
    CHECK(sameStorage->adapter.SubmitResult(old,sameStorage->now,true)==Result::Invalid);
    CHECK(f.adapter.SubmitResult(f.submission,f.now,true)==Result::NoEntryRetained);return 0;
}
int FeedRetirementRequiresDrainedExactNativeReplacement(){
    Fixture f;CHECK(f.Setup());const auto resource=f.view.binding.context.resource;
    const auto replacement=Replacement(f.view,f.now);const auto drain=Drain(f.view.identity,f.now);
    auto badDrain=drain;badDrain.callbacksDrained=false;
    CHECK(!f.adapter.RetireForReplacementActor(replacement,f.now,badDrain));
    badDrain=drain;badDrain.expected.serverPlayer+=4;
    CHECK(!f.adapter.RetireForReplacementActor(replacement,f.now,badDrain));
    badDrain=drain;badDrain.callbackRevision=0;
    CHECK(!f.adapter.RetireForReplacementActor(replacement,f.now,badDrain));
    badDrain=drain;badDrain.lifetimeEndEpoch=0;
    CHECK(!f.adapter.RetireForReplacementActor(replacement,f.now,badDrain));
    auto same=replacement;same.identity=f.view.identity;same.identity.owner.equipGeneration++;
    same.identity.owner.space++;same.identity.owner.weapon+=4;
    CHECK(!f.adapter.ActorIdentityForReplacement(same.identity.owner));
    CHECK(!f.adapter.RetireForReplacementActor(same,f.now,drain));
    same=replacement;same.identity.serverSoldier=f.view.identity.serverSoldier;
    CHECK(!f.adapter.RetireForReplacementActor(same,f.now,drain));
    CHECK(!f.adapter.RetireForReplacementActor(replacement,replacement.deadlineNs,drain));
    CHECK(f.adapter.ReadGate(resource)&&!f.adapter.Retirement(0));
    CHECK(f.adapter.ActorIdentityForReplacement(replacement.identity.owner)==f.view.identity);
    CHECK(f.adapter.RetireForReplacementActor(replacement,f.now,drain));
    CHECK(!f.adapter.RetireForReplacementActor(replacement,f.now,drain));
    CHECK(!f.adapter.ReadGate(resource)&&!f.adapter.Read(f.view.identity.owner,f.now));
    const auto proof=f.adapter.Retirement(0);CHECK(proof&&proof->event==1&&FeedEquipmentActorRetirementValid(*proof));
    CHECK(proof->previous==f.view.identity&&proof->replacement==replacement.identity);
    CHECK(!proof->pending&&!proof->nativeDispatched&&!proof->ambiguous&&!f.adapter.Retirement(1));
    CHECK(f.adapter.SubmitResult(f.submission,f.now,true)==FeedEquipmentSubmitResult::Invalid);return 0;
}
int FeedRetirementPreservesReceiptsAndUnrelatedActorDebt(){
    Fixture f;CHECK(f.Setup(FeedAmmunitionEffect::PhysicalOnly));
    CHECK(f.adapter.Submit(f.submission,f.now));CHECK(f.Finish(f.Call()));
    const auto historical=f.adapter.Receipt(10,1);CHECK(historical);
    f.now+=30;
    f.submission.command.request.id=2;CHECK(f.adapter.Submit(f.submission,f.now));
    auto uncertain=f.Call();uncertain.exact=false;CHECK(!f.adapter.Call(uncertain));
    f.now=uncertain.completedNs+20;
    const auto replacement=Replacement(f.view,f.now);auto other=f.view;other.identity=replacement.identity;
    other.binding.owner=other.identity.owner;other.binding.context.resource.actor=other.identity.owner.soldier;
    other.binding.context.resource.actorGeneration=other.identity.owner.actorGeneration;
    CHECK(f.adapter.Publish(other,f.now));
    CHECK(f.adapter.PublishGate({other.binding.context.resource,11,1,true,false},f.now));
    CHECK(f.adapter.RetireForReplacementActor(replacement,f.now,Drain(f.view.identity,f.now)));
    const auto retired=f.adapter.Retirement(0);CHECK(retired&&retired->pending==f.submission.command&&retired->ambiguous);
    CHECK(retired->retiredItems==1&&!retired->nativeDispatched);
    CHECK(f.adapter.Receipt(10,1)==historical&&!f.adapter.Receipt(10,2));
    CHECK(!f.adapter.Pending()&&!f.adapter.Invoked()&&!f.adapter.ReadGate(f.view.binding.context.resource));
    CHECK(f.adapter.ReadGate(other.binding.context.resource)->blocksFire);
    CHECK(f.adapter.Read(other.identity.owner,f.now));
    Fixture native;CHECK(native.Setup());CHECK(native.adapter.Submit(native.submission,native.now));
    auto entered=native.Call();entered.exact=false;CHECK(!native.adapter.Call(entered));
    native.now=entered.completedNs+20;
    CHECK(native.adapter.RetireForReplacementActor(Replacement(native.view,native.now),native.now,Drain(native.view.identity,native.now)));
    const auto unknown=native.adapter.Retirement(0);
    CHECK(unknown&&unknown->nativeDispatched&&unknown->ambiguous&&unknown->pending==native.submission.command);
    CHECK(!native.adapter.Receipt(10,1));return 0;
}
int FeedOnlyRespawnsReclaimBoundedSlotsAndKeepAudit(){
    Fixture f;CHECK(f.Setup());auto current=f.view;
    for(std::uint64_t event=1;event<=24;++event){
        f.now+=100;const auto replacement=Replacement(current,f.now);
        CHECK(f.adapter.RetireForReplacementActor(replacement,f.now,Drain(current.identity,f.now,event)));
        CHECK(!f.adapter.ReadGate(current.binding.context.resource));
        const auto proof=f.adapter.Retirement(event-1);
        CHECK(proof&&proof->event==event&&proof->retiredItems==1&&FeedEquipmentActorRetirementValid(*proof));
        CHECK(proof->previous==current.identity&&proof->replacement==replacement.identity);
        current.identity=replacement.identity;current.binding.owner=current.identity.owner;
        current.binding.context.resource.actor=current.identity.owner.soldier;
        current.binding.context.resource.actorGeneration=current.identity.owner.actorGeneration;
        current.observedNs=current.binding.observedNs=f.now;
        current.deadlineNs=current.binding.deadlineNs=f.now+100*Ms;
        CHECK(f.adapter.Publish(current,f.now));
        CHECK(f.adapter.PublishGate({current.binding.context.resource,10+event,1,false,true},f.now));
    }
    CHECK(f.adapter.Retirement(0)->event==1&&f.adapter.Retirement(23)->event==24);
    CHECK(!f.adapter.Retirement(24)&&f.adapter.ReadGate(current.binding.context.resource));return 0;
}
int RetirementReclaimsOnlyOldActorRefusedCommands(){
    Fixture f;CHECK(f.Setup());using Result=FeedEquipmentSubmitResult;
    CHECK(f.adapter.SubmitResult(f.submission,f.now,true)==Result::NoEntryRetained);
    auto current=f.view;current.identity=Replacement(f.view,f.now).identity;
    current.binding.owner=current.identity.owner;current.binding.context.resource.actor=current.identity.owner.soldier;
    current.binding.context.resource.actorGeneration=current.identity.owner.actorGeneration;
    current.observedNs=current.binding.observedNs=f.now;
    current.deadlineNs=current.binding.deadlineNs=f.now+100*Ms;
    CHECK(f.adapter.Publish(current,f.now));
    CHECK(f.adapter.PublishGate({current.binding.context.resource,11,1,false,true},f.now));
    auto retained=f.submission;retained.source=*f.adapter.Read(current.identity.owner,f.now);
    const auto owner=FeedEquipmentOwner(current.identity.owner);
    auto& c=retained.command;c.authority.owner=c.before.owner=c.request.completion.candidate.owner=owner;
    c.authority.lifetime=c.before.lifetime=c.request.lifetime=11;
    c.before.sequence=c.request.completion.candidate.snapshotSequence=retained.source.sequence;
    c.before.observedNs=retained.source.observedNs;c.before.deadlineNs=retained.source.deadlineNs;
    c.request.requestedNs=f.now+20;c.request.completion.completedNs=f.now+10;
    retained.geometry.observedNs=f.now;retained.geometry.deadlineNs=f.now+90*Ms;
    f.now+=30;CHECK(f.adapter.SubmitResult(retained,f.now,true)==Result::NoEntryRetained);
    auto replacement=Replacement(f.view,f.now);replacement.identity=current.identity;
    CHECK(f.adapter.RetireForReplacementActor(replacement,f.now,Drain(f.view.identity,f.now)));
    const auto proof=f.adapter.Retirement(0);CHECK(proof&&proof->refusedCommands==1&&proof->retiredItems==1);
    CHECK(!proof->pending&&!proof->nativeDispatched&&!proof->ambiguous&&!f.adapter.Receipt(10,1));
    CHECK(f.adapter.ReadGate(current.binding.context.resource)->blocksFire);
    FeedEquipmentNativeCall observed{retained.command,current.identity,100,f.now+100,f.now+110,false,false,true,true};
    CHECK(!f.adapter.Observe(f.Row(0,observed),observed.completedNs+20));
    CHECK(!f.adapter.Observe(f.Row(1,observed),observed.completedNs+20));
    CHECK(f.adapter.Observe(f.Row(2,observed),observed.completedNs+20));
    const auto receipt=f.adapter.Receipt(11,1);CHECK(receipt&&receipt->noTransferEntry&&receipt->command==retained.command);
    CHECK(!f.adapter.Receipt(10,1));return 0;
}
}
int main(){
    CHECK(RefillJoinsExactFeedCommandAndThreeUpdates()==0);
    CHECK(PhysicalOnlyRequiresRealUnchangedUpdates()==0);
    CHECK(AttachmentRefillKeepsTheExactPartialSupply()==0);
    CHECK(UnsupportedAndWrongProvenanceStayUnavailable()==0);
    CHECK(SameCountsCannotReplaceOwnServerCallback()==0);
    CHECK(WrongOwnerAndFailedCallsRetainUncertainty()==0);
    CHECK(ExpiryRequiresObservedNoEntryRejection()==0);
    CHECK(PartialReplicationAndHistoricalReceipts()==0);
    CHECK(ReadAtUsesActualEarlierCoherentView()==0);
    CHECK(NoEntryRejectionRequiresLaterOwnCallbacks()==0);
    CHECK(RefusedUncertainCountsNeverReleaseDebt()==0);
    CHECK(BoundedRefusalPressureRetainsFrontendRetry()==0);
    CHECK(GatePersistsUntilVerifiedCloseAndResourceRetirement()==0);
    CHECK(AdmittedCommandCannotBecomeRefusedNoEntry()==0);
    CHECK(IssuedNoEntrySurvivesExpiredObservationRollover()==0);
    CHECK(IssuedProofRejectsForgerySubstitutionAndNewAdapter()==0);
    CHECK(FeedRetirementRequiresDrainedExactNativeReplacement()==0);
    CHECK(FeedRetirementPreservesReceiptsAndUnrelatedActorDebt()==0);
    CHECK(FeedOnlyRespawnsReclaimBoundedSlotsAndKeepAudit()==0);
    CHECK(RetirementReclaimsOnlyOldActorRefusedCommands()==0);
    std::puts("Bc2FeedEquipmentAdapterTests: 20 deterministic command/native-observation cases (no actual-game or headset claim)");
}
