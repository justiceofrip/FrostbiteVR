#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <optional>
#include <ostream>
namespace fvr::bc2 {
enum class AmmoResourceReadReason:unsigned {Available,Busy,NoView,Revision,Owner,Future,Expired,Invalid};
struct AmmoResourceReadRow {
    AmmoResourceReadReason reason=AmmoResourceReadReason::Available;
    std::int64_t requestedNow=0,processingNow=0,bindingObserved=0,bindingDeadline=0,snapshotObserved=0,snapshotDeadline=0;
    std::uint64_t revision=0,published=0,current=0,request=0;
    std::uint32_t wantedWeapon=0,publishedWeapon=0;
};
// Diagnostic only. No retries, prior-view reuse, validity extension or authority.
// Retain the first failure of each reason and the first failure after an
// accepted operation view. A long shutdown tail cannot overwrite the evidence
// at the failed gesture; the last-failure ring still reports its explicit loss.
class AmmoResourceReadEvidence {
public:
    void Note(const AmmoResourceReadRow& row)noexcept {
        counts_[unsigned(row.reason)].fetch_add(1,std::memory_order_relaxed);
        if(gate_.test_and_set(std::memory_order_acquire)){
            (row.reason==AmmoResourceReadReason::Available?availableBusy_:busy_).fetch_add(1,std::memory_order_relaxed);return;
        }
        if(row.reason==AmmoResourceReadReason::Available){
            lastAvailable_=row;gate_.clear(std::memory_order_release);return;
        }
        auto& first=firstByReason_[unsigned(row.reason)];if(!first)first=row;
        if(!firstOperationFailure_&&lastAvailable_&&lastAvailable_->request){
            firstOperationFailure_=row;priorOperationView_=lastAvailable_;
        }
        rows_[total_%rows_.size()]=row;++total_;gate_.clear(std::memory_order_release);
    }
    void Report(std::ostream& out,bool drained)const {
        out<<"{\"schema\":2,\"drained\":"<<(drained?"true":"false")
           <<",\"capacity\":"<<rows_.size()<<",\"reason_names\":[\"available\",\"busy\",\"no_view\",\"revision\",\"owner\",\"future\",\"expired\",\"invalid\"],\"counts\":[";
        for(unsigned n=0;n<counts_.size();++n){if(n)out<<',';out<<counts_[n].load(std::memory_order_relaxed);}out<<']';
        if(drained){const auto count=total_<rows_.size()?total_:rows_.size();
            out<<",\"total\":"<<total_<<",\"overwritten\":"<<(total_-count)<<",\"busy_dropped\":"<<busy_.load()<<",\"rows\":[";
            const auto row=[&](const AmmoResourceReadRow& r){
                out<<"{\"reason\":"<<unsigned(r.reason)<<",\"requested_now_ns\":"<<r.requestedNow<<",\"processing_now_ns\":"<<r.processingNow
                   <<",\"binding_observed_ns\":"<<r.bindingObserved<<",\"binding_deadline_ns\":"<<r.bindingDeadline
                   <<",\"snapshot_observed_ns\":"<<r.snapshotObserved<<",\"snapshot_deadline_ns\":"<<r.snapshotDeadline
                   <<",\"revision\":"<<r.revision<<",\"published_revision\":"<<r.published<<",\"current_revision\":"<<r.current
                   <<",\"request\":"<<r.request<<",\"wanted_weapon\":"<<r.wantedWeapon<<",\"published_weapon\":"<<r.publishedWeapon<<'}';
            };
            for(std::uint64_t n=total_-count;n<total_;++n){if(n!=total_-count)out<<',';row(rows_[n%rows_.size()]);}out<<']';
            out<<",\"first_by_reason\":[";
            for(unsigned n=0;n<firstByReason_.size();++n){if(n)out<<',';if(firstByReason_[n])row(*firstByReason_[n]);else out<<"null";}out<<']';
            out<<",\"first_operation_failure\":";if(firstOperationFailure_)row(*firstOperationFailure_);else out<<"null";
            out<<",\"prior_operation_view\":";if(priorOperationView_)row(*priorOperationView_);else out<<"null";
            out<<",\"available_busy_dropped\":"<<availableBusy_.load(std::memory_order_relaxed);
        }out<<'}';
    }
private:
    std::array<std::atomic<std::uint64_t>,8> counts_{};
    std::atomic<std::uint64_t> busy_=0,availableBusy_=0;std::atomic_flag gate_=ATOMIC_FLAG_INIT;
    std::array<AmmoResourceReadRow,128> rows_{};std::uint64_t total_=0;
    std::array<std::optional<AmmoResourceReadRow>,8> firstByReason_{};
    std::optional<AmmoResourceReadRow> lastAvailable_,firstOperationFailure_,priorOperationView_;
};
}
