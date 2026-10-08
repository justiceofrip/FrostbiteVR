#pragma once
#include "fvr/interaction/RigPose.h"
#include <array>
#include <optional>
#include <string>
#include <vector>
namespace fvr::bc2 {
struct RigMemory {
    void* context=nullptr;
    bool (*read)(void*,std::uint32_t,void*,std::size_t)=nullptr;
};
struct RigIdentity {
    std::uint32_t soldier=0,weak=0,animation=0,skeleton=0,pose=0,worldHeader=0,worldMatrices=0,skinMatrices=0,evaluatedMatrices=0,count=0;
    bool nativeIk=false;
    bool operator==(const RigIdentity&)const=default;
};
struct RigSnapshot {
    RigIdentity identity{};
    std::vector<std::string> names;
    std::vector<std::int32_t> parents;
    std::vector<math::Matrix4> world; // canonical LH, original native data untouched
    std::vector<std::array<std::byte,64>> nativeWorld,nativeEvaluated;
    std::vector<math::Matrix4> inverseBind,evaluatedWorld;
    float skinConsistencyError=0;
    interaction::ArmJoints left{},right{};
    std::uint32_t weaponBone=0;
    std::vector<std::uint32_t> nativeHiddenLeaves; // Verified collapsed native geometry, always copied unchanged.
};
// Read-only BC2 first-person rig snapshot. Caller establishes local soldier
// ownership and validates the native getter/pose producer before using this as
// a binding. No write capability is granted; animation synchronization is separate.
std::optional<RigSnapshot> ReadFirstPersonRig(const RigMemory&,std::uint32_t soldier,std::uint32_t weak);
struct RigBoneEdit {
    std::uint32_t index=0,address=0;
    std::array<std::byte,64> before{},after{};
};
struct RigPosePlan {
    RigIdentity identity{};
    std::vector<RigBoneEdit> edits;
};
// Generates candidate palette edits in a copy. A validated identity and exact
// bytes travel with the plan; this API neither writes nor authorizes writing.
// The game adapter must still establish the consumer/evaluation lifetime.
std::optional<RigPosePlan> BuildRigPosePlan(const RigSnapshot&,
    std::span<const interaction::BoneWrite>);
}
