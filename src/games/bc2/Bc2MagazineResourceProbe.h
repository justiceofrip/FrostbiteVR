#pragma once
#include "Bc2MagazinePhysicalProbe.h"

namespace fvr::bc2 {
// Explicit finite controller diagnostic. The only mutable gameplay argument is
// a synthetic InputFrame, supplied BEFORE ordinary action/hand arbitration.
// Observe never dispatches, acknowledges, cancels or changes the consumer.
class Bc2MagazineResourceProbe {
public:
    enum class Phase:unsigned {Warmup,ApproachMagazine,Grip,Pull,Release,Pouch,
        GrabReplacement,ApproachRail,Enter,Stroke,WaitReceipt,Settle,Done,Failed,Carry};
    explicit Bc2MagazineResourceProbe(bool enabled=false,bool originalReturn=false,
        bool returnThenReplace=false,bool chestSupply=true)noexcept:
        enabled_(enabled),originalReturn_(originalReturn||returnThenReplace),combined_(returnThenReplace),chest_(chestSupply){}
    void Prepare(interaction::InputFrame&,const ReloadStateOwner&,std::string_view,
        const MagazineRawContact&,const MagazineResourceProbeState&,
        std::int64_t observed,std::int64_t deadline,std::int64_t now)noexcept;
    void Observe(const MagazineResourceProbeState&,const MagazinePackCounters&,std::int64_t now)noexcept;
    bool CancelConsumer()const noexcept{return phase_==Phase::Done||phase_==Phase::Failed;}
    bool Completed()const noexcept{return phase_==Phase::Done;}
    Phase Current()const noexcept{return phase_;}
    void Report(std::ostream&)const;
private:
    void Move(Phase,std::int64_t)noexcept;
    void Fail(unsigned,std::int64_t)noexcept;
    void Begin(const AmmoResourceView&,std::int64_t)noexcept;
    bool Receipt(const AmmoResourceView&,const interaction::AmmunitionReceipt&,std::int64_t)const noexcept;
    struct Input {interaction::InputFrame frame;ReloadStateOwner owner;std::int64_t observed,deadline;};
    std::array<std::optional<Input>,32> history_{};unsigned next_=0;
    std::optional<interaction::InputFrame> lastInput_;
    const MagazineEquipmentProfile* profile_=nullptr;
    ReloadStateOwner owner_{};interaction::AmmoResourceContext context_{};
    std::optional<interaction::RemovedMagazineResource> original_;
    std::optional<interaction::AmmunitionReceipt> removal_,seat_;
    interaction::AmmunitionCounts before_{};
    std::uint64_t requestBaseline_=0,seatRequest_=0,lastRaw_=0;
    unsigned acquiredBaseline_=0,completedCycles_=0;
    bool enabled_=false,originalReturn_=false,combined_=false,second_=false,chest_=true,armed_=false;
    bool removedPair_=false,hiddenPair_=false,replacementPair_=false,attachedPair_=false;
    bool seatHandReleased_=false,supportReleased_=false,discarded_=false;
    Phase phase_=Phase::Warmup;unsigned failure_=0;
    std::int64_t first_=0,armedAt_=0,phaseAt_=0,lastNow_=0,alignedAt_=0,gapAt_=0;
    math::Pose command_{};MagazinePackCounters packs_{},baseline_{};
    std::array<std::optional<unsigned>,4> roleStarts_{};
    struct Cycle {interaction::AmmunitionCounts before{},after{};std::uint64_t remove=0,seat=0;
        bool originalReturn=false,discarded=false,handReleased=false,supportReleased=false;};
    std::array<Cycle,2> cycles_{};
    // Immutable copies of accepted native receipts for post-run correlation.
    // They never return to a consumer or change resource completion.
    ReloadHoldIdentity nativeIdentity_{};
    struct NativeCycle {ReloadHoldIdentity identity;interaction::AmmunitionReceipt remove,seat;};
    std::array<NativeCycle,2> nativeCycles_{};
    struct Row {unsigned phase=0,failure=0;std::int64_t now=0;};
    std::array<Row,96> rows_{};unsigned rowCount_=0;
};
}
