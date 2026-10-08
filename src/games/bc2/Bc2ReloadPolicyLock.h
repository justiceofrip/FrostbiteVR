#pragma once
#include <atomic>
#include <cstdint>

namespace fvr::bc2 {
enum class ReloadPolicyLockFailure:unsigned {None,Deadline,AttemptLimit,Clock};
struct ReloadPolicyLockEvidence {
    bool held=false,contended=false;
    ReloadPolicyLockFailure failure=ReloadPolicyLockFailure::None;
    unsigned attempts=0;
    std::int64_t beginNs=0,endNs=0;
};
// Serialize only the short in-memory request policy operation. Native reads,
// originals and callbacks belong OUTSIDE this scope. A brief normal overlap
// gets a bounded retry; a stalled/reentrant owner still fails closed.
class ReloadPolicyLock {
    std::atomic_flag& gate_;
    ReloadPolicyLockEvidence evidence_{};
public:
    static constexpr std::int64_t WaitNs=250000;
    static constexpr unsigned MaxAttempts=4096;
    template<class Clock,class Pause>
    ReloadPolicyLock(std::atomic_flag& gate,Clock&& clock,Pause&& pause,bool retry=true)noexcept:gate_(gate) {
        evidence_.attempts=1;
        if(!gate_.test_and_set(std::memory_order_acquire)){evidence_.held=true;return;}
        evidence_.contended=true;
        if(!retry){evidence_.failure=ReloadPolicyLockFailure::AttemptLimit;return;}
        evidence_.beginNs=evidence_.endNs=clock();
        if(evidence_.beginNs<=0){evidence_.failure=ReloadPolicyLockFailure::Clock;return;}
        while(evidence_.attempts<MaxAttempts){
            pause();evidence_.endNs=clock();
            if(!WithinBudget())return;
            ++evidence_.attempts;
            if(!gate_.test_and_set(std::memory_order_acquire)){
                evidence_.endNs=clock();
                if(!WithinBudget()){gate_.clear(std::memory_order_release);return;}
                evidence_.held=true;return;
            }
        }
        evidence_.failure=ReloadPolicyLockFailure::AttemptLimit;
    }
    ~ReloadPolicyLock(){if(evidence_.held)gate_.clear(std::memory_order_release);}
    ReloadPolicyLock(const ReloadPolicyLock&)=delete;
    ReloadPolicyLock& operator=(const ReloadPolicyLock&)=delete;
    bool Held()const noexcept{return evidence_.held;}
    const ReloadPolicyLockEvidence& Evidence()const noexcept{return evidence_;}
private:
    bool WithinBudget()noexcept {
        if(evidence_.endNs<evidence_.beginNs){evidence_.failure=ReloadPolicyLockFailure::Clock;return false;}
        if(evidence_.endNs-evidence_.beginNs>=WaitNs){evidence_.failure=ReloadPolicyLockFailure::Deadline;return false;}
        return true;
    }
};
}
