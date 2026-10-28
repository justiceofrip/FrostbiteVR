#pragma once
#include <array>
#include <cstdint>
#include <cmath>
#include <optional>

namespace fvr::interaction {
// Pure bookkeeping for a future verified native adapter. No native addresses,
// writes, attack admission, damage, audio or hand/holster ownership live here.
// Branch tokens are adapter-issued identities of one three-copy native cohort.
struct MeleeTimingOwner {
    std::uint64_t actor=0,actorGeneration=0,item=0,itemGeneration=0,cohortGeneration=0;
    std::array<std::uint64_t,3> branches{};
    bool operator==(const MeleeTimingOwner&)const=default;
};
struct MeleeTimingContract {
    float nativeWindupSeconds=0,retainedWindupSeconds=0,totalCycleSeconds=0;
    bool operator==(const MeleeTimingContract&)const=default;
};
struct MeleeTimingIntent {
    MeleeTimingOwner owner{};std::uint64_t serial=0;
    std::int64_t observedNs=0,deadlineNs=0;MeleeTimingContract contract{};
};
enum class MeleeTimingPhase:unsigned { WindupPrepared,RecoveryPrepared };
struct MeleeTimingBoundary {
    MeleeTimingOwner owner{};std::uint64_t serial=0,invocation=0,revision=0;
    unsigned branch=0;std::int64_t observedNs=0,deadlineNs=0;
    MeleeTimingPhase phase=MeleeTimingPhase::WindupPrepared;
    float timerSeconds=0,deltaSeconds=0;
    bool originalBoundaryVerified=false,ownerRetained=false;
};
enum class MeleeTimingStage:unsigned { Native,AdvanceIssued,Advanced,RecoveryIssued,Paid,Stock,Unknown };
enum class MeleeTimingWrite:unsigned { Applied,NotApplied,Unknown };
struct MeleeTimingPlan {
    MeleeTimingOwner owner{};std::uint64_t serial=0,invocation=0,revision=0;
    unsigned branch=0;std::int64_t deadlineNs=0;
    MeleeTimingPhase phase=MeleeTimingPhase::WindupPrepared;
    float expectedSeconds=0,replacementSeconds=0;
    bool operator==(const MeleeTimingPlan&)const=default;
};
// Exact recorded native-copy identity, not a new gameplay intent or timer match.
// Adapter must witness export+restore and compare the complete native payload.
struct MeleeTimingSnapshot {
    std::uint64_t receipt=0,bufferToken=0,exportInvocation=0,sourceRevision=0;
    unsigned sourceBranch=0;MeleeTimingStage stage=MeleeTimingStage::Native;
    std::array<std::uint32_t,16> payload{};
    bool operator==(const MeleeTimingSnapshot&)const=default;
};
class MeleeResponseTiming {
public:
    bool Begin(const MeleeTimingIntent& intent,std::int64_t now)noexcept {
        if(active_&&!Complete())return false;
        if(!OwnerValid(intent.owner)||!intent.serial||intent.serial<=lastSerial_||
           !Fresh(intent.observedNs,intent.deadlineNs,now)||!ContractValid(intent.contract))return false;
        intent_=intent;lastSerial_=intent.serial;active_=admission_=true;
        branches_={};for(auto& branch:branches_)branch.revision=1;
        snapshots_={};return true;
    }
    std::optional<MeleeTimingPlan> Prepare(const MeleeTimingBoundary& b,std::int64_t now)noexcept {
        if(!Boundary(b,now))return {};
        auto& branch=branches_[b.branch];const auto& c=intent_.contract;
        if(branch.pending||branch.stage==MeleeTimingStage::Unknown)return {};
        MeleeTimingPlan p{intent_.owner,intent_.serial,b.invocation,b.revision,b.branch,b.deadlineNs,b.phase,b.timerSeconds,0};
        if(b.phase==MeleeTimingPhase::WindupPrepared){
            if(branch.stage!=MeleeTimingStage::Native||b.timerSeconds!=c.nativeWindupSeconds)return {};
            // Do not collapse notifications into a hitch-sized Update. A later
            // callback cannot retrospectively accelerate this stock branch.
            if(!admission_||now>=intent_.deadlineNs||b.deltaSeconds>=c.retainedWindupSeconds){
                branch.stage=MeleeTimingStage::Stock;branch.lastInvocation=b.invocation;return {};
            }
            p.replacementSeconds=c.retainedWindupSeconds;branch.stage=MeleeTimingStage::AdvanceIssued;
        }else{
            if(branch.stage!=MeleeTimingStage::Advanced||b.timerSeconds<0||
               b.timerSeconds>c.totalCycleSeconds-c.nativeWindupSeconds+0.00001f)return {};
            p.replacementSeconds=b.timerSeconds+c.nativeWindupSeconds-c.retainedWindupSeconds;
            if(!std::isfinite(p.replacementSeconds)||p.replacementSeconds>=c.totalCycleSeconds)return {};
            branch.stage=MeleeTimingStage::RecoveryIssued;
        }
        branch.pending=p;branch.issueObservedNs=b.observedNs;branch.lastInvocation=b.invocation;return p;
    }
    // A timer write that may already have happened creates unresolved debt.
    // An expired proof never becomes a fictitious NotApplied result.
    bool Resolve(const MeleeTimingPlan& p,MeleeTimingWrite result,float observed,std::int64_t now)noexcept {
        if(!active_||p.branch>=branches_.size()||p.owner!=intent_.owner||p.serial!=intent_.serial)return false;
        auto& branch=branches_[p.branch];if(!branch.pending||*branch.pending!=p||branch.revision!=p.revision)return false;
        branch.pending.reset();
        const bool advance=p.phase==MeleeTimingPhase::WindupPrepared;
        if(result==MeleeTimingWrite::NotApplied&&observed==p.expectedSeconds){
            branch.stage=advance?MeleeTimingStage::Native:MeleeTimingStage::Advanced;return true;
        }
        if(result!=MeleeTimingWrite::Applied||!std::isfinite(observed)||observed!=p.replacementSeconds||
           now<branch.issueObservedNs||now>=p.deadlineNs){branch.stage=MeleeTimingStage::Unknown;return false;}
        branch.stage=advance?MeleeTimingStage::Advanced:MeleeTimingStage::Paid;return true;
    }
    // Tracking/menu/gesture completion closes new advances only. Existing native
    // recovery obligations survive it and are serviced with new exact evidence.
    void RevokeAdmission()noexcept{admission_=false;}
    bool Complete()const noexcept {
        if(!active_)return false;
        for(const auto& branch:branches_)if(branch.pending||
            (branch.stage!=MeleeTimingStage::Paid&&branch.stage!=MeleeTimingStage::Stock))return false;
        return true;
    }
    bool Retire(const MeleeTimingOwner& owner,bool nativeLifetimeRetired)noexcept {
        if(!active_||owner!=intent_.owner||!nativeLifetimeRetired)return false;
        active_=admission_=false;branches_={};snapshots_={};return true;
    }
    // Capture is observational and bounded. It grants no write on its own.
    std::optional<MeleeTimingSnapshot> Capture(const MeleeTimingBoundary& b,std::uint64_t bufferToken,
        const std::array<std::uint32_t,16>& payload,std::int64_t now)noexcept {
        if(!Boundary(b,now)||!bufferToken||nextSnapshot_==UINT64_MAX)return {};
        auto& branch=branches_[b.branch];if(branch.pending)return {};
        if(!((branch.stage==MeleeTimingStage::Advanced&&b.phase==MeleeTimingPhase::WindupPrepared)||
             (branch.stage==MeleeTimingStage::Paid&&b.phase==MeleeTimingPhase::RecoveryPrepared)))return {};
        MeleeTimingSnapshot snapshot{++nextSnapshot_,bufferToken,b.invocation,b.revision,b.branch,branch.stage,payload};
        snapshots_[(snapshot.receipt-1)%snapshots_.size()]=snapshot;branch.lastInvocation=b.invocation;return snapshot;
    }
    // Only an exact witnessed native restore can carry/reopen debt after
    // prediction rewind. Similar timer values, elapsed time and pointer identity
    // alone are insufficient; a changed payload rejects the handoff.
    bool Restored(const MeleeTimingBoundary& b,const MeleeTimingSnapshot& snapshot,std::uint64_t bufferToken,
        const std::array<std::uint32_t,16>& payload,std::int64_t now)noexcept {
        if(!Boundary(b,now)||!snapshot.receipt||snapshot.bufferToken!=bufferToken||snapshot.payload!=payload||
           snapshots_[(snapshot.receipt-1)%snapshots_.size()]!=snapshot)return false;
        auto& branch=branches_[b.branch];
        if(branch.pending||branch.revision==UINT64_MAX||
           !((snapshot.stage==MeleeTimingStage::Advanced&&b.phase==MeleeTimingPhase::WindupPrepared)||
             (snapshot.stage==MeleeTimingStage::Paid&&b.phase==MeleeTimingPhase::RecoveryPrepared)))return false;
        branch.stage=snapshot.stage;++branch.revision;branch.lastInvocation=b.invocation;return true;
    }
    MeleeTimingStage Stage(unsigned branch)const noexcept{return branch<branches_.size()?branches_[branch].stage:MeleeTimingStage::Unknown;}
    std::uint64_t Revision(unsigned branch)const noexcept{return branch<branches_.size()?branches_[branch].revision:0;}
private:
    struct Branch {
        MeleeTimingStage stage=MeleeTimingStage::Native;std::uint64_t revision=0,lastInvocation=0;
        std::int64_t issueObservedNs=0;std::optional<MeleeTimingPlan> pending;
    };
    static bool OwnerValid(const MeleeTimingOwner& o)noexcept {
        return o.actor&&o.actorGeneration&&o.item&&o.itemGeneration&&o.cohortGeneration&&
            o.branches[0]&&o.branches[1]&&o.branches[2]&&o.branches[0]!=o.branches[1]&&
            o.branches[0]!=o.branches[2]&&o.branches[1]!=o.branches[2];
    }
    static bool Fresh(std::int64_t begin,std::int64_t end,std::int64_t now)noexcept {
        return begin>0&&end>begin&&end-begin<=150000000&&now>=begin&&now<end;
    }
    static bool ContractValid(const MeleeTimingContract& c)noexcept {
        return std::isfinite(c.nativeWindupSeconds)&&std::isfinite(c.retainedWindupSeconds)&&
            std::isfinite(c.totalCycleSeconds)&&c.retainedWindupSeconds>0&&
            c.nativeWindupSeconds>c.retainedWindupSeconds&&c.totalCycleSeconds>c.nativeWindupSeconds&&c.totalCycleSeconds<=60;
    }
    bool Boundary(const MeleeTimingBoundary& b,std::int64_t now)const noexcept {
        return active_&&b.owner==intent_.owner&&b.serial==intent_.serial&&b.branch<branches_.size()&&
            b.revision==branches_[b.branch].revision&&b.invocation>branches_[b.branch].lastInvocation&&
            b.originalBoundaryVerified&&b.ownerRetained&&Fresh(b.observedNs,b.deadlineNs,now)&&
            (b.phase==MeleeTimingPhase::WindupPrepared||b.phase==MeleeTimingPhase::RecoveryPrepared)&&
            std::isfinite(b.timerSeconds)&&std::isfinite(b.deltaSeconds)&&b.deltaSeconds>0&&b.deltaSeconds<=1;
    }
    bool active_=false,admission_=false;std::uint64_t lastSerial_=0,nextSnapshot_=0;
    MeleeTimingIntent intent_{};std::array<Branch,3> branches_{};
    std::array<MeleeTimingSnapshot,8> snapshots_{};
};
}
