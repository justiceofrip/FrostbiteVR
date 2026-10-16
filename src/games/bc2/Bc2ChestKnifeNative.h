#pragma once
#include "KnifeNativeReader.h"
#include "Bc2KnifeDrawReturnGate.h"
#include "Bc2BodyHolster.h"
#include "Bc2OriginalFiringObserver.h"
#include <ostream>
namespace fvr::bc2::chestKnifeNative {
// PRIVATE only. Existing draw/hold/return default; motion requires a separate
// explicit pre-start opt-in. Requires the existing firing dispatcher installed.
bool Install(std::span<const std::byte>,const engine::PeImage&,std::uintptr_t imageBase,
    const ReloadStateMemory&,bool motionEnabled=false)noexcept;
void Start()noexcept;
struct View {
    private_knife::Anchor anchor{};private_knife::Snapshot snapshot{};
    bool owned=false,healthy=false,observationComplete=false,motionEnabled=false;
};
// Current coherent observation only; it creates no staged native action.
std::optional<View> Inspect(const ReloadStateOwner&,unsigned knifeWeapon,std::uint64_t sequence,std::int64_t nowNs)noexcept;
// No-effect draw cleanup after its exact new paired empty receipt.
void AcknowledgeNoEffectRehide(const Bc2BodyHolster&)noexcept;

// Original gather thread only. Call before portable Prepare sees this source.
// False means skip knife processing; other gameplay remains independent.
// Drains an accepted stage or atomically revokes an unconsumed old stage.
// Call as soon as the gather sees actual new hand metadata, before other
// consumers. This revokes only an unconsumed first native boundary.
void SourceAdvanced(const interaction::HandInteractionSample&)noexcept;
bool BeforePrepare(const interaction::KnifeSample&,interaction::ChestKnifeCustody&)noexcept;
// Proves real shared claims through policy.CanCommit and repeats actual native
// identity/config/count admission. Chest draw body interval must already exist.
bool Stage(const interaction::KnifeCommand&,const interaction::KnifeSample&,
    const interaction::HandInteraction&,interaction::ChestKnifeCustody&,
    const private_knife::Anchor&,const ReloadStateOwner& actualSelectedOwner,
    std::uint64_t bodyOperation)noexcept;
// Read-only native outcome. Return is delivered only after the coordinator's
// exact operation has a NEW current paired hide + empty-hand receipt.
std::optional<interaction::KnifeReceipt> Observe(const ReloadStateOwner& actualSelectedOwner,
    const Bc2BodyHolster&,const std::optional<BodyFreeRightEvidence>&,std::int64_t nowNs)noexcept;
// True is permission to begin new hide of the observed actual original weapon,
// not permission to select it. Still requires explicit accepted Return command.
std::optional<std::uint64_t> ReturnNeedsRehide()noexcept;
// Known actual native actor replacement only; missing input/read is not replacement.
void ObserveLifetime(const ReloadStateOwner&)noexcept;
// Explicit session close, independent of XR input. Settled Held only; accepted
// incomplete operations remain owned. Keep dispatcher/guards alive while polling.
enum class SessionCloseStatus {Pending,Complete,Unresolved};
void RequestSessionClose()noexcept;
SessionCloseStatus PollSessionClose()noexcept;
// Stop the shared firing dispatcher only after close succeeds. False retains callbacks/module.
bool Stop()noexcept;
void Report(std::ostream&);
}
