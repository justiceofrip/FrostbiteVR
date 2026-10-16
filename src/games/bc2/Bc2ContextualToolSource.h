#pragma once
#include "Bc2EquipmentIdentity.h"
#include "Bc2ReloadState.h"
#include "fvr/interaction/ControllerInput.h"
#include "fvr/interaction/HandInteraction.h"
#include "fvr/interaction/TrackingMath.h"
#include <optional>

namespace fvr::bc2 {
// Observation of the existing generic, settled native wrist/weapon binding.
// No muzzle, authored tool offset, native action or paired-eye claim is implied.
struct ContextualToolAttachment {
    ReloadStateOwner owner{};
    interaction::HandInteractionSample input{};
    WeaponEquipmentIdentity equipment{};
    std::uint64_t rigFingerprint=0;
    math::Matrix4 nativeWristInWeapon{};
};
// Readback from the already verified native HaloAimer setters and copied
// soldier aim fields. This acknowledges steering, never a lock/mission hit.
struct ContextualToolAim {
    ReloadStateOwner owner{};
    interaction::HandInteractionSample input{};
    std::uint64_t operation=0;
    unsigned aimer=0;
    float yaw=0,pitch=0;
};
bool ContextualToolSourceCurrent(const interaction::InputFrame&,
    const interaction::HandInteractionSample&,std::int64_t now)noexcept;
bool ContextualToolQuiet(const interaction::InputFrame&,bool allowRightClick=false)noexcept;
// A newer render sample can tighten a current-input contact, but cannot widen
// a lease already recorded for that same input. Token/owner/contact admission
// still belongs to the caller and HandInteraction::Renew.
inline interaction::HandContactProof ContextualToolRenewalContact(
    interaction::HandContactProof contact,const interaction::HandClaim& current)noexcept {
    if(contact.inputSequence==current.inputSequence&&contact.deadlineNs>current.deadlineNs)
        contact.deadlineNs=current.deadlineNs;
    return contact;
}
bool ContextualToolAttachmentCurrent(const ContextualToolAttachment&,const ReloadStateOwner&,
    const interaction::HandInteractionSample&,std::uint64_t selectedSequence,std::int64_t now)noexcept;
std::optional<ContextualToolAim> ObserveContextualToolAim(const ReloadStateOwner&,
    const interaction::InputFrame&,const interaction::HandInteractionSample&,std::uint64_t operation,
    unsigned aimer,float desiredYaw,float desiredPitch,float actualYaw,float actualPitch,
    float copiedYaw,float copiedPitch)noexcept;
bool ContextualToolAimCurrent(const ContextualToolAim&,const ReloadStateOwner&,
    const interaction::HandInteractionSample&,std::uint64_t operation,std::int64_t now)noexcept;

enum class ContextualToolToggleKind {None,Draw,Return};
struct ContextualToolToggleResult {
    ContextualToolToggleKind kind=ContextualToolToggleKind::None;
    interaction::HandContactProof intent{}; // Explicit binding eligibility, not spatial contact.
};
// Right-click alone is currently unmapped. Both clicks retain recenter. A new
// neutral packet is required after owner/space/input loss and after every edge.
class ContextualToolToggle {
public:
    ContextualToolToggleResult Update(const interaction::InputFrame&,
        const interaction::HandInteractionSample&,interaction::HandInteractionKey,bool active)noexcept;
    void Missing()noexcept{armed_=false;owner_={};}
private:
    interaction::HandInteractionOwner owner_{};
    interaction::HandInteractionKey item_{};
    bool active_=false;
    std::uint64_t sequence_=0;
    std::int64_t observed_=0,deadline_=0;
    bool armed_=false;
};
}
