#pragma once
#include "Bc2NativeOperationBinding.h"
#include <array>
#include <cstdint>
#include <span>
#include <string_view>
#if __has_include(<Bc2NativeOperationBuild.generated.h>)
#if defined(FVR_BC2_NATIVE_OPERATION_CAPABILITIES_HEADER) || defined(FVR_BC2_NATIVE_OPERATION_BUILD_SHA256)
#error "Generated operation configuration cannot be combined with legacy command-line definitions"
#endif
#include <Bc2NativeOperationBuild.generated.h>
#endif
namespace fvr::bc2 {
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
inline constexpr bool OperationDigest(std::string_view text)noexcept{
    if(text.size()!=64)return false;
    for(char c:text)if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')))return false;
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
// Compiled receipts never change during the module lifetime. Validate their
// strings and review flags at compile time; every admission still compares the
// complete live binding and rig. Keep inapplicable and invalid matching rows
// distinct so contradictory/duplicate receipts have the generic policy's result.
template<std::size_t N> class NativeOperationAdmissionTable {
    struct Entry {
        NativeOperationBinding binding;
        std::uint64_t rig=0;
        bool applicable=false,approved=false;
    };
    std::array<Entry,N> entries_{};
    bool buildValid_=false;
public:
    consteval NativeOperationAdmissionTable(const std::array<NativeOperationCapability,N>& records,
        std::string_view build):buildValid_(OperationDigest(build)){
        for(std::size_t n=0;n<N;++n){const auto& r=records[n];
            entries_[n]={r.binding,r.rigFingerprint,
                r.algorithm==NativeOperationAlgorithm&&r.sourceSha256==build,
                r.reviewed&&r.hideShowVerified&&r.inputSuppressionVerified&&OperationDigest(r.executableSha256)&&
                    OperationDigest(r.hideShowReceiptSha256)&&OperationDigest(r.inputSuppressionReceiptSha256)};
        }
    }
    constexpr bool Admitted(const NativeOperationBinding& binding,std::uint64_t rig)const noexcept{
        if(!buildValid_||!CompleteOperationBinding(binding)||!rig)return false;
        unsigned matches=0;
        for(const auto& r:entries_){
            if(!r.applicable||r.binding!=binding||r.rig!=rig)continue;
            if(!r.approved)return false;
            ++matches;
        }
        return matches==1;
    }
};
}
#ifdef FVR_BC2_NATIVE_OPERATION_CAPABILITIES_HEADER
#include FVR_BC2_NATIVE_OPERATION_CAPABILITIES_HEADER
#else
namespace fvr::bc2 {inline constexpr std::array<NativeOperationCapability,0> NativeOperationCapabilities{};}
#endif
namespace fvr::bc2 {
inline constexpr NativeOperationAdmissionTable CompiledNativeOperationTable{NativeOperationCapabilities,NativeOperationBuild};
inline bool CompiledNativeOperationAdmitted(const NativeOperationBinding& binding,std::uint64_t rig)noexcept{
    return CompiledNativeOperationTable.Admitted(binding,rig);
}
}
