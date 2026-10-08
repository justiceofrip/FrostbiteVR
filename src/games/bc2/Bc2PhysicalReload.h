#include "Bc2ReloadStartOrigin.h"
#include "Bc2ReloadObservation.h"
#include "Bc2ReloadKeepAlive.h"
#pragma once
#include "Bc2AmmoSupply.h"
#include "Bc2MagazineInteraction.h"
#include "Bc2ReloadPreview.h"
#include "Bc2ReloadRequestCycle.h"
#include "Bc2ReloadRetirement.h"
#include "fvr/interaction/ControllerInput.h"
#include "fvr/interaction/Feedback.h"
#include <ostream>

namespace fvr::bc2 {
struct PhysicalReloadApi {
    void* context=nullptr;
    std::optional<Bc2AmmoReserveLease> (*reserve)(void*)noexcept=nullptr;
    std::optional<ReloadHoldIdentity> (*identity)(void*)noexcept=nullptr;
    bool (*start)(void*,const ReloadCycleControl&)noexcept=nullptr;
    bool (*keep)(void*,const ReloadCycleControl&)noexcept=nullptr;
    std::optional<ReloadRoundLease> (*lease)(void*,const ReloadHoldIdentity&,std::uint64_t)noexcept=nullptr;
    bool (*submit)(void*,const Bc2ReloadNativeRequest&)noexcept=nullptr;
    std::optional<Bc2ReloadAckEvidence> (*ack)(void*,const ReloadHoldIdentity&,std::uint64_t)noexcept=nullptr;
    void (*cancel)(void*)noexcept=nullptr;
    std::optional<ReloadCycleRetirement> (*retire)(void*,const ReloadHoldIdentity&,std::uint64_t)noexcept=nullptr;
    std::int64_t (*clock)(void*)noexcept=nullptr;
    ReloadKeepAliveResult (*keepObserved)(void*,const ReloadCycleControl&)noexcept=nullptr;
    ReloadReserveObservation (*reserveObserved)(void*)noexcept=nullptr;
};
// Observation labels only. They never authorize native state changes.
enum class PhysicalReloadCancelReason : unsigned {
    External, ApiOrClock, UnsafeInput, RequestedInput, WrongAsset, OwnerChanged,
    MissingSource, FullMagazine, EmptyReserve, Retiring, IntentExhausted,
    KeepAliveRejected, BridgeCancelled, StartIdentityRejected, StartRejected,
    HoldNotEstablished, ManualPolicyCancelled, NativeOwnersRejected,
    NativeLeaseRejected, SubmissionRejected
};
enum PhysicalReloadCancelFlag : unsigned {
    ReloadCancelBodyDraw=1, ReloadCancelFixture=2, ReloadCancelAimInvalid=4,
    ReloadCancelInactive=8, ReloadCancelFire=16, ReloadCancelUse=32,
    ReloadCancelNextWeapon=64, ReloadCancelPreviousWeapon=128, ReloadCancelNoPouch=256, ReloadCancelMagazineBusy=512
};
struct PhysicalReloadSample {
    bool actionFlagsKnown=false;std::uint64_t actionHeld=0,actionPressed=0;
    ReloadStateOwner nativeOwner{};
    interaction::HandInteractionSample input{};
    interaction::HandInteractionKey weapon{};
    math::Matrix4 bodyFromHand{};
    std::uint64_t geometrySequence=0,trackingEpoch=0;
    bool gripPressed=false,cancel=false;
    unsigned cancelFlags=0; // Diagnostic source bits; policy still consumes cancel only.
    std::string_view asset{};
    std::shared_ptr<const SelectedMeshesSnapshot> meshes;
    ReloadRawContact raw{};
    std::optional<interaction::HandInteractionSample> originalHandEvidence;
    // Current read-only identity mapping for a different held magazine-family
    // item. Used only to retire an old SPAS action block, never shell authority.
    std::optional<MagazineFamilyEvidence> replacementFamily;
};
struct PhysicalReloadResult {
    ReloadTracking tracking{};
    bool reloadHeld=false,ammoOwnsHand=false;
    std::array<interaction::FeedbackEvent,2> feedback{};
    unsigned feedbackCount=0;
};
// Adapter composition, used directly by Gameplay. Runtime callbacks remain the
// sole native authority. Current source/claims and renderer N-1 evidence never
// exchange timestamps; counts are observed, never written here.
struct PhysicalReloadProbeState {
    bool active=false,held=false,nativeHolding=false,pending=false;
    unsigned acquired=0,submitted=0,completed=0;
    std::optional<Bc2AmmoReserveLease> reserve;
    ReloadHoldIdentity identity{};std::uint64_t cycle=0;
};
class Bc2PhysicalReload {
public:
    static interaction::AmmoSupplyConfig DefaultPouch()noexcept;
    explicit Bc2PhysicalReload(bool enabled=false,PhysicalReloadApi api={},
        interaction::AmmoSupplyConfig pouch=DefaultPouch())noexcept;
    void EnableBeltAmmo(bool enabled=true,interaction::SupplyAnchorFrame frame=interaction::SupplyAnchorFrame::HeadYaw)noexcept {beltEnabled_=enabled;beltFrame_=frame;}
    PhysicalReloadResult Tick(const PhysicalReloadSample&,interaction::HandInteraction&,std::uint64_t& sharedIntent)noexcept;
    void Cancel(const interaction::HandInteractionSample&,interaction::HandInteraction&,
        PhysicalReloadCancelReason reason=PhysicalReloadCancelReason::External,unsigned sourceFlags=0)noexcept;
    void Report(std::ostream&)const;
    bool BlocksEquipment()const noexcept {return active_||supply_.Held().has_value()||
        ((retiring_||supply_.Pending().has_value())&&blocksCurrent_);}
    PhysicalReloadProbeState ProbeState(std::int64_t now)const noexcept {
        return {active_,bool(supply_.Held()),lease_&&lease_->allThreeHeld&&lease_->deadlineNs>now,
            bool(supply_.Pending()),acquired_,submitted_,completed_,reserve_,native_,cycle_};
    }
private:
    struct Evidence {interaction::AmmoSupplySample supply{};interaction::HandClaim item{},gun{};};
    struct DeferredSeat {
        Bc2ReloadInteractionSample contact{};interaction::ReloadInsertionSeat seat{};
        Bc2ReloadTargets targets{};Evidence original{};
    };
    const Evidence* Original(const PhysicalReloadSample&)const noexcept;
    std::int64_t Now(std::int64_t fallback)const noexcept;
    bool Api()const noexcept;
    void Journal(unsigned,std::int64_t,unsigned reason=0,std::uint64_t seat=0,std::uint64_t sourceSequence=0)noexcept;
    void Feedback(PhysicalReloadResult&,interaction::FeedbackKind,const interaction::HandInteractionSample&)noexcept;
    void Retire(const PhysicalReloadSample&,interaction::HandInteraction&,const std::optional<interaction::AmmoSupplySource>&)noexcept;
    Bc2ReloadInteractionSample Interaction(const PhysicalReloadSample&,const Evidence*,const ReloadRoundLease&,std::int64_t)const noexcept;
    bool enabled_=false,active_=false,retiring_=false,beltEnabled_=false;
    // Durable exact callback-drain fact, not a native ammo/pose receipt. Pending
    // resource credit stays quarantined until the ordinary fresh rebaseline.
    bool retirementDrained_=false,blocksCurrent_=true;
    void RefreshRetirementBlock(const PhysicalReloadSample&,bool safe)noexcept;
    interaction::SupplyAnchorFrame beltFrame_=interaction::SupplyAnchorFrame::HeadYaw;
    interaction::AmmoSupplyConfig pouch_{};
    PhysicalReloadApi api_{};
    interaction::AmmoSupply supply_;
    Bc2ReloadInteraction insertion_{true};
    interaction::ManualReload manual_;
    Bc2ReloadRequestBridge bridge_{true};
    std::array<std::optional<Evidence>,32> history_{};std::size_t historyNext_=0;
    std::optional<Bc2AmmoReserveLease> reserve_;
    std::optional<ReloadRoundLease> lease_;
    std::optional<Bc2ReloadAckEvidence> acknowledgement_;
    std::optional<Bc2ReloadBridgeResult> completion_;
    std::optional<Bc2ReloadTargets> pendingTargets_;
    // Presentation continuity only: a missing renderer contact may reuse the
    // exact captured pose until its ORIGINAL leases expire. Never a seat event
    // or permission to advance the native reload transaction.
    std::optional<ReloadPreview> guidedPreview_;
    // Renderer contact can arrive after ManualReload already saw this input
    // generation. Retain the exact one-shot event until a fresh input can use it.
    std::optional<DeferredSeat> deferredSeat_;
    std::optional<ReloadCycleRetirement> retirement_;
    ReloadHoldIdentity native_{};
    interaction::HandInteractionOwner physical_{};
    std::uint64_t nextCycle_=0,cycle_=0,lastRaw_=0,nextFeedback_=0;
    std::array<interaction::FeedbackEvent,32> feedbackEvents_{};
    unsigned feedbackEventCount_=0,feedbackEventDropped_=0;
    ReloadKeepAliveResult Keep(const ReloadCycleControl&,std::int64_t)noexcept;
    std::int64_t acceptedControlDeadline_=0,startupGapDeadline_=0;
    unsigned startupGapWaits_=0,startupGapExpired_=0;
    std::int64_t pulseUntil_=0,started_=0,lastNow_=0;
    unsigned acquired_=0,startedCount_=0,submitted_=0,completed_=0,cancelled_=0,reconciled_=0;
    unsigned lastGeometryFailure_=0;
    // Bounded diagnostic copies only; never consumed as gameplay authority.
    struct Transaction {
        interaction::AmmoSupplyReservation reservation{};
        ReloadRoundLease before{},after{};
        Bc2ReloadAckEvidence ack{};Bc2AmmoReserveLease reserve{};
        interaction::HandInteractionSample original{},current{};
        std::int64_t submittedNs=0,resolvedNs=0;
        bool resolved=false;
    };
    std::array<Transaction,16> transactions_{};unsigned transactionCount_=0,transactionDropped_=0;
    void ReportTransactions(std::ostream&)const;
    // Bounded observation only. Raw metrics come from the exact original
    // contact used by the recognizer; they never drive capture or native ammo.
    struct GeometryRow {
        std::uint64_t cycle=0,item=0,source=0,current=0,priorSource=0;
        std::int64_t observed=0,deadline=0,now=0;
        std::array<float,3> rail{};
        float radial=0,distance=0,keyedAngle=0,axisAngle=0,deltaMeters=0,deltaAngle=0,progress=0,alignment=0;
        unsigned phase=0,adapterReason=0,insertionReason=0;
        bool captured=false,seated=false,withinCapture=false;
    };
    std::array<GeometryRow,192> geometryRows_{};
    unsigned geometryCount_=0,geometryNext_=0;
    std::uint64_t geometryTotal_=0,geometrySource_=0,geometryItem_=0,geometryCycle_=0;
    std::uint64_t geometryFree_=0,geometryGuided_=0,geometryCaptured_=0,geometrySeated_=0,geometryPoseJumps_=0;
    struct GeometryNearest {
        std::uint64_t cycle=0,item=0,samples=0,jumps=0,captures=0,seats=0;
        GeometryRow closest{},front{};bool frontValid=false;
    };
    std::array<GeometryNearest,16> geometryNearest_{};unsigned geometryNearestCount_=0;
    std::uint64_t geometryUnretainedSamples_=0;
    math::Matrix4 geometryPrior_{};
    void ObserveGeometry(const Bc2ReloadInteractionSample&,const Bc2ReloadInteractionResult&,std::uint64_t currentSequence)noexcept;
    void ReportGeometry(std::ostream&)const;
    // Transition-only diagnostic copies. Never consumed as gameplay authority.
    struct AvailabilityState {
        ReloadStateOwner native{};interaction::HandInteractionOwner physical{};
        interaction::HandInteractionKey weapon{};
        std::array<char,96> asset{};
        std::uint32_t flags=0,sourceFlags=0;
        unsigned cancelReason=~0u,supplyReason=~0u;
        int loaded=-1,reserve=-1,capacity=-1;
        bool operator==(const AvailabilityState&)const=default;
    };
    struct AvailabilityRow {
        AvailabilityState state{};
        std::uint64_t sequence=0,geometrySequence=0,rawSequence=0,meshSequence=0;
        std::int64_t now=0,observed=0,deadline=0,reserveObserved=0,reserveDeadline=0,rawObserved=0,rawDeadline=0;
        std::array<float,3> bodyHand{};float primaryDistance=0,alternateDistance=0;
    };
    std::array<AvailabilityRow,96> availability_{};
    std::optional<AvailabilityState> availabilityLast_;
    unsigned availabilityCount_=0,availabilityNext_=0;
    std::uint64_t availabilityTransitions_=0,availabilitySamples_=0;
    void ObserveAvailability(const PhysicalReloadSample&,const interaction::HandInteraction&,bool safe,bool selected,bool source,
        std::optional<PhysicalReloadCancelReason>,std::optional<interaction::AmmoSupplyReason>)noexcept;
    void ReportAvailability(std::ostream&)const;

