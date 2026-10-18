// Private experimental geometry. Carry/rail are design estimates, not native animation calibration.
#pragma once
#include "Bc2MagazineGeometryProfile.h"
#include <array>
namespace fvr::bc2::generated {
inline const std::array<MagazineGeometryProfile,1> ExperimentalMagazineGeometry = [] {
 std::array<MagazineGeometryProfile,1> out{};
 {
  auto& g=out[0];g.asset="Nambu100";g.mesh="Objects/Weapons/Handheld/Beach/Nambu100/Nambu100_Mesh";
  g.configurationPath="Objects/Weapons/Handheld/Beach/Nambu100/Nambu100";
  g.meshKind=SelectedMeshKind::Unknown;g.rigFingerprint=0xa7f219a1426216abULL;
  auto& c=g.interaction;auto& p=c.insertion;
  p.id=0xf8e376bab7552db6ULL;p.revision=1;
  p.family=interaction::ReloadInsertionFamily::Magazine;p.orientation=interaction::ReloadInsertionOrientation::Keyed;
  p.approach=interaction::ReloadInsertionApproach::RailContact;
  p.itemFromHand=math::Matrix4{{{{{-0.768824179f,-0.448484633f,0.45581895f,0.f}},{{-0.615781646f,0.711426727f,-0.338651703f,0.f}},{{-0.172401699f,-0.541048561f,-0.823130675f,0.f}},{{0.158145397f,0.0534860381f,0.0138427702f,1.f}}}}};
  p.itemFromInsertion=math::Matrix4{{{{{0.f,1.f,0.f,0.f}},{{0.f,0.f,-1.f,0.f}},{{-1.f,0.f,0.f,0.f}},{{-0.0569306351f,-0.00568795204f,-0.0114991143f,1.f}}}}};
  p.weaponFromEntry=math::Matrix4{{{{{-3.15039823e-16f,1.f,-2.03684795e-16f,0.f}},{{-5.18617098e-17f,2.03684795e-16f,-1.f,0.f}},{{-1.f,3.15039823e-16f,-5.18617098e-17f,0.f}},{{0.12571689f,0.0983480215f,-0.64724005f,1.f}}}}};
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
  c.removalContact={0xbfa639f7f2017fefULL,1};
  c.pullMeters=0.09f;c.maxPullStepMeters=0.07f;
  g.bones={"jntWpn_1","jntWpn_12","LeftHand",{"LeftHandThumb1","LeftHandThumb2","LeftHandThumb3","LeftHandIndex1","LeftHandIndex2","LeftHandIndex3","LeftHandMiddle1","LeftHandMiddle2","LeftHandMiddle3","LeftHandRing1","LeftHandRing2","LeftHandRing3","LeftHandPinky1","LeftHandPinky2","LeftHandPinky3"}};
  g.attachedItem=math::Matrix4{{{{{1.f,-3.15039823e-16f,5.18617098e-17f,0.f}},{{-3.15039823e-16f,1.f,-2.03684795e-16f,0.f}},{{5.18617098e-17f,-2.03684795e-16f,1.f,-0.f}},{{0.0826475248f,0.104035974f,-0.635740936f,1.f}}}}};
  g.wristFromFinger[0]=math::Matrix4{{{{{0.706686307f,0.018719087f,0.707279337f,0.f}},{{0.361395743f,0.849856127f,-0.383585296f,0.f}},{{-0.608266044f,0.526682218f,0.593816689f,0.f}},{{0.0292514917f,0.00496417191f,0.00582850305f,1.f}}}}};
  g.wristFromFinger[1]=math::Matrix4{{{{{0.573866962f,0.335938694f,0.746874758f,0.f}},{{0.285607423f,0.772636901f,-0.566974972f,0.f}},{{-0.76753183f,0.53868118f,0.347444206f,0.f}},{{0.0681192383f,0.00599372169f,0.0447288664f,1.f}}}}};
  g.wristFromFinger[2]=math::Matrix4{{{{{0.615000904f,0.457975112f,0.641897721f,0.f}},{{0.179258526f,0.7115313f,-0.679403849f,0.f}},{{-0.767880374f,0.53289962f,0.355496449f,0.f}},{{0.0892143754f,0.0183427037f,0.072183706f,1.f}}}}};
  g.wristFromFinger[3]=math::Matrix4{{{{{0.0342997187f,0.449293253f,0.892725659f,0.f}},{{-0.451733386f,0.803766135f,-0.387165273f,0.f}},{{-0.891493397f,-0.389994325f,0.230529715f,0.f}},{{0.0456151478f,0.0355907977f,0.0610533096f,1.f}}}}};
  g.wristFromFinger[4]=math::Matrix4{{{{{0.512955527f,-0.676916827f,0.527882787f,0.f}},{{-0.080860267f,0.574113422f,0.814773218f,0.f}},{{-0.854598294f,-0.460627169f,0.239758978f,0.f}},{{0.0472234687f,0.0566582511f,0.1029134f,1.f}}}}};
  g.wristFromFinger[5]=math::Matrix4{{{{{0.0616632326f,-0.603491945f,-0.794981206f,0.f}},{{0.39867352f,-0.715305638f,0.573931415f,0.f}},{{-0.915017524f,-0.352328422f,0.1964882f,0.f}},{{0.0633075242f,0.035433082f,0.11946551f,1.f}}}}};
  g.wristFromFinger[6]=math::Matrix4{{{{{0.155863804f,0.170805521f,0.972898735f,0.f}},{{-0.543602592f,0.837203031f,-0.059894139f,0.f}},{{-0.824744019f,-0.519534945f,0.223339973f,0.f}},{{0.0233935136f,0.0343667194f,0.0705799982f,1.f}}}}};
  g.wristFromFinger[7]=math::Matrix4{{{{{0.497964083f,-0.85395915f,-0.150948806f,0.f}},{{0.267374674f,-0.014396403f,0.963485094f,0.f}},{{-0.824950032f,-0.520140859f,0.221158158f,0.f}},{{0.0309577625f,0.0426561069f,0.117795888f,1.f}}}}};
  g.wristFromFinger[8]=math::Matrix4{{{{{0.037428849f,-0.446726493f,-0.893887309f,0.f}},{{0.56081834f,-0.730974313f,0.388792158f,0.f}},{{-0.827092419f,-0.51586044f,0.223172883f,0.f}},{{0.0465694753f,0.0158835635f,0.113063479f,1.f}}}}};
  g.wristFromFinger[9]=math::Matrix4{{{{{0.0300725667f,0.438144152f,0.898401549f,0.f}},{{-0.690110981f,0.65930405f,-0.298437604f,0.f}},{{-0.72307847f,-0.61102199f,0.322195056f,0.f}},{{0.00391550874f,0.0227049775f,0.0743211955f,1.f}}}}};
  g.wristFromFinger[10]=math::Matrix4{{{{{0.574601653f,-0.6131458f,0.542111768f,0.f}},{{-0.0999724574f,0.604834427f,0.790051153f,0.f}},{{-0.812304406f,-0.508160944f,0.286241169f,0.f}},{{0.00522022326f,0.0417140978f,0.113298831f,1.f}}}}};
  g.wristFromFinger[11]=math::Matrix4{{{{{0.340835086f,-0.765188855f,-0.546184457f,0.f}},{{0.559623622f,-0.301691761f,0.771883076f,0.f}},{{-0.755415677f,-0.568742559f,0.325390621f,0.f}},{{0.0223699156f,0.0234140081f,0.129478823f,1.f}}}}};
  g.wristFromFinger[12]=math::Matrix4{{{{{-0.0361468221f,0.526139959f,0.84962942f,0.f}},{{-0.553373048f,0.697405145f,-0.455416659f,0.f}},{{-0.832148831f,-0.486623886f,0.265942693f,0.f}},{{-0.013539345f,0.0061179474f,0.0738790482f,1.f}}}}};
  g.wristFromFinger[13]=math::Matrix4{{{{{0.294570681f,0.111389269f,0.949115665f,0.f}},{{-0.440511045f,0.897197108f,0.0314224176f,0.f}},{{-0.848043709f,-0.427352056f,0.313356166f,0.f}},{{-0.0148675339f,0.0254505803f,0.105098068f,1.f}}}}};
  g.wristFromFinger[14]=math::Matrix4{{{{{0.461395969f,-0.881371368f,0.1014804f,0.f}},{{0.318008385f,0.271081497f,0.908507286f,0.f}},{{-0.828241768f,-0.386909981f,0.405359395f,0.f}},{{-0.00793007719f,0.0280739175f,0.127450764f,1.f}}}}};
  g.assemblyCount=3;
  g.assembly[0]={"jntWpn_13","jntWpn_12",math::Matrix4{{{{{1.f,-6.30079646e-16f,1.0372342e-16f,0.f}},{{-6.30079646e-16f,1.f,-4.0736959e-16f,0.f}},{{1.0372342e-16f,-4.0736959e-16f,1.f,0.f}},{{3.64285242e-18f,6.30898608e-18f,-1.6904304e-17f,1.f}}}}}};
  g.assembly[1]={"jntWpnwpnJnt_14","jntWpn_13",math::Matrix4{{{{{1.f,-6.30079646e-16f,1.0372342e-16f,0.f}},{{-6.30079646e-16f,1.f,-4.0736959e-16f,0.f}},{{1.0372342e-16f,-4.0736959e-16f,1.f,0.f}},{{3.64285242e-18f,6.30898608e-18f,-1.6904304e-17f,1.f}}}}}};
  g.assembly[2]={"jntWpn_15","jntWpnwpnJnt_14",math::Matrix4{{{{{1.f,-6.30079646e-16f,1.0372342e-16f,0.f}},{{-6.30079646e-16f,1.f,-4.0736959e-16f,0.f}},{{1.0372342e-16f,-4.0736959e-16f,1.f,0.f}},{{3.64285242e-18f,6.30898608e-18f,-1.6904304e-17f,1.f}}}}}};
 }
 return out;
}();
}
