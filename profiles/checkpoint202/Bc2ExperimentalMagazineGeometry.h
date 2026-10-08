// Private experimental geometry. Carry/rail are design estimates, not native animation calibration.
#pragma once
#include "Bc2MagazineGeometryProfile.h"
#include <array>
namespace fvr::bc2::generated {
inline const std::array<MagazineGeometryProfile,1> ExperimentalMagazineGeometry = [] {
 std::array<MagazineGeometryProfile,1> out{};
 {
  auto& g=out[0];g.asset="AEK971_sp";g.mesh="Objects/Weapons/Handheld/RU_rgl_AEK971/RU_rgl_AEK971_Mesh";
  g.meshKind=SelectedMeshKind::Unknown;g.rigFingerprint=0xa7f219a1426216abULL;
  auto& c=g.interaction;auto& p=c.insertion;
  p.id=0x148b30c5b8939168ULL;p.revision=1;
  p.family=interaction::ReloadInsertionFamily::Magazine;p.orientation=interaction::ReloadInsertionOrientation::Keyed;
  p.approach=interaction::ReloadInsertionApproach::RailContact;
  p.itemFromHand=math::Matrix4{{{{{-0.515190454f,0.74389184f,0.425680311f,0.f}},{{0.717634364f,0.645950361f,-0.260286478f,0.f}},{{-0.468593337f,0.171385711f,-0.866629807f,0.f}},{{0.0566705845f,-0.0764580724f,0.0388906263f,1.f}}}}};
  p.itemFromInsertion=math::Matrix4{{{{{1.f,0.f,0.f,0.f}},{{0.f,0.f,-1.f,0.f}},{{0.f,1.f,0.f,0.f}},{{-0.00354574306f,0.0826972723f,0.00432417542f,1.f}}}}};
  p.weaponFromEntry=math::Matrix4{{{{{1.f,-1.96151655e-16f,7.17835012e-17f,0.f}},{{-7.17835012e-17f,1.09233369e-17f,-1.f,0.f}},{{-1.96151655e-16f,1.f,-1.09233369e-17f,0.f}},{{-0.00354575233f,-0.0729902335f,-0.40999081f,1.f}}}}};
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
  c.removalContact={0x53ce7f88fdc7c331ULL,1};
  c.pullMeters=0.09f;c.maxPullStepMeters=0.07f;
  g.bones={"jntWpn_1","jntWpn_6","LeftHand",{"LeftHandThumb1","LeftHandThumb2","LeftHandThumb3","LeftHandIndex1","LeftHandIndex2","LeftHandIndex3","LeftHandMiddle1","LeftHandMiddle2","LeftHandMiddle3","LeftHandRing1","LeftHandRing2","LeftHandRing3","LeftHandPinky1","LeftHandPinky2","LeftHandPinky3"}};
  g.attachedItem=math::Matrix4{{{{{1.f,-1.96151655e-16f,7.17835012e-17f,0.f}},{{-1.96151655e-16f,1.f,-1.09233369e-17f,0.f}},{{7.17835012e-17f,-1.09233369e-17f,1.f,-0.f}},{{-9.26707425e-09f,-0.0556875058f,-0.414314985f,1.f}}}}};
  g.wristFromFinger[0]=math::Matrix4{{{{{0.78433213f,-0.321070746f,0.530788739f,0.f}},{{0.465582013f,0.870116471f,-0.161649978f,0.f}},{{-0.409946946f,0.373912961f,0.83194507f,0.f}},{{0.0292514917f,0.00496417191f,0.00582850305f,1.f}}}}};
  g.wristFromFinger[1]=math::Matrix4{{{{{0.268701274f,-0.0325587284f,0.96267313f,0.f}},{{0.397798932f,0.913967421f,-0.0801221728f,0.f}},{{-0.877243202f,0.404479273f,0.258536038f,0.f}},{{0.0723897586f,-0.012694719f,0.0350218835f,1.f}}}}};
  g.wristFromFinger[2]=math::Matrix4{{{{{-0.549210556f,0.338792097f,0.763929107f,0.f}},{{0.419640516f,0.90233223f,-0.0984803784f,0.f}},{{-0.722682228f,0.266489141f,-0.637741275f,0.f}},{{0.082267118f,-0.0138915658f,0.0704093914f,1.f}}}}};
  g.wristFromFinger[3]=math::Matrix4{{{{{0.106752982f,0.237702437f,0.965453962f,0.f}},{{-0.506242452f,0.84871354f,-0.152983355f,0.f}},{{-0.855758366f,-0.472422351f,0.210937766f,0.f}},{{0.0456151478f,0.0355907977f,0.0610533096f,1.f}}}}};
  g.wristFromFinger[4]=math::Matrix4{{{{{0.596911947f,-0.607028966f,0.524606482f,0.f}},{{-0.141908061f,0.563681856f,0.813710555f,0.f}},{{-0.789657033f,-0.56015944f,0.250326131f,0.f}},{{0.0506208172f,0.0467367141f,0.106323645f,1.f}}}}};
  g.wristFromFinger[5]=math::Matrix4{{{{{0.38846966f,-0.76201144f,-0.518102199f,0.f}},{{0.4655722f,-0.322902808f,0.824000184f,0.f}},{{-0.795194221f,-0.561313052f,0.229333399f,0.f}},{{0.069337381f,0.0277029245f,0.122773024f,1.f}}}}};
  g.wristFromFinger[6]=math::Matrix4{{{{{0.228498948f,-0.109982013f,0.967311836f,0.f}},{{-0.475077898f,0.854665831f,0.209397488f,0.f}},{{-0.849758332f,-0.507395579f,0.143040219f,0.f}},{{0.0233935136f,0.0343667194f,0.0705799982f,1.f}}}}};
  g.wristFromFinger[7]=math::Matrix4{{{{{0.528687647f,-0.768251972f,0.360940826f,0.f}},{{-0.0690703677f,0.384880788f,0.920378218f,0.f}},{{-0.84600157f,-0.51152291f,0.150418272f,0.f}},{{0.0344828292f,0.0290291664f,0.117524749f,1.f}}}}};
  g.wristFromFinger[8]=math::Matrix4{{{{{0.385390861f,-0.786386781f,-0.482772943f,0.f}},{{0.375352121f,-0.344347143f,0.860543915f,0.f}},{{-0.842961843f,-0.512855609f,0.162463704f,0.f}},{{0.051057759f,0.00494363582f,0.128840634f,1.f}}}}};
  g.wristFromFinger[9]=math::Matrix4{{{{{0.194507679f,-0.0836370879f,0.977328809f,0.f}},{{-0.504957881f,0.845656582f,0.172865506f,0.f}},{{-0.840942508f,-0.527133553f,0.122253492f,0.f}},{{0.00391550874f,0.0227049775f,0.0743211955f,1.f}}}}};
  g.wristFromFinger[10]=math::Matrix4{{{{{0.477473166f,-0.703111557f,0.526928377f,0.f}},{{-0.146724448f,0.52748019f,0.836801402f,0.f}},{{-0.866309017f,-0.47686349f,0.148693981f,0.f}},{{0.0123543293f,0.0190763374f,0.116723133f,1.f}}}}};
  g.wristFromFinger[11]=math::Matrix4{{{{{0.43366253f,-0.851556683f,-0.294598073f,0.f}},{{0.303476393f,-0.169811892f,0.937585196f,0.f}},{{-0.848433196f,-0.495999129f,0.184786296f,0.f}},{{0.0266051025f,-0.00190889092f,0.132449958f,1.f}}}}};
  g.wristFromFinger[12]=math::Matrix4{{{{{0.24419483f,0.00185906927f,0.969724409f,0.f}},{{-0.465702777f,0.87735951f,0.115590715f,0.f}},{{-0.850582041f,-0.479830005f,0.21511243f,0.f}},{{-0.013539345f,0.0061179474f,0.0738790482f,1.f}}}}};
  g.wristFromFinger[13]=math::Matrix4{{{{{0.433385218f,-0.537207329f,0.723592108f,0.f}},{{-0.177441663f,0.736330622f,0.652940787f,0.f}},{{-0.883567603f,-0.411370273f,0.223791845f,0.f}},{{-0.00456658231f,0.00618625756f,0.109510871f,1.f}}}}};
  g.wristFromFinger[14]=math::Matrix4{{{{{0.478466061f,-0.860618436f,0.174373556f,0.f}},{{0.164610898f,0.282966342f,0.944898567f,0.f}},{{-0.862538974f,-0.423398108f,0.277056965f,0.f}},{{0.00564010618f,-0.00646555342f,0.126552245f,1.f}}}}};
 }
 return out;
}();
}
