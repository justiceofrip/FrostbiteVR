#pragma once
#include "Bc2PhysicalReload.h"
namespace fvr::bc2 {
// Diagnostic-only inverse of the existing TrackedRig mapping. The coherent raw
// wrist/body sample and its original controller input are both required.
std::optional<math::Pose> PhysicalReloadProbeController(const ReloadRawContact&,const interaction::InputFrame& original,
    const math::Matrix4& desiredRawWristWorldMeters)noexcept;
class Bc2PhysicalReloadProbe {
public:
    // Two rounds is a separate bounded fixture; the verified one-round default
    // keeps its original motion and completion behavior.
    explicit Bc2PhysicalReloadProbe(bool enabled=false,unsigned rounds=1)noexcept:enabled_(enabled),rounds_(rounds){}
    // Edits only a private synthetic-fixture input copy before hand ownership.
    // Never publishes geometry or invokes native reload dispatch/ack functions.
    void Prepare(interaction::InputFrame&,const ReloadStateOwner&,std::string_view asset,const ReloadRawContact&,
        const PhysicalReloadProbeState&,std::int64_t observedNs,std::int64_t deadlineNs,std::int64_t nowNs)noexcept;
    void Observe(const PhysicalReloadProbeState&,std::int64_t nowNs)noexcept;
    bool CancelConsumer()const noexcept{return phase_==Phase::Done||phase_==Phase::Failed;}
    void Report(std::ostream&)const;
private:
    enum class Phase:unsigned {Warmup,Grab,WaitHold,Approach,Enter,Stroke,WaitAck,Done,Failed,Rearm};
    void PhaseTo(Phase,std::int64_t)noexcept;
    void Fail(unsigned,std::int64_t)noexcept;
    struct Input {interaction::InputFrame frame{};std::int64_t observed=0,deadline=0;ReloadStateOwner owner{};};
    std::array<std::optional<Input>,32> history_{};unsigned next_=0;
    bool enabled_=false;Phase phase_=Phase::Warmup;unsigned failure_=0;
    std::int64_t first_=0,phaseAt_=0,lastNow_=0,alignedAt_=0;
    ReloadStateOwner owner_{};std::optional<interaction::InputFrame> last_;
    math::Pose command_{};int loadedBefore_=-1,reserveBefore_=-1;
    unsigned completed_=0,submitted_=0,rounds_=1;
    bool preparationReserveFresh_=false,preparationRaw_=false;
    int preparationLoaded_=-1,preparationReserve_=-1,preparationCapacity_=-1;
    std::uint64_t lastRaw_=0,neutralFirst_=0,neutralLast_=0;
    struct Round {unsigned number=0,acquired=0;std::int64_t completedNs=0;
        std::uint64_t firstNeutral=0,lastNeutral=0,grabInput=0;int loaded=-1,reserve=-1;};
    std::array<Round,2> roundsEvidence_{};
    struct Row {unsigned phase=0,reason=0;std::int64_t now=0;std::uint64_t input=0,raw=0;
        float positionError=0,angleError=0,rail=0;int loaded=-1,reserve=-1;};
    std::array<Row,192> rows_{};unsigned rowsCount_=0;std::int64_t rowAt_=0;
};
}
