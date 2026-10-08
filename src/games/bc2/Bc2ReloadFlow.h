#pragma once
#include "Bc2ReloadState.h"
namespace fvr::bc2 {
enum class ReloadTransferPath : std::uint8_t {OrdinaryState12,TimedInterruption,SpecialLogic4,ZeroDurationPreparation};
struct ReloadTransferSite {std::uint32_t returnRva=0;ReloadTransferPath path=ReloadTransferPath::OrdinaryState12;};
struct ReloadFlowBinding {
    ReloadStateBinding state{};
    // Update(__thiscall, context*, extra), forwarding thunk(context*, extra,
    // extra), special logic4(context*), preparation(), and the two soldier routes.
    std::array<ReloadCodeProof,8> code{}; // final two: snapshot restore/export
    std::array<ReloadTransferSite,5> transfers{};
    std::array<std::uint32_t,16> stepDispatchRvas{};
    std::uint32_t branch3cGetterRva=0,branch40GetterRva=0;
    static constexpr bool manualGateEnabled=false,nativeCallEnabled=false,authorityProven=false;
};
// Whole function proofs and explicit call links describe this inspected build.
// Addresses are observation boundaries only, never an authorization to invoke.
std::optional<ReloadFlowBinding> DiscoverReloadFlow(std::span<const std::byte>,const engine::PeImage&);
bool ValidateReloadFlowLive(const ReloadStateMemory&,const ReloadFlowBinding&,std::uint32_t imageBase)noexcept;
// Additional complete prelude callees required only by the opt-in dt=0 probe.
using ReloadHoldCodeProof=std::array<ReloadCodeProof,2>;
std::optional<ReloadHoldCodeProof> DiscoverReloadHoldCode(std::span<const std::byte>,const engine::PeImage&,const ReloadFlowBinding&);
bool ValidateReloadHoldCodeLive(const ReloadStateMemory&,const ReloadHoldCodeProof&,std::uint32_t imageBase,std::uint32_t imageSize)noexcept;

struct ReloadUpdateContext {
    float deltaSeconds=0,reloadTimeMultiplier=0;
    std::uint32_t rawWord1c=0,inputFlags=0; // +1c retained without a claimed clock/tick meaning
    // Retained raw labels: +24 gates notifications AND reload initiation, so it
    // must not be treated as an effects-only flag that can safely be toggled.
    std::array<bool,5> flags24Through28{};
    bool fireRequested=false,orderRequested=false,reloadRequested=false;
};
// Decode an already copied caller-owned context; no memory or native calls.
// Unknown flag bits, nonfinite/negative timing and malformed bools fail closed.
// Delta <=1s and multiplier <=1024 are conservative observer bounds, not
// asserted native simulation limits.
std::optional<ReloadUpdateContext> DecodeReloadUpdateContext(std::span<const std::byte>)noexcept;
// Native 0x40-byte firing snapshot, copied by value. Padding and packed flags
// remain raw; this projection only names fields proved by both export/restore.
struct ReloadFiringSnapshot {
    std::uint32_t current=0,next=0;
    float phaseTimer=0;
    std::int32_t loaded=0,reserve=0;
};
std::optional<ReloadFiringSnapshot> DecodeReloadFiringSnapshot(std::span<const std::byte>)noexcept;
struct ReloadTransferInvocation {
    ReloadTransferPath path=ReloadTransferPath::OrdinaryState12;
    std::uint8_t branch=0;
    std::uint32_t firing=0,wrapperOffset=0,returnRva=0;
    static constexpr bool authoritative=false,acknowledgement=false;
};
// Correlates provenance with an already owner-validated selected-item snapshot.
// Call only with a discovered/validated binding and a current owner-validated
// snapshot. Caller owns freshness and same-invocation sampling; this classifier
// does not establish temporal coherence from the two input values alone.
// It does not infer authoritative branch, native request identity, successful
// transfer, per-shell acknowledgement or readiness from a function entry.
std::optional<ReloadTransferInvocation> ClassifyReloadTransfer(const ReloadFlowBinding&,std::uint32_t imageBase,
    std::uint32_t returnAddress,std::uint32_t firing,const ReloadStateSnapshot&)noexcept;
}
