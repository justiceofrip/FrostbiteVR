#include "Test.h"
#include "Bc2AmmoResourceService.h"
#include "fvr/interaction/FixedContainerReload.h"
#include <memory>
using namespace fvr::bc2;using namespace fvr::interaction;
namespace {
constexpr std::int64_t Ms=1000000;
using Matrix=fvr::math::Matrix4;
Matrix Identity(){Matrix m{};for(unsigned n=0;n<4;++n)m.values[n][n]=1;return m;}
struct Fixture {
    AmmoResourceService service;
    ReloadHoldIdentity identity{{0x10000,0x20000,0x30000,0x40000,1,2,3},{0x50000,0x60000,0x70000},0x80000,0x90000,0xa0000};
    AmmoResourceBinding binding{identity.owner,{{0x20000,1,0x40000,19},2,3},0xb0000,0xc0000,0xd0000,0xe0000,1000*Ms,1100*Ms};
    HandInteractionOwner hand{(std::uint64_t(0x30000)<<32)|0x20000,1,71,3};
    FixedContainerProfile profile{};FixedContainerSample x{};HandInteraction hands;
    AmmunitionLedger* ledger=nullptr;std::unique_ptr<FixedContainerReload> policy;
    HandClaimToken gun{},object{};bool holding=false;std::uint64_t intent=0;
    Fixture(){
        profile.kind=FixedContainerKind::EnBlocClip;profile.id=20;profile.revision=21;profile.ammunitionKind=22;
        profile.capacity=profile.roundsPerInsertion=8;profile.maxAgeNs=100*Ms;
        auto& g=profile.geometry[2];g.contact={102,.02f,.1f};auto& m=g.motion;
        m.operation=FeedPhysicalOperation::SeatFeed;m.kind=FeedMotionKind::FreePlacement;m.measured=true;
        m.weaponFromMotion=m.motionFromContact=Identity();m.travel=.1f;m.completeFraction=.9f;m.releaseFraction=.8f;
        m.pathToleranceMeters=.005f;m.orientationToleranceRadians=.1f;m.maxStepMeters=.04f;m.maxStepRadians=.25f;
        m.endpointDwellNs=20*Ms;m.maximumGestureNs=1000*Ms;m.maxInputGapNs=40*Ms;
        m.placement.weaponFromSource=m.placement.weaponFromDestination=m.placement.sourceFromGrip=Identity();
        m.placement.weaponFromDestination.values[3][0]=.1f;
        x.ammunition={binding.context,1,1000*Ms,1100*Ms,{3,2,8},true};
        x.input={hand,1,1000*Ms,1100*Ms,1000*Ms,true,{true,true},{false,false}};
        x.held=true;x.weaponFromContact=Identity();x.interaction.hand={44,1};x.interaction.trackingGeneration=1;
        x.interaction.focused=x.interaction.contact.tracked=true;
        service.Select(binding,identity,x.ammunition,x.input.nowNs);
        service.WithSelectedLedger(binding,x.input.nowNs,[&](AmmunitionLedger& shared)noexcept{
            ledger=&shared;policy=std::make_unique<FixedContainerReload>(profile,binding.context,FeedKey{7,8},FeedKey{9,10},
                FeedSupplyKey{{11,12},{13,14}},30,shared,true,hand);
            return policy->Bind(x.ammunition,x.input.nowNs);
        });
        HandClaimRequest q{hand,InteractionHand::Right,HandClaimKind::GunHold,{0x40000,71},{{300,1},1,1100*Ms,true},++intent,0};
        if(const auto acquired=hands.Acquire(x.input,q);acquired.claim)gun=acquired.claim->token;
    }
    void Tick(float position,bool grip){
        ++x.ammunition.sequence;++x.input.sequence;x.input.nowNs+=10*Ms;
        x.input.observedNs=x.ammunition.observedNs=binding.observedNs=x.input.nowNs;
        x.input.deadlineNs=x.ammunition.deadlineNs=binding.deadlineNs=x.input.nowNs+100*Ms;
        x.input.released={false,false};x.ammunition.counts=ledger->Snapshot().counts;
        auto& c=x.interaction.contact;c.owner={0x20000,1,0x40000,2,3};c.mechanism={7,8};
        c.snapshotSequence=x.ammunition.sequence;c.inputSequence=x.input.sequence;c.observedNs=x.input.observedNs;
        c.weaponFromHandContact=Identity();c.weaponFromHandContact.values[3][0]=position*.1f;
        x.interaction.grip=grip;x.claim=holding?std::optional{object}:std::nullopt;
        service.Select(binding,identity,x.ammunition,x.input.nowNs);
    }
    FixedContainerResult Update(){
        FixedContainerResult result;
        service.WithSelectedLedger(binding,x.input.nowNs,[&](AmmunitionLedger& shared)noexcept{
            if(&shared!=ledger)return false;result=policy->Update(x,hands);return true;
        });return result;
    }
    FixedContainerResult Run(float p,bool grip){
        Tick(p,grip);hands.Update(x.input);hands.Renew(x.input,gun,{{300,1},x.input.sequence,x.input.deadlineNs,true});
        if(holding)hands.Renew(x.input,object,{object.contact,x.input.sequence,x.input.deadlineNs,true});
        return Update();
    }
    std::optional<FixedContainerRequest> Insert(bool centralRelease=false){
        if(Run(0,false).interaction.status!=FeedInteractionStatus::Armed)return {};
        Tick(0,true);hands.Update(x.input);hands.Renew(x.input,gun,{{300,1},x.input.sequence,x.input.deadlineNs,true});
        HandClaimRequest q{hand,InteractionHand::Left,HandClaimKind::AmmoObject,{11,12},
            {{102,21},x.input.sequence,x.input.deadlineNs,true},++intent,0};
        const auto claim=hands.Acquire(x.input,q);if(!claim.claim)return {};
        object=claim.claim->token;holding=true;x.claim=object;
        if(Update().interaction.status!=FeedInteractionStatus::Acquired)return {};
        for(float p:{.2f,.4f,.6f,.8f,1.f,1.f,1.f})Run(p,true);
        Tick(1,false);x.input.released[0]=true;
        if(centralRelease){
            const auto before=hands.Current(InteractionHand::Left);const auto event=hands.Update(x.input);
            for(const auto& released:event.released)if(before&&released&&released->token==before->token)
                x.releaseEvidence=FixedContainerReleaseEvidence{*before,*released,x.input};
        }
        return Update().dispatch;
    }
    AmmoResourceRequest Request(const FixedContainerRequest& physical){
        AmmoResourceRequest request{binding,{binding.context,1,x.input.observedNs,x.input.deadlineNs,
            AmmunitionOperation::InsertRounds,{},8}};
        request.preparedCommand=physical.ammunition;return request;
    }
    AmmoResourceOwnUpdate Row(unsigned branch,const AmmoResourceNativeCall& call){
        AmmoResourceOwnUpdate row;row.beforeOwner=row.afterOwner=identity.owner;row.firing=identity.firing[branch];
        row.serverPlayer=identity.serverPlayer;row.serverSoldier=identity.serverSoldier;row.serverItem=identity.serverItem;
        row.invocation=call.invocation+(branch==2?0:branch+1);row.beginNs=call.beginNs-1;row.endNs=call.endNs+10+branch;
        row.branch=branch;row.depth=1;row.loadedBefore=call.before.loaded;row.reserveBefore=call.before.reserve;
        row.loadedAfter=call.after.loaded;row.reserveAfter=call.after.reserve;row.current=row.next=2;
        row.finished=row.identityRetained=row.neutralContext=true;return row;
    }
};
int SharedLedgerRequiresActualServiceReceipt(){
    Fixture f;CHECK(f.ledger&&f.policy&&f.gun.id);CHECK(f.hand.actor!=f.binding.context.resource.actor);
    CHECK(f.hand.equipGeneration!=f.binding.context.equipGeneration);
    const auto physical=f.Insert();CHECK(physical);CHECK(f.policy->Pending()==physical);
    CHECK(f.ledger->Phase()==AmmunitionLedgerPhase::Queued);CHECK(f.ledger->Pending()==physical->ammunition);
    CHECK((physical->ammunition.before==AmmunitionCounts{3,2,8}));CHECK((physical->ammunition.after==AmmunitionCounts{5,0,8}));
    CHECK(f.service.Select(f.binding,f.identity,f.x.ammunition,f.x.input.nowNs));
    const auto request=f.Request(*physical);const auto accepted=f.service.Accept(request,f.x.input.nowNs);
    CHECK(accepted==physical->ammunition);CHECK(!f.service.Accept(request,f.x.input.nowNs));
    AmmoResourceNativeCall actual;
    CHECK(f.service.InvokeNative(f.x.ammunition,[&]()noexcept{return f.x.input.nowNs;},[&](std::int64_t began)noexcept{
        actual={f.identity,accepted->id,500,began,began+1,accepted->before,accepted->after,true,true,true};return actual;
    }));
    CHECK(!f.service.Dispatch(f.x.ammunition,f.x.input.nowNs));
    CHECK(!f.service.Observe(f.Row(0,actual),actual.endNs+100));CHECK(!f.service.Observe(f.Row(1,actual),actual.endNs+100));
    CHECK(!f.service.Outcome());CHECK(f.ledger->Snapshot().counts==accepted->before);
    CHECK(f.service.Observe(f.Row(2,actual),actual.endNs+100));
    CHECK(f.service.Outcome()&&f.service.Outcome()->receipt);const auto receipt=*f.service.Outcome()->receipt;
    CHECK(f.ledger->Snapshot().counts==accepted->after);f.Tick(1,false);
    bool complete=false;CHECK(f.service.WithSelectedLedger(f.binding,f.x.input.nowNs,[&](AmmunitionLedger& ledger)noexcept{
        complete=&ledger==f.ledger&&f.policy->Complete(*physical,receipt,f.x.ammunition,f.x.input.nowNs);return complete;
    }));CHECK(complete&&!f.policy->Pending());CHECK(f.policy->Step()==FixedContainerStep::Complete);
    CHECK(f.ledger->Snapshot().counts==accepted->after);return 0;
}
int PreparedCommandCannotBeSubstitutedOrRestamped(){
    Fixture f;const auto physical=f.Insert();CHECK(physical);
    CHECK(f.service.Select(f.binding,f.identity,f.x.ammunition,f.x.input.nowNs));
    auto request=f.Request(*physical);request.preparedCommand->after.loaded++;
    CHECK(!f.service.Accept(request,f.x.input.nowNs));CHECK(f.ledger->Pending()==physical->ammunition);
    request=f.Request(*physical);request.intent.id=2;request.intent.deadlineNs=physical->ammunition.admissionDeadlineNs-1;
    CHECK(!f.service.Accept(request,f.x.input.nowNs));CHECK(!f.ledger->Pending());
    CHECK(f.policy->RejectUndispatched(*physical));CHECK(!f.policy->Pending());
    CHECK((f.ledger->Snapshot().counts==AmmunitionCounts{3,2,8}));return 0;
}
bool Exchange(void* p,unsigned index,std::uint32_t expected,std::uint32_t next,std::uint32_t& seen)noexcept {
    auto& words=*static_cast<std::array<std::uint32_t,2>*>(p);seen=words[index];if(seen!=expected)return false;words[index]=next;return true;
}
int ScopedNativeInterlockRestoresInputs(){
    for(bool blocked:{false,true})for(bool empty:{false,true}){
        std::array<std::uint32_t,2> words{0x12340000u,7u};const auto original=words;FixedContainerInputGuard guard;
        CHECK(guard.Apply({&words,Exchange},original,blocked,empty));
        CHECK(words[1]==(blocked?0u:3u));CHECK((words[0]&255u)==unsigned(blocked||empty));
        CHECK(guard.Restore({&words,Exchange})&&guard.Exact()&&words==original);
    }
    std::array<std::uint32_t,2> words{0,7};FixedContainerInputGuard changed;
    CHECK(changed.Apply({&words,Exchange},words,true,false));words[1]=8;
    CHECK(!changed.Restore({&words,Exchange})&&!changed.Exact()&&words[1]==8);return 0;
}
int OlderRendererEvidenceUsesCurrentSafetyWithoutRollback(){
    Fixture f;CHECK(f.Run(0,false).interaction.status==FeedInteractionStatus::Armed);
    f.Tick(0,true);f.hands.Update(f.x.input);f.hands.Renew(f.x.input,f.gun,{{300,1},f.x.input.sequence,f.x.input.deadlineNs,true});
    HandClaimRequest request{f.hand,InteractionHand::Left,HandClaimKind::AmmoObject,{11,12},
        {{102,21},f.x.input.sequence,f.x.input.deadlineNs,true},++f.intent,0};
    const auto acquired=f.hands.Acquire(f.x.input,request);CHECK(acquired.claim);
    f.object=acquired.claim->token;f.holding=true;f.x.claim=f.object;CHECK(f.Update().interaction.status==FeedInteractionStatus::Acquired);
    const auto original=f.x.input;auto safety=original;++safety.sequence;safety.observedNs=safety.nowNs=original.nowNs+Ms;safety.deadlineNs=safety.nowNs+100*Ms;
    CHECK(f.hands.Update(safety).inputValid);
    CHECK(f.hands.RenewFrom(safety,original,f.object,{f.object.contact,original.sequence,original.deadlineNs,true}).accepted);
    f.x.input.nowNs=safety.nowNs;f.x.currentSafety=safety;
    CHECK(f.Update().status==FixedContainerStatus::Gesture);CHECK(f.hands.Current(InteractionHand::Left)->token==f.object);
    f.x.currentSafety->focused=false;CHECK(f.Update().status==FixedContainerStatus::Suspended);
    CHECK(!f.hands.Current(InteractionHand::Left));return 0;
}
int StableResourceRebindPreservesMechanismState(){
    Fixture f;const auto old=f.binding.context;const auto step=f.policy->Step();
    f.binding.context.equipGeneration++;f.binding.context.space++;f.identity.owner.equipGeneration++;f.identity.owner.space++;
    f.binding.owner=f.identity.owner;f.x.ammunition.context=f.binding.context;f.hand.equipGeneration++;f.hand.space++;
    f.x.input.owner=f.hand;f.Tick(0,false);
    CHECK(f.policy->RebindContext(f.x.ammunition,f.hand,f.x.input.nowNs));CHECK(f.policy->Step()==step);
    auto foreign=f.x.ammunition;foreign.context.resource.weaponGeneration++;
    CHECK(!f.policy->RebindContext(foreign,f.hand,f.x.input.nowNs));CHECK(f.policy->Step()==step);
    CHECK(f.ledger->Snapshot().context.resource==old.resource);return 0;
}
int ActualCentralReleaseEventCompletesPlacement(){
    Fixture f;const auto request=f.Insert(true);CHECK(request&&f.x.releaseEvidence);
    CHECK(f.x.releaseEvidence->released.reason==HandInteractionReason::Released);
    CHECK(f.ledger->Phase()==AmmunitionLedgerPhase::Queued&&!f.service.Active());return 0;
}
int PendingServiceReceiptCompletesAfterStowAndContextChange(){
    Fixture f;const auto physical=f.Insert();CHECK(physical);
    const auto request=f.Request(*physical);const auto accepted=f.service.Accept(request,f.x.input.nowNs);CHECK(accepted);
    AmmoResourceNativeCall actual;unsigned nativeCalls=0;
    CHECK(f.service.InvokeNative(f.x.ammunition,[&]()noexcept{return f.x.input.nowNs;},[&](std::int64_t began)noexcept{
        ++nativeCalls;actual={f.identity,accepted->id,600,began,began+1,accepted->before,accepted->after,true,true,true};return actual;
    }));
    const std::array rows{f.Row(0,actual),f.Row(1,actual),f.Row(2,actual)};
    f.policy->Suspend();f.binding.context.equipGeneration++;f.binding.context.space++;
    f.identity.owner.equipGeneration++;f.identity.owner.space++;f.binding.owner=f.identity.owner;
    f.x.ammunition.context=f.binding.context;f.hand.equipGeneration++;f.hand.space++;f.x.input.owner=f.hand;
    f.Tick(1,false); // Select cancels admission, retaining the already entered native operation.
    CHECK(f.ledger->Phase()==AmmunitionLedgerPhase::NeedsReconciliation);
    CHECK(!f.policy->RebindContext(f.x.ammunition,f.hand,f.x.input.nowNs));
    CHECK(!f.service.Observe(rows[0],f.x.input.nowNs));CHECK(!f.service.Observe(rows[1],f.x.input.nowNs));
    CHECK(f.service.Observe(rows[2],f.x.input.nowNs));
    CHECK(f.service.Outcome()&&f.service.Outcome()->receipt);const auto receipt=*f.service.Outcome()->receipt;
    f.Tick(1,false);CHECK(f.service.View());CHECK(f.ledger->Snapshot().context==f.binding.context);
    CHECK(!f.policy->Complete(*physical,receipt,f.x.ammunition,f.x.input.nowNs));
    auto bad=receipt;++bad.authorityInvocation;
    CHECK(!f.policy->CompleteAfterContextChange(*physical,bad,f.x.ammunition,f.x.input.nowNs));
    bad=receipt;bad.copiesVerified=false;
    CHECK(!f.policy->CompleteAfterContextChange(*physical,bad,f.x.ammunition,f.x.input.nowNs));
    auto foreign=f.x.ammunition;++foreign.context.resource.weaponGeneration;
    CHECK(!f.policy->CompleteAfterContextChange(*physical,receipt,foreign,f.x.input.nowNs));
    auto counts=f.x.ammunition;--counts.counts.loaded;
    CHECK(!f.policy->CompleteAfterContextChange(*physical,receipt,counts,f.x.input.nowNs));
    auto stale=f.x.ammunition;stale.sequence=physical->ammunition.sourceSequence;
    CHECK(!f.policy->CompleteAfterContextChange(*physical,receipt,stale,f.x.input.nowNs));
    CHECK(f.policy->Pending()==physical);
    CHECK(f.service.WithSelectedLedger(f.binding,f.x.input.nowNs,[&](AmmunitionLedger& ledger)noexcept{
        return &ledger==f.ledger&&f.policy->CompleteAfterContextChange(*physical,receipt,f.x.ammunition,f.x.input.nowNs);
    }));
    CHECK(!f.policy->Pending()&&f.policy->Step()==FixedContainerStep::Complete);
    CHECK(f.policy->RebindContext(f.x.ammunition,f.hand,f.x.input.nowNs));
    CHECK(f.policy->CompleteAfterContextChange(*physical,receipt,f.x.ammunition,f.x.input.nowNs));
    CHECK(nativeCalls==1&&!f.service.Active()&&!f.ledger->Pending());
    CHECK(f.ledger->Snapshot().counts==accepted->after);
    CHECK(accepted->before.loaded+accepted->before.reserve==accepted->after.loaded+accepted->after.reserve);return 0;
}
int ChangedCountsWithoutServiceReceiptCannotConsumePending(){
    Fixture f;const auto physical=f.Insert();CHECK(physical);
    f.ledger->Cancel(); // An uncalled queued command cannot grant a receipt.
    f.binding.context.equipGeneration++;f.binding.context.space++;
    f.identity.owner.equipGeneration++;f.identity.owner.space++;f.binding.owner=f.identity.owner;
    f.x.ammunition.context=f.binding.context;f.Tick(1,false);
    f.x.ammunition.counts=physical->ammunition.after;++f.x.ammunition.sequence;
    CHECK(f.service.Select(f.binding,f.identity,f.x.ammunition,f.x.input.nowNs));
    const AmmunitionReceipt invented{physical->ammunition,600,physical->ammunition.requestedNs,
        physical->ammunition.requestedNs+1,physical->ammunition.before,physical->ammunition.after,true,true};
    CHECK(!f.policy->CompleteAfterContextChange(*physical,invented,f.x.ammunition,f.x.input.nowNs));
    CHECK(f.policy->Pending()==physical&&f.policy->BlocksFire());return 0;
}
int NativeCountClockMayBeNewerThanOriginalRendererClock(){
    Fixture f;
    const auto sample=[&](float p,bool grip,bool acquire){
        if(f.x.currentSafety)f.x.input.sequence=f.x.currentSafety->sequence;
        f.x.currentSafety.reset();f.Tick(p,grip);const auto original=f.x.input;
        f.hands.Update(original);
        auto safety=original;++safety.sequence;safety.observedNs=safety.nowNs=original.nowNs+Ms;
        safety.deadlineNs=safety.nowNs+100*Ms;f.hands.Update(safety);
        f.hands.Renew(safety,f.gun,{{300,1},safety.sequence,safety.deadlineNs,true});
        if(acquire){
            const auto got=f.hands.AcquireFrom(safety,original,{f.hand,InteractionHand::Left,HandClaimKind::AmmoObject,{11,12},
                {{102,21},original.sequence,original.deadlineNs,true},++f.intent,0});
            if(got.claim){f.object=got.claim->token;f.holding=true;}
        }else if(f.holding)f.hands.RenewFrom(safety,original,f.object,{f.object.contact,original.sequence,original.deadlineNs,true});
        f.x.claim=f.holding?std::optional{f.object}:std::nullopt;f.x.input.nowNs=safety.nowNs;f.x.currentSafety=safety;
        ++f.x.ammunition.sequence;f.x.ammunition.observedNs=f.binding.observedNs=safety.nowNs;
        f.x.ammunition.deadlineNs=f.binding.deadlineNs=safety.deadlineNs;
        f.x.interaction.contact.snapshotSequence=f.x.ammunition.sequence;
        f.service.Select(f.binding,f.identity,f.x.ammunition,safety.nowNs);
        return f.Update();
    };
    CHECK(sample(0,false,false).interaction.status==FeedInteractionStatus::Armed);
    CHECK(sample(0,true,true).interaction.status==FeedInteractionStatus::Acquired);
    CHECK(sample(.2f,true,false).interaction.status==FeedInteractionStatus::Moving);
    CHECK(f.x.ammunition.observedNs>f.x.input.observedNs);
    CHECK(f.x.interaction.contact.observedNs==f.x.input.observedNs);
    CHECK(f.hands.Current(InteractionHand::Left)->deadlineNs==f.x.input.deadlineNs);
    const auto originalDeadline=f.x.input.deadlineNs;
    f.x.input.nowNs=originalDeadline;f.x.currentSafety->nowNs=originalDeadline;
    CHECK(f.Update().status==FixedContainerStatus::Invalid);
    CHECK(!f.policy->Pending());return 0;
}
}
int main(){CHECK(SharedLedgerRequiresActualServiceReceipt()==0);CHECK(PreparedCommandCannotBeSubstitutedOrRestamped()==0);
    CHECK(ScopedNativeInterlockRestoresInputs()==0);CHECK(OlderRendererEvidenceUsesCurrentSafetyWithoutRollback()==0);
    CHECK(StableResourceRebindPreservesMechanismState()==0);CHECK(ActualCentralReleaseEventCompletesPlacement()==0);
    CHECK(PendingServiceReceiptCompletesAfterStowAndContextChange()==0);CHECK(ChangedCountsWithoutServiceReceiptCannotConsumePending()==0);
    CHECK(NativeCountClockMayBeNewerThanOriginalRendererClock()==0);return 0;}
