#include "Test.h"
#include "Bc2MagazineResourceReload.h"
#include <cstring>
#include <cstdio>
using namespace fvr;using namespace fvr::bc2;using namespace fvr::interaction;using namespace fvr::interaction::reload_insertion_detail;
namespace {
constexpr std::int64_t Ms=1000000;
auto Pose(float x=0,float y=0,float z=0){auto p=Identity();p.values[3]={x,y,z,1};return p;}
struct Fixture {
    AmmoResourceService service;AmmoResourceChannel channel;MagazineResourceApi api;
    std::optional<Bc2MagazinePhysicalReload> adapter;
    HandInteraction hands;std::optional<HandClaim> gun;MagazinePhysicalSample s;MagazinePhysicalResult result;
    ReloadHoldIdentity native{{0x10000,0x20000,0x30000,0x40000,5,3,7},{0x50000,0x60000,0x70000},0x80000,0x90000,0xa0000};
    AmmoResourceBinding binding;AmmunitionCounts counts{27,83,30};std::int64_t now=1000*Ms;
    std::uint64_t sequence=0,intent=0,nativeSequence=0,invocation=0;unsigned calls=0;
    std::shared_ptr<SelectedMeshesSnapshot> meshes=std::make_shared<SelectedMeshesSnapshot>();
    const MagazineEquipmentProfile* profile=&Xm8MagazineEquipment();bool completeNative=true;
    std::optional<AmmoResourceNativeCall> outstanding;
    Fixture(const MagazineEquipmentProfile& equipment=Xm8MagazineEquipment()):profile(&equipment){
        binding={native.owner,{{native.owner.soldier,5,native.owner.weapon,45},3,7},0xd0000,0x130000,0x140000,0x150000,now,now+100*Ms};
        s.nativeOwner=native.owner;s.weapon={0xb0000,17};s.trackingEpoch=7;
        s.input={{(std::uint64_t(0x30000)<<32)|0x20000,5,17,7},0,now,now+100*Ms,now,true,{true,true},{true,false}};
        s.asset=profile->geometry->asset;s.meshes=meshes;s.bodyFromHand=Pose(2);
        s.family.binding={native.owner,s.weapon,binding.inventory,0xc0000,2,profile};
        if(profile->native->identityRoute!=MagazineIdentityRoute::LinkedLauncherAlias){
            s.weapon.id=native.owner.weapon;s.family.binding.weapon=s.weapon;s.family.binding.launcher=s.family.binding.launcherSlot=0;
            auto& identity=s.family.binding.equipment;identity.weapon=native.owner.weapon;identity.data=binding.data;identity.persistence=binding.persistence;
            std::memcpy(identity.asset.data(),s.asset.data(),s.asset.size());
        }
        meshes->owner=native.owner;meshes->stateCount=1;meshes->soleConfiguredArray=0x110000;meshes->states[0].count=1;
        meshes->weaponData=binding.data;
        auto& mesh=meshes->states[0].meshes[0];mesh.kind=profile->geometry->meshKind;mesh.address=0x120000;
        std::memcpy(mesh.assetPath.data(),profile->geometry->mesh.data(),profile->geometry->mesh.size());
        s.raw.valid=true;s.raw.owner=s.nativeOwner;s.raw.rigFingerprint=profile->geometry->rigFingerprint;s.raw.weaponWorldMeters=Pose();s.raw.nativeMagazineAttached=true;
        api.context=this;api.read=[](void* p,const ReloadStateOwner& o,std::int64_t now)noexcept{return static_cast<Fixture*>(p)->channel.Read(o,now);};
        api.submit=[](void* p,const AmmoResourceRequest& r)noexcept{return static_cast<Fixture*>(p)->channel.Submit(r);};
        MagazinePhysicalApi playerApi;playerApi.context=this;playerApi.resourceRead=api.read;playerApi.resourceSubmit=api.submit;
        adapter.emplace(true,playerApi,AmmoSupplyConfig{InteractionHand::Left,1000,{1001,1},{0,0,0},.15f,200*Ms});adapter->EnableBodyAmmo();
    }
    AmmunitionSnapshot Snapshot(){return {binding.context,++nativeSequence,now,now+100*Ms,counts,true};}
    void Select(){binding.observedNs=now;binding.deadlineNs=now+100*Ms;service.Select(binding,native,Snapshot(),now);channel.Publish(service.View());}
    AmmoResourceOwnUpdate Row(unsigned branch,const AmmoResourceNativeCall& c){AmmoResourceOwnUpdate r;
        r.beforeOwner=r.afterOwner=c.identity.owner;r.firing=c.identity.firing[branch];r.serverPlayer=c.identity.serverPlayer;r.serverSoldier=c.identity.serverSoldier;r.serverItem=c.identity.serverItem;
        r.invocation=c.invocation+(branch==2?0:branch+1);r.beginNs=c.beginNs-1;r.endNs=c.endNs+10+branch;r.branch=branch;r.depth=1;
        r.loadedBefore=c.before.loaded;r.reserveBefore=c.before.reserve;r.loadedAfter=c.after.loaded;r.reserveAfter=c.after.reserve;
        r.current=r.next=2;r.finished=r.identityRetained=r.neutralContext=true;return r;}
    void Drain(){if(const auto r=channel.Take())if(const auto c=service.Accept(*r,now)){
        const auto adjacent=Snapshot();if(service.Dispatch(adjacent,now)){
            ++calls;outstanding=AmmoResourceNativeCall{native,c->id,(invocation+=10),now+1,now+2,c->before,c->after,true,true,true};counts=c->after;
            service.Call(*outstanding);
        }}
        if(outstanding&&completeNative){for(unsigned branch:{0,1,2})service.Observe(Row(branch,*outstanding),now+100);outstanding.reset();}
    }
    void Send(bool grip=false,bool eject=false,float travel=-1000.f,bool pouch=false){
        now+=20*Ms;++sequence;s.input.sequence=sequence;s.input.observedNs=s.input.nowNs=now;s.input.deadlineNs=now+100*Ms;
        s.input.released[0]=!grip;s.gripPressed=grip;s.ejectPressed=eject;s.geometrySequence=sequence;s.bodyFromHand=pouch?Pose():Pose(2);
        s.family.observedNs=now;s.family.deadlineNs=now+100*Ms;s.family.verified=true;
        if(s.weapon.id==native.owner.weapon)s.family.carried=SelectedCarriedWeaponEvidence{native.owner,s.input.owner,s.weapon,
            s.family.binding.equipment,binding.inventory,binding.switching,0,0,sequence,now,now+100*Ms,true};
        meshes->sequence=sequence;meshes->observedNs=now;meshes->deadlineNs=now+100*Ms;
        const auto& p=profile->geometry->interaction.insertion;if(travel==-1000.f)travel=p.travelMeters;
        s.raw.rawLeftWristWorldMeters=Multiply(p.itemFromHand,Multiply(*InverseRigid(p.itemFromInsertion),Multiply(TravelPose(p,travel),p.weaponFromEntry)));
        s.raw.inputEvidence=s.input;s.originalHandEvidence=s.input;
        hands.Update(s.input);HandContactProof proof{{1002,1},sequence,s.input.deadlineNs,true};
        if(gun)gun=hands.Renew(s.input,gun->token,proof).claim;
        if(!gun)gun=hands.Acquire(s.input,{s.input.owner,InteractionHand::Right,HandClaimKind::GunHold,s.weapon,proof,++intent,0}).claim;
        Select();result=adapter->Tick(s,hands,intent);Drain();
    }
    bool Eject(){Send();Send(false,true);Send(false,true);Send(false);return result.interaction.phase==DetachableMagazinePhase::WellEmpty;}
    void Insert(){Send();Send(true,false,-.1f,true);
        const float finish=profile->geometry->interaction.insertion.travelMeters;
        for(unsigned n=0;n<100;++n){const float along=std::min(finish,-.1f+.01f*float(n));
            Send(true,false,along);if(result.interaction.phase==DetachableMagazinePhase::AwaitingSeat||result.interaction.phase==DetachableMagazinePhase::Complete)break;}}
    void SameWeaponContextChange(unsigned kind){
        adapter->Cancel(s.input,hands);
        ++native.owner.equipGeneration;++binding.context.equipGeneration;
        if(kind==1){++native.owner.space;++binding.context.space;++s.input.owner.space;++s.trackingEpoch;}
        binding.owner=native.owner;s.nativeOwner=native.owner;s.family.binding.owner=native.owner;meshes->owner=native.owner;s.raw.owner=native.owner;
        ++s.input.owner.equipGeneration;++s.weapon.generation;s.family.binding.weapon=s.weapon;
    }

};
int SharedHandAdapter(){unsigned profiles=0;for(const auto* profile:{&Xm8MagazineEquipment(),FindMagazineEquipment("AEK971_sp")}){
    if(!profile||!profile->Ready())continue;++profiles;
    for(int loaded:{0,27,30}){
    Fixture f(*profile);f.counts.loaded=loaded;CHECK(f.Eject());CHECK(f.counts.loaded==0&&f.counts.reserve==83&&f.calls==1);
    CHECK(f.result.tracking.resource&&f.result.tracking.target&&f.result.tracking.target->role==MagazinePropRole::Hidden);
    CHECK(MagazineTargetFresh(f.result.tracking,f.now));CHECK(f.result.bodyAmmo);CHECK(f.service.View()->originalState==MagazineResourceState::Discarded);
    f.Insert();CHECK(f.result.interaction.phase==DetachableMagazinePhase::AwaitingSeat);
    CHECK(!f.hands.Current(InteractionHand::Left));CHECK(f.counts.loaded==30&&f.counts.reserve==53&&f.calls==2);
    // This is the original gather result, before the next server publication.
    // A physically seated magazine must remain visible and free the support
    // hand without claiming that ammunition has completed already.
    CHECK(f.result.tracking.target&&MagazineTargetFresh(f.result.tracking,f.now));
    CHECK(f.result.tracking.target->role==MagazinePropRole::Attached);
    CHECK(!MagazineBlocksSupport(f.result,f.now));
    CHECK(f.result.blocksWeaponActions);
    f.Send();CHECK(f.result.interaction.phase==DetachableMagazinePhase::Complete);CHECK(!f.result.blocksWeaponActions);
    CHECK(f.result.tracking.target&&MagazineTargetFresh(f.result.tracking,f.now));
    f.Send();CHECK(f.result.interaction.phase==DetachableMagazinePhase::Attached);CHECK(f.result.bodyAmmo);
}}std::printf("Shared BC2 adapter profiles exercised: %u (compiled enabled geometry only).\n",profiles);return 0;}
int NamespaceProof(){Fixture f;f.Send();const auto v=*f.service.View();
    const auto map=BindMagazineResourceOwners(v,f.s.family,f.s.input.owner,f.s.weapon,f.now);CHECK(map);
    CHECK(map->physical.actor!=map->native.resource.actor&&map->physical.equipGeneration!=map->native.equipGeneration&&map->weapon.id!=map->native.resource.weapon);
    for(unsigned bad=0;bad<7;++bad){auto view=v;auto family=f.s.family;auto owner=f.s.input.owner;auto weapon=f.s.weapon;
        if(bad==0)++view.binding.inventory;if(bad==1)++family.binding.owner.weapon;if(bad==2)++owner.actor;if(bad==3)++owner.equipGeneration;
        if(bad==4)++view.binding.context.resource.weapon;if(bad==5)++weapon.id;if(bad==6)family.deadlineNs=f.now;
        CHECK(!BindMagazineResourceOwners(view,family,owner,weapon,f.now));}
    return 0;
}
int ReturnOriginal(){for(int loaded:{0,27,30}){Fixture f;f.counts={loaded,0,30};f.Send();f.Send(true);
    CHECK(f.calls==1);for(float p:{.075f,.05f,.025f,0.f,-.025f,-.05f})f.Send(true,false,p);
    CHECK(f.result.interaction.phase==DetachableMagazinePhase::RemovedHeld);
    for(float p:{-.025f,0.f,.025f,.05f,.075f,.1f,.1f,.1f,.1f,.1f,.1f,.1f}){
        f.Send(true,false,p);if(f.calls==2)break;}
    CHECK(f.calls==2&&f.counts.loaded==loaded&&f.counts.reserve==0);
    CHECK(f.result.interaction.phase==DetachableMagazinePhase::AwaitingOriginalReturn);
    CHECK(!f.hands.Current(InteractionHand::Left));CHECK(!MagazineBlocksSupport(f.result,f.now));
    auto absent=f.result;absent.tracking.target.reset();CHECK(MagazineBlocksSupport(absent,f.now));
    f.Send();CHECK(f.result.interaction.phase==DetachableMagazinePhase::Attached);
}return 0;}
int RestoreEmptyAfterSwitch(){Fixture f;CHECK(f.Eject());f.adapter->Cancel(f.s.input,f.hands);
    ++f.native.owner.equipGeneration;f.binding.owner=f.native.owner;++f.binding.context.equipGeneration;f.s.nativeOwner=f.native.owner;f.s.family.binding.owner=f.native.owner;f.meshes->owner=f.native.owner;f.s.raw.owner=f.native.owner;
    ++f.s.input.owner.equipGeneration;++f.s.weapon.generation;f.s.family.binding.weapon=f.s.weapon;
    f.Send();CHECK(f.result.interaction.phase==DetachableMagazinePhase::WellEmpty);CHECK(f.calls==1);
    f.Insert();CHECK(f.calls==2&&f.counts.loaded==30);f.Send();CHECK(f.result.interaction.phase==DetachableMagazinePhase::Complete);return 0;
}
int SeatPublicationBoundary(){Fixture f;CHECK(f.Eject());f.completeNative=false;f.Insert();
    CHECK(f.result.interaction.phase==DetachableMagazinePhase::AwaitingSeat);
    CHECK(f.result.tracking.resource&&f.result.tracking.resource->submittedSeat);
    CHECK(f.result.tracking.target&&MagazineTargetFresh(f.result.tracking,f.now));
    CHECK(!MagazineBlocksSupport(f.result,f.now)&&f.result.blocksWeaponActions);
    const auto tracking=f.result.tracking;
    for(unsigned bad=0;bad<7;++bad){auto t=tracking;auto p=*tracking.resource;auto& r=*p.submittedSeat;
        if(bad==0)++r.intent.id;if(bad==1)++r.binding.inventory;if(bad==2)++r.intent.context.space;
        if(bad==3)r.intent.deadlineNs=f.now;if(bad==4)r.discard=true;
        if(bad==5){p.resource.request=p.seatedRequest;p.resource.requestState=AmmoResourceRequestState::Rejected;}
        if(bad==6)r.intent.operation=AmmunitionOperation::RemoveMagazine;
        t.resource=std::make_shared<const MagazineResourcePresentation>(p);CHECK(!MagazineTargetFresh(t,f.now));
    }
    // A later server publication may already contain changed counts while
    // the last owning client Update has not completed. No second request.
    f.Send(true);CHECK(f.result.interaction.phase==DetachableMagazinePhase::AwaitingSeat);
    CHECK(f.result.tracking.target&&MagazineTargetFresh(f.result.tracking,f.now));
    CHECK(!MagazineBlocksSupport(f.result,f.now)&&f.result.blocksWeaponActions&&f.calls==2);
    f.completeNative=true;f.Send(true);f.Send(true);
    CHECK(f.result.interaction.phase==DetachableMagazinePhase::Complete);CHECK(f.calls==2);
    return 0;
}
int CancelledSeatSurvivesContextChanges(){
    for(bool switched:{false,true}){Fixture f;CHECK(f.Eject());f.completeNative=false;f.Insert();
        CHECK(f.calls==2&&f.outstanding);f.adapter->Cancel(f.s.input,f.hands);
        if(switched){++f.native.owner.equipGeneration;f.binding.owner=f.native.owner;++f.binding.context.equipGeneration;
            f.s.nativeOwner=f.native.owner;f.s.family.binding.owner=f.native.owner;f.meshes->owner=f.native.owner;f.s.raw.owner=f.native.owner;
            ++f.s.input.owner.equipGeneration;++f.s.weapon.generation;f.s.family.binding.weapon=f.s.weapon;}
        f.Send();CHECK(f.result.blocksWeaponActions);CHECK(f.calls==2);
        f.completeNative=true;f.Send();
        // Consume the terminal outcome after the original hand/view lifetimes.
        f.now+=400*Ms;f.Send();CHECK(f.result.interaction.phase==DetachableMagazinePhase::Attached);
        CHECK(!f.result.blocksWeaponActions&&f.counts.loaded==30&&f.counts.reserve==53&&f.calls==2);
        CHECK(f.result.bodyAmmo);CHECK(f.Eject());CHECK(f.calls==3);f.Insert();f.Send();
        CHECK(f.result.interaction.phase==DetachableMagazinePhase::Complete&&f.counts.loaded==30&&f.counts.reserve==23&&f.calls==4);
    }return 0;
}
}
int CurrentBatchLifecycle(){unsigned rows=0;
    for(const auto& row:RegisteredMagazineNativeProfiles()){
        if(std::uint64_t(row.id)<2)continue;const auto p=FindMagazineEquipment(row.id);CHECK(p&&p->Ready());++rows;
        const auto capacity=p->native->configuration.values.baseCapacity;
        for(unsigned change=0;change<2;++change){Fixture f(*p);f.counts={capacity-3,capacity*4,capacity};
            CHECK(f.Eject());f.Insert();f.Send();CHECK(f.result.interaction.phase==DetachableMagazinePhase::Complete);
            CHECK(f.calls==2&&f.counts.loaded==capacity&&!f.adapter->BlocksEquipment());
            // Immediately begin another removal from the completed full gun.
            CHECK(f.Eject());CHECK(f.calls==3&&f.counts.loaded==0&&f.counts.reserve==capacity*3);
            f.completeNative=false;f.Insert();CHECK(f.calls==4&&f.outstanding);
            // Same-gun stow/draw/equip epoch and tracking/vehicle-space change
            // retain the original service operation across lost hand custody.
            f.SameWeaponContextChange(change);f.Send();CHECK(f.result.blocksWeaponActions&&f.calls==4);
            f.completeNative=true;f.Send();f.now+=400*Ms;f.Send();
            CHECK(f.result.interaction.phase==DetachableMagazinePhase::Attached&&!f.adapter->BlocksEquipment());
            CHECK(f.counts.loaded==capacity&&f.counts.reserve==capacity*2&&f.calls==4);
            CHECK(f.result.bodyAmmo);CHECK(f.Eject());CHECK(f.calls==5);f.Insert();f.Send();
            CHECK(f.result.interaction.phase==DetachableMagazinePhase::Complete&&f.calls==6);
            CHECK(f.counts.loaded==capacity&&f.counts.reserve==capacity&&!f.adapter->BlocksEquipment());
        }
    }
    CHECK(rows==12);return 0;
}
int main(){return CurrentBatchLifecycle();}
