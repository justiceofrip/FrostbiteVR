#pragma once
#include "Bc2InventoryReloadProbe.h"
#include "Bc2MagazineResourceProbe.h"
#include "Bc2PhysicalReloadProbe.h"
#include "Bc2PumpControllerProbe.h"
namespace fvr::bc2 {
// Finite input-only composition. This object has no consumer/native APIs.
struct ResourceInventoryObservation {
    std::shared_ptr<const BodyHolsterProbeSample> body;
    std::optional<BodyInventoryDisplay> display;
    MagazineRawContact magazineRaw{};MagazineResourceProbeState magazine{};
    ReloadRawContact shellRaw{};PhysicalReloadProbeState shell{};
    std::optional<Bc2AmmoReserveLease> reserve;
    bool shellBlocks=false;
    Bc2PumpRawContact pumpRaw{};
    std::optional<Bc2NativeCycleView> pumpNative;
    ReloadRawContact supportRaw{};math::Matrix4 supportTarget{};bool supportVerified=false;
};
class Bc2ResourceInventoryProbe {
public:
    Bc2ResourceInventoryProbe()noexcept {
        firstMagazine_.UseCombinedWarmup();fullReturn_.UseCombinedWarmup();
    }
    enum class Phase:unsigned {Setup,InitialDraw,FirstMagazine,OtherDraw,Fire,WaitShot,Shell,ShellSettle,OriginalDraw,FullReturn,Dwell,Done,Failed,
        Pump,ShellSupport,RifleFire,WaitRifleShot,RifleSupport,RifleSupportRelease,OriginObservation,EmptyFire,EmptyWait,EmptyDwell,
        RevisitDraw,RevisitPump,RevisitSettle,RevisitRifle};
    bool RequireRifleSupport()noexcept{if(first_||magazineOnly_||supportOnly_)return false;rifleSupportRequired_=true;return true;}
    // Input-only contact check: no magazine/cycle publication is required or granted.
    bool EnableSupportOnly()noexcept {
        if(first_||supportOnly_||magazineOnly_||pump_||replacementEnabled_||rifleSupportRequired_)return false;
        supportOnly_=rifleSupportRequired_=true;return true;
    }
    // Private subset: ordinary shoulder draw, original return, then chest refill.
    // No native/consumer mutation and no unsupported second mechanism required.
    bool EnableMagazineOnly(bool fireToEmpty=false,bool fireAfter=false,bool requireSupport=false)noexcept {
        if(first_||supportOnly_||magazineOnly_||pump_||replacementEnabled_||rifleSupportRequired_||(fireToEmpty&&(fireAfter||requireSupport)))return false;
        magazineOnly_=true;emptyMagazineFire_=fireToEmpty;magazinePostFire_=fireAfter;
        rifleSupportRequired_=requireSupport;
        if(fireToEmpty||fireAfter){firstMagazine_=Bc2MagazineResourceProbe{true,false,false,true};firstMagazine_.UseCombinedWarmup();}
        return true;
    }
    bool EnablePump(const Bc2PumpCalibration&,Bc2PumpControllerProbe::Scenario=Bc2PumpControllerProbe::Scenario::None,unsigned cycles=1)noexcept;
    // Private input fixture: hold the SPAS across the view for assembly inspection.
    // Ordinary player tracking, measured contacts and native authority are unchanged.
    bool UsePumpInspectionPose()noexcept;
    bool EnablePumpRevisit()noexcept;
    // Private diagnostic progression only; never retires a real consumer.
    bool EnableActorReplacement(bool magazine=false)noexcept;
    bool ActorReplacementWitnessed()const noexcept{return replacementWitness_.has_value()||replacementMagazineWitness_.has_value();}
    bool ActorReplacementResumed()const noexcept{return replacementResumed_;}
    bool PumpEnabled()const noexcept{return pump_.has_value();}
    void Prepare(interaction::InputFrame&,const ReloadStateOwner&,std::string_view,
        const ResourceInventoryObservation&,std::int64_t observed,std::int64_t deadline,std::int64_t now)noexcept;
    void Observe(const ResourceInventoryObservation&,const MagazinePackCounters&,std::int64_t now)noexcept;
    void ObservePump(const Bc2PhysicalPumpResult&,const std::optional<Bc2NativeCycleView>&,Bc2PumpPackCounters,std::int64_t now)noexcept;
    void ObserveSupport(const Bc2PumpTracking&,const interaction::SupportGripResult&,
        const interaction::HandInteractionSample& current,const interaction::HandInteractionSample& original,
        const std::optional<interaction::HandClaim>& left,const std::optional<interaction::HandClaim>& gun,bool shellReturned,std::int64_t now)noexcept;
    bool CancelConsumer()const noexcept{return phase_==Phase::Failed;}
    bool CancelShell()const noexcept{return CancelConsumer()||phase_==Phase::ShellSettle;}
    bool Completed()const noexcept{return phase_==Phase::Done;}
    bool Recording()const noexcept{return began_!=0;}
    Phase State()const noexcept{return phase_;}
    void Report(std::ostream&)const;
private:
    void PrepareSupportOnly(interaction::InputFrame&,const ReloadStateOwner&,std::string_view,const ResourceInventoryObservation&,std::int64_t,std::int64_t,std::int64_t)noexcept;
    void PrepareEmptyMagazine(interaction::InputFrame&,const ReloadStateOwner&,const ResourceInventoryObservation&,std::int64_t)noexcept;
    void Move(Phase,std::int64_t)noexcept;void Fail(unsigned,std::int64_t)noexcept;
    bool Reserve(const ResourceInventoryObservation&,const ReloadStateOwner&,std::int64_t)const noexcept;
    bool Held(const ResourceInventoryObservation&,const ReloadStateOwner&,std::int64_t)const noexcept;
    bool Original(const ResourceInventoryObservation&,const ReloadStateOwner&,std::int64_t)const noexcept;
    void PrepareShellSupport(interaction::InputFrame&,const ReloadStateOwner&,const ReloadRawContact&,std::int64_t now)noexcept;
    void PrepareRifleSupport(interaction::InputFrame&,const ReloadStateOwner&,const ResourceInventoryObservation&,std::int64_t now)noexcept;
    void RecordInput(const interaction::InputFrame&,const ReloadStateOwner&,std::int64_t observed,std::int64_t deadline,std::int64_t now)noexcept;
    bool WitnessReplacement(const std::optional<Bc2NativeCycleView>&,std::int64_t now)noexcept;
    bool WitnessMagazineReplacement(const ResourceInventoryObservation&,std::int64_t now)noexcept;
    const ReloadStateOwner* ReplacementSourceOwner()const noexcept;
    bool ResumeReplacement(interaction::InputFrame&,const ReloadStateOwner&,std::int64_t observed,std::int64_t deadline,std::int64_t now)noexcept;
    bool ObserveOriginInput(interaction::InputFrame&,const ReloadStateOwner&,const ResourceInventoryObservation&,
        std::int64_t observed,std::int64_t deadline,std::int64_t now)noexcept;
    std::optional<Bc2NativeCycleView> originBefore_;
    ReloadStateOwner originAfter_{};
    bool originVerified_=false,originResumed_=false,originStroke_=false;std::int64_t originVerifiedNs_=0;
    Bc2InventoryReloadProbe inventory_{false,true};
    Bc2MagazineResourceProbe firstMagazine_{true,false,true,true},fullReturn_{true,true,false,true};
    Bc2PhysicalReloadProbe shell_{true,1};
    std::optional<Bc2PumpControllerProbe> pump_;
    std::optional<Bc2PumpControllerProbe> revisitPump_;
    bool revisitEnabled_=false,revisitCompleted_=false;
    ReloadStateOwner revisitOwner_{};
    int revisitLoaded_=-1,revisitReserve_=-1;
    std::uint64_t revisitShotSequence_=0;
    Bc2PumpCalibration calibration_{};
    unsigned pumpCycles_=1;
    Bc2PumpControllerProbe::Scenario pumpScenario_=Bc2PumpControllerProbe::Scenario::None;
    bool replacementEnabled_=false,replacementResumed_=false,replacementMagazine_=false;
    bool pumpInspectionPose_=false,supportOnly_=false;
    std::optional<interaction::BodySlotAssignment> replacementShoulderTarget_;
    bool replacementShoulderBound_=false;
    std::optional<Bc2NativeCycleView> replacementWitness_;
    std::optional<AmmoResourceView> replacementMagazineWitness_;
    ReloadStateOwner replacementOwner_{};
    std::int64_t replacementWitnessAt_=0,replacementInputObserved_=0,replacementInputDeadline_=0,replacementAt_=0;
    std::uint64_t replacementInput_=0,replacementPendingEquip_=0;
    std::string replacementPriorReport_;
    struct Source {interaction::InputFrame input;ReloadStateOwner owner;std::int64_t observed=0,deadline=0;};
    std::array<std::optional<Source>,32> history_{};unsigned historyAt_=0;
    math::Pose supportCommand_{};
    std::optional<math::Pose> pumpRightCommand_;bool pumpRightReturned_=false;
    std::uint64_t shellSupportToken_=0,shellSupportClaim_=0,shellSupportSource_=0,lastSupportRaw_=0;
    std::int64_t shellSupportObserved_=0,shellSupportDeadline_=0,shellCompletedAt_=0,supportAlignedAt_=0;
    AmmoResourceBinding rifleShotBinding_{};ReloadHoldIdentity rifleShotIdentity_{};
    std::uint64_t rifleShotBaseline_=0,rifleShotSequence_=0,rifleShotRequest_=0;
    unsigned rifleShotCount_=0;
    std::int64_t rifleFireAt_=0,rifleShotObserved_=0;
    bool emptyMagazineFire_=false,magazinePostFire_=false;
    AmmoResourceBinding emptyBinding_{};ReloadHoldIdentity emptyIdentity_{};
    interaction::AmmunitionCounts emptyBefore_{};
    unsigned emptyPulses_=0,emptyShots_=0;
    std::uint64_t emptyRequest_=0,emptySequence_=0;
    std::int64_t emptyObserved_=0,emptyPulseAt_=0,emptyAt_=0,emptyVerifiedAt_=0;
    struct EmptyShot {unsigned pulse;std::uint64_t sequence;std::int64_t observed,input;int loaded;};
    std::array<EmptyShot,64> emptyShotRows_{};
    Phase phase_=Phase::Setup;unsigned failure_=0;
    bool rifleSupportRequired_=false,rifleSupportReleased_=false,magazineOnly_=false;
    std::uint64_t rifleSupportClaim_=0,rifleSupportSequence_=0;
    std::int64_t rifleSupportBegan_=0,rifleSupportLast_=0,rifleSupportMaxGap_=0;
    unsigned rifleSupportSamples_=0;
    std::int64_t first_=0,began_=0,phaseAt_=0,lastNow_=0;
    ReloadStateOwner initial_{},shellOwner_{};interaction::AmmoResourceIdentity original_{};
    std::int64_t observed_=0,deadline_=0;
    interaction::AmmunitionCounts afterFirst_{};
    int shellLoaded_=-1,shellReserve_=-1;std::uint64_t shotSequence_=0,shotObserved_=0;
    std::optional<interaction::InputFrame> lastInput_;
    struct Row {unsigned phase=0,failure=0;std::int64_t now=0;};
    std::array<Row,32> transitions_{};unsigned transitionCount_=0;
    struct InputRow {std::uint64_t generation=0;std::int64_t now=0;unsigned phase=0,weapon=0;float trigger=0,left=0,right=0;};
    std::array<InputRow,512> inputs_{};unsigned inputCount_=0,inputDropped_=0;InputRow previous_{};
};
}
