// Private experimental geometry. Carry/rail are design estimates, not native animation calibration.
#pragma once
#include "Bc2MagazineGeometryProfile.h"
#include <array>
namespace fvr::bc2::generated {
inline const std::array<MagazineGeometryProfile,2> ExperimentalMagazineGeometry = [] {
 std::array<MagazineGeometryProfile,2> out{};
 {
  auto& g=out[0];g.asset="SCAR_sp";g.mesh="Objects/Weapons/Handheld/UL_rif_FNSCARL/UL_rif_FNSCARL_Mesh";
  g.configurationPath="Objects/Weapons/Handheld/UL_rif_FNSCARL/SP_rif_FNSCARL";
  g.meshKind=SelectedMeshKind::Unknown;g.rigFingerprint=0xa7f219a1426216abULL;
  auto& c=g.interaction;auto& p=c.insertion;
  p.id=0x44586c5c876da2a0ULL;p.revision=1;
  p.family=interaction::ReloadInsertionFamily::Magazine;p.orientation=interaction::ReloadInsertionOrientation::Keyed;
  p.approach=interaction::ReloadInsertionApproach::RailContact;
  p.itemFromHand=math::Matrix4{{{{{-0.427160812f,0.740587428f,0.5187137f,0.f}},{{0.829611908f,0.549154496f,-0.100863385f,0.f}},{{-0.359552115f,0.387246177f,-0.848977429f,0.f}},{{0.043796617f,-0.0955955669f,0.0172200157f,1.f}}}}};
  p.itemFromInsertion=math::Matrix4{{{{{1.f,0.f,0.f,0.f}},{{0.f,0.f,-1.f,0.f}},{{0.f,1.f,0.f,0.f}},{{-0.00393676758f,0.0809018612f,-0.00433373451f,1.f}}}}};
  p.weaponFromEntry=math::Matrix4{{{{{1.f,-2.32382219e-16f,1.57304183e-16f,0.f}},{{-1.57304183e-16f,2.0008824e-17f,-1.f,0.f}},{{-2.32382219e-16f,1.f,-2.0008824e-17f,0.f}},{{-0.00393678457f,-0.0840978384f,-0.437004745f,1.f}}}}};
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
  c.removalContact={0x031d2311c239f0f9ULL,1};
  c.pullMeters=0.09f;c.maxPullStepMeters=0.07f;
  g.bones={"jntWpn_1","jntWpn_6","LeftHand",{"LeftHandThumb1","LeftHandThumb2","LeftHandThumb3","LeftHandIndex1","LeftHandIndex2","LeftHandIndex3","LeftHandMiddle1","LeftHandMiddle2","LeftHandMiddle3","LeftHandRing1","LeftHandRing2","LeftHandRing3","LeftHandPinky1","LeftHandPinky2","LeftHandPinky3"}};
  g.attachedItem=math::Matrix4{{{{{1.f,-2.32382219e-16f,1.57304183e-16f,0.f}},{{-2.32382219e-16f,1.f,-2.0008824e-17f,0.f}},{{1.57304183e-16f,-2.0008824e-17f,1.f,-0.f}},{{-1.69886541e-08f,-0.0649996996f,-0.43267101f,1.f}}}}};
  g.wristFromFinger[0]=math::Matrix4{{{{{0.986033675f,0.116431393f,0.119085359f,0.f}},{{-0.10924088f,0.991871676f,-0.065245757f,0.f}},{{-0.125714049f,0.0513255241f,0.990737941f,0.f}},{{0.0292514917f,0.00496417191f,0.00582850305f,1.f}}}}};
  g.wristFromFinger[1]=math::Matrix4{{{{{0.717477545f,-0.142652844f,0.681818259f,0.f}},{{-0.0122588206f,0.976068591f,0.21711708f,0.f}},{{-0.696473756f,-0.164134917f,0.698558541f,0.f}},{{0.0834833435f,0.0113678985f,0.0123781978f,1.f}}}}};
  g.wristFromFinger[2]=math::Matrix4{{{{{0.907617684f,0.0462176045f,0.417245818f,0.f}},{{-0.126755768f,0.97770177f,0.167428264f,0.f}},{{-0.400203842f,-0.204849168f,0.893237764f,0.f}},{{0.109857552f,0.00612403272f,0.0374415846f,1.f}}}}};
  g.wristFromFinger[3]=math::Matrix4{{{{{0.20649458f,-0.248536225f,0.946356029f,0.f}},{{-0.475764902f,0.819658177f,0.31907402f,0.f}},{{-0.85498991f,-0.516130039f,0.0510101585f,0.f}},{{0.0456151478f,0.0355907977f,0.0610533096f,1.f}}}}};
  g.wristFromFinger[4]=math::Matrix4{{{{{0.534886785f,-0.816334991f,-0.217929598f,0.f}},{{0.448509381f,0.0557371007f,0.892038514f,0.f}},{{-0.716055489f,-0.574883082f,0.39594694f,0.f}},{{0.0552977213f,0.0239368827f,0.105428139f,1.f}}}}};
  g.wristFromFinger[5]=math::Matrix4{{{{{-0.0714629664f,-0.417042972f,-0.906072957f,0.f}},{{0.644199508f,-0.712824664f,0.277286842f,0.f}},{{-0.76151168f,-0.563876013f,0.319599443f,0.f}},{{0.0720694457f,-0.00165983397f,0.0985948145f,1.f}}}}};
  g.wristFromFinger[6]=math::Matrix4{{{{{0.349843329f,-0.628607629f,0.694594914f,0.f}},{{-0.420850013f,0.556960962f,0.716016587f,0.f}},{{-0.836955741f,-0.542813905f,-0.0697004524f,0.f}},{{0.0233935136f,0.0343667194f,0.0705799982f,1.f}}}}};
  g.wristFromFinger[7]=math::Matrix4{{{{{0.456277846f,-0.732677181f,-0.504969975f,0.f}},{{0.219831125f,-0.457082466f,0.861829389f,0.f}},{{-0.862255649f,-0.504241775f,-0.0474913538f,0.f}},{{0.0403718109f,0.00385967151f,0.104289485f,1.f}}}}};
  g.wristFromFinger[8]=math::Matrix4{{{{{0.126856354f,-0.0988551514f,-0.986982839f,0.f}},{{0.488856674f,-0.859558328f,0.148924924f,0.f}},{{-0.863091315f,-0.501385221f,-0.0607144348f,0.f}},{{0.0546766151f,-0.0191105509f,0.0884581294f,1.f}}}}};
  g.wristFromFinger[9]=math::Matrix4{{{{{0.462670965f,-0.655104899f,0.597304905f,0.f}},{{-0.166268454f,0.597678373f,0.784305657f,0.f}},{{-0.870798702f,-0.462188418f,0.167605151f,0.f}},{{0.00391550874f,0.0227049775f,0.0743211955f,1.f}}}}};
  g.wristFromFinger[10]=math::Matrix4{{{{{0.46875961f,-0.822416948f,-0.32232715f,0.f}},{{0.17055332f,-0.273767771f,0.946553101f,0.f}},{{-0.866704098f,-0.498679828f,0.0119346677f,0.f}},{{0.023988738f,-0.00571710174f,0.100235591f,1.f}}}}};
  g.wristFromFinger[11]=math::Matrix4{{{{{0.0736077833f,-0.216250976f,-0.973559145f,0.f}},{{0.571444433f,-0.790911098f,0.218885576f,0.f}},{{-0.817332952f,-0.572446636f,0.0653582036f,0.f}},{{0.0379794445f,-0.030263146f,0.0906153411f,1.f}}}}};
  g.wristFromFinger[12]=math::Matrix4{{{{{0.339927092f,-0.567123292f,0.750213799f,0.f}},{{-0.212039159f,0.730958677f,0.648643822f,0.f}},{{-0.916236306f,-0.379566311f,0.128220311f,0.f}},{{-0.013539345f,0.0061179474f,0.0738790482f,1.f}}}}};
  g.wristFromFinger[13]=math::Matrix4{{{{{0.527631114f,-0.841372265f,-0.117038967f,0.f}},{{0.205646356f,-0.00716559604f,0.978600138f,0.f}},{{-0.824205669f,-0.540408518f,0.169244349f,0.f}},{{-0.00104896948f,-0.0147205885f,0.101445113f,1.f}}}}};
  g.wristFromFinger[14]=math::Matrix4{{{{{0.260662426f,-0.28052905f,-0.923774081f,0.f}},{{0.67960787f,-0.626292575f,0.381956481f,0.f}},{{-0.685702736f,-0.727365839f,0.0273988084f,0.f}},{{0.0113773113f,-0.0345358112f,0.0986887189f,1.f}}}}};
 }
 {
  auto& g=out[1];g.asset="SCAR_sp_s";g.mesh="Objects/Weapons/Handheld/UL_rif_FNSCARL/UL_rif_FNSCARL_Mesh";
  g.configurationPath="Objects/Weapons/Handheld/UL_rif_FNSCARL/SP_rif_FNSCARL_Scoped";
  g.meshKind=SelectedMeshKind::Unknown;g.rigFingerprint=0xa7f219a1426216abULL;
  auto& c=g.interaction;auto& p=c.insertion;
  p.id=0xa1c7801792a50fc0ULL;p.revision=1;
  p.family=interaction::ReloadInsertionFamily::Magazine;p.orientation=interaction::ReloadInsertionOrientation::Keyed;
  p.approach=interaction::ReloadInsertionApproach::RailContact;
  p.itemFromHand=math::Matrix4{{{{{-0.427160812f,0.740587428f,0.5187137f,0.f}},{{0.829611908f,0.549154496f,-0.100863385f,0.f}},{{-0.359552115f,0.387246177f,-0.848977429f,0.f}},{{0.043796617f,-0.0955955669f,0.0172200157f,1.f}}}}};
  p.itemFromInsertion=math::Matrix4{{{{{1.f,0.f,0.f,0.f}},{{0.f,0.f,-1.f,0.f}},{{0.f,1.f,0.f,0.f}},{{-0.00393676758f,0.0809018612f,-0.00433373451f,1.f}}}}};
  p.weaponFromEntry=math::Matrix4{{{{{1.f,-2.32382219e-16f,1.57304183e-16f,0.f}},{{-1.57304183e-16f,2.0008824e-17f,-1.f,0.f}},{{-2.32382219e-16f,1.f,-2.0008824e-17f,0.f}},{{-0.00393678457f,-0.0840978384f,-0.437004745f,1.f}}}}};
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
  c.removalContact={0xe682cf5ad7f15d99ULL,1};
  c.pullMeters=0.09f;c.maxPullStepMeters=0.07f;
  g.bones={"jntWpn_1","jntWpn_6","LeftHand",{"LeftHandThumb1","LeftHandThumb2","LeftHandThumb3","LeftHandIndex1","LeftHandIndex2","LeftHandIndex3","LeftHandMiddle1","LeftHandMiddle2","LeftHandMiddle3","LeftHandRing1","LeftHandRing2","LeftHandRing3","LeftHandPinky1","LeftHandPinky2","LeftHandPinky3"}};
  g.attachedItem=math::Matrix4{{{{{1.f,-2.32382219e-16f,1.57304183e-16f,0.f}},{{-2.32382219e-16f,1.f,-2.0008824e-17f,0.f}},{{1.57304183e-16f,-2.0008824e-17f,1.f,-0.f}},{{-1.69886541e-08f,-0.0649996996f,-0.43267101f,1.f}}}}};
  g.wristFromFinger[0]=math::Matrix4{{{{{0.986033675f,0.116431393f,0.119085359f,0.f}},{{-0.10924088f,0.991871676f,-0.065245757f,0.f}},{{-0.125714049f,0.0513255241f,0.990737941f,0.f}},{{0.0292514917f,0.00496417191f,0.00582850305f,1.f}}}}};
  g.wristFromFinger[1]=math::Matrix4{{{{{0.717477545f,-0.142652844f,0.681818259f,0.f}},{{-0.0122588206f,0.976068591f,0.21711708f,0.f}},{{-0.696473756f,-0.164134917f,0.698558541f,0.f}},{{0.0834833435f,0.0113678985f,0.0123781978f,1.f}}}}};
  g.wristFromFinger[2]=math::Matrix4{{{{{0.907617684f,0.0462176045f,0.417245818f,0.f}},{{-0.126755768f,0.97770177f,0.167428264f,0.f}},{{-0.400203842f,-0.204849168f,0.893237764f,0.f}},{{0.109857552f,0.00612403272f,0.0374415846f,1.f}}}}};
  g.wristFromFinger[3]=math::Matrix4{{{{{0.20649458f,-0.248536225f,0.946356029f,0.f}},{{-0.475764902f,0.819658177f,0.31907402f,0.f}},{{-0.85498991f,-0.516130039f,0.0510101585f,0.f}},{{0.0456151478f,0.0355907977f,0.0610533096f,1.f}}}}};
  g.wristFromFinger[4]=math::Matrix4{{{{{0.534886785f,-0.816334991f,-0.217929598f,0.f}},{{0.448509381f,0.0557371007f,0.892038514f,0.f}},{{-0.716055489f,-0.574883082f,0.39594694f,0.f}},{{0.0552977213f,0.0239368827f,0.105428139f,1.f}}}}};
  g.wristFromFinger[5]=math::Matrix4{{{{{-0.0714629664f,-0.417042972f,-0.906072957f,0.f}},{{0.644199508f,-0.712824664f,0.277286842f,0.f}},{{-0.76151168f,-0.563876013f,0.319599443f,0.f}},{{0.0720694457f,-0.00165983397f,0.0985948145f,1.f}}}}};
  g.wristFromFinger[6]=math::Matrix4{{{{{0.349843329f,-0.628607629f,0.694594914f,0.f}},{{-0.420850013f,0.556960962f,0.716016587f,0.f}},{{-0.836955741f,-0.542813905f,-0.0697004524f,0.f}},{{0.0233935136f,0.0343667194f,0.0705799982f,1.f}}}}};
  g.wristFromFinger[7]=math::Matrix4{{{{{0.456277846f,-0.732677181f,-0.504969975f,0.f}},{{0.219831125f,-0.457082466f,0.861829389f,0.f}},{{-0.862255649f,-0.504241775f,-0.0474913538f,0.f}},{{0.0403718109f,0.00385967151f,0.104289485f,1.f}}}}};
  g.wristFromFinger[8]=math::Matrix4{{{{{0.126856354f,-0.0988551514f,-0.986982839f,0.f}},{{0.488856674f,-0.859558328f,0.148924924f,0.f}},{{-0.863091315f,-0.501385221f,-0.0607144348f,0.f}},{{0.0546766151f,-0.0191105509f,0.0884581294f,1.f}}}}};
  g.wristFromFinger[9]=math::Matrix4{{{{{0.462670965f,-0.655104899f,0.597304905f,0.f}},{{-0.166268454f,0.597678373f,0.784305657f,0.f}},{{-0.870798702f,-0.462188418f,0.167605151f,0.f}},{{0.00391550874f,0.0227049775f,0.0743211955f,1.f}}}}};
  g.wristFromFinger[10]=math::Matrix4{{{{{0.46875961f,-0.822416948f,-0.32232715f,0.f}},{{0.17055332f,-0.273767771f,0.946553101f,0.f}},{{-0.866704098f,-0.498679828f,0.0119346677f,0.f}},{{0.023988738f,-0.00571710174f,0.100235591f,1.f}}}}};
  g.wristFromFinger[11]=math::Matrix4{{{{{0.0736077833f,-0.216250976f,-0.973559145f,0.f}},{{0.571444433f,-0.790911098f,0.218885576f,0.f}},{{-0.817332952f,-0.572446636f,0.0653582036f,0.f}},{{0.0379794445f,-0.030263146f,0.0906153411f,1.f}}}}};
  g.wristFromFinger[12]=math::Matrix4{{{{{0.339927092f,-0.567123292f,0.750213799f,0.f}},{{-0.212039159f,0.730958677f,0.648643822f,0.f}},{{-0.916236306f,-0.379566311f,0.128220311f,0.f}},{{-0.013539345f,0.0061179474f,0.0738790482f,1.f}}}}};
  g.wristFromFinger[13]=math::Matrix4{{{{{0.527631114f,-0.841372265f,-0.117038967f,0.f}},{{0.205646356f,-0.00716559604f,0.978600138f,0.f}},{{-0.824205669f,-0.540408518f,0.169244349f,0.f}},{{-0.00104896948f,-0.0147205885f,0.101445113f,1.f}}}}};
  g.wristFromFinger[14]=math::Matrix4{{{{{0.260662426f,-0.28052905f,-0.923774081f,0.f}},{{0.67960787f,-0.626292575f,0.381956481f,0.f}},{{-0.685702736f,-0.727365839f,0.0273988084f,0.f}},{{0.0113773113f,-0.0345358112f,0.0986887189f,1.f}}}}};
 }
 return out;
}();
}
