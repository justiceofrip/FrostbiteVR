#pragma once
#include "Bc2BodyHolsterObservation.h"
#include "Bc2BodyHolsterChallenge.h"
#include "Bc2BodyHolsterProbeSession.h"
#include "Bc2DiagnosticFireAmmo.h"
#include <ostream>
#include <string_view>
namespace fvr::bc2 {
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

