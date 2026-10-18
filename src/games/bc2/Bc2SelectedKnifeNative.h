#pragma once
#include "Bc2SelectedKnifeNativeReader.h"
#include "Bc2SelectedKnifeMotion.h"
#include <ostream>

namespace fvr::bc2::selectedKnife {
// Independent measured melee listeners; never registers a second original
// firing observer or hooks the existing Step/Update/Compose dispatcher.
bool Install(std::span<const std::byte>,const engine::PeImage&,std::uintptr_t,
    const ReloadStateMemory&)noexcept;
void Start()noexcept;
bool Stop()noexcept;
// Gather thread: one shared SelectedMeleeAttack policy owns trigger and jab.
// handled=false preserves the existing verified action37 trigger fallback when
// the optional adapter was not installed. Once handled, fire is one intent only.
struct Request {bool handled=false,fire=false;std::uint64_t serial=0;};
Request Prepare(const SelectedKnifeRenderSource&,const interaction::InputFrame&,
    const interaction::HandInteraction&,const SelectedMeshesSnapshot*,
    const std::optional<SelectedKnifeRenderReceipt>&,const std::optional<math::Matrix4>& currentBody,
    bool actionsAvailable,std::uint64_t inputEpoch)noexcept;
// Call immediately after the SAME input-buffer transaction. A cache publication
// is not native completion. Acquisition and original attack return supply that.
void Published(const Request&,bool applied)noexcept;
// New continuous packets retain an already published intent. An actual source
// interruption revokes only further blade substitution, never native outcome.
void SourceAdvanced(const interaction::HandInteractionSample&,std::uint64_t inputEpoch)noexcept;
Request Missing()noexcept;
// Known current actor observation only; missing reads do not retire anything.
void ObserveOwner(const ReloadStateOwner&)noexcept;
void Report(std::ostream&);
}
