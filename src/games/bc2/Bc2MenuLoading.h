#pragma once
#include "Bc2MenuBinding.h"
#include <array>

namespace fvr::bc2 {
struct MenuLoadingCandidates {
    std::uint32_t managerGlobal=0,ownerVtable=0,eventVtable=0;
    bool operator==(const MenuLoadingCandidates&)const=default;
};
struct MenuLoadingCodeSpan {std::uint32_t rva=0,size=0;};
// Exact inspected x86 loader construction, retirement and transition evidence.
// The caller must also compare every returned span against the current loaded
// image before using a binding. Discovery alone grants no runtime authority.
std::optional<MenuLoadingCandidates> DiscoverMenuLoading(
    std::span<const std::byte>,const engine::PeImage&);
std::array<MenuLoadingCodeSpan,9> MenuLoadingCodeSpans() noexcept;

struct MenuLoadingSnapshot {
    std::uint32_t manager=0,owner=0;
    bool active=false;
    bool operator==(const MenuLoadingSnapshot&)const=default;
};
// Read-only presentation ownership; contains no input controller or action.
// A stable null loader is a valid inactive observation, not gameplay authority.
// A typed loader with no competing main/pause owner is active until native
// retirement clears its slot. Black loading frames are included. Neither
// c3efc=8, readiness events, world-pair arrival nor elapsed time is a predicate.
// Two complete snapshots each bracket manager and all three owner slots.
std::optional<MenuLoadingSnapshot> ReadMenuLoading(
    const MenuMemory&,const MenuLoadingCandidates&,std::uint32_t imageBase);
}
