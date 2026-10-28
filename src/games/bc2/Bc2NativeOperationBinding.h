#pragma once
#include <array>
#include <cstdint>

namespace fvr::bc2 {
// Runtime content discriminator, not a cryptographic binary attestation. Review
// receipts must additionally pin the executable SHA256 and compiled source set.
struct NativeOperationBinding {
    std::uint64_t executableFingerprint=0,executableBytes=0;
    std::uint32_t imageBase=0,imageBytes=0;
    std::array<std::uint32_t,4> codeRva{},codeBytes{};
    std::array<std::uint64_t,4> codeFingerprint{};
    bool operator==(const NativeOperationBinding&)const=default;
};
inline constexpr bool CompleteOperationBinding(const NativeOperationBinding& b)noexcept{
    if(!b.executableFingerprint||!b.executableBytes||b.imageBase!=0x400000||!b.imageBytes)return false;
    for(unsigned n=0;n<4;++n)if(!b.codeRva[n]||!b.codeBytes[n]||!b.codeFingerprint[n]||
        std::uint64_t(b.codeRva[n])+b.codeBytes[n]>b.imageBytes)return false;
    return true;
}
}
