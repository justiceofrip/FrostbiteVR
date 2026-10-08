#pragma once
#include "Bc2ReloadHold.h"
#include "fvr/interaction/ManualReload.h"
#include <algorithm>
#include <cmath>
namespace fvr::bc2 {
struct ReloadMagazineLease {
    ReloadHoldIdentity identity{};
    std::uint64_t cycle=0,sequence=0;
    std::int64_t observedNs=0,deadlineNs=0;
    int loaded=0,reserve=0,capacity=0;
    bool nativeBindingVerified=false,allThreeHeld=false;
};
struct ReloadMagazineTransfer {
    ReloadHoldIdentity identity{};
    std::uint64_t cycle=0,invocation=0;
    std::int64_t beginNs=0,endNs=0;
    unsigned branch=3;
    int loadedBefore=0,reserveBefore=0,loadedAfter=0,reserveAfter=0;
    // Mint only from an owned, fully captured native Transfer invocation whose
    // inspected caller is OrdinaryState12 and whose identity survived exit.
    bool ordinaryState12Verified=false,identityRetained=false;
    bool operator==(const ReloadMagazineTransfer&)const=default;
};
struct ReloadMagazineSample {
    ReloadHoldIdentity identity{};
    std::uint64_t cycle=0,sequence=0;
    std::int64_t observedNs=0,deadlineNs=0;
    std::array<ReloadFiringObservation,3> branches{};
    bool nativeBindingVerified=false,allThreeHeld=false;
};
struct ReloadMagazineAcknowledgement {
    interaction::ManualReloadAck semantic{};
    ReloadHoldIdentity identity{};
    std::uint64_t cycle=0,sampleSequence=0,serverInvocation=0;
    int loadedBefore=0,reserveBefore=0,loadedAfter=0,reserveAfter=0;
};
// Observation of an actual native Restore hook, never inferred from counts.
struct ReloadMagazinePredictionRestore {
 ReloadHoldIdentity identity{};std::uint64_t cycle=0,invocation=0;
 std::int64_t beginNs=0,endNs=0;unsigned branch=3,caller=0;
 int loadedBefore=0,reserveBefore=0,loadedAfter=0,reserveAfter=0;
 bool immutableSnapshotMatched=false,identityRetained=false;
 ReloadObservedConfig config{};
};
enum class ReloadMagazinePhase:unsigned {Idle,Awaiting,Complete,Failed};
enum class ReloadMagazineFailure:unsigned {None,Expired,OwnerChanged,InvalidEvidence,UnexpectedAmmo,RepeatedTransfer,Reverted,NotSettled};
// XM8 pooled-magazine completion ledger. Gameplay dispatch is off by default.
// A removed visual magazine never changes these native pooled counts.
// Native completion ledger only. No native hook/call, time override, ammo write,
// resource spawn, gesture interpretation, or runtime capability enablement.
// The adapter serializes these short operations without holding its lock across
// originals. Client propagation can replace a predicted transfer; the local
// server MUST have one actual conserved native transfer before acknowledgement.
class ReloadMagazineCompletion {
public:
    explicit ReloadMagazineCompletion(bool enabled=false)noexcept:enabled_(enabled){}
    bool Begin(const interaction::ManualReloadRequest& request,const ReloadMagazineLease& lease,std::int64_t nowNs,std::int64_t completionDeadlineNs,std::int64_t maximumDurationNs=3500000000ll)noexcept {
        if(!enabled_||phase_==ReloadMagazinePhase::Awaiting||!request.id||request.id<=lastRequest_||
           request.operation!=interaction::ReloadOperation::SeatMagazine||!ValidIdentity(lease.identity)||
           request.owner!=Owner(lease.identity)||!lease.nativeBindingVerified||!lease.allThreeHeld||!lease.cycle||!lease.sequence||
           !Fresh(lease.observedNs,lease.deadlineNs,nowNs)||lease.loaded<0||lease.reserve<=0||lease.reserve>1000000||lease.capacity<=lease.loaded||lease.capacity>1000000||
           maximumDurationNs<=0||maximumDurationNs>10000000000ll||completionDeadlineNs<=nowNs||completionDeadlineNs-nowNs>maximumDurationNs)return false;
        units_=std::min(lease.capacity-lease.loaded,lease.reserve);
        request_=request;lease_=lease;lastRequest_=request.id;started_=nowNs;deadline_=completionDeadlineNs;
        phase_=ReloadMagazinePhase::Awaiting;failure_=ReloadMagazineFailure::None;transfers_={};advanced_=0;ackTaken_=false;lastSample_=lease.sequence;lastObserved_=lease.observedNs;completedNs_=ackDeadline_=0;return true;
    }
    bool ObservePredictionRestore(const ReloadMagazinePredictionRestore& e)noexcept {
        if(phase_!=ReloadMagazinePhase::Awaiting||e.branch>=2||transfers_[2]||
           e.identity!=lease_.identity||e.cycle!=lease_.cycle||!e.invocation||!e.caller||
           !e.immutableSnapshotMatched||!e.identityRetained||e.beginNs<started_||e.endNs<e.beginNs||e.endNs>=deadline_||
           !transfers_[e.branch]||e.invocation<=transfers_[e.branch]->invocation||e.beginNs<transfers_[e.branch]->endNs||
           e.loadedBefore!=lease_.loaded+units_||e.reserveBefore!=lease_.reserve-units_||
           e.loadedAfter!=lease_.loaded||e.reserveAfter!=lease_.reserve)return false;
        // Retire only this predicted receipt. No server credit/lease renewal.
        transfers_[e.branch].reset();advanced_&=~(1u<<e.branch);return true;
    }
    bool Observe(const ReloadMagazineTransfer& e)noexcept {
        if(phase_!=ReloadMagazinePhase::Awaiting)return false;
        if(e.identity!=lease_.identity||e.cycle!=lease_.cycle)return Fail(ReloadMagazineFailure::OwnerChanged);
        if(e.branch>=3||!e.invocation||!e.ordinaryState12Verified||!e.identityRetained||e.beginNs<started_||e.endNs<e.beginNs)return Fail(ReloadMagazineFailure::InvalidEvidence);
        if(e.endNs>=deadline_)return Fail(ReloadMagazineFailure::Expired);
        if(transfers_[e.branch]){
            if(*transfers_[e.branch]==e)return true; // Same diagnostic record, no new credit.
            return Fail(ReloadMagazineFailure::RepeatedTransfer);
        }
        if(e.loadedBefore!=lease_.loaded||e.reserveBefore!=lease_.reserve||e.loadedAfter!=lease_.loaded+units_||e.reserveAfter!=lease_.reserve-units_)return Fail(ReloadMagazineFailure::UnexpectedAmmo);
        transfers_[e.branch]=e;advanced_|=1u<<e.branch;return true;
    }
    bool Observe(const ReloadMagazineSample& s,std::int64_t nowNs)noexcept {
        if(phase_!=ReloadMagazinePhase::Awaiting)return false;
        if(nowNs>=deadline_)return Fail(ReloadMagazineFailure::Expired);
        if(s.identity!=lease_.identity||s.cycle!=lease_.cycle)return Fail(ReloadMagazineFailure::OwnerChanged);
        if(!s.nativeBindingVerified||!Fresh(s.observedNs,s.deadlineNs,nowNs)||s.observedNs<started_||s.observedNs<lastObserved_||s.sequence<lastSample_)return Fail(ReloadMagazineFailure::InvalidEvidence);
        if(s.sequence==lastSample_)return false;lastSample_=s.sequence;lastObserved_=s.observedNs;
        bool allExpected=true,naturallyFinished=true;
        for(unsigned n=0;n<3;++n){const auto& b=s.branches[n];
            if(b.address!=lease_.identity.firing[n]||b.wrapperOffset!=(n==0?0x3cu:n==1?0x40u:0x10u))return Fail(ReloadMagazineFailure::OwnerChanged);
            if(b.currentState>15||b.previousState>15||b.nextState>15||!std::isfinite(b.phaseTimer)||(b.flagsA8&(8|16)))return Fail(ReloadMagazineFailure::InvalidEvidence);
            const bool expected=b.loaded==lease_.loaded+units_&&b.reserve==lease_.reserve-units_;
            if(expected)advanced_|=1u<<n;
            else if(b.loaded==lease_.loaded&&b.reserve==lease_.reserve){
                if(advanced_&(1u<<n))return Fail(ReloadMagazineFailure::Reverted);allExpected=false;
            }else return Fail(ReloadMagazineFailure::UnexpectedAmmo);
            naturallyFinished&=(b.loaded==lease_.capacity||b.reserve==0)&&(b.currentState==1||b.currentState==2)&&(b.nextState==1||b.nextState==2);
            if(s.allThreeHeld)return Fail(ReloadMagazineFailure::NotSettled); // A refill receipt requires normal reload exit, never another held shell interval.
        }
        if(!allExpected||!transfers_[2]||s.observedNs<transfers_[2]->endNs)return false;
        if(!naturallyFinished)return false;
        completedNs_=s.observedNs;ackDeadline_=std::min(s.deadlineNs,deadline_);phase_=ReloadMagazinePhase::Complete;return true;
    }
    std::optional<ReloadMagazineAcknowledgement> TakeAcknowledgement(const ReloadHoldIdentity& current,std::uint64_t cycle,std::int64_t nowNs)noexcept {
        if(phase_!=ReloadMagazinePhase::Complete||ackTaken_)return {};
        if(current!=lease_.identity||cycle!=lease_.cycle){Fail(ReloadMagazineFailure::OwnerChanged);return {};}
        if(nowNs<completedNs_||nowNs>=ackDeadline_){Fail(ReloadMagazineFailure::Expired);return {};}
        ackTaken_=true;
        return ReloadMagazineAcknowledgement{{request_.id,request_.owner,request_.operation,interaction::ReloadAcknowledgement::Applied},
            lease_.identity,lease_.cycle,lastSample_,transfers_[2]->invocation,lease_.loaded,lease_.reserve,lease_.loaded+units_,lease_.reserve-units_};
    }
    ReloadMagazinePhase Phase()const noexcept{return phase_;}
    ReloadMagazineFailure Failure()const noexcept{return failure_;}
    // Observed copies that reached the requested refill. Magazine reloads
    // finish their native tail; they are never re-held as another shell.
    unsigned AdvancedMask()const noexcept{return advanced_;}
    void Cancel()noexcept{phase_=ReloadMagazinePhase::Failed;failure_=ReloadMagazineFailure::OwnerChanged;}
private:
    static interaction::ManualReloadOwner Owner(const ReloadHoldIdentity& i){return {i.owner.soldier,i.owner.actorGeneration,i.owner.weapon,i.owner.equipGeneration,i.owner.space};}
    static bool ValidIdentity(const ReloadHoldIdentity& i){const auto& o=i.owner;
        return o.player>=0x10000&&o.soldier>=0x10000&&o.weak>=0x10000&&o.weapon>=0x10000&&o.actorGeneration&&o.equipGeneration&&o.space&&
            i.serverPlayer>=0x10000&&i.serverSoldier>=0x10000&&i.serverItem>=0x10000&&i.firing[0]>=0x10000&&i.firing[1]>=0x10000&&i.firing[2]>=0x10000&&
            i.firing[0]!=i.firing[1]&&i.firing[0]!=i.firing[2]&&i.firing[1]!=i.firing[2];
    }
    static bool Fresh(std::int64_t observed,std::int64_t deadline,std::int64_t now){return observed>0&&now>=observed&&deadline>now&&deadline-observed<=200000000;}
    bool Fail(ReloadMagazineFailure reason){phase_=ReloadMagazinePhase::Failed;failure_=reason;return false;}
    int units_=0;
    bool enabled_=false,ackTaken_=false;
    ReloadMagazinePhase phase_=ReloadMagazinePhase::Idle;ReloadMagazineFailure failure_=ReloadMagazineFailure::None;
    interaction::ManualReloadRequest request_{};ReloadMagazineLease lease_{};
    std::uint64_t lastRequest_=0,lastSample_=0;std::int64_t started_=0,deadline_=0,lastObserved_=0,completedNs_=0,ackDeadline_=0;unsigned advanced_=0;
    std::array<std::optional<ReloadMagazineTransfer>,3> transfers_{};
};
}
