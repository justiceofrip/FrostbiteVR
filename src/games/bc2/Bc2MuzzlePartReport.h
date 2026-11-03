#pragma once
#include "Bc2MuzzlePartObservation.h"
#include <limits>
#include <ostream>

namespace fvr::bc2::muzzle_part_observation {
// Worker/post-drain only. No callbacks invoke this serializer. Numeric addresses
// identify source snapshots; they are never dereferenced by this report.
inline void Report(std::ostream& out,const std::optional<Frame>& value) {
    if(!value||!value->profile){out<<"null";return;}
    const auto& f=*value;const auto& p=*f.profile;const auto& r=f.rig;
    const auto precision=out.precision();out.precision(std::numeric_limits<float>::max_digits10);
    const auto text=[&](std::string_view s){out<<'"';for(const unsigned char c:s){
        if(c=='"'||c=='\\')out<<'\\'<<char(c);
        else if(c<32){constexpr char hex[]="0123456789abcdef";out<<"\\u00"<<hex[c>>4]<<hex[c&15];}
        else out<<char(c);}out<<'"';};
    const auto vector=[&](const math::Vec3& v){out<<'['<<v.x<<','<<v.y<<','<<v.z<<']';};
    const auto matrix=[&](const math::Matrix4& m){out<<'[';for(unsigned n=0;n<16;++n){if(n)out<<',';out<<m.values[n/4][n%4];}out<<']';};
    out<<"{\"source_scope\":\"committed immutable shot publication\",\"native_write_authority\":false,\"render_submission_verified\":false"
       <<",\"active_state_verified\":false,\"matrix_convention\":\"row_vector_LH_world_units\",\"profile_tuple_sha256\":";text(p.tupleSha256);
    out<<",\"configuration\":";text(p.configuration);out<<",\"asset\":";text(p.asset);out<<",\"mesh\":";text(p.mesh);
    out<<",\"part\":";text(p.part);out<<",\"parent\":";text(p.parent);
    out<<",\"rig_fingerprint\":"<<p.rigFingerprint<<",\"expected_index\":"<<p.expectedIndex<<",\"bone\":"<<f.bone
       <<",\"configured_state_index\":"<<p.state<<",\"local_exit_metres\":";vector(p.localExitMeters);
    out<<",\"local_outward\":";vector(p.localOutward);
    out<<",\"rig\":{\"soldier\":"<<r.soldier<<",\"weak\":"<<r.weak<<",\"animation\":"<<r.animation<<",\"skeleton\":"<<r.skeleton
       <<",\"pose\":"<<r.pose<<",\"world_header\":"<<r.worldHeader<<",\"world_matrices\":"<<r.worldMatrices
       <<",\"skin_matrices\":"<<r.skinMatrices<<",\"evaluated_matrices\":"<<r.evaluatedMatrices<<",\"count\":"<<r.count
       <<",\"native_ik\":"<<(r.nativeIk?"true":"false")<<'}';
    out<<",\"weapon\":"<<f.equipment.weapon<<",\"weapon_data\":"<<f.equipment.data<<",\"persistence\":"<<f.equipment.persistence
       <<",\"equipment_asset\":";text(f.equipment.Asset());
    out<<",\"input_generation\":"<<f.generation<<",\"owner_generation\":"<<f.ownerGeneration<<",\"equipment_generation\":"<<f.equipmentGeneration
       <<",\"space\":"<<f.space<<",\"input_deadline_qpc\":"<<f.deadlineQpc<<",\"configuration_sequence\":"<<f.configurationSequence
       <<",\"configuration_deadline_ns\":"<<f.configurationDeadlineNs<<",\"units_per_metre\":"<<f.unitsPerMetre;
    out<<",\"native_part\":";matrix(f.nativePart);out<<",\"tracked_part\":";matrix(f.trackedPart);
    out<<",\"native_weapon\":";matrix(f.nativeWeapon);out<<",\"tracked_weapon\":";matrix(f.trackedWeapon);
    out<<'}';out.precision(precision);
}
} // namespace fvr::bc2::muzzle_part_observation