    struct Cancellation {
        PhysicalReloadCancelReason reason=PhysicalReloadCancelReason::External;
        unsigned sourceFlags=0;interaction::HandInteractionSample input{};
        std::uint64_t cycle=0,itemGeneration=0,pendingRequest=0;
        bool held=false,pending=false,reservePresent=false,leasePresent=false,allThreeHeld=false;
        int loaded=-1,reserve=-1,capacity=-1;
        std::int64_t reserveObserved=0,reserveDeadline=0,leaseObserved=0,leaseDeadline=0;
    };
    std::array<Cancellation,32> cancellations_{};unsigned cancellationCount_=0,cancellationDropped_=0;
    void ObserveCancellation(const interaction::HandInteractionSample&,PhysicalReloadCancelReason,unsigned)noexcept;
    void ReportCancellations(std::ostream&)const;
    ReloadStartOrigin startOrigin_{};
    struct Row {unsigned event=0,reason=0;std::int64_t now=0;std::uint64_t cycle=0,item=0,request=0,itemId=0,claim=0,seat=0,sourceSequence=0;ReloadStartOrigin startOrigin{};};
    std::array<Row,128> rows_{};unsigned rowCount_=0,dropped_=0;
};
// Head-yaw-relative waist pouch; raw controller pose in metres. This is a
// configurable comfort anchor, not authored hip/IK geometry or native units.
std::optional<math::Matrix4> PhysicalReloadPouchPose(const interaction::InputFrame&)noexcept;
} // namespace fvr::bc2
