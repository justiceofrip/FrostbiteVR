#pragma once
#include "fvr/interaction/ShellReloadAudio.h"
#include "Bc2ShotgunAudioProfile.h"
#include <array>
#include <filesystem>
#include <iosfwd>
#include <string_view>
namespace fvr::bc2::shellReloadAudio {
struct Source {
    unsigned soldier=0,weak=0,weapon=0;
    interaction::ShellReloadAudioIdentity identity{};
    std::int64_t observedNs=0,deadlineNs=0;
    bool manual=false,focused=false,tracked=false,spas=false;
    const ShotgunAudioProfile* profile=nullptr;
};
struct Config {bool replace=false;std::filesystem::path insertionWave;};
bool Install(const Config& config={});
// Called only by the already verified animation-sound callback. True means
// suppress this exact local1P start; all other sounds call their original.
bool Dispatch(bool start,std::uintptr_t receiver,const std::array<unsigned char,16>& guid)noexcept;
void Publish(const Source&);
bool Emit(const interaction::ShellReloadAudioEvent&);
void RetireActor(std::uint64_t actor,std::uint64_t generation);
void Stop()noexcept;
bool Drain()noexcept;
void Report(std::ostream&);
}
