#include "Bc2MagazineConsumerFixture.h"
#include "Bc2MagazineRegistryFixture.generated.h"
#include "Bc2ReloadNativePolicy.h"
#include "Bc2ReloadRetirement.h"
#include "Bc2MagazineNativeAdapter.h"
using namespace magazine_consumer_fixture;
namespace {
ReloadObservedConfig Config(const MagazineNativeProfile& profile){const auto& d=profile.configuration;const auto& v=d.values;
 ReloadObservedConfig c;c.weaponData=0x210000;c.firingData=0xc0000;c.primaryFire=0xd0000;c.ammoAddress=c.primaryFire+0x170;
 std::memcpy(c.assetName.data(),d.assetName.data(),d.assetName.size());std::memcpy(c.assetPath.data(),d.assetPath.data(),d.assetPath.size());
 c.fireLogicType=v.fireLogicType;c.reloadType=v.reloadType;c.fireInputAction=v.fireInputAction;c.reloadInputAction=v.reloadInputAction;
 c.baseCapacity=v.baseCapacity;c.numberOfMagazines=v.numberOfMagazines;c.reloadDelay=v.reloadDelay;c.reloadTime=v.reloadTime;
 c.reloadThreshold=v.reloadThreshold;c.postReloadTime=v.postReloadTime;c.boltDelay=v.boltDelay;c.boltTime=v.boltTime;
 c.holdBoltUntilFireRelease=v.holdBoltUntilFireRelease;c.holdBoltUntilZoomRelease=v.holdBoltUntilZoomRelease;return c;
}
const MagazineNativeRegistration* Row(std::string_view path){for(const auto& row:RegisteredMagazineNativeProfiles())
 if(row.profile->configuration.assetPath==path)return &row;return nullptr;}
int ExactPathAndDisabled(){const auto a=Row("Objects/Weapons/A"),b=Row("Objects/Weapons/B"),off=Row("Objects/Weapons/Disabled");CHECK(a&&b&&off);
 CHECK(a->id!=b->id&&std::uint64_t(a->id)>255&&std::uint64_t(b->id)>255);
 const auto ga=FindMagazineEquipment(a->id),gb=FindMagazineEquipment(b->id);
 CHECK(ga&&gb&&ga->Ready()&&gb->Ready()&&ga->geometry!=gb->geometry);
 CHECK(ga->geometry->configurationPath==a->profile->configuration.assetPath&&
       gb->geometry->configurationPath==b->profile->configuration.assetPath);
 CHECK(!FindMagazineNativeProfile("registry_rifle")&&!FindMagazineEquipment("registry_rifle"));
 CHECK(FindMagazineNativeProfile(Config(*a->profile))==a&&FindMagazineNativeProfile(Config(*b->profile))==b);
 CHECK(!ResolveMagazineNativeProfile(off->id)&&!FindMagazineEquipment(off->id)&&!FindMagazineNativeProfile(Config(*off->profile)));
 auto wrong=Config(*a->profile);wrong.assetPath[0]='x';CHECK(!FindMagazineNativeProfile(wrong));
 wrong=Config(*a->profile);++wrong.baseCapacity;CHECK(!FindMagazineNativeProfile(wrong));
 CHECK(&Xm8MagazineGeometry()==FindMagazineEquipment(NativeMagazineProfileId::ScopedXm8)->geometry);return 0;}
struct Consumer:Fixture {
 Bc2ReloadNativePolicy selector;ReloadObservedConfig observed{};
 Consumer(const MagazineEquipmentProfile& p):Fixture(true,p,true),observed(Config(*p.native)){
  reserve.capacity=p.native->configuration.values.baseCapacity;reserve.loaded=reserve.capacity-3;reserve.reserve=83;
  if(!selector.SelectMagazineProfile(p.nativeId,true,false))throw std::runtime_error("fixture profile selection failed");
  api.reserve=[](void* context)noexcept->std::optional<Bc2AmmoReserveLease>{auto& f=*static_cast<Consumer*>(context);
   return f.source&&f.selector.MatchesSelectedConfig(f.observed)?std::optional{f.reserve}:std::nullopt;};
  api.context=this;policy.reset();policy.emplace(true,api,AmmoSupplyConfig{InteractionHand::Left,1000,{1001,1},{0,0,0},.15f,200*Ms});
 }
};
int ActualConsumers(){for(const auto path:{"Objects/Weapons/A","Objects/Weapons/B","Objects/Weapons/Drum"}){
 const auto row=Row(path);CHECK(row);const auto p=FindMagazineEquipment(Config(*row->profile));CHECK(p&&p->Ready()&&p->nativeId==row->id&&p->experimentalGeometry&&!p->geometryVerified);
 Consumer f(*p);const auto loaded=f.reserve.loaded;CHECK(f.Insert());CHECK(f.submits==1&&f.submitted->reservedUnits==3);
 CHECK(f.result.tracking.family.carried&&!f.result.tracking.family.binding.launcher);
 CHECK(f.result.tracking.family.binding.profile==p);
 f.Complete();f.Send();CHECK(f.result.completed==1&&f.reserve.loaded==loaded+3&&f.reserve.reserve==80);
 f.allowRetire=true;f.Send();f.Send();CHECK(!f.policy->BlocksEquipment());
 Consumer denied(*p);denied.observed.assetPath[0]='x';denied.Send();denied.Send(true);CHECK(denied.starts==0&&!denied.result.tracking.enabled);
 denied.observed=Config(*p->native);denied.Send();CHECK(MagazineTrackingFresh(denied.result.tracking,denied.now));
 }return 0;}
int OriginalReturn(){const auto row=Row("Objects/Weapons/B");CHECK(row);Consumer f(*FindMagazineEquipment(row->id));
 f.Send();f.Send(true);CHECK(f.starts==1&&f.result.interaction.original);const auto rounds=f.reserve.loaded;
 f.held=true;f.reserve.reloadInputReady=false;
 for(float z:{.115f,.09f,.065f,.04f,.015f,-.01f,-.035f,-.035f,0.f,.025f,.05f,.075f,.1f,.12f,.14f,.14f,.14f,.14f,.14f}){
  f.Send(true,false,z);if(f.result.interaction.originalSeat)break;}
 CHECK(f.result.interaction.originalSeat&&f.submits==0);
 f.allowRetire=true;f.Send();f.reserve.reloadInputReady=true;f.Send();
 CHECK(f.policy->ProbeState(f.now).originalReturns==1&&f.reserve.loaded==rounds&&f.reserve.reserve==83);return 0;}
int ProfileRetirement(){const auto a=Row("Objects/Weapons/A"),b=Row("Objects/Weapons/B");CHECK(a&&b);Bc2ReloadNativePolicy p;
 Consumer f(*FindMagazineEquipment(a->id));const auto id=f.reserve.identity;auto now=f.now;
 CHECK(p.SelectMagazineProfile(a->id,true,false));
 auto start=[&](std::uint64_t cycle){const auto o=id.owner;return p.StartMagazine({id,cycle,cycle,now,now+100*Ms,true},
  {cycle,{o.soldier,o.actorGeneration,o.weapon,o.equipGeneration,o.space},ReloadOperation::UnseatMagazine,0,0},now);};
 CHECK(start(1));p.Cancel();CHECK(!p.SelectMagazineProfile(b->id,true,false));CHECK(p.DrainCancelledInvocations(true));
 CHECK(p.SelectMagazineProfile(b->id,true,true));CHECK(p.MatchesSelectedConfig(Config(*b->profile))&&!p.MatchesSelectedConfig(Config(*a->profile)));
 CHECK(p.SelectMagazineProfile(a->id,true,false));CHECK(p.SelectMagazineProfile(b->id,true,false));CHECK(p.SelectMagazineProfile(a->id,true,false));
 CHECK(start(2));p.Cancel();CHECK(!p.SelectMagazineProfile(b->id,true,false));CHECK(p.DrainCancelledInvocations(true));CHECK(p.SelectMagazineProfile(b->id,true,true));
 now+=1000*Ms;CHECK(!p.MagazineLease(id,2,now)&&!p.TakeMagazineAcknowledgement(id,2,now));return 0;}
int TimingsAndPresentation(){for(const auto& r:RegisteredMagazineNativeProfiles()){if(!r.enabled||std::uint64_t(r.id)<2)continue;
 const auto& p=*r.profile;const auto cfg=Config(p);struct Memory {const MagazineNativeProfile* p;std::uint32_t primary;bool corrupt=false;} m{&p,cfg.primaryFire};
 ReloadStateMemory memory{&m,[](void* context,std::uint32_t at,void* out,std::size_t bytes)noexcept{auto& x=*static_cast<Memory*>(context);if(bytes!=4)return false;
  for(const auto& w:x.p->configuration.timing)if(at==x.primary+w.offset){auto value=w.expected+(x.corrupt?1:0);std::memcpy(out,&value,4);return true;}return false;}};
 CHECK(p.ReadTiming(memory,cfg));m.corrupt=true;CHECK(!p.ReadTiming(memory,cfg));
 CHECK(Near(p.HoldCeiling(),cfg.reloadTime*cfg.reloadThreshold)&&Near(p.TailCeiling(),cfg.reloadTime*(1-cfg.reloadThreshold)));
 CHECK(p.completionDeadlineNs>std::int64_t(cfg.reloadTime*1000000000.));
 }return 0;}
int SameConsumerPathRevisits(){const auto a=Row("Objects/Weapons/A"),b=Row("Objects/Weapons/B");CHECK(a&&b);
 Consumer f(*FindMagazineEquipment(a->id));std::uint64_t request=0,seat=0,cycle=0;
 for(const auto row:{a,b,a}){
  const auto p=FindMagazineEquipment(row->id);CHECK(p&&f.selector.SelectMagazineProfile(row->id,true,false));
  f.profile=p;f.observed=Config(*p->native);f.s.asset=p->geometry->asset;f.s.raw.rigFingerprint=p->geometry->rigFingerprint;
  ++f.s.nativeOwner.equipGeneration;f.reserve.identity.owner=f.s.nativeOwner;f.s.raw.owner=f.s.nativeOwner;f.meshes->owner=f.s.nativeOwner;
  ++f.s.input.owner.equipGeneration;f.s.weapon={f.s.nativeOwner.weapon,f.s.input.owner.equipGeneration};
  ++f.equipment.data;f.meshes->weaponData=f.equipment.data;f.observed.weaponData=f.equipment.data;
  f.reserve.capacity=p->native->configuration.values.baseCapacity;f.reserve.loaded=f.reserve.capacity-3;f.reserve.reserve=83;f.reserve.reloadInputReady=true;
  f.hands.Reset();f.gun.reset();f.held=f.gateSent=f.allowRetire=false;f.submitted.reset();f.ack.reset();f.unseat.reset();f.s.bodyFromHand=Pose(2);
  const auto starts=f.starts,submits=f.submits;f.Send();f.Send(false,true);CHECK(f.starts==starts+1&&f.unseat);
  CHECK(f.unseat->id>request&&f.cycle>cycle);request=f.unseat->id;cycle=f.cycle;
  f.held=true;f.Send(false,true);CHECK(f.result.interaction.phase==DetachableMagazinePhase::WellEmpty);f.Send();
  f.s.bodyFromHand=Pose();f.Send(true,false,-.1f);
  for(float z:{-.075f,-.04f,0.f,.025f,.05f,.075f,.1f,.12f,.14f,.14f,.14f,.14f,.14f,.14f}){f.Send(true,false,z);if(f.submits>submits)break;}
  CHECK(f.submits==submits+1&&f.submitted&&f.submitted->request.id>request&&f.submitted->reservation.seat>seat);
  request=f.submitted->request.id;seat=f.submitted->reservation.seat;f.Complete();f.Send();f.allowRetire=true;f.Send();f.Send();
  CHECK(!f.policy->BlocksEquipment()&&f.reserve.loaded==f.reserve.capacity&&f.reserve.reserve==80);
 }
 CHECK(f.starts==3&&f.submits==3&&f.policy->ProbeState(f.now).completed==3);return 0;}
int ChangedExactPathCancelsHeldOriginal(){const auto a=Row("Objects/Weapons/A"),b=Row("Objects/Weapons/B");CHECK(a&&b);
 Consumer f(*FindMagazineEquipment(a->id));f.Send();f.Send(true);CHECK(f.starts==1&&f.result.interaction.original);
 const auto old=f.result.tracking;const auto loaded=f.reserve.loaded,remaining=f.reserve.reserve;
 // Deliberately retain every raw pointer/owner and name: the changed immutable
 // exact-path profile itself must invalidate the old active binding.
 f.profile=FindMagazineEquipment(b->id);f.observed=Config(*b->profile);CHECK(f.selector.SelectMagazineProfile(b->id,true,false));
 f.Send(true);CHECK(f.cancels==1&&!f.result.tracking.target&&f.submits==0);
 CHECK(!MagazineTargetRetained(old,f.result.tracking,f.now)&&f.reserve.loaded==loaded&&f.reserve.reserve==remaining);return 0;}
}
int main(){if(ExactPathAndDisabled()||ActualConsumers()||OriginalReturn()||ProfileRetirement()||TimingsAndPresentation()||SameConsumerPathRevisits()||ChangedExactPathCancelsHeldOriginal())return 1;
 std::puts("7 optional-registry exact-config, real consumer, original-return, retirement, timing, revisit and held-path-change groups passed");return 0;}
