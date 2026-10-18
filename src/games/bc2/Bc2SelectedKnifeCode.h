#pragma once
#include "Bc2ReloadState.h"
namespace fvr::bc2::selectedKnife {
//418 reviewed executable3911fcc8..., plus426's checked retry/receiver epilogue.
// No whole-image hash substitutes for the ABI/caller/owner checks at use.
inline constexpr std::array<ReloadCodeProof,12> Code{{
    {0x150d90,115,0x0cbf1f727ab5075full},
    {0x17abd0,999,0xc56443adb82fd943ull},
    {0x178500,287,0xd73f53048332aa13ull},
    {0x177790,1445,0x5f653a5787a78517ull},
    {0x177d40,318,0xace2da8fac53a4f8ull},
    {0x2dd9d2,19,0x943ee7f6e8c88d10ull},
    {0x2dd768,8,0x64e34cd680f4c2a6ull},
    {0x2c4c00,2021,0x285025669572311aull},
    {0x2c40ab,20,0x4fc7c4343416a17dull},
    {0x17aef0,37,0xcd3ca4dcd2ba56e0ull},
    {0x210fa0,4,0xdb99bd8143c149d2ull},
    {0x210fb0,14,0xb8ee7a81813e9809ull}
}};
inline bool VerifyCode(const ReloadStateMemory& memory,unsigned base,unsigned size)noexcept {
    if(base!=0x400000||size!=0x1913000||!memory.read)return false;
    std::array<std::byte,2021> bytes{};
    for(const auto& p:Code){if(std::uint64_t(p.rva)+p.size>size||!memory.read(memory.context,base+p.rva,bytes.data(),p.size))return false;
        std::uint64_t hash=14695981039346656037ull;
        for(unsigned n=0;n<p.size;++n){hash^=std::to_integer<unsigned char>(bytes[n]);hash*=1099511628211ull;}
        if(hash!=p.fingerprint)return false;}
    for(const auto pair:{std::pair{0x1417cd8u,0x533d20u},std::pair{0x1417c70u,0x578500u},
        std::pair{0x1417c7cu,0x57abd0u},std::pair{0x1424858u,0x68d8c0u},std::pair{0x1424920u,0x6c4c00u},
        std::pair{0x142482cu,0x610fa0u}}){unsigned value=0;
        if(!memory.read(memory.context,pair.first,&value,4)||value!=pair.second)return false;}
    return true;
}
}
