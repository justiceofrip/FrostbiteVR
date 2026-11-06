#pragma once
#include "Bc2BodyHolsterObservation.h"
#include "Bc2BodyHolsterChallenge.h"
#include "Bc2BodyHolsterProbeSession.h"
#include "Bc2DiagnosticFireAmmo.h"
#include <atomic>
#include <cstdint>
#include <utility>
#include <ostream>
#include <string_view>
namespace fvr::bc2 {
// Diagnostic read only: retain even a refused immutable publication so its
// original clocks and exact first failed predicate describe the same load.
struct BodyHolsterProbeObservation {
    std::shared_ptr<const BodyHolsterProbeSample> sample;
    std::int64_t observedNs=0;
    bool enabled=false;
    std::string_view rejectionReason="sample_missing";
    bool Available()const noexcept{return rejectionReason.empty();}
};
inline std::string_view BodyHolsterProbeReadRejection(const BodyHolsterProbeSample* sample,bool enabled,std::int64_t now)noexcept {
    if(!enabled)return "gameplay_disabled";
    if(!sample)return "sample_missing";
    if(sample->sampledNs<=0)return "sample_time_nonpositive";
    if(sample->sampledNs>now)return "sample_time_future";
    if(now-sample->sampledNs>150000000)return "sample_age_expired";
    if(sample->hand.observedNs<=0)return "input_time_nonpositive";
    if(sample->hand.observedNs>now)return "input_time_future";
    if(sample->hand.deadlineNs<=now)return "input_lease_expired";
    return {};
}
template<class Clock>
BodyHolsterProbeObservation ObserveBodyHolsterProbe(const std::atomic<std::shared_ptr<const BodyHolsterProbeSample>>& publication,
    const std::atomic<bool>& enabled,Clock clock)noexcept {
    BodyHolsterProbeObservation result;
    result.sample=publication.load(std::memory_order_acquire);
    result.observedNs=clock(); // Must follow the single load; never restamp its source.
    result.enabled=enabled.load();
    result.rejectionReason=BodyHolsterProbeReadRejection(result.sample.get(),result.enabled,result.observedNs);
    return result;
}
// Explicit snapshot worker only. A cold diagnostic window may still hold an
// expired copy from its previous window. Wait for a subsequent original copy,
// never make the retained copy current by reopening the observation window.
// This grants no action/claim authority and never changes a source timestamp.
template<class Read,class Clock,class Pause>
BodyHolsterProbeObservation PrepareBodyHolsterObservation(BodyHolsterProbeObservation current,
    bool cold,std::int64_t requestedNs,Read read,Clock clock,Pause pause)noexcept {
    const auto pending=[](const BodyHolsterProbeObservation& value)noexcept {
        return value.enabled&&(value.rejectionReason=="sample_missing"||
            value.rejectionReason=="sample_age_expired"||value.rejectionReason=="input_lease_expired");
    };
    // Warm, already-fresh, disabled and other refused sources never sleep.
    if(!cold||!pending(current))return current;
    constexpr std::int64_t budgetNs=50000000;
    if(requestedNs<=0||requestedNs>INT64_MAX-budgetNs)return current;
    const auto deadline=requestedNs+budgetNs;
    for(unsigned attempt=0;attempt<50;++attempt){
        const auto before=clock();
        if(before<requestedNs||before>=deadline)return current;
        pause();
        auto next=read(); // One immutable load and its own observer clock.
        const auto after=clock();
        if(after<before||after>=deadline||next.observedNs<requestedNs||next.observedNs>after){
            // Scheduling can overrun a sleep. Such a late copy is not admitted.
            next.rejectionReason="observation_prepare_deadline";return next;
        }
        if(next.Available()){
            if(!next.sample||next.sample->sampledNs<=requestedNs)
                next.rejectionReason="sample_predates_request";
            return next;
        }
        current=std::move(next);
        if(!pending(current))return current;
    }
    return current; // Bounded exhaustion remains explicitly unavailable.
}

enum class BodyHolsterFixturePhase:unsigned {Warmup,Baseline,ReachHolster,PressHolster,WaitEmpty,
    FreeA,FreeB,ReachDraw,PressDraw,WaitDraw,Restored,Done,Failed,FirePulse,FireReleased};
struct BodyHolsterFixtureSample {
    BodyHolsterFixturePhase phase=BodyHolsterFixturePhase::Warmup;
    unsigned failure=0;std::int64_t sampledNs=0;
    std::shared_ptr<const BodyHolsterProbeSample> actual;
};
// Commands a PRIVATE synthetic input copy. Never publishes claims, contact
// proofs, inventory acknowledgements or render receipts. Production body policy
// must discover the assigned shoulder from the real native inventory itself.
class Bc2BodyHolsterProbe {
public:
    explicit Bc2BodyHolsterProbe(bool enabled=false,BodyHolsterDiagnosticProfile profile=BodyHolsterDiagnosticProfile::Spas)noexcept:enabled_(enabled),profile_(profile){}
    void Prepare(interaction::InputFrame&,const ReloadStateOwner&,std::string_view asset,
        std::shared_ptr<const BodyHolsterProbeSample>,std::int64_t observedNs,std::int64_t deadlineNs,std::int64_t nowNs,
        const std::optional<Bc2AmmoReserveLease>& ammo={})noexcept;
    void Observe(std::shared_ptr<const BodyHolsterProbeSample>,std::int64_t nowNs)noexcept;
    bool ChallengeWanted(const BodyHolsterProbeSample&,std::int64_t nowNs)const noexcept;
    void RecordChallenge(const BodyHolsterChallengeRow&,std::int64_t nowNs)noexcept;
    bool FireRequested()const noexcept{return profile_==BodyHolsterDiagnosticProfile::ScopedXm8Fire||profile_==BodyHolsterDiagnosticProfile::ExactConfiguredTableFire;}
    bool NeedsNeutralFire()const noexcept{return fireUnavailable_||pulseEnd_>0||Failed();}
    bool Failed()const noexcept{return phase_==BodyHolsterFixturePhase::Failed;}
    BodyHolsterFixturePhase Phase()const noexcept{return phase_;}
    BodyHolsterFixtureSample Sample()const noexcept{return {phase_,failure_,lastNow_,actual_};}
    void Cancel(unsigned reason,std::int64_t nowNs)noexcept;
    void Report(std::ostream&)const;
private:
    bool Fresh(const BodyHolsterProbeSample&,std::int64_t)const noexcept;
    bool Empty(const BodyHolsterProbeSample&,std::int64_t)const noexcept;
    bool Held(const BodyHolsterProbeSample&,std::int64_t)const noexcept;
    bool FireHeld(const BodyHolsterProbeSample&,std::int64_t)const noexcept;
    bool FirePresentationUnavailable(const BodyHolsterProbeSample&,std::int64_t)const noexcept;
    void DeferFire(std::int64_t)noexcept;
    void ResumeFireEvidence(std::int64_t)noexcept;
    void PhaseTo(BodyHolsterFixturePhase,std::int64_t)noexcept;
    void Row(std::int64_t,bool force=false)noexcept;
    BodyHolsterDiagnosticProfile profile_=BodyHolsterDiagnosticProfile::Disabled;
    DiagnosticFireAmmo diagnosticFireAmmo_;
    bool enabled_=false;BodyHolsterFixturePhase phase_=BodyHolsterFixturePhase::Warmup;unsigned failure_=0;
    std::int64_t first_=0,phaseAt_=0,lastNow_=0,lastRow_=0,originalObserved_=0,originalDeadline_=0;
    std::uint64_t lastInput_=0,baselineClaim_=0,hiddenRequest_=0,showRequest_=0,emptyPairs_=0;
    ReloadStateOwner owner_{};interaction::HandInteractionOwner physical_{};
    interaction::HandInteractionKey physicalGun_{};
    std::optional<interaction::BodySlotAssignment> slot_;
    interaction::BodyAnchor anchor_{};math::Pose command_{};float squeeze_=0;
    std::shared_ptr<const BodyHolsterProbeSample> actual_;
    std::optional<BodyHolsterChallengeRow> challenge_;
    std::uint64_t restoredClaim_=0,fireCommits_=0,releaseCommits_=0,fireFirstMs_=0,fireLastMs_=0;
    std::int64_t pulseStart_=0,pulseEnd_=0,fireUnavailableFirst_=0,fireUnavailableAt_=0;float trigger_=0;
    bool fireUnavailable_=false;unsigned fireUnavailableCount_=0;
    struct Evidence {unsigned phase=0,reason=0,bodyPhase=0;std::int64_t now=0,observed=0,deadline=0;
        std::uint64_t input=0,tick=0,request=0,right=0,left=0,draw=0,receiptInput=0,pairedFree=0,inventoryCommit=0;
        unsigned slot=0;math::Vec3 grip{},consumedGrip{};float squeeze=0,consumedSqueeze=0,trigger=0,consumedTrigger=0,fireCache=0;bool fireRead=false,fireRequested=false;bool hidden=false,suppressed=false,free=false,blocksActions=false,allowsGunHold=false;};
    std::array<Evidence,192> rows_{};unsigned rowCount_=0;
};
}

