#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace fvr::bc2 {
// Copied presentation identity only. This never grants equipment/input authority
// or writes the original animation palette.
struct RigPaletteCopyOwner {
    std::uint32_t soldier=0,weak=0,weapon=0;
    std::uint64_t actorGeneration=0,equipmentGeneration=0,space=0;
    bool operator==(const RigPaletteCopyOwner&)const=default;
    explicit operator bool()const noexcept {
        return soldier&&weak&&weapon&&actorGeneration&&equipmentGeneration&&space;
    }
};
struct RigPaletteCopyState {
    RigPaletteCopyOwner owner{};
    std::uint64_t input=0;
    std::int64_t deadline=0;
    bool valid=false;
};
struct RigPaletteCopyObservation {
    RigPaletteCopyState state{};
    std::int64_t now=0;
};
using RigPaletteBone=std::array<std::byte,64>;
inline bool RigPaletteCopyFresh(const RigPaletteCopyState& captured,
    const RigPaletteCopyObservation& current)noexcept {
    return captured.valid&&current.state.valid&&captured.owner&&
        current.state.owner==captured.owner&&captured.input&&
        current.state.input>=captured.input&&current.now>0&&
        current.now<captured.deadline&&current.now<current.state.deadline;
}
// Recheck at EACH original native pack call. A pose admitted by the first copy
// is not a lease to copy it again after a recenter, actor change, expired input or
// producer mutation. Read failures forward the native source. The caller still
// owns native callback/selected-equipment admission and final receipts.
template<class Read,class Observe>
bool RigPaletteCurrentAtCopy(const RigPaletteCopyState& captured,
    std::uintptr_t expectedSource,std::uintptr_t source,unsigned count,
    std::span<const RigPaletteBone> before,std::span<const RigPaletteBone> posed,
    std::vector<RigPaletteBone>& scratch,Read&& read,Observe&& observe) {
    if(!expectedSource||source!=expectedSource||count<6||count>1024||
       before.size()!=count||posed.size()!=count)return false;
    const auto first=observe();if(!RigPaletteCopyFresh(captured,first))return false;
    scratch.resize(count); // The native scope reuses capacity across both copies.
    if(!read(source,scratch.data(),scratch.size()*sizeof(RigPaletteBone))||
       !std::equal(scratch.begin(),scratch.end(),before.begin()))return false;
    const auto last=observe();
    return RigPaletteCopyFresh(captured,last)&&last.now>=first.now&&
        last.state.input>=first.state.input;
}
}
