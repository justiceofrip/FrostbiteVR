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
};
class Bc2ResourceInventoryProbe {
public:
    enum class Phase:unsigned {Setup,InitialDraw,FirstMagazine,OtherDraw,Fire,WaitShot,Shell,ShellSettle,OriginalDraw,FullReturn,Dwell,Done,Failed,
        Pump,ShellSupport,RifleFire,WaitRifleShot};
    bool EnablePump(const Bc2PumpCalibration&)noexcept;
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
    void Move(Phase,std::int64_t)noexcept;void Fail(unsigned,std::int64_t)noexcept;
    bool Reserve(const ResourceInventoryObservation&,const ReloadStateOwner&,std::int64_t)const noexcept;
    bool Held(const ResourceInventoryObservation&,const ReloadStateOwner&,std::int64_t)const noexcept;
    bool Original(const ResourceInventoryObservation&,const ReloadStateOwner&,std::int64_t)const noexcept;
    void PrepareShellSupport(interaction::InputFrame&,const ReloadStateOwner&,const ReloadRawContact&,std::int64_t now)noexcept;
    Bc2InventoryReloadProbe inventory_{false,true};
    Bc2MagazineResourceProbe firstMagazine_{true,false,true,true},fullReturn_{true,true,false,true};
    Bc2PhysicalReloadProbe shell_{true,1};
    std::optional<Bc2PumpControllerProbe> pump_;
    Bc2PumpCalibration calibration_{};
    struct Source {interaction::InputFrame input;ReloadStateOwner owner;std::int64_t observed=0,deadline=0;};
    std::array<std::optional<Source>,32> history_{};unsigned historyAt_=0;
    math::Pose supportCommand_{};
    std::optional<math::Pose> pumpRightCommand_;bool pumpRightReturned_=false;
    std::uint64_t shellSupportToken_=0,shellSupportClaim_=0,shellSupportSource_=0,lastSupportRaw_=0;
    std::int64_t shellSupportObserved_=0,shellSupportDeadline_=0,shellCompletedAt_=0,supportAlignedAt_=0;
    std::uint64_t rifleShotBaseline_=0,rifleShotSequence_=0;
    std::int64_t rifleFireAt_=0,rifleShotObserved_=0;
    Phase phase_=Phase::Setup;unsigned failure_=0;
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
