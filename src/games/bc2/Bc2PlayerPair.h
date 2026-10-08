#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
namespace fvr::bc2 {
struct PlayerPairMemory {
 void* context=nullptr;
 bool (*read)(void*,std::uint32_t,void*,std::size_t)=nullptr;
};
// The caller verifies reflected actor/effects types, local client ownership and
// these discovered image bindings. This read-only check establishes the indexed
// server counterpart; it grants no native write capability.
std::optional<std::uint32_t> MatchServerPlayer(const PlayerPairMemory&,
 std::uint32_t serverContext,std::uint32_t managerVtable,
 std::uint32_t clientPlayer,std::uint32_t serverPlayer) noexcept;
std::optional<std::uint32_t> FindServerPlayer(const PlayerPairMemory&,
 std::uint32_t serverContext,std::uint32_t managerVtable,
 std::uint32_t clientPlayer) noexcept;
}
