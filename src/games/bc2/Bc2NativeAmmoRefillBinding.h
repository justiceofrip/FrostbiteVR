#pragma once
#include "Bc2ReloadState.h"
#include <cstring>
namespace fvr::bc2 {
// Verify the exact already-installed x86 Transfer hook AND its original
// trampoline before a private direct call. Reconstruct only the five bytes
// this owned hook displaced; reject third-party redirects or changed bodies.
inline bool NativeAmmoRefillTrampolineVerified(const ReloadStateMemory& m,std::uint32_t base,
    const ReloadStateBinding& b,std::uint32_t callback,std::uint32_t trampoline)noexcept {
    const auto& proof=b.code[4];
    if(!m.read||base!=0x400000||b.imageSize!=0x1913000||proof.rva!=0x2d6f40||proof.size!=254||
       proof.fingerprint!=0xd83485215d90a16aull||callback<0x10000||trampoline<0x10000||trampoline>UINT32_MAX-10)return false;
    std::array<std::byte,254> body{};std::array<std::byte,10> original{};
    if(!m.read(m.context,base+proof.rva,body.data(),body.size())||
       !m.read(m.context,trampoline,original.data(),original.size()))return false;
    const auto destination=[](const std::byte* code,std::uint32_t address){
        std::int32_t offset=0;std::memcpy(&offset,code+1,4);return address+5+std::uint32_t(offset);
    };
    constexpr std::array<std::byte,5> prefix{std::byte{0x51},std::byte{0x55},std::byte{0x56},std::byte{0x8b},std::byte{0xf1}};
    if(body[0]!=std::byte{0xe9}||destination(body.data(),base+proof.rva)!=callback||
       original[5]!=std::byte{0xe9}||destination(original.data()+5,trampoline+5)!=base+proof.rva+5)return false;
    for(unsigned n=0;n<5;++n){if(original[n]!=prefix[n])return false;body[n]=original[n];}
    std::uint64_t hash=14695981039346656037ull;
    for(const auto v:body){hash^=std::to_integer<unsigned char>(v);hash*=1099511628211ull;}
    return hash==proof.fingerprint;
}
}
