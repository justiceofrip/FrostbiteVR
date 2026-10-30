#pragma once
#include "Bc2NativeCycleService.h"
#include <cstring>
namespace fvr::bc2 {
// Native adapter journal for the interval owned by the ammunition observer.
// It retains genuine guarded own-Update rows, never constructs a receipt from
// matching counts, and never changes the inventory's accounting.
class Bc2CycleReloadRouting {
public:
    bool Begin(const NativeCycleRecoveryScope& scope,const interaction::WeaponCycleReloadCommand& command)noexcept {
        if(command_||!command.permit.debt.guardEpoch||!interaction::weapon_cycle_detail::MagazineCommand(command.command)||
           command.command.context!=command.permit.resource)return false;
        scope_=scope;command_=command;failed_=false;open_={};last_=command.permit.second;rows_={};receipt_.reset();return true;
    }
    bool Active()const noexcept{return command_.has_value();}
    bool Failed()const noexcept{return failed_;}
    bool Drained()const noexcept{return std::none_of(open_.begin(),open_.end(),[](auto id){return id!=0;});}
    const auto& Command()const noexcept{return command_;}
    const auto& Scope()const noexcept{return scope_;}
    NativeCycleRecoveryDecision Evaluate(const NativeCycleRecoveryScope& scope,const ReloadFlowEventInput& entry,
        int capacity,std::int64_t deadline)noexcept {
        const auto b=entry.boundary.branch;const auto c=DecodeReloadUpdateContext(entry.copiedContext);
        if(!command_||failed_||scope!=scope_||b>=3||open_[b]||!entry.nativeInvocation||
           entry.nativeInvocation<=last_[b]||entry.kind!=ReloadFlowEvent::Update||entry.depth!=1||
           entry.nativeParent||entry.nativeUpdate!=entry.nativeInvocation||!entry.contextCopied||!c||
           !ValidManualReloadDelta(c->deltaSeconds)||c->reloadTimeMultiplier!=1||c->flags24Through28[2]||
           !Boundary(entry.boundary,b)||capacity!=command_->command.before.capacity||
           deadline<=entry.nowNs||deadline-entry.nowNs>100000000){failed_=true;return {};}
        open_[b]=last_[b]=entry.nativeInvocation;
        return {entry.nativeInvocation,command_->permit.debt.guardEpoch,b,capacity,deadline};
    }
    bool Finish(const NativeCycleRecoveryDecision& d,const ReloadFlowRecord& r)noexcept {
        if(!command_||failed_||!d||d.branch>=3||open_[d.branch]!=d.invocation){failed_=true;return false;}
        open_[d.branch]=0;
        const auto& g=r.exit.cycleGuard;std::array<std::uint32_t,2> original{};
        std::memcpy(original.data(),r.entry.copiedContext.data()+0x28,8);
        if(r.id!=d.invocation||d.epoch!=command_->permit.debt.guardEpoch||r.exit.cycleGuardEpoch!=d.epoch||
           !ValidateCycleUpdate(r,scope_.identity,d.branch)||!g.Exact()||g.original!=original||
           g.effective!=std::array<std::uint32_t,2>{(original[0]&~255u)|1u,0}||
           !r.exit.cycleGuardSteps||r.exit.cycleGuardRejected||r.exit.holdRequested||r.exit.hold.applied||
           r.exit.nowNs<=r.entry.nowNs||r.exit.nowNs>=d.deadlineNs||
           !Boundary(r.entry.boundary,d.branch)||!Boundary(*r.exit.boundary,d.branch)||
           (r.exit.boundary->flagsA8!=r.entry.boundary.flagsA8&&
            r.exit.boundary->flagsA8!=(r.entry.boundary.flagsA8&~2u))){failed_=true;return false;}
        const auto& after=*r.exit.boundary;const auto& expected=command_->command.after;
        if(receipt_&&(after.loaded!=expected.loaded||after.reserve!=expected.reserve)){failed_=true;return false;}
        if(!receipt_&&after.loaded==expected.loaded&&after.reserve==expected.reserve)
            rows_[d.branch]=interaction::WeaponCycleIdleUpdate{d.branch,r.id,d.epoch,r.entry.nowNs,
                r.exit.nowNs,d.deadlineNs,after.loaded,after.reserve,d.capacity};
        return true;
    }
    bool Restore(const ReloadFlowRecord& r)noexcept {
        const auto b=r.entry.boundary.branch;
        if(!command_||failed_||b>=2||open_[b]||r.id<=last_[b]||r.entry.kind!=ReloadFlowEvent::Restore||
           !r.finished||!r.identityRetained||!r.exit.boundary||r.entry.depth!=1||r.entry.nativeParent||
           r.entry.nativeUpdate||r.entry.nativeInvocation!=r.id||!ReloadRestoreMatched(r)||
           !Boundary(r.entry.boundary,b)||!Boundary(*r.exit.boundary,b)||
           (rows_[b]&&(r.exit.boundary->loaded!=command_->command.after.loaded||
                       r.exit.boundary->reserve!=command_->command.after.reserve))){
            failed_=true;return false;
        }
        last_[b]=r.id;return true; // Restores never produce an own-Update vote.
    }
    bool Receipt(const interaction::AmmunitionReceipt& receipt,
        const std::array<std::optional<AmmoResourceOwnUpdate>,3>& actual)noexcept {
        if(receipt_)return receipt==*receipt_;
        if(!command_||failed_||receipt.command!=command_->command||!receipt.authorityVerified||!receipt.copiesVerified)return false;
        for(unsigned b=0;b<3;++b)if(!rows_[b]||!actual[b]||rows_[b]->invocation!=actual[b]->invocation||
            rows_[b]->beginNs!=actual[b]->beginNs||rows_[b]->endNs!=actual[b]->endNs||
            rows_[b]->loaded!=actual[b]->loadedAfter||rows_[b]->reserve!=actual[b]->reserveAfter)return false;
        receipt_=receipt;return true;
    }
    std::optional<interaction::WeaponCycleReloadAmmoRebase> Rebase(const interaction::WeaponCycleReloadDrain& drain)const noexcept {
        if(!command_||failed_||!Drained()||!receipt_||command_->permit.debt.guardEpoch==UINT64_MAX||
           drain.revision<=command_->drain.revision||drain.observedNs<receipt_->completedNs)return {};
        interaction::WeaponCycleReloadAmmoRebase result{*command_,*receipt_,{},drain,command_->permit.debt.guardEpoch+1};
        for(unsigned b=0;b<3;++b){if(!rows_[b])return {};result.ownUpdates[b]=*rows_[b];}
        return result;
    }
    void Applied()noexcept {command_.reset();receipt_.reset();rows_={};open_={};}
private:
    bool Boundary(const ReloadFlowBoundary& b,unsigned branch)const noexcept {
        if(!command_||b.branch!=branch||b.owner!=scope_.identity.owner||b.firing!=scope_.identity.firing[branch]||
           !b.snapshotSequence||b.wrapperOffset!=(branch==0?0x3cu:branch==1?0x40u:0x10u)||
           b.current!=2||b.next!=2||b.timer!=0||(b.flagsA8&(8|16)))return false;
        if(branch==2&&(b.serverPlayer!=scope_.identity.serverPlayer||b.serverSoldier!=scope_.identity.serverSoldier||
            b.serverItem!=scope_.identity.serverItem))return false;
        const auto& c=command_->command;
        return (b.loaded==c.before.loaded&&b.reserve==c.before.reserve)||
            (b.loaded==c.after.loaded&&b.reserve==c.after.reserve);
    }
    NativeCycleRecoveryScope scope_{};
    std::optional<interaction::WeaponCycleReloadCommand> command_;
    std::optional<interaction::AmmunitionReceipt> receipt_;
    std::array<std::optional<interaction::WeaponCycleIdleUpdate>,3> rows_{};
    std::array<std::uint64_t,3> open_{},last_{};
    bool failed_=false;
};
}
namespace fvr::bc2::reloadFlowRuntime {
// Explicit private SPAS trial, pre-Start only. Normal compilation returns
// false. FVR_BC2_MANUAL_CYCLE_CANDIDATE is not a profile admission flag; exact
// measured contact/rig and controller capability remain caller prerequisites.
// This first trial excludes reload/resource transactions and diagnostics.
bool EnableNativePumpCandidate()noexcept;
// Separate ordinary capability, pre-Start/default OFF. No finite input driver.
bool EnableNativeCycleDispatcher()noexcept;
// Caller has retired physical custody. Drains exact native/request owners;
// retains unresolved debt and returns false while it cannot change selection.
bool SelectNativeCycleMode(Bc2NativeCycleMode,const ReloadStateOwner&)noexcept;
// Exact M95_sp private trial, independently selected before Start.
bool EnableNativeBoltCandidate()noexcept;
bool EnableNativeBoltRecording(unsigned cycles)noexcept;
// Pre-Start diagnostic capacity only; cannot select or acknowledge a cycle.
bool EnableOrdinaryBoltInputRecording(unsigned cycles)noexcept;
bool EnableOrdinaryResourceInputRecording()noexcept;
// Finite diagnostic only, before Start: eight cycles receive a fixed 60-second
// window and storage. No call can extend a running recorder or native lease.
bool EnableNativePumpRecording(unsigned cycles)noexcept;
// Pre-Start only; does not extend an opened recorder or a native control lease.
bool EnableResourcePumpRecording()noexcept;
Bc2NativeCycleSubmitResult PublishNativeCycleControlResult(const Bc2NativeCycleControl&)noexcept;
bool PublishNativeCycleControl(const Bc2NativeCycleControl&)noexcept;
Bc2NativeCycleSubmitResult PrepareNativeCycleReload(const Bc2NativeCycleControl&,
    const interaction::WeaponCycleReloadPause&)noexcept;
Bc2NativeCycleSubmitResult ResumeNativeCycleReload(const Bc2NativeCycleControl&,
    const interaction::WeaponCycleIdleDebtLease&)noexcept;
std::optional<Bc2NativeCycleView> ReadNativeCycleView(std::int64_t now)noexcept;
bool AcknowledgeNativeCycleReady(const interaction::WeaponCycleReady&)noexcept;
void CancelNativeCycle(bool lifetimeEnded=false)noexcept;
}
