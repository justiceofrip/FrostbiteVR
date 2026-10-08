#include "Test.h"
#include "fvr/interaction/AmmunitionInventory.h"
using namespace fvr::interaction;
namespace {
constexpr std::int64_t Ms=1000000;
struct Fixture {
    AmmunitionInventory<> inventory;
    std::uint64_t sequence=0,intent=0;std::int64_t now=1000*Ms;
    AmmunitionSnapshot Sample(unsigned weapon,int loaded=22,int reserve=191,int capacity=30,unsigned equip=1,unsigned space=1){
        now+=Ms;return {{{1,2,weapon,4},equip,space},++sequence,now,now+100*Ms,{loaded,reserve,capacity},true};
    }
    std::optional<AmmunitionCommand> Queue(const AmmunitionSnapshot& s,AmmunitionOperation op,
                                          std::optional<RemovedMagazineResource> original={}){
        return inventory.Submit({s.context,++intent,now,now+80*Ms,op,original},s,now);
    }
    AmmunitionReceipt Receipt(const AmmunitionCommand& command){
        const auto begin=now;now+=20*Ms;return {command,command.id+100,begin,now,command.before,command.after,true,true};
    }
    int Finish(const AmmunitionSnapshot& s,AmmunitionOperation op,std::optional<RemovedMagazineResource> original={}){
        CHECK(inventory.Select(s,now));const auto c=Queue(s,op,original);CHECK(c);
        CHECK(inventory.Dispatch(*c,s,now)==c);const auto r=Receipt(*c);CHECK(inventory.Complete(r,now));return 0;
    }
};
int AllItemsKeepTheirOwnMagazine(){
    Fixture f;std::array<RemovedMagazineResource,50> originals;
    for(unsigned n=0;n<50;++n){
        const int capacity=std::array<int,5>{8,15,30,32,100}[n%5];const int loaded=int(n)%(capacity+1);
        const auto s=f.Sample(n+1,loaded,200,capacity);CHECK(f.Finish(s,AmmunitionOperation::RemoveMagazine)==0);
        originals[n]=*f.inventory.Find(s.context.resource)->Original();
        CHECK(originals[n].rounds==loaded&&f.inventory.Find(s.context.resource)->WellEmpty());
    }
    CHECK(f.inventory.Size()==50);
    for(unsigned n=50;n-->0;){
        const auto& original=originals[n];auto s=f.Sample(n+1,0,200,original.capacity,100+n,4);
        CHECK(f.inventory.Select(s,f.now));
        CHECK(!f.Queue(s,AmmunitionOperation::ReturnMagazine,originals[(n+1)%50]));
        if(n%2){
            CHECK(f.Finish(s,AmmunitionOperation::ReturnMagazine,original)==0);
            CHECK(f.inventory.Find(s.context.resource)->Snapshot().counts.loaded==original.rounds);
        }else{
            CHECK(f.inventory.Discard(original));CHECK(!f.inventory.Discard(original));
            CHECK(f.Finish(s,AmmunitionOperation::RefillMagazine)==0);
            const auto c=f.inventory.Find(s.context.resource)->Snapshot().counts;CHECK(c.loaded+c.reserve==200);
        }
    }
    return 0;
}
int InFlightCompletionSurvivesSelection(){
    Fixture f;auto a=f.Sample(1);CHECK(f.inventory.Select(a,f.now));
    const auto remove=f.Queue(a,AmmunitionOperation::RemoveMagazine);CHECK(remove&&f.inventory.Dispatch(*remove,a,f.now));
    auto b=f.Sample(2,7,24,8);CHECK(f.inventory.Select(b,f.now));
    CHECK(f.inventory.Find(a.context.resource)->Phase()==AmmunitionLedgerPhase::NeedsReconciliation);
    const auto shell=f.Queue(b,AmmunitionOperation::InsertRound);CHECK(shell&&f.inventory.Dispatch(*shell,b,f.now));
    auto receipt=f.Receipt(*remove);CHECK(f.inventory.Complete(receipt,f.now));
    CHECK(f.inventory.Find(b.context.resource)->Pending()==shell);
    const auto original=*f.inventory.Find(a.context.resource)->Original();
    receipt=f.Receipt(*shell);CHECK(f.inventory.Complete(receipt,f.now));
    CHECK(f.inventory.Find(b.context.resource)->Snapshot().counts.loaded==8);
    a=f.Sample(1,0,191,30,10,2);CHECK(f.Finish(a,AmmunitionOperation::ReturnMagazine,original)==0);
    CHECK(f.inventory.Find(a.context.resource)->Snapshot().counts.loaded==22);
    return 0;
}
int NoStaleDispatchOrAutomaticRetry(){
    Fixture f;auto a=f.Sample(1);CHECK(f.inventory.Select(a,f.now));
    const AmmunitionIntent gesture{a.context,++f.intent,f.now,f.now+5*Ms,AmmunitionOperation::RemoveMagazine,{}};
    auto c=f.inventory.Submit(gesture,a,f.now);CHECK(c&&c->admissionDeadlineNs==gesture.deadlineNs);
    CHECK(!f.inventory.Submit(gesture,a,f.now));
    CHECK(!f.inventory.Dispatch(*c,a,f.now+6*Ms));
    CHECK(!f.inventory.Submit(gesture,a,f.now));
    c=f.Queue(a,AmmunitionOperation::RemoveMagazine);CHECK(c);
    auto b=f.Sample(2);CHECK(f.inventory.Select(b,f.now));
    CHECK(!f.inventory.Dispatch(*c,a,f.now));
    CHECK(f.inventory.Find(a.context.resource)->Phase()==AmmunitionLedgerPhase::Ready);
    CHECK(!f.inventory.Complete(f.Receipt(*c),f.now));
    const auto selected=f.inventory.Selected();CHECK(selected&&*selected==b.context);
    CHECK(!f.inventory.Select(a,f.now));CHECK(!f.inventory.Selected());
    CHECK(!f.Queue(b,AmmunitionOperation::RemoveMagazine));
    b=f.Sample(2);CHECK(f.Finish(b,AmmunitionOperation::RemoveMagazine)==0);
    return 0;
}
int ReplacedItemsAndCapacityCannotStealOldRounds(){
    Fixture f;auto a=f.Sample(1);CHECK(f.Finish(a,AmmunitionOperation::RemoveMagazine)==0);
    const auto original=*f.inventory.Find(a.context.resource)->Original();
    auto replaced=f.Sample(1);++replaced.context.resource.weaponGeneration;
    CHECK(f.inventory.Select(replaced,f.now));CHECK(f.inventory.Size()==2);
    CHECK(!f.Queue(replaced,AmmunitionOperation::ReturnMagazine,original));
    CHECK(f.inventory.Find(a.context.resource)->Original()==original);
    auto old=f.Sample(1,30);CHECK(!f.inventory.Select(old,f.now)); // illegal native refill while well empty
    old=f.Sample(1,0,191,32);CHECK(!f.inventory.Select(old,f.now));
    old=f.Sample(1,0);CHECK(f.Finish(old,AmmunitionOperation::ReturnMagazine,original)==0);
    AmmunitionInventory<1> bounded;auto one=f.Sample(10);CHECK(bounded.Select(one,f.now));
    auto two=f.Sample(11);CHECK(!bounded.Select(two,f.now)&&bounded.Size()==1&&!bounded.Selected());
    CHECK(bounded.Find(one.context.resource)&&!bounded.Find(two.context.resource));
    return 0;
}
}
int main(){
    CHECK(AllItemsKeepTheirOwnMagazine()==0);CHECK(InFlightCompletionSurvivesSelection()==0);
    CHECK(NoStaleDispatchOrAutomaticRetry()==0);CHECK(ReplacedItemsAndCapacityCannotStealOldRounds()==0);
    std::puts("AmmunitionInventory: 50 independent items, returns, discards, swaps and late completions passed (mock native receipts)");
    return 0;
}
