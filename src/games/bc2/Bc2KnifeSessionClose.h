#pragma once
#include <cstdint>
#include <limits>
namespace fvr::bc2::private_knife {
enum class KnifeClosePhase {Unrequested,WaitingForProof,Returning,Complete,Unresolved};
// Serialized under the existing native service lock. This gate has no input
// packet, pose, hand claim, Fire bit or draw operation. It only admits a bounded
// session-close return after the adapter proves a settled already-owned knife.
class KnifeSessionClose {
public:
    static constexpr std::int64_t LimitNs=2000000000ll;
    void Begin(std::int64_t start,bool owned,bool unsettled,std::int64_t now)noexcept {
        if(phase_!=KnifeClosePhase::Unrequested)return;
        started_=start;
        if(start<=0||start>(std::numeric_limits<std::int64_t>::max)()-LimitNs||unsettled){Reject();return;}
        deadline_=start+LimitNs;
        if(now<start||now>=deadline_){Reject();return;}
        phase_=owned?KnifeClosePhase::WaitingForProof:KnifeClosePhase::Complete;
        if(!owned)completed_=now;
    }
    void CheckDeadline(std::int64_t now)noexcept {
        if(phase_!=KnifeClosePhase::WaitingForProof&&phase_!=KnifeClosePhase::Returning)return;
        if(now<started_||now>=deadline_)Reject();
    }
    bool Admit(std::int64_t now,bool currentSettledOwnedProof)noexcept {
        CheckDeadline(now);
        if(phase_!=KnifeClosePhase::WaitingForProof||!currentSettledOwnedProof)return false;
        phase_=KnifeClosePhase::Returning;return true;
    }
    bool Choose(unsigned inventory,std::int64_t now,bool exactNativeReturnBoundary)noexcept {
        CheckDeadline(now);
        if(phase_!=KnifeClosePhase::Returning||inventory>=2||!exactNativeReturnBoundary||(mask_&(1u<<inventory)))return false;
        mask_|=1u<<inventory;return true;
    }
    bool Complete(std::int64_t now,bool allCopiesOriginalIdle,bool callbacksDrained)noexcept {
        CheckDeadline(now);
        if(phase_!=KnifeClosePhase::Returning||mask_!=3||!allCopiesOriginalIdle||!callbacksDrained)return false;
        phase_=KnifeClosePhase::Complete;completed_=now;handback_=true;return true;
    }
    void Reject()noexcept {if(phase_!=KnifeClosePhase::Complete)phase_=KnifeClosePhase::Unresolved;}
    KnifeClosePhase Phase()const noexcept{return phase_;}
    unsigned Mask()const noexcept{return mask_;}
    bool DesktopHandback()const noexcept{return handback_;}
    std::int64_t StartedNs()const noexcept{return started_;}
    std::int64_t DeadlineNs()const noexcept{return deadline_;}
    std::int64_t CompletedNs()const noexcept{return completed_;}
private:
    KnifeClosePhase phase_=KnifeClosePhase::Unrequested;unsigned mask_=0;
    bool handback_=false;std::int64_t started_=0,deadline_=0,completed_=0;
};
}
