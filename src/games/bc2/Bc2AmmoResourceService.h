#pragma once
#include "Bc2AmmoResourceBinding.h"
#include "Bc2AmmoResourceEvidence.h"
#include "Bc2AmmoResourceReadEvidence.h"
#include "fvr/interaction/AmmunitionInventory.h"

namespace fvr::bc2 {
// Provenance for the consumed physical event. Its original input lifetime is
// independent from the native observation that was valid at construction.
// Admission still requires a fresh, exact current native view and own Update.
struct AmmoResourceSourceProof {
    std::int64_t constructedNs=0;
    std::uint64_t inputSequence=0,snapshotSequence=0;
    std::int64_t snapshotObservedNs=0,snapshotDeadlineNs=0;
    bool operator==(const AmmoResourceSourceProof&)const=default;
};
// The gather thread submits one edge/seat, never a held-button retry. Native
// admission rechecks both this inventory binding and the current observation.
struct AmmoResourceRequest {
    AmmoResourceBinding binding{};
    interaction::AmmunitionIntent intent{};
    bool discard=false;
    std::optional<AmmoResourceSourceProof> sourceProof;
    bool operator==(const AmmoResourceRequest&)const=default;
};
inline bool AmmoResourceConstructionValid(const AmmoResourceRequest& r,std::int64_t now)noexcept {
    if(!r.sourceProof)return false;
    const auto& p=*r.sourceProof;const auto& i=r.intent;
    return p.constructedNs>0&&p.inputSequence&&p.snapshotSequence&&
        i.observedNs>0&&i.observedNs<=p.constructedNs&&p.constructedNs<=now&&now<i.deadlineNs&&
        i.deadlineNs-i.observedNs<=100000000&&r.binding.context==i.context&&
        p.snapshotObservedNs>0&&p.snapshotObservedNs<=p.constructedNs&&p.constructedNs<p.snapshotDeadlineNs&&
        p.snapshotDeadlineNs-p.snapshotObservedNs<=100000000&&
        AmmoResourceBindingFresh(r.binding,r.binding.owner,p.constructedNs);
}
enum class AmmoResourceRequestState:unsigned {None,Queued,Dispatched,Completed,Rejected,Uncertain};
enum class AmmoResourceRejectionReason:unsigned {
    Busy,NoView,ViewTime,ViewInvalid,SourceBindingTime,SourceBindingInvalid,
    ContextBindingMismatch,IntentTime,Discard,LedgerSubmit,CompletionBegin,
    Dispatch,QueuedExpired,DispatchedExpired,SourceProof
};
inline const char* AmmoResourceRejectionName(AmmoResourceRejectionReason r)noexcept {
    constexpr const char* names[]={"busy","no_view","view_time","view_invalid","source_binding_time",
        "source_binding_invalid","context_binding_mismatch","intent_time","discard","ledger_submit",
        "completion_begin","dispatch","queued_expired","dispatched_expired","source_proof"};
    const auto index=unsigned(r);return index<std::size(names)?names[index]:"unknown";
}
// First rejected unique operation, copied before any cancellation/publication
// mutation. This is diagnostic evidence only; none of these copies authorize
// a native operation or renew the submitted intent/source clocks.
struct AmmoResourceRejection {
    AmmoResourceRejectionReason reason{};
    std::int64_t processingNowNs=0;
    AmmoResourceRequest request{};
    std::optional<AmmoResourceBinding> currentBinding;
    std::optional<interaction::AmmunitionSnapshot> currentSnapshot,adjacentSnapshot;
    std::optional<interaction::AmmunitionCommand> activeCommand;
    AmmoResourceRequestState state=AmmoResourceRequestState::None;
    bool nativeDispatched=false;
};
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
// A missing publication and a verified identity/validation failure are different
// outcomes. Consumers may retain existing custody through the former without
// receiving a view, renewed native deadline or permission to submit work.
struct AmmoResourceRead {
    AmmoResourceReadReason reason=AmmoResourceReadReason::NoView;
    std::optional<AmmoResourceView> view;
    bool Deferred()const noexcept {
        return !view&&(reason==AmmoResourceReadReason::Busy||reason==AmmoResourceReadReason::NoView||
            reason==AmmoResourceReadReason::Revision||reason==AmmoResourceReadReason::Future||reason==AmmoResourceReadReason::Expired);
    }
};
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
    AmmoResourceRead ReadObserved(const ReloadStateOwner& owner,std::int64_t now,std::int64_t processingNow=0)noexcept {
        const auto revision=revision_.load(std::memory_order_acquire);
        AmmoResourceReadRow row;row.requestedNow=now;row.processingNow=processingNow?processingNow:now;
        row.revision=revision;row.wantedWeapon=owner.weapon;
        const auto reject=[&](AmmoResourceReadReason why)->AmmoResourceRead{row.reason=why;readEvidence_.Note(row);return {why,{}};};
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
        return {AmmoResourceReadReason::Available,view_};
    }
    std::optional<AmmoResourceView> Read(const ReloadStateOwner& owner,std::int64_t now,std::int64_t processingNow=0)noexcept {
        return ReadObserved(owner,now,processingNow).view;
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
        if(active_||awaitingPublication_){NoteRejection(AmmoResourceRejectionReason::Busy,r,now);return {};} // do not replace the pending publication receipt key
        request_=r.intent.id;submitted_=r;nativeDispatched_=false;state_=AmmoResourceRequestState::Rejected;receipt_.reset();
        using Reason=AmmoResourceRejectionReason;
        const auto reject=[&](Reason reason)->std::optional<AmmunitionCommand>{NoteRejection(reason,r,now);Finish(now);Refresh();return {};};
        if(!view_)return reject(Reason::NoView);
        if(!AmmoResourceViewFresh(*view_,now))return reject(
            now<view_->binding.observedNs||now>=view_->binding.deadlineNs||
            now<view_->snapshot.observedNs||now>=view_->snapshot.deadlineNs?Reason::ViewTime:Reason::ViewInvalid);
        // New requests retain exact construction provenance, never refreshed
        // controller clocks. Legacy requests still require their source live at
        // admission. An old source is never used as current native authority.
        if(r.sourceProof){
            if(!AmmoResourceConstructionValid(r,now)||view_->binding.observedNs<r.binding.observedNs||
               view_->snapshot.sequence<r.sourceProof->snapshotSequence||
               view_->snapshot.observedNs<r.sourceProof->snapshotObservedNs)return reject(Reason::SourceProof);
            if(r.binding.owner!=view_->binding.owner)return reject(Reason::ContextBindingMismatch);
        }else if(!AmmoResourceBindingFresh(r.binding,view_->binding.owner,now))return reject(
            now<r.binding.observedNs||now>=r.binding.deadlineNs?Reason::SourceBindingTime:Reason::SourceBindingInvalid);
        if(r.binding.context!=view_->binding.context||r.binding.inventory!=view_->binding.inventory||
           r.binding.switching!=view_->binding.switching||r.binding.data!=view_->binding.data||r.binding.persistence!=view_->binding.persistence||
           r.intent.context!=view_->snapshot.context)return reject(Reason::ContextBindingMismatch);
        if(r.intent.observedNs<=0||now<r.intent.observedNs||now>=r.intent.deadlineNs||
           r.intent.deadlineNs-r.intent.observedNs>100000000)return reject(Reason::IntentTime);
        if(r.discard){
            if(r.intent.operation!=AmmunitionOperation::RemoveMagazine||!r.intent.original||!inventory_.Discard(*r.intent.original))return reject(Reason::Discard);
            state_=AmmoResourceRequestState::Completed;Finish(now);Refresh();return {};
        }
        const auto command=inventory_.Submit(r.intent,view_->snapshot,now);
        if(!command)return reject(Reason::LedgerSubmit);
        completion_=AmmoResourceCompletion{};
        if(!completion_.Begin(*command,view_->identity)){inventory_.Cancel(*command);return reject(Reason::CompletionBegin);}
        identity_=view_->identity;active_=command;state_=AmmoResourceRequestState::Queued;Refresh();return command;
    }
    bool Dispatch(const interaction::AmmunitionSnapshot& adjacent,std::int64_t now)noexcept {
        if(!active_||state_!=AmmoResourceRequestState::Queued)return false;
        if(!inventory_.Dispatch(*active_,adjacent,now)){
            if(submitted_)NoteRejection(AmmoResourceRejectionReason::Dispatch,*submitted_,now,adjacent);
            active_.reset();state_=AmmoResourceRequestState::Rejected;Finish(now);Refresh();return false;}
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
        if(active_&&now>=(state_==AmmoResourceRequestState::Queued?active_->admissionDeadlineNs:active_->deadlineNs)){
            if(submitted_)NoteRejection(state_==AmmoResourceRequestState::Queued?AmmoResourceRejectionReason::QueuedExpired:
                AmmoResourceRejectionReason::DispatchedExpired,*submitted_,now);
            Cancel(now);
        }
    }
    const auto& View()const noexcept{return view_;}
    const auto& Active()const noexcept{return active_;}
    const auto& Inventory()const noexcept{return inventory_;}
    const auto& Outcome()const noexcept{return outcome_;}
    const auto& FirstRejection()const noexcept{return firstRejection_;}
private:
    void NoteRejection(AmmoResourceRejectionReason reason,const AmmoResourceRequest& request,std::int64_t now,
                      std::optional<interaction::AmmunitionSnapshot> adjacent={})noexcept {
        if(firstRejection_)return;
        AmmoResourceRejection row;row.reason=reason;row.processingNowNs=now;row.request=request;
        if(view_){row.currentBinding=view_->binding;row.currentSnapshot=view_->snapshot;}
        row.adjacentSnapshot=adjacent;row.activeCommand=active_;row.state=state_;row.nativeDispatched=nativeDispatched_;
        firstRejection_=row;
    }
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
    std::optional<AmmoResourceRejection> firstRejection_;
    bool nativeDispatched_=false,awaitingPublication_=false;
    AmmoResourceCompletion completion_;
    ReloadHoldIdentity identity_{};
    std::uint64_t lastRequest_=0,request_=0;
    AmmoResourceRequestState state_=AmmoResourceRequestState::None;
};
}
