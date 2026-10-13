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
    // Combined scripts already wait for real inventory/renderer publication.
    // Shorten only their redundant startup delay, before any input is observed.
    bool UseCombinedWarmup()noexcept {
        if(!enabled_||first_||armed_||phase_!=Phase::Warmup)return false;
        warmupNs_=1000000000;return true;
    }
    void Prepare(interaction::InputFrame&,const ReloadStateOwner&,std::string_view,
        const MagazineRawContact&,const MagazineResourceProbeState&,
        std::int64_t observed,std::int64_t deadline,std::int64_t now)noexcept;
    void Observe(const MagazineResourceProbeState&,const MagazinePackCounters&,std::int64_t now)noexcept;
    bool CancelConsumer()const noexcept{return phase_==Phase::Done||phase_==Phase::Failed;}
    bool Completed()const noexcept{return phase_==Phase::Done;}
    bool PresentationContinuityVerified()const noexcept{return Completed()&&presentationFallbacks_==0;}
    Phase Current()const noexcept{return phase_;}
    // Immutable diagnostic witness only. It cannot acknowledge or alter the
    // ordinary resource consumer, and requires the actual removal receipt.
    std::optional<AmmoResourceView> DetachedOriginal(const MagazineResourceProbeState&,std::int64_t now)const noexcept;
    void Report(std::ostream&)const;
    enum GuardFault:unsigned {
        OwnerChanged=1u,ProfileMissing=2u,ProfileChanged=4u,SpaceMismatch=8u,
        InvalidFrame=16u,Unfocused=32u,InvalidHead=64u,MovingHead=128u,
        LeftGripLost=256u,RightGripLost=512u,RightAimLost=1024u,
        InvalidObservation=2048u,FutureObservation=4096u,ExpiredInput=8192u,LongLease=16384u
    };
    struct GuardFailure {
        unsigned faults=0;Phase phase=Phase::Warmup;bool armed=false;
        ReloadStateOwner expected{},actual{};
        std::uint64_t sequence=0,previousSequence=0,space=0;
        std::int64_t observed=0,deadline=0,now=0;
        std::uintptr_t publishedProfile=0,selectedProfile=0,expectedProfile=0;
    };
    const std::optional<GuardFailure>& FirstGuardFailure()const noexcept{return guardFailure_;}
private:
    void Move(Phase,std::int64_t)noexcept;
    void Fail(unsigned,std::int64_t)noexcept;
    void Begin(const AmmoResourceView&,std::int64_t)noexcept;
    bool IdleCancellation(const MagazineResourceProbeState&,std::int64_t)const noexcept;
    bool Receipt(const AmmoResourceView&,const interaction::AmmunitionReceipt&,std::int64_t)const noexcept;
    struct Input {interaction::InputFrame frame;ReloadStateOwner owner;std::int64_t observed,deadline;};
    std::array<std::optional<Input>,32> history_{};unsigned next_=0;
    std::optional<interaction::InputFrame> lastInput_;
    // First failing Prepare only; later shutdown/expired packets cannot replace
    // onset evidence. This observation never relaxes a driver admission rule.
    std::optional<GuardFailure> guardFailure_;
    std::int64_t idleRecoveryAt_=0;
    unsigned idleRecoveries_=0;
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
    // A render miss fails visual continuity, but must not neutralize the
    // emulated player's grip and manufacture a mechanical cancellation.
    std::uint64_t presentationFallbacks_=0;std::int64_t firstPresentationFallbackNs_=0;
    std::int64_t first_=0,armedAt_=0,phaseAt_=0,lastNow_=0,alignedAt_=0,gapAt_=0,warmupNs_=6000000000ll;
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
