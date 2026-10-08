#pragma once
#include "fvr/engine/PeImage.h"
#include <array>
#include <optional>
namespace fvr::bc2 {
struct OpticTypeEvidence {
    std::uint32_t registration=0,typeInfo=0,metadata=0,fields=0,parent=0;
    std::uint16_t size=0;std::uint8_t fieldCount=0;
};
struct OpticObservationCandidates {
    // RVAs. This identifies a native lens-filter pass, NOT an offscreen scope
    // scene or reticle. No input, runtime hooks or graphics replacement enabled.
    std::uint32_t filterRenderer=0,filterCaller=0,filterReturn=0;
    OpticTypeEvidence zoomLevel{},aiming{},scopeFilter{},sniperFilter{};
    bool nativeAdsStateVerified=false,sceneTargetVerified=false,reticleVerified=false;
};
// Static executable evidence only. Runtime must separately verify loaded code,
// thread/view ownership, exact weapon-state/filter identity and resource lifetime.
std::optional<OpticObservationCandidates> DiscoverOpticObservation(
    std::span<const std::byte>,const engine::PeImage&);
}
