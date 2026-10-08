#pragma once
#include <cstdint>
#include <limits>
namespace fvr::graphics {
// Portable coordinator for a single verified native DXGI chain. The adapter
// supplies only read-only observation and SetFullscreenState(FALSE,nullptr).
// It never requests fullscreen, resizes buffers, activates/shows a window,
// changes window styles/association flags, or moves any pointer.
struct DesktopWindowIdentity {
    std::uint64_t chain=0,device=0,window=0;
    std::uint32_t process=0,presentThread=0;
    bool operator==(const DesktopWindowIdentity&)const=default;
};
struct DesktopWindowObservation {
    DesktopWindowIdentity identity{};
    std::uint32_t windowThread=0,swapEffect=0,width=0,height=0;
    std::uint64_t foregroundWindow=0;
    std::int32_t descriptionResult=0,fullscreenResult=0;
    bool windowValid=false,windowClassVerified=false,windowed=false,fullscreen=false,visible=false,minimized=false;
};
struct DesktopWindowModeGuard {
    std::int64_t nowNs=0;
    bool enabled=false,nativeOwnerVerified=false,presentBoundary=false,testPresent=false;
    // SetFullscreenState may synchronously invoke the native WndProc/WM_SIZE.
    // No mod lock, temporary native render state or mod-held backbuffer reference
    // may span that call. The native window thread must remain free to run.
    bool nativeStateRestored=false,noModLocks=false,noModBackbufferReferences=false;
};
struct DesktopWindowModeCalls {
    void* context=nullptr;
    bool (*observe)(void*,DesktopWindowObservation&)=nullptr;
    std::int32_t (*setWindowed)(void*)=nullptr;
};
enum class DesktopWindowModePhase : std::uint8_t {NotStarted,AwaitingRetry,AwaitingConfirmation,Ready,Failed};
enum class DesktopWindowModeReason : std::uint8_t {
    None,Disabled,TestPresent,UnsafeBoundary,InvalidIdentity,OwnerChanged,WrongThread,
    UnsupportedSwapEffect,Minimized,ReadFailed,TransitionFailed,UnexpectedStatus,TimedOut,RetryLimit,InvalidClock,Reentrant
};
struct DesktopWindowModeReport {
    DesktopWindowModePhase phase=DesktopWindowModePhase::NotStarted;
    DesktopWindowModeReason reason=DesktopWindowModeReason::None;
    DesktopWindowObservation before{},after{};
    std::uint32_t attempts=0,observations=0,reentrantCalls=0;
    std::int32_t lastSetResult=0;
    std::int64_t startedNs=0,lastNs=0;
    bool initialObserved=false,afterObserved=false,requested=false,confirmed=false,initiallyWindowed=false;
    bool foregroundChanged=false,foregroundBecameGame=false;
};
// Same window/render thread and DXGI_SWAP_EFFECT_DISCARD only: both are proven
// for the current BC2 chain. Flip-model buffer ownership and other engines need
// their own verified adapter. Construct once per session; no automatic reset
// permits an owner replacement or repeated failure to restart the attempt.
class DesktopWindowMode {
public:
    static constexpr std::int32_t ModeChangeInProgress=0x087a0008;
    static constexpr std::int64_t TimeoutNs=2'000'000'000,RetryNs=100'000'000;
    static constexpr std::uint32_t MaxAttempts=3;
    const DesktopWindowModeReport& Report()const noexcept{return report_;}
    const DesktopWindowModeReport& Step(const DesktopWindowIdentity& expected,
        const DesktopWindowModeGuard& guard,const DesktopWindowModeCalls& calls) noexcept {
        if(busy_){++report_.reentrantCalls;return report_;} // Synchronous WndProc.
        if(report_.phase==DesktopWindowModePhase::Ready||report_.phase==DesktopWindowModePhase::Failed)return report_;
        if(!guard.enabled){report_.reason=DesktopWindowModeReason::Disabled;return report_;}
        if(guard.testPresent){report_.reason=DesktopWindowModeReason::TestPresent;return report_;}
        if(!guard.nativeOwnerVerified||!guard.presentBoundary||!guard.nativeStateRestored||!guard.noModLocks||!guard.noModBackbufferReferences){
            report_.reason=DesktopWindowModeReason::UnsafeBoundary;return report_;}
        if(!expected.chain||!expected.device||!expected.window||!expected.process||!expected.presentThread)
            return Fail(DesktopWindowModeReason::InvalidIdentity);
        if(guard.nowNs<=0||(report_.lastNs&&guard.nowNs<report_.lastNs))
            return Fail(DesktopWindowModeReason::InvalidClock);
        if(report_.initialObserved&&expected!=expected_)return Fail(DesktopWindowModeReason::OwnerChanged);
        if(report_.startedNs&&guard.nowNs-report_.startedNs>=TimeoutNs)return Fail(DesktopWindowModeReason::TimedOut);
        if(!calls.observe||!calls.setWindowed)return Fail(DesktopWindowModeReason::ReadFailed);
        busy_=true;struct BusyScope{bool& b;~BusyScope(){b=false;}} busyScope{busy_};
        report_.lastNs=guard.nowNs;
        DesktopWindowObservation observed{};
        const bool readOkay=calls.observe(calls.context,observed);++report_.observations;
        if(!report_.initialObserved){report_.before=observed;report_.initialObserved=readOkay;expected_=expected;
            if(readOkay)report_.startedNs=guard.nowNs;}
        else {report_.after=observed;report_.afterObserved=readOkay;UpdateForeground(observed,readOkay);}
        if(!readOkay||observed.descriptionResult!=0||observed.fullscreenResult!=0)return Fail(DesktopWindowModeReason::ReadFailed);
        const auto validity=Validate(observed,expected);if(validity!=DesktopWindowModeReason::None)return Fail(validity);
        if(observed.windowed&&!observed.fullscreen){
            report_.initiallyWindowed=!report_.requested;report_.confirmed=true;report_.phase=DesktopWindowModePhase::Ready;
            report_.reason=DesktopWindowModeReason::None;return report_;}
        // Disagreeing query/description values indicate an in-flight native mode
        // transition. Observe its completion; do not issue a competing request.
        if(observed.windowed==observed.fullscreen){
            report_.phase=report_.requested&&report_.lastSetResult==ModeChangeInProgress?
                DesktopWindowModePhase::AwaitingRetry:DesktopWindowModePhase::AwaitingConfirmation;
            report_.reason=DesktopWindowModeReason::None;return report_;}
        if(report_.phase==DesktopWindowModePhase::AwaitingConfirmation&&report_.requested)return report_;
        if(report_.phase==DesktopWindowModePhase::AwaitingRetry&&guard.nowNs<retryAt_)return report_;
        if(report_.attempts>=MaxAttempts)return Fail(DesktopWindowModeReason::RetryLimit);
        ++report_.attempts;report_.requested=true;
        report_.lastSetResult=calls.setWindowed(calls.context);
        auto failure=DesktopWindowModeReason::None;
        if(report_.lastSetResult==ModeChangeInProgress){
            report_.phase=DesktopWindowModePhase::AwaitingRetry;report_.reason=DesktopWindowModeReason::None;
            retryAt_=guard.nowNs>std::numeric_limits<std::int64_t>::max()-RetryNs?
                std::numeric_limits<std::int64_t>::max():guard.nowNs+RetryNs;
        }else if(report_.lastSetResult==0){
            report_.phase=DesktopWindowModePhase::AwaitingConfirmation;report_.reason=DesktopWindowModeReason::None;
        }else failure=report_.lastSetResult<0?DesktopWindowModeReason::TransitionFailed:DesktopWindowModeReason::UnexpectedStatus;
        // S_OK is not sufficient evidence. Capture the post-call state/focus
        // immediately, then allow a later Present to confirm native WM_SIZE.
        DesktopWindowObservation after{};
        const bool afterOkay=calls.observe(calls.context,after);++report_.observations;
        report_.after=after;report_.afterObserved=afterOkay;UpdateForeground(after,afterOkay);
        if(!afterOkay||after.descriptionResult!=0||after.fullscreenResult!=0)return Fail(DesktopWindowModeReason::ReadFailed);
        const auto afterValidity=Validate(after,expected);if(afterValidity!=DesktopWindowModeReason::None)return Fail(afterValidity);
        if(after.windowed&&!after.fullscreen){report_.confirmed=true;report_.phase=DesktopWindowModePhase::Ready;}
        if(failure!=DesktopWindowModeReason::None)return Fail(failure);
        return report_;
    }
private:
    static DesktopWindowModeReason Validate(const DesktopWindowObservation& o,const DesktopWindowIdentity& expected)noexcept{
        if(o.identity!=expected||!o.windowValid||!o.windowClassVerified||!o.width||!o.height)
            return DesktopWindowModeReason::OwnerChanged;
        if(o.windowThread!=expected.presentThread)return DesktopWindowModeReason::WrongThread;
        if(o.swapEffect!=0)return DesktopWindowModeReason::UnsupportedSwapEffect;
        if(o.minimized)return DesktopWindowModeReason::Minimized;
        return DesktopWindowModeReason::None;
    }
    void UpdateForeground(const DesktopWindowObservation& o,bool valid)noexcept{
        if(!valid||!report_.initialObserved)return;
        report_.foregroundChanged|=o.foregroundWindow!=report_.before.foregroundWindow;
        report_.foregroundBecameGame|=report_.before.foregroundWindow!=expected_.window&&o.foregroundWindow==expected_.window;
    }
    const DesktopWindowModeReport& Fail(DesktopWindowModeReason reason)noexcept{
        report_.phase=DesktopWindowModePhase::Failed;report_.reason=reason;return report_;
    }
    DesktopWindowIdentity expected_{};
    DesktopWindowModeReport report_{};
    std::int64_t retryAt_=0;
    bool busy_=false;
};
}
