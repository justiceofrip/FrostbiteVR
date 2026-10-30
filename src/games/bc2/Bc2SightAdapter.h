#pragma once
#include "Bc2AuthoredSight.h"
#include "Bc2WeaponModeFamilies.h"
#include "fvr/interaction/RigPose.h"
namespace fvr::bc2 {
enum class SightAssemblyKind:unsigned {OpposedLeaves,Subtree};
struct SightAdapterProfile {
 const WeaponModeFamilyProfile* family=nullptr;
 SightAssemblyKind assembly=SightAssemblyKind::OpposedLeaves;
 std::string_view rear,front,root;
 const AuthoredSightGeometry* authored=nullptr;
 float travelRadians=1.570796327f;
 bool nativeSightAccepted=false;
 const OpposedSightGeometry* opposed=nullptr;
 bool sharedGeometryEligible=false;
};
inline constexpr std::array<std::string_view,3> Gp30SightChain{"jntWpn_17","jntWpnwpnJnt_16","jntWpn_1"};
inline const AuthoredSightGeometry Gp30SightGeometry{
 "Objects/Weapons/Handheld/RU_rgl_AEK971/RU_rgl_AEK971_Mesh",
 WeaponModeFamilies[1].primaryAsset,WeaponModeFamilies[1].primaryConfiguration,
 WeaponModeFamilies[1].secondaryAsset,WeaponModeFamilies[1].secondaryConfiguration,
 Gp30SightChain,0xa7f219a1426216abull,
 {-.003534470918f,-.006739854813f,-.03502561152f},{.003235569922f,.01918625832f,.002518229187f},
 {-.000007675875633f,-1.f,0.f},.7179032976f};
inline const std::array<SightAdapterProfile,3> SightAdapterProfiles{{
 {&WeaponModeFamilies[0],SightAssemblyKind::OpposedLeaves,"jntWpn_9","jntWpn_11","jntWpn_1",nullptr,1.570796327f,true,&Xm320OpposedSightGeometry,false},
 {&WeaponModeFamilies[1],SightAssemblyKind::Subtree,"jntWpn_17","","jntWpn_1",&Gp30SightGeometry,.7179032976f,false},
 // Installed mesh, skeleton, HandsIkPose and AltDeploy bytes match the scoped
 // pair. Its observed native acceptance is still false until a retained run.
 {&WeaponModeFamilies[2],SightAssemblyKind::OpposedLeaves,"jntWpn_9","jntWpn_11","jntWpn_1",nullptr,1.570796327f,false,&Xm320OpposedSightGeometry,true},
}};
inline bool SightAdapterEnabled(const SightAdapterProfile& p)noexcept {
 return p.family&&WeaponModeFamilyEligible(*p.family)&&(p.nativeSightAccepted||p.sharedGeometryEligible);
}
inline WeaponSightContact MeasureOpposedSightAdapterContact(const SightAdapterProfile& p,
 const WeaponSightObservation& s,std::string_view configuration)noexcept {
 if(!SightAdapterEnabled(p)||p.assembly!=SightAssemblyKind::OpposedLeaves||!p.opposed||
    p.rear!=p.opposed->rear||p.front!=p.opposed->front||p.root!=p.opposed->root)return {};
 const auto& family=*p.family;
 if(!((s.assetName==family.primaryAsset&&configuration==family.primaryConfiguration)||
      (s.assetName==family.secondaryAsset&&configuration==family.secondaryConfiguration)))return {};
 const auto contact=MeasureWeaponSightContact(s,*p.opposed);
 // Measurement alone cannot reserve the hand: both leaves and the captured
 // wrist must support the same grasp that the held preview will consume.
 // Keep the standalone reader's measurement-only contract unchanged.
 if(!contact.valid||!contact.previewValid||
    !interaction::sight_grasp_detail::Proper(contact.frontLocalMeters)||
    !interaction::sight_grasp_detail::Proper(contact.rawHandLocalFrameMeters)||
    !interaction::SightGraspBinding::Begin(contact.pivotMeters,contact.axis,contact.sightLocalMeters,
        contact.handLocalFrameMeters,contact.graspPointMeters,contact.palmPointWristMeters,p.travelRadians))return {};
 return contact;
}
inline const SightAdapterProfile* SightAdapterForFamily(const WeaponModeFamilyProfile* family)noexcept {
 if(!family)return nullptr;
 for(const auto& p:SightAdapterProfiles)if(p.family==family)return &p;
 return nullptr;
}
// Actual pose-plan input for either assembly. Retarget the whole descendant
// subtree from the original palette, preserving unskinned children and local
// native animation. These CPU writes grant no native palette authorization.
inline std::optional<std::vector<interaction::BoneWrite>> BuildSightAssemblyWrites(
 const SightAdapterProfile& p,std::span<const std::string> names,
 std::span<const std::int32_t> parents,std::span<const math::Matrix4> native,
 std::span<const unsigned> hidden,const math::Matrix4& rearWorld,
 const std::optional<math::Matrix4>& frontWorld={}) {
 using namespace interaction;
 if(names.empty()||names.size()>1024||names.size()!=parents.size()||names.size()!=native.size()||
    p.rear.empty()||p.root.empty()||!sight_grasp_detail::Proper(rearWorld))return {};
 const auto index=[&](std::string_view name)->std::optional<unsigned>{
  const auto it=std::find(names.begin(),names.end(),name);
  if(it==names.end()||std::find(it+1,names.end(),name)!=names.end())return {};
  return unsigned(it-names.begin());
 };
 const auto rear=index(p.rear),root=index(p.root);if(!rear||!root||*rear==*root)return {};
 if(std::find(hidden.begin(),hidden.end(),*rear)!=hidden.end())return {};
 if(p.assembly==SightAssemblyKind::Subtree){
  if(!p.authored||p.authored->boneChain.size()<2||p.authored->boneChain.front()!=p.rear||p.authored->boneChain.back()!=p.root)return {};
  unsigned previous=*rear;
  for(unsigned n=1;n<p.authored->boneChain.size();++n){const auto ancestor=index(p.authored->boneChain[n]);
   if(!ancestor||parents[previous]!=std::int32_t(*ancestor))return {};previous=*ancestor;}
  auto writes=RetargetRigSubtree(parents,native,*rear,rearWorld);
  if(!writes||writes->empty())return {};
  for(const auto& w:*writes)if(std::find(hidden.begin(),hidden.end(),w.index)!=hidden.end())return {};
  return writes;
 }
 if(p.assembly!=SightAssemblyKind::OpposedLeaves||p.front.empty()||!frontWorld||!sight_grasp_detail::Proper(*frontWorld))return {};
 const auto front=index(p.front);if(!front||*front==*rear||*front==*root)return {};
 for(unsigned n:{*rear,*front})if(parents[n]!=std::int32_t(*root)||
    std::find(parents.begin(),parents.end(),std::int32_t(n))!=parents.end()||
    std::find(hidden.begin(),hidden.end(),n)!=hidden.end())return {};
 return std::vector<BoneWrite>{{*rear,rearWorld},{*front,*frontWorld}};
}
inline WeaponSightContact MeasureSightAdapterContact(const SightAdapterProfile& p,const AuthoredSightObservation& s)noexcept {
 if(p.assembly!=SightAssemblyKind::Subtree||!p.authored)return {};
 auto result=MeasureAuthoredSightContact(*p.authored,s);
 if(result.valid)result.previewValid=true; // Subtree preview; no fabricated front leaf.
 return result;
}
struct SightAdapterNativeFrame {math::Matrix4 rear{},rawHand{};};
// Raw animation progress can remain observable during mode attachment changes.
// This result has no contact point, hand claim, ready bit or acknowledgement.
inline std::optional<SightAdapterNativeFrame> ReadSightAdapterNativeFrame(
 const SightAdapterProfile& p,const AuthoredSightObservation& s){
 using namespace interaction;
 if(p.assembly!=SightAssemblyKind::Subtree||!p.authored||s.names.size()!=s.native.size()||
    !s.generation||s.now<=0||s.deadline<=s.now||!std::isfinite(s.unitsPerMetre)||s.unitsPerMetre<=0)return {};
 const auto& g=*p.authored;
 const bool mode=(s.asset==g.primaryAsset&&s.configuration==g.primaryConfiguration)||
     (s.asset==g.secondaryAsset&&s.configuration==g.secondaryConfiguration);
 if(!mode||s.selectedMeshPath!=g.meshPath||s.rigFingerprint!=g.rigFingerprint)return {};
 const auto rear=std::find(s.names.begin(),s.names.end(),p.rear),root=std::find(s.names.begin(),s.names.end(),p.root);
 if(rear==s.names.end()||root==s.names.end())return {};
 const auto idx=unsigned(rear-s.names.begin()),rootIdx=unsigned(root-s.names.begin());
 if(!BuildSightAssemblyWrites(p,s.names,s.parents,s.native,s.hidden,s.native[idx]))return {};
 const auto inverse=InverseAnimatedTransform(s.native[rootIdx]),placed=InverseAnimatedTransform(s.placedWeapon);
 if(!inverse||!placed||!sight_grasp_detail::Proper(s.rawAnatomicalWrist))return {};
 SightAdapterNativeFrame out{Multiply(s.native[idx],*inverse),Multiply(s.rawAnatomicalWrist,*placed)};
 for(auto matrix:{&out.rear,&out.rawHand})for(unsigned n=0;n<3;++n)matrix->values[3][n]/=s.unitsPerMetre;
 if(!sight_grasp_detail::Proper(out.rear)||!sight_grasp_detail::Proper(out.rawHand))return {};
 return out;
}
}
