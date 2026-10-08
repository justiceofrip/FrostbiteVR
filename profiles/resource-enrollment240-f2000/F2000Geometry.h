// Private experimental geometry. Carry/rail are design estimates, not native animation calibration.
#pragma once
#include "Bc2MagazineGeometryProfile.h"
#include <array>
namespace fvr::bc2::generated {
inline const std::array<MagazineGeometryProfile,1> ExperimentalMagazineGeometry = [] {
 std::array<MagazineGeometryProfile,1> out{};
 {
  auto& g=out[0];g.asset="F2000_sp";g.mesh="Objects/Weapons/Handheld/BU_rif_F2000/BU_rif_F2000_Mesh";
  g.configurationPath="Objects/Weapons/Handheld/BU_rif_F2000/SP_rif_F2000";
  g.meshKind=SelectedMeshKind::Unknown;g.rigFingerprint=0xa7f219a1426216abULL;
  auto& c=g.interaction;auto& p=c.insertion;
  p.id=0xad03b2e0731a13faULL;p.revision=1;
  p.family=interaction::ReloadInsertionFamily::Magazine;p.orientation=interaction::ReloadInsertionOrientation::Keyed;
  p.approach=interaction::ReloadInsertionApproach::RailContact;
  p.itemFromHand=math::Matrix4{{{{{-0.534427876f,0.825101377f,0.183288198f,0.f}},{{0.367033697f,0.421893174f,-0.82903101f,0.f}},{{-0.761362667f,-0.375784337f,-0.528311481f,0.f}},{{0.0885596941f,-0.113206247f,0.0358441255f,1.f}}}}};
  p.itemFromInsertion=math::Matrix4{{{{{1.f,0.f,0.f,0.f}},{{0.f,0.f,-1.f,0.f}},{{0.f,1.f,0.f,0.f}},{{-0.00422296845f,0.0819444433f,-0.0114707564f,1.f}}}}};
  p.weaponFromEntry=math::Matrix4{{{{{1.f,-2.4023974e-16f,1.99326083e-16f,0.f}},{{-1.99326083e-16f,-1.00209609e-16f,-1.f,0.f}},{{-2.4023974e-16f,1.f,1.00209609e-16f,0.f}},{{-0.00422297422f,-0.0331732625f,-0.215019724f,1.f}}}}};
  p.travelMeters=0.1f;
  p.captureDistanceMeters=0.07f;
  p.releaseDistanceMeters=0.15f;
  p.postCaptureTravelMeters=0.035f;
  p.seatToleranceMeters=0.004f;
  p.captureAngleRadians=0.785398163f;
  p.releaseAngleRadians=1.30899694f;
  p.maxStepMeters=0.07f;
  p.maxStepRadians=1.04719755f;
  p.alignmentNs=120000000LL;
  p.seatDwellNs=60000000LL;
  p.maxSampleGapNs=100000000LL;
  p.maxGuidedNs=5000000000LL;
  c.removalContact={0xea46fdad364e41a3ULL,1};
  c.pullMeters=0.09f;c.maxPullStepMeters=0.07f;
  g.bones={"jntWpn_1","jntWpnwpnJnt_16","LeftHand",{"LeftHandThumb1","LeftHandThumb2","LeftHandThumb3","LeftHandIndex1","LeftHandIndex2","LeftHandIndex3","LeftHandMiddle1","LeftHandMiddle2","LeftHandMiddle3","LeftHandRing1","LeftHandRing2","LeftHandRing3","LeftHandPinky1","LeftHandPinky2","LeftHandPinky3"}};
  g.attachedItem=math::Matrix4{{{{{1.f,-2.4023974e-16f,1.99326083e-16f,0.f}},{{-2.4023974e-16f,1.f,1.00209609e-16f,0.f}},{{1.99326083e-16f,1.00209609e-16f,1.f,-0.f}},{{-5.76861895e-09f,-0.0151177058f,-0.203548968f,1.f}}}}};
  g.wristFromFinger[0]=math::Matrix4{{{{{0.777400183f,-0.293338321f,0.556418534f,0.f}},{{0.428855289f,0.894300624f,-0.127708787f,0.f}},{{-0.460143561f,0.337903866f,0.821029159f,0.f}},{{0.0292514917f,0.00496417191f,0.00582850305f,1.f}}}}};
  g.wristFromFinger[1]=math::Matrix4{{{{{0.797955196f,-0.11206683f,0.592206493f,0.f}},{{0.365273491f,0.87147762f,-0.327264473f,0.f}},{{-0.479419213f,0.47745972f,0.736335137f,0.f}},{{0.0720085015f,-0.0111694357f,0.0364315223f,1.f}}}}};
  g.wristFromFinger[2]=math::Matrix4{{{{{0.614917234f,0.139114556f,0.776224153f,0.f}},{{0.314112554f,0.85965233f,-0.402903432f,0.f}},{{-0.723332634f,0.491574015f,0.484917404f,0.f}},{{0.101341039f,-0.0152889708f,0.0582008138f,1.f}}}}};
  g.wristFromFinger[3]=math::Matrix4{{{{{-0.109821739f,0.541742339f,0.833339321f,0.f}},{{-0.531540496f,0.676442139f,-0.509794795f,0.f}},{{-0.839883258f,-0.498940147f,0.21366994f,0.f}},{{0.0456151478f,0.0355907977f,0.0610533096f,1.f}}}}};
  g.wristFromFinger[4]=math::Matrix4{{{{{0.51939109f,-0.342272166f,0.782995951f,0.f}},{{-0.316712529f,0.773923407f,0.548393776f,0.f}},{{-0.793678821f,-0.532815469f,0.293567038f,0.f}},{{0.0404655838f,0.0609932079f,0.100128763f,1.f}}}}};
  g.wristFromFinger[5]=math::Matrix4{{{{{0.582428711f,-0.77915473f,0.231721177f,0.f}},{{0.0824796215f,0.340234737f,0.936716305f,0.f}},{{-0.808686533f,-0.526458195f,0.262426866f,0.f}},{{0.0567514305f,0.0502610406f,0.124680112f,1.f}}}}};
  g.wristFromFinger[6]=math::Matrix4{{{{{0.227132772f,-0.101133165f,0.968598362f,0.f}},{{-0.481215164f,0.853032447f,0.201909907f,0.f}},{{-0.846665618f,-0.511964577f,0.145084812f,0.f}},{{0.0233935136f,0.0343667194f,0.0705799982f,1.f}}}}};
  g.wristFromFinger[7]=math::Matrix4{{{{{0.531586678f,-0.753646318f,0.386565429f,0.f}},{{-0.0846428389f,0.406839652f,0.909569726f,0.f}},{{-0.84276402f,-0.516235144f,0.152479775f,0.f}},{{0.0344165271f,0.0294586112f,0.117587186f,1.f}}}}};
  g.wristFromFinger[8]=math::Matrix4{{{{{0.401168003f,-0.804824056f,-0.437404245f,0.f}},{{0.360713799f,-0.300128562f,0.883067609f,0.f}},{{-0.841991561f,-0.512036216f,0.169909167f,0.f}},{{0.0510823446f,0.00583098362f,0.12970643f,1.f}}}}};
  g.wristFromFinger[9]=math::Matrix4{{{{{0.194781958f,-0.083430312f,0.977291856f,0.f}},{{-0.508082069f,0.843696116f,0.173290148f,0.f}},{{-0.838994994f,-0.530298263f,0.121947335f,0.f}},{{0.00391550874f,0.0227049775f,0.0743211955f,1.f}}}}};
  g.wristFromFinger[10]=math::Matrix4{{{{{0.478610164f,-0.695036484f,0.536522689f,0.f}},{{-0.156113604f,0.53395649f,0.830974734f,0.f}},{{-0.86403753f,-0.481471445f,0.147052353f,0.f}},{{0.012366229f,0.0190853085f,0.11672153f,1.f}}}}};
  g.wristFromFinger[11]=math::Matrix4{{{{{0.44261075f,-0.852113373f,-0.279282157f,0.f}},{{0.295933093f,-0.155196394f,0.942516676f,0.f}},{{-0.846474647f,-0.499816845f,0.183476953f,0.f}},{{0.0266509374f,-0.00165890937f,0.132734708f,1.f}}}}};
  g.wristFromFinger[12]=math::Matrix4{{{{{0.252012278f,-0.0117075165f,0.967653216f,0.f}},{{-0.461181238f,0.87762315f,0.13072671f,0.f}},{{-0.850765348f,-0.479208244f,0.215772522f,0.f}},{{-0.013539345f,0.0061179474f,0.0738790482f,1.f}}}}};
  g.wristFromFinger[13]=math::Matrix4{{{{{0.43734789f,-0.544201305f,0.715941173f,0.f}},{{-0.171010677f,0.731261457f,0.66031207f,0.f}},{{-0.882882875f,-0.411219675f,0.226751421f,0.f}},{{-0.00427933582f,0.00568776316f,0.109434767f,1.f}}}}};
  g.wristFromFinger[14]=math::Matrix4{{{{{0.48275597f,-0.857628209f,0.177258367f,0.f}},{{0.162251761f,0.286491486f,0.944244139f,0.f}},{{-0.860593423f,-0.427079014f,0.277457162f,0.f}},{{0.00602067785f,-0.00712876348f,0.126295952f,1.f}}}}};
  g.assemblyCount=3;
  g.assembly[0]={"jntWpn_17","jntWpnwpnJnt_16",math::Matrix4{{{{{1.f,-4.80479479e-16f,3.98652166e-16f,0.f}},{{-4.80479479e-16f,1.f,2.00419219e-16f,0.f}},{{3.98652166e-16f,2.00419219e-16f,1.f,0.f}},{{2.92462765e-08f,0.0764105096f,-1.38585596e-08f,1.f}}}}}};
  g.assembly[1]={"jntWpn_18","jntWpn_17",math::Matrix4{{{{{1.f,-4.80479479e-16f,3.98652166e-16f,0.f}},{{-4.80479479e-16f,1.f,2.00419219e-16f,0.f}},{{3.98652166e-16f,2.00419219e-16f,1.f,0.f}},{{2.92462765e-08f,0.0764105096f,-1.38585596e-08f,1.f}}}}}};
  g.assembly[2]={"jntWpn_19","jntWpn_18",math::Matrix4{{{{{1.f,-4.80479479e-16f,3.98652166e-16f,0.f}},{{-4.80479479e-16f,1.f,2.00419219e-16f,0.f}},{{3.98652166e-16f,2.00419219e-16f,1.f,0.f}},{{2.92462765e-08f,0.0764105096f,-1.38585596e-08f,1.f}}}}}};
 }
 return out;
}();
}
