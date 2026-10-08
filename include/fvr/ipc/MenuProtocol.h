#pragma once
#include "fvr/graphics/SharedTextureProtocol.h"
#include <cmath>
namespace fvr::ipc {
// Independent menu lane: an opaque native UI raster is never a world-eye pair.
enum class MenuMode:std::uint32_t {Unknown,Gameplay,Menu};
struct alignas(8) MenuState {
    std::uint64_t sequence=0,epoch=0;
    std::int64_t deadlineQpc=0;
    MenuMode mode=MenuMode::Unknown;
    std::uint32_t logicalWidth=0,logicalHeight=0,inputReady=0;
    std::uint64_t reserved=0;
};
enum MenuControlFlags:std::uint32_t {MenuFocused=1,MenuPoint=2,MenuDown=4};
struct alignas(8) MenuControl {
    std::uint64_t sequence=0,space=0,menuEpoch=0;
    std::int64_t deadlineQpc=0;
    // Monotonic edge IDs; unchanged IDs cannot repeat a native command.
    std::uint64_t toggle=0,cancel=0;
    float u=0,v=0;
    std::uint32_t flags=0,reserved=0;
};
struct MenuSurface {MenuState state{};graphics::TextureDescriptor descriptor{};graphics::PairTicket ticket{};};
static_assert(sizeof(MenuState)==48&&sizeof(MenuControl)==64&&sizeof(MenuSurface)==208);
inline bool ValidMenuState(const MenuState& s)noexcept {
    return s.sequence&&s.epoch&&s.deadlineQpc>0&&std::uint32_t(s.mode)<=2&&s.inputReady<=1&&!s.reserved&&
        (s.mode!=MenuMode::Menu||(s.logicalWidth&&s.logicalWidth<=16384&&s.logicalHeight&&s.logicalHeight<=16384));
}
inline bool ValidMenuControl(const MenuControl& c)noexcept {
    return c.sequence&&c.space&&c.deadlineQpc>0&&!c.reserved&&!(c.flags&~7u)&&std::isfinite(c.u)&&std::isfinite(c.v)&&
        (!(c.flags&MenuPoint)||((c.flags&MenuFocused)&&c.menuEpoch&&c.u>=0&&c.u<=1&&c.v>=0&&c.v<=1))&&
        (!(c.flags&MenuDown)||(c.flags&MenuPoint));
}
inline bool ValidMenuSurface(const MenuSurface& s)noexcept {
    return ValidMenuState(s.state)&&s.state.mode==MenuMode::Menu&&graphics::Valid(s.ticket,s.descriptor);
}
}
