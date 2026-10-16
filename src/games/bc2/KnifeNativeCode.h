#pragma once
#include "Bc2WeaponMode.h"
#include "Bc2ReloadState.h"
#include <array>
namespace fvr::bc2::private_knife {
// Exact native316 chooser/deploy route. This is a code-admission prerequisite,
// not installation, callable ABI, or authority to change a selector or datum.
inline constexpr std::array<ReloadCodeProof,4> DrawReturnCode{{
    {0x2ef590,433,0x0071c4bf9465827eull}, // whole original chooser
    {0x2f1980,134,0xf2340f29464f47f4ull}, // whole owning inventory update
    {0x2d6eb0,42,0x86f6fd3bcb5db20eull}, // whole ready query
    {0x2e6d95,28,0x04b779a7e7e3d3eeull} // inspected caller span, not whole function
}};
inline bool VerifyDrawReturnCode(const ReloadStateMemory& m,unsigned base,unsigned imageSize,
    const WeaponModeCandidates& discovered)noexcept {
    if(base!=0x400000||imageSize!=0x1913000||discovered.selector!=DrawReturnCode[0].rva||
       discovered.update!=DrawReturnCode[1].rva||!m.read)return false;
    std::array<std::byte,433> bytes{};
    for(const auto& proof:DrawReturnCode){
        if(std::uint64_t(proof.rva)+proof.size>imageSize||
           !m.read(m.context,base+proof.rva,bytes.data(),proof.size))return false;
        std::uint64_t hash=14695981039346656037ull;
        for(unsigned n=0;n<proof.size;++n){hash^=std::to_integer<unsigned char>(bytes[n]);hash*=1099511628211ull;}
        if(hash!=proof.fingerprint)return false;
    }
    return true;
}
// Native318 exact setter and automatic-return spans. Client span begins
// inside its owning function; it is a byte proof, not a guessed function entry.
// The final three proofs bind the direct prediction-copy loop and both
// object getters. ReloadFlow also verifies the complete export/restore bodies.
inline constexpr std::array<ReloadCodeProof,6> AttackCode{{
    {0x2d7c80,88,0xffadda0765a41020ull},
    {0x29f470,151,0x5af3a3d76e9eee6cull},
    {0x4827b0,198,0xab04e42ef7a5f40full},
    {0x492127,87,0x60e98c5566d042e5ull},
    {0x3efce0,4,0x1db3a6f6ffc9c09cull},
    {0x3efcf0,4,0x1c797ef6febd9160ull},
}};
inline bool VerifyAttackCode(const ReloadStateMemory& memory,unsigned base,unsigned imageSize)noexcept {
    if(base!=0x400000||imageSize!=0x1913000||!memory.read)return false;
    std::array<std::byte,198> bytes{};
    for(const auto& proof:AttackCode){
        if(std::uint64_t(proof.rva)+proof.size>imageSize||!memory.read(memory.context,base+proof.rva,bytes.data(),proof.size))return false;
        std::uint64_t hash=14695981039346656037ull;
        for(unsigned n=0;n<proof.size;++n){hash^=std::to_integer<unsigned char>(bytes[n]);hash*=1099511628211ull;}
        if(hash!=proof.fingerprint)return false;
    }return true;
}
// Only these original caller returns are evidenced by316. The owning update
// retains all eligibility, setter, deploy, and replication work.
inline constexpr unsigned ChooseCallerRva=0x2f19d2;
inline constexpr unsigned DeployReadyCallerRva=0x2e6da5;
}
