#pragma once
#include "Bc2ReloadHold.h"
#include "fvr/interaction/ManualReload.h"
#include <algorithm>
#include <cmath>
namespace fvr::bc2 {
struct ReloadRoundLease {
    ReloadHoldIdentity identity{};
    std::uint64_t cycle=0,sequence=0;
    std::int64_t observedNs=0,deadlineNs=0;
    int loaded=0,reserve=0,capacity=0;
    bool nativeBindingVerified=false,allThreeHeld=false;
};
struct ReloadRoundTransfer {
    ReloadHoldIdentity identity{};
    std::uint64_t cycle=0,invocation=0;
    std::int64_t beginNs=0,endNs=0;
    unsigned branch=3;
    int loadedBefore=0,reserveBefore=0,loadedAfter=0,reserveAfter=0;
    // Mint only from an owned, fully captured native Transfer invocation whose
    // inspected caller is OrdinaryState12 and whose identity survived exit.
    bool ordinaryState12Verified=false,identityRetained=false;
    bool operator==(const ReloadRoundTransfer&)const=default;
};
struct ReloadRoundSample {
    ReloadHoldIdentity identity{};
    std::uint64_t cycle=0,sequence=0;
    std::int64_t observedNs=0,deadlineNs=0;
    std::array<ReloadFiringObservation,3> branches{};
    bool nativeBindingVerified=false,allThreeHeld=false;
};
struct ReloadRoundAcknowledgement {
    interaction::ManualReloadAck semantic{};
    ReloadHoldIdentity identity{};
    std::uint64_t cycle=0,sampleSequence=0,serverInvocation=0;
};
enum class ReloadRoundPhase:unsigned {Idle,Awaiting,Complete,Failed};
enum class ReloadRoundFailure:unsigned {None,Expired,OwnerChanged,InvalidEvidence,UnexpectedAmmo,RepeatedTransfer,Reverted,NotSettled};
// Native completion ledger only. No native hook/call, time override, ammo write,
// resource spawn, gesture interpretation, or runtime capability enablement.
// The adapter serializes these short operations without holding its lock across
// originals. Client propagation can replace a predicted transfer; the local
// server MUST have one actual conserved native transfer before acknowledgement.
class ReloadRoundCompletion {
public:
    explicit ReloadRoundCompletion(bool enabled=false)noexcept:enabled_(enabled){}
    bool Begin(const interaction::ManualReloadRequest& request,const ReloadRoundLease& lease,std::int64_t nowNs,std::int64_t completionDeadlineNs)noexcept {
        if(!enabled_||phase_==ReloadRoundPhase::Awaiting||!request.id||request.id<=lastRequest_||
           request.operation!=interaction::ReloadOperation::InsertRound||!ValidIdentity(lease.identity)||
           request.owner!=Owner(lease.identity)||!lease.nativeBindingVerified||!lease.allThreeHeld||!lease.cycle||!lease.sequence||
           !Fresh(lease.observedNs,lease.deadlineNs,nowNs)||lease.loaded<0||lease.reserve<=0||lease.reserve>1000000||lease.capacity<=lease.loaded||lease.capacity>1000000||
           completionDeadlineNs<=nowNs||completionDeadlineNs-nowNs>1500000000)return false;
        request_=request;lease_=lease;lastRequest_=request.id;started_=nowNs;deadline_=completionDeadlineNs;
        phase_=ReloadRoundPhase::Awaiting;failure_=ReloadRoundFailure::None;transfers_={};advanced_=0;ackTaken_=false;lastSample_=lease.sequence;lastObserved_=lease.observedNs;completedNs_=ackDeadline_=0;return true;
    }
    bool Observe(const ReloadRoundTransfer& e)noexcept {
        if(phase_!=ReloadRoundPhase::Awaiting)return false;
        if(e.identity!=lease_.identity||e.cycle!=lease_.cycle)return Fail(ReloadRoundFailure::OwnerChanged);
        if(e.branch>=3||!e.invocation||!e.ordinaryState12Verified||!e.identityRetained||e.beginNs<started_||e.endNs<e.beginNs)return Fail(ReloadRoundFailure::InvalidEvidence);
        if(e.endNs>=deadline_)return Fail(ReloadRoundFailure::Expired);
        if(transfers_[e.branch]){
            if(*transfers_[e.branch]==e)return true; // Same diagnostic record, no new credit.
            return Fail(ReloadRoundFailure::RepeatedTransfer);
        }
        if(e.loadedBefore!=lease_.loaded||e.reserveBefore!=lease_.reserve||e.loadedAfter!=lease_.loaded+1||e.reserveAfter!=lease_.reserve-1)return Fail(ReloadRoundFailure::UnexpectedAmmo);
        transfers_[e.branch]=e;advanced_|=1u<<e.branch;return true;
    }
    bool Observe(const ReloadRoundSample& s,std::int64_t nowNs)noexcept {
        if(phase_!=ReloadRoundPhase::Awaiting)return false;
        if(nowNs>=deadline_)return Fail(ReloadRoundFailure::Expired);
        if(s.identity!=lease_.identity||s.cycle!=lease_.cycle)return Fail(ReloadRoundFailure::OwnerChanged);
        if(!s.nativeBindingVerified||!Fresh(s.observedNs,s.deadlineNs,nowNs)||s.observedNs<started_||s.observedNs<lastObserved_||s.sequence<lastSample_)return Fail(ReloadRoundFailure::InvalidEvidence);
        if(s.sequence==lastSample_)return false;lastSample_=s.sequence;lastObserved_=s.observedNs;
        bool allExpected=true,naturallyFinished=true;
        for(unsigned n=0;n<3;++n){const auto& b=s.branches[n];
            if(b.address!=lease_.identity.firing[n]||b.wrapperOffset!=(n==0?0x3cu:n==1?0x40u:0x10u))return Fail(ReloadRoundFailure::OwnerChanged);
            if(b.currentState>15||b.previousState>15||b.nextState>15||!std::isfinite(b.phaseTimer)||(b.flagsA8&(8|16)))return Fail(ReloadRoundFailure::InvalidEvidence);
            const bool expected=b.loaded==lease_.loaded+1&&b.reserve==lease_.reserve-1;
            if(expected)advanced_|=1u<<n;
            else if(b.loaded==lease_.loaded&&b.reserve==lease_.reserve){
                if(advanced_&(1u<<n))return Fail(ReloadRoundFailure::Reverted);allExpected=false;
            }else return Fail(ReloadRoundFailure::UnexpectedAmmo);
            naturallyFinished&=(b.loaded==lease_.capacity||b.reserve==0)&&(b.currentState==1||b.currentState==2)&&(b.nextState==1||b.nextState==2);
            if(s.allThreeHeld&&(b.currentState!=11||b.nextState!=12||b.phaseTimer<=0||b.phaseTimer>1))return Fail(ReloadRoundFailure::NotSettled);
        }
        if(!allExpected||!transfers_[2]||s.observedNs<transfers_[2]->endNs)return false;
        if(!s.allThreeHeld&&!naturallyFinished)return false;
        completedNs_=s.observedNs;ackDeadline_=std::min(s.deadlineNs,deadline_);phase_=ReloadRoundPhase::Complete;return true;
    }
    std::optional<ReloadRoundAcknowledgement> TakeAcknowledgement(const ReloadHoldIdentity& current,std::uint64_t cycle,std::int64_t nowNs)noexcept {
        if(phase_!=ReloadRoundPhase::Complete||ackTaken_)return {};
        if(current!=lease_.identity||cycle!=lease_.cycle){Fail(ReloadRoundFailure::OwnerChanged);return {};}
        if(nowNs<completedNs_||nowNs>=ackDeadline_){Fail(ReloadRoundFailure::Expired);return {};}
        ackTaken_=true;
        return ReloadRoundAcknowledgement{{request_.id,request_.owner,request_.operation,interaction::ReloadAcknowledgement::Applied},
            lease_.identity,lease_.cycle,lastSample_,transfers_[2]->invocation};
    }
    ReloadRoundPhase Phase()const noexcept{return phase_;}
    ReloadRoundFailure Failure()const noexcept{return failure_;}
    // Observed copies that reached the requested round. Adapter can re-hold
    // those copies under a SEPARATE current hold lease while others catch up.
    unsigned AdvancedMask()const noexcept{return advanced_;}
    void Cancel()noexcept{phase_=ReloadRoundPhase::Failed;failure_=ReloadRoundFailure::OwnerChanged;}
private:
    static interaction::ManualReloadOwner Owner(const ReloadHoldIdentity& i){return {i.owner.soldier,i.owner.actorGeneration,i.owner.weapon,i.owner.equipGeneration,i.owner.space};}
    static bool ValidIdentity(const ReloadHoldIdentity& i){const auto& o=i.owner;
        return o.player>=0x10000&&o.soldier>=0x10000&&o.weak>=0x10000&&o.weapon>=0x10000&&o.actorGeneration&&o.equipGeneration&&o.space&&
            i.serverPlayer>=0x10000&&i.serverSoldier>=0x10000&&i.serverItem>=0x10000&&i.firing[0]>=0x10000&&i.firing[1]>=0x10000&&i.firing[2]>=0x10000&&
            i.firing[0]!=i.firing[1]&&i.firing[0]!=i.firing[2]&&i.firing[1]!=i.firing[2];
    }
    static bool Fresh(std::int64_t observed,std::int64_t deadline,std::int64_t now){return observed>0&&now>=observed&&deadline>now&&deadline-observed<=200000000;}
    bool Fail(ReloadRoundFailure reason){phase_=ReloadRoundPhase::Failed;failure_=reason;return false;}
    bool enabled_=false,ackTaken_=false;
    ReloadRoundPhase phase_=ReloadRoundPhase::Idle;ReloadRoundFailure failure_=ReloadRoundFailure::None;
    interaction::ManualReloadRequest request_{};ReloadRoundLease lease_{};
    std::uint64_t lastRequest_=0,lastSample_=0;std::int64_t started_=0,deadline_=0,lastObserved_=0,completedNs_=0,ackDeadline_=0;unsigned advanced_=0;
    std::array<std::optional<ReloadRoundTransfer>,3> transfers_{};
};
}
