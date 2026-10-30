#include "Test.h"
#include "Bc2PistolAfterFeedEvidence.h"
using namespace fvr::bc2;
using namespace fvr::interaction;
namespace {
constexpr std::int64_t Now=1000000000;
ReloadHoldIdentity Identity(){return {{0x10000,0x20000,0x30000,0x40000,1,2,3},{0x50000,0x60000,0x70000},0x80000,0x90000,0xa0000};}
AmmoResourceContext Context(const ReloadHoldIdentity& i){return {{i.owner.soldier,i.owner.actorGeneration,i.owner.weapon,1},i.owner.equipGeneration,i.owner.space};}
struct Episode {
    ReloadHoldIdentity identity=Identity();AmmoResourceRequest request{};AmmunitionCommand command{};AmmunitionSnapshot sample{};AmmoResourceOutcome outcome{};
    Episode(std::uint64_t id,AmmunitionOperation operation,int before,int after,int reserveBefore,int reserveAfter,std::int64_t at){
        const auto context=Context(identity);request.binding={identity.owner,context,0xb0000,0xc0000,0xd0000,1,at,at+100000000};
        request.intent={context,id,at,at+100000000,operation,{}};
        command={context,id+100,id+200,at+1,at+100000000,at+2000000000ll,operation,{before,reserveBefore,17},{after,reserveAfter,17},{},before!=after||reserveBefore!=reserveAfter};
        sample={context,command.sourceSequence,at,at+100000000,command.before,true};
        outcome={request,AmmoResourceRequestState::Completed,AmmunitionReceipt{command,id+1000,at+2,at+1000,command.before,command.after,true,true},at+1001,true,{}};
    }
    void ReturnFrom(const Episode& removal){
        const RemovedMagazineResource original{command.context.resource,removal.command.id,removal.command.before.loaded,17};
        command.original=request.intent.original=original;outcome.request=request;outcome.receipt->command=command;
    }
};
bool Admit(PistolAfterFeedEvidence& evidence,const Episode& e){return evidence.Admit(e.request,e.command,e.identity,e.sample,e.command.before.loaded>0);}
fvr::bc2::PistolAfterFeedOwnUpdate Row(const Episode& feed,unsigned branch,std::int64_t at=Now+2000000){
    fvr::bc2::PistolAfterFeedOwnUpdate r;r.identity=feed.identity;r.context=feed.command.context;r.own.beforeOwner=r.own.afterOwner=r.identity.owner;
    r.own.firing=r.beforeFiring=r.identity.firing[branch];r.own.branch=branch;r.own.depth=1;
    if(branch==2){r.own.serverPlayer=r.beforeServerPlayer=r.identity.serverPlayer;r.own.serverSoldier=r.beforeServerSoldier=r.identity.serverSoldier;
        r.own.serverItem=r.beforeServerItem=r.identity.serverItem;}
    r.own.invocation=10000+branch;r.own.beginNs=at;r.own.endNs=at+10+branch;
    r.nativeInvocation=r.nativeUpdate=r.own.invocation;
    r.own.loadedBefore=r.own.loadedAfter=feed.command.after.loaded;r.own.reserveBefore=r.own.reserveAfter=feed.command.after.reserve;
    r.own.current=r.own.next=2;r.own.finished=r.own.identityRetained=r.own.neutralContext=true;
    r.before.verified=r.after.verified=r.effectiveContextVerified=r.finiteAmmoVerified=true;return r;
}
int ExactLaterOwnUpdates(){
    for(unsigned failure=0;failure<18;++failure){PistolAfterFeedEvidence evidence;
        Episode remove{1,AmmunitionOperation::RemoveMagazine,0,0,85,85,Now};
        Episode feed{2,AmmunitionOperation::RefillMagazine,0,17,85,68,Now+1000000};
        CHECK(Admit(evidence,remove)&&evidence.Blocks(remove.command.context.resource)&&evidence.Outcome(remove.outcome));
        CHECK(Admit(evidence,feed)&&evidence.Outcome(feed.outcome));
        for(unsigned n=0;n<3;++n){auto r=Row(feed,n);if(n==2){
            if(failure==1)r.own.beginNs=feed.outcome.receipt->completedNs-1;
            if(failure==2)r.own.invocation=10000;
            if(failure==3)r.own.invocation=feed.outcome.receipt->authorityInvocation;
            if(failure==4)++r.own.serverItem;
            if(failure==5)++r.beforeServerSoldier;
            if(failure==6)++r.own.afterOwner.space;
            if(failure==7)r.own.depth=2;
            if(failure==8)r.own.finished=false;
            if(failure==9)r.own.identityRetained=false;
            if(failure==10)r.own.neutralContext=false;
            if(failure==11)r.own.animationHold=true;
            if(failure==12)r.after.inhibit8a=1;
            if(failure==13)r.after.releaseA4=1;
            if(failure==14)r.after.releaseA5=1;
            if(failure==15)r.own.timer=.01f;
            if(failure==16)r.effectiveContextVerified=false;
            if(failure==17)r.finiteAmmoVerified=false;
        }evidence.Observe(r);}
        const auto state=evidence.Read(feed.command.context.resource,Now+2000100);CHECK(state&&state->emptyDebt&&state->blocksFire);
        CHECK(bool(state->ready)==(failure==0));
        if(state->ready){CHECK(state->ready->semantic.feed.intent==feed.request.intent&&state->ready->semantic.feed.receipt==*feed.outcome.receipt);
            CHECK(state->ready->semantic.updates[2].invocation==10002&&state->ready->feedIdentity==feed.identity);}
    }return 0;
}
int ExactOutcomeAndHistoricalJournal(){
    for(unsigned failure=0;failure<10;++failure){PistolAfterFeedEvidence evidence;Episode remove{1,AmmunitionOperation::RemoveMagazine,16,0,85,85,Now};
        CHECK(Admit(evidence,remove));auto outcome=remove.outcome;
        if(failure==1)++outcome.request.intent.id;
        if(failure==2)++outcome.receipt->command.id;
        if(failure==3)++outcome.receipt->command.sourceSequence;
        if(failure==4)++outcome.receipt->command.context.space;
        if(failure==5)outcome.receipt->authorityVerified=false;
        if(failure==6)outcome.receipt->copiesVerified=false;
        if(failure==7)outcome.receipt->authorityInvocation=0;
        if(failure==8)outcome.state=AmmoResourceRequestState::Uncertain;
        if(failure==9)outcome.receipt->completedNs=outcome.receipt->command.deadlineNs;
        CHECK(evidence.Outcome(outcome)==(failure==0));CHECK(evidence.Blocks(remove.command.context.resource));
        const auto admitted=evidence.Event(0);CHECK(admitted&&admitted->kind==PistolAfterFeedEventKind::Admitted&&admitted->command==remove.command);
        CHECK(admitted->admission.observedNs==Now&&admitted->admission.sequence==remove.sample.sequence&&admitted->initialPolicyReadyVerified);
        if(failure==0){const auto done=evidence.Event(admitted->event);CHECK(done&&done->outcome&&done->outcome->receipt==remove.outcome.receipt);
            const auto state=evidence.Read(remove.command.context.resource,Now+10000000000ll);CHECK(state&&state->removal&&!state->ready);}
    }return 0;
}
int TacticalAndEmptyReturnStayDistinct(){
    for(int loaded:{0,16}){PistolAfterFeedEvidence evidence;Episode remove{1,AmmunitionOperation::RemoveMagazine,loaded,0,85,85,Now};
        Episode feed{2,AmmunitionOperation::ReturnMagazine,0,loaded,85,85,Now+1000000};feed.ReturnFrom(remove);
        CHECK(Admit(evidence,remove)&&evidence.Outcome(remove.outcome)&&Admit(evidence,feed)&&evidence.Outcome(feed.outcome));
        for(unsigned n=0;n<3;++n)evidence.Observe(Row(feed,n));
        const auto state=evidence.Read(feed.command.context.resource,Now+2000100);CHECK(state&&state->emptyDebt==(loaded==0));
        if(loaded){CHECK(state->ready);PistolAfterFeedRelease release{{state->ready->semantic.feed,state->ready->semantic,{},true}};
            CHECK(evidence.Release(release,Now+2000100)&&!evidence.Blocks(feed.command.context.resource));}
        else CHECK(!state->ready&&evidence.Blocks(feed.command.context.resource));
    }return 0;
}
int StowMigrationAndExpiryRetainDebt(){
    PistolAfterFeedEvidence evidence;Episode remove{1,AmmunitionOperation::RemoveMagazine,0,0,85,85,Now};
    Episode feed{2,AmmunitionOperation::RefillMagazine,0,17,85,68,Now+1000000};
    CHECK(Admit(evidence,remove)&&evidence.Outcome(remove.outcome)&&Admit(evidence,feed)&&evidence.Outcome(feed.outcome));
    CHECK(!evidence.Read(feed.command.context.resource,Now+10000000000ll)->ready&&evidence.Blocks(feed.command.context.resource));
    for(unsigned n=0;n<3;++n){auto r=Row(feed,n,Now+10000000000ll);++r.identity.owner.equipGeneration;++r.identity.owner.space;
        r.context=Context(r.identity);r.own.beforeOwner=r.own.afterOwner=r.identity.owner;CHECK(evidence.Observe(r));}
    const auto state=evidence.Read(feed.command.context.resource,Now+10000000100ll);CHECK(state&&state->ready&&state->emptyDebt);
    CHECK(state->ready->feedIdentity==feed.identity&&state->ready->currentIdentity.owner.space==feed.identity.owner.space+1);
    CHECK(state->ready->semantic.feed.receipt.command.context==feed.command.context);
    CHECK(!evidence.Read(feed.command.context.resource,Now+10200000000ll)->ready&&evidence.Blocks(feed.command.context.resource));return 0;
}
int NoEntryRejectionAndActorRetirement(){
    PistolAfterFeedEvidence evidence;Episode remove{1,AmmunitionOperation::RemoveMagazine,0,0,85,85,Now};CHECK(Admit(evidence,remove));
    auto rejected=remove.outcome;rejected.state=AmmoResourceRequestState::Rejected;rejected.receipt.reset();
    CHECK(!evidence.Outcome(rejected)&&evidence.Blocks(remove.command.context.resource)); // native may have run
    rejected.nativeDispatched=false;CHECK(evidence.Outcome(rejected)&&!evidence.Blocks(remove.command.context.resource));
    remove.request.intent.id=3;remove.outcome.request=remove.request;CHECK(Admit(evidence,remove)&&evidence.Outcome(remove.outcome));
    auto replacement=Identity();replacement.owner.soldier+=0x100;replacement.owner.weak+=0x100;
    replacement.owner.actorGeneration++;replacement.serverSoldier+=0x100;
    Bc2AmmoReserveLease lease{replacement,10,Now+10000,Now+100000000,17,85,17,true,false,true};
    AmmoResourceActorDrain drain{remove.identity,{},5,6,Now+10000,Now+100000000,true};
    auto invalid=drain;invalid.callbacksDrained=false;CHECK(!evidence.Retire(lease,invalid,Now+10001));
    CHECK(evidence.Retire(lease,drain,Now+10001)&&!evidence.Read(remove.command.context.resource,Now+10001));
    const auto retired=evidence.Retirement(0);CHECK(retired&&retired->retiredItems==1&&retired->resources[0]==remove.command.context.resource);
    CHECK(retired->callbackRevision==5&&retired->lifetimeEndEpoch==6&&!evidence.Blocks(remove.command.context.resource));return 0;
}
PistolAfterFeedStroke Stroke(const PistolAfterFeedReadiness& ready){
    PistolAfterFeedStroke s;s.feed=ready.semantic.feed;s.context=ready.semantic.context;
    s.request=11;s.profile=12;s.revision=13;s.inputSequence=14;s.observedNs=Now+1500000;s.deadlineNs=s.observedNs+100000000;
    const auto actor=(std::uint64_t(ready.feedIdentity.owner.weak)<<32)|ready.feedIdentity.owner.soldier;
    const auto handEquip=s.context.equipGeneration+70;
    s.gun={1,{actor,s.context.resource.actorGeneration,handEquip,s.context.space},InteractionHand::Right,HandClaimKind::GunHold,
        {s.context.resource.weapon,handEquip},{60,2},0};
    s.mechanism={2,s.gun.owner,InteractionHand::Left,HandClaimKind::Mechanism,s.gun.item,{70,2},s.gun.id};return s;
}
int EmptyReleaseRequiresExactPhysicalAndFreshNative(){
    for(unsigned mode=0;mode<7;++mode){PistolAfterFeedEvidence evidence;
        Episode remove{1,AmmunitionOperation::RemoveMagazine,0,0,85,85,Now};
        Episode feed{2,AmmunitionOperation::RefillMagazine,0,17,85,68,Now+1000000};
        CHECK(Admit(evidence,remove)&&evidence.Outcome(remove.outcome)&&Admit(evidence,feed)&&evidence.Outcome(feed.outcome));
        for(unsigned n=0;n<3;++n)CHECK(evidence.Observe(Row(feed,n)));
        const auto state=evidence.Read(feed.command.context.resource,Now+2000100);CHECK(state&&state->ready);
        PistolAfterFeedRelease release{{state->ready->semantic.feed,state->ready->semantic,Stroke(*state->ready),false}};
        if(mode==1){release.clearance.stroke.reset();release.clearance.retainedPolicyReady=true;}
        if(mode==2)++release.clearance.stroke->feed.intent.id;
        if(mode==3)release.clearance.stroke->observedNs=Now+2000050;
        if(mode==4)release.clearance.stroke->mechanism.prerequisiteClaim=0;
        if(mode==5)++release.clearance.nativeReady.updates[0].invocation;
        if(mode==6){auto contradictory=Row(feed,2);contradictory.own.invocation=contradictory.nativeInvocation=contradictory.nativeUpdate=10020;
            contradictory.own.endNs++;contradictory.after.releaseA5=1;CHECK(!evidence.Observe(contradictory));}
        CHECK(evidence.Release(release,Now+2000100)==(mode==0));
        CHECK(evidence.Blocks(feed.command.context.resource)==(mode!=0));
    }return 0;
}
int OldDebtSurvivesLoadedRemoval(){
    PistolAfterFeedEvidence evidence;Episode remove{1,AmmunitionOperation::RemoveMagazine,0,0,85,85,Now};
    Episode feed{2,AmmunitionOperation::RefillMagazine,0,17,85,68,Now+1000000};
    CHECK(Admit(evidence,remove)&&evidence.Outcome(remove.outcome)&&Admit(evidence,feed)&&evidence.Outcome(feed.outcome));
    Episode again{3,AmmunitionOperation::RemoveMagazine,17,0,68,68,Now+3000000};
    Episode returned{4,AmmunitionOperation::ReturnMagazine,0,17,68,68,Now+4000000};returned.ReturnFrom(again);
    CHECK(Admit(evidence,again)&&evidence.Outcome(again.outcome)&&Admit(evidence,returned)&&evidence.Outcome(returned.outcome));
    for(unsigned n=0;n<3;++n)CHECK(evidence.Observe(Row(returned,n,Now+5000000)));
    const auto state=evidence.Read(returned.command.context.resource,Now+5000100);CHECK(state&&state->ready&&state->emptyDebt);
    CHECK(!evidence.Release({{state->ready->semantic.feed,state->ready->semantic,{},true}},Now+5000100));return 0;
}
}
int ReleaseLatchesDoNotInventAnInitialSlideObligation(){
    const AmmunitionCounts loaded{7,34,17};const PistolAfterFeedNativeBits latches{0,1,1,true};
    CHECK(PistolAfterFeedInitialPolicyReady(latches,true,2,2,0,7,34,loaded));
    CHECK(!latches.Ready()); // Actual firing still waits for neutral own Updates.
    CHECK(!PistolAfterFeedInitialPolicyReady(latches,true,2,2,0,0,34,{0,34,17}));
    CHECK(!PistolAfterFeedInitialPolicyReady(latches,true,12,1,0,7,34,loaded));
    CHECK(!PistolAfterFeedInitialPolicyReady(latches,false,2,2,0,7,34,loaded));
    CHECK(!PistolAfterFeedInitialPolicyReady({},true,2,2,0,7,34,loaded));return 0;
}
int main(){if(ReleaseLatchesDoNotInventAnInitialSlideObligation()||ExactLaterOwnUpdates()||ExactOutcomeAndHistoricalJournal()||TacticalAndEmptyReturnStayDistinct()||
    StowMigrationAndExpiryRetainDebt()||NoEntryRejectionAndActorRetirement()||EmptyReleaseRequiresExactPhysicalAndFreshNative()||OldDebtSurvivesLoadedRemoval())return 1;
    std::puts("8 pistol evidence groups passed; exact receipts/own Updates, durable gate, typed release, actor drain.");}
