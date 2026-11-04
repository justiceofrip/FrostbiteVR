#pragma once
#include <array>
#include <cstdint>
#include <cstddef>
#include <optional>
namespace fvr::bc2 {
// Callback composition, immutable from Start through successful drain.
// enter/leave observe only. The optional explicit copy callback may supply one
// caller-local Update input; dispatcher retains the only native invocation.
struct OriginalFiringUpdate {
    std::uint32_t firing=0,context=0,caller=0,argument=0;
};
struct OriginalFiringToken {std::array<std::uint64_t,8> words{};};
struct OriginalFiringObserver {
    void* context=nullptr;
    OriginalFiringToken (*enter)(void*,const OriginalFiringUpdate&)noexcept=nullptr;
    void (*leave)(void*,const OriginalFiringUpdate&,const OriginalFiringToken&)noexcept=nullptr;
    std::optional<std::array<std::byte,0x30>> (*copyInput)(void*,const OriginalFiringUpdate&,OriginalFiringToken&)noexcept=nullptr;
    // A consumer with native-call state supplies a distinct abnormal exit.
    void (*abort)(void*,const OriginalFiringUpdate&,const OriginalFiringToken&)noexcept=nullptr;
};
class OriginalFiringScope {
public:
    OriginalFiringScope(const OriginalFiringObserver& observer,OriginalFiringUpdate call)noexcept:observer_(observer),call_(call){
        previous_=current_;current_=this;
        if(observer_.enter&&observer_.leave)token_=observer_.enter(observer_.context,call_);
    }
    ~OriginalFiringScope(){
        if(closed_)return;
        if(observer_.enter&&observer_.leave)observer_.leave(observer_.context,call_,token_);
        current_=previous_;
    }
    // The native unwind leaf calls this while the owning hook frame is live.
    // A subsequent C++ destructor must neither publish a return nor pop twice.
    void Abort()noexcept {
        if(closed_)return;
        closed_=true;copied_=false;nativeContext_=0;current_=previous_;
        if(observer_.enter&&observer_.leave&&observer_.abort)observer_.abort(observer_.context,call_,token_);
    }
    std::optional<std::array<std::byte,0x30>> InputCopy()noexcept {
        if(copyRequested_)return {};copyRequested_=true;
        auto result=observer_.copyInput?observer_.copyInput(observer_.context,call_,token_):std::nullopt;
        copied_=bool(result);return result;
    }
    // Exact same-thread original invocation only. A copied input is exposed
    // after copyInput returns; nested calls shadow and restore this scope.
    static const OriginalFiringScope* Current()noexcept{return current_;}
    const OriginalFiringUpdate& Call()const noexcept{return call_;}
    const OriginalFiringToken& Token()const noexcept{return token_;}
    void* ObserverContext()const noexcept{return observer_.context;}
    bool Copied()const noexcept{return copied_;}
    void BindCopiedContext(std::uintptr_t context)noexcept {if(copied_)nativeContext_=context;}
    std::uintptr_t NativeContext()const noexcept{return copied_?nativeContext_:call_.context;}
    OriginalFiringScope(const OriginalFiringScope&)=delete;
    OriginalFiringScope& operator=(const OriginalFiringScope&)=delete;
private:
    inline static thread_local const OriginalFiringScope* current_=nullptr;
    const OriginalFiringScope* previous_=nullptr;std::uintptr_t nativeContext_=0;bool copied_=false;
    OriginalFiringObserver observer_{};OriginalFiringUpdate call_{};OriginalFiringToken token_{};bool copyRequested_=false,closed_=false;
};
namespace reloadFlowRuntime {
// Must follow Install and precede Start. One independent observer; no replacement.
bool RegisterOriginalFiringObserver(const OriginalFiringObserver&)noexcept;
// Only after successful dispatcher Stop/drain, while consumer still lives.
bool RemoveOriginalFiringObserver(void* exactContext)noexcept;
}
}
