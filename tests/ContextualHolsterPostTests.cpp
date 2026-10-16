#include "KnifeInventoryEntryFixture.inc"
#include <memory>
namespace {
using namespace fvr::bc2;
using namespace fvr::interaction;
constexpr unsigned Tool=0x13400,Data=0x14800;
void AddTool(HolsterRun& h){
    h.r.f.Word(Fixture::array+4*4,Tool);h.r.f.Word(Tool+4,Data);h.r.f.Word(Data,Fixture::table);
    h.r.f.Word(Data+0x84,12);h.r.f.Word(Data+0xc,0x2b200);h.r.f.Text(0x2b200,"LZ-537");h.r.f.types[Data]="SoldierWeaponData";
    for(auto& hand:h.r.s.input.hands){hand.active=Components;hand.gripTracked=hand.aimTracked=true;}
}
void Evaluate(HolsterRun& h,std::optional<BodyHolsterSample>& evaluated){
    auto outer=h.Sample();
    if(const auto demand=h.holster.Demand(outer)){HolsterInputOverride writer;
        if(writer.Apply(h.cache,*demand,{nullptr,[](void*,const HolsterSuppressionRequest&)noexcept{return true;}}))outer.suppression=writer.Commit();}
#ifdef FVR_TEST_OLD_INCOMPLETE_POST
    h.out=h.r.adapter.TickHolster(h.r.f.Memory(),h.r.s,outer,h.holster,h.r.hands,h.r.intent);
    evaluated=outer; // Exact frozen404 Gameplay handoff after the by-value call.
#else
    h.out=h.r.adapter.TickHolster(h.r.f.Memory(),h.r.s,outer,h.holster,h.r.hands,h.r.intent,&evaluated);
#endif
    h.last=outer;
}
int InitialHideCarriesTheEvaluatedSample(){
    auto storage=std::make_unique<HolsterRun>();auto& h=*storage;AddTool(h);h.Step();CHECK(h.Empty());h.Step(0);
    std::optional<BodyHolsterSample> completed;Evaluate(h,completed);
    CHECK(h.out.freeRight&&h.holster.Phase()==BodyHolsterPhase::Empty&&completed);
    // Frozen404 fails here although the real holster produced a paired hide.
    CHECK(BodyFreeRightCurrent(*h.out.freeRight,*completed,h.r.hands));
    CHECK(!BodyFreeRightCurrent(*h.out.freeRight,h.last,h.r.hands));
    CHECK(completed->hand.sequence==h.r.s.hand.sequence&&completed->hand.observedNs==h.r.s.hand.observedNs&&
          completed->hand.deadlineNs==h.r.s.hand.deadlineNs&&completed->hand.nowNs==h.r.s.hand.nowNs);
    for(unsigned bad=0;bad<4;++bad){auto wrong=*completed;
        if(bad==0)wrong.hand.nowNs=wrong.body.nowNs=wrong.hand.deadlineNs;
        if(bad==1)++wrong.body.inventory.owner.generation;
        if(bad==2)++wrong.nativeTick;
        if(bad==3)wrong.body.focused=false;
        CHECK(!BodyFreeRightCurrent(*h.out.freeRight,wrong,h.r.hands));}
    return 0;
}
int ActualToolReturnRequiresTheNewPairedHide(){
    auto storage=std::make_unique<HolsterRun>();auto& h=*storage;AddTool(h);h.Step();CHECK(h.Empty());h.Step(0);
    h.r.s.input.hands[1].grip.position={.12f,1.45f,-.15f};h.r.Advance(1);
    std::optional<BodyHolsterSample> completed;Evaluate(h,completed);CHECK(completed&&h.out.freeRight);
    const auto original=*h.out.freeRight;const auto key=h.r.adapter.ItemKey(Tool);CHECK(key);
    const auto claim=h.r.hands.Acquire(h.r.s.hand,{h.r.s.hand.owner,InteractionHand::Right,HandClaimKind::BodyInventory,
        {key->id,key->generation},{{5,1},h.r.s.hand.sequence,h.r.s.hand.deadlineNs,true},++h.r.intent,0});CHECK(claim.claim);
    constexpr std::uint64_t operation=17;
    CHECK(h.r.adapter.BeginTemporaryTool(h.r.f.Memory(),h.r.s,*completed,h.holster,*key,operation,original,claim.claim->token,h.r.hands));
    const auto select=[&](unsigned slot,unsigned weapon){
        h.r.f.Word(Fixture::inventory+0x14c,slot);h.r.f.owner.weapon=weapon;h.r.s.owner.weapon=weapon;++h.r.s.owner.equipGeneration;
        ++h.r.s.hand.owner.equipGeneration;h.r.s.gun={weapon,h.r.s.hand.owner.equipGeneration};h.r.Advance(0);Evaluate(h,completed);
    };
    select(4,Tool);CHECK(h.holster.TemporaryToolActive()&&!h.out.freeRight);
    CHECK(h.holster.RequestTemporaryToolRehide(operation));select(0,Fixture::a);
    CHECK(h.holster.Phase()==BodyHolsterPhase::HidePending&&!h.out.freeRight);
    CHECK(h.holster.RequestId()>original.visibility.request);
    for(unsigned n=0;n<3&&!h.out.freeRight;++n){h.r.Advance(0);Evaluate(h,completed);}
    CHECK(h.out.freeRight&&completed&&h.holster.LastTemporaryToolReturn()==operation&&!h.holster.TemporaryToolActive());
    // This is the same strict predicate used by contextual Tick retirement and PublishProbe.
    CHECK(BodyFreeRightCurrent(*h.out.freeRight,*completed,h.r.hands));
    CHECK(!BodyFreeRightCurrent(original,*completed,h.r.hands));
    CHECK(!BodyFreeRightCurrent(*h.out.freeRight,h.last,h.r.hands));
    return 0;
}
int UnevaluatedAndFailedSamplesCannotRetainAuthority(){
    auto storage=std::make_unique<HolsterRun>();auto& h=*storage;AddTool(h);h.Step();CHECK(h.Empty());h.Step(0);
    std::optional<BodyHolsterSample> completed;Evaluate(h,completed);CHECK(completed&&h.out.freeRight);
    const auto old=*h.out.freeRight;
    auto ordinaryStorage=std::make_unique<HolsterRun>();auto& ordinary=*ordinaryStorage;
    ordinary.scopedXm8=true;ordinary.invalidMetadata=1;ordinary.r.Advance();Evaluate(ordinary,completed);
    CHECK(!completed); // No holster evaluation: the preceding output was cleared.
    h.r.f.denyMap=true;h.r.Advance();Evaluate(h,completed);
    CHECK(!h.out.freeRight&&(!completed||!BodyFreeRightCurrent(old,*completed,h.r.hands)));
    return 0;
}
}
int main(){
    CHECK(InitialHideCarriesTheEvaluatedSample()==0);
    CHECK(ActualToolReturnRequiresTheNewPairedHide()==0);
    CHECK(UnevaluatedAndFailedSamplesCannotRetainAuthority()==0);
    std::puts("Contextual holster post sample PASS: actual inventory/paired hide, native-selected tool return/new hide, stale/foreign/unevaluated rejection.");
}
