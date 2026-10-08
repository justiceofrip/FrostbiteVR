#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>
namespace fvr::engine {
struct PatternByte {std::uint8_t value=0;bool wildcard=false;};
std::optional<std::vector<PatternByte>> ParsePattern(std::string_view);
// Search an explicitly bounded executable section, never the whole address space.
std::optional<std::size_t> UniqueMatch(std::span<const std::byte>,std::span<const PatternByte>) noexcept;
struct BindingEvidence {
    bool executableSection=false,uniqueSignature=false,abi=false,objectIdentity=false,relationships=false;
    bool Verified() const noexcept {return executableSection&&uniqueSignature&&abi&&objectIdentity&&relationships;}
};
struct FrostbiteEvidence {
    BindingEvidence camera,renderOnly,stateRestore,eyeTargets,localOwner,input,weapon,skeleton,ui,vehicle;
    bool coordinates=false,simulationOnce=false,gpuOwnership=false;
    std::uint64_t Capabilities() const noexcept;
};
}