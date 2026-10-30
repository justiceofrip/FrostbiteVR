#pragma once
#include "Bc2BoltRuntimeGeometry.h"
#include "Bc2SelectedMeshes1p.h"
#ifdef BC2_BOLT_EQUIPMENT_HEADER
#include BC2_BOLT_EQUIPMENT_HEADER
#endif
namespace fvr::bc2 {
inline std::span<const Bc2BoltCalibration> EnrolledBoltEquipment()noexcept {
#ifdef BC2_BOLT_EQUIPMENT_HEADER
    return generated::BoltEquipment;
#else
    return {};
#endif
}
inline const Bc2BoltCalibration* FindBoltEquipment(std::string_view config,
    std::string_view asset,std::string_view mesh,std::uint64_t rig)noexcept {
    if(config.empty())return nullptr;
    const Bc2BoltCalibration* found=nullptr;
    for(const auto& row:EnrolledBoltEquipment())if(row.configurationPath==config&&row.asset==asset&&
        row.mesh==mesh&&row.rigFingerprint==rig&&BoltCalibrationValid(row)){
        if(found)return nullptr;found=&row;
    }
    return found;
}
inline bool BoltEquipmentSelected(const SelectedMeshesSnapshot& s,const ReloadStateOwner& owner,
    const Bc2BoltCalibration& e,std::int64_t now)noexcept {
    if(s.owner!=owner||!s.sequence||s.observedNs<=0||s.observedNs>now||s.deadlineNs<=now||
       s.deadlineNs-s.observedNs>250000000||s.stateCount!=1||!s.soleConfiguredArray||s.states[0].count>8||
       !s.configurationPathVerified||!ReloadDescriptorText(s.configurationPath,e.configurationPath)||
       !ReloadDescriptorText(s.weaponName,e.asset))return false;
    unsigned count=0;
    for(unsigned n=0;n<s.states[0].count;++n)count+=ReloadDescriptorText(s.states[0].meshes[n].assetPath,e.mesh);
    return count==1;
}
}
