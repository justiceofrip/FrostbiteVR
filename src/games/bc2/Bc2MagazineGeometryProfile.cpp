#include "Bc2MagazineGeometryProfile.h"
#include "Bc2Xm8MagazineCalibration.h"
#include "Bc2MagazineNativeProfile.h"
#include "Bc2MagazineAssembly.h"
#include <algorithm>
#ifdef FVR_BC2_EXPERIMENTAL_MAGAZINE_HEADER
#include FVR_BC2_EXPERIMENTAL_MAGAZINE_HEADER
#endif
namespace fvr::bc2 {
const MagazineGeometryProfile& Xm8MagazineGeometry()noexcept {
 static const MagazineGeometryProfile profile=[] {
  MagazineGeometryProfile g;g.asset=Xm8MagazineAsset;g.mesh=Xm8MagazineMesh;
  g.meshKind=SelectedMeshKind::Xm8;g.rigFingerprint=Xm8MagazineRig;
  auto& c=g.interaction;auto& p=c.insertion;p.id=0x584d384d4147ull;p.revision=1;
  p.family=interaction::ReloadInsertionFamily::Magazine;p.orientation=interaction::ReloadInsertionOrientation::Keyed;
  p.approach=interaction::ReloadInsertionApproach::RailContact;
  p.itemFromHand=xm8_magazine_calibration::ItemFromHand;p.itemFromInsertion=xm8_magazine_calibration::ItemFromInsertion;
  p.weaponFromEntry=xm8_magazine_calibration::WeaponFromEntry;p.travelMeters=.1f;
  p.captureDistanceMeters=.07f;p.releaseDistanceMeters=.15f;p.postCaptureTravelMeters=.035f;
  p.captureAngleRadians=.785398163f;p.releaseAngleRadians=1.30899694f;p.seatToleranceMeters=.004f;
  p.maxStepMeters=.07f;p.maxStepRadians=1.04719755f;p.alignmentNs=120000000;p.seatDwellNs=60000000;
  p.maxSampleGapNs=100000000;p.maxGuidedNs=5000000000ll;
  c.removalContact={0x584d384d414750ull,1};c.pullMeters=.09f;c.maxPullStepMeters=.07f;
  g.bones={"jntWpn_1","jntWpn_6","LeftHand",{
   "LeftHandThumb1","LeftHandThumb2","LeftHandThumb3",
   "LeftHandIndex1","LeftHandIndex2","LeftHandIndex3",
   "LeftHandMiddle1","LeftHandMiddle2","LeftHandMiddle3",
   "LeftHandRing1","LeftHandRing2","LeftHandRing3",
   "LeftHandPinky1","LeftHandPinky2","LeftHandPinky3"}};
  g.attachedItem=xm8_magazine_calibration::AttachedItem;
  g.wristFromFinger=xm8_magazine_calibration::WristFromFinger;return g;
 }();return profile;
}
namespace {
bool ExperimentalShape(const MagazineGeometryProfile& g)noexcept {
 if(g.asset.empty()||g.mesh.empty()||g.asset.find('\0')!=std::string_view::npos||
  g.mesh.find('\0')!=std::string_view::npos||!g.rigFingerprint||
  !interaction::DetachableMagazine(g.interaction).ValidConfig())return false;
 const auto matrix=[](const math::Matrix4& m){return interaction::reload_insertion_detail::Rigid(m)&&
  std::hypot(m.values[3][0],m.values[3][1],m.values[3][2])<=1.5f;};
 if(!matrix(g.attachedItem)||!MagazineAssemblyShape(g))return false;
 for(const auto& finger:g.wristFromFinger)if(!matrix(finger))return false;
 std::array<std::string_view,18> names{g.bones.weapon,g.bones.magazine,g.bones.wrist};
 std::copy(g.bones.fingers.begin(),g.bones.fingers.end(),names.begin()+3);
 for(std::size_t n=0;n<names.size();++n)if(names[n].empty()||names[n].find('\0')!=std::string_view::npos||
  std::find(names.begin(),names.begin()+n,names[n])!=names.begin()+n)return false;
 const auto& p=g.interaction.insertion;
 const auto inverse=interaction::InverseRigid(p.itemFromInsertion);if(!inverse)return false;
 const auto closed=interaction::Multiply(*inverse,interaction::Multiply(
  interaction::reload_insertion_detail::TravelPose(p,p.travelMeters),p.weaponFromEntry));
 if(!matrix(closed))return false;
 for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)
  if(std::abs(closed.values[r][c]-g.attachedItem.values[r][c])>.0001f)return false;
 return true;
}
}
const MagazineGeometryProfile* SelectExperimentalMagazineGeometry(
 std::span<const MagazineGeometryProfile> entries,std::string_view asset)noexcept {
 if(entries.size()>256)return nullptr;
 const auto native=FindMagazineNativeProfile(asset);
 return native?SelectExperimentalMagazineGeometry(entries,*native->profile):nullptr;
}
const MagazineGeometryProfile* SelectExperimentalMagazineGeometry(
 std::span<const MagazineGeometryProfile> entries,const MagazineNativeProfile& native)noexcept {
 if(entries.size()>256||!native.Reviewed())return nullptr;
 const MagazineGeometryProfile* found=nullptr;
 for(const auto& entry:entries)if(MagazineGeometryMatchesConfiguration(entry,native)){
  if(found||!ExperimentalShape(entry))return nullptr;found=&entry;
 }
 return found;
}
bool MagazineGeometryMatchesConfiguration(const MagazineGeometryProfile& geometry,
 const MagazineNativeProfile& native)noexcept {
 if(geometry.asset!=native.configuration.assetName)return false;
 if(!geometry.configurationPath.empty())return
  geometry.configurationPath.find('\0')==std::string_view::npos&&
  geometry.configurationPath==native.configuration.assetPath;
 // Old private calibrated headers predate configuration paths. They cannot
 // grant geometry to any newly registered same-name variant, including one
 // with otherwise copied native values. Regeneration removes this exception.
 return &native==&Xm8MagazineNativeProfile||&native==&AekMagazineNativeProfile;
}
const MagazineGeometryProfile* FindMagazineGeometry(std::string_view asset)noexcept {
 const auto native=FindMagazineNativeProfile(asset);
 return native?FindMagazineGeometry(*native->profile):nullptr;
}
const MagazineGeometryProfile* FindMagazineGeometry(const MagazineNativeProfile& native)noexcept {
 // Accepted calibration only for the immutable accepted descriptor, never a
 // same-name generated variant with a different exact native configuration.
 if(&native==&Xm8MagazineNativeProfile)return &Xm8MagazineGeometry();
 if(!native.Reviewed())return nullptr;
#ifdef FVR_BC2_EXPERIMENTAL_MAGAZINE_HEADER
 return SelectExperimentalMagazineGeometry(generated::ExperimentalMagazineGeometry,native);
#else
 return nullptr;
#endif
}
}
