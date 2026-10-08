#pragma once
#include "Bc2AmmoResourceBinding.h"
#include "Bc2AmmoResourceEvidence.h"
#include "Bc2AmmoResourceReadEvidence.h"
#include "fvr/interaction/AmmunitionInventory.h"

namespace fvr::bc2 {
// The gather thread submits one edge/seat, never a held-button retry. Native
// admission rechecks both this inventory binding and the current observation.
struct AmmoResourceRequest {
    AmmoResourceBinding binding{};
    interaction::AmmunitionIntent intent{};
    bool discard=false;
    bool operator==(const AmmoResourceRequest&)const=default;
};
enum class AmmoResourceRequestState:unsigned {None,Queued,Dispatched,Completed,Rejected,Uncertain};
// A terminal result retains its original request across selection/tracking
// changes. It grants no current counts, hand ownership or new command admission.
struct AmmoResourceOutcome {
    AmmoResourceRequest request{};
    AmmoResourceRequestState state=AmmoResourceRequestState::None;
    std::optional<interaction::AmmunitionReceipt> receipt;
    std::int64_t resolvedNs=0;
    bool nativeDispatched=false;
};
struct AmmoResourceView {
    AmmoResourceBinding binding{};
    ReloadHoldIdentity identity{};
    interaction::AmmunitionSnapshot snapshot{};
    interaction::AmmunitionLedgerPhase phase=interaction::AmmunitionLedgerPhase::Unbound;
    interaction::MagazineResourceState originalState=interaction::MagazineResourceState::None;
    std::optional<interaction::RemovedMagazineResource> original;
    std::optional<interaction::AmmunitionReceipt> receipt;
    std::optional<interaction::AmmunitionCommand> command;
    std::uint64_t request=0;
    AmmoResourceRequestState requestState=AmmoResourceRequestState::None;
    bool wellEmpty=false;
    std::optional<AmmoResourceOutcome> terminal;
};
inline bool AmmoResourceViewFresh(const AmmoResourceView& v,std::int64_t now)noexcept {
    interaction::AmmunitionLedger validator;
    return AmmoResourceBindingFresh(v.binding,v.identity.owner,now)&&v.snapshot.context==v.binding.context&&
        validator.Bind(v.snapshot,now);
}
// A single bounded command slot and latest immutable result. No native calls,
// allocator, blocking wait or geometry ownership lives in this cross-thread pipe.
// Request IDs are global for this session, including discarded/rejected events.
class AmmoResourceChannel {
    struct Guard {
        std::atomic_flag& flag;bool held;
        explicit Guard(std::atomic_flag& f)noexcept:flag(f),held(!f.test_and_set(std::memory_order_acquire)){}
        ~Guard(){if(held)flag.clear(std::memory_order_release);}
    };
public:
    bool Submit(const AmmoResourceRequest& r)noexcept {
        Guard g(requestGate_);if(!g.held||request_||!r.intent.id||r.intent.id<=lastRequest_)return false;
        lastRequest_=r.intent.id;request_=r;return true;
    }
    std::optional<AmmoResourceRequest> Take()noexcept {
        Guard g(requestGate_);if(!g.held)return {};auto r=request_;request_.reset();return r;
    }
    bool Publish(const std::optional<AmmoResourceView>& view,const std::optional<AmmoResourceOutcome>& outcome={})noexcept {
        const auto revision=revision_.fetch_add(1,std::memory_order_acq_rel)+1;
        Guard g(viewGate_);if(!g.held)return false;
        view_=view;published_=revision;
        const auto& terminal=outcome?outcome:view?view->terminal:outcome;
        if(terminal&&(!outcome_||terminal->request.intent.id>=outcome_->request.intent.id))outcome_=terminal;
        return true;
    }
    std::optional<AmmoResourceView> Read(const ReloadStateOwner& owner,std::int64_t now,std::int64_t processingNow=0)noexcept {
        const auto revision=revision_.load(std::memory_order_acquire);
        AmmoResourceReadRow row;row.requestedNow=now;row.processingNow=processingNow?processingNow:now;
        row.revision=revision;row.wantedWeapon=owner.weapon;
        const auto reject=[&](AmmoResourceReadReason why)->std::optional<AmmoResourceView>{row.reason=why;readEvidence_.Note(row);return {};};
        Guard g(viewGate_);if(!g.held)return reject(AmmoResourceReadReason::Busy);
        row.published=published_;row.current=revision_.load(std::memory_order_acquire);
        if(!view_)return reject(AmmoResourceReadReason::NoView);
        row.bindingObserved=view_->binding.observedNs;row.bindingDeadline=view_->binding.deadlineNs;
        row.snapshotObserved=view_->snapshot.observedNs;row.snapshotDeadline=view_->snapshot.deadlineNs;
        row.request=view_->request;row.publishedWeapon=view_->binding.owner.weapon;
        if(published_!=revision||row.current!=revision)return reject(AmmoResourceReadReason::Revision);
        if(view_->binding.owner!=owner)return reject(AmmoResourceReadReason::Owner);
        if(!AmmoResourceViewFresh(*view_,now)){
            if(now<row.bindingObserved||now<row.snapshotObserved)return reject(AmmoResourceReadReason::Future);
            if(now>=row.bindingDeadline||now>=row.snapshotDeadline)return reject(AmmoResourceReadReason::Expired);
            return reject(AmmoResourceReadReason::Invalid);
        }
        readEvidence_.Note(row);
        return view_;
    }
    void ReportReadEvidence(std::ostream& out,bool drained)const {readEvidence_.Report(out,drained);}
    std::optional<AmmoResourceOutcome> Outcome(std::uint64_t request,const interaction::AmmoResourceContext& context)noexcept {
        Guard g(viewGate_);
        if(!g.held||!outcome_||outcome_->request.intent.id!=request||outcome_->request.intent.context!=context)return {};
        return outcome_;
    }
private:
    std::atomic_flag requestGate_=ATOMIC_FLAG_INIT,viewGate_=ATOMIC_FLAG_INIT;
    std::atomic<std::uint64_t> revision_=0;
    std::uint64_t published_=0,lastRequest_=0;
    std::optional<AmmoResourceRequest> request_;
    std::optional<AmmoResourceView> view_;
    std::optional<AmmoResourceOutcome> outcome_;
    AmmoResourceReadEvidence readEvidence_;
};

// Serialized by the native adapter, not the gather thread. One operation may
// be in flight; every stable weapon keeps its own ammunition/original magazine.
// Engine helpers still execute only in the owning server Update, between
// Dispatch and Call. The service cannot acknowledge them on its own.
class AmmoResourceService {
public:
    bool Select(const AmmoResourceBinding& binding,const ReloadHoldIdentity& identity,
                const interaction::AmmunitionSnapshot& snapshot,std::int64_t now)noexcept {
        AmmoResourceView next;next.binding=binding;next.identity=identity;next.snapshot=snapshot;
        if(!AmmoResourceViewFresh(next,now)){view_.reset();return false;}
        // A completed helper can precede the next coherent native count sample.
        // Retain the prior bounded in-flight publication instead of rebinding
        // the completed ledger to an older count snapshot.
        if(awaitingPublication_&&receipt_&&receipt_->command.context==snapshot.context&&
           !ReceiptSnapshot(snapshot))return false;
        if(active_){
            if(active_->context!=snapshot.context||identity!=identity_){
                Cancel(now);view_.reset();return false;
            }
        }else if(!inventory_.Select(snapshot,now)){view_.reset();return false;}
        view_=next;
        if(awaitingPublication_&&receipt_&&receipt_->command.context!=snapshot.context)awaitingPublication_=false;
        Refresh();return true;
    }
    std::optional<interaction::AmmunitionCommand> Accept(const AmmoResourceRequest& r,std::int64_t now)noexcept {
        using namespace interaction;
        if(!r.intent.id||r.intent.id<=lastRequest_)return {};
        lastRequest_=r.intent.id;
        if(active_||awaitingPublication_)return {}; // do not replace the pending publication receipt key
        request_=r.intent.id;submitted_=r;nativeDispatched_=false;state_=AmmoResourceRequestState::Rejected;receipt_.reset();
        const auto reject=[&]()->std::optional<AmmunitionCommand>{Finish(now);Refresh();return {};};
        if(!view_||!AmmoResourceViewFresh(*view_,now)||!AmmoResourceBindingFresh(r.binding,view_->binding.owner,now)||
           r.binding.context!=view_->binding.context||r.binding.inventory!=view_->binding.inventory||
           r.binding.switching!=view_->binding.switching||r.binding.data!=view_->binding.data||r.binding.persistence!=view_->binding.persistence||
           r.intent.context!=view_->snapshot.context||r.intent.observedNs<=0||now<r.intent.observedNs||now>=r.intent.deadlineNs||
           r.intent.deadlineNs-r.intent.observedNs>100000000)return reject();
        if(r.discard){
            if(r.intent.operation!=AmmunitionOperation::RemoveMagazine||!r.intent.original||!inventory_.Discard(*r.intent.original))return reject();
            state_=AmmoResourceRequestState::Completed;Finish(now);Refresh();return {};
        }
        const auto command=inventory_.Submit(r.intent,view_->snapshot,now);
        if(!command)return reject();
        completion_=AmmoResourceCompletion{};
        if(!completion_.Begin(*command,view_->identity)){inventory_.Cancel(*command);return reject();}
        identity_=view_->identity;active_=command;state_=AmmoResourceRequestState::Queued;Refresh();return command;
    }
    bool Dispatch(const interaction::AmmunitionSnapshot& adjacent,std::int64_t now)noexcept {
        if(!active_||state_!=AmmoResourceRequestState::Queued)return false;
        if(!inventory_.Dispatch(*active_,adjacent,now)){active_.reset();state_=AmmoResourceRequestState::Rejected;Finish(now);Refresh();return false;}
        nativeDispatched_=true;state_=AmmoResourceRequestState::Dispatched;Refresh();return true;
    }
    bool Call(const AmmoResourceNativeCall& call)noexcept {
        if(!active_||state_!=AmmoResourceRequestState::Dispatched)return false;
        if(!completion_.Call(call)){Cancel(call.endNs);return false;}return true;
    }
    bool Observe(const AmmoResourceOwnUpdate& row,std::int64_t now)noexcept {
        if(!active_||state_==AmmoResourceRequestState::Queued)return false;
        completion_.Observe(row);
        if(completion_.Failed()){Cancel(now);return false;}
        const auto receipt=completion_.Receipt(now);if(!receipt)return false;
        if(!inventory_.Complete(*receipt,now)){Cancel(now);return false;}
        receipt_=receipt;active_.reset();state_=AmmoResourceRequestState::Completed;awaitingPublication_=true;
        Finish(receipt->completedNs);
        // Completion does not renew the selection/count publication. A new
        // coherent native sample is required before presentation consumes it.
        Refresh();return true;
    }
    void Cancel(std::int64_t now=0)noexcept {
        if(!active_)return;
        inventory_.Cancel(*active_);
        if(state_==AmmoResourceRequestState::Queued){active_.reset();state_=AmmoResourceRequestState::Rejected;Finish(now);}
        else state_=AmmoResourceRequestState::Uncertain;
        Refresh();
    }
    void Expire(std::int64_t now)noexcept {
        if(active_&&now>=(state_==AmmoResourceRequestState::Queued?active_->admissionDeadlineNs:active_->deadlineNs))Cancel(now);
    }
    const auto& View()const noexcept{return view_;}
    const auto& Active()const noexcept{return active_;}
    const auto& Inventory()const noexcept{return inventory_;}
    const auto& Outcome()const noexcept{return outcome_;}
private:
    void Finish(std::int64_t now)noexcept {
        if(!submitted_||(state_!=AmmoResourceRequestState::Completed&&state_!=AmmoResourceRequestState::Rejected))return;
        if(state_==AmmoResourceRequestState::Rejected&&nativeDispatched_)return;
        outcome_=AmmoResourceOutcome{*submitted_,state_,receipt_,std::max(now,submitted_->intent.observedNs),nativeDispatched_};
    }
    bool ReceiptSnapshot(const interaction::AmmunitionSnapshot& snapshot)const noexcept {
        return receipt_&&snapshot.context==receipt_->command.context&&snapshot.counts==receipt_->after&&
            snapshot.observedNs>=receipt_->completedNs;
    }
    void Refresh()noexcept {
        if(!view_)return;
        view_->terminal=outcome_;
        // The outcome is durable immediately, but is not fresh ammunition.
        // Preserve the entire prior view until actual counts/time converge.
        if(awaitingPublication_){
            if(!ReceiptSnapshot(view_->snapshot))return;
            awaitingPublication_=false;
        }
        const auto* ledger=inventory_.Find(view_->snapshot.context.resource);if(!ledger)return;
        view_->phase=ledger->Phase();view_->original=ledger->Original();view_->originalState=ledger->ResourceState();
        view_->wellEmpty=ledger->WellEmpty();view_->request=request_;view_->requestState=state_;
        view_->receipt=receipt_&&receipt_->command.context==view_->snapshot.context?receipt_:std::nullopt;
        view_->command=active_&&active_->context==view_->snapshot.context?active_:std::nullopt;
    }
    interaction::AmmunitionInventory<> inventory_;
    std::optional<AmmoResourceView> view_;
    std::optional<interaction::AmmunitionCommand> active_;
    std::optional<interaction::AmmunitionReceipt> receipt_;
    std::optional<AmmoResourceRequest> submitted_;
    std::optional<AmmoResourceOutcome> outcome_;
    bool nativeDispatched_=false,awaitingPublication_=false;
    AmmoResourceCompletion completion_;
    ReloadHoldIdentity identity_{};
    std::uint64_t lastRequest_=0,request_=0;
    AmmoResourceRequestState state_=AmmoResourceRequestState::None;
};
}
