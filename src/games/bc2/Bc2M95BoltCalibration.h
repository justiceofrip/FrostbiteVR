#pragma once
#include "Bc2M95BoltGeometry.h"
#include "Bc2PhysicalBolt.h"
namespace fvr::bc2 {
// Authored preparation only. No caller in this payload activates M95 native
// holding or promotes this reference to a native closed pose. Root's bounded
// original-shot rig capture must supply the independent native join first.
inline Bc2BoltCalibration M95AuthoredBoltCalibration(std::uint64_t revision){
    Bc2BoltCalibration c;c.asset=M95BoltAsset;c.mesh=M95BoltMesh;c.partName="jntWpn_3";
    c.rigFingerprint=M95BoltRigFingerprint;c.profile=M95BoltProfile(M95AuthoredReferencePart,revision);
    c.wristFromPart=M95AuthoredPartFromWrist;return c;
}
}
