#pragma once
#include "Bc2PhysicalPump.h"
#include "Bc2PumpReceiverGapProbe.h"
#include "fvr/interaction/ControllerInput.h"
namespace fvr::bc2 {
std::optional<math::Pose> PumpProbeController(const Bc2PumpRawContact&,const interaction::InputFrame& original,
    const math::Matrix4& desiredWristWorldMeters)noexcept;
// Explicit finite fixture: Prepare only changes ordinary controller fields.
// It cannot submit native requests, create contacts or acknowledge outcomes.
class Bc2PumpControllerProbe {
public:
    enum class Phase:unsigned {Warmup,Approach,Fire,WaitHeld,Grip,Rear,RearDwell,Forward,WaitReady,Settle,Done,Failed,
        InterruptLeft,RecoveryNeutral,RecoveryGrip,RecoveryRear,RecoveryRearDwell,RecoveryForward,RecoveryWait,SupportBeforeFire};
    enum class Scenario:unsigned {None,LeftTrackingLoss,HeldSupport,HeldSupportTrackingLoss,HeldSupportNaturalTrackingLoss,HeldSupportNatural,HeldSupportNaturalReceiverGap};
    explicit Bc2PumpControllerProbe(Bc2PumpCalibration c,unsigned cycles=2,Scenario scenario=Scenario::None)noexcept
        :calibration_(c),scenario_(scenario),requested_(cycles){}
    void Prepare(interaction::InputFrame&,const ReloadStateOwner&,std::string_view,const Bc2PumpRawContact&,
        const std::optional<Bc2NativeCycleView>&,std::int64_t observed,std::int64_t deadline,std::int64_t now)noexcept;
    void Observe(const Bc2PhysicalPumpResult&,const std::optional<Bc2NativeCycleView>&,Bc2PumpPackCounters,std::int64_t now)noexcept;
    void ObserveSupport(const Bc2PumpTracking&,const interaction::SupportGripResult&,std::int64_t now)noexcept;
    // Input-script resumption after an observed production rebase receipt.
    // Does not modify a gameplay consumer, native service, lease or outcome.
    bool FollowTrackingRebase(const interaction::InputFrame&,const ReloadStateOwner&,
        const Bc2NativeCycleView&,std::int64_t now)noexcept;
    void ObserveHeldSupport(const interaction::SupportGripResult&,const interaction::HandInteractionSample&,
        const std::optional<interaction::HandClaim>& left,const std::optional<interaction::HandClaim>& gun,std::int64_t now)noexcept;
    bool ReceiverGap()const noexcept{return scenario_==Scenario::HeldSupportNaturalReceiverGap;}
    bool AwaitingReceiver()const noexcept{return ReceiverGap()&&phase_==Phase::InterruptLeft;}
    bool ObserveMissingReceiver(std::int64_t now)noexcept;
    // Historical first fresh neutral in the new space, recorded before the
    // script's200ms neutral dwell. It never grants or renews native authority.
    bool ObserveReceiverReturnNeutral(const interaction::InputFrame&,const ReloadStateOwner&,
        std::int64_t observed,std::int64_t deadline,std::int64_t now)noexcept;
    void ObserveMissingReceiverNative(const std::optional<Bc2NativeCycleView>&,bool handsEmpty,std::int64_t now)noexcept;
    std::optional<PumpReceiverGapWitness> ReceiverGapReady(std::int64_t now)const noexcept;
    bool Failed()const noexcept{return phase_==Phase::Failed;}
    bool Completed()const noexcept{return phase_==Phase::Done;}
    Phase Current()const noexcept{return phase_;}
    void Report(std::ostream&)const;
private:
    bool NaturalStroke()const noexcept{return scenario_==Scenario::HeldSupportNaturalTrackingLoss||scenario_==Scenario::HeldSupportNatural||ReceiverGap();}
    bool Interrupts()const noexcept{return scenario_==Scenario::LeftTrackingLoss||scenario_==Scenario::HeldSupportTrackingLoss||scenario_==Scenario::HeldSupportNaturalTrackingLoss||(ReceiverGap()&&!completed_);}
    bool HeldSupportStart()const noexcept{return scenario_==Scenario::HeldSupport||scenario_==Scenario::HeldSupportTrackingLoss||NaturalStroke();}
    void Move(Phase,std::int64_t)noexcept;
    void Fail(unsigned,std::int64_t)noexcept;
    struct Source {interaction::InputFrame frame;ReloadStateOwner owner;std::int64_t observed=0,deadline=0;};
    std::array<std::optional<Source>,32> history_{};unsigned next_=0;
    std::optional<interaction::InputFrame> last_;
    Bc2PumpCalibration calibration_;ReloadStateOwner owner_{};
    Bc2PhysicalPumpResult physical_{};Bc2PumpPackCounters packs_{};
    std::optional<interaction::WeaponCycleLease> held_;
    std::optional<interaction::WeaponCycleIdleDebtLease> recovery_;
    std::optional<Bc2NativeCycleTrackingRebase> followedRebase_;
    std::uint64_t recoveryGun_=0;
    struct ReturnNeutral {std::uint64_t sequence=0,space=0;std::int64_t observed=0,deadline=0,accepted=0;};
    std::optional<ReturnNeutral> receiverReturnNeutral_;
    struct RecoveryAuthorityFailure {
        unsigned predicates=0;std::uint64_t grant=0,neutral=0,gun=0;
        std::int64_t neutralObserved=0,neutralDeadline=0,leaseObserved=0,leaseDeadline=0,now=0;
    } recoveryAuthorityFailure_{};
    Scenario scenario_=Scenario::None;
    std::int64_t lostStarted_=0,lostEnded_=0,neutralAt_=0,partialAt_=0;
    std::uint64_t lostFirst_=0,lostLast_=0,neutralSequence_=0,partialSource_=0,partialGun_=0;
    float partialTravel_=0;unsigned interruptedPairs_=0;
    bool interruptionObserved_=false,supportAdopted_=false;
    std::optional<PumpReceiverGapWitness> receiverGapWitness_;unsigned receiverMissingSamples_=0;
    unsigned receiverConvergingSamples_=0;std::int64_t receiverConvergingFirst_=0,receiverConvergingLast_=0;
    bool resumedPause_=false;unsigned pauseRegrips_=0;
    interaction::HandClaimToken initialSupport_{},initialGun_{};
    interaction::HandClaimToken receiverReturnedGun_{};
    std::uint64_t supportFireSource_=0;std::int64_t supportFireAt_=0;
    std::int64_t naturalSupportAt_=0;float supportCorrection_=0;
    // A pre-shot hand reset may be retried once using actual neutral/regrip
    // input. It never retires native debt or changes an in-progress stroke.
    bool preShotLossPending_=false;unsigned preShotRegrips_=0;
    std::uint64_t preShotCycle_=0,preShotShot_=0,preShotLossSequence_=0;
    std::int64_t preShotLossNs_=0;
    math::Pose command_{};bool armed_=false,rearPair_=false;
    Phase phase_=Phase::Warmup;unsigned failure_=0,completed_=0,baselinePairs_=0,rearPairs_=0,requested_=2;
    std::uint64_t lastRaw_=0;std::int64_t first_=0,phaseAt_=0,lastNow_=0,alignedAt_=0;
    int loaded_=0,reserve_=0,capacity_=0;
    std::uint64_t inputs_=0,rawPackets_=0,rawMatches_=0,poses_=0,viewMisses_=0,lastRig_=0,lastRawSeen_=0;
    unsigned lastNativePhase_=0;bool lastRawValid_=false,lastMapping_=false,lastOwnerMatch_=false,lastAssetMatch_=false;
    struct Cycle {ReloadHoldIdentity native{};interaction::WeaponCycleReady ready{};
        std::optional<interaction::WeaponCycleIdleDebtReady> recovered;int loaded=0,reserve=0,capacity=0;
        unsigned pairs=0;bool rearPair=false,supportReturned=false;
        std::uint64_t supportToken=0,supportClaim=0,supportSource=0;std::int64_t supportObserved=0,supportDeadline=0;};
    std::array<Cycle,8> cycles_{};
    struct Row {Phase phase;unsigned failure;std::int64_t now;};std::array<Row,128> rows_{};unsigned rowCount_=0;
};
}
