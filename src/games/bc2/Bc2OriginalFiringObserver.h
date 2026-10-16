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
};
class OriginalFiringScope {
public:
    OriginalFiringScope(const OriginalFiringObserver& observer,OriginalFiringUpdate call)noexcept:observer_(observer),call_(call){
        if(observer_.enter&&observer_.leave)token_=observer_.enter(observer_.context,call_);
    }
    ~OriginalFiringScope(){if(observer_.enter&&observer_.leave)observer_.leave(observer_.context,call_,token_);}
    std::optional<std::array<std::byte,0x30>> InputCopy()noexcept {
        if(copyRequested_)return {};copyRequested_=true;
        return observer_.copyInput?observer_.copyInput(observer_.context,call_,token_):std::nullopt;
    }
    OriginalFiringScope(const OriginalFiringScope&)=delete;
    OriginalFiringScope& operator=(const OriginalFiringScope&)=delete;
private:
    OriginalFiringObserver observer_{};OriginalFiringUpdate call_{};OriginalFiringToken token_{};bool copyRequested_=false;
};
namespace reloadFlowRuntime {
// Must follow Install and precede Start. One independent observer; no replacement.
bool RegisterOriginalFiringObserver(const OriginalFiringObserver&)noexcept;
// Only after successful dispatcher Stop/drain, while consumer still lives.
bool RemoveOriginalFiringObserver(void* exactContext)noexcept;
}
}
