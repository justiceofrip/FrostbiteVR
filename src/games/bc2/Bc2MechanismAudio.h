#pragma once
#include "fvr/interaction/MechanismAudio.h"
#include <cstdint>
#include <filesystem>
#include <iosfwd>

namespace fvr::bc2::mechanismAudio {
enum class Cue : unsigned { None, Spas, M95 };
struct Source {
    unsigned soldier=0,weak=0,weapon=0;
    interaction::MechanismAudioIdentity identity{};
    Cue cue=Cue::None;
    std::int64_t observedNs=0,deadlineNs=0;
    bool manual=false,focused=false,tracked=false;
};
struct Config {
    // Observe first. Enabling replacement requires the recorded local-owner,
    // animation-GUID and first-person callback witness for this executable.
    bool replaceSpas=false;
    std::filesystem::path rearWave,closureWave;
};
bool Install(std::uintptr_t image,const Config& config={});
void Publish(const Source& source);
// Repeated publication of the same accepted physical stage is harmless.
// False means no sound was queued; it never changes gameplay success/failure.
bool Emit(const interaction::MechanismAudioEvent& event);
void RetireActor(std::uint64_t actor,std::uint64_t generation);
bool Stop() noexcept;
bool DrainAndRemove(unsigned timeoutMs=3000) noexcept;
void Report(std::ostream& out);
}
