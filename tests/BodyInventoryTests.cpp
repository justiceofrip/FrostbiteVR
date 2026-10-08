#include "Test.h"
#include "fvr/interaction/BodyInventory.h"
#include <algorithm>
using namespace fvr::interaction;
namespace {
constexpr std::int64_t ms=1000000;
constexpr BodyItemKey rifle{101,1},shotgun{202,1};
constexpr BodySlotId back=1,chest=2;
struct Fixture {
    BodyInventory policy{{250*ms,100*ms}};
    std::vector<BodyInventoryItem> items{{rifle,{chest,back},2,10},{shotgun,{back,chest},2,20}};
    BodyInventorySample sample{};
    Fixture(){sample.inventory.owner={11,12,13};sample.inventory.revision=1;
        sample.inventory.sequence=1;sample.inventory.observedNs=sample.nowNs=1000*ms;
        sample.inventory.items=items;sample.inventory.selected=rifle;
        sample.inventory.presentation=BodyPresentation::WeaponVisible;
        sample.focused=sample.handTracked=true;policy.Update(sample);}
    BodyInventoryResult Tick(std::int64_t delta=10*ms){sample.nowNs+=delta;
        sample.inventory.observedNs=sample.nowNs;++sample.inventory.sequence;sample.inventory.items=items;return policy.Update(sample);}
    BodyInventoryResult Intent(BodyInventoryOperation op,BodySlotId slot,BodyItemKey item){
        ++sample.intent.serial;sample.intent.operation=op;sample.intent.slot=slot;sample.intent.item=item;return Tick();}
    void Neutral(){sample.intent.operation=BodyInventoryOperation::None;sample.acknowledgement.reset();Tick();}
    void Ack(const BodyInventoryRequest& r){sample.acknowledgement=BodyInventoryAcknowledgement{r.id,r.revision,r.owner,r.operation,r.item,true};}
    void Empty(){sample.inventory.presentation=BodyPresentation::EmptyHands;sample.inventory.fireSuppressed=true;}
    void Visible(BodyItemKey item){sample.inventory.selected=item;sample.inventory.presentation=BodyPresentation::WeaponVisible;sample.inventory.fireSuppressed=false;}
};
int HolsterAndDraw(){
    Fixture f;auto out=f.Intent(BodyInventoryOperation::Holster,chest,rifle);
    CHECK(out.request&&out.blockFire&&out.phase==BodyInventoryPhase::AwaitingHolster&&!out.emptyHands);
    const auto holster=*out.request;CHECK(!f.Tick().request);
    f.Empty();out=f.Tick();CHECK(!out.committedRequest&&out.phase==BodyInventoryPhase::AwaitingHolster); // State alone is not ack.
    f.Ack(holster);out=f.Tick();CHECK(out.committedRequest==holster.id&&out.emptyHands&&out.blockFire&&!out.held);
    CHECK(!f.Tick().committedRequest);f.Neutral();
    out=f.Intent(BodyInventoryOperation::Draw,back,shotgun);CHECK(out.request&&out.emptyHands&&out.blockFire);
    const auto draw=*out.request;CHECK(draw.id>holster.id);
    f.Ack(draw);out=f.Tick();CHECK(!out.committedRequest); // Ack alone is not actual selection.
    f.Visible(shotgun);out=f.Tick();CHECK(out.committedRequest==draw.id&&out.held==shotgun&&!out.blockFire&&!out.emptyHands);
    return 0;
}
int ExactAcknowledgement(){
    for(unsigned wrong=0;wrong<6;++wrong){
        Fixture f;auto out=f.Intent(BodyInventoryOperation::Holster,chest,rifle);CHECK(out.request);auto command=*out.request;
        f.Empty();f.Ack(command);
        if(wrong==0)++f.sample.acknowledgement->request;
        if(wrong==1)++f.sample.acknowledgement->owner.actor;
        if(wrong==2)++f.sample.acknowledgement->revision;
        if(wrong==3)++f.sample.acknowledgement->item.generation;
        if(wrong==4)f.sample.acknowledgement->operation=BodyInventoryOperation::Draw;
        if(wrong==5)++f.sample.acknowledgement->owner.space;
        CHECK(!f.Tick().committedRequest);f.Ack(command);CHECK(f.Tick().committedRequest==command.id);
    }
    Fixture reject;auto r=reject.Intent(BodyInventoryOperation::Holster,chest,rifle);reject.Ack(*r.request);
    reject.sample.acknowledgement->accepted=false;r=reject.Tick();
    CHECK(r.cancelledRequest&&r.reason==BodyInventoryReason::NativeRejected&&!r.committedRequest&&r.blockFire);return 0;
}
int PickupReplacement(){
    Fixture f;auto out=f.Intent(BodyInventoryOperation::Draw,back,shotgun);CHECK(out.request);const auto old=*out.request;
    f.items[1].key={shotgun.id,2};++f.sample.inventory.revision;f.Visible(f.items[1].key);f.Ack(old);
    out=f.Tick();CHECK(out.cancelledRequest==old.id&&!out.committedRequest&&out.reason==BodyInventoryReason::InventoryChanged);
    CHECK(std::none_of(out.slots.begin(),out.slots.end(),[](const auto& s){return s.item==shotgun;}));
    f.Neutral();out=f.Intent(BodyInventoryOperation::Draw,back,shotgun);
    CHECK(!out.request&&out.reason==BodyInventoryReason::WrongItem); // Old captured body contact cannot draw new item.
    f.Visible(rifle);f.Neutral();out=f.Intent(BodyInventoryOperation::Draw,back,f.items[1].key);CHECK(out.request&&out.request->item.generation==2);
    f.items.erase(f.items.begin()+1);++f.sample.inventory.revision;out=f.Tick();
    CHECK(out.cancelledRequest&&!out.request&&out.slots.size()==1);return 0;
}
int SlotPreferences(){
    Fixture f;
    // Adapter changes category preferences: higher-priority shotgun wins back
    // even if the rifle is first in the native inventory array.
    f.items[0].preferredSlots={back,chest};f.items[1].preferredSlots={back,chest};++f.sample.inventory.revision;
    auto out=f.Tick();CHECK(out.slots.size()==2);
    CHECK(std::find(out.slots.begin(),out.slots.end(),BodySlotAssignment{back,shotgun})!=out.slots.end());
    CHECK(std::find(out.slots.begin(),out.slots.end(),BodySlotAssignment{chest,rifle})!=out.slots.end());
    std::reverse(f.items.begin(),f.items.end());++f.sample.inventory.revision;
    const auto reordered=f.Tick();CHECK(reordered.slots==out.slots);
    f.items.push_back({{303,1},{back,chest},2,1});++f.sample.inventory.revision;out=f.Tick();
    CHECK(out.slots.size()==2); // No duplicate body slot or cloned overflow weapon.
    f.Neutral();out=f.Intent(BodyInventoryOperation::Draw,9,{303,1});CHECK(!out.request&&out.reason==BodyInventoryReason::UnassignedSlot);return 0;
}
int StableShoulderReplacement(){
    Fixture f;
    // Initially two rifles: unchanged rifle occupies its preferred shoulder.
    f.items[1].priority=10;f.items[1].preferredSlots={chest,back};
    ++f.sample.inventory.owner.generation;auto out=f.Tick();
    CHECK(std::find(out.slots.begin(),out.slots.end(),BodySlotAssignment{chest,rifle})!=out.slots.end());
    // Replace the other rifle with a higher-priority shotgun also preferring
    // that shoulder. The surviving exact physical item keeps its assignment.
    f.items[1]={{303,1},{chest,back},2,100};++f.sample.inventory.revision;
    out=f.Tick();
    CHECK(out.slots.size()==2);
    CHECK(std::find(out.slots.begin(),out.slots.end(),BodySlotAssignment{chest,rifle})!=out.slots.end());
    CHECK(std::find(out.slots.begin(),out.slots.end(),BodySlotAssignment{back,{303,1}})!=out.slots.end());
    std::reverse(f.items.begin(),f.items.end());++f.sample.inventory.revision;
    CHECK(f.Tick().slots==out.slots);
    // Recentring changes the coordinate space, not either physical item.
    const auto beforeRecenter=out.slots;
    ++f.sample.inventory.owner.space;out=f.Tick();
    CHECK(out.slots==beforeRecenter);
    CHECK(!out.request&&!out.committedRequest);
    // A new actor incarnation retires the old assignments and restores
    // deterministic initial priority allocation.
    ++f.sample.inventory.owner.generation;out=f.Tick();
    CHECK(std::find(out.slots.begin(),out.slots.end(),BodySlotAssignment{chest,{303,1}})!=out.slots.end());
    return 0;
}
int DuplicateCadenceAndTimeout(){
    Fixture f;auto out=f.Intent(BodyInventoryOperation::Holster,chest,rifle);const auto command=*out.request;
    // Exact ack on unchanged snapshot cannot commit; a later observation is required.
    f.Ack(command);out=f.policy.Update(f.sample);CHECK(!out.committedRequest&&!out.request);
    f.Empty();out=f.policy.Update(f.sample);CHECK(out.reason==BodyInventoryReason::InvalidSnapshot&&!out.committedRequest&&out.cancelledRequest==command.id);
    Fixture timeout;out=timeout.Intent(BodyInventoryOperation::Holster,chest,rifle);
    for(unsigned n=0;n<24;++n)CHECK(!timeout.Tick().cancelledRequest);
    out=timeout.Tick();CHECK(out.reason==BodyInventoryReason::Timeout&&out.cancelledRequest&&!out.request);
    Fixture duplicate;out=duplicate.Intent(BodyInventoryOperation::Holster,chest,rifle);
    duplicate.sample.nowNs+=50*ms;out=duplicate.policy.Update(duplicate.sample);CHECK(!out.request&&!out.cancelledRequest&&out.blockFire);
    duplicate.sample.focused=false;out=duplicate.policy.Update(duplicate.sample);CHECK(out.cancelledRequest&&out.reason==BodyInventoryReason::InputUnavailable);return 0;
}
int LifecycleAndStaleData(){
    for(unsigned which=0;which<6;++which){
        Fixture f;auto out=f.Intent(BodyInventoryOperation::Holster,chest,rifle);const auto command=*out.request;f.Ack(command);f.Empty();
        if(which==0)++f.sample.inventory.owner.actor;
        if(which==1)++f.sample.inventory.owner.generation;
        if(which==2)++f.sample.inventory.owner.space;
        if(which==3)f.sample.focused=false;
        if(which==4)f.sample.handTracked=false;
        if(which==5)++f.sample.inventory.revision;
        out=f.Tick();CHECK(out.cancelledRequest==command.id&&!out.committedRequest&&out.blockFire);
    }
    Fixture stale;auto r=stale.Intent(BodyInventoryOperation::Holster,chest,rifle);stale.sample.nowNs+=101*ms;
    r=stale.policy.Update(stale.sample);CHECK(r.reason==BodyInventoryReason::StaleSnapshot&&r.cancelledRequest&&r.blockFire);
    Fixture clock;clock.sample.nowNs-=ms;clock.sample.inventory.observedNs=clock.sample.nowNs;++clock.sample.inventory.sequence;
    r=clock.policy.Update(clock.sample);CHECK(r.reason==BodyInventoryReason::ClockReversed&&r.blockFire);return 0;
}
int MissingBindingsAndInvalidInventory(){
    Fixture unknown;unknown.sample.inventory.presentation=BodyPresentation::Unknown;unknown.Tick();unknown.Neutral();
    auto r=unknown.Intent(BodyInventoryOperation::Holster,chest,rifle);CHECK(!r.request&&r.blockFire&&r.reason==BodyInventoryReason::UnavailablePresentation);
    Fixture unblocked;unblocked.Empty();unblocked.sample.inventory.fireSuppressed=false;r=unblocked.Tick();
    CHECK(r.reason==BodyInventoryReason::InvalidSnapshot&&!r.emptyHands&&r.blockFire);
    Fixture duplicate;duplicate.items.push_back({{rifle.id,2},{3},1,1});++duplicate.sample.inventory.revision;
    r=duplicate.Tick();CHECK(r.reason==BodyInventoryReason::InvalidSnapshot&&r.slots.empty());
    Fixture noRevision;noRevision.items[1].key.generation++;r=noRevision.Tick();CHECK(r.reason==BodyInventoryReason::InvalidSnapshot);
    Fixture wrongSelected;wrongSelected.sample.inventory.selected={999,1};r=wrongSelected.Tick();CHECK(r.reason==BodyInventoryReason::InvalidSnapshot);
    wrongSelected.sample.inventory.selected=rifle;r=wrongSelected.Tick();
    CHECK(r.phase==BodyInventoryPhase::Held&&r.slots.size()==2); // Transient invalid snapshots recover without a pickup.
    Fixture retained;retained.items.resize(1);retained.items[0].preferredSlots={back,chest};++retained.sample.inventory.revision;
    r=retained.Tick();CHECK(r.slots.size()==1&&r.slots[0].slot==chest); // Preserve prior compatible fallback.
    retained.sample.inventory.selected={999,1};CHECK(retained.Tick().slots.empty());
    retained.sample.inventory.selected=rifle;r=retained.Tick();CHECK(r.slots.size()==1&&r.slots[0].slot==chest);
    Fixture rollback;++rollback.sample.inventory.revision;rollback.Tick();--rollback.sample.inventory.revision;
    r=rollback.Tick();CHECK(r.reason==BodyInventoryReason::StaleSnapshot&&r.blockFire);
    return 0;
}
int FreshIntentAndSelection(){
    Fixture f;f.sample.intent={10,BodyInventoryOperation::Holster,chest,rifle};++f.sample.inventory.owner.generation;
    auto r=f.Tick();CHECK(!r.request);CHECK(!f.Tick().request);f.Neutral();CHECK(!f.Tick().request);
    r=f.Intent(BodyInventoryOperation::Holster,chest,rifle);CHECK(r.request);const auto command=*r.request;
    f.Visible(shotgun);r=f.Tick();CHECK(r.cancelledRequest==command.id&&r.reason==BodyInventoryReason::SelectionChanged);
    Fixture changed;changed.sample.intent={8,BodyInventoryOperation::Draw,back,shotgun};
    ++changed.sample.inventory.revision;CHECK(!changed.Tick().request);changed.Neutral();
    changed.sample.intent.operation=BodyInventoryOperation::Draw;r=changed.Tick();
    CHECK(!r.request&&r.reason==BodyInventoryReason::StaleIntent); // Contact captured before loadout refresh is consumed.
    // A stale acknowledgement cannot produce another commit after cancellation.
    f.Ack(command);CHECK(!f.Tick().committedRequest);
    return 0;
}
}
int main(){
    if(HolsterAndDraw()||ExactAcknowledgement()||PickupReplacement()||SlotPreferences()||StableShoulderReplacement()||
       DuplicateCadenceAndTimeout()||LifecycleAndStaleData()||MissingBindingsAndInvalidInventory()||FreshIntentAndSelection())return 1;
    std::puts("Body inventory tests passed");return 0;
}
