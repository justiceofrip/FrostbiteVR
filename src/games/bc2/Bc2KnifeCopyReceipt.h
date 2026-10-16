#pragma once
#include "Bc2OriginalFiringObserver.h"
namespace fvr::bc2::private_knife {
inline std::uint64_t KnifeContextHash(const std::array<std::byte,0x30>& bytes)noexcept {
    std::uint64_t hash=14695981039346656037ull;
    for(auto b:bytes){hash^=std::to_integer<unsigned char>(b);hash*=1099511628211ull;}return hash;
}
// The low halves retain the original knife ammo sentinel. The exact call owns
// these high halves even if a coherent actor replacement retires shared state.
inline void SetKnifeContextHash(OriginalFiringToken& token,std::uint64_t hash)noexcept {
    token.words[4]=(token.words[4]&0xffffffffull)|((hash&0xffffffffull)<<32);
    token.words[5]=(token.words[5]&0xffffffffull)|(hash&0xffffffff00000000ull);
}
inline std::uint64_t KnifeContextHash(const OriginalFiringToken& token)noexcept {
    return (token.words[4]>>32)|(token.words[5]&0xffffffff00000000ull);
}
}
