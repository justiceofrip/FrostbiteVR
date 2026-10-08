#pragma once
#include "fvr/engine/PeImage.h"
#include <array>
#include <cstdint>
#include <optional>
namespace fvr::bc2 {
// Only BC2 APT bindings: no OS cursor and no native queue/source writes.
struct MenuBindingCandidates {
    std::uint32_t mousePump=0,setCursor=0,sendEvent=0,managerGlobal=0;
    std::uint32_t inputEnabled=0,inputBlocked=0,targetGlobal=0;
    unsigned logicalWidth=0,logicalHeight=0;
    std::uint32_t mousePumpCaller=0,mousePumpReturn=0,inputNodeGlobal=0;
    bool operator==(const MenuBindingCandidates&)const=default;
};
std::optional<MenuBindingCandidates> DiscoverMenuBinding(std::span<const std::byte>,const engine::PeImage&);
struct MenuBindingCodeSpan {std::uint32_t rva=0,size=0;};
// Complete functions relied on for dispatch, derived from the verified call
// graph. Compare these bytes against the loaded image before installing hooks.
std::optional<std::array<MenuBindingCodeSpan,11>> MenuBindingCodeSpans(
    std::span<const std::byte>,const engine::PeImage&,const MenuBindingCandidates&);

struct MenuMemory {
    void* context=nullptr;
    bool (*read)(void*,std::uint32_t,void*,std::size_t)=nullptr;
};
// The native task may migrate between worker threads. Verify the original
// callsite, exact active controller, and the original InputNode argument;
// serialize adapter state separately. Never use the first callback thread ID
// as lifetime ownership. Requires a discovered/live-validated binding.
bool IsMenuPumpBoundary(const MenuMemory&,const MenuBindingCandidates&,std::uint32_t imageBase,
    std::uint64_t returnAddress,std::uint64_t self,std::uint64_t inputNode,std::uint64_t expectedController);
struct MenuTargetSnapshot {
    std::uint32_t manager=0,ui=0,target=0,queue=0,buffer=0,count=0,capacity=0;
    bool operator==(const MenuTargetSnapshot&)const=default;
};
// Input-ready is NOT menu-open: the same target also serves the HUD. A separate
// verified menu-state binding must authorize every command and its menu epoch.
std::optional<MenuTargetSnapshot> ReadMenuTarget(const MenuMemory&,const MenuBindingCandidates&,std::uint32_t imageBase);
enum class MenuPointerKind : std::uint8_t {Move,PrimaryDown,PrimaryUp};
struct MenuPointerCommand {
    std::uint64_t generation=0,menuEpoch=0;
    std::int64_t deadlineNs=0;
    std::int32_t x=0,y=0;
    MenuPointerKind kind=MenuPointerKind::Move;
};
std::optional<MenuPointerCommand> MakeMenuPointerCommand(const MenuBindingCandidates&,
    float logicalU,float logicalV,MenuPointerKind,std::uint64_t generation,std::uint64_t menuEpoch,std::int64_t deadlineNs) noexcept;
struct MenuDispatchGuard {
    std::uint64_t menuEpoch=0;
    std::int64_t nowNs=0;
    bool nativeUiThread=false,menuOwned=false,focused=false,tracked=false;
};
struct MenuNativeCalls {
    void* context=nullptr;
    // Addresses are validated image-base-relative wrappers; manager is this.
    // A true result means invoked, NOT that APT activated a menu item.
    bool (*cursor)(void*,std::uint32_t function,std::uint32_t manager,std::int32_t x,std::int32_t y)=nullptr;
    bool (*event)(void*,std::uint32_t function,std::uint32_t manager,const std::array<std::uint32_t,3>&)=nullptr;
};
enum class MenuDispatchResult : std::uint8_t {Rejected,TargetChanged,QueueFull,CallFailed,Invoked};
// Minted only by a successful down. Non-copyable so a caller cannot replay a
// copied release capability. Forget only after the original UI/session retires.
class MenuOwnedPress {
public:
    MenuOwnedPress()=default;
    MenuOwnedPress(const MenuOwnedPress&)=delete;
    MenuOwnedPress& operator=(const MenuOwnedPress&)=delete;
    bool Active()const noexcept{return generation_!=0;}
    std::uint64_t Epoch()const noexcept{return epoch_;}
    std::uint64_t Generation()const noexcept{return generation_;}
    const MenuTargetSnapshot& Target()const noexcept{return target_;}
    void Forget()noexcept{generation_=epoch_=0;}
private:
    MenuTargetSnapshot target_{};
    MenuBindingCandidates binding_{};
    std::uint32_t base_=0;
    std::uint64_t epoch_=0,generation_=0;
    friend MenuDispatchResult DispatchMenuPointer(const MenuMemory&,const MenuBindingCandidates&,std::uint32_t,
        const MenuTargetSnapshot&,const MenuPointerCommand&,const MenuDispatchGuard&,const MenuNativeCalls&,MenuOwnedPress&);
    friend MenuDispatchResult ReleaseMenuPointer(const MenuMemory&,const MenuBindingCandidates&,std::uint32_t,
        const MenuTargetSnapshot&,std::uint64_t,const MenuDispatchGuard&,const MenuNativeCalls&,MenuOwnedPress&);
};
// Call only from the original native UI pump thread. Revalidates target before
// cursor, and again before button dispatch. Never calls arbitrary event types.
// The interaction/channel owner supplies once-only edges and neutral rearming.
MenuDispatchResult DispatchMenuPointer(const MenuMemory&,const MenuBindingCandidates&,std::uint32_t imageBase,
    const MenuTargetSnapshot&,const MenuPointerCommand&,const MenuDispatchGuard&,const MenuNativeCalls&,MenuOwnedPress&);
// Cleanup only: originalEpoch is the epoch of the successful down, even if the
// current menu has closed. Current focus/tracking/menu-owned are irrelevant.
// No cursor call or new down. Retains ownership on failure for a bounded caller
// retry; a replacement target cannot receive this up. A successful up consumes it.
MenuDispatchResult ReleaseMenuPointer(const MenuMemory&,const MenuBindingCandidates&,std::uint32_t imageBase,
    const MenuTargetSnapshot& originalTarget,std::uint64_t originalEpoch,const MenuDispatchGuard&,const MenuNativeCalls&,MenuOwnedPress&);
}
