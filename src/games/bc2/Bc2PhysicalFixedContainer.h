#pragma once
#include "Bc2FixedContainerPresentation.h"
#include <memory>
#include <ostream>

namespace fvr::bc2 {
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
};
struct Bc2PhysicalFixedContainerSample {
    ReloadStateOwner nativeOwner{};
    interaction::HandInteractionSample input{};
    interaction::HandInteractionKey weapon{};
    const FixedContainerEquipmentProfile* equipment=nullptr;
    std::string_view configurationPath,mesh;
    std::uint64_t rigFingerprint=0,geometrySequence=0,trackingEpoch=0;
    math::Matrix4 bodyFromHand{};
    Bc2FixedContainerRawContact raw{};
    std::optional<interaction::FixedContainerReleaseEvidence> releaseEvidence;
    bool grip=false,gripPressed=false,cancel=false,finishLoading=false;
};
struct Bc2PhysicalFixedContainerResult {
    Bc2FixedContainerTracking tracking{};
    std::optional<FixedContainerBodyAmmo> bodyAmmo;
    std::optional<interaction::AmmoSupplyVisualSample> bodyVisual;
    bool blocksFire=false,ammoOwnsHand=false,mechanismOwnsHand=false;
    interaction::FixedContainerStatus status=interaction::FixedContainerStatus::Invalid;
};
class Bc2PhysicalFixedContainer {
public:
    explicit Bc2PhysicalFixedContainer(bool enabled=false,Bc2PhysicalFixedContainerApi api={})noexcept;
    ~Bc2PhysicalFixedContainer();
    Bc2PhysicalFixedContainer(const Bc2PhysicalFixedContainer&)=delete;
    Bc2PhysicalFixedContainer& operator=(const Bc2PhysicalFixedContainer&)=delete;
    Bc2PhysicalFixedContainerResult Tick(const Bc2PhysicalFixedContainerSample&,interaction::HandInteraction&,std::uint64_t& sharedIntent)noexcept;
    void Cancel(const interaction::HandInteractionSample&,interaction::HandInteraction&)noexcept;
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
