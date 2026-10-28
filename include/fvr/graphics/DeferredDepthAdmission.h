#pragma once
#include <cstdint>
namespace fvr::graphics {
// Identity only. The graphics adapter must still validate its exact lease,
// device/context owner and native lifetime before admitting any resource work.
struct DepthAdmissionKey {
    std::uint64_t channel=0,request=0,owner=0,frame=0,device=0,tracking=0,space=0;
    std::int64_t predicted=0;
    unsigned slot=0,slots=0;
    bool operator==(const DepthAdmissionKey&)const=default;
    bool Valid()const noexcept {
        return channel&&request&&owner&&frame&&device&&tracking&&space&&predicted>0&&
            (slots==1||slots==2||slots==3)&&slot<slots;
    }
};
enum class DepthAdmissionResult {Ready,Busy,Rejected};
// A failed try-lock at eye-scope entry must not lose the first clear/source.
// Retain observation on Busy only. Retry once at the first required world-depth
// copy, before its source is cleared. Never wait, retry at a later stage, or
// turn an unavailable optional attachment into a fatal/color-pair failure.
class DeferredDepthAdmission {
public:
    template<class Attempt> bool Start(const DepthAdmissionKey& key,Attempt&& attempt)noexcept {
        if(started_)return false;
        started_=true;key_=key;
        result_=key.Valid()?attempt():DepthAdmissionResult::Rejected;
        return result_!=DepthAdmissionResult::Rejected;
    }
    template<class Attempt> bool BeforeWorldCopy(const DepthAdmissionKey& current,Attempt&& attempt)noexcept {
        if(!started_||worldBoundaryUsed_)return false;
        worldBoundaryUsed_=true;
        if(!current.Valid()||current!=key_){result_=DepthAdmissionResult::Rejected;return false;}
        if(result_==DepthAdmissionResult::Busy){
            // Consume permission before invoking the adapter. Persistent Busy
            // cannot defer the world snapshot past its destructive clear.
            result_=DepthAdmissionResult::Rejected;
            const auto retried=attempt();
            if(retried==DepthAdmissionResult::Ready)result_=retried;
        }
        return result_==DepthAdmissionResult::Ready;
    }
private:
    DepthAdmissionKey key_{};
    DepthAdmissionResult result_=DepthAdmissionResult::Rejected;
    bool started_=false,worldBoundaryUsed_=false;
};
}
