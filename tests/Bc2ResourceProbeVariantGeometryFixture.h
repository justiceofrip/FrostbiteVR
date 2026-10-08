#pragma once
#define ExperimentalMagazineGeometry BaselineMagazineGeometry
#include "../profiles/checkpoint202/Bc2ExperimentalMagazineGeometry.h"
#undef ExperimentalMagazineGeometry
namespace fvr::bc2::generated {
// Synthetic exact-path variants, confined to the standalone mock executable.
inline const auto ExperimentalMagazineGeometry=[] {
 std::array<MagazineGeometryProfile,BaselineMagazineGeometry.size()+2> out{};
 std::copy(BaselineMagazineGeometry.begin(),BaselineMagazineGeometry.end(),out.begin());
 for(unsigned n=0;n<2;++n){auto& g=out[BaselineMagazineGeometry.size()+n];g=Xm8MagazineGeometry();
  g.asset="registry_rifle";g.configurationPath=n?"Objects/Weapons/B":"Objects/Weapons/A";
  auto& p=g.interaction.insertion;p.id+=100+n;p.travelMeters=n?.12f:.09f;
  g.interaction.pullMeters=n?.105f:.075f;
  g.attachedItem=interaction::Multiply(*interaction::InverseRigid(p.itemFromInsertion),
   interaction::Multiply(interaction::reload_insertion_detail::TravelPose(p,p.travelMeters),p.weaponFromEntry));
 }
 return out;
}();
}
