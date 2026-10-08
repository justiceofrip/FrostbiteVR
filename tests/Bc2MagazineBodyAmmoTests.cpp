#include "Bc2MagazineBodyAmmo.h"
#include "Bc2MagazineConsumerFixture.h"

using namespace magazine_consumer_fixture;
namespace {
MagazinePhysicalResult DetachedIdle(Fixture& f,Bc2MagazineDetached& detached,
 const std::optional<Bc2AmmoReserveLease>& post) {
 detached.Prepare(f.s,f.reserve,f.s.input.sequence,0xe0000,f.hands);
 return detached.Commit({}, {},post,f.hands,f.intent,f.now,f.now);
}
std::optional<AmmoSupplyVisualSample> Display(Fixture& f,const MagazineTracking& tracking,std::int64_t now=0) {
 return f.policy->BodyAmmoDisplay(tracking,f.hands.Current(InteractionHand::Right),now?now:f.now);
}
int FullMagazineUsesSameSourceAcrossRoutes(){
 Fixture f;f.policy->EnableBodyAmmo(true,SupplyAnchorFrame::RecenteredBody);
 f.reserve.loaded=f.reserve.capacity;f.reserve.allThreeIdle=true;f.reserve.reloadInputReady=false;f.Send();
 Bc2MagazineDetached detached{true};CHECK(detached.Routes(f.reserve,false));
 const auto committed=DetachedIdle(f,detached,f.reserve);
 CHECK(MagazineTrackingFresh(committed.tracking,f.now)&&!committed.tracking.detach);
 const auto visual=Display(f,committed.tracking);CHECK(visual&&f.result.bodyAmmo);
 CHECK(visual->source==f.result.bodyAmmo->source&&visual->gun.token==f.result.bodyAmmo->gun.token);
 CHECK(visual->source.objectUnits==30&&visual->source.reserveUnits==83);
 CHECK(visual->source.observedNs==f.reserve.observedNs&&visual->source.deadlineNs==f.reserve.deadlineNs);
 CHECK(visual->input.sequence==committed.tracking.inputEvidence.sequence&&
  visual->input.deadlineNs==committed.tracking.inputEvidence.deadlineNs);
 CHECK(visual->frame==SupplyAnchorFrame::RecenteredBody);
 // A failed initial routing read picks physical Tick. Display does not change
 // when that route succeeds using the same final owner/count publication.
 CHECK(!detached.Routes({},false));CHECK(Display(f,f.result.tracking)->source==visual->source);
 CHECK(f.starts==0&&f.submits==0&&f.cancels==0);return 0;
}
int PartialFullZeroTransitionsUseFinalPublication(){
 Fixture f;f.policy->EnableBodyAmmo();Bc2MagazineDetached detached{true};
 std::optional<AmmoSupplyVisualSample> prior;
 for(const auto counts:{std::pair{27,83},std::pair{30,80},std::pair{29,80},std::pair{30,2},
                       std::pair{27,0},std::pair{30,0},std::pair{30,80}}){
  f.reserve.loaded=counts.first;f.reserve.reserve=counts.second;f.reserve.allThreeIdle=true;
  f.reserve.reloadInputReady=counts.first<30&&counts.second>0;f.Send();
  const bool routes=detached.Routes(f.reserve,false);
  const auto final=routes?DetachedIdle(f,detached,f.reserve):f.result;
  const auto visual=Display(f,final.tracking);CHECK(bool(visual)==(counts.second>0));
  if(visual){
   CHECK(BodyAmmoFresh({*visual,final.tracking,{}},f.now));
   CHECK(visual->source.identity.owner==f.s.input.owner&&visual->gun.token==f.gun->token);
   CHECK(visual->source.objectUnits==unsigned(std::min(counts.first==30?30:30-counts.first,counts.second)));
   CHECK(visual->source.reserveUnits==unsigned(counts.second)&&visual->source.sequence==f.reserve.sequence);
   CHECK(visual->source.deadlineNs==f.reserve.deadlineNs);
   if(prior)CHECK(visual->source.identity==prior->source.identity);
   prior=visual;
  }
  // Missing final evidence is absence. A previous available prop cannot be
  // resurrected while final Commit lacks reserve, even before old expiry.
  CHECK(!Display(f,{}));
  if(routes){const auto missing=DetachedIdle(f,detached,{});CHECK(!Display(f,missing.tracking));}
 }
 CHECK(f.starts==0&&f.submits==0&&f.cancels==0);return 0;
}
int CurrentOwnerClaimsAndOriginalDeadlines(){
 Fixture f;f.policy->EnableBodyAmmo();f.Send();const auto baseline=f.result.tracking;
 CHECK(Display(f,baseline));
 for(unsigned fault=0;fault<14;++fault){
  auto tracking=baseline;auto gun=f.gun;
  if(fault==0)tracking.enabled=false;
  if(fault==1)tracking.reserve.verified=false;
  if(fault==2)tracking.reserve.deadlineNs=f.now;
  if(fault==3)tracking.family.deadlineNs=f.now;
  if(fault==4)tracking.inputEvidence.deadlineNs=f.now;
  if(fault==5)gun->deadlineNs=f.now;
  if(fault==6)++tracking.owner.equipGeneration;
  if(fault==7)++tracking.inputEvidence.owner.space;
  if(fault==8)++gun->token.item.generation;
  if(fault==9)++tracking.inputEvidence.sequence;
  if(fault==10)tracking.inputEvidence.focused=false;
  if(fault==11){auto selected=std::make_shared<SelectedMeshesSnapshot>(*tracking.selected);
   selected->deadlineNs=f.now;tracking.selected=selected;}
  if(fault==12)tracking.detach=std::make_shared<MagazineDetachAuthorization>();
  if(fault==13)tracking.reserve.reloadInputReady=tracking.reserve.allThreeIdle=false;
  CHECK(!f.policy->BodyAmmoDisplay(tracking,gun,f.now));
 }
 CHECK(Display(f,baseline,baseline.reserve.deadlineNs-1));
 CHECK(!Display(f,baseline,baseline.reserve.deadlineNs));
 f.hands.Release(f.s.input,f.gun->token);CHECK(!Display(f,baseline));
 f.policy->EnableBodyAmmo(false);CHECK(!f.policy->BodyAmmoDisplay(baseline,f.gun,f.now));return 0;
}
int HeldLeaseAndInteractionOccupancy(){
 Fixture f;f.policy->EnableBodyAmmo();f.Send();auto tracking=f.result.tracking;tracking.cycle=9;
 ReloadMagazineLease held{tracking.reserve.identity,9,10,f.now,f.now+100*Ms,
  tracking.reserve.loaded,tracking.reserve.reserve,tracking.reserve.capacity,true,true};
 const AmmoSupplyContact contact{{0,0,0},.15f};
 const auto build=[&](const ReloadMagazineLease* lease){return BuildMagazineBodyAmmo(tracking,f.gun,contact,SupplyAnchorFrame::HeadYaw,f.now,lease);};
 CHECK(build(&held));CHECK(!build(nullptr));
 for(unsigned fault=0;fault<7;++fault){auto wrong=held;
  if(fault==0)wrong.allThreeHeld=false;if(fault==1)wrong.nativeBindingVerified=false;
  if(fault==2)++wrong.identity.owner.weapon;if(fault==3)++wrong.cycle;
  if(fault==4)++wrong.reserve;if(fault==5)wrong.deadlineNs=f.now;if(fault==6)wrong.sequence=0;
  CHECK(!build(&wrong));
 }
 for(const auto role:{MagazinePropRole::Removed,MagazinePropRole::Replacement}){
  tracking.target=MagazinePropTarget{};tracking.target->role=role;CHECK(!build(&held));
 }
 tracking.target=MagazinePropTarget{};tracking.target->role=MagazinePropRole::Attached;
 tracking.target->handTarget=true;CHECK(!build(&held));
 // Actual consumer supply ownership stays authoritative, independently of
 // an otherwise valid tracking sample. Getter cannot clear or advance it.
 Fixture cycle;cycle.policy->EnableBodyAmmo();CHECK(cycle.Eject());CHECK(Display(cycle,cycle.result.tracking));
 cycle.s.bodyFromHand=Pose();cycle.Send(true,false,-.1f);CHECK(cycle.result.ownsLeftHand);
 CHECK(!Display(cycle,cycle.result.tracking));
 Fixture pending;pending.policy->EnableBodyAmmo();CHECK(pending.Insert());
 CHECK(pending.policy->ProbeState(pending.now).pending);const auto submitted=pending.submits;
 CHECK(!Display(pending,pending.result.tracking)&&pending.submits==submitted);
 pending.policy->Cancel(pending.s.input,pending.hands);CHECK(!Display(pending,pending.result.tracking));
 return 0;
}
}
int main(){
 if(FullMagazineUsesSameSourceAcrossRoutes()||PartialFullZeroTransitionsUseFinalPublication()||
    CurrentOwnerClaimsAndOriginalDeadlines()||HeldLeaseAndInteractionOccupancy())return 1;
 std::cout<<"MagazineBodyAmmo: 4 CPU groups passed; display-only source, no headset acceptance\n";
}
