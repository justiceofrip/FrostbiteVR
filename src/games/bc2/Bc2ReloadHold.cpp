#include "Bc2ReloadHold.h"
#include "Bc2ReloadConfigDescriptor.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
namespace fvr::bc2 {
namespace {
bool Owner(const ReloadHoldIdentity& k){
    const auto& o=k.owner;
    return o.player>=0x10000&&o.soldier>=0x10000&&o.weak>=0x10000&&o.weapon>=0x10000&&
        o.actorGeneration&&o.equipGeneration&&o.space&&k.serverPlayer>=0x10000&&k.serverSoldier>=0x10000&&k.serverItem>=0x10000&&
        std::all_of(k.firing.begin(),k.firing.end(),[](auto x){return x>=0x10000;})&&
        k.firing[0]!=k.firing[1]&&k.firing[0]!=k.firing[2]&&k.firing[1]!=k.firing[2];
}
bool ContextSafe(const ReloadUpdateContext& c){
    return std::isfinite(c.deltaSeconds)&&c.deltaSeconds>0&&c.deltaSeconds<=.05f&&
        c.inputFlags==0&&!c.fireRequested&&!c.orderRequested&&!c.reloadRequested&&
        !c.flags24Through28[2]&&!c.flags24Through28[3]&&!c.flags24Through28[4]&&
        std::isfinite(c.reloadTimeMultiplier)&&c.reloadTimeMultiplier>0&&c.reloadTimeMultiplier<=4;
}
bool States(const ReloadHoldInput& in,bool arm,ReloadHoldTarget target){
    const bool pump=target==ReloadHoldTarget::SpasPump;
    for(unsigned n=0;n<3;++n){const auto& b=in.branches[n];
        if(b.address!=in.identity.firing[n]||b.wrapperOffset!=(n==0?0x3cu:n==1?0x40u:0x10u)||
           b.currentState!=(pump?7u:11u)||b.nextState!=(pump?8u:12u)||(pump&&b.previousState!=6)||!std::isfinite(b.phaseTimer)||b.phaseTimer<=0||
           (arm&&(b.phaseTimer<.1f||b.phaseTimer>.25f))||b.phaseTimer>(pump?.5f:1.f)||b.loaded<(pump?1:0)||b.reserve<(pump?0:1)||
           in.capacities[n]<=0||in.capacities[n]>1000000||b.loaded>=in.capacities[n]||(b.flagsA8&(8|16))||
           b.loaded!=in.branches[0].loaded||b.reserve!=in.branches[0].reserve||in.capacities[n]!=in.capacities[0])return false;
    }return true;
}
}
bool IsDiagnosticSpasConfig(const ReloadObservedConfig& c)noexcept {
    return MatchesReloadDescriptor(c,SpasReloadDescriptor);
}
bool ReloadHoldProbe::Enable(ReloadHoldTarget target)noexcept {
    if(Phase()!=ReloadHoldPhase::Disabled||(target!=ReloadHoldTarget::Reload&&target!=ReloadHoldTarget::SpasPump))return false;
    target_=target;auto expected=ReloadHoldPhase::Disabled;return phase_.compare_exchange_strong(expected,ReloadHoldPhase::Waiting);
}
void ReloadHoldProbe::Abort(ReloadHoldReason why)noexcept {
    auto expected=ReloadHoldPhase::Holding;
    if(phase_.compare_exchange_strong(expected,ReloadHoldPhase::Aborted,std::memory_order_acq_rel))reason_.store(why,std::memory_order_release);
}
void ReloadHoldProbe::Stop()noexcept {
    Abort(ReloadHoldReason::Stopped);auto expected=ReloadHoldPhase::Waiting;
    if(phase_.compare_exchange_strong(expected,ReloadHoldPhase::Aborted))reason_.store(ReloadHoldReason::Stopped);
}
bool ReloadHoldProbe::Allows(std::int64_t now)noexcept {
    if(Phase()!=ReloadHoldPhase::Holding)return false;
    if(now<begin_.load()){Abort(ReloadHoldReason::OwnerOrRead);return false;}
    if(now>=deadline_.load()){
        auto expected=ReloadHoldPhase::Holding;
        if(phase_.compare_exchange_strong(expected,ReloadHoldPhase::Released,std::memory_order_acq_rel))reason_.store(ReloadHoldReason::Expired,std::memory_order_release);
        return false;
    }return true;
}
bool ReloadHoldProbe::Targets(std::uint32_t firing)const noexcept {
    return Phase()==ReloadHoldPhase::Holding&&std::find(armedIdentity_.firing.begin(),armedIdentity_.firing.end(),firing)!=armedIdentity_.firing.end();
}
bool ReloadHoldProbe::Evaluate(const ReloadHoldInput& in)noexcept {
    auto phase=Phase();if(phase!=ReloadHoldPhase::Waiting&&phase!=ReloadHoldPhase::Holding)return false;
    if(phase==ReloadHoldPhase::Holding&&!Allows(in.nowNs))return false;
    if(gate_.test_and_set(std::memory_order_acquire)){++contention_;Abort(ReloadHoldReason::Contention);return false;}
    struct Unlock{std::atomic_flag& f;~Unlock(){f.clear(std::memory_order_release);}} unlock{gate_};
    phase=Phase();if(phase!=ReloadHoldPhase::Waiting&&phase!=ReloadHoldPhase::Holding)return false;
    if(!in.verified||in.branch>=3||in.nowNs<=0||in.leaseDeadlineNs<=in.nowNs||
       in.leaseDeadlineNs-in.nowNs>200000000||!Owner(in.identity)||!IsDiagnosticSpasConfig(in.config)){
        if(in.branch<3)contexts_[in.branch]=0;Abort(ReloadHoldReason::OwnerOrRead);return false;
    }
    if(phase==ReloadHoldPhase::Holding&&(in.identity!=armedIdentity_||in.config!=armedConfig_)){
        Abort(ReloadHoldReason::OwnerOrRead);return false;
    }
    if(in.identity!=pendingIdentity_){pendingIdentity_=in.identity;contexts_={};}
    if(!ContextSafe(in.context)){contexts_[in.branch]=0;Abort(ReloadHoldReason::UnsafeInput);return false;}
    contexts_[in.branch]=in.nowNs;
    if(!States(in,phase==ReloadHoldPhase::Waiting,target_)){
        Abort(ReloadHoldReason::StateChanged);return false;
    }
    if(phase==ReloadHoldPhase::Holding&&(in.branches[0].loaded!=loaded_||in.branches[0].reserve!=reserve_)){
        Abort(ReloadHoldReason::StateChanged);return false;
    }
    for(auto time:contexts_)if(time<=0||time>in.nowNs||in.nowNs-time>ContextFreshNs){
        Abort(ReloadHoldReason::UnsafeInput);return false;
    }
    if(phase==ReloadHoldPhase::Waiting){
        if(in.nowNs>std::numeric_limits<std::int64_t>::max()-DurationNs)return false;
        armedIdentity_=in.identity;armedConfig_=in.config;loaded_=in.branches[0].loaded;reserve_=in.branches[0].reserve;
        begin_.store(in.nowNs);deadline_.store(in.nowNs+DurationNs);
        auto expected=ReloadHoldPhase::Waiting;
        if(!phase_.compare_exchange_strong(expected,ReloadHoldPhase::Holding,std::memory_order_release))return false;
    }
    return Allows(in.nowNs);
}
bool ReloadDeltaOverride::Apply(const ReloadDeltaAccess& a,std::uint32_t expected)noexcept {
    if(applied||!a.compareExchange||!a.restore)return false;
    float dt=0;std::memcpy(&dt,&expected,4);if(!std::isfinite(dt)||dt<=0||dt>.05f)return false;
    std::uint32_t observed=0;if(!a.compareExchange(a.context,expected,0u,observed)||observed!=expected)return false;
    original=expected;applied=true;return true;
}
bool ReloadDeltaOverride::Restore(const ReloadDeltaAccess& a)noexcept {
    if(!applied||restored||!a.restore)return false;
    if(!a.restore(a.context,original,beforeRestore))return false;
    unexpectedNativeWrite=beforeRestore!=0;restored=true;return !unexpectedNativeWrite;
}
void RunReloadDeltaOverride(const ReloadDeltaAccess& access,std::uint32_t expected,bool request,
    void (*original)(void*),void* originalContext,ReloadDeltaOverride& transaction){
    if(request)transaction.Apply(access,expected);
#if defined(_MSC_VER)
    __try {original(originalContext);}
    __finally {if(transaction.applied)transaction.Restore(access);}
#else
    struct Restore {const ReloadDeltaAccess& a;ReloadDeltaOverride& t;~Restore(){if(t.applied)t.Restore(a);}} restore{access,transaction};
    original(originalContext);
#endif
}

}
