#pragma once
#include "Bc2ContextualToolInput.h"

namespace fvr::bc2 {
enum class ContextualToolSelection {Unknown,Original,Tool};
// Bookkeeping for an operation already committed by shared body custody. This
// neither acquires custody nor acknowledges a native selection/cache command.
// Metadata invalidation revokes actions but cannot forget the body's operation.
class ContextualToolLifetime {
public:
    bool Begin(std::uint64_t operation,const ContextualToolObservation& anchor)noexcept {
        const auto& i=anchor.inventory;
        if(operation_||!operation||i.count>i.items.size()||i.count<=4||i.selected>1||
           i.owner.weapon!=i.items[i.selected].weapon||!i.owner.weapon||
           !BodyLongGunCategory(i.items[i.selected].category)||i.items[4]!=anchor.tool.item||
           anchor.tool.item.category!=12||!anchor.tool.item.weapon)return false;
        operation_=operation;anchor_=anchor;revoked_=false;return true;
    }
    // Only the shared body owner can end this bookkeeping lifetime. In
    // particular a route/firing change is not an acknowledgement of rehide.
    bool ObserveBody(std::uint64_t currentOperation)noexcept {
        if(!operation_||currentOperation==operation_)return false;
        operation_=0;anchor_.reset();equip_.reset();return_.reset();revoked_=false;return true;
    }
    ContextualToolSelection Observe(const ReloadStateOwner& owner,
        const std::optional<ContextualToolObservation>& current)noexcept {
        if(!anchor_)return ContextualToolSelection::Unknown;
        if(!SameActor(anchor_->inventory.owner,owner)){
            revoked_=true;return ContextualToolSelection::Unknown;
        }
        if(!current)return ContextualToolSelection::Unknown;
        if(!SameCarried(*anchor_,*current)||!SameActor(current->inventory.owner,owner)||
           current->inventory.owner.weapon!=owner.weapon){
            revoked_=true;return ContextualToolSelection::Unknown;
        }
        if(!SameConfiguration(*anchor_,*current))revoked_=true;
        const auto& i=current->inventory;const auto& original=anchor_->inventory.items[anchor_->inventory.selected];
        if(i.selected==original.slot&&owner.weapon==original.weapon)return ContextualToolSelection::Original;
        if(i.selected==4&&owner.weapon==anchor_->tool.item.weapon)return ContextualToolSelection::Tool;
        revoked_=true;return ContextualToolSelection::Unknown;
    }
    bool ConfigurationCurrent(const ContextualToolObservation& current)const noexcept {
        return anchor_&&!revoked_&&SameConfiguration(*anchor_,current);
    }
    bool Revoked()const noexcept{return revoked_;}
    std::uint64_t Operation()const noexcept{return operation_;}
    const std::optional<ContextualToolObservation>& Anchor()const noexcept{return anchor_;}
    // Diagnostic copies of actual writer commits, never evidence of native
    // consumption. Keep original cache/input/lease fields through read gaps and
    // metadata revocation. Do not replay them or overwrite an unresolved edge.
    bool RememberCommit(const ToolInputReceipt& receipt)noexcept {
        const auto& r=receipt.request;
        if(!operation_||r.operation!=operation_||!r.serial)return false;
        if(r.kind==ToolInputKind::Equip&&!equip_&&!return_){equip_=receipt;return true;}
        if(r.kind==ToolInputKind::Return&&equip_&&!return_){return_=receipt;return true;}
        return false;
    }
    const std::optional<ToolInputReceipt>& EquipCommit()const noexcept{return equip_;}
    const std::optional<ToolInputReceipt>& ReturnCommit()const noexcept{return return_;}
private:
    static bool SameActor(const ReloadStateOwner& a,const ReloadStateOwner& b)noexcept {
        return a.player==b.player&&a.soldier==b.soldier&&a.weak==b.weak&&a.actorGeneration==b.actorGeneration;
    }
    static bool SameCarried(const ContextualToolObservation& a,const ContextualToolObservation& b)noexcept {
        return SameActor(a.inventory.owner,b.inventory.owner)&&a.inventory.inventory==b.inventory.inventory&&
            a.inventory.count==b.inventory.count&&a.inventory.items==b.inventory.items;
    }
    static bool SameConfiguration(const ContextualToolObservation& a,const ContextualToolObservation& b)noexcept {
        return SameCarried(a,b)&&a.inventory.switching==b.inventory.switching&&
            a.inventory.routes==b.inventory.routes&&a.inventory.routeCount==b.inventory.routeCount&&a.tool==b.tool;
    }
    std::uint64_t operation_=0;std::optional<ContextualToolObservation> anchor_;bool revoked_=false;
    std::optional<ToolInputReceipt> equip_,return_;
};
}
