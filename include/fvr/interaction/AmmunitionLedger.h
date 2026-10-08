#pragma once
#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>

namespace fvr::interaction {
// Stable inventory identity, supplied by an adapter. Equip/reference-space
// changes belong to the command context, not to the physical magazine's owner.
struct AmmoResourceIdentity {
    std::uint64_t actor=0,actorGeneration=0,weapon=0,weaponGeneration=0;
    bool operator==(const AmmoResourceIdentity&)const=default;
};
struct AmmoResourceContext {
    AmmoResourceIdentity resource{};
    std::uint64_t equipGeneration=0,space=0;
    bool operator==(const AmmoResourceContext&)const=default;
};
struct AmmunitionCounts {
    int loaded=0,reserve=0,capacity=0;
    bool operator==(const AmmunitionCounts&)const=default;
};
struct AmmunitionSnapshot {
    AmmoResourceContext context{};
    std::uint64_t sequence=0;
    std::int64_t observedNs=0,deadlineNs=0;
    AmmunitionCounts counts{};
    // Exact current owner, finite counts, and idle native copies. An adapter
    // must establish this; time, hand geometry and a render frame cannot.
    bool coherentIdleVerified=false;
};
enum class AmmunitionOperation:std::uint8_t {RemoveMagazine,ReturnMagazine,RefillMagazine,InsertRound};
struct RemovedMagazineResource {
    AmmoResourceIdentity owner{};
    std::uint64_t id=0;
    int rounds=0,capacity=0;
    bool operator==(const RemovedMagazineResource&)const=default;
};
struct AmmunitionCommand {
    AmmoResourceContext context{};
    std::uint64_t id=0,sourceSequence=0;
    std::int64_t requestedNs=0,admissionDeadlineNs=0,deadlineNs=0;
    AmmunitionOperation operation{};
    AmmunitionCounts before{},after{};
    std::optional<RemovedMagazineResource> original;
    // An empty magazine can be removed/returned without an ammunition write.
    // It still needs adapter proof of its empty, controlled native state.
    bool mutationRequired=false;
    bool operator==(const AmmunitionCommand&)const=default;
};
struct AmmunitionReceipt {
    AmmunitionCommand command{};
    std::uint64_t authorityInvocation=0;
    std::int64_t beganNs=0,completedNs=0;
    AmmunitionCounts before{},after{};
    // Adapter verifies the actual callback, its local postcondition AND all
    // required copies after it. A successful function return alone is not this.
    bool authorityVerified=false,copiesVerified=false;
    bool operator==(const AmmunitionReceipt&)const=default;
};
enum class AmmunitionLedgerPhase:std::uint8_t {Unbound,Ready,Queued,Dispatched,NeedsReconciliation};
enum class MagazineResourceState:std::uint8_t {None,Held,Discarded,Returned};

// One ledger per stable weapon instance. No native calls, timers that reload a
// gun, implicit reserve grants, render state or hand claims belong here.
// Native dispatch is at most once. Ambiguous effects block further commands.
class AmmunitionLedger {
public:
    bool Bind(const AmmunitionSnapshot& s,std::int64_t now)noexcept {
        if(!Fresh(s,now)||phase_==AmmunitionLedgerPhase::Queued||phase_==AmmunitionLedgerPhase::Dispatched||
           phase_==AmmunitionLedgerPhase::NeedsReconciliation)return false;
        if(phase_!=AmmunitionLedgerPhase::Unbound){
            if(s.context.resource!=snapshot_.context.resource||s.sequence<=snapshot_.sequence||s.observedNs<snapshot_.observedNs)return false;
            // A detached well must remain empty. A native auto-refill cannot
            // silently become a replacement magazine or consume the old token.
            if(wellEmpty_&&s.counts.loaded!=0)return false;
            if(original_&&s.counts.capacity!=original_->capacity)return false;
        }
        snapshot_=s;phase_=AmmunitionLedgerPhase::Ready;return true;
    }
    std::optional<AmmunitionCommand> Queue(AmmunitionOperation operation,const AmmunitionSnapshot& s,
        std::int64_t now,std::optional<RemovedMagazineResource> original={})noexcept {
        if(phase_!=AmmunitionLedgerPhase::Ready||!Fresh(s,now)||s.context!=snapshot_.context||
           s.sequence<snapshot_.sequence||s.observedNs<snapshot_.observedNs||
           next_==std::numeric_limits<std::uint64_t>::max()||now>std::numeric_limits<std::int64_t>::max()-2000000000ll)return {};
        if(wellEmpty_&&s.counts.loaded!=0)return {};
        auto after=s.counts;
        switch(operation){
        case AmmunitionOperation::RemoveMagazine:
            if(wellEmpty_||original||state_==MagazineResourceState::Held)return {};
            after.loaded=0;break;
        case AmmunitionOperation::ReturnMagazine:
            if(!wellEmpty_||state_!=MagazineResourceState::Held||!original_||original!=original_||
               original_->owner!=s.context.resource||original_->capacity!=s.counts.capacity||s.counts.loaded!=0)return {};
            after.loaded=original_->rounds;break;
        case AmmunitionOperation::RefillMagazine:
            if(!wellEmpty_||state_==MagazineResourceState::Held||original||s.counts.loaded!=0||s.counts.reserve<=0)return {};
            after.loaded=std::min(s.counts.capacity,s.counts.reserve);after.reserve-=after.loaded;break;
        case AmmunitionOperation::InsertRound:
            if(wellEmpty_||state_==MagazineResourceState::Held||original||s.counts.loaded>=s.counts.capacity||s.counts.reserve<=0)return {};
            ++after.loaded;--after.reserve;break;
        default:return {};
        }
        // The current observation must still describe the expected resource
        // state; queued work cannot reuse a snapshot under a new context.
        snapshot_=s;
        command_=AmmunitionCommand{s.context,++next_,s.sequence,now,s.deadlineNs,now+2000000000ll,operation,s.counts,after,original,s.counts!=after};
        phase_=AmmunitionLedgerPhase::Queued;return command_;
    }
    std::optional<AmmunitionCommand> Dispatch(const AmmunitionSnapshot& current,std::int64_t now)noexcept {
        if(phase_!=AmmunitionLedgerPhase::Queued||!command_)return {};
        if(!Fresh(current,now)||current.context!=command_->context||current.sequence<command_->sourceSequence||
           current.observedNs<snapshot_.observedNs||current.counts!=command_->before||now>=command_->admissionDeadlineNs){
            Cancel();return {};
        }
        phase_=AmmunitionLedgerPhase::Dispatched;return command_;
    }
    bool Complete(const AmmunitionReceipt& receipt,std::int64_t now)noexcept {
        // A duplicate may be acknowledged only when it cannot masquerade as
        // completion of a different operation now in flight.
        if(lastReceipt_&&receipt==*lastReceipt_)return !command_;
        if((phase_!=AmmunitionLedgerPhase::Dispatched&&phase_!=AmmunitionLedgerPhase::NeedsReconciliation)||
           !command_||receipt.command!=*command_)return false;
        if(!receipt.authorityVerified||!receipt.copiesVerified||!receipt.authorityInvocation||
           receipt.beganNs<command_->requestedNs||receipt.completedNs<receipt.beganNs||
           now<receipt.completedNs||now>=command_->deadlineNs||receipt.completedNs>=command_->deadlineNs||
           receipt.before!=command_->before||receipt.after!=command_->after){
            phase_=AmmunitionLedgerPhase::NeedsReconciliation;return false;
        }
        switch(command_->operation){
        case AmmunitionOperation::RemoveMagazine:
            original_=RemovedMagazineResource{command_->context.resource,command_->id,command_->before.loaded,command_->before.capacity};
            state_=MagazineResourceState::Held;wellEmpty_=true;break;
        case AmmunitionOperation::ReturnMagazine:state_=MagazineResourceState::Returned;wellEmpty_=false;break;
        case AmmunitionOperation::RefillMagazine:wellEmpty_=false;break;
        case AmmunitionOperation::InsertRound:break;
        }
        snapshot_.counts=command_->after;snapshot_.observedNs=receipt.completedNs;
        lastReceipt_=receipt;command_.reset();phase_=AmmunitionLedgerPhase::Ready;return true;
    }
    bool Discard(const RemovedMagazineResource& resource)noexcept {
        if(phase_!=AmmunitionLedgerPhase::Ready||!wellEmpty_||state_!=MagazineResourceState::Held||
           !original_||resource!=*original_)return false;
        // No native mutation and no reserve credit: these rounds left the gun
        // when the removal was acknowledged, and remain in the discarded item.
        state_=MagazineResourceState::Discarded;return true;
    }
    void Cancel()noexcept {
        if(phase_==AmmunitionLedgerPhase::Dispatched){phase_=AmmunitionLedgerPhase::NeedsReconciliation;return;}
        if(phase_==AmmunitionLedgerPhase::Queued){command_.reset();phase_=AmmunitionLedgerPhase::Ready;}
    }
    void Expire(std::int64_t now)noexcept {
        if(command_&&(now<command_->requestedNs||now>=(phase_==AmmunitionLedgerPhase::Queued?
            command_->admissionDeadlineNs:command_->deadlineNs)))Cancel();
    }
    AmmunitionLedgerPhase Phase()const noexcept{return phase_;}
    MagazineResourceState ResourceState()const noexcept{return state_;}
    bool WellEmpty()const noexcept{return wellEmpty_;}
    const auto& Original()const noexcept{return original_;}
    const auto& Pending()const noexcept{return command_;}
    const auto& Snapshot()const noexcept{return snapshot_;}
    // Resolution of an ambiguous dispatched operation must use exact retained
    // native evidence. There is intentionally no force-reset or timeout refill.
private:
    static bool Fresh(const AmmunitionSnapshot& s,std::int64_t now)noexcept {
        const auto& o=s.context.resource;const auto& c=s.counts;
        return s.coherentIdleVerified&&o.actor&&o.actorGeneration&&o.weapon&&o.weaponGeneration&&
            s.context.equipGeneration&&s.context.space&&s.sequence&&s.observedNs>0&&now>=s.observedNs&&
            s.deadlineNs>now&&s.deadlineNs-s.observedNs<=100000000&&
            c.capacity>0&&c.capacity<=1000000&&c.loaded>=0&&c.loaded<=c.capacity&&c.reserve>=0&&c.reserve<=1000000;
    }
    AmmunitionLedgerPhase phase_=AmmunitionLedgerPhase::Unbound;
    MagazineResourceState state_=MagazineResourceState::None;
    AmmunitionSnapshot snapshot_{};
    std::optional<AmmunitionCommand> command_;
    std::optional<AmmunitionReceipt> lastReceipt_;
    std::optional<RemovedMagazineResource> original_;
    std::uint64_t next_=0;
    bool wellEmpty_=false;
};
}
