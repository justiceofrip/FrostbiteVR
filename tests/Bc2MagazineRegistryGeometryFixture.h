#pragma once
#include "Bc2MagazineGeometryProfile.h"
#include "Bc2SightContact.h"
#include "fvr/interaction/ReloadInsertion.h"
namespace fvr::bc2::generated {
// Synthetic geometry only, no game assets or real weapon enablement.
inline const std::array<MagazineGeometryProfile,3> ExperimentalMagazineGeometry=[] {
 std::array<MagazineGeometryProfile,3> out{};
 for(unsigned n=0;n<out.size();++n){auto& g=out[n];g=Xm8MagazineGeometry();
  g.asset=n==2?"registry_drum":"registry_rifle";g.mesh=n==2?"fixture/mesh/drum":"fixture/mesh/rifle";
  g.configurationPath=n==0?"Objects/Weapons/A":n==1?"Objects/Weapons/B":"Objects/Weapons/Drum";
  g.meshKind=SelectedMeshKind::Unknown;
  auto& c=g.interaction;auto& p=c.insertion;p.id=0x52454749535400ull+n;p.revision=2;
  p.itemFromHand=p.itemFromInsertion=p.weaponFromEntry=interaction::reload_insertion_detail::Identity();
  p.weaponFromEntry.values[3]={.2f,-.1f,.3f,1};p.travelMeters=.14f;
  c.pullMeters=.12f;c.maxPullStepMeters=.05f;c.ackTimeoutNs=10000000000ll;
  g.attachedItem=interaction::Multiply(interaction::reload_insertion_detail::TravelPose(p,p.travelMeters),p.weaponFromEntry);
  for(auto& f:g.wristFromFinger)f=interaction::reload_insertion_detail::Identity();
  std::vector<std::string> names{"root",std::string(g.bones.weapon),std::string(g.bones.wrist),"untouched",std::string(g.bones.magazine)};
  std::vector<int> parents{-1,0,0,0,1};
  for(unsigned k=0;k<15;++k){names.push_back(std::string(g.bones.fingers[k]));parents.push_back(k%3?int(names.size()-2):2);}
  std::vector<math::Matrix4> bind(names.size(),interaction::reload_insertion_detail::Identity());g.rigFingerprint=SightRigFingerprint(names,parents,bind);
 }
 return out;
}();
}

