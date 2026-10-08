#pragma once
#include <atomic>
namespace fvr::bc2 {
enum class RigWorkerEnableResult {Enabled,AlreadyEnabled,Failed};
// A global MinHook enable may partly succeed. From BeginGlobalStart onward,
// quiescence requires an explicit successful disable, even if admission never
// opened. This policy contains no native calls and is independently testable.
class RigWorkerLifecycle {
public:
    enum class State {Absent,Disabled,GlobalStartPending,Admitted,DisableRequired};
    bool Created()noexcept {auto expected=State::Absent;return state_.compare_exchange_strong(expected,State::Disabled);}
    bool BeginGlobalStart()noexcept {auto expected=State::Disabled;return state_.compare_exchange_strong(expected,State::GlobalStartPending);}
    bool PendingGlobalStart()const noexcept{return state_.load()==State::GlobalStartPending;}
    bool CompleteGlobalStart(bool globalSucceeded,RigWorkerEnableResult result)noexcept {
        auto expected=State::GlobalStartPending;
        const bool accept=globalSucceeded&&(result==RigWorkerEnableResult::Enabled||result==RigWorkerEnableResult::AlreadyEnabled);
        return state_.compare_exchange_strong(expected,accept?State::Admitted:State::DisableRequired)&&accept;
    }
    bool Admitted()const noexcept{return state_.load()==State::Admitted;}
    bool EntryMayBeEnabled()const noexcept {const auto s=state_.load();return s==State::GlobalStartPending||s==State::Admitted||s==State::DisableRequired;}
    void StopAdmission()noexcept {auto s=state_.load();while(s!=State::Absent&&s!=State::Disabled&&s!=State::DisableRequired&&
        !state_.compare_exchange_weak(s,State::DisableRequired)){};}
    void ConfirmDisable(bool succeeded)noexcept {
        StopAdmission();if(!succeeded)return;
        auto s=state_.load();while(s!=State::Absent&&s!=State::Disabled&&!state_.compare_exchange_weak(s,State::Disabled)){};
    }
    bool Quiescent(unsigned inFlight)const noexcept {const auto s=state_.load();return !inFlight&&(s==State::Absent||s==State::Disabled);}
private:
    std::atomic<State> state_{State::Absent};
};
}
