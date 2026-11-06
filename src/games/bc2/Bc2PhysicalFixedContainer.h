#pragma once
#include "Bc2FixedContainerPresentation.h"
#include <memory>
#include <ostream>

namespace fvr::bc2 {
// Bounded diagnostic copies only. Historical receipts never grant a current
// input/native lease; the post snapshot is the one consumed by policy Complete.
struct Bc2FixedContainerCompletionObservation {
    std::uint64_t lifetime=0;
    interaction::FeedSupplyKey supply{};
    AmmoResourceOutcome outcome{};
    interaction::AmmunitionSnapshot postSnapshot{};
    std::int64_t acceptedNs=0;
};
enum class Bc2FixedContainerReleaseStage:std::uint8_t {
    Captured,DuplicateRaw,RawRejected,GateUnavailable,LedgerUnavailable,
    Policy,RequestUnavailable,Submitted,SubmissionRejected,Invalidated,Cancelled
};
// One historical attempt, retained across duplicate polls. This copy grants no
// claim, source lease, gesture completion or native ammunition authority.
struct Bc2FixedContainerReleaseObservation {
    std::uint64_t lifetime=0,lastRaw=0;
    interaction::FeedSupplyKey supply{};
    interaction::FixedContainerReleaseEvidence evidence{};
    interaction::HandInteractionSample input{},rawInput{};
    Bc2FixedContainerReleaseStage stage=Bc2FixedContainerReleaseStage::Captured;
    interaction::FixedContainerStatus status=interaction::FixedContainerStatus::Invalid;
    interaction::FixedContainerRejection rejection=interaction::FixedContainerRejection::Unknown;
    interaction::FeedInteractionStatus feedStatus=interaction::FeedInteractionStatus::NeedNeutral;
    bool rawAdmitted=false,retainedEvidence=false,policyUpdated=false,dispatch=false;
};
struct Bc2FixedContainerObservation {
    ReloadStateOwner owner{};
    interaction::HandInteractionSample input{};
    const FixedContainerEquipmentProfile* equipment=nullptr;
    Bc2FixedContainerRawContact raw{};
    Bc2FixedContainerTracking tracking{};
    interaction::FeedSupplyKey supply{};
    interaction::AmmoSupplyContact pouch{};
    interaction::SupplyAnchorFrame frame=interaction::SupplyAnchorFrame::RecenteredBody;
    std::optional<AmmoResourceView> view;
    std::optional<AmmoResourceRequest> pendingRequest;
    std::optional<interaction::HandClaim> leftClaim;
    std::optional<Bc2FixedContainerCompletionObservation> lastCompletion;
    std::optional<Bc2FixedContainerReleaseObservation> lastRelease;
    interaction::FixedContainerStatus status=interaction::FixedContainerStatus::Invalid;
    bool rawAdmitted=false; // Exact Tick predicate, including recorded hand evidence.
};
bool FixedContainerObservationJoined(const Bc2FixedContainerObservation&,const ReloadStateOwner&,
    const interaction::HandInteractionSample&,const FixedContainerEquipmentProfile*,std::int64_t now)noexcept;
// Pure serialization of immutable copies/static profiles. No native/service reads.
void WriteFixedContainerObservation(std::ostream&,const Bc2FixedContainerObservation*,const ReloadStateOwner&,
    const interaction::HandInteractionSample&,const RigIdentity&,const FixedContainerEquipmentProfile*,std::int64_t now);
struct Bc2PhysicalFixedContainerApi {
    void* context=nullptr;
    std::optional<AmmoResourceView> (*read)(void*,const ReloadStateOwner&,std::int64_t)noexcept=nullptr;
    bool (*withLedger)(void*,const AmmoResourceBinding&,const interaction::HandInteractionOwner&,void*,
        bool (*)(void*,interaction::AmmunitionLedger&)noexcept)noexcept=nullptr;
    bool (*submit)(void*,const AmmoResourceRequest&)noexcept=nullptr;
    std::optional<AmmoResourceOutcome> (*outcome)(void*,std::uint64_t,const interaction::AmmoResourceContext&)noexcept=nullptr;
    bool (*publishGate)(void*,const FixedContainerGate&)noexcept=nullptr;
    std::optional<FixedContainerGate> (*readGate)(void*,const interaction::AmmoResourceIdentity&)noexcept=nullptr;
    std::uint64_t (*nextRequest)(void*)noexcept=nullptr;
    // Optional typed result; a legacy false remains a genuine rejection.
    FixedContainerGateResult (*publishGateObserved)(void*,const FixedContainerGate&)noexcept=nullptr;
};
struct Bc2PhysicalFixedContainerSample {
    ReloadStateOwner nativeOwner{};
    interaction::HandInteractionSample input{};
    interaction::HandInteractionKey weapon{};
    std::optional<FixedContainerBodyAmmo::FamilyBinding> family;
    const FixedContainerEquipmentProfile* equipment=nullptr;
    std::string_view configurationPath,mesh;
    std::uint64_t rigFingerprint=0,geometrySequence=0,trackingEpoch=0;
    math::Matrix4 bodyFromHand{};
    Bc2FixedContainerRawContact raw{};
    std::optional<interaction::FixedContainerReleaseEvidence> releaseEvidence;
    bool grip=false,gripPressed=false,cancel=false,finishLoading=false;
    Bc2FixedContainerObservation* observation=nullptr; // On-demand stack copy; never retained.
};
struct Bc2PhysicalFixedContainerResult {
    Bc2FixedContainerTracking tracking{};
    std::optional<FixedContainerBodyAmmo> bodyAmmo;
    std::optional<interaction::AmmoSupplyVisualSample> bodyVisual;
    bool blocksFire=false,ammoOwnsHand=false,mechanismOwnsHand=false;
    interaction::FixedContainerStatus status=interaction::FixedContainerStatus::Invalid;
};
// Exact first failed predicate in the existing short-circuit admission order.
enum class FixedContainerReleaseRejection:std::uint8_t {
    Unknown,Disabled,ReadUnavailable,LedgerUnavailable,SubmitUnavailable,
    OutcomeUnavailable,GateUnavailable,RequestUnavailable,EquipmentUnknown,
    EquipmentMismatch,CancelRequested,InputNotFresh,ViewMissing,ViewOwner,
    ViewStale,PhysicalItemUnavailable,WeaponMismatch,CapacityMismatch,
    GunMissing,GunKind,GunItem,GunOwner,GunInput,GunExpired,
    RawInvalid,RawEquipment,RawNativeOwner,RawHandOwner,RawSequence,
    RawObserved,RawInputNotFresh,RawSoldier,RawWeak,RawWrist,RawWorld,RawEvidence
};
class Bc2PhysicalFixedContainer {
public:
    explicit Bc2PhysicalFixedContainer(bool enabled=false,Bc2PhysicalFixedContainerApi api={})noexcept;
    ~Bc2PhysicalFixedContainer();
    Bc2PhysicalFixedContainer(const Bc2PhysicalFixedContainer&)=delete;
    Bc2PhysicalFixedContainer& operator=(const Bc2PhysicalFixedContainer&)=delete;
    Bc2PhysicalFixedContainerResult Tick(const Bc2PhysicalFixedContainerSample&,interaction::HandInteraction&,std::uint64_t& sharedIntent)noexcept;
    void Cancel(const interaction::HandInteractionSample&,interaction::HandInteraction&,interaction::HandReleaseCause cause={})noexcept;
    void Report(std::ostream&)const;
    static interaction::AmmoSupplyContact BodyAmmoContact()noexcept {const auto p=interaction::ChestAmmoSupply();return {p.pouchCenterMeters,p.pouchRadiusMeters};}
    void SetBodyContact(interaction::AmmoSupplyContact contact)noexcept {if(interaction::ValidBodyAnchor({1,
        {contact.centerMeters[0],contact.centerMeters[1],contact.centerMeters[2]},contact.radiusMeters}))pouch_=contact;}
    void SetBodyFrame(interaction::SupplyAnchorFrame frame)noexcept{frame_=frame;}
private:
    struct Slot;
    bool enabled_=false;Bc2PhysicalFixedContainerApi api_{};
    interaction::SupplyAnchorFrame frame_=interaction::SupplyAnchorFrame::RecenteredBody;
    interaction::AmmoSupplyContact pouch_=BodyAmmoContact();
    std::array<std::unique_ptr<Slot>,64> slots_{};Slot* active_=nullptr;
    std::uint64_t nextLifetime_=0,ticks_=0,submitted_=0,completed_=0,rejected_=0;
};
}
