#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <string_view>
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
inline constexpr std::string_view NativeOperationAlgorithm="bc2-weighted-palette12-stride64-gather-actions-v1";
#ifdef FVR_BC2_NATIVE_OPERATION_BUILD_SHA256
inline constexpr std::string_view NativeOperationBuild=FVR_BC2_NATIVE_OPERATION_BUILD_SHA256;
#else
inline constexpr std::string_view NativeOperationBuild{};
#endif
struct NativeOperationCapability {
    NativeOperationBinding binding;
    std::uint64_t rigFingerprint=0;
    std::string_view algorithm,sourceSha256,executableSha256,hideShowReceiptSha256,inputSuppressionReceiptSha256;
    bool reviewed=false,hideShowVerified=false,inputSuppressionVerified=false;
};
inline bool OperationDigest(std::string_view text)noexcept{
    if(text.size()!=64)return false;
    for(char c:text)if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')))return false;
    return true;
}
inline bool CompleteOperationBinding(const NativeOperationBinding& b)noexcept{
    if(!b.executableFingerprint||!b.executableBytes||b.imageBase!=0x400000||!b.imageBytes)return false;
    for(unsigned n=0;n<4;++n)if(!b.codeRva[n]||!b.codeBytes[n]||!b.codeFingerprint[n]||
        std::uint64_t(b.codeRva[n])+b.codeBytes[n]>b.imageBytes)return false;
    return true;
}
inline bool NativeOperationAdmitted(const NativeOperationBinding& binding,std::uint64_t rig,
    std::span<const NativeOperationCapability> records,std::string_view build=NativeOperationBuild)noexcept{
    if(!CompleteOperationBinding(binding)||!rig||!OperationDigest(build))return false;
    unsigned matches=0;
    for(const auto& r:records){
        if(r.binding!=binding||r.rigFingerprint!=rig||r.algorithm!=NativeOperationAlgorithm||r.sourceSha256!=build)continue;
        // Duplicate contradictory receipts cannot be resolved by array order.
        if(!r.reviewed||!r.hideShowVerified||!r.inputSuppressionVerified||!OperationDigest(r.executableSha256)||
           !OperationDigest(r.hideShowReceiptSha256)||!OperationDigest(r.inputSuppressionReceiptSha256))return false;
        ++matches;
    }
    return matches==1;
}
}
#ifdef FVR_BC2_NATIVE_OPERATION_CAPABILITIES_HEADER
#include FVR_BC2_NATIVE_OPERATION_CAPABILITIES_HEADER
#else
namespace fvr::bc2 {inline constexpr std::array<NativeOperationCapability,0> NativeOperationCapabilities{};}
#endif
