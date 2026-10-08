#include "Test.h"
#include <array>
#include "fvr/interaction/AmmunitionLedger.h"
using namespace fvr::interaction;
namespace {
constexpr std::int64_t Start=1000000000;
AmmunitionSnapshot Sample(int loaded=22,int reserve=191,int capacity=30){
    return {{{1,2,3,4},5,6},1,Start,Start+100000000,{loaded,reserve,capacity},true};
}
AmmunitionSnapshot Next(const AmmunitionLedger& ledger){
    auto s=ledger.Snapshot();++s.sequence;s.observedNs+=1000000;s.deadlineNs=s.observedNs+100000000;return s;
}
AmmunitionReceipt Receipt(const AmmunitionCommand& c,std::int64_t delay=20000000){
    return {c,c.id+100,c.requestedNs+1000,c.requestedNs+delay,c.before,c.after,true,true};
}
int Complete(AmmunitionLedger& ledger,AmmunitionOperation op,AmmunitionSnapshot s,
             std::optional<RemovedMagazineResource> original={}){
    const auto command=ledger.Queue(op,s,s.observedNs,original);CHECK(command);
    CHECK(ledger.Dispatch(s,s.observedNs)==command);
    CHECK(!ledger.Dispatch(s,s.observedNs)); // no second native invocation
    auto receipt=Receipt(*command);
    CHECK(ledger.Complete(receipt,receipt.completedNs));
    auto resource=ledger.Original();auto counts=ledger.Snapshot().counts;
    CHECK(ledger.Complete(receipt,receipt.completedNs+1)); // duplicate cannot credit ammo
    CHECK(ledger.Original()==resource&&ledger.Snapshot().counts==counts);
    return 0;
}
int RemoveReturnAndDiscard(){
    for(int capacity:{1,8,15,30,32,50,100,200})for(int loaded:{0,capacity/2,capacity})for(int reserve:{0,1,191}){
        AmmunitionLedger ledger;auto s=Sample(loaded,reserve,capacity);CHECK(ledger.Bind(s,Start));
        CHECK(Complete(ledger,AmmunitionOperation::RemoveMagazine,s)==0);
        CHECK(ledger.WellEmpty()&&ledger.Original()->rounds==loaded);
        CHECK((ledger.Snapshot().counts==AmmunitionCounts{0,reserve,capacity}));
        auto original=*ledger.Original();
        CHECK(!ledger.Queue(AmmunitionOperation::RefillMagazine,Next(ledger),Next(ledger).observedNs));
        auto wrong=original;++wrong.id;
        CHECK(!ledger.Queue(AmmunitionOperation::ReturnMagazine,Next(ledger),Next(ledger).observedNs,wrong));
        CHECK(Complete(ledger,AmmunitionOperation::ReturnMagazine,Next(ledger),original)==0);
        CHECK(!ledger.WellEmpty()&&ledger.Snapshot().counts==s.counts);
        CHECK(Complete(ledger,AmmunitionOperation::RemoveMagazine,Next(ledger))==0);
        CHECK(ledger.Original()->id!=original.id);
        CHECK(!ledger.Discard(original));
        CHECK(ledger.Discard(*ledger.Original()));
        CHECK(!ledger.Discard(*ledger.Original()));
        CHECK((ledger.Snapshot().counts==AmmunitionCounts{0,reserve,capacity}));
        CHECK(!ledger.Queue(AmmunitionOperation::ReturnMagazine,Next(ledger),Next(ledger).observedNs,ledger.Original()));
        if(reserve){
            CHECK(Complete(ledger,AmmunitionOperation::RefillMagazine,Next(ledger))==0);
            CHECK(ledger.Snapshot().counts.loaded==std::min(capacity,reserve));
            CHECK(ledger.Snapshot().counts.loaded+ledger.Snapshot().counts.reserve==reserve);
        }else CHECK(!ledger.Queue(AmmunitionOperation::RefillMagazine,Next(ledger),Next(ledger).observedNs));
    }return 0;
}
int StableResourceAcrossHolsterAndSpace(){
    AmmunitionLedger ledger;auto s=Sample();CHECK(ledger.Bind(s,Start));
    CHECK(Complete(ledger,AmmunitionOperation::RemoveMagazine,s)==0);const auto original=*ledger.Original();
    auto rebound=Next(ledger);++rebound.context.equipGeneration;++rebound.context.space;
    CHECK(ledger.Bind(rebound,rebound.observedNs));
    CHECK(ledger.Original()==original&&ledger.ResourceState()==MagazineResourceState::Held);
    auto foreign=Next(ledger);++foreign.context.resource.weaponGeneration;
    CHECK(!ledger.Bind(foreign,foreign.observedNs));
    auto filled=Next(ledger);filled.counts.loaded=30;
    CHECK(!ledger.Bind(filled,filled.observedNs)); // stock refill cannot hide missing magazine
    CHECK(Complete(ledger,AmmunitionOperation::ReturnMagazine,rebound,original)==0);
    CHECK(ledger.Snapshot().counts==s.counts);
    return 0;
}
int AmbiguousEffectsAreNotRetried(){
    AmmunitionLedger ledger;auto s=Sample();CHECK(ledger.Bind(s,Start));
    auto c=ledger.Queue(AmmunitionOperation::RemoveMagazine,s,Start);CHECK(c);
    CHECK(ledger.Dispatch(s,Start));ledger.Cancel();
    CHECK(ledger.Phase()==AmmunitionLedgerPhase::NeedsReconciliation&&!ledger.Original());
    CHECK(!ledger.Dispatch(s,Start)&&!ledger.Bind(s,Start)&&!ledger.Queue(AmmunitionOperation::RemoveMagazine,s,Start));
    auto r=Receipt(*c);r.copiesVerified=false;CHECK(!ledger.Complete(r,r.completedNs));
    r=Receipt(*c,150000000); // valid convergence can arrive after controller admission expires
    CHECK(ledger.Complete(r,r.completedNs));
    CHECK(ledger.Original()->rounds==22&&ledger.Snapshot().counts.loaded==0);
    const auto old=r;const auto next=Next(ledger);
    const auto returned=ledger.Queue(AmmunitionOperation::ReturnMagazine,next,next.observedNs,ledger.Original());CHECK(returned);
    CHECK(!ledger.Complete(old,next.observedNs));
    CHECK(ledger.Phase()==AmmunitionLedgerPhase::Queued&&ledger.Pending()==returned);
    CHECK(ledger.Dispatch(next,next.observedNs));
    CHECK(!ledger.Complete(old,next.observedNs));
    const auto receipt=Receipt(*returned);CHECK(ledger.Complete(receipt,receipt.completedNs));
    return 0;
}
int InputAndReceiptRejection(){
    for(unsigned reason=0;reason<8;++reason){
        AmmunitionLedger ledger;auto s=Sample();CHECK(ledger.Bind(s,Start));
        const auto c=ledger.Queue(AmmunitionOperation::RemoveMagazine,s,Start);CHECK(c);
        auto changed=s;
        if(reason==0)++changed.context.equipGeneration;
        if(reason==1)++changed.context.space;
        if(reason==2)++changed.context.resource.actorGeneration;
        if(reason==3)changed.counts.loaded--;
        if(reason==4)changed.counts.reserve--;
        if(reason==5)changed.coherentIdleVerified=false;
        if(reason==6)changed.sequence=0;
        if(reason==7)changed.deadlineNs=Start;
        CHECK(!ledger.Dispatch(changed,Start));CHECK(!ledger.Original());
        CHECK(ledger.Phase()==AmmunitionLedgerPhase::Ready&&!ledger.Pending());
    }
    for(unsigned reason=0;reason<7;++reason){
        AmmunitionLedger ledger;auto s=Sample();CHECK(ledger.Bind(s,Start));
        const auto c=ledger.Queue(AmmunitionOperation::RemoveMagazine,s,Start);CHECK(c&&ledger.Dispatch(s,Start));
        auto r=Receipt(*c);
        if(reason==0)r.authorityVerified=false;
        if(reason==1)r.copiesVerified=false;
        if(reason==2)r.authorityInvocation=0;
        if(reason==3)++r.after.reserve;
        if(reason==4)r.beganNs=Start-1;
        if(reason==5)r.completedNs=c->deadlineNs;
        if(reason==6)r.after.loaded=22;
        CHECK(!ledger.Complete(r,r.completedNs));CHECK(!ledger.Original());
        CHECK(ledger.Phase()==AmmunitionLedgerPhase::NeedsReconciliation&&!ledger.Dispatch(s,Start));
    }
    AmmunitionLedger ledger;auto s=Sample();CHECK(ledger.Bind(s,Start));
    auto c=ledger.Queue(AmmunitionOperation::RemoveMagazine,s,Start);CHECK(c);
    ledger.Expire(s.deadlineNs);CHECK(ledger.Phase()==AmmunitionLedgerPhase::Ready);
    CHECK(!ledger.Complete(Receipt(*c),Start+20000000)&&!ledger.Original());
    return 0;
}
int ShellsUseTheSameConservationRules(){
    for(int capacity:{1,5,8,15}){
        AmmunitionLedger ledger;auto s=Sample(0,capacity+4,capacity);CHECK(ledger.Bind(s,Start));
        for(int loaded=0;loaded<capacity;++loaded){
            const auto before=ledger.Snapshot().counts;
            CHECK(Complete(ledger,AmmunitionOperation::InsertRound,loaded?Next(ledger):s)==0);
            const auto after=ledger.Snapshot().counts;
            CHECK(after.loaded==loaded+1&&after.loaded+after.reserve==before.loaded+before.reserve);
            CHECK(!ledger.Original()&&!ledger.WellEmpty());
        }
        CHECK(!ledger.Queue(AmmunitionOperation::InsertRound,Next(ledger),Next(ledger).observedNs));
    }return 0;
}
int IndependentWeaponsDuringInterruptedOperations(){
    // Mock native receipts: this exercises shared custody, not a BC2 inventory binding.
    std::array<AmmunitionLedger,4> weapons;
    std::array<AmmunitionCommand,4> commands;
    std::array<RemovedMagazineResource,4> originals;
    const std::array<int,4> capacities{15,30,32,100};
    for(unsigned n=0;n<weapons.size();++n){
        auto s=Sample(int(n+1),100,capacities[n]);s.context.resource.weapon+=n;
        auto& w=weapons[n];CHECK(w.Bind(s,Start));
        const auto c=w.Queue(AmmunitionOperation::RemoveMagazine,s,Start);CHECK(c&&w.Dispatch(s,Start));
        commands[n]=*c;w.Cancel(); // selected weapon switches before its replication finishes
        CHECK(w.Phase()==AmmunitionLedgerPhase::NeedsReconciliation&&!w.Original());
    }
    for(unsigned n=0;n<weapons.size();++n){
        auto& w=weapons[n];const auto foreign=Receipt(commands[(n+1)%weapons.size()]);
        CHECK(!w.Complete(foreign,foreign.completedNs));
        const auto own=Receipt(commands[n],150000000);CHECK(w.Complete(own,own.completedNs));
        originals[n]=*w.Original();CHECK(originals[n].rounds==int(n+1));
    }
    for(int n=3;n>=0;--n){
        auto& w=weapons[n];auto s=Next(w);s.context.equipGeneration+=20;s.context.space+=3;
        CHECK(w.Bind(s,s.observedNs));
        CHECK(!w.Queue(AmmunitionOperation::ReturnMagazine,s,s.observedNs,originals[(n+1)%4]));
        CHECK(Complete(w,AmmunitionOperation::ReturnMagazine,s,originals[n])==0);
        CHECK(w.Snapshot().counts.loaded==n+1&&w.Snapshot().counts.reserve==100);
    }
    return 0;
}
}
int main(){
    CHECK(RemoveReturnAndDiscard()==0);CHECK(StableResourceAcrossHolsterAndSpace()==0);
    CHECK(AmbiguousEffectsAreNotRetried()==0);CHECK(InputAndReceiptRejection()==0);
    CHECK(ShellsUseTheSameConservationRules()==0);
    CHECK(IndependentWeaponsDuringInterruptedOperations()==0);
    std::puts("AmmunitionLedger: native command accounting, original return, discard, rebind and ambiguous effects passed");return 0;
}
