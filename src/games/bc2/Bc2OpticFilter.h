#pragma once
#include "Bc2OpticDiscovery.h"
#include "Bc2SelectedMeshes1p.h"
#include <vector>
namespace fvr::bc2 {
// Borrowed image bytes remain alive until Disable + Quiescent. No optics writes.
struct OpticFilterBinding {
    SelectedMeshesBinding image{};
    OpticObservationCandidates candidates{};
};
std::optional<OpticFilterBinding> DiscoverOpticFilter(std::span<const std::byte>,const engine::PeImage&);
bool ValidateOpticFilterLive(const ReloadStateMemory&,const OpticFilterBinding&,std::uint32_t base) noexcept;
enum class OpticFilterStatus:std::uint8_t {Observed,Disabled,Arguments,Expired,Caller,ReadFailure,Owner,Metadata,FilterType,Unmatched,Changed};
struct OpticFilterCall {std::uint32_t renderer=0,filter=0,caller=0;};
struct OpticFilterSample {
    ReloadStateOwner owner{};
    std::uint64_t sequence=0;
    std::int64_t observedNs=0,deadlineNs=0;
    OpticFilterCall call{};
    std::uint32_t filterTable=0,filterType=0;
    // Raw native resource-wrapper identities; these are NOT D3D interfaces.
    std::array<std::uint32_t,3> rendererWrappers{}; // +c8, +d8, +dc
    std::array<float,4> scissor{};
    std::array<float,2> blurCenter{};
    float blurScale=0;
    std::array<std::uint32_t,8> stateAddresses{},zoomedFilters{},nonZoomedFilters{},zoomMeshes{};
    std::uint8_t stateCount=0,zoomedMatchMask=0,nonZoomedMatchMask=0;
    std::array<char,128> assetName{};
    // Actual argument/configuration equality is evidence, not an ADS ack or
    // proof that a configured state is currently active. Shared filters remain
    // explicit in both masks; never guess an active state from their names.
    static constexpr bool activeStateVerified=false,nativeAdsAcknowledged=false,
        magnifiedSceneVerified=false,reticleVerified=false,renderAuthority=false;
};
struct OpticFilterRead {
    OpticFilterStatus status=OpticFilterStatus::Disabled;
    std::optional<OpticFilterSample> sample;
    std::uint32_t reads=0,bytes=0;
};
OpticFilterRead ReadOpticFilter(const ReloadStateMemory&,const OpticFilterBinding&,
    const SelectedMeshesSnapshot&,const ReloadStateOwner&,OpticFilterCall,std::int64_t nowNs,bool enabled=false) noexcept;
// Resource wrappers may legitimately change during rendering. Association
// requires identical owner, source lease, filter bytes and configured states.
bool SameOpticFilterOwner(const OpticFilterSample&,const OpticFilterSample&) noexcept;
}
