#pragma once
#include "Bc2SelectedMeshes1p.h"
#include <algorithm>
#include <string_view>
#include <span>
namespace fvr::bc2 {
struct VisibilityDescriptor {
    std::string_view asset;
    std::span<const std::string_view> meshes,weightedNames;
    std::uint64_t rigFingerprint=0;
    bool nativeAdmitted=false;
    // Separate reviewed native receipts; neither geometry nor one successful
    // render pair grants input suppression. Defaults preserve every generated
    // descriptor's unadmitted state.
    std::string_view configurationPath;
    bool hideShowVerified=false,inputSuppressionVerified=false;
    // Generated after all LOD sections, known weapon subtree and arm influence
    // exclusion have been checked. This is data eligibility, not native proof.
    bool weightedSectionDataVerified=false;
};
inline constexpr std::uint64_t VisibilityNativeRig=0xa7f219a1426216abull;
inline bool VisibilityProductionAdmission(const SelectedMeshesSnapshot& s,
 const VisibilityDescriptor& d)noexcept {
    const bool legacyReviewed=d.nativeAdmitted&&d.hideShowVerified&&d.inputSuppressionVerified;
    const bool operationReviewed=d.weightedSectionDataVerified&&
        NativeOperationAdmitted(s.operationBinding,d.rigFingerprint,NativeOperationCapabilities);
    if((!legacyReviewed&&!operationReviewed)||d.rigFingerprint!=VisibilityNativeRig||d.configurationPath.empty()||
       d.configurationPath.find('\0')!=std::string_view::npos||!s.configurationPathVerified)return false;
    const auto end=std::find(s.configurationPath.begin(),s.configurationPath.end(),'\0');
    return end!=s.configurationPath.end()&&
        std::string_view(s.configurationPath.data(),std::size_t(end-s.configurationPath.begin()))==d.configurationPath;
}
// Every configured mesh must match a unique descriptor path. Unknown-kind
// assets are legitimate; a missing/extra attachment is never silently ignored.
inline bool VisibilityConfigurationMatches(const SelectedMeshesSnapshot& s,
    const ReloadStateOwner& o,const VisibilityDescriptor& d,std::int64_t now)noexcept {
    if(s.owner!=o||!s.sequence||s.observedNs<=0||s.observedNs>now||s.deadlineNs<=now||s.deadlineNs-s.observedNs>200000000||
       s.weaponData<0x10000||s.stateTypeInfo<0x10000||s.meshTypeInfo<0x10000||s.stateCount!=1||s.soleConfiguredArray<0x10000||
       s.states[0].array!=s.soleConfiguredArray||d.meshes.empty()||d.meshes.size()>8||s.states[0].count!=d.meshes.size()||
       d.weightedNames.empty()||d.weightedNames.size()>64||!d.rigFingerprint)return false;
    const auto end=std::find(s.weaponName.begin(),s.weaponName.end(),'\0');
    if(end==s.weaponName.end()||std::string_view(s.weaponName.data(),std::size_t(end-s.weaponName.begin()))!=d.asset)return false;
    for(std::size_t n=0;n<d.meshes.size();++n){
        if(d.meshes[n].empty()||std::find(d.meshes.begin(),d.meshes.begin()+n,d.meshes[n])!=d.meshes.begin()+n)return false;
        unsigned matches=0;
        for(unsigned j=0;j<s.states[0].count;++j){const auto& m=s.states[0].meshes[j];
            const auto e=std::find(m.assetPath.begin(),m.assetPath.end(),'\0');
            if(m.address<0x10000||m.typeInfo!=s.meshTypeInfo||m.namePointer<0x10000||e==m.assetPath.end())return false;
            if(std::string_view(m.assetPath.data(),std::size_t(e-m.assetPath.begin()))==d.meshes[n])++matches;
            for(unsigned k=0;k<j;++k)if(m.address==s.states[0].meshes[k].address)return false;
        }
        if(matches!=1)return false;
    }
    return true;
}
inline const VisibilityDescriptor* ResolveVisibilityDescriptor(const SelectedMeshesSnapshot& s,
 const ReloadStateOwner& o,std::span<const VisibilityDescriptor> rows,std::int64_t now,bool diagnostic=false)noexcept {
    const VisibilityDescriptor* result=nullptr;
    for(const auto& d:rows)if((diagnostic||VisibilityProductionAdmission(s,d))&&VisibilityConfigurationMatches(s,o,d,now)){
        if(result)return nullptr;result=&d;
    }
    return result;
}
}
