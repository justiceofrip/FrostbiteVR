#include "Bc2VehicleObservation.h"
#include "Bc2HeldFireCadence.h"
#include "Bc2LiveMechanismSnapshotBudget.h"
#include "Test.h"
#include <iostream>
#include <limits>
#include <map>

using namespace fvr;using namespace fvr::bc2;using namespace fvr::interaction;
namespace fvr::bc2 {
struct VehicleObservationTestAccess {
    static bool Lock(VehicleObservationPublication& p){return !p.gate_.test_and_set(std::memory_order_acquire);}
    static void Unlock(VehicleObservationPublication& p){p.gate_.clear(std::memory_order_release);}
};
}
namespace {
using R=VehicleObservationReason;
struct Fixture {
    VehicleObservationPublication publication;GameplayInputLease lease;VehicleControllerActions controls;
    VehicleObservation seed;VehicleRouteResult routes;CampaignVehicleSnapshot consumers;VehicleSeatProfile profile;
    std::int64_t now=1010000000;
    Fixture(){
        InputFrame input;input.generation=input.spaceGeneration=1;input.predictedNs=1000000000;
        input.trackingBasisGeneration=4;input.focused=input.headValid=true;
        input.menu={InputMenuOwner::Gameplay,11,3,1080000000};
        for(auto& h:input.hands){h.active=Components;h.gripTracked=h.aimTracked=true;}
        lease.Observe(ipc::ChannelResult::Ok,input,1100000000);
        seed.owner={0x11000,0x12000,0x14000,0x16000,0x18000,0x19000,0x1a000,0x1b000,2,3,4,5};
        seed.seated=seed.nativePlaying=seed.allowedRead=true;seed.allowedMask=0x11;seed.gatherSequence=1;
        routes.status=VehicleRouteStatus::Okay;auto& s=routes.snapshot;s.identity=seed.owner.Seat();
        s.player=seed.owner.player;s.weak=seed.owner.weak;s.fingerprint=123;
        s.routes[0]={0,0};s.routes[1]={4,1};s.routes[2]={16,36};s.routes[3]={17,42};s.count=4;
        profile.role=VehicleSeatRole::Driver;profile.routeFingerprint=s.fingerprint;
        profile.axes[0]=EntryAction::Throttle;profile.axes[1]=EntryAction::Yaw;profile.exitVerified=true;
        consumers.profile=profile;consumers.owner=s.identity;consumers.driver=0x20000;consumers.camera=0x21000;
        Source();
    }
    void Source(){
        seed.input=lease.input;seed.inputDeadlineNs=lease.deadlineQpc;seed.inputObservedNs=lease.deadlineQpc-100000000;
        seed.controls=controls.Update(seed.input,{{seed.owner.Seat().actor,seed.owner.actorGeneration,seed.owner.controlled,seed.owner.entry},
            seed.input.spaceGeneration},seed.nativePlaying,true);
    }
    VehicleObservation Assemble(){return AssembleVehicleObservation(seed,routes,&profile,&consumers,false);}
    bool Publish(){return publication.Publish(publication.BeginGather(),Assemble(),seed.owner,true,now);}
    std::string Snapshot(std::size_t padding=0,bool firearm=false){
        NavigationObservation navigation;navigation.reason=NavigationObservationReason::NotOnFoot;
        auto vehicle=publication.Read(now);
        const auto navSection=",\"navigationObservation\":"+NavigationObservationJson(navigation);
        const auto vehicleSection=",\"vehicleObservation\":"+VehicleObservationJson(vehicle);
        const auto payload=std::string("{\"valid\":")+(firearm?"true,\"invalid_reason\":null":"false,\"invalid_reason\":\"no_shot_publication\"")+
            navSection+vehicleSection+",\"diagnostics\":\""+std::string(padding,'x')+"\"}";
        return CompleteVehicleNavigationSnapshot(payload,navSection,navigation,vehicleSection,vehicle,
            [](const auto& v){return v.reason;},[&](const auto& v){return publication.Current(v,now);},77,now,
            firearm?"payload_limit":"no_shot_publication",live_snapshot::MaximumBytes);
    }
};
int NeutralSeatExistsWithoutFirearmAndRemainsOptIn(){
    Fixture p;CHECK(!p.Publish());CHECK(p.publication.Read(p.now).reason==R::Disabled);
    CHECK(p.publication.Enable());CHECK(p.Publish());const auto v=p.publication.Read(p.now);CHECK(v.Available());
    // The real vehicle policy arms on the first neutral packet but has not
    // produced an active command. Observation must still carry this sample.
    CHECK(!v.controls.active&&v.controls.armedLeft&&v.controls.armedRight);
    CHECK(v.owner==p.seed.owner&&v.owner.seatGeneration==3&&v.input.trackingBasisGeneration==4);
    CHECK(v.inputObservedNs==1000000000&&v.inputDeadlineNs==1100000000&&v.validUntilNs==1080000000);
    CHECK(v.axes[0].consumer==p.consumers.driver&&v.axes[0].permitted&&v.axes[1].action==4&&!v.axes[2].mapped);
    const auto json=p.Snapshot();CHECK(json.find("\"valid\":false,\"invalid_reason\":\"no_shot_publication\"")!=std::string::npos);
    CHECK(json.find("\"navigationObservation\":{\"observationOnly\":true,\"available\":false,\"reason\":\"not_on_foot\"")!=std::string::npos);
    CHECK(json.find("\"vehicleObservation\":{\"observationOnly\":true,\"available\":true")!=std::string::npos);
    CHECK(json.find("\"fire\":false")!=std::string::npos&&json.find("\"weapon\"")==std::string::npos);
    CHECK(json.find("\"seatButtonKnown\":false")!=std::string::npos);
    CHECK(json.find("\"exit\":{\"available\":false,\"reason\":\"exit_binding_unavailable\"")!=std::string::npos);
    CHECK(json.find("\"held\":0")!=std::string::npos&&json.find("\"armedRight\":true")!=std::string::npos);
    return 0;
}
int TypedAssemblyRejectsMismatchedOwnerRoutesAndConsumers(){
    Fixture p;CHECK(p.Assemble().Available());auto changed=p.routes;++changed.snapshot.identity.seatGeneration;
    CHECK(AssembleVehicleObservation(p.seed,changed,&p.profile,&p.consumers,false).reason==R::OwnerUnavailable);
    changed=p.routes;changed.status=VehicleRouteStatus::Changed;
    CHECK(AssembleVehicleObservation(p.seed,changed,&p.profile,&p.consumers,false).reason==R::RoutesUnavailable);
    auto profile=p.profile;++profile.routeFingerprint;
    CHECK(AssembleVehicleObservation(p.seed,p.routes,&profile,&p.consumers,false).reason==R::ProfileUnavailable);
    CHECK(AssembleVehicleObservation(p.seed,p.routes,&p.profile,nullptr,false).reason==R::ConsumersUnavailable);
    auto consumers=p.consumers;++consumers.owner.actorGeneration;
    CHECK(AssembleVehicleObservation(p.seed,p.routes,&p.profile,&consumers,false).reason==R::ConsumersUnavailable);
    consumers=p.consumers;consumers.driver=0;
    CHECK(AssembleVehicleObservation(p.seed,p.routes,&p.profile,&consumers,false).reason==R::ConsumersUnavailable);
    changed=p.routes;changed.snapshot.routes[1].conceptId=8;
    CHECK(AssembleVehicleObservation(p.seed,changed,&p.profile,&p.consumers,false).reason==R::ConsumersUnavailable);
    // A gunner exposes native turret axes, independently of hull permission.
    p.profile.role=VehicleSeatRole::Gunner;p.profile.axes[0].reset();p.profile.axes[1].reset();
    p.profile.axes[2]=static_cast<EntryAction>(6);p.profile.axes[3]=EntryAction::Pitch;p.profile.axes[4]=EntryAction::Fire;
    p.routes.snapshot.routes[0]={6,8};p.routes.snapshot.routes[1]={5,7};p.seed.allowedMask=(1u<<6)|(1u<<5);
    p.consumers.profile=p.profile;p.consumers.driver=0;p.consumers.weapons=1;
    auto gunner=p.Assemble();CHECK(gunner.Available()&&!gunner.axes[0].mapped&&gunner.axes[2].permitted&&gunner.axes[3].permitted);
    p.seed.allowedMask=1u<<6;gunner=p.Assemble();CHECK(gunner.axes[2].permitted&&!gunner.axes[3].permitted);
    return 0;
}
int ExpiryTrackingAndOwnerGatesPreserveOriginalClocks(){
    Fixture p;CHECK(p.publication.Enable());CHECK(p.Publish());const auto first=p.publication.Read(p.now);
    CHECK(p.publication.Read(1080000000).reason==R::MenuUnavailable);CHECK(p.publication.Read(1100000000).reason==R::StaleInput);
    for(unsigned side=0;side<2;++side){p.Source();p.seed.input.hands[side].gripTracked=false;CHECK(!p.Publish());CHECK(p.publication.Read(p.now).reason==R::HandsUntracked);
        p.Source();p.seed.input.hands[side].aimTracked=false;CHECK(!p.Publish());CHECK(p.publication.Read(p.now).reason==R::HandsUntracked);}
    p.Source();p.seed.input.focused=false;CHECK(!p.Publish());CHECK(p.publication.Read(p.now).reason==R::FocusLoss);
    p.Source();p.seed.input.headValid=false;CHECK(!p.Publish());CHECK(p.publication.Read(p.now).reason==R::HeadUntracked);
    p.Source();p.seed.input.hands[0].stickX=std::numeric_limits<float>::quiet_NaN();CHECK(!p.Publish());CHECK(p.publication.Read(p.now).reason==R::InvalidInput);
    CHECK(p.Snapshot().find("nan")==std::string::npos);
    p.Source();p.seed.input.menu.owner=InputMenuOwner::Menu;CHECK(!p.Publish());CHECK(p.publication.Read(p.now).reason==R::MenuUnavailable);
    p.Source();p.seed.nativePlaying=false;CHECK(!p.Publish());CHECK(p.publication.Read(p.now).reason==R::NativeInactive);p.seed.nativePlaying=true;
    p.seed.allowedRead=false;CHECK(!p.Publish());CHECK(p.publication.Read(p.now).reason==R::NativeInactive);p.seed.allowedRead=true;
    auto wrong=p.seed.owner;++wrong.inputEpoch;
    CHECK(!p.publication.Publish(p.publication.BeginGather(),p.Assemble(),wrong,true,p.now));CHECK(p.publication.Read(p.now).reason==R::OwnerUnavailable);
    CHECK(!p.publication.Publish(p.publication.BeginGather(),p.Assemble(),p.seed.owner,false,p.now));
    CHECK(p.Publish());p.lease.Observe(ipc::ChannelResult::Busy,{},9000000000);p.Source();p.now+=5000000;++p.seed.gatherSequence;CHECK(p.Publish());
    auto repeat=p.publication.Read(p.now);CHECK(repeat.inputDeadlineNs==first.inputDeadlineNs&&repeat.inputObservedNs==first.inputObservedNs&&repeat.input.menu==first.input.menu);
    p.seed.inputObservedNs+=1000000;p.seed.inputDeadlineNs+=1000000;CHECK(!p.Publish());CHECK(p.publication.Read(p.now).reason==R::SourceClockChanged);
    p.Source();CHECK(p.Publish());p.seed.input.menu.deadlineNs+=1000000;CHECK(!p.Publish());CHECK(p.publication.Read(p.now).reason==R::MenuClockChanged);
    return 0;
}
int ReentrancyTransitionsAndBusyPublicationRevokeOldValues(){
    Fixture p;CHECK(p.publication.Enable());CHECK(p.Publish());auto old=p.publication.Read(p.now);
    const auto outer=p.publication.BeginGather(),inner=p.publication.BeginGather();
    CHECK(p.publication.Publish(inner,p.Assemble(),p.seed.owner,true,p.now));
    CHECK(!p.publication.Publish(outer,p.Assemble(),p.seed.owner,true,p.now));CHECK(p.publication.Read(p.now).serial==inner);
    CHECK(p.publication.Current(old,p.now)==R::Changed);
    p.publication.InvalidatePlayer(999,R::OwnerUnavailable);CHECK(p.publication.Read(p.now).Available());
    p.publication.InvalidatePlayer(p.seed.owner.player,R::OwnerUnavailable);CHECK(p.publication.Read(p.now).reason==R::OwnerUnavailable);
    for(auto reason:{R::RouteChanged,R::InputPause}){CHECK(p.Publish());old=p.publication.Read(p.now);p.publication.Invalidate(reason);
        CHECK(p.publication.Read(p.now).reason==reason&&p.publication.Current(old,p.now)==R::Changed);}
    CHECK(p.Publish());CHECK(VehicleObservationTestAccess::Lock(p.publication));
    CHECK(p.publication.Read(p.now).reason==R::Busy);const auto dropped=p.publication.BeginGather();
    CHECK(!p.publication.Publish(dropped,p.Assemble(),p.seed.owner,true,p.now));
    VehicleObservationTestAccess::Unlock(p.publication);CHECK(p.publication.Read(p.now).reason==R::Busy);
    CHECK(!p.publication.Publish(dropped,p.Assemble(),p.seed.owner,true,p.now));
    CHECK(p.Publish());old=p.publication.Read(p.now);CHECK(VehicleObservationTestAccess::Lock(p.publication));p.publication.Stop();
    VehicleObservationTestAccess::Unlock(p.publication);CHECK(p.publication.Read(p.now).reason==R::Stopped&&p.publication.Current(old,p.now)==R::Stopped);
    CHECK(!p.Publish());return 0;
}
struct BindingMemory {
    std::map<unsigned,unsigned char> bytes;unsigned permissionAddress=0,permissionReads=0;bool changePermission=false;
    void Word(unsigned at,unsigned value){for(unsigned n=0;n<4;++n)bytes[at+n]=static_cast<unsigned char>(value>>(8*n));}
    VehicleRouteMemory Access(){return {this,[](void* context,unsigned at,void* dst,std::size_t size){
        auto& m=*static_cast<BindingMemory*>(context);
        if(at==m.permissionAddress&&++m.permissionReads==2&&m.changePermission)m.Word(at,0);
        for(std::size_t n=0;n<size;++n){const auto found=m.bytes.find(at+unsigned(n));if(found==m.bytes.end())return false;
            static_cast<unsigned char*>(dst)[n]=found->second;}return true;},nullptr};}
    BindingMemory(const VehicleRouteSnapshot& s){
        const auto player=s.player,router=s.identity.router;permissionAddress=player+0x160;
        Word(player+0xc74,s.identity.cache);Word(s.identity.entry+0x198,router);Word(permissionAddress,96);
        Word(0x1570e9c,1);Word(router+0x14,0x30000);Word(router+0x18,1);
        Word(0x30000,0x31000);Word(0x30004,0x32000);Word(0x31000,17);Word(0x31004,42);
        Word(router+4,0x40000);Word(router+8,0x41000);Word(0x40000,0x14196f8);Word(0x41000,0x141ca1c);
        Word(0x40008,0x42000);Word(0x41004,0x43000);Word(0x41008,0x43000+76*16);
        Word(0x40000+0x20+4*42,0);Word(0x43000+42*16,0);Word(0x43000+42*16+4,0);
    }
};
int PassiveNeutralBindingReadsCurrentPermissionWithoutMutatingInput(){
    Fixture p;auto value=p.Assemble();BindingMemory memory(p.routes.snapshot);const auto before=memory.bytes;
    CHECK(value.input.hands[1].held==0&&!value.controls.active);
    ObserveVehicleSeatBinding(value,memory.Access(),p.routes.snapshot,true);
    CHECK(value.permissionRead&&value.playerPermissionMask==96&&value.seatBindingVerified&&value.conceptPermissionVerified);
    CHECK(value.seatBinding.unboundConcept&&memory.bytes==before&&memory.permissionReads==2);
    CHECK(value.input.hands[1].held==0&&!value.controls.active);
    CHECK(VehicleObservationJson(value).find("\"seatCycle\":{\"available\":true")!=std::string::npos);
    memory.Word(memory.permissionAddress,0);ObserveVehicleSeatBinding(value,memory.Access(),p.routes.snapshot,true);
    CHECK(value.permissionRead&&!value.playerPermissionMask&&!value.seatBindingVerified&&!value.conceptPermissionVerified);
    CHECK(VehicleObservationJson(value).find("\"seatCycle\":{\"available\":false")!=std::string::npos);
    BindingMemory changed(p.routes.snapshot);changed.changePermission=true;
    ObserveVehicleSeatBinding(value,changed.Access(),p.routes.snapshot,true);CHECK(!value.permissionRead&&!value.seatBindingVerified);
    BindingMemory absent(p.routes.snapshot);absent.bytes.erase(absent.permissionAddress);
    ObserveVehicleSeatBinding(value,absent.Access(),p.routes.snapshot,true);CHECK(!value.permissionRead&&!value.seatBindingVerified);
    ObserveVehicleSeatBinding(value,memory.Access(),p.routes.snapshot,false);CHECK(!value.seatCycleCodeVerified&&!value.permissionRead);
    return 0;
}
int CombinedFallbackAndFinalBookendRetainBothIndependentSections(){
    Fixture p;CHECK(p.publication.Enable());CHECK(p.Publish());
    for(bool firearm:{false,true}){const auto json=p.Snapshot(live_snapshot::MaximumBytes,firearm);
        CHECK(json.size()<=live_snapshot::MaximumBytes&&json.find("\"nonce\":\"77\"")!=std::string::npos);
        CHECK(json.find("\"valid\":false")!=std::string::npos);
        CHECK(json.find(firearm?"\"invalid_reason\":\"payload_limit\"":"\"invalid_reason\":\"no_shot_publication\"")!=std::string::npos);
        CHECK(json.find("\"navigationObservation\":")!=std::string::npos&&json.find("\"vehicleObservation\":{\"observationOnly\":true,\"available\":true")!=std::string::npos);
        CHECK(json.find("\"inputDeadlineNs\":1100000000")!=std::string::npos&&json.find("\"validUntilNs\":1080000000")!=std::string::npos);
    }
    NavigationObservation navigation;navigation.reason=NavigationObservationReason::NotOnFoot;
    const auto ns=",\"navigationObservation\":"+NavigationObservationJson(navigation);
    for(bool expire:{false,true}){CHECK(p.Publish());const auto current=p.publication.Read(p.now);
        const auto section=",\"vehicleObservation\":"+VehicleObservationJson(current);
        const auto json=CompleteVehicleNavigationSnapshot("{\"valid\":false"+ns+section+"}",ns,navigation,section,current,
            [&](const auto& v){if(!expire)p.publication.BeginGather();return v.reason;},
            [&](const auto& v){return p.publication.Current(v,expire?v.validUntilNs:p.now);},77,p.now,"no_shot_publication",live_snapshot::MaximumBytes);
        CHECK(json.find("\"available\":true")==std::string::npos);
        CHECK(json.find(expire?"\"reason\":\"menu_unavailable\"":"\"reason\":\"observation_changed\"")!=std::string::npos);
        CHECK(json.find("\"input\":null,\"capabilities\":null")!=std::string::npos);
    }
    return 0;
}
}
int main(){
    for(auto test:{NeutralSeatExistsWithoutFirearmAndRemainsOptIn,TypedAssemblyRejectsMismatchedOwnerRoutesAndConsumers,
        ExpiryTrackingAndOwnerGatesPreserveOriginalClocks,ReentrancyTransitionsAndBusyPublicationRevokeOldValues,
        PassiveNeutralBindingReadsCurrentPermissionWithoutMutatingInput,CombinedFallbackAndFinalBookendRetainBothIndependentSections})if(test())return 1;
    std::cout<<"Vehicle observation cases passed: typed seat, neutral permission, original deadlines and combined snapshot\n";return 0;
}
