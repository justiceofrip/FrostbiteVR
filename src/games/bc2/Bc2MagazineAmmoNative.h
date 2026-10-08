#pragma once
#include "Bc2ReloadState.h"
#include <cstring>

namespace fvr::bc2 {
// Inspected BC2 build only. The actual signed helper and both rounding paths
// were executed in the isolated x86 tests before this diagnostic adapter.
// Code/table proof alone never authorizes a call or establishes server ownership.
struct MagazineAmmoNativeBinding {
    std::uint32_t function=0,primaryVtable=0,secondaryVtable=0;
};
inline std::optional<MagazineAmmoNativeBinding> ReadMagazineAmmoNativeBinding(
    const ReloadStateMemory& memory,std::uint32_t base,const ReloadStateBinding& firing)noexcept {
    if(!memory.read||base!=0x400000||firing.preferredBase!=base||firing.imageSize!=0x1913000||
       firing.firingVtableRva!=0x1025814)return {};
    const auto hash=[](std::span<const std::byte> bytes){std::uint64_t h=14695981039346656037ull;
        for(auto b:bytes){h^=std::to_integer<unsigned char>(b);h*=1099511628211ull;}return h;};
    std::array<std::byte,71> code{};std::array<std::byte,171> rounding{};
    if(!memory.read(memory.context,base+0x2d70b0,code.data(),code.size())||hash(code)!=0x26e6ecd88d49eeecull||
       !memory.read(memory.context,base+0x852450,rounding.data(),rounding.size())||hash(rounding)!=0xa8859302e477d3f5ull)return {};
    const auto secondary=base+firing.firingVtableRva-16;std::uint32_t slot=0;
    if(!memory.read(memory.context,secondary+12,&slot,4)||slot!=base+0x2d70b0)return {};
    return MagazineAmmoNativeBinding{slot,base+firing.firingVtableRva,secondary};
}
}
