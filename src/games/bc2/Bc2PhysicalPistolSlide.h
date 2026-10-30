#pragma once
#include "Bc2PistolAfterFeedEvidence.h"
#include "Bc2PistolSlidePresentation.h"
#include "Bc2EquipmentIdentity.h"
#include "Bc2ReloadPreview.h"
#include <memory>
#include <iosfwd>

namespace fvr::bc2 {
// Owned by the Gameplay result that consumed source.input. Renderers retain
// this immutable pointer with that publication; there is no global-latest read.
struct Bc2PhysicalPistolSlidePresentation {
    ReloadStateOwner owner{};
    Bc2PistolSlideSource source{};
    interaction::HandInteractionSample currentSafety{};
    interaction::HandClaim currentGun{},currentMechanism{};
    Bc2PistolSlideSelection selection{};
    std::uint64_t registrationKey=0;
    interaction::PistolAfterFeedTarget target{};
    std::uint64_t revision=0;
    float signedDisplacementMeters=0;
    bool blocksFire=true;
};
struct Bc2PhysicalPistolSlideApi {
    void* context=nullptr;
    std::optional<PistolAfterFeedEvent> (*event)(void*,std::uint64_t after)noexcept=nullptr;
    std::optional<PistolAfterFeedResourceState> (*read)(void*,const interaction::AmmoResourceIdentity&,std::int64_t)noexcept=nullptr;
    bool (*release)(void*,const PistolAfterFeedRelease&)noexcept=nullptr;
    std::optional<PistolAfterFeedActorRetirement> (*retirement)(void*,std::uint64_t after)noexcept=nullptr;
};
struct Bc2PhysicalPistolSlideSample {
    ReloadStateOwner owner{};
    std::optional<WeaponEquipmentIdentity> equipment;
    std::optional<AmmoResourceBinding> binding;
    interaction::HandInteractionSample input{};
    // Actual outer Gameplay arbiter observation, captured before and after
    // Update. Retained with its source packet so delayed raw release is real.
    std::optional<interaction::HandClaim> previousLeft;
    interaction::HandInteractionResult lifecycle{};
    interaction::HandInteractionKey weapon{};
    std::shared_ptr<const SelectedMeshesSnapshot> selected;
    ReloadRawContact raw{};
    std::string_view asset{},configurationPath{},meshPath{};
    std::uint64_t frameSequence=0,rigFingerprint=0;
    bool selectedRigVerified=false;
    bool grip=false,reloadPressed=false,cancel=false;
};
struct Bc2PhysicalPistolSlideResult {
    bool selected=false,blocksFire=false,ownsHand=false,pending=false;
    // Retained unfinished slide policy permits idle holstering. A real hand
    // claim or unresolved native request alone blocks equipment changes.
    bool blocksEquipment=false,evidenceGap=false,releaseRefused=false;
    interaction::PistolSlidePhase phase=interaction::PistolSlidePhase::Idle;
    interaction::PistolSlideFailure failure=interaction::PistolSlideFailure::None;
    std::shared_ptr<const Bc2PhysicalPistolSlidePresentation> presentation;
};
class Bc2PhysicalPistolSlide {
public:
    explicit Bc2PhysicalPistolSlide(Bc2PhysicalPistolSlideApi api={})noexcept;
    ~Bc2PhysicalPistolSlide();
    Bc2PhysicalPistolSlide(const Bc2PhysicalPistolSlide&)=delete;
    Bc2PhysicalPistolSlide& operator=(const Bc2PhysicalPistolSlide&)=delete;
    Bc2PhysicalPistolSlideResult Tick(const Bc2PhysicalPistolSlideSample&,interaction::HandInteraction&,std::uint64_t& intent)noexcept;
    void Suspend(const interaction::HandInteractionSample&,interaction::HandInteraction&)noexcept;
    void Report(std::ostream&)const;
    // Suppression only. Exact fresh binding is mandatory; address/name alone
    // cannot transfer an old resource's obligation to a replacement pickup.
    bool BlocksFire(const ReloadStateOwner&,const WeaponEquipmentIdentity&,
        const std::optional<AmmoResourceBinding>& freshBinding={})const noexcept;
private:
    struct State;
    std::unique_ptr<State> state_;
};
}
