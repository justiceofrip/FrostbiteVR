#include "Bc2MagazineInteraction.h"
namespace fvr::bc2 {
namespace {
using namespace interaction;
bool Fresh(std::int64_t a,std::int64_t b,std::int64_t now)noexcept{return a>0&&a<=now&&b>now&&b-a<=200000000;}
bool Identity(const ReloadHoldIdentity& n)noexcept{const auto& o=n.owner;return o.player>=0x10000&&o.soldier>=0x10000&&
 o.weak>=0x10000&&o.weapon>=0x10000&&o.actorGeneration&&o.equipGeneration&&o.space&&n.serverPlayer>=0x10000&&
 n.serverSoldier>=0x10000&&n.serverItem>=0x10000&&n.firing[0]>=0x10000&&n.firing[1]>=0x10000&&n.firing[2]>=0x10000&&
 n.firing[0]!=n.firing[1]&&n.firing[0]!=n.firing[2]&&n.firing[1]!=n.firing[2];}
bool Lease(const Bc2MagazineOwnerMap& m,const ReloadMagazineLease& n,std::int64_t now)noexcept{
 return MagazineFamilyFresh(m.family,m.native.owner,m.physical,m.weapon,now)&&n.nativeBindingVerified&&n.identity==m.native&&n.cycle==m.cycle&&n.cycle&&n.sequence&&
 Fresh(n.observedNs,n.deadlineNs,now)&&n.loaded>=0&&n.capacity>0&&n.loaded<=n.capacity&&n.reserve>=0;}
}
interaction::DetachableMagazineConfig Xm8MagazineConfig()noexcept {return Xm8MagazineGeometry().interaction;}
bool MagazineSelected(const SelectedMeshesSnapshot& s,const ReloadStateOwner& o,std::string_view asset,
 const MagazineEquipmentProfile& equipment,std::int64_t now)noexcept {
 if(!equipment.Ready()||asset!=equipment.geometry->asset)return false;
 const auto& profile=*equipment.geometry;
 if(profile.meshKind!=SelectedMeshKind::Unknown){
  const auto mesh=FindSelectedMesh(s,o,profile.meshKind,now);
  return mesh&&std::find(mesh->assetPath.begin(),mesh->assetPath.end(),'\0')!=mesh->assetPath.end()&&
   std::string_view(mesh->assetPath.data())==profile.mesh;
 }
 // A new exact measured mesh needs no fabricated enum classification. Preserve
 // the sole-state, unique-match, owner and original-deadline restrictions.
 if(s.owner!=o||now<s.observedNs||now>=s.deadlineNs||s.stateCount!=1||!s.soleConfiguredArray||s.states[0].count>8)return false;
 unsigned matches=0;
 for(unsigned n=0;n<s.states[0].count;++n){const auto& mesh=s.states[0].meshes[n];
  if(std::find(mesh.assetPath.begin(),mesh.assetPath.end(),'\0')==mesh.assetPath.end())return false;
  if(std::string_view(mesh.assetPath.data())==profile.mesh)++matches;
 }
 return matches==1;
}
bool Xm8MagazineSelected(const SelectedMeshesSnapshot& s,const ReloadStateOwner& o,std::string_view asset,std::int64_t now)noexcept {
 return MagazineSelected(s,o,asset,Xm8MagazineEquipment(),now);
}
bool MagazineFamilyFresh(const MagazineFamilyEvidence& e,const ReloadStateOwner& o,const HandInteractionOwner& p,
 HandInteractionKey weapon,std::int64_t now)noexcept {
 const auto& b=e.binding;
 if(!e.verified||!Fresh(e.observedNs,e.deadlineNs,now)||b.owner!=o||b.weapon!=weapon||!b.profile||!b.profile->Ready()||
  weapon.id<0x10000||weapon.id>UINT32_MAX||weapon.generation!=p.equipGeneration||b.inventory<0x10000||
  p.actor!=((std::uint64_t(o.weak)<<32)|o.soldier)||p.actorGeneration!=o.actorGeneration||p.space!=o.space||!p.equipGeneration)return false;
 // Dispatch by the actual hand identity. A native item needs no launcher
 // relationship; an unequal alias still needs the explicitly supported route.
 if(weapon.id!=o.weapon)
  return b.profile->native->identityRoute==MagazineIdentityRoute::LinkedLauncherAlias&&
   !e.carried&&b.equipment==WeaponEquipmentIdentity{}&&b.launcher>=0x10000&&
   b.launcher!=o.weapon&&b.launcher!=weapon.id&&b.launcherSlot<9;
 return e.carried&&SelectedCarriedWeaponFresh(*e.carried,o,p,weapon,now)&&
  e.carried->observedNs==e.observedNs&&e.carried->deadlineNs==e.deadlineNs&&e.carried->inventory==b.inventory&&
  e.carried->equipment==b.equipment&&b.equipment.Asset()==b.profile->geometry->asset&&!b.launcher&&!b.launcherSlot;
}
std::optional<MagazineFamilyEvidence> ResolveMagazineFamily(const WeaponModeMemory& memory,const ReloadStateOwner& o,
 const HandInteractionSample& in,HandInteractionKey weapon){return ResolveMagazineFamily(memory,o,in,weapon,Xm8MagazineEquipment());}
std::optional<MagazineFamilyEvidence> ResolveMagazineFamily(const WeaponModeMemory& memory,const ReloadStateOwner& o,
 const HandInteractionSample& in,HandInteractionKey weapon,const MagazineEquipmentProfile& profile){
 if(!profile.Ready()||!in.sequence||!in.focused||!in.tracked[0]||!in.tracked[1]||in.released[1]||
  !Fresh(in.observedNs,in.deadlineNs,in.nowNs))return {};
 MagazineFamilyEvidence e;e.observedNs=in.observedNs;e.deadlineNs=in.deadlineNs;e.verified=true;
 e.binding.owner=o;e.binding.weapon=weapon;e.binding.profile=&profile;
 if(weapon.id!=o.weapon){
  if(profile.native->identityRoute!=MagazineIdentityRoute::LinkedLauncherAlias)return {};
  const auto family=ResolveWeaponMode(memory,o.soldier,o.weapon);
  if(!family||family->action!=33||family->persistent!=weapon.id)return {};
  e.binding.inventory=family->inventory;e.binding.launcher=family->targetWeapon;e.binding.launcherSlot=family->targetSlot;
 }else{
  e.carried=ReadSelectedCarriedWeapon(memory,o,in,weapon,true);
  if(!e.carried)return {};
  e.binding.inventory=e.carried->inventory;e.binding.equipment=e.carried->equipment;
 }
 return MagazineFamilyFresh(e,o,in.owner,weapon,in.nowNs)?std::optional{e}:std::nullopt;
}
interaction::ManualReloadOwner MagazinePhysicalOwner(const Bc2ReloadOwnerMap& m)noexcept{
 return {m.physical.actor,m.physical.actorGeneration,m.weapon.id,m.physical.equipGeneration,m.physical.space};}
interaction::ManualReloadOwner MagazineNativeOwner(const Bc2ReloadOwnerMap& m)noexcept{
 const auto& o=m.native.owner;return {o.soldier,o.actorGeneration,o.weapon,o.equipGeneration,o.space};}
std::optional<Bc2MagazineOwnerMap> BindMagazineOwners(const HandInteractionOwner& p,HandInteractionKey weapon,
 const Bc2AmmoReserveLease& n,std::uint64_t cycle,std::int64_t now,const MagazineFamilyEvidence& family)noexcept{
 const auto& o=n.identity.owner;
 if(!n.verified||!n.sequence||!Identity(n.identity)||!Fresh(n.observedNs,n.deadlineNs,now)||n.loaded<0||n.capacity<=0||
 n.loaded>n.capacity||n.reserve<0||p.actor!=((std::uint64_t(o.weak)<<32)|o.soldier)||p.actorGeneration!=o.actorGeneration||
 !p.equipGeneration||p.space!=o.space||!MagazineFamilyFresh(family,o,p,weapon,now)||weapon.generation!=p.equipGeneration)return {};
 return Bc2MagazineOwnerMap{{n.identity,p,weapon,cycle},family};
}
std::optional<AmmoSupplySource> MagazineSupply(const Bc2MagazineOwnerMap& m,const Bc2AmmoReserveLease& n,
 std::uint64_t epoch,std::int64_t now,unsigned pendingUnits)noexcept{
 const auto owners=BindMagazineOwners(m.physical,m.weapon,n,m.cycle,now,m.family);
 if(!owners||*owners!=m||!epoch)return {};
 const auto p=m.family.binding.profile->geometry->interaction.insertion;
 // BC2 uses a pooled round reserve. This object represents exactly one native
 // refill cost, not a tracked partial magazine or a new stock of ammunition.
 const auto units=pendingUnits?pendingUnits:unsigned(std::min(n.capacity-n.loaded,n.reserve));
 if(!units||units>unsigned(n.capacity))return {};
 return AmmoSupplySource{{m.physical,m.weapon,{p.id,p.revision},{m.native.serverItem,m.native.owner.equipGeneration},epoch},
 ReloadInsertionFamily::Magazine,unsigned(n.reserve),units,n.sequence,n.observedNs,n.deadlineNs,true};
}
std::optional<AmmoSupplySource> MagazineBodySupply(const Bc2MagazineOwnerMap& m,const Bc2AmmoReserveLease& n,
 std::uint64_t epoch,std::int64_t now)noexcept {
 // Retain existing partial/empty display identity; only full capacity has no
 // refill cost. Display its available reserve prop without minting a refill.
 if(n.loaded!=n.capacity)return MagazineSupply(m,n,epoch,now);
 if(n.reserve<=0||n.capacity<=0)return {};
 return MagazineSupply(m,n,epoch,now,unsigned(std::min(n.capacity,n.reserve)));
}
std::optional<AmmoSupplySource> Xm8MagazineSupply(const Bc2MagazineOwnerMap& m,const Bc2AmmoReserveLease& n,
 std::uint64_t epoch,std::int64_t now,unsigned pendingUnits)noexcept{return MagazineSupply(m,n,epoch,now,pendingUnits);}
std::optional<MagazineNativeObservation> MagazineGateObservation(const Bc2MagazineOwnerMap& m,
 const ReloadMagazineGateAcknowledgement& g,const ManualReloadRequest& request,std::int64_t now)noexcept{
 if(!Lease(m,g.lease,now)||!g.lease.allThreeHeld||request.owner!=MagazinePhysicalOwner(m)||!request.id||
 request.operation!=ReloadOperation::UnseatMagazine||g.semantic.request!=request.id||g.semantic.owner!=MagazineNativeOwner(m)||
 g.semantic.operation!=request.operation||g.semantic.status!=ReloadAcknowledgement::Applied)return {};
 return MagazineNativeObservation{m.physical,m.weapon,m.cycle,g.lease.observedNs,g.lease.deadlineNs,true,true,true,
 {request.id,request.owner,request.operation,ReloadAcknowledgement::Applied}};
}
std::optional<ReloadMagazineNativeRequest> MagazineSeatRequest(const Bc2MagazineOwnerMap& m,const ReloadMagazineLease& n,
 const AmmoSupplyReservation& p,const ManualReloadRequest& request,std::int64_t now)noexcept{
 if(!Lease(m,n,now))return {};
 const auto profile=m.family.binding.profile->geometry->interaction.insertion;
 if(!n.allThreeHeld||p.identity.owner!=m.physical||p.identity.weapon!=m.weapon||
 p.identity.pool!=HandInteractionKey{m.native.serverItem,m.native.owner.equipGeneration}||
 p.identity.profile!=HandInteractionKey{profile.id,profile.revision}||!p.identity.trackingEpoch||
 p.cycle!=m.cycle||!p.seat||!p.item.id||!p.item.generation||!p.claim.id||p.claim.owner!=m.physical||p.claim.item!=p.item||
 p.claim.hand!=InteractionHand::Left||p.claim.kind!=HandClaimKind::AmmoObject||
 p.request!=request.id||request.owner!=MagazinePhysicalOwner(m)||request.operation!=ReloadOperation::SeatMagazine||
 p.operation!=request.operation||p.startedNs>now||!p.units||p.units!=unsigned(std::min(n.capacity-n.loaded,n.reserve))||
 p.reserveBefore!=unsigned(n.reserve))return {};
 auto native=request;native.owner=MagazineNativeOwner(m);
 return ReloadMagazineNativeRequest{native,n,{p.item,p.claim,p.seat,p.request,p.cycle},p.units};
}
std::optional<AmmoSupplyReceipt> MagazineSupplyReceipt(const Bc2MagazineOwnerMap& m,const AmmoSupplyReservation& p,
 const ReloadMagazineLease& before,const ReloadMagazineAckEvidence& e,const Bc2AmmoReserveLease& current,std::int64_t now)noexcept{
 const auto& a=e.acknowledgement;const auto owners=BindMagazineOwners(m.physical,m.weapon,current,m.cycle,now,m.family);
 const auto source=MagazineSupply(m,current,p.identity.trackingEpoch,now,p.units);
 if(!owners||*owners!=m||!source||source->identity!=p.identity||!e.verified||!a.serverInvocation||
 a.identity!=m.native||a.cycle!=m.cycle||p.cycle!=m.cycle||!a.sampleSequence||a.sampleSequence<before.sequence||
 !Fresh(e.observedNs,e.deadlineNs,now)||e.observedNs<p.startedNs||!before.nativeBindingVerified||!before.allThreeHeld||
 before.identity!=m.native||before.cycle!=m.cycle||a.loadedBefore!=before.loaded||a.reserveBefore!=before.reserve||
 p.reserveBefore!=unsigned(before.reserve)||!p.units||p.units!=unsigned(std::min(before.capacity-before.loaded,before.reserve))||
 a.loadedAfter!=a.loadedBefore+int(p.units)||a.reserveAfter!=a.reserveBefore-int(p.units)||
 current.loaded!=a.loadedAfter||current.reserve!=a.reserveAfter||current.capacity!=before.capacity||
 current.observedNs<e.observedNs||current.sequence<=p.sourceSequence||a.semantic.request!=p.request||
 a.semantic.owner!=MagazineNativeOwner(m)||a.semantic.operation!=ReloadOperation::SeatMagazine||
 p.operation!=a.semantic.operation||a.semantic.status!=ReloadAcknowledgement::Applied)return {};
 return AmmoSupplyReceipt{p,{p.request,MagazinePhysicalOwner(m),p.operation,ReloadAcknowledgement::Applied},*source,
 a.serverInvocation,e.observedNs,e.deadlineNs,true};
}
} // namespace fvr::bc2
