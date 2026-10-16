#pragma once
#include "Bc2ContextualToolInput.h"
#include "Bc2BodyHolster.h"
#include "Bc2ContextualToolSource.h"
#include "Bc2ContextualToolLifetime.h"
#include "Bc2ContextualToolProbeWitness.h"

namespace fvr::bc2 {
struct ToolControlLevels {bool zoom=false,fire=false;};
// Fresh original source + exact current shared GunHold. First selected/claim
// observation is a boundary; a newer two-trigger neutral arms both controls.
class ContextualToolControlGate {
public:
    ToolControlLevels Update(const ContextualToolObservation&,const interaction::InputFrame&,
        const interaction::HandInteractionSample&,const interaction::HandClaimToken&,
        const interaction::HandInteraction&)noexcept;
    void Suspend()noexcept{identity_.reset();armed_=false;}
private:
    struct Identity {ReloadStateOwner owner{};ContextualToolData tool{};
        interaction::HandClaimToken hold{};bool operator==(const Identity&)const=default;};
    std::optional<Identity> identity_;
    std::uint64_t sequence_=0;std::int64_t observed_=0,deadline_=0,predicted_=0;
    std::array<float,2> triggers_{},squeezes_{};
    std::array<std::uint32_t,2> active_{},held_{};
    bool armed_=false;
};
struct ContextualToolBoundary {
    HolsterInputOwner cache;
    void* clockContext=nullptr;
    std::int64_t(*now)(void*)noexcept=nullptr;
    bool(*aimCurrent)(void*,const ContextualToolAim&)noexcept=nullptr;
};
enum class ContextualToolPhase:unsigned {Idle,Selecting,AwaitNeutral,Held,Returning,Rehiding,Cleanup};
class Bc2ContextualToolAdapter {
public:
    // Static/native layout verification only. No process calls or new hooks.
    bool Configure(const WeaponModeMemory&,const ContextualToolBinding&,const ReloadStateBinding&)noexcept;
    // Serialized gather, after this call's ordinary suppression + TickHolster.
    // Clears full-suppression/freeRight output whenever tool writes commit.
    std::optional<ToolInputReceipt> Tick(const BodyDrawSample&,BodyHolsterSample&,
        BodyHolsterResult&,const std::optional<ContextualToolAttachment>&,
        const std::optional<ContextualToolAim>&,bool otherMechanicsSettled,
        std::span<std::byte> actualCache,ContextualToolBoundary,Bc2BodyInventory&,
        Bc2BodyHolster&,interaction::HandInteraction&,std::uint64_t& sharedIntent)noexcept;
    // Only an actually selected admitted tool in this live shared body operation.
    bool WantsHeadAim(const ReloadStateOwner&,const interaction::InputFrame&,
        const interaction::HandInteractionSample&,const Bc2BodyHolster&)const noexcept;
    void Missing(const interaction::HandInteractionSample&,interaction::HandInteraction&)noexcept;
    bool Active()const noexcept{return phase_!=ContextualToolPhase::Idle;}
    ContextualToolPhase Phase()const noexcept{return phase_;}
    std::uint64_t Operation()const noexcept{return lifetime_.Operation();}
    const std::optional<interaction::HandClaimToken>& Hold()const noexcept{return hold_;}
    void PublishProbe(const BodyDrawSample&,const BodyHolsterSample&,const BodyHolsterResult&,
        const Bc2BodyHolster&,const interaction::HandInteraction&)noexcept;
    std::optional<ContextualToolProbeWitness> ProbeWitness(std::int64_t now)const noexcept{return probe_.Read(now);}
    void Report(std::ostream&)const;
private:
    struct WriteContext;
    bool Current(WriteContext&,const ToolInputRequest&)const noexcept;
    std::optional<ContextualToolObservation> Read(const ReloadStateOwner&)const noexcept;
    void SuspendHold(const interaction::HandInteractionSample&,interaction::HandInteraction&)noexcept;
    void Release(const interaction::HandInteractionSample&,interaction::HandInteraction&)noexcept;
    void Retire(const interaction::HandInteractionSample&,interaction::HandInteraction&,bool unresolved,std::uint64_t currentBodyOperation)noexcept;
    WeaponModeMemory memory_{};ContextualToolBinding binding_{};ReloadStateBinding firingBinding_{};bool configured_=false;
    ContextualToolPhase phase_=ContextualToolPhase::Idle;
    ContextualToolLifetime lifetime_;
    ContextualToolProbePublication probe_;
    std::optional<interaction::HandClaimToken> hold_;
    interaction::BodyItemKey toolKey_{};
    ContextualToolControlGate controls_;
    ContextualToolToggle toggle_;
    std::uint64_t serial_=0,boundarySequence_=0,drawSequence_=0;
    bool regripArmed_=false;
    std::uint64_t equipCommits_=0,selected_=0,controlCommits_=0,returnCommits_=0,returned_=0,
        rejected_=0,readGaps_=0,unresolvedRetirements_=0,metadataRevocations_=0,commitRecordFailures_=0;
};
}
