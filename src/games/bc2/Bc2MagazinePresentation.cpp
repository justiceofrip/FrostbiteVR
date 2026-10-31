#include "Bc2MagazinePresentation.h"
#include "Bc2SightContact.h"
#include "Bc2WeaponVisibility.h"
#include "Bc2MagazineAssembly.h"
#include <algorithm>
namespace fvr::bc2 {
namespace {
using namespace interaction;using namespace reload_insertion_detail;
math::Matrix4 Units(math::Matrix4 m,float units)noexcept{for(unsigned n=0;n<3;++n)m.values[3][n]*=units;return m;}
// Native animation may change a declared child outside the authored reload.
// Reject that packet instead of making a rigid assembly erase native motion.
bool AssemblyVisibleRigid(const RigSnapshot& rig,const MagazinePresentationBinding& b,float units,
 MagazineRawContactEvidence* evidence=nullptr)noexcept{
 const auto reject=[&](MagazineRawContactReason reason){if(evidence)evidence->reason=reason;return false;};
 if(evidence)evidence->magazineBone=b.magazine;
 const auto hidden=[&](unsigned n){return std::find(rig.nativeHiddenLeaves.begin(),rig.nativeHiddenLeaves.end(),n)!=rig.nativeHiddenLeaves.end();};
 if(hidden(b.magazine))return reject(MagazineRawContactReason::MagazineHidden);
 if(!Rigid(rig.evaluatedWorld[b.magazine]))return reject(MagazineRawContactReason::MagazineNotRigid);
 if(!b.assemblyCount)return true;
 const auto inverse=InverseRigid(rig.evaluatedWorld[b.magazine]);if(!inverse)return reject(MagazineRawContactReason::MagazineInverseUnavailable);
 for(unsigned n=0;n<b.assemblyCount;++n){const auto bone=b.assembly[n];
  if(evidence){evidence->childBone=bone;evidence->relativeDistanceMeters.reset();evidence->relativeAngleRadians.reset();}
  if(hidden(bone))return reject(MagazineRawContactReason::ChildHidden);
  if(!Rigid(rig.evaluatedWorld[bone]))return reject(MagazineRawContactReason::ChildNotRigid);
  const auto relative=Units(Multiply(rig.evaluatedWorld[bone],*inverse),1/units);
  const auto& expected=b.geometry->assembly[b.assemblyProfileMember[n]].itemFromBone;
  const auto distance=Distance(relative,expected);
  if(evidence)evidence->relativeDistanceMeters=distance;
  if(distance>.001f)return reject(MagazineRawContactReason::ChildPositionMismatch);
  const auto angle=Angle(relative,expected);
  if(evidence)evidence->relativeAngleRadians=angle;
  if(angle>.01f)return reject(MagazineRawContactReason::ChildAngleMismatch);
 }
 if(evidence){evidence->childBone=~0u;evidence->relativeDistanceMeters.reset();evidence->relativeAngleRadians.reset();}
 return true;
}
}
namespace {
bool TrackingFresh(const MagazineTracking& t,std::int64_t now,std::int64_t resourceAt)noexcept {
 const auto& in=t.inputEvidence;
 if(t.resource&&(t.detach||t.retainedVisualSuppression||!MagazineResourcePresentationFresh(*t.resource,t.family,t.reserve,
     in.owner,t.family.binding.weapon,resourceAt)))return false;
 if(t.retainedVisualSuppression){
  if(!t.detach||t.detach->restoring||!t.target||t.target->role!=MagazinePropRole::Removed)return false;
  const auto& r=*t.retainedVisualSuppression;const auto& original=t.detach->suppression;
  auto current=in;current.nowNs=now;
  const HolsterSuppressionRequest expected{t.owner,current,t.detach->request,r.nativeTick,r.cache};
  if(r.cache!=original.cache||r.nativeTick<original.nativeTick||r.input.sequence<original.input.sequence||
   r.input.observedNs<original.input.observedNs||!HolsterSuppressionCurrent(r,expected))return false;
 }
 const auto map=BindMagazineOwners(in.owner,t.family.binding.weapon,t.reserve,t.cycle,resourceAt,t.family);
 if(!MagazineFamilyFresh(t.family,t.owner,in.owner,t.family.binding.weapon,now))return false;
 if(t.detach&&(!MagazineDetachAuthorizationFresh(*t.detach,now)||t.detach->request!=t.cycle||
  t.detach->current.identity!=t.reserve.identity||t.detach->current.sequence!=t.reserve.sequence||
  t.detach->current.loaded!=t.reserve.loaded||t.detach->current.reserve!=t.reserve.reserve||t.detach->current.capacity!=t.reserve.capacity||
  t.detach->current.observedNs!=t.reserve.observedNs||t.detach->current.deadlineNs!=t.reserve.deadlineNs||
  !t.reserve.allThreeIdle||t.detach->original.weapon!=t.family.binding.weapon||
  t.detach->suppression.input.owner!=in.owner||(!t.retainedVisualSuppression&&(t.detach->suppression.input.sequence!=in.sequence||
  t.detach->suppression.input.observedNs!=in.observedNs||t.detach->suppression.input.deadlineNs!=in.deadlineNs))))return false;
 return t.enabled&&map&&map->native.owner==t.owner&&t.selected&&MagazineSelected(*t.selected,t.owner,t.family.binding.profile->geometry->asset,*t.family.binding.profile,now)&&
 (!t.family.carried||
  t.selected->weaponData==t.family.binding.equipment.data)&&
 in.sequence&&in.focused&&in.tracked[0]&&in.tracked[1]&&!in.released[1]&&in.observedNs>0&&in.observedNs<=now&&
 in.deadlineNs>now&&in.deadlineNs-in.observedNs<=100000000;
}
bool TargetFresh(const MagazineTracking& t,std::int64_t now,std::int64_t resourceAt)noexcept {
 if(!TrackingFresh(t,now,resourceAt)||!t.target)return false;
 const auto& v=*t.target;const auto p=t.family.binding.profile->geometry->interaction.insertion;const auto& g=v.gunClaim;
 if(t.resource&&!MagazineResourceRoleAllowed(*t.resource,v.role,t.cycle,resourceAt))return false;
 if(t.replacementFrame&&t.removalFrame)return false;
 const auto& carry=t.removalFrame?t.removalFrame:t.replacementFrame;
 if(carry){const auto& f=*carry;
  if(v.role!=(t.removalFrame?MagazinePropRole::Removed:MagazinePropRole::Replacement)||!v.handTarget||f.owner!=v.owner||f.item!=v.item||
   f.handClaim!=v.handClaim||f.gunClaim!=v.gunClaim||f.inputSequence!=v.inputSequence||f.cycle!=v.nativeCycle||
   f.observedNs!=v.observedNs||f.deadlineNs!=v.deadlineNs||!Rigid(f.weaponWorldMeters))return false;}
 if(t.detach&&(v.originalMagazine!=std::optional{t.detach->original}||
  (t.detach->restoring&&v.role!=MagazinePropRole::Attached)))return false;
 if(v.owner!=t.inputEvidence.owner||v.weapon!=t.family.binding.weapon||
 v.profile!=HandInteractionKey{p.id,p.revision}||!v.inputSequence||v.inputSequence>t.inputEvidence.sequence||
 v.trackingEpoch!=t.owner.space||v.nativeCycle!=t.cycle||v.observedNs<=0||v.observedNs>now||v.deadlineNs<=now||
 v.deadlineNs-v.observedNs>100000000||!g.id||g.owner!=v.owner||g.item!=v.weapon||
 g.hand!=InteractionHand::Right||g.kind!=HandClaimKind::GunHold)return false;
 if(v.role==MagazinePropRole::Attached)return !v.handTarget;
 if(!t.cycle)return false;
 if(v.role==MagazinePropRole::Hidden)return !v.handTarget;
 const auto& h=v.handClaim;
 return v.handTarget&&!t.inputEvidence.released[0]&&h.id&&h.id!=g.id&&h.owner==v.owner&&h.item==v.item&&h.hand==InteractionHand::Left&&
 ((v.role==MagazinePropRole::Removed&&h.kind==HandClaimKind::Mechanism&&h.item==v.weapon&&h.prerequisiteClaim==g.id&&
  ((!v.originalMagazine)||(v.originalMagazine&&v.originalMagazine->owner==v.owner&&
   v.originalMagazine->weapon==v.weapon&&v.originalMagazine->profile==v.profile&&Key(v.originalMagazine->item)&&v.originalMagazine->item!=v.weapon&&
   v.originalMagazine->trackingEpoch==v.trackingEpoch&&v.originalMagazine->capacity&&v.originalMagazine->rounds<=v.originalMagazine->capacity)))||
 (v.role==MagazinePropRole::Replacement&&h.kind==HandClaimKind::AmmoObject&&h.item!=v.weapon));
}
}
bool MagazineTrackingFresh(const MagazineTracking& t,std::int64_t now)noexcept {return TrackingFresh(t,now,now);}
bool MagazineTargetFresh(const MagazineTracking& t,std::int64_t now)noexcept {return TargetFresh(t,now,now);}
bool MagazinePaletteTargetFresh(const MagazineTracking& t,std::int64_t now)noexcept {
 if(!t.resource||!t.resource->presentationObservedNs)return MagazineTargetFresh(t,now);
 const auto admitted=t.resource->presentationObservedNs;
 if(t.detach||t.retainedVisualSuppression||admitted!=t.inputEvidence.nowNs||
    admitted<t.inputEvidence.observedNs||admitted>now)return false;
 // Validate the original admission without modifying any source timestamp,
 // then independently enforce every pose/owner/claim/selection deadline now.
 return TargetFresh(t,admitted,admitted)&&TargetFresh(t,now,admitted);
}
bool MagazineTargetRetained(const MagazineTracking& old,const MagazineTracking& current,std::int64_t now)noexcept {
 if(!MagazinePaletteTargetFresh(old,now)||!MagazinePaletteTargetFresh(current,now)||old.owner!=current.owner||
 old.family.binding!=current.family.binding||old.cycle!=current.cycle||old.reserve.identity!=current.reserve.identity||old.inputEvidence.owner!=current.inputEvidence.owner||
 old.inputEvidence.sequence>current.inputEvidence.sequence||old.inputEvidence.observedNs>current.inputEvidence.observedNs)return false;
 const auto& a=*old.target;const auto& b=*current.target;
 if(bool(old.resource)!=bool(current.resource)||(old.resource&&
    (old.resource->resource.snapshot.context!=current.resource->resource.snapshot.context||
     old.resource->resource.wellEmpty!=current.resource->resource.wellEmpty||
     old.resource->resource.original!=current.resource->resource.original)))return false;
 if(bool(old.replacementFrame)!=bool(current.replacementFrame)||bool(old.removalFrame)!=bool(current.removalFrame))return false;
 const auto& oldFrame=old.removalFrame?old.removalFrame:old.replacementFrame;
 const auto& currentFrame=current.removalFrame?current.removalFrame:current.replacementFrame;
 if(oldFrame&&a.inputSequence==b.inputSequence){const auto& x=*oldFrame;const auto& y=*currentFrame;
  if(x.observedNs!=y.observedNs||x.deadlineNs!=y.deadlineNs||x.weaponWorldMeters.values!=y.weaponWorldMeters.values)return false;}
 if(bool(old.detach)!=bool(current.detach)||(old.detach&&!MagazineDetachAuthorizationRetained(*old.detach,*current.detach,now)))return false;
 return a.role==b.role&&a.owner==b.owner&&a.weapon==b.weapon&&a.profile==b.profile&&a.item==b.item&&
 a.handClaim==b.handClaim&&a.gunClaim==b.gunClaim&&a.trackingEpoch==b.trackingEpoch&&a.nativeCycle==b.nativeCycle&&
 old.selected->weaponData==current.selected->weaponData&&old.selected->soleConfiguredArray==current.selected->soleConfiguredArray&&
 old.selected->states==current.selected->states&&old.selected->sequence<=current.selected->sequence;
}
std::optional<MagazineCarryFrame> MagazineRemovalFrame(const MagazineTracking& t,const MagazineRawContact& raw)noexcept {
 if(!t.target||t.target->role!=MagazinePropRole::Removed||!t.target->handTarget)return {};
 const auto& v=*t.target;const auto& in=raw.inputEvidence;
 if(!raw.valid||raw.owner!=t.owner||in.owner!=v.owner||in.sequence!=v.inputSequence||
  in.observedNs!=v.observedNs||in.deadlineNs<v.deadlineNs||!Rigid(raw.weaponWorldMeters))return {};
 return MagazineCarryFrame{v.owner,v.item,v.handClaim,v.gunClaim,v.inputSequence,v.nativeCycle,v.observedNs,v.deadlineNs,raw.weaponWorldMeters};
}
namespace magazine_presentation_detail {
std::optional<MagazinePresentationBinding> Derive(const RigSnapshot& rig) {return Derive(rig,Xm8MagazineGeometry());}
std::optional<MagazinePresentationBinding> Derive(const RigSnapshot& rig,const MagazineGeometryProfile& profile,bool completeCoincidentDescendants) {
 const auto count=rig.names.size();if(!count||count>1024||rig.parents.size()!=count||rig.inverseBind.size()!=count||rig.evaluatedWorld.size()!=count)return {};
 const auto named=[&](std::string_view name)->std::optional<std::uint32_t>{const auto i=std::find(rig.names.begin(),rig.names.end(),name);
 if(i==rig.names.end()||std::find(i+1,rig.names.end(),name)!=rig.names.end())return {};return unsigned(i-rig.names.begin());};
 std::array<std::string_view,18> roles{profile.bones.weapon,profile.bones.magazine,profile.bones.wrist};
 std::copy(profile.bones.fingers.begin(),profile.bones.fingers.end(),roles.begin()+3);
 for(std::size_t n=0;n<roles.size();++n)if(roles[n].empty()||std::find(roles.begin(),roles.begin()+n,roles[n])!=roles.begin()+n)return {};
 const auto root=named(profile.bones.weapon),mag=named(profile.bones.magazine),wrist=named(profile.bones.wrist);
 if(!root||!mag||!wrist||rig.weaponBone!=*root||rig.parents[*mag]!=int(*root)||!MagazineAssemblyShape(profile))return {};
 for(std::size_t n=0;n<count;++n){auto at=int(n);std::size_t steps=0;while(at!=-1){if(at<0||std::size_t(at)>=count||++steps>count)return {};at=rig.parents[at];}}
 MagazinePresentationBinding out;out.geometry=&profile;out.weapon=*root;out.magazine=*mag;out.wrist=*wrist;unsigned slot=0;
 out.assemblyCount=profile.assemblyCount;
 for(unsigned n=0;n<out.assemblyCount;++n){
  const auto bone=named(profile.assembly[n].bone),parent=named(profile.assembly[n].parent);
  if(!bone||!parent||rig.parents[*bone]!=int(*parent))return {};out.assembly[n]=*bone;out.assemblyProfileMember[n]=n;
 }
 out.fingerprint=SightRigFingerprint(rig.names,rig.parents,rig.inverseBind);
 // Authored visible-member lists can end before the shared skeleton's dormant
 // co-located controls. Close ONLY those descendants from the exact captured
 // inverse bind, never from a learned animated pose or a bone-name convention.
 // Unreviewed branches directly under the magazine remain unsupported.
 std::vector<std::uint32_t> missing;
 for(std::size_t n=0;n<count;++n){auto at=rig.parents[n];while(at!=-1&&at!=int(*mag))at=rig.parents[at];
  if(at==int(*mag)&&std::find(out.assembly.begin(),out.assembly.begin()+out.assemblyCount,n)==out.assembly.begin()+out.assemblyCount)
   missing.push_back(std::uint32_t(n));
 }
 if(!missing.empty()){
  if(!completeCoincidentDescendants||!out.fingerprint||out.fingerprint!=profile.rigFingerprint||
     missing.size()+out.assemblyCount>out.assembly.size())return {};
  while(!missing.empty()){
   bool added=false;
   for(auto i=missing.begin();i!=missing.end();){const auto bone=*i;const auto parent=rig.parents[bone];
    const auto ancestor=std::find(out.assembly.begin(),out.assembly.begin()+out.assemblyCount,unsigned(parent));
    if(ancestor==out.assembly.begin()+out.assemblyCount){++i;continue;}
    const auto bind=InverseRigid(rig.inverseBind[bone]);if(!bind||!Rigid(rig.inverseBind[parent]))return {};
    const auto local=Multiply(*bind,rig.inverseBind[parent]);
    for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)
     if(std::abs(local.values[r][c]-float(r==c))>(r==3?.000001f:.00001f))return {};
    const auto slot=out.assemblyCount++;out.assembly[slot]=bone;
    out.assemblyProfileMember[slot]=out.assemblyProfileMember[std::size_t(ancestor-out.assembly.begin())];
    i=missing.erase(i);added=true;
   }
   if(!added)return {};
  }
 }
 for(unsigned digit=0;digit<5;++digit){auto parent=*wrist;for(unsigned j=0;j<3;++j){
 const auto bone=named(profile.bones.fingers[digit*3+j]);if(!bone||rig.parents[*bone]!=int(parent))return {};
 out.fingers[slot++]=*bone;parent=*bone;}}
 for(std::size_t n=0;n<count;++n){if(n==*wrist)continue;auto at=rig.parents[n];while(at!=-1&&at!=int(*wrist))at=rig.parents[at];
 if(at==int(*wrist)&&std::find(out.fingers.begin(),out.fingers.end(),n)==out.fingers.end())return {};}
 return out.fingerprint?std::optional{out}:std::nullopt;
}
}
std::optional<MagazinePresentationBinding> BindMagazinePresentation(const RigSnapshot& rig,std::string_view asset){
 const auto equipment=FindMagazineEquipment(asset);
 return equipment?BindMagazinePresentation(rig,*equipment):std::nullopt;
}
std::optional<MagazinePresentationBinding> BindMagazinePresentation(const RigSnapshot& rig,const MagazineEquipmentProfile& equipment){
 if(!equipment.Ready())return {};
 const auto profile=equipment.geometry;
 const auto binding=magazine_presentation_detail::Derive(rig,*profile,true);
 return binding&&binding->fingerprint==profile->rigFingerprint?binding:std::nullopt;
}
MagazineRawContact BuildMagazineRawContact(const MagazineTracking& t,const RigSnapshot& rig,std::string_view asset,
 const math::Matrix4& wrist,const math::Matrix4& weapon,float units,std::int64_t now,const math::Matrix4* trackingBody){
 MagazineRawContact out;
 const auto reject=[&](MagazineRawContactReason reason){out.evidence.reason=reason;return out;};
 if(t.retainedVisualSuppression)return reject(MagazineRawContactReason::RetainedVisualSuppression);
 if(!MagazineTrackingFresh(t,now))return reject(MagazineRawContactReason::TrackingNotFresh);
 const auto& geometry=*t.family.binding.profile->geometry;
 const auto binding=magazine_presentation_detail::Derive(rig,geometry,true);
 if(asset!=geometry.asset)return reject(MagazineRawContactReason::AssetMismatch);
 if(!binding)return reject(MagazineRawContactReason::RigBindingUnavailable);
 out.evidence.magazineBone=binding->magazine;
 if(binding->fingerprint!=geometry.rigFingerprint)return reject(MagazineRawContactReason::RigFingerprintMismatch);
 if(rig.identity.soldier!=t.owner.soldier||rig.identity.weak!=t.owner.weak)return reject(MagazineRawContactReason::RigOwnerMismatch);
 if(!std::isfinite(units)||units<=0)return reject(MagazineRawContactReason::InvalidUnits);
 if(!Rigid(wrist))return reject(MagazineRawContactReason::WristNotRigid);
 if(!Rigid(weapon))return reject(MagazineRawContactReason::WeaponNotRigid);
 if(!AssemblyVisibleRigid(rig,*binding,units,&out.evidence))return out;
 out.evidence.reason=MagazineRawContactReason::Accepted;
 out.valid=true;out.owner=t.owner;out.rig=rig.identity;out.rigFingerprint=binding->fingerprint;out.inputEvidence=t.inputEvidence;
 out.rawLeftWristWorldMeters=Units(wrist,1/units);out.weaponWorldMeters=Units(weapon,1/units);
 if(trackingBody&&Rigid(*trackingBody))out.trackingBodyWorldMeters=Units(*trackingBody,1/units);
 if(Rigid(rig.evaluatedWorld[binding->weapon])&&Rigid(rig.evaluatedWorld[binding->magazine])){
 const auto relative=Units(Multiply(rig.evaluatedWorld[binding->magazine],*InverseRigid(rig.evaluatedWorld[binding->weapon])),1/units);
 out.nativeMagazineAttached=Distance(relative,geometry.attachedItem)<.01f&&
 Angle(relative,geometry.attachedItem)<.1f;}
 return out;
}
MagazineRawContact AdmitMagazineRawContact(const MagazineTracking* guard,const ReloadStateOwner& owner,
 const MagazineRawContact* frame,std::uint64_t frameInput,std::int64_t frameDeadlineNs,std::int64_t now)noexcept {
 const auto reject=[](MagazineRawContactReason reason){return RejectedMagazineRawContact(reason);};
 if(!guard)return reject(MagazineRawContactReason::GuardMissing);
 if(guard->owner!=owner)return reject(MagazineRawContactReason::GuardOwnerMismatch);
 if(!MagazineTrackingFresh(*guard,now))return reject(MagazineRawContactReason::GuardNotFresh);
 if(!frame||frameDeadlineNs<=now)return reject(MagazineRawContactReason::FrameUnavailable);
 // Rejected builder packets are diagnostics only. Their empty authority fields
 // are expected, and must not conceal the actual rejection with an owner error.
 if(!frame->valid)return *frame;
 if(frame->owner!=owner)return reject(MagazineRawContactReason::ContactOwnerMismatch);
 const auto& input=frame->inputEvidence;
 if(input.sequence!=frameInput)return reject(MagazineRawContactReason::FrameInputMismatch);
 if(input.sequence>guard->inputEvidence.sequence)return reject(MagazineRawContactReason::InputNewerThanGuard);
 if(input.observedNs>now)return reject(MagazineRawContactReason::InputInFuture);
 if(input.deadlineNs<=now)return reject(MagazineRawContactReason::InputExpired);
 return *frame;
}
MagazinePresentationPlan BuildMagazinePresentation(const RigSnapshot& rig,const MagazinePresentationBinding& binding,
 const MagazineTracking& t,const math::Matrix4& weapon,float units,std::int64_t now){
 MagazinePresentationPlan out;if(!MagazinePaletteTargetFresh(t,now))return out;
 const auto& geometry=*t.family.binding.profile->geometry;
 const auto actual=magazine_presentation_detail::Derive(rig,geometry,true);
 if(!actual||*actual!=binding||!MagazinePaletteTargetFresh(t,now)||rig.identity.soldier!=t.owner.soldier||rig.identity.weak!=t.owner.weak||
 !std::isfinite(units)||units<=0||!Rigid(weapon)||!AssemblyVisibleRigid(rig,binding,units)||
 std::find(rig.nativeHiddenLeaves.begin(),rig.nativeHiddenLeaves.end(),binding.magazine)!=rig.nativeHiddenLeaves.end())return out;
 const auto& v=*t.target;if(!Rigid(v.weaponFromItemMeters)||(v.handTarget&&!Rigid(v.weaponFromHandMeters)))return out;
 out.binding=binding;out.hideMagazine=v.role==MagazinePropRole::Hidden;if(out.hideMagazine)return out;
 const auto& carry=t.removalFrame?t.removalFrame:t.replacementFrame;
 const auto frame=carry?Units(carry->weaponWorldMeters,units):weapon;
 const auto itemWorld=Multiply(Units(v.weaponFromItemMeters,units),frame);
 out.writes.push_back({binding.magazine,itemWorld});
 for(unsigned n=0;n<binding.assemblyCount;++n)out.writes.push_back({binding.assembly[n],
  Multiply(Units(geometry.assembly[binding.assemblyProfileMember[n]].itemFromBone,units),itemWorld)});
 if(v.handTarget){
 // Removal motion/contact keeps its independently sampled raw wrist. The
 // displayed hand must use the SAME authored magazine grasp as these fingers,
 // rather than the arbitrary in-radius wrist offset captured on initial grab.
 const auto hand=v.role==MagazinePropRole::Removed?
  Multiply(geometry.interaction.insertion.itemFromHand,v.weaponFromItemMeters):v.weaponFromHandMeters;
 const auto wrist=Multiply(Units(hand,units),frame);out.wristTarget=wrist;
 out.writes.push_back({binding.wrist,wrist});for(unsigned n=0;n<15;++n)out.writes.push_back({binding.fingers[n],
 Multiply(Units(geometry.wristFromFinger[n],units),wrist)});}
 return out;
}
std::optional<std::vector<std::array<std::byte,64>>> HideMagazinePackedPalette(const RigSnapshot& rig,
 const MagazinePresentationBinding& binding,std::span<const std::array<std::byte,64>> ordinary){
 if(!binding.geometry)return {};
 const auto actual=magazine_presentation_detail::Derive(rig,*binding.geometry,true);if(!actual||*actual!=binding||ordinary.size()!=rig.names.size())return {};
 std::array<std::uint32_t,1+MagazineAssemblyLimit> indices{binding.magazine};
 std::copy_n(binding.assembly.begin(),binding.assemblyCount,indices.begin()+1);
 return weapon_visibility_detail::CollapseWeightedPalette(ordinary,std::span(indices).first(1+binding.assemblyCount));
}
ReloadPackedPaletteChoice SelectMagazinePackedPalette(const MagazineTracking& old,const MagazineTracking* current,std::int64_t now,
 std::span<const std::array<std::byte,64>> posed,std::span<const std::array<std::byte,64>> ordinary,bool coherent)noexcept{
 if(!posed.empty()&&posed.size()<=1024&&current&&coherent&&MagazineTargetRetained(old,*current,now))return {posed,false};
 if(!posed.empty()&&posed.size()<=1024&&ordinary.size()==posed.size())return {ordinary,true};return {{},true};
}
std::uint64_t ClassifyMagazinePaletteFallback(const MagazineTracking& old,const MagazineTracking* current,
 std::int64_t now,std::span<const std::array<std::byte,64>> posed,bool coherent)noexcept {
 std::uint64_t flags=0;
 const auto expired=[&](const MagazineTracking& t){
  const auto past=[&](std::int64_t deadline){return deadline>0&&now>=deadline;};
  const bool poseAdmission=t.resource&&t.resource->presentationObservedNs>0;
  return past(t.inputEvidence.deadlineNs)||(!poseAdmission&&past(t.reserve.deadlineNs))||past(t.family.deadlineNs)||
   (t.selected&&past(t.selected->deadlineNs))||(t.target&&past(t.target->deadlineNs))||
   (t.removalFrame&&past(t.removalFrame->deadlineNs))||(t.replacementFrame&&past(t.replacementFrame->deadlineNs));
 };
 if(posed.empty()||posed.size()>1024)flags|=MagazineFallbackBit(MagazineFallbackReason::InvalidPalette);
 if(!current)flags|=MagazineFallbackBit(MagazineFallbackReason::CurrentMissing);
 if(!coherent)flags|=MagazineFallbackBit(MagazineFallbackReason::ShotIncoherent);
 if(expired(old))flags|=MagazineFallbackBit(MagazineFallbackReason::OldExpired);
 if(current&&expired(*current))flags|=MagazineFallbackBit(MagazineFallbackReason::CurrentExpired);
 if(!flags&&current&&!MagazineTargetRetained(old,*current,now))flags|=MagazineFallbackBit(MagazineFallbackReason::RetentionMismatch);
 return flags;
}
} // namespace fvr::bc2
