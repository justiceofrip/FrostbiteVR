#pragma once
#include "Bc2MagazineAmmoMove.h"
#include "Bc2NativeAmmoRefill.h"
#include "Bc2ReloadHold.h"

namespace fvr::bc2 {
// Private diagnostic policy, never an ordinary magazine acknowledgement.
// Only the exact server Update may request the two signed native operations.
// Client observations must come from their own completed original Updates.
enum class AmmoMoveProbePhase:unsigned {Waiting,Removing,Removed,Returning,Returned,Complete,Failed};
struct AmmoMoveProbeDecision {
    MagazineAmmoMove plan{};
    ReloadHoldIdentity identity{};
    std::uint64_t invocation=0;
    bool call=false;
    std::optional<NativeAmmoRefill> refill;
};
class MagazineAmmoRoundtripProbe {
public:
    explicit MagazineAmmoRoundtripProbe(bool refill=false)noexcept:refill_(refill){}
    AmmoMoveProbeDecision Before(const ReloadHoldInput& in,std::uint64_t invocation,
                                 bool idlePolicy,std::int64_t startNs,std::int64_t now)noexcept {
        if(phase_==AmmoMoveProbePhase::Complete||phase_==AmmoMoveProbePhase::Failed||
           phase_==AmmoMoveProbePhase::Removing||phase_==AmmoMoveProbePhase::Returning)return {};
        if(!Valid(in,now)||!invocation||startNs<=0||now<startNs)return {};
        if(phase_!=AmmoMoveProbePhase::Waiting&&in.identity!=identity_){Fail(1);return {};}
        const auto& state=in.branches[in.branch];
        if(phase_==AmmoMoveProbePhase::Waiting){
            if(!idlePolicy||now-startNs<2000000000ll||now-startNs>8000000000ll)return {};
            if(identity_!=in.identity){identity_=in.identity;neutral_={};}
            neutral_[in.branch]=now;
            if(in.branch!=2)return {};
            for(auto t:neutral_)if(t<=0||now<t||now-t>=50000000)return {};
            for(unsigned n=0;n<3;++n){const auto& s=in.branches[n];
                if(s.currentState!=2||s.nextState!=2||s.phaseTimer!=0||s.loaded!=state.loaded||
                   s.reserve!=state.reserve||in.capacities[n]!=in.capacities[2])return {};
            }
            const auto plan=PlanMagazineAmmoMove(MagazineAmmoMoveKind::Remove,state.loaded,state.reserve,in.capacities[2],state.loaded);
            if(!plan||plan->capacity>200)return {};
            // Refuse an impossible refill before removing any original rounds.
            if(refill_&&!PlanNativeAmmoRefill(0,state.reserve,plan->capacity,in.config.reloadType))return {};
            original_=*plan;phase_=AmmoMoveProbePhase::Removing;pendingInvocation_=invocation;
            expectedLoaded_=plan->loaded;expectedReserve_=plan->reserve;
            return {*plan,identity_,invocation,true};
        }
        if(in.branch!=2)return {};
        if(phase_==AmmoMoveProbePhase::Removed){
            if(now<removedNs_){Fail(2);return {};}
            if(now-removedNs_<350000000ll||(!EmptyConverged()&&now-removedNs_<1500000000ll))return {};
            if(state.currentState!=2||state.nextState!=2||state.phaseTimer!=0||
               state.loaded!=0||state.reserve!=original_.reserve||in.capacities[2]!=original_.capacity){Fail(3);return {};}
            const auto plan=PlanMagazineAmmoMove(MagazineAmmoMoveKind::Return,0,original_.reserve,original_.capacity,original_.rounds);
            if(!plan){Fail(4);return {};}
            const auto refill=refill_?PlanNativeAmmoRefill(0,original_.reserve,original_.capacity,in.config.reloadType):std::nullopt;
            if(refill_&&!refill){Fail(10);return {};}
            if(refill){expectedLoaded_=refill->expectedLoaded;expectedReserve_=refill->expectedReserve;}
            phase_=AmmoMoveProbePhase::Returning;pendingInvocation_=invocation;
            return {*plan,identity_,invocation,true,refill};
        }
        if(phase_==AmmoMoveProbePhase::Returned&&now-returnedNs_>2000000000ll)Fail(5);
        return {};
    }
    void Called(const AmmoMoveProbeDecision& decision,bool exact,std::int64_t now)noexcept {
        if(!decision.call||decision.identity!=identity_||decision.invocation!=pendingInvocation_||
           (phase_!=AmmoMoveProbePhase::Removing&&phase_!=AmmoMoveProbePhase::Returning)){Fail(6);return;}
        ++calls_;pendingInvocation_=0;
        if(!exact||now<=0){Fail(7);return;}
        ++exactCalls_;
        if(phase_==AmmoMoveProbePhase::Removing){removedNs_=now;phase_=AmmoMoveProbePhase::Removed;}
        else {returnedNs_=now;phase_=AmmoMoveProbePhase::Returned;}
    }
    void ObserveOwnUpdate(const ReloadHoldIdentity& identity,unsigned branch,const ReloadFiringObservation& state,
                          std::uint64_t invocation,bool retained,std::int64_t now)noexcept {
        if(!retained||identity!=identity_||branch>=3||!invocation||state.address!=identity.firing[branch]||
           state.currentState!=2||state.nextState!=2)return;
        if(phase_==AmmoMoveProbePhase::Removed&&now>=removedNs_&&state.loaded==0&&state.reserve==original_.reserve){
            emptyMask_|=1u<<branch;emptyInvocation_[branch]=invocation;emptyNs_[branch]=now;
        }
        if(phase_==AmmoMoveProbePhase::Returned&&now>=returnedNs_&&state.loaded==expectedLoaded_&&state.reserve==expectedReserve_){
            returnedMask_|=1u<<branch;returnedInvocation_[branch]=invocation;
            if(returnedMask_==7){if(EmptyConverged())phase_=AmmoMoveProbePhase::Complete;else Fail(8);}
        }
    }
    bool EmptyConverged()const noexcept{return emptyMask_==7;}
    AmmoMoveProbePhase Phase()const noexcept{return phase_;}
    unsigned Failure()const noexcept{return failure_;}
    unsigned Calls()const noexcept{return calls_;}unsigned ExactCalls()const noexcept{return exactCalls_;}
    unsigned EmptyMask()const noexcept{return emptyMask_;}unsigned ReturnedMask()const noexcept{return returnedMask_;}
    const auto& Identity()const noexcept{return identity_;}const auto& Original()const noexcept{return original_;}
    const auto& EmptyInvocations()const noexcept{return emptyInvocation_;}
    const auto& ReturnedInvocations()const noexcept{return returnedInvocation_;}
    std::int64_t RemovedNs()const noexcept{return removedNs_;}std::int64_t ReturnedNs()const noexcept{return returnedNs_;}
    bool Refill()const noexcept{return refill_;}int ExpectedLoaded()const noexcept{return expectedLoaded_;}
    int ExpectedReserve()const noexcept{return expectedReserve_;}
    void RejectDispatch()noexcept{Fail(9);}
private:
    static bool Valid(const ReloadHoldInput& in,std::int64_t now)noexcept {
        const auto& o=in.identity.owner;const auto& c=in.context;
        if(!in.verified||in.branch>=3||o.player<0x10000||o.soldier<0x10000||o.weak<0x10000||o.weapon<0x10000||
           !o.actorGeneration||!o.equipGeneration||!o.space||in.identity.serverPlayer<0x10000||
           in.identity.serverSoldier<0x10000||in.identity.serverItem<0x10000||
           in.contextObservedNs<=0||now<in.contextObservedNs||now-in.contextObservedNs>=50000000||now>=in.leaseDeadlineNs||
           !ValidManualReloadDelta(c.deltaSeconds)||c.reloadTimeMultiplier!=1||c.inputFlags||
           c.fireRequested||c.orderRequested||c.reloadRequested||!c.flags24Through28[0]||c.flags24Through28[2]||c.flags24Through28[4])return false;
        for(unsigned n=0;n<3;++n){const auto& s=in.branches[n];
            if(in.identity.firing[n]<0x10000||s.address!=in.identity.firing[n]||s.wrapperOffset!=(n==0?0x3cu:n==1?0x40u:0x10u)||
               s.flagsA8&(8|16)||s.loaded<0||in.capacities[n]<=0||s.loaded>in.capacities[n]||s.reserve<0)return false;
            for(unsigned k=0;k<n;++k)if(in.identity.firing[k]==in.identity.firing[n])return false;
        }
        return true;
    }
    void Fail(unsigned reason)noexcept{if(!failure_)failure_=reason;phase_=AmmoMoveProbePhase::Failed;}
    AmmoMoveProbePhase phase_=AmmoMoveProbePhase::Waiting;
    ReloadHoldIdentity identity_{};MagazineAmmoMove original_{};
    std::array<std::int64_t,3> neutral_{},emptyNs_{};
    std::array<std::uint64_t,3> emptyInvocation_{},returnedInvocation_{};
    std::uint64_t pendingInvocation_=0;
    std::int64_t removedNs_=0,returnedNs_=0;
    unsigned emptyMask_=0,returnedMask_=0,calls_=0,exactCalls_=0,failure_=0;
    bool refill_=false;int expectedLoaded_=0,expectedReserve_=0;
};
}
