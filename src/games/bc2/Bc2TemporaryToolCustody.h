#pragma once
#include "Bc2BodyInventory.h"
namespace fvr::bc2 {
// Inventory/assignment identity during an explicitly owned temporary native
// selection. It retains no controller, native observation or render authority.
// The original display is immutable; fresh space-only observations have a
// separate revision. This comparison can also serve ordinary stock gadgets.
enum class TemporaryToolObservation:unsigned {Missing,Original,Tool,ActorReplaced,ForeignSelection,InventoryChanged,AssignmentsChanged};
class Bc2TemporaryToolCustody {
public:
    bool Begin(const BodyInventoryDisplay& original,interaction::BodyItemKey tool,std::uint64_t serial,std::int64_t now)noexcept {
        if(active_||!serial||!BodyInventoryDisplayFresh(original,now)||!tool.id||!tool.generation||
           tool.id==original.physicalSelected.id||original.selectedOwner.weapon!=original.physicalSelected.id)return false;
        const auto end=original.carried.items.begin()+original.carried.count;
        if(std::count_if(original.carried.items.begin(),end,[&](const auto& i){return i.weapon==tool.id;})!=1)return false;
        if(!std::any_of(original.slots.begin(),original.slots.begin()+original.count,
            [&](const auto& s){return s.assignment.item==original.physicalSelected;}))return false;
        original_=original;tool_=tool;serial_=serial;space_=original.physicalOwner.space;revision_=original.revision;
        observed_=original.observedNs;active_=true;return true;
    }
    TemporaryToolObservation Observe(const BodyInventoryDisplay* current,std::int64_t now)noexcept {
        if(!active_||!current||!BodyInventoryDisplayFresh(*current,now)||current->observedNs<observed_)return TemporaryToolObservation::Missing;
        const auto& a=original_.selectedOwner;const auto& b=current->selectedOwner;
        if(a.player!=b.player||a.soldier!=b.soldier||a.weak!=b.weak||a.actorGeneration!=b.actorGeneration||
           current->physicalOwner.actor!=original_.physicalOwner.actor||current->physicalOwner.actorGeneration!=original_.physicalOwner.actorGeneration)
            return TemporaryToolObservation::ActorReplaced;
        if(current->carried!=original_.carried)return TemporaryToolObservation::InventoryChanged;
        if(current->count!=original_.count||current->slots!=original_.slots)return TemporaryToolObservation::AssignmentsChanged;
        if(current->physicalOwner.space!=space_){
            if(current->observedNs<=observed_||b.space!=current->physicalOwner.space||current->revision<=revision_)
                return TemporaryToolObservation::Missing;
            // Only a new reference space with unchanged exact item/assignment
            // identity may change this observational revision. No input/pose is
            // admitted or moved into that space by this bookkeeping operation.
            space_=current->physicalOwner.space;revision_=current->revision;
        }else if(current->revision!=revision_)return TemporaryToolObservation::AssignmentsChanged;
        observed_=current->observedNs;
        if(b.weapon==a.weapon&&current->physicalSelected==original_.physicalSelected)return TemporaryToolObservation::Original;
        if(b.weapon==tool_.id&&current->physicalSelected==tool_)return TemporaryToolObservation::Tool;
        return TemporaryToolObservation::ForeignSelection;
    }
    const BodyInventoryDisplay& Original()const noexcept{return original_;}
    interaction::BodyItemKey Tool()const noexcept{return tool_;}
    std::uint64_t Serial()const noexcept{return serial_;}
private:
    BodyInventoryDisplay original_{};interaction::BodyItemKey tool_{};
    std::uint64_t serial_=0,space_=0,revision_=0;std::int64_t observed_=0;bool active_=false;
};
}
