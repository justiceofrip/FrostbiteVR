#include "Bc2ContextInteractEvidence.h"
#include "Bc2NavigationObservation.h"
#include "Bc2HeldFireCadence.h"
#include "Bc2LiveMechanismSnapshotBudget.h"
#include "Bc2VehicleCommit.h"
#include "Bc2VehicleRoutes.h"
#include "Bc2HolsterInput.h"
#include "Bc2MagazinePhysicalReload.h"
#include "fvr/interaction/MenuPointer.h"
#include "fvr/interaction/VehicleTracking.h"
#include "Test.h"
#include <bit>
#include <cstring>
#include <iostream>
#include <limits>
#include <sstream>
using namespace fvr;using namespace fvr::bc2;using namespace fvr::interaction;
namespace fvr::bc2 {
struct NavigationObservationTestAccess {
 static bool Lock(NavigationObservationPublication& p){return !p.gate_.test_and_set(std::memory_order_acquire);}
 static void Unlock(NavigationObservationPublication& p){p.gate_.clear(std::memory_order_release);}
};
}
namespace {
struct Input {
 InputFrame f;ControllerActions foot;VehicleControllerActions seated;InputOwner owner{1,1,true,true};
 VehicleControlOwner seat{{1,1,2,3},1};std::array<std::byte,InputBytes> bytes{};
 Input(){f.generation=f.spaceGeneration=1;f.predictedNs=1000000000;f.focused=f.headValid=true;
  for(auto& h:f.hands){h.active=Components;h.gripTracked=h.aimTracked=true;}}
 void Next(){++f.generation;f.predictedNs+=10000000;}
 ActionOutput Foot(){Next();return foot.Update(f,owner,f.predictedNs);}
 VehicleControlOutput Seat(bool playing=true,bool fresh=true){Next();return seated.Update(f,seat,playing,fresh);}
 unsigned Word(unsigned at)const {unsigned v=0;std::memcpy(&v,bytes.data()+at,4);return v;}
 void Put(unsigned at,unsigned v){std::memcpy(bytes.data()+at,&v,4);}
 void ArmFoot(){Foot();Foot();Foot();}
};
int XUsesNativeInteractAndKeepsReloadSeparate(){Input p;p.ArmFoot();p.f.hands[0].held=Primary;auto a=p.Foot();
 CHECK(a.active&&(a.held&Use)&&!(a.held&Reload));
 p.Put(0x98,0x103);p.Put(0x9c,(1u<<1)|(1u<<4));InputOverride write;CHECK(write.Apply(p.bytes,a));write.Commit();
 CHECK((p.Word(0x98)&(1u<<27))&&!(p.Word(0x98)&(1u<<16))&&!(p.Word(0x98)&(1u<<29)));
 CHECK(p.Word(0x9c)==((1u<<1)|(1u<<4))); // Generic Use never synthesizes or changes a launcher-mode command.
 p.f.hands[0].held=0;p.f.hands[1].held=Secondary;a=p.Foot();InputOverride reload;CHECK(reload.Apply(p.bytes,a));reload.Commit();
 CHECK(!(p.Word(0x98)&(1u<<27))&&(p.Word(0x98)&(1u<<29)));return 0;}
bool FootAlias(){VehicleRouteSnapshot r;r.identity={(std::uint64_t(0x30000)<<32)|0x20000,1,1,0x20000,0x40000,0x50000,0x60000};r.fingerprint=1;r.count=2;r.routes[0]={16,36};r.routes[1]={27,36};return HasOnFootContextUseVehicleAlias(r);}
int VerifiedSharedConceptUsesOneFilteredHold(){Input p;p.ArmFoot();CHECK(FootAlias());p.f.hands[0].held=Primary;auto a=p.Foot();CHECK(a.held&Use);
 constexpr auto both=(1u<<16)|(1u<<27);p.Put(0x98,0x103);InputOverride first;CHECK(first.Apply(p.bytes,a,{},{},{},FootAlias()));first.Commit();CHECK((p.Word(0x98)&both)==both);
 // Duplicate packets retain one level, never mint another pressed edge.
 const auto duplicate=p.foot.Update(p.f,p.owner,p.f.predictedNs);CHECK((duplicate.held&Use)&&!duplicate.pressed);InputOverride repeated;CHECK(repeated.Apply(p.bytes,duplicate,{},{},{},FootAlias()));repeated.Commit();CHECK((p.Word(0x98)&both)==both);
 p.f.hands[0].held=0;a=p.Foot();InputOverride release;CHECK(release.Apply(p.bytes,a,{},{},{},FootAlias()));release.Commit();CHECK(!(p.Word(0x98)&both));
 // Each original native Gather rebuilds its cache. If the mapping ceases to be
 // an alias, the preceding VR command cannot survive into the next native tick.
 p.Put(0x98,0x103);p.f.hands[0].held=Primary;a=p.Foot();InputOverride noAlias;CHECK(noAlias.Apply(p.bytes,a));noAlias.Commit();CHECK(!(p.Word(0x98)&(1u<<16))&&(p.Word(0x98)&(1u<<27)));
 // Context replacement requires neutral before the seated policy can exit.
 p.Put(0x98,0x11);CHECK(!p.Seat().axes.exit&&!p.Seat().axes.exit);CHECK(!(p.Word(0x98)&both));
 return 0;
}
int SharedConceptHonorsMenuReloadAndFocus(){Input p;p.ArmFoot();constexpr auto both=(1u<<16)|(1u<<27);p.f.hands[0].held=Primary;auto a=p.Foot();
 MagazinePhysicalResult owned;owned.blocksWeaponActions=true;ApplyMagazinePhysicalActions(a,Xm8MagazineAsset,owned);
 InputOverride blocked;CHECK(blocked.Apply(p.bytes,a,{},{},{},FootAlias()));blocked.Commit();CHECK(!(p.Word(0x98)&both));
 MenuPointer menu;InputFrame frame=p.f;frame.hands[0].held=0;menu.Update(frame,nullptr,0,0);++frame.generation;menu.Update(frame,nullptr,0,0);
 ++frame.generation;frame.hands[0].held=Primary|Secondary;auto m=menu.Update(frame,nullptr,0,0);CHECK(m.suppressLeftFaceButtons);m.FilterGameplayInput(frame);
 a=p.foot.Update(frame,p.owner,frame.predictedNs);CHECK(!(a.held&Use));InputOverride menuBlock;if(a.active){CHECK(menuBlock.Apply(p.bytes,a,{},{},{},FootAlias()));menuBlock.Commit();}CHECK(!(p.Word(0x98)&both));
 p.Next();p.f.generation=frame.generation+1;p.f.hands[0].held=Primary;p.f.focused=false;CHECK(!(p.Foot().held&Use));p.f.focused=true;CHECK(!(p.Foot().held&Use));
 p.f.hands[0].held=0;p.Foot();p.Foot();p.f.hands[0].held=Primary;CHECK(p.Foot().held&Use);
 return 0;
}
int XExitsBoatThroughVerifiedProfileOnly(){Input p;p.Seat();p.Seat();p.f.hands[0].held=Primary;const auto controls=p.Seat();CHECK(controls.active&&controls.axes.exit);
 VehicleSeatProfile profile;profile.role=VehicleSeatRole::Driver;profile.routeFingerprint=99;
 profile.axes[unsigned(VehicleAxis::Throttle)]=EntryAction::Throttle;profile.axes[unsigned(VehicleAxis::Steer)]=EntryAction::Yaw;profile.exitVerified=true;
 VehicleSeatSnapshot seat{{1,2,3,0x10000,0x20000,0x30000,0x40000},99,true,true};
 VehicleInputCommand command;command.identity=seat.identity;command.active=controls.active;command.exitHeld=controls.axes.exit;
 p.Put(0x98,0x11);p.Put(0x9c,0xabcdef);p.Put(8+4*8,std::bit_cast<unsigned>(.7f));
 const auto plan=BuildVehicleInputPlan(profile,seat,command,p.bytes);VehicleInputOverride write;CHECK(write.Apply(p.bytes,plan));write.Commit();
 CHECK((p.Word(0x98)&(1u<<16))&&!(p.Word(0x98)&(1u<<27))&&p.Word(0x9c)==0xabcdef&&p.Word(8+4*8)==std::bit_cast<unsigned>(.7f));
 profile.exitVerified=false;p.Put(0x98,0x11);const auto refused=BuildVehicleInputPlan(profile,seat,command,p.bytes);VehicleInputOverride noExit;
 CHECK(noExit.Apply(p.bytes,refused));noExit.Commit();CHECK(!(p.Word(0x98)&(1u<<16)));
 seat.routeFingerprint=100;CHECK(BuildVehicleInputPlan(profile,seat,command,p.bytes).status==VehicleInputPlanStatus::UnverifiedBinding);return 0;}
int HeldEntryCannotImmediatelyExitOrReenter(){Input p;p.ArmFoot();p.f.hands[0].held=Primary;CHECK(p.Foot().held&Use);
 CHECK(!p.Seat().axes.exit&&!p.Seat().axes.exit);p.f.hands[0].held=0;p.Seat();p.f.hands[0].held=Primary;CHECK(p.Seat().axes.exit);
 ++p.owner.generation;CHECK(!(p.Foot().held&Use)&&!(p.Foot().held&Use));
 p.f.hands[0].held=0;p.Foot();p.Foot();p.f.hands[0].held=Primary;CHECK(p.Foot().held&Use);
 ++p.seat.seat.seat;CHECK(!p.Seat().axes.exit);return 0;}
int MenuChordAndNativePauseSuppressBothContexts(){Input p;MenuPointer menu;menu.Update(p.f,nullptr,0,0);p.Next();menu.Update(p.f,nullptr,0,0);p.ArmFoot();p.Seat();p.Seat();
 p.f.hands[0].held=Primary|Secondary;p.Next();auto m=menu.Update(p.f,nullptr,0,0);CHECK(m.suppressLeftFaceButtons);
 auto filtered=p.f;m.FilterGameplayInput(filtered);CHECK(!filtered.hands[0].held);
 CHECK(!(p.foot.Update(filtered,p.owner,filtered.predictedNs).held&Use));CHECK(!p.seated.Update(filtered,p.seat,true,true).axes.exit);
 p.f.hands[0].held=Primary;p.owner.playing=false;CHECK(!(p.Foot().held&Use));CHECK(!p.Seat(false).axes.exit);
 p.owner.playing=true;CHECK(!(p.Foot().held&Use));CHECK(!p.Seat().axes.exit);return 0;}
int ExpiredTrackingCannotReuseExitAndNeedsNeutral(){Input p;p.Seat();p.Seat();p.f.hands[0].held=Primary;CHECK(p.Seat().axes.exit);
 CHECK(!p.Seat(true,false).axes.exit);CHECK(!p.Seat().axes.exit);p.f.hands[0].held=0;p.Seat();p.f.hands[0].held=Primary;CHECK(p.Seat().axes.exit);
 p.f.hands[0].gripTracked=false;CHECK(!p.Seat().axes.exit);p.f.hands[0].gripTracked=true;CHECK(!p.Seat().axes.exit);return 0;}
int ReloadOwnershipBlocksUseButEmptyHolsterSuppressionPreservesIt(){Input p;p.ArmFoot();p.f.hands[0].held=Primary;auto a=p.Foot();MagazinePhysicalResult magazine;magazine.blocksWeaponActions=true;
 ApplyMagazinePhysicalActions(a,Xm8MagazineAsset,magazine);CHECK(!(a.held&Use));InputOverride blocked;CHECK(blocked.Apply(p.bytes,a));blocked.Commit();CHECK(!(p.Word(0x98)&(1u<<27)));
 a=p.Foot();InputOverride normal;CHECK(normal.Apply(p.bytes,a,{},{},{},FootAlias()));normal.Commit();CHECK(p.Word(0x98)&(1u<<27));
 HolsterSuppressionRequest request;request.owner={0x10000,0x20000,0x30000,0x40000,1,1,1};request.request=1;request.nativeTick=2;request.cache=0x50000;
 request.input={{(std::uint64_t(0x30000)<<32)|0x20000,1,1,1},1,100,200,150,true,{true,true},{false,false}};
 HolsterInputOverride suppress;CHECK(suppress.Apply(p.bytes,request,{nullptr,[](void*,const HolsterSuppressionRequest&)noexcept{return true;}}));
 CHECK(suppress.Commit());CHECK((p.Word(0x98)&((1u<<27)|(1u<<16)))==((1u<<27)|(1u<<16)));return 0;}
int JournalReportsDispatchWithoutInventingExit(){ContextInteractEvidence log;ContextInteractRecord r;r.input=1;r.nowNs=100;r.action=16;
 r.controllerHeld=r.semanticHeld=r.committed=true;log.Observe(r);r.input=2;r.ownerCurrent=r.readback=r.nativeHeld=true;log.Observe(r);
 std::ostringstream out;log.Report(out);CHECK(out.str().find("\"native_interaction_acceptance_verified\":false")!=std::string::npos);
 CHECK(out.str().find("\"verified_held_readbacks\":1")!=std::string::npos&&out.str().find("\"unverified_held_commits\":1")!=std::string::npos);
 for(unsigned n=0;n<100;++n){r.input++;r.nowNs+=100000000;log.Observe(r);}std::ostringstream bounded;log.Report(bounded);CHECK(bounded.str().find("\"dropped\":38")!=std::string::npos);
 CHECK(bounded.str().find("\"input\":39,")<bounded.str().find("\"input\":102,"));CHECK(bounded.str().find("\"input\":1,")==std::string::npos);
 // Once full, held sampling still advances its watermark instead of treating
 // every following gather as a dropped 100ms sample.
 for(unsigned n=0;n<99;++n){++r.input;r.nowNs+=1000000;log.Observe(r);}std::ostringstream later;log.Report(later);CHECK(later.str().find("\"dropped\":38")!=std::string::npos);
 ++r.input;r.nowNs+=1000000;r.actionMask=(1u<<16)|(1u<<27);r.contextAliasVerified=true;r.routeFingerprint=99;log.Observe(r);std::ostringstream alias;log.Report(alias);
 CHECK(alias.str().find("\"context_alias_verified\":1,\"route_fingerprint\":99")!=std::string::npos&&alias.str().find("\"dropped\":39")!=std::string::npos);return 0;}
struct NavigationFixture {
 NavigationObservationPublication publication;GameplayInputLease lease;NavigationObservation sample;
 std::int64_t now=1010000000;
 NavigationFixture(){Input raw;raw.f.menu={InputMenuOwner::Gameplay,11,3,1080000000};raw.f.trackingBasisGeneration=4;
  raw.f.hands[0].stickY=.5f;raw.f.hands[1].aim.position.x=.25f;
  lease.Observe(ipc::ChannelResult::Ok,raw.f,1100000000);
  sample.owner={1,2,3,4,3,5,6,7,8,9,10};sample.onFoot=sample.nativePlaying=sample.allowedRead=true;sample.allowedMask=0x103;
  sample.gatherSequence=1;Source();}
 void Source(){sample.input=lease.input;sample.inputDeadlineNs=lease.deadlineQpc;sample.inputObservedNs=lease.deadlineQpc-100000000;}
 bool Publish(){return publication.Publish(publication.BeginGather(),sample,sample.owner,true,now);}
 std::string Snapshot(bool valid=false,std::size_t padding=0){const auto value=publication.Read(now);
  const auto section=",\"navigationObservation\":"+NavigationObservationJson(value);
  std::string payload=std::string("{\"valid\":")+(valid?"true,\"invalid_reason\":null":"false,\"invalid_reason\":\"no_shot_publication\"")+
   section+",\"diagnostics\":\""+std::string(padding,'x')+"\"}";
  return CompleteNavigationSnapshot(payload,section,value,[&](const auto& v){return publication.Current(v,now);},77,now,
   valid?"payload_limit":"no_shot_publication",live_snapshot::MaximumBytes);}
};
int NavigationWorksWithoutShotOrEquipmentAndStaysOptIn(){NavigationFixture p;
 CHECK(!p.Publish());CHECK(p.publication.Read(p.now).reason==NavigationObservationReason::Disabled);
 CHECK(p.publication.Enable());CHECK(p.Publish());auto read=p.publication.Read(p.now);CHECK(read.Available());
 CHECK(read.owner==p.sample.owner&&read.input.generation==p.lease.input.generation&&read.input.hands[0].stickY==.5f);
 CHECK(read.input.hands[1].aim.position.x==.25f&&read.input.menu==p.lease.input.menu);
 CHECK(read.sampledNs==p.now&&read.validUntilNs==1080000000&&read.inputObservedNs==1000000000&&read.inputDeadlineNs==1100000000);
 const auto missing=p.Snapshot();CHECK(missing.find("\"valid\":false,\"invalid_reason\":\"no_shot_publication\"")!=std::string::npos);
 CHECK(missing.find("\"navigationObservation\":{\"observationOnly\":true,\"available\":true,\"reason\":null")!=std::string::npos);
 CHECK(missing.find("\"trackingBasisGeneration\":4")!=std::string::npos&&missing.find("\"menu\":{\"owner\":1,\"sequence\":11,\"epoch\":3,\"deadlineNs\":1080000000}")!=std::string::npos);
 CHECK(missing.find("\"weapon\"")==std::string::npos&&missing.find("physical_equipment_generation")==std::string::npos&&missing.find("\"mapping\"")==std::string::npos);
 const auto withShot=p.Snapshot(true);CHECK(withShot.find("\"valid\":true,\"invalid_reason\":null")!=std::string::npos);
 CHECK(withShot.find("\"navigationObservation\":{\"observationOnly\":true,\"available\":true")!=std::string::npos);return 0;}
int NavigationRejectsCurrentOwnerMenuSeatAndTracking(){using R=NavigationObservationReason;
 NavigationFixture p;CHECK(p.publication.Enable());CHECK(p.Publish());auto wrong=p.sample.owner;++wrong.player;
 CHECK(!p.publication.Publish(p.publication.BeginGather(),p.sample,wrong,true,p.now));CHECK(p.publication.Read(p.now).reason==R::OwnerUnavailable);
 CHECK(!p.publication.Publish(p.publication.BeginGather(),p.sample,p.sample.owner,false,p.now));CHECK(p.publication.Read(p.now).reason==R::OwnerUnavailable);
 CHECK(p.Publish());p.sample.onFoot=false;p.sample.owner.controlled=90;CHECK(!p.Publish());CHECK(p.publication.Read(p.now).reason==R::NotOnFoot);
 p.sample.onFoot=true;p.sample.owner.controlled=p.sample.owner.soldier;p.sample.allowedMask=3;CHECK(!p.Publish());CHECK(p.publication.Read(p.now).reason==R::NativeInactive);
 p.sample.allowedMask=0x103;p.sample.allowedRead=false;CHECK(!p.Publish());CHECK(p.publication.Read(p.now).reason==R::NativeInactive);
 p.sample.allowedRead=true;p.sample.nativePlaying=false;CHECK(!p.Publish());CHECK(p.publication.Read(p.now).reason==R::NativeInactive);
 p.sample.nativePlaying=true;p.sample.input.menu.owner=InputMenuOwner::Menu;CHECK(!p.Publish());CHECK(p.publication.Read(p.now).reason==R::MenuUnavailable);
 p.Source();p.sample.input.focused=false;CHECK(!p.Publish());CHECK(p.publication.Read(p.now).reason==R::FocusLoss);
 p.Source();p.sample.input.headValid=false;CHECK(!p.Publish());CHECK(p.publication.Read(p.now).reason==R::HeadUntracked);
 p.Source();p.sample.input.hands[1].aimTracked=false;CHECK(!p.Publish());CHECK(p.publication.Read(p.now).reason==R::HandsUntracked);
 p.Source();p.sample.input.hands[0].stickX=std::numeric_limits<float>::quiet_NaN();CHECK(!p.Publish());CHECK(p.publication.Read(p.now).reason==R::InvalidInput);
 CHECK(p.Snapshot().find("nan")==std::string::npos);p.Source();CHECK(p.Publish());
 CHECK(p.publication.Read(1080000000).reason==R::MenuUnavailable);CHECK(p.publication.Read(1100000000).reason==R::StaleInput);
 p.now=1100000000;CHECK(!p.Publish());CHECK(p.publication.Read(p.now).reason==R::StaleInput);return 0;}
int NavigationRepeatsPreserveOriginalSourceAndMenuDeadlines(){using R=NavigationObservationReason;NavigationFixture p;
 CHECK(p.publication.Enable());CHECK(p.Publish());const auto first=p.publication.Read(p.now);
 p.lease.Observe(ipc::ChannelResult::Busy,{},9000000000);p.Source();p.now+=5000000;++p.sample.gatherSequence;CHECK(p.Publish());
 const auto repeat=p.publication.Read(p.now);CHECK(repeat.gatherSequence>first.gatherSequence&&repeat.sampledNs>first.sampledNs);
 CHECK(repeat.inputObservedNs==first.inputObservedNs&&repeat.inputDeadlineNs==first.inputDeadlineNs&&repeat.validUntilNs==first.validUntilNs&&repeat.input.menu==first.input.menu);
 // Even an erroneously refreshed upstream deadline cannot renew this packet.
 p.sample.inputObservedNs+=1000000;p.sample.inputDeadlineNs+=1000000;CHECK(!p.Publish());CHECK(p.publication.Read(p.now).reason==R::SourceClockChanged);
 p.Source();CHECK(p.Publish());p.sample.input.menu.deadlineNs+=1000000;CHECK(!p.Publish());CHECK(p.publication.Read(p.now).reason==R::MenuClockChanged);
 auto next=p.lease.input;++next.generation;next.predictedNs+=10000000;p.lease.Observe(ipc::ChannelResult::Ok,next,1110000000);p.Source();CHECK(p.Publish());
 const auto newer=p.publication.Read(p.now);CHECK(newer.input.generation==2&&newer.inputDeadlineNs==1110000000&&newer.validUntilNs==first.validUntilNs);
 p.now=first.validUntilNs;CHECK(p.publication.Read(p.now).reason==R::MenuUnavailable);return 0;}
int NavigationLocalInvalidationsCannotResurrectCopies(){using R=NavigationObservationReason;NavigationFixture p;CHECK(p.publication.Enable());CHECK(p.Publish());
 auto copy=p.publication.Read(p.now);p.publication.InvalidatePlayer(999,R::OwnerUnavailable);CHECK(p.publication.Current(copy,p.now)==R::Available);
 p.publication.InvalidatePlayer(p.sample.owner.player,R::OwnerUnavailable);CHECK(!p.publication.Read(p.now).Available());CHECK(p.publication.Current(copy,p.now)==R::Changed);
 CHECK(!p.publication.Publish(copy.serial,p.sample,p.sample.owner,true,p.now));CHECK(p.Publish());copy=p.publication.Read(p.now);
 p.publication.Invalidate(R::RouteChanged);CHECK(p.publication.Read(p.now).reason==R::RouteChanged);CHECK(p.publication.Current(copy,p.now)==R::Changed);
 ++p.sample.owner.actorGeneration;++p.sample.owner.movementEpoch;CHECK(p.Publish());copy=p.publication.Read(p.now);
 p.publication.Invalidate(R::InputPause);CHECK(p.publication.Read(p.now).reason==R::InputPause);CHECK(p.publication.Current(copy,p.now)==R::Changed);
 ++p.sample.owner.inputEpoch;++p.sample.owner.movementEpoch;CHECK(p.Publish());copy=p.publication.Read(p.now);
 const auto section=",\"navigationObservation\":"+NavigationObservationJson(copy);
 CHECK(NavigationObservationTestAccess::Lock(p.publication));CHECK(p.publication.Read(p.now).reason==R::Busy);
 const auto dropped=p.publication.BeginGather();CHECK(!p.publication.Publish(dropped,p.sample,p.sample.owner,true,p.now));
 NavigationObservationTestAccess::Unlock(p.publication);CHECK(p.publication.Read(p.now).reason==R::Busy);
 CHECK(!p.publication.Publish(dropped,p.sample,p.sample.owner,true,p.now));CHECK(!p.publication.Read(p.now).Available());
 auto serialized=CompleteNavigationSnapshot("{\"valid\":false"+section+"}",section,copy,[&](const auto& v){return p.publication.Current(v,p.now);},77,p.now,"no_shot_publication",live_snapshot::MaximumBytes);
 CHECK(serialized.find("\"available\":true")==std::string::npos&&serialized.find("\"reason\":\"observation_changed\"")!=std::string::npos);
 CHECK(p.Publish());copy=p.publication.Read(p.now);CHECK(NavigationObservationTestAccess::Lock(p.publication));p.publication.Stop();
 NavigationObservationTestAccess::Unlock(p.publication);CHECK(p.publication.Read(p.now).reason==R::Stopped);
 CHECK(p.publication.Current(copy,p.now)==R::Stopped);CHECK(!p.publication.Publish(copy.serial,p.sample,p.sample.owner,true,p.now));CHECK(!p.Publish());return 0;}
int NavigationSurvivesBoundedPayloadFallback(){NavigationFixture p;CHECK(p.publication.Enable());CHECK(p.Publish());
 for(const bool shot:{false,true}){const auto bounded=p.Snapshot(shot,live_snapshot::MaximumBytes);
  CHECK(bounded.size()<=live_snapshot::MaximumBytes&&bounded.find("\"valid\":false")!=std::string::npos);
  CHECK(bounded.find(shot?"\"invalid_reason\":\"payload_limit\"":"\"invalid_reason\":\"no_shot_publication\"")!=std::string::npos);
  CHECK(bounded.find("\"navigationObservation\":{\"observationOnly\":true,\"available\":true")!=std::string::npos);
  CHECK(bounded.find("\"inputDeadlineNs\":1100000000")!=std::string::npos&&bounded.find("\"validUntilNs\":1080000000")!=std::string::npos);
  CHECK(bounded.find("\"diagnosticsOmitted\":\"payload_limit\"")!=std::string::npos);}
 return 0;}
}
int main(){for(auto test:{XUsesNativeInteractAndKeepsReloadSeparate,VerifiedSharedConceptUsesOneFilteredHold,SharedConceptHonorsMenuReloadAndFocus,XExitsBoatThroughVerifiedProfileOnly,HeldEntryCannotImmediatelyExitOrReenter,
 MenuChordAndNativePauseSuppressBothContexts,ExpiredTrackingCannotReuseExitAndNeedsNeutral,ReloadOwnershipBlocksUseButEmptyHolsterSuppressionPreservesIt,
 JournalReportsDispatchWithoutInventingExit,NavigationWorksWithoutShotOrEquipmentAndStaysOptIn,NavigationRejectsCurrentOwnerMenuSeatAndTracking,
 NavigationRepeatsPreserveOriginalSourceAndMenuDeadlines,NavigationLocalInvalidationsCannotResurrectCopies,NavigationSurvivesBoundedPayloadFallback})if(test())return 1;
 std::cout<<"Context interact:14 controller/native-cache/menu/ownership/journal/navigation groups passed; actual vehicle transition unverified\n";}
