#pragma once
#include "ControllerInput.h"
#include <algorithm>
#include <cstdint>
#include <optional>

namespace fvr::interaction {
enum class FeedbackKind : std::uint32_t {ReloadCapture=1,ReloadApplied=2};
// Runtime-neutral event. Times are monotonic host/native shared-clock
// nanoseconds, NOT predicted XR display time. No native object crosses IPC.
struct FeedbackEvent {
    std::uint64_t id=0,inputSequence=0,space=0;
    std::int64_t observedNs=0,deadlineNs=0;
    FeedbackKind kind{};std::uint32_t hand=0;
};
inline bool ValidFeedback(const FeedbackEvent& e,std::int64_t now)noexcept {
    return e.id&&e.inputSequence&&e.space&&e.hand<2&&
        (e.kind==FeedbackKind::ReloadCapture||e.kind==FeedbackKind::ReloadApplied)&&
        e.observedNs>0&&now>=e.observedNs&&e.deadlineNs>now&&
        e.deadlineNs>e.observedNs&&e.deadlineNs-e.observedNs<=100000000;
}
struct HapticPulse {unsigned hand=0;float amplitude=0;std::int64_t durationNs=0;};
struct HapticFeedbackResult {bool stop=false;std::optional<HapticPulse> pulse;};
// One instance per XR session. Consume IDs even on rejected delivery; no event
// is retried across focus/menu/tracking loss, reference changes or reconnect.
class HapticFeedbackPolicy {
public:
    void Suspend()noexcept {barrier_=std::max(barrier_,lastInput_);}
    HapticFeedbackResult Update(const InputFrame& input,std::int64_t now,const FeedbackEvent* event=nullptr)noexcept {
        HapticFeedbackResult out;
        const bool transition=space_&&space_!=input.spaceGeneration;
        const bool reversed=(lastNow_&&now<lastNow_)||input.generation<lastInput_;
        const bool safe=now>0&&ValidInput(input)&&input.focused&&input.headValid&&
            input.hands[0].gripTracked&&input.hands[1].gripTracked&&!reversed;
        if(transition){barrier_=input.generation?input.generation-1:0;out.stop=true;}
        if(!safe){barrier_=std::max(barrier_,input.generation);out.stop=true;}
        space_=input.spaceGeneration;lastInput_=input.generation;lastNow_=now;
        if(!event||!event->id||event->id<=seen_)return out;
        seen_=event->id;
        if(!safe||!ValidFeedback(*event,now)||event->space!=space_||
           event->inputSequence<=barrier_||event->inputSequence>input.generation)return out;
        if(event->kind==FeedbackKind::ReloadCapture){
            if(lastCapture_&&now-lastCapture_<60000000)return out;
            // Stronger shell cues after headset feedback; authority and freshness are unchanged.
            lastCapture_=now;out.pulse=HapticPulse{event->hand,.55f,35000000};
        }else out.pulse=HapticPulse{event->hand,.9f,75000000};
        return out;
    }
private:
    std::uint64_t seen_=0,space_=0,barrier_=0,lastInput_=0;
    std::int64_t lastNow_=0,lastCapture_=0;
};
}
