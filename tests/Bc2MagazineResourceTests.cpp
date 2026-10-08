#include "Test.h"
#include "Bc2MagazineResourceReload.h"
#include <cstring>
#include <cstdio>
#include <sstream>
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
    const MagazineEquipmentProfile* profile=&Xm8MagazineEquipment();bool completeNative=true;unsigned receiptFault=0;
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
        api.context=this;api.read=[](void* p,const ReloadStateOwner& o,std::int64_t now)noexcept{
            auto& f=*static_cast<Fixture*>(p);auto view=f.channel.Read(o,now);
            if(view&&view->receipt){auto& r=*view->receipt;
                if(f.receiptFault==1)++r.command.context.equipGeneration;
                if(f.receiptFault==2)r.copiesVerified=false;
                if(f.receiptFault==3)r.completedNs=now+1;
            }return view;};
        api.submit=[](void* p,const AmmoResourceRequest& r)noexcept{return static_cast<Fixture*>(p)->channel.Submit(r);};
        api.outcome=[](void* p,std::uint64_t request,const AmmoResourceContext& context)noexcept{return static_cast<Fixture*>(p)->channel.Outcome(request,context);};
        MagazinePhysicalApi playerApi;playerApi.context=this;playerApi.resourceRead=api.read;playerApi.resourceSubmit=api.submit;
        playerApi.resourceOutcome=api.outcome;
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
    void Send(bool grip=false,bool eject=false,float travel=.1f,bool pouch=false){
        now+=20*Ms;++sequence;s.input.sequence=sequence;s.input.observedNs=s.input.nowNs=now;s.input.deadlineNs=now+100*Ms;
        s.input.released[0]=!grip;s.gripPressed=grip;s.ejectPressed=eject;s.geometrySequence=sequence;s.bodyFromHand=pouch?Pose():Pose(2);
        s.family.observedNs=now;s.family.deadlineNs=now+100*Ms;s.family.verified=true;
        if(s.weapon.id==native.owner.weapon)s.family.carried=SelectedCarriedWeaponEvidence{native.owner,s.input.owner,s.weapon,
            s.family.binding.equipment,binding.inventory,binding.switching,0,0,sequence,now,now+100*Ms,true};
        meshes->sequence=sequence;meshes->observedNs=now;meshes->deadlineNs=now+100*Ms;
        const auto& p=profile->geometry->interaction.insertion;
        s.raw.rawLeftWristWorldMeters=Multiply(p.itemFromHand,Multiply(*InverseRigid(p.itemFromInsertion),Multiply(TravelPose(p,travel),p.weaponFromEntry)));
        s.raw.inputEvidence=s.input;s.originalHandEvidence=s.input;
        hands.Update(s.input);HandContactProof proof{{1002,1},sequence,s.input.deadlineNs,true};
        if(gun)gun=hands.Renew(s.input,gun->token,proof).claim;
        if(!gun)gun=hands.Acquire(s.input,{s.input.owner,InteractionHand::Right,HandClaimKind::GunHold,s.weapon,proof,++intent,0}).claim;
        Select();result=adapter->Tick(s,hands,intent);Drain();
    }
    void Repeat(){
        // Native callbacks run independently of tracked input. Publish the
        // completed operation, then Gather again using the SAME input packet.
        now+=Ms;s.input.nowNs=now;Select();hands.Update(s.input);
        result=adapter->Tick(s,hands,intent);
    }
    bool Eject(){Send();Send(false,true);Send(false,true);Send(false);return result.interaction.phase==DetachableMagazinePhase::WellEmpty;}
    void Insert(){Send();Send(true,false,-.1f,true);for(float p:{-.075f,-.04f,0.f,.035f,.07f,.1f,.1f,.1f,.1f,.1f,.1f}){
        Send(true,false,p);if(result.interaction.phase==DetachableMagazinePhase::AwaitingSeat||result.interaction.phase==DetachableMagazinePhase::Complete)break;}}
};
int CompletionBetweenInputPackets(){
    unsigned profiles=0;
    for(const auto* profile:{&Xm8MagazineEquipment(),FindMagazineEquipment("AEK971_sp"),FindMagazineEquipment("F2000_sp")}){
        if(!profile||!profile->Ready())continue;++profiles;Fixture f(*profile);
        const float length=profile->geometry->interaction.insertion.travelMeters;
        f.Send(false,false,length);f.Send(true,false,length);
        CHECK(f.calls==1&&f.result.interaction.phase==DetachableMagazinePhase::PreparingRemoval);
        // Real service receipt and changed ammo arrive on a repeated packet.
        // No physical gesture or completion may be manufactured by that repeat.
        for(unsigned n=0;n<3;++n){f.Repeat();CHECK(f.result.interaction.phase==DetachableMagazinePhase::PreparingRemoval);CHECK(f.calls==1);}
        f.Send(true,false,length);
        if(f.result.interaction.phase!=DetachableMagazinePhase::Pulling){
            for(unsigned n=0;n<250&&f.result.interaction.phase==DetachableMagazinePhase::PreparingRemoval;++n)f.Send(true,false,length);
            std::fprintf(stderr,"Lost receipt reproduction: phase=%u reason=%u calls=%u loaded=%d reserve=%d\n",
                unsigned(f.result.interaction.phase),unsigned(f.result.interaction.reason),f.calls,f.counts.loaded,f.counts.reserve);
            std::ostringstream report;f.adapter->Report(report);std::fprintf(stderr,"%s\n",report.str().c_str());
        }
        CHECK(f.result.interaction.phase==DetachableMagazinePhase::Pulling);
        for(unsigned n=1;n<=12;++n)f.Send(true,false,length-(length+.05f)*n/12);
        CHECK(f.result.interaction.phase==DetachableMagazinePhase::RemovedHeld);
        for(unsigned n=1;n<=12;++n)f.Send(true,false,-.05f+(length+.05f)*n/12);
        for(unsigned n=0;n<20&&f.calls!=2;++n)f.Send(true,false,length);
        CHECK(f.calls==2&&f.result.interaction.phase==DetachableMagazinePhase::AwaitingOriginalReturn);
        for(unsigned n=0;n<3;++n){f.Repeat();CHECK(f.result.interaction.phase==DetachableMagazinePhase::AwaitingOriginalReturn);}
        f.Send(false,false,length);CHECK(f.result.interaction.phase==DetachableMagazinePhase::Attached);
        CHECK(f.counts.loaded==27&&f.counts.reserve==83);
        // Exercise the actual failed second-removal ordering with the SAME
        // persistent adapter and monotonically increasing native/request IDs.
        f.Send(true,false,length);CHECK(f.calls==3);
        for(unsigned n=0;n<3;++n)f.Repeat();
        f.Send(true,false,length);CHECK(f.result.interaction.phase==DetachableMagazinePhase::Pulling);
        for(unsigned n=1;n<=12;++n)f.Send(true,false,length-(length+.05f)*n/12);
        f.Send(false,false,-.05f);f.Send(false,false,-.1f);
        CHECK(f.result.interaction.phase==DetachableMagazinePhase::WellEmpty);
        f.Insert();CHECK(f.calls==4&&f.result.interaction.phase==DetachableMagazinePhase::AwaitingSeat);
        for(unsigned n=0;n<3;++n){f.Repeat();CHECK(f.result.interaction.phase==DetachableMagazinePhase::AwaitingSeat);CHECK(f.result.blocksWeaponActions);}
        f.Send(false,false,length);CHECK(f.result.interaction.phase==DetachableMagazinePhase::Complete);
        CHECK(f.counts.loaded==30&&f.counts.reserve==53&&f.calls==4);
        f.Send(false,false,length);CHECK(f.result.interaction.phase==DetachableMagazinePhase::Attached);
        for(unsigned n=0;n<10;++n)f.Repeat();CHECK(f.calls==4);
        std::ostringstream report;f.adapter->Report(report);
        CHECK(report.str().find("\"submitted\":5,\"completed\":5,\"rejected\":0")!=std::string::npos);
    }CHECK(profiles>0);std::printf("Repeated-input completion regression: %u compiled profiles, original return + second removal + refill.\n",profiles);return 0;
}
int DeferredCompletionCannotRenewOrChangeOwner(){
    for(unsigned fault=0;fault<6;++fault){Fixture f;f.Send();f.Send(true);CHECK(f.calls==1);
        f.Repeat();CHECK(f.result.interaction.phase==DetachableMagazinePhase::PreparingRemoval);
        if(fault<3){
            if(fault==0)f.s.cancel=true;
            if(fault==1)++f.s.input.owner.equipGeneration;
            if(fault==2){f.now=f.s.input.deadlineNs;f.s.input.nowNs=f.now;f.Select();
                f.result=f.adapter->Tick(f.s,f.hands,f.intent);}
            else f.Send(true);
        }else {f.receiptFault=fault-2;f.Send(true);}
        CHECK(f.result.interaction.phase==DetachableMagazinePhase::Cancelled);
        CHECK(!f.result.interaction.transaction.acknowledged&&!f.result.interaction.transaction.completed);
        CHECK(!f.result.tracking.target&&f.calls==1&&f.counts.loaded==0&&f.counts.reserve==83);
    }
    // Missing current resource publication never falls back to an old receipt.
    Fixture f;f.Send();f.Send(true);f.Repeat();f.channel.Publish({});
    ++f.s.input.sequence;f.s.input.nowNs=f.now+Ms;f.result=f.adapter->Tick(f.s,f.hands,f.intent);
    CHECK(f.result.interaction.phase==DetachableMagazinePhase::Cancelled&&f.calls==1);
    std::puts("Deferred completion rejects cancellation, changed owner, expired input, three corrupt receipts and missing current publication.");
    return 0;
}
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
int TerminalDoesNotRequireNewMagazineSelection(){
    for(unsigned mutation=0;mutation<7;++mutation){Fixture f;CHECK(f.Eject());f.completeNative=false;f.Insert();
        CHECK(f.calls==2&&f.outstanding&&f.adapter->BlocksEquipment());
        f.adapter->Cancel(f.s.input,f.hands);CHECK(f.adapter->BlocksEquipment());
        f.now+=20*Ms;
        for(unsigned branch:{0,1,2})CHECK(branch==2?f.service.Observe(f.Row(branch,*f.outstanding),f.now):!f.service.Observe(f.Row(branch,*f.outstanding),f.now));
        CHECK(f.service.Outcome());auto outcome=*f.service.Outcome();
        if(mutation==1)++outcome.request.intent.id;
        if(mutation==2)++outcome.request.intent.context.resource.weaponGeneration;
        if(mutation==3)outcome.receipt->copiesVerified=false;
        if(mutation==4)++outcome.receipt->after.reserve;
        if(mutation==5)outcome.resolvedNs=f.now+1;
        if(mutation==6)outcome.state=AmmoResourceRequestState::Uncertain;
        // A shell weapon has no magazine resource view. Terminal publication is
        // the actual old request outcome; no new counts, geometry, or authority.
        f.channel.Publish({},outcome);f.s.nativeOwner.weapon+=0x4000;f.s.asset="SPAS12_sp";f.s.family={};
        ++f.s.input.owner.equipGeneration;++f.s.weapon.generation;
        f.s.input.sequence=++f.sequence;f.s.input.nowNs=f.s.input.observedNs=f.now;f.s.input.deadlineNs=f.now+100*Ms;
        f.hands.Update(f.s.input);f.result=f.adapter->Tick(f.s,f.hands,f.intent);
        CHECK(!f.channel.Read(f.s.nativeOwner,f.now));CHECK(!f.result.tracking.target);CHECK(f.calls==2);
        CHECK(f.result.blocksWeaponActions==(mutation!=0));CHECK(f.adapter->BlocksEquipment()==(mutation!=0));
        if(!mutation){
            // Repeated cancellation cannot settle or charge the old supply twice.
            f.adapter->Cancel(f.s.input,f.hands);CHECK(!f.adapter->BlocksEquipment());
            CHECK(f.counts.loaded==30&&f.counts.reserve==53);
        }
    }return 0;
}
}
int main(){if(CompletionBetweenInputPackets()||DeferredCompletionCannotRenewOrChangeOwner()||NamespaceProof()||SharedHandAdapter()||ReturnOriginal()||RestoreEmptyAfterSwitch()||SeatPublicationBoundary()||CancelledSeatSurvivesContextChanges()||TerminalDoesNotRequireNewMagazineSelection())return 1;
    std::puts("BC2 resource hand adapter: measured XM8 rail, unequal native/physical identities, chest display, original return, refill and equip restoration passed (mock native Updates).");return 0;}
