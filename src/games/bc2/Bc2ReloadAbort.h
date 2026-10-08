#pragma once
#include "Bc2ReloadRequestCycle.h"
#include "Bc2MagazineNativeProfile.h"
#include <cstring>
#include <limits>
namespace fvr::bc2 {
// Stop-only cleanup, not a reload request or a retirement/transfer receipt.
// Serialized by the adapter's request lock; no lock spans any native call.
enum class ReloadAbortFailure:unsigned {None,Expired,Owner,Cycle,Input,State,NativePostcondition,UpdatePostcondition,NewCycle,Stopped};
enum class ReloadAbortAdmission:unsigned {None,DeferredFire,AlreadyClaimed,AlreadyIdle,HelperClaimed,Rejected};
// Value-only history, serialized by the existing request lock. No additional
// native reads, input writes, allocations, or capability deadline renewal.
struct ReloadAbortAdmissionSample {
    std::int64_t observedNs=0;std::uint64_t invocation=0;
    unsigned branch=3;ReloadUpdateContext context{};ReloadFiringObservation before{};
};
struct ReloadAbortCycleReport {
    std::uint64_t cycle=0,revision=0,claims=0,fireDeferred=0,dispatchEpoch=0;
    std::int64_t armedNs=0,deadlineNs=0,terminalNs=0;
    std::int32_t loaded=0,reserve=0;
    unsigned claimed=0,completed=0;std::array<std::uint64_t,3> deferredByBranch{};
    ReloadAbortFailure failure{};ReloadAbortAdmission lastAdmission{};bool active=false;
    ReloadAbortAdmissionSample first{},last{},firstDeferred{};
};
struct ReloadAbortDecision {
    std::uint64_t cycle=0,invocation=0,ownerRevision=0;
    unsigned branch=3;std::int64_t deadlineNs=0;
    ReloadFiringObservation before{};
    bool call=false;std::uint64_t dispatchEpoch=0;
};
enum class ReloadAbortTiming:unsigned {Spas,VerifiedXm8Magazine};
class ReloadAbortCleanup {
public:
    // Adapter-only namespace transition after exact old-cycle retirement and
    // shared callback drain. Histories survive; stale decisions cannot complete
    // a same-numbered cycle belonging to another native reload family.
    bool BeginDispatchEpoch(std::uint64_t epoch,bool callbacksDrained)noexcept {
        if(!callbacksDrained||active_||!epoch||epoch<=dispatchEpoch_)return false;
        dispatchEpoch_=epoch;lastCycle_=0;lease_={};invocation_={};revision_=0;
        claimed_=completed_=0;deadline_=began_=0;failure_=ReloadAbortFailure::None;return true;
    }
    bool Arm(const ReloadRoundLease& lease,std::uint64_t revision,std::int64_t now,
        bool heldNoPending,ReloadAbortTiming timing=ReloadAbortTiming::Spas)noexcept {
        if(timing==ReloadAbortTiming::VerifiedXm8Magazine)return ArmMagazine(lease,revision,now,heldNoPending,Xm8MagazineNativeProfile);
        return timing==ReloadAbortTiming::Spas&&ArmWithCeiling(lease,revision,now,heldNoPending,1.f);
    }
    bool ArmMagazine(const ReloadRoundLease& lease,std::uint64_t revision,std::int64_t now,
        bool heldNoPending,const MagazineNativeProfile& profile,bool emptyStepControl=false)noexcept {
        return profile.Reviewed()&&ArmWithCeiling(lease,revision,now,heldNoPending,profile.HoldCeiling(),emptyStepControl);
    }
private:
    bool ArmWithCeiling(const ReloadRoundLease& lease,std::uint64_t revision,std::int64_t now,
        bool heldNoPending,float ceiling,bool emptyStepControl=false)noexcept {
        if(!std::isfinite(ceiling)||ceiling<=0||ceiling>10||!heldNoPending||!revision||!lease.cycle||lease.cycle<=lastCycle_||!lease.sequence||
           !lease.nativeBindingVerified||!lease.allThreeHeld||lease.loaded<0||(!lease.loaded&&!emptyStepControl)||lease.reserve<0||
           lease.capacity<lease.loaded||lease.capacity>1000000||lease.observedNs<=0||
           now<lease.observedNs||now>=lease.deadlineNs||lease.deadlineNs-lease.observedNs>200000000||
           now>std::numeric_limits<std::int64_t>::max()-200000000)return false;
        Abandon(ReloadAbortFailure::NewCycle,now);
        timerCeiling_=ceiling;lease_=lease;lastCycle_=lease.cycle;revision_=revision;began_=now;
        deadline_=std::min(lease.deadlineNs,now+200000000);claimed_=completed_=0;
        invocation_={};active_=true;failure_=ReloadAbortFailure::None;
        auto& r=history_[historyTotal_++%HistoryCapacity];r={};r.cycle=lease.cycle;r.revision=revision;r.dispatchEpoch=dispatchEpoch_;
        r.armedNs=now;r.deadlineNs=deadline_;r.loaded=lease.loaded;r.reserve=lease.reserve;r.active=true;return true;
    }
public:
    void Abandon(ReloadAbortFailure why,std::int64_t now=0)noexcept {
        if(active_){active_=false;failure_=why;SyncHistory(now);}
    }
    ReloadAbortDecision Claim(const ReloadHoldInput& in,std::uint64_t revision,
        std::uint64_t policyCycle,bool policyCancelled,std::uint64_t invocation,
        bool nativeConfigVerified,std::int64_t now)noexcept {
        ReloadAbortDecision d;if(!active_)return d;
        auto& report=CurrentHistory();++report.claims;
        report.last={now,invocation,in.branch,in.context,in.branch<3?in.branches[in.branch]:ReloadFiringObservation{}};
        if(report.claims==1)report.first=report.last;
        const auto fail=[&](ReloadAbortFailure why){report.lastAdmission=ReloadAbortAdmission::Rejected;Abandon(why,now);return d;};
        if(now<began_||now>=deadline_)return fail(ReloadAbortFailure::Expired);
        if(!in.verified||in.identity!=lease_.identity||revision!=revision_||
           in.nowNs<began_||in.nowNs>now||now>=in.leaseDeadlineNs)return fail(ReloadAbortFailure::Owner);
        if(!policyCancelled||policyCycle!=lease_.cycle)return fail(ReloadAbortFailure::Cycle);
        if(in.branch>=3||!invocation||!nativeConfigVerified)return fail(ReloadAbortFailure::State);
        if(claimed_&(1u<<in.branch)){report.lastAdmission=ReloadAbortAdmission::AlreadyClaimed;return d;}
        const auto& c=in.context;
        const bool fireOnly=c.inputFlags==1&&c.fireRequested&&!c.orderRequested&&!c.reloadRequested;
        if(!std::isfinite(c.deltaSeconds)||c.deltaSeconds<=0||c.deltaSeconds>.05f||c.reloadTimeMultiplier!=1||
           (!fireOnly&&(c.inputFlags||c.fireRequested||c.orderRequested||c.reloadRequested))||
           c.flags24Through28[2]||c.flags24Through28[3]||c.flags24Through28[4])return fail(ReloadAbortFailure::Input);
        const auto& b=in.branches[in.branch];
        if(b.address!=lease_.identity.firing[in.branch]||b.wrapperOffset!=(in.branch==0?0x3cu:in.branch==1?0x40u:0x10u)||
           b.loaded!=lease_.loaded||b.reserve!=lease_.reserve||(b.flagsA8&(8|16))||
           !std::isfinite(b.phaseTimer)||b.phaseTimer<0||b.phaseTimer>timerCeiling_)return fail(ReloadAbortFailure::State);
        const bool idle=(b.currentState==1||b.currentState==2)&&(b.nextState==1||b.nextState==2);
        if(!idle&&(b.currentState<10||b.currentState>12||(b.nextState!=1&&(b.nextState<10||b.nextState>12))))return fail(ReloadAbortFailure::State);
        // Genuine Fire remains unchanged and executes through original Update.
        // Only wait for a later NEUTRAL callback inside the ORIGINAL deadline;
        // combined helper+Fire execution has not been verified. State/ammo may
        // change naturally while waiting and must reject that later attempt.
        if(fireOnly){
            report.lastAdmission=ReloadAbortAdmission::DeferredFire;
            if(!report.fireDeferred)report.firstDeferred=report.last;
            ++report.fireDeferred;++report.deferredByBranch[in.branch];return d;
        }
        if(idle){
            report.lastAdmission=ReloadAbortAdmission::AlreadyIdle;
            claimed_|=1u<<in.branch;completed_|=1u<<in.branch;if(completed_==7)active_=false;SyncHistory(now);return d;
        }
        report.lastAdmission=ReloadAbortAdmission::HelperClaimed;
        claimed_|=1u<<in.branch;invocation_[in.branch]=invocation;
        SyncHistory(now);
        d={lease_.cycle,invocation,revision_,in.branch,deadline_,b,true,dispatchEpoch_};return d;
    }
    bool Allows(const ReloadAbortDecision& d,std::uint64_t revision,std::uint64_t policyCycle,
        bool cancelled,std::int64_t now)noexcept {
        if(d.dispatchEpoch!=dispatchEpoch_||!active_||!d.call||d.branch>=3||d.invocation!=invocation_[d.branch]||d.cycle!=lease_.cycle)return false;
        if(now<began_||now>=deadline_){Abandon(ReloadAbortFailure::Expired,now);return false;}
        if(revision!=revision_){Abandon(ReloadAbortFailure::Owner,now);return false;}
        if(policyCycle!=lease_.cycle||!cancelled){Abandon(ReloadAbortFailure::Cycle,now);return false;}return true;
    }
    bool Finish(const ReloadAbortDecision& d,const ReloadFiringObservation& after,
        bool ownerRetained,bool helperExact,std::int64_t now)noexcept {
        if(d.dispatchEpoch!=dispatchEpoch_||!d.call||d.branch>=3||d.cycle!=lease_.cycle||d.invocation!=invocation_[d.branch])return false;
        if(!helperExact){Abandon(ReloadAbortFailure::NativePostcondition,now);return false;}
        if(now<began_||now>=deadline_){Abandon(ReloadAbortFailure::Expired,now);return false;}
        if(!ownerRetained||after.address!=d.before.address||after.wrapperOffset!=d.before.wrapperOffset||
           after.loaded!=d.before.loaded||after.reserve!=d.before.reserve||
           (after.currentState!=1&&after.currentState!=2)||(after.nextState!=1&&after.nextState!=2)){
            Abandon(ReloadAbortFailure::UpdatePostcondition,now);return false;
        }
        if(!active_)return false;completed_|=1u<<d.branch;if(completed_==7)active_=false;SyncHistory(now);return true;
    }
    bool Active()const noexcept{return active_;}
    unsigned Claimed()const noexcept{return claimed_;}
    unsigned Completed()const noexcept{return completed_;}
    std::uint64_t Cycle()const noexcept{return lease_.cycle;}
    std::uint64_t Revision()const noexcept{return revision_;}
    std::int64_t Deadline()const noexcept{return deadline_;}
    const ReloadRoundLease& OriginalLease()const noexcept{return lease_;}
    ReloadAbortFailure Failure()const noexcept{return failure_;}
    static constexpr std::uint64_t HistoryCapacity=32;
    std::uint64_t HistoryTotal()const noexcept{return historyTotal_;}
    std::uint64_t HistoryCount()const noexcept{return std::min(historyTotal_,HistoryCapacity);}
    // Oldest retained first. Call only for index<HistoryCount(), under the
    // request lock or once native callbacks have drained, like other fields.
    const ReloadAbortCycleReport& History(std::uint64_t index)const noexcept {
        return history_[(historyTotal_-HistoryCount()+index)%HistoryCapacity];
    }
private:
    ReloadAbortCycleReport& CurrentHistory()noexcept{return history_[(historyTotal_-1)%HistoryCapacity];}
    void SyncHistory(std::int64_t now)noexcept {
        if(!historyTotal_)return;auto& r=CurrentHistory();r.claimed=claimed_;r.completed=completed_;
        r.active=active_;r.failure=failure_;if(!active_)r.terminalNs=now;
    }
    std::uint64_t dispatchEpoch_=0;
    float timerCeiling_=1.f;
    ReloadRoundLease lease_{};std::array<std::uint64_t,3> invocation_{};
    std::uint64_t revision_=0,lastCycle_=0;std::int64_t began_=0,deadline_=0;
    unsigned claimed_=0,completed_=0;bool active_=false;ReloadAbortFailure failure_{};
    std::array<ReloadAbortCycleReport,HistoryCapacity> history_{};std::uint64_t historyTotal_=0;
};
// Exact helper contract from the whole 128-byte native function: no calls;
// true+ReloadLogic0+reload10..12 writes next1 and zeros both phase timers only.
// The adapter reads both arrays on the same actual original Update stack.
inline bool ReloadAbortHelperPostcondition(const std::array<std::byte,0xac>& before,
    const std::array<std::byte,0xac>& after)noexcept {
    auto expected=before;const std::uint32_t one=1,zero=0;
    std::memcpy(expected.data()+0x44,&one,4);std::memcpy(expected.data()+0x50,&zero,4);
    std::memcpy(expected.data()+0x54,&zero,4);return expected==after;
}
}
