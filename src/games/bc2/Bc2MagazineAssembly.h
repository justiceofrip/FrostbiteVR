#pragma once
#include "Bc2MagazineGeometryProfile.h"
#include <algorithm>

namespace fvr::bc2 {
// A complete, ordered subtree in the exact rig. These data describe rigid
// presentation only; no magazine/chamber/native admission is inferred.
inline bool MagazineAssemblyShape(const MagazineGeometryProfile& profile)noexcept {
 if(profile.assemblyCount>profile.assembly.size())return false;
 std::array<std::string_view,18+MagazineAssemblyLimit> names{
  profile.bones.weapon,profile.bones.magazine,profile.bones.wrist};
 std::copy(profile.bones.fingers.begin(),profile.bones.fingers.end(),names.begin()+3);
 for(unsigned n=0;n<profile.assemblyCount;++n){
  const auto& member=profile.assembly[n];const auto end=names.begin()+18+n;
  if(member.bone.empty()||member.bone.find('\0')!=std::string_view::npos||
   std::find(names.begin(),end,member.bone)!=end||
   (member.parent!=profile.bones.magazine&&std::find(names.begin()+18,end,member.parent)==end)||
   !interaction::reload_insertion_detail::Rigid(member.itemFromBone)||
   std::hypot(member.itemFromBone.values[3][0],member.itemFromBone.values[3][1],member.itemFromBone.values[3][2])>1.5f)return false;
  names[18+n]=member.bone;
 }
 return true;
}
}
