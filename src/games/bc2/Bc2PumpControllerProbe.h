#pragma once
#include "Bc2PhysicalPump.h"
#include "fvr/interaction/ControllerInput.h"
namespace fvr::bc2 {
std::optional<math::Pose> PumpProbeController(const Bc2PumpRawContact&,const interaction::InputFrame& original,
    const math::Matrix4& desiredWristWorldMeters)noexcept;
// Explicit finite fixture: Prepare only changes ordinary controller fields.
// It cannot submit native requests, create contacts or acknowledge outcomes.
class Bc2PumpControllerProbe {
public:
    enum class Phase:unsigned {Warmup,Approach,Fire,WaitHeld,Grip,Rear,RearDwell,Forward,WaitReady,Settle,Done,Failed};
    explicit Bc2PumpControllerProbe(Bc2PumpCalibration c,unsigned cycles=2)noexcept:calibration_(c),requested_(cycles){}
    void Prepare(interaction::InputFrame&,const ReloadStateOwner&,std::string_view,const Bc2PumpRawContact&,
        const std::optional<Bc2NativeCycleView>&,std::int64_t observed,std::int64_t deadline,std::int64_t now)noexcept;
    void Observe(const Bc2PhysicalPumpResult&,const std::optional<Bc2NativeCycleView>&,Bc2PumpPackCounters,std::int64_t now)noexcept;
    void ObserveSupport(const Bc2PumpTracking&,const interaction::SupportGripResult&,std::int64_t now)noexcept;
    bool Failed()const noexcept{return phase_==Phase::Failed;}
    bool Completed()const noexcept{return phase_==Phase::Done;}
    Phase Current()const noexcept{return phase_;}
    void Report(std::ostream&)const;
private:
    void Move(Phase,std::int64_t)noexcept;
    void Fail(unsigned,std::int64_t)noexcept;
    struct Source {interaction::InputFrame frame;ReloadStateOwner owner;std::int64_t observed=0,deadline=0;};
    std::array<std::optional<Source>,32> history_{};unsigned next_=0;
    std::optional<interaction::InputFrame> last_;
    Bc2PumpCalibration calibration_;ReloadStateOwner owner_{};
    Bc2PhysicalPumpResult physical_{};Bc2PumpPackCounters packs_{};
    std::optional<interaction::WeaponCycleLease> held_;
    math::Pose command_{};bool armed_=false,rearPair_=false;
    Phase phase_=Phase::Warmup;unsigned failure_=0,completed_=0,baselinePairs_=0,rearPairs_=0,requested_=2;
    std::uint64_t lastRaw_=0;std::int64_t first_=0,phaseAt_=0,lastNow_=0,alignedAt_=0;
    int loaded_=0,reserve_=0,capacity_=0;
    std::uint64_t inputs_=0,rawPackets_=0,rawMatches_=0,poses_=0,viewMisses_=0,lastRig_=0,lastRawSeen_=0;
    unsigned lastNativePhase_=0;bool lastRawValid_=false,lastMapping_=false,lastOwnerMatch_=false,lastAssetMatch_=false;
    struct Cycle {ReloadHoldIdentity native{};interaction::WeaponCycleReady ready{};int loaded=0,reserve=0,capacity=0;
        unsigned pairs=0;bool rearPair=false,supportReturned=false;
        std::uint64_t supportToken=0,supportClaim=0,supportSource=0;std::int64_t supportObserved=0,supportDeadline=0;};
    std::array<Cycle,8> cycles_{};
    struct Row {Phase phase;unsigned failure;std::int64_t now;};std::array<Row,128> rows_{};unsigned rowCount_=0;
};
}
