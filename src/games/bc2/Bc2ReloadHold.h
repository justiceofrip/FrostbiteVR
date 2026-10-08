#pragma once
#include "Bc2ReloadFlow.h"
#include <atomic>
namespace fvr::bc2 {
struct ReloadHoldIdentity {
    ReloadStateOwner owner{};
    std::array<std::uint32_t,3> firing{};
    std::uint32_t serverPlayer=0,serverSoldier=0,serverItem=0;
    bool operator==(const ReloadHoldIdentity&)const=default;
};
struct ReloadHoldInput {
    ReloadHoldIdentity identity{};ReloadObservedConfig config{};
    std::array<ReloadFiringObservation,3> branches{};
    std::array<std::int32_t,3> capacities{};
    ReloadUpdateContext context{};
    std::int64_t nowNs=0,leaseDeadlineNs=0;
    unsigned branch=3;
    bool verified=false;
    std::int64_t contextObservedNs=0; // Before actual context read; never policy processing time.
};
enum class ReloadHoldTarget:unsigned {Reload,SpasPump};
enum class ReloadHoldPhase:unsigned {Disabled,Waiting,Holding,Released,Aborted};
enum class ReloadHoldReason:unsigned {None,Expired,OwnerOrRead,UnsafeInput,StateChanged,Contention,PatchFailed,RestoreFailed,Stopped};
// A one-shot diagnostic policy. No pointer writes or native calls. All three
// branches and their last verified input contexts must agree before arming.
// The short policy lock is NEVER retained across a native invocation.
class ReloadHoldProbe {
public:
    static constexpr std::int64_t DurationNs=350000000,ContextFreshNs=50000000;
    bool Enable(ReloadHoldTarget target=ReloadHoldTarget::Reload)noexcept;
    ReloadHoldTarget Target()const noexcept{return target_;}
    bool Evaluate(const ReloadHoldInput&)noexcept;
    bool Allows(std::int64_t nowNs)noexcept;
    bool Targets(std::uint32_t firing)const noexcept;
    void Abort(ReloadHoldReason)noexcept;
    void Stop()noexcept;
    ReloadHoldPhase Phase()const noexcept{return phase_.load(std::memory_order_acquire);}
    ReloadHoldReason Reason()const noexcept{return reason_.load(std::memory_order_acquire);}
    std::int64_t BeginNs()const noexcept{return begin_.load();}
    std::int64_t DeadlineNs()const noexcept{return deadline_.load();}
    unsigned Contention()const noexcept{return contention_.load();}
    // Only read after callbacks drain, or once Holding/Released/Aborted was
    // acquired and BeginNs()>0. Armed identity is never modified afterwards.
    const ReloadHoldIdentity& Identity()const noexcept{return armedIdentity_;}
    std::int32_t Loaded()const noexcept{return loaded_;}
    std::int32_t Reserve()const noexcept{return reserve_;}
private:
    ReloadHoldTarget target_=ReloadHoldTarget::Reload; // Configured before callbacks begin.
    std::atomic<ReloadHoldPhase> phase_=ReloadHoldPhase::Disabled;
    std::atomic<ReloadHoldReason> reason_=ReloadHoldReason::None;
    std::atomic<std::int64_t> begin_=0,deadline_=0;
    std::atomic<unsigned> contention_=0;
    std::atomic_flag gate_=ATOMIC_FLAG_INIT;
    ReloadHoldIdentity pendingIdentity_{},armedIdentity_{};
    ReloadObservedConfig armedConfig_{};
    std::array<std::int64_t,3> contexts_{};
    std::int32_t loaded_=0,reserve_=0;
};
bool IsDiagnosticSpasConfig(const ReloadObservedConfig&)noexcept;
struct ReloadDeltaAccess {
    void* context=nullptr;
    bool (*compareExchange)(void*,std::uint32_t expected,std::uint32_t replacement,std::uint32_t& observed)=nullptr;
    bool (*restore)(void*,std::uint32_t value,std::uint32_t& previous)=nullptr;
};
// Trivial value state so the native adapter can use it from __try/__finally.
// Only the original four delta bytes are restored, never the whole context.
struct ReloadDeltaOverride {
    std::uint32_t original=0,beforeRestore=0;
    bool applied=false,restored=false,unexpectedNativeWrite=false;
    bool Apply(const ReloadDeltaAccess&,std::uint32_t expected)noexcept;
    bool Restore(const ReloadDeltaAccess&)noexcept;
};
// Exact original-once invocation with cleanup even on native SEH (MSVC).
// The caller supplies an already validated current-invocation delta address.
void RunReloadDeltaOverride(const ReloadDeltaAccess&,std::uint32_t expected,bool request,
    void (*original)(void*),void* originalContext,ReloadDeltaOverride&);
}
