#pragma once
#include "fvr/interaction/TrackingMath.h"
#include "Bc2Rig.h"
#include <array>
#include <string>
#include <vector>
#include <ostream>
namespace fvr::bc2 {
// Capture-only observations. This data never grants a runtime write capability.
struct WeaponBoneCapture {
    std::string name,parentName;
    math::Matrix4 native{};
    bool hidden=false; // Adapter-validated collapsed geometry; never an attachment.
    std::optional<math::Matrix4> inverseBind; // Same rig snapshot; canonical bind-to-bone transform.
};
// Selected-equipment provenance and the actual sampled actor-rig identity.
// Neither record proves which mesh/animation asset was submitted to graphics.
struct WeaponCaptureProvenance {
    std::uint64_t equipmentGeneration=0;
    std::uint32_t weaponData=0,persistence=0;
    RigIdentity rig{};
    bool operator==(const WeaponCaptureProvenance&)const=default;
};
struct WeaponCaptureSample {
    // Compatibility label: selected SoldierWeaponData name, NOT mesh binding.
    std::string assetName,skeleton,rootName,leftName,rightName,muzzleName;
    std::optional<WeaponCaptureProvenance> provenance; // Legacy absence explicit.
    std::uint64_t generation=0,space=0,ownerGeneration=0,capturedMs=0,equipAgeMs=0,captureSequence=0,captureEpisode=0;
    float unitsPerMeter=1;bool attachmentPending=true;
    std::int64_t predictedNs=0;
    std::uint32_t actor=0,weapon=0;
    math::Matrix4 nativeWeapon{},nativeLeft{},nativeRight{};
    std::optional<math::Matrix4> nativeMuzzle;
    std::vector<WeaponBoneCapture> nativeWeaponBones;
    unsigned weaponBonesDropped=0;
    bool weaponBonesComplete=false;
    // Optional inclusive wrist/finger subtree from the SAME native evaluated
    // snapshot as nativeWeaponBones, before any VR retarget or hand posing.
    // Existing sample identity/time/units apply to both subtrees. Missing old
    // captures remain distinguishable from a complete, captured subtree.
    std::vector<WeaponBoneCapture> nativeLeftHandBones;
    unsigned leftHandBonesDropped=0;
    bool leftHandBonesCaptured=false,leftHandBonesComplete=false;
};
class WeaponCapture {
public:
    static constexpr unsigned MaxGroups=32,SamplesPerGroup=96,MaxWeaponBones=64,MaxLeftHandBones=64;
    bool Observe(WeaponCaptureSample);
    std::vector<WeaponCaptureSample> Samples()const;
    void Report(std::ostream&)const;
private:
    struct Group {std::array<WeaponCaptureSample,SamplesPerGroup> samples{};unsigned count=0,next=0;};
    std::vector<Group> groups_;
    std::uint64_t rejected_=0,capacityDropped_=0,observed_=0,episodeSince_=0,episode_=0,stableSince_=0;
    math::Matrix4 attachmentCandidate_{};
    std::optional<WeaponCaptureSample> previous_;
};
}
