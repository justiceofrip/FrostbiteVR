#include "fvr/interaction/FeedAmmunition.h"
#include "Test.h"
#include <type_traits>
using namespace fvr::interaction;
namespace {
using Effect=FeedAmmunitionEffect;
using Op=FeedPhysicalOperation;
static_assert(!std::is_copy_constructible_v<FeedAmmunition>);
static_assert(!std::is_copy_assignable_v<FeedAmmunition>);
struct Fixture {
    FeedMechanismProfile profile{};
    FeedOwner owner{1,2,3,4,5};FeedKey mechanism{6,7};
    FeedSupplyKey original{{10,1},{20,1}},goal{{30,2},{40,2}};
    FeedAmmunitionSnapshot sample{};
    FeedAmmunitionAuthority authority{};
    std::optional<FeedAmmunition> ledger{};
    std::int64_t now=1000;
    std::uint64_t requestId=0;
    Fixture(int loaded=25,int reserve=80,int capacity=50){
        profile.family=FeedFamily::Belt;profile.id=1;profile.revision=2;profile.ammunitionKind=3;
        profile.maxAgeNs=100;profile.cover=profile.latch=profile.container=profile.charge=true;
        for(unsigned i=0;i<profile.contacts.size();++i)profile.contacts[i]={100+i,.025f,.2f};
        sample.owner=owner;sample.mechanism=mechanism;sample.lifetime=500;sample.sequence=1;
        sample.observedNs=now;sample.deadlineNs=now+100;sample.counts={loaded,reserve,capacity};
        sample.loadedSupply=original;sample.coherentIdleVerified=true;
        authority.key={80,1};authority.binding={90,1};authority.owner=owner;authority.mechanism=mechanism;
        authority.lifetime=500;authority.profileId=profile.id;authority.profileRevision=profile.revision;authority.verified=true;
        ledger.emplace(profile,owner,mechanism,goal,500);
    }
    bool Bind(){return ledger->Bind(sample,now);}
    void Tick(){++sample.sequence;sample.observedNs=++now;sample.deadlineNs=now+100;}
    FeedReloadRequest Request(Effect effect,FeedSupplyKey supply,int rounds,Op op=Op::WithdrawFeed){
        authority.effect=effect;authority.supply=supply;authority.rounds=rounds;authority.operation=op;
        const auto part=feed_mechanism_detail::OperationPart(op);
        const bool resourceOp=op==Op::WithdrawFeed||op==Op::DetachContainer||op==Op::AttachContainer||op==Op::SeatFeed;
        const auto physicalSupply=effect==Effect::PhysicalOnly||effect==Effect::Unavailable?goal:supply;
        FeedPhysicalCandidate c{owner,mechanism,resourceOp?physicalSupply:FeedSupplyKey{},goal,profile.id,profile.revision,
            sample.sequence,op,part,profile.contacts[static_cast<unsigned>(part)].role};
        return FeedReloadRequest{++requestId,500,authority.binding,{requestId,requestId,now,c},now};
    }
    std::optional<FeedAmmunitionCommand> Send(Effect effect,FeedSupplyKey supply,int rounds,Op op=Op::WithdrawFeed){
        const auto request=Request(effect,supply,rounds,op);return ledger->Dispatch(request,authority,sample,now);
    }
    FeedAmmunitionReceipt Receipt(const FeedAmmunitionCommand& command,bool applied=true){
        FeedAmmunitionReceipt r;r.command=command;r.authorityInvocation=command.request.id;
        r.beganNs=command.request.requestedNs;r.completedNs=++now;
        r.post=command.before;r.post.sequence++;r.post.observedNs=now;r.post.deadlineNs=now+100;
        r.post.counts=applied?command.after:command.before.counts;
        r.post.loadedSupply=applied?command.loadedSupplyAfter:command.before.loadedSupply;
        r.acknowledgement={command.request,applied?FeedReloadAcknowledgement::Applied:FeedReloadAcknowledgement::Rejected,
            r.post.sequence,r.post.observedNs};
        r.authorityVerified=r.copiesVerified=true;r.noTransferEntry=!applied;return r;
    }
    bool Finish(const FeedAmmunitionCommand& command,bool applied=true){
        const auto receipt=Receipt(command,applied);const bool result=ledger->Complete(receipt,now);
        if(result)sample=receipt.post;return result;
    }
    int Held(FeedSupplyKey supply)const {
        for(const auto& r:ledger->State().resources)if(r&&r->supply==supply)return r->rounds;
        return -1;
    }
    std::uint64_t Total()const {
        const auto s=ledger->State();std::uint64_t total=std::uint64_t(s.snapshot.counts.loaded)+s.snapshot.counts.reserve;
        for(const auto& r:s.resources)if(r)total+=r->rounds;return total;
    }
};
int PartialReplacementRetainsEveryRemainder(){
    Fixture f;CHECK(f.Bind());CHECK(f.Total()==105);
    auto c=f.Send(Effect::ExtractLoaded,f.original,25);CHECK(c&&c->after.loaded==0&&c->after.reserve==80);
    CHECK(f.Held(f.original)==-1&&f.Total()==105);CHECK(f.Finish(*c));
    CHECK(f.Held(f.original)==25&&f.Total()==105);
    c=f.Send(Effect::AcquireReserve,f.goal,65,Op::AttachContainer);CHECK(c&&f.Finish(*c));
    CHECK(f.sample.counts.reserve==15&&f.Held(f.goal)==65&&f.Total()==105);
    c=f.Send(Effect::LoadSupply,f.goal,50,Op::SeatFeed);CHECK(c&&f.Finish(*c));
    CHECK(f.sample.counts.loaded==50&&f.Held(f.goal)==15&&f.Held(f.original)==25&&f.Total()==105);
    c=f.Send(Effect::ReturnSupply,f.original,25,Op::DetachContainer);CHECK(c&&f.Finish(*c));
    CHECK(f.Held(f.original)==0&&f.sample.counts.reserve==40&&f.Total()==105);
    c=f.Send(Effect::ReturnSupply,f.goal,15,Op::CloseCover);CHECK(c&&f.Finish(*c));
    CHECK(f.Held(f.goal)==0&&f.sample.counts.reserve==55&&f.sample.counts.loaded==50&&f.Total()==105);
    return 0;
}
int PartialOriginalReturnsWithoutReserveGrant(){
    Fixture f(17,0);CHECK(f.Bind());auto c=f.Send(Effect::ExtractLoaded,f.original,17);CHECK(c&&f.Finish(*c));
    CHECK(f.Held(f.original)==17&&f.sample.counts.loaded==0);
    c=f.Send(Effect::LoadSupply,f.original,17,Op::SeatFeed);CHECK(c&&f.Finish(*c));
    CHECK(f.Held(f.original)==0&&f.sample.counts.loaded==17&&f.sample.counts.reserve==0&&f.Total()==17);
    return 0;
}
int EmptyBeltHasIdentityWithoutSyntheticRounds(){
    Fixture f(0,0);CHECK(f.Bind());auto c=f.Send(Effect::ExtractLoaded,f.original,0);
    CHECK(c&&!c->mutationRequired&&f.Finish(*c));CHECK(f.Held(f.original)==0&&f.Total()==0);
    CHECK(!f.Send(Effect::AcquireReserve,f.goal,1,Op::AttachContainer));
    c=f.Send(Effect::LoadSupply,f.original,0,Op::SeatFeed);CHECK(c&&!c->mutationRequired&&f.Finish(*c));
    CHECK(f.sample.loadedSupply==f.original&&f.Total()==0);
    CHECK(!f.Send(Effect::ReturnSupply,f.original,1,Op::CloseCover));return 0;
}
int NoImplicitTransferAtAnyPhysicalStage(){
    for(unsigned n=1;n<=9;++n){
        Fixture f;CHECK(f.Bind());auto request=f.Request(Effect::Unavailable,{},0,static_cast<Op>(n));
        CHECK(!f.ledger->Dispatch(request,f.authority,f.sample,f.now));
    }
    Fixture f;CHECK(f.Bind());auto c=f.Send(Effect::PhysicalOnly,{},0,Op::SeatFeed);
    CHECK(c&&!c->mutationRequired&&c->after==f.sample.counts);CHECK(f.Finish(*c));
    CHECK(f.Held(f.goal)==-1&&f.sample.loadedSupply==f.original&&f.Total()==105);return 0;
}
int TransferStageComesFromExplicitAuthority(){
    Fixture f(0,70);f.sample.loadedSupply={};CHECK(f.Bind());
    auto c=f.Send(Effect::AcquireReserve,f.goal,60,Op::CloseCover);CHECK(c&&f.Finish(*c));
    CHECK(f.sample.counts.loaded==0&&f.sample.counts.reserve==10&&f.Held(f.goal)==60);
    c=f.Send(Effect::LoadSupply,f.goal,50,Op::CycleAction);CHECK(c&&f.Finish(*c));
    CHECK(f.sample.counts.loaded==50&&f.Held(f.goal)==10&&f.Total()==70);return 0;
}
int AtomicNativeRefillDoesNotInventAnIntermediateTransfer(){
    Fixture partial(17,20);CHECK(partial.Bind());
    auto c=partial.Send(Effect::RefillLoaded,partial.original,20,Op::CloseCover);CHECK(c);
    CHECK(c->after.loaded==37&&c->after.reserve==0&&partial.Held(partial.original)==-1);
    CHECK(partial.Finish(*c));CHECK(partial.Held(partial.original)==0&&partial.Total()==37);
    Fixture replacement(12,70);CHECK(replacement.Bind());
    c=replacement.Send(Effect::ExtractLoaded,replacement.original,12);CHECK(c&&replacement.Finish(*c));
    c=replacement.Send(Effect::RefillLoaded,replacement.goal,50,Op::SeatFeed);CHECK(c&&replacement.Finish(*c));
    CHECK(replacement.sample.counts.loaded==50&&replacement.sample.counts.reserve==20&&
        replacement.Held(replacement.original)==12&&replacement.Held(replacement.goal)==0&&replacement.Total()==82);
    CHECK(!replacement.Send(Effect::RefillLoaded,replacement.goal,1,Op::CloseCover));
    Fixture wrong;CHECK(wrong.Bind());CHECK(!wrong.Send(Effect::RefillLoaded,wrong.goal,10,Op::SeatFeed));
    Fixture empty(0,0);empty.sample.loadedSupply={};CHECK(empty.Bind());
    CHECK(!empty.Send(Effect::RefillLoaded,empty.goal,1,Op::SeatFeed));return 0;
}
int DispatchAndReceiptsAreExactlyOnce(){
    Fixture f;CHECK(f.Bind());const auto request=f.Request(Effect::ExtractLoaded,f.original,25);
    const auto c=f.ledger->Dispatch(request,f.authority,f.sample,f.now);CHECK(c);
    for(unsigned i=0;i<4;++i)CHECK(!f.ledger->Dispatch(request,f.authority,f.sample,f.now));
    const auto receipt=f.Receipt(*c);CHECK(f.ledger->Complete(receipt,f.now));f.sample=receipt.post;
    CHECK(f.Held(f.original)==25&&f.ledger->State().acceptedOperations==1);
    for(unsigned i=0;i<4;++i)CHECK(f.ledger->Complete(receipt,f.now));
    CHECK(f.Total()==105&&f.Held(f.original)==25&&f.ledger->State().acceptedOperations==1);
    CHECK(!f.ledger->Dispatch(request,f.authority,f.sample,f.now));
    auto changed=receipt;++changed.authorityInvocation;CHECK(!f.ledger->Complete(changed,f.now));
    const auto next=f.Send(Effect::LoadSupply,f.original,25,Op::SeatFeed);CHECK(next);
    CHECK(!f.ledger->Complete(receipt,f.now));CHECK(f.ledger->State().pending==next);
    auto nextReceipt=f.Receipt(*next);nextReceipt.authorityInvocation=receipt.authorityInvocation;
    CHECK(!f.ledger->Complete(nextReceipt,f.now)&&f.ledger->State().pending==next);
    ++nextReceipt.authorityInvocation;CHECK(f.ledger->Complete(nextReceipt,f.now)&&f.Total()==105);return 0;
}
int WrongIdentityOrAuthorityCannotSpend(){
    for(unsigned mismatch=0;mismatch<20;++mismatch){
        Fixture f;CHECK(f.Bind());auto r=f.Request(Effect::ExtractLoaded,f.original,25);
        switch(mismatch){
        case 0:++r.lifetime;break;case 1:++r.binding.id;break;
        case 2:++r.completion.candidate.owner.actor;break;case 3:++r.completion.candidate.owner.actorGeneration;break;
        case 4:++r.completion.candidate.owner.weapon;break;case 5:++r.completion.candidate.owner.equipGeneration;break;
        case 6:++r.completion.candidate.owner.space;break;case 7:++r.completion.candidate.mechanism.generation;break;
        case 8:++r.completion.candidate.supply.resource.generation;break;case 9:++r.completion.candidate.supply.container.generation;break;
        case 10:++r.completion.candidate.goal.resource.id;break;case 11:++f.authority.lifetime;break;
        case 12:++f.authority.owner.weapon;break;case 13:f.authority.verified=false;break;
        case 14:f.authority.key={};break;case 15:++f.authority.profileRevision;break;
        case 16:f.authority.operation=Op::SeatFeed;break;case 17:++f.sample.lifetime;break;
        case 18:++f.sample.owner.actorGeneration;break;case 19:++f.sample.mechanism.id;break;
        }
        CHECK(!f.ledger->Dispatch(r,f.authority,f.sample,f.now));
        CHECK(!f.ledger->State().pending&&f.Held(f.original)==-1&&f.Total()==105);
    }return 0;
}
int ResourceAndContainerCannotAlias(){
    Fixture f;f.goal.resource=f.original.resource;
    f.ledger.emplace(f.profile,f.owner,f.mechanism,f.goal,500);CHECK(f.Bind());
    auto c=f.Send(Effect::ExtractLoaded,f.original,25);CHECK(c&&f.Finish(*c));
    CHECK(!f.Send(Effect::AcquireReserve,f.goal,20,Op::AttachContainer));CHECK(f.Total()==105);
    Fixture g;g.goal.container=g.original.container;
    g.ledger.emplace(g.profile,g.owner,g.mechanism,g.goal,500);CHECK(g.Bind());
    c=g.Send(Effect::ExtractLoaded,g.original,25);CHECK(c&&g.Finish(*c));
    CHECK(!g.Send(Effect::AcquireReserve,g.goal,20,Op::AttachContainer));CHECK(g.Total()==105);return 0;
}
int InterruptedTransferRetainsLateAcceptedEvidence(){
    Fixture f;CHECK(f.Bind());const auto c=f.Send(Effect::ExtractLoaded,f.original,25);CHECK(c);
    auto receipt=f.Receipt(*c);f.ledger->Suspend();f.now+=1000000;
    CHECK(f.ledger->State().pending==c&&f.Held(f.original)==-1);
    CHECK(!f.ledger->Retire(receipt.post,f.now));CHECK(f.ledger->State().pending==c);
    CHECK(f.ledger->Complete(receipt,f.now));f.sample=receipt.post;
    CHECK(f.ledger->State().suspended&&f.Held(f.original)==25&&f.Total()==105);
    CHECK(!f.Send(Effect::LoadSupply,f.original,25,Op::SeatFeed));
    f.Tick();CHECK(f.ledger->Resume(f.sample,f.now));
    const auto returned=f.Send(Effect::LoadSupply,f.original,25,Op::SeatFeed);CHECK(returned&&f.Finish(*returned));
    CHECK(f.Total()==105&&f.sample.counts.loaded==25);return 0;
}
int InexactReceiptNeverDropsPendingRounds(){
    for(unsigned mismatch=0;mismatch<16;++mismatch){
        Fixture f;CHECK(f.Bind());const auto c=f.Send(Effect::ExtractLoaded,f.original,25);CHECK(c);
        const auto good=f.Receipt(*c);auto bad=good;
        switch(mismatch){
        case 0:++bad.command.request.id;break;case 1:++bad.acknowledgement.request.lifetime;break;
        case 2:bad.acknowledgement.status=FeedReloadAcknowledgement::None;break;
        case 3:bad.authorityVerified=false;break;case 4:bad.copiesVerified=false;break;
        case 5:bad.authorityInvocation=0;break;case 6:++bad.post.counts.loaded;break;
        case 7:++bad.post.counts.reserve;break;case 8:++bad.post.owner.actor;break;
        case 9:++bad.post.lifetime;break;case 10:bad.post.loadedSupply=f.goal;break;
        case 11:++bad.acknowledgement.snapshotSequence;break;case 12:--bad.post.sequence;break;
        case 13:++bad.acknowledgement.observedNs;break;case 14:--bad.beganNs;break;
        case 15:++bad.completedNs;break;
        }
        CHECK(!f.ledger->Complete(bad,f.now));CHECK(f.ledger->State().pending==c);
        CHECK(f.Held(f.original)==-1&&f.Total()==105);
        CHECK(f.ledger->Complete(good,f.now));CHECK(f.Held(f.original)==25&&f.Total()==105);
    }return 0;
}
int RejectedTransferRequiresUnchangedCountProof(){
    Fixture f;CHECK(f.Bind());const auto c=f.Send(Effect::ExtractLoaded,f.original,25);CHECK(c);
    auto receipt=f.Receipt(*c,false);auto wrong=receipt;wrong.post.counts.loaded=0;
    CHECK(!f.ledger->Complete(wrong,f.now));CHECK(f.ledger->State().pending==c);
    wrong=receipt;wrong.noTransferEntry=false;
    CHECK(!f.ledger->Complete(wrong,f.now)&&f.ledger->State().pending==c);
    CHECK(f.ledger->Complete(receipt,f.now));f.sample=receipt.post;
    CHECK(f.sample.counts.loaded==25&&f.Held(f.original)==-1&&f.Total()==105);
    CHECK(f.ledger->State().acceptedOperations==0);
    const auto retry=f.Send(Effect::ExtractLoaded,f.original,25);CHECK(retry&&f.Finish(*retry));
    CHECK(f.Held(f.original)==25&&f.Total()==105);return 0;
}
int RetirementKeepsCustodyAndLifetimeBoundary(){
    Fixture f;CHECK(f.Bind());const auto c=f.Send(Effect::ExtractLoaded,f.original,25);CHECK(c&&f.Finish(*c));
    const auto receipt=*f.ledger->State().lastReceipt;
    CHECK(!f.ledger->Retire(f.sample,f.now));f.Tick();auto foreign=f.sample;++foreign.lifetime;
    CHECK(!f.ledger->Retire(foreign,f.now));CHECK(f.ledger->Retire(f.sample,f.now));
    CHECK(f.ledger->State().retired&&f.Held(f.original)==25&&f.Total()==105);
    f.Tick();CHECK(!f.ledger->Resume(f.sample,f.now));CHECK(!f.Bind());
    CHECK(!f.Send(Effect::LoadSupply,f.original,25,Op::SeatFeed));
    FeedAmmunition next(f.profile,f.owner,f.mechanism,f.goal,501);
    CHECK(!next.Complete(receipt,f.now));CHECK(!next.Bind(f.sample,f.now));
    f.sample.lifetime=501;CHECK(next.Bind(f.sample,f.now));CHECK(!next.Complete(receipt,f.now));
    CHECK(f.Held(f.original)==25);return 0;
}
int CountsAreBoundedAndObservationsCannotRefill(){
    for(unsigned invalid=0;invalid<7;++invalid){
        Fixture f;
        switch(invalid){case 0:f.sample.counts.loaded=-1;break;case 1:f.sample.counts.reserve=-1;break;
        case 2:f.sample.counts.capacity=0;break;case 3:f.sample.counts.loaded=51;break;
        case 4:f.sample.counts.capacity=1000001;break;case 5:f.sample.counts.reserve=1000001;break;
        case 6:f.sample.loadedSupply={};break;}
        CHECK(!f.Bind());
    }
    Fixture f;CHECK(f.Bind());CHECK(!f.Bind());
    CHECK(!f.Send(Effect::ExtractLoaded,f.original,26));CHECK(!f.Send(Effect::AcquireReserve,f.goal,81,Op::AttachContainer));
    CHECK(!f.Send(Effect::AcquireReserve,f.goal,-1,Op::AttachContainer));
    auto c=f.Send(Effect::ExtractLoaded,f.original,25);CHECK(c&&f.Finish(*c));
    CHECK(!f.Send(Effect::LoadSupply,f.original,26,Op::SeatFeed));
    f.Tick();f.sample.counts.loaded=50;CHECK(!f.ledger->Resume(f.sample,f.now));
    CHECK(!f.Send(Effect::PhysicalOnly,{},0,Op::CloseCover));CHECK(f.Held(f.original)==25&&f.Total()==105);return 0;
}
int OnlyExactFreshAdmissionCanDispatch(){
    for(unsigned invalid=0;invalid<7;++invalid){
        Fixture f;CHECK(f.Bind());auto request=f.Request(Effect::ExtractLoaded,f.original,25);
        switch(invalid){case 0:f.now=f.sample.deadlineNs;break;case 1:--f.now;break;
        case 2:++f.sample.counts.reserve;break;case 3:++f.sample.deadlineNs;break;
        case 4:++request.completion.candidate.snapshotSequence;break;
        case 5:request.completion.completedNs=0;break;case 6:f.sample.coherentIdleVerified=false;break;}
        CHECK(!f.ledger->Dispatch(request,f.authority,f.sample,f.now));CHECK(f.Total()==105);
    }return 0;
}
}
int main(){
    if(PartialReplacementRetainsEveryRemainder()||PartialOriginalReturnsWithoutReserveGrant()||
       EmptyBeltHasIdentityWithoutSyntheticRounds()||NoImplicitTransferAtAnyPhysicalStage()||
       TransferStageComesFromExplicitAuthority()||AtomicNativeRefillDoesNotInventAnIntermediateTransfer()||DispatchAndReceiptsAreExactlyOnce()||
       WrongIdentityOrAuthorityCannotSpend()||ResourceAndContainerCannotAlias()||
       InterruptedTransferRetainsLateAcceptedEvidence()||InexactReceiptNeverDropsPendingRounds()||
       RejectedTransferRequiresUnchangedCountProof()||RetirementKeepsCustodyAndLifetimeBoundary()||
       CountsAreBoundedAndObservationsCannotRefill()||OnlyExactFreshAdmissionCanDispatch())return 1;
    std::puts("FeedAmmunitionTests: 15 deterministic synthetic conservation cases passed (no native/headset claim)");return 0;
}
