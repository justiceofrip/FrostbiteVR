#pragma once
#include "fvr/math/StereoMath.h"
#include <cstdint>
#include <type_traits>
namespace fvr::bc2 {
// Private diagnostic observation only. Identity is compared, never dereferenced
// by the receiver. All actions still enter through ordinary controller packets.
struct ChestKnifeProbeWitness {
    std::uint64_t version=2,pid=0,creation=0,tickMs=0;
    std::uint64_t observedNs=0,deadlineNs=0,input=0,space=0;
    std::uint64_t actor=0,actorGeneration=0,weapon=0,bodyOperation=0;
    std::uint64_t draws=0,returns=0,phase=0;
    std::uint64_t empty=0,held=0,attackReady=0,nativeOwned=0;
    std::uint64_t recognizerReady=0;
    math::Matrix4 wristInGrip{};
};
static_assert(std::is_trivially_copyable_v<ChestKnifeProbeWitness>);
static_assert(sizeof(ChestKnifeProbeWitness)==224);
}
