#include "Test.h"
#include "Bc2MagazineResourceReload.h"
#include "Bc2InputBinding.h"
#include <cstring>
#include <cstdio>
#include <sstream>
#include <functional>
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
    bool publishNative=true,hideRead=false,deferDrain=false;std::function<void(Fixture&)> beforeTick;
    std::optional<AmmoResourceReadReason> forceReadReason;
    Fixture(const MagazineEquipmentProfile& equipment=Xm8MagazineEquipment(),bool typed=false):profile(&equipment){
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
            auto& f=*static_cast<Fixture*>(p);auto view=f.hideRead?std::optional<AmmoResourceView>{}:f.channel.Read(o,now);
            if(view&&view->receipt){auto& r=*view->receipt;
                if(f.receiptFault==1)++r.command.context.equipGeneration;
                if(f.receiptFault==2)r.copiesVerified=false;
                if(f.receiptFault==3)r.completedNs=now+1;
            }return view;};
        api.submit=[](void* p,const AmmoResourceRequest& r)noexcept{return static_cast<Fixture*>(p)->channel.Submit(r);};
        api.outcome=[](void* p,std::uint64_t request,const AmmoResourceContext& context)noexcept{return static_cast<Fixture*>(p)->channel.Outcome(request,context);};
        MagazinePhysicalApi playerApi;playerApi.context=this;playerApi.resourceRead=api.read;playerApi.resourceSubmit=api.submit;
        playerApi.resourceOutcome=api.outcome;
        if(typed)playerApi.resourceReadObserved=[](void* p,const ReloadStateOwner& owner,std::int64_t now)noexcept{
            auto& f=*static_cast<Fixture*>(p);
            if(f.forceReadReason)return AmmoResourceRead{*f.forceReadReason,{}};
            if(f.hideRead)return AmmoResourceRead{AmmoResourceReadReason::NoView,{}};
            return f.channel.ReadObserved(owner,now);
        };
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
        s.raw.rawLeftWristWorldMeters=InsertionHandWorld(p,travel,Pose());
        s.raw.inputEvidence=s.input;s.originalHandEvidence=s.input;
        hands.Update(s.input);HandContactProof proof{{1002,1},sequence,s.input.deadlineNs,true};
        if(gun)gun=hands.Renew(s.input,gun->token,proof).claim;
        if(!gun)gun=hands.Acquire(s.input,{s.input.owner,InteractionHand::Right,HandClaimKind::GunHold,s.weapon,proof,++intent,0}).claim;
        if(publishNative)Select();if(beforeTick)beforeTick(*this);result=adapter->Tick(s,hands,intent);if(!deferDrain)Drain();
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
int IdleViewFailureSurvivesStartupCancellation(){
 Fixture f(Xm8MagazineEquipment(),true);
 f.s.input.focused=false;f.Send();f.s.input.focused=true;f.Send();
 f.forceReadReason=AmmoResourceReadReason::Owner;f.Send();const auto sequence=f.sequence;
 std::ostringstream first;f.adapter->Report(first);
 const auto marker="\"resource_read_reason\":"+std::to_string(unsigned(AmmoResourceReadReason::Owner));
 CHECK(first.str().find("\"first_cancel_by_cause\":[")!=std::string::npos);
 CHECK(first.str().find(marker)!=std::string::npos);
 CHECK(first.str().find("\"input\":"+std::to_string(sequence)+",")!=std::string::npos);
 f.forceReadReason=AmmoResourceReadReason::Invalid;for(unsigned n=0;n<100;++n)f.Send();
 std::ostringstream later;f.adapter->Report(later);
 CHECK(later.str().find(marker)!=std::string::npos);
 CHECK(later.str().find("\"input\":"+std::to_string(sequence)+",")!=std::string::npos);
 CHECK(f.calls==0&&f.counts.loaded==27&&f.counts.reserve==83);return 0;
}
int IdleResourceGapBlocksAdmission(){
    unsigned profiles=0;
    for(const auto* profile:{&Xm8MagazineEquipment(),FindMagazineEquipment("AEK971_sp"),FindMagazineEquipment("F2000_sp")}){
        if(!profile||!profile->Ready())continue;++profiles;
        for(bool missing:{false,true}){
            Fixture f(*profile);f.Send();const auto nativeView=*f.channel.Read(f.native.owner,f.now);
            const auto gun=f.gun->token;const auto counts=f.counts;
            f.publishNative=false;f.hideRead=missing;
            // Real native publication expires while real input and selected
            // identity keep advancing. No native lease is renewed by waiting.
            if(!missing)for(unsigned n=0;n<4;++n)f.Send();
            std::optional<HandClaim> support;
            for(unsigned n=0;n<20;++n){
                f.Send(true,true);CHECK(f.result.interaction.phase==DetachableMagazinePhase::Attached);
                CHECK(f.result.idleAwaitingResource&&f.result.idleFireContinuity&&f.result.blocksWeaponActions&&f.adapter->BlocksEquipment());
                CHECK(!f.result.ownsLeftHand&&!MagazineBlocksSupport(f.result,f.now));
                CHECK(!f.result.tracking.enabled&&!f.result.tracking.target&&!f.result.tracking.resource&&!f.result.bodyAmmo);
                CHECK(!f.result.interaction.prop&&!f.result.interaction.transaction.request&&!MagazineTrackingFresh(f.result.tracking,f.now));
                CHECK(f.result.tracking.family.binding.profile==profile&&f.gun->token==gun);
                CHECK(!f.adapter->ResourceProbeState(f.now).resource&&f.calls==0&&f.counts==counts);
                ActionOutput out;out.held=out.pressed=Fire|AlternateFire|Use|NextWeapon|PreviousWeapon|Reload;
                ApplyMagazinePhysicalActions(out,profile,f.result);CHECK(out.held==Fire&&out.pressed==Fire);
                HandContactProof contact{{1003,1},f.sequence,f.s.input.deadlineNs,true};
                if(support)support=f.hands.Renew(f.s.input,support->token,contact).claim;
                else support=f.hands.Acquire(f.s.input,{f.s.input.owner,InteractionHand::Left,HandClaimKind::WeaponSupport,f.s.weapon,contact,++f.intent,gun.id}).claim;
                CHECK(support&&support->token.prerequisiteClaim==gun.id);
            }
            const auto& retained=f.service.View()->snapshot;
            CHECK(retained.context==nativeView.snapshot.context&&retained.sequence==nativeView.snapshot.sequence&&
                retained.observedNs==nativeView.snapshot.observedNs&&retained.deadlineNs==nativeView.snapshot.deadlineNs&&retained.counts==nativeView.snapshot.counts);
            f.publishNative=true;f.hideRead=false;f.Send();
            CHECK(f.result.interaction.phase==DetachableMagazinePhase::Attached&&!f.result.idleAwaitingResource);
            CHECK(!f.result.blocksWeaponActions&&f.adapter->ResourceProbeState(f.now).resource);
            // The same persistent consumer can now submit and complete exactly
            // one normal removal after actual fresh native evidence returns.
            CHECK(f.Eject());CHECK(f.calls==1&&f.counts.loaded==0&&f.counts.reserve==counts.reserve);
        }
    }
    CHECK(profiles>0);std::printf("Idle native gaps: %u profiles; no view and expired view, preserved support/Fire, blocked reload/equipment, normal resume.\n",profiles);return 0;
}
int IdleResourceGapKeepsFireLevel(){
    unsigned schedules=0;
    for(const auto* profile:{&Xm8MagazineEquipment(),FindMagazineEquipment("AEK971_sp"),FindMagazineEquipment("F2000_sp")}){
        if(!profile||!profile->Ready())continue;
        for(auto reason:{AmmoResourceReadReason::Busy,AmmoResourceReadReason::NoView,AmmoResourceReadReason::Revision,
                         AmmoResourceReadReason::Future,AmmoResourceReadReason::Expired}){
            Fixture f(*profile,true);f.Send();const auto counts=f.counts;const auto gun=f.gun->token;
            f.forceReadReason=reason;
            std::optional<HandClaim> support;
            for(unsigned n=0;n<8;++n){
                if(n%2)f.Repeat();else f.Send(true);
                CHECK(f.result.idleAwaitingResource&&f.result.blocksWeaponActions&&f.adapter->BlocksEquipment());
                CHECK(!f.adapter->ResourceProbeState(f.now).resource);
                HandContactProof contact{{1003,1},f.sequence,f.s.input.deadlineNs,true};
                if(support)support=f.hands.Renew(f.s.input,support->token,contact).claim;
                else support=f.hands.Acquire(f.s.input,{f.s.input.owner,InteractionHand::Left,HandClaimKind::WeaponSupport,
                    f.s.weapon,contact,++f.intent,gun.id}).claim;
                CHECK(support&&support->token.prerequisiteClaim==gun.id&&!MagazineBlocksSupport(f.result,f.now));
                ActionOutput out;out.active=true;out.owner=f.native.owner.soldier;
                out.held=Fire|AlternateFire|Reload|Use|NextWeapon|PreviousWeapon;
                ApplyMagazinePhysicalActions(out,profile,f.result);
                CHECK(out.held==Fire&&out.pressed==0);
                std::array<std::byte,InputBytes> cache{};InputOverride patch;
                CHECK(patch.Apply(cache,out));patch.Commit();float fire=0;
                std::memcpy(&fire,cache.data()+8+4*unsigned(EntryAction::Fire),sizeof fire);CHECK(fire==1.f);
                // Earlier semantic/mechanism suppression and trigger release
                // stay neutral: this filter cannot reassert Fire or an edge.
                out.held=out.pressed=0;ApplyMagazinePhysicalActions(out,profile,f.result);
                CHECK(patch.Apply(cache,out));patch.Commit();
                std::memcpy(&fire,cache.data()+8+4*unsigned(EntryAction::Fire),sizeof fire);
                CHECK(fire==0.f&&out.pressed==0&&f.calls==0&&f.counts==counts);
            }
            f.forceReadReason.reset();f.Send();
            CHECK(!f.result.idleAwaitingResource&&!f.result.blocksWeaponActions&&f.calls==0);++schedules;
        }
    }
    // Actual channel timing: a native publication can advance between hand
    // sampling and resource reading while the original controller lease lives.
    Fixture future(Xm8MagazineEquipment(),true);future.Send();
    future.beforeTick=[](Fixture& f){f.now+=Ms;f.Select();};future.Send();
    CHECK(future.channel.ReadObserved(future.native.owner,future.s.input.nowNs,future.now).reason==AmmoResourceReadReason::Future);
    CHECK(future.channel.Read(future.native.owner,future.now)&&future.result.idleAwaitingResource&&future.result.idleFireContinuity);
    ActionOutput held;held.held=Fire;ApplyMagazinePhysicalActions(held,future.profile,future.result);
    CHECK(held.held==Fire&&held.pressed==0&&future.calls==0);
    CHECK(schedules>0);std::printf("Idle held Fire: %u fresh/duplicate publication-gap schedules, native cache continuity and real Future ordering.\n",schedules);
    return 0;
}
int IdleFireCannotUseRetiredOrPendingResource(){
    for(auto state:{AmmoResourceRequestState::Queued,AmmoResourceRequestState::Dispatched,
                    AmmoResourceRequestState::Uncertain,AmmoResourceRequestState::ActorRetired}){
        Fixture f(Xm8MagazineEquipment(),true);
        f.beforeTick=[state](Fixture& x){auto v=*x.service.View();v.requestState=state;x.channel.Publish(v);};
        f.Send();f.beforeTick={};f.forceReadReason=AmmoResourceReadReason::Busy;f.Send();
        CHECK(f.result.idleAwaitingResource&&!f.result.idleFireContinuity&&f.result.blocksWeaponActions&&f.adapter->BlocksEquipment()&&f.calls==0);
        ActionOutput out;out.held=out.pressed=Fire|AlternateFire|Reload|Use|NextWeapon|PreviousWeapon;
        ApplyMagazinePhysicalActions(out,f.profile,f.result);CHECK(out.held==0&&out.pressed==0);
    }
    for(auto reason:{AmmoResourceReadReason::Owner,AmmoResourceReadReason::Invalid}){
        Fixture f(Xm8MagazineEquipment(),true);f.Send();f.forceReadReason=reason;f.Send();
        CHECK(!f.result.idleAwaitingResource&&!f.result.idleFireContinuity&&f.calls==0);
    }
    Fixture retired(Xm8MagazineEquipment(),true);
    retired.beforeTick=[](Fixture& f){auto v=*f.service.View();v.terminal.emplace();
        v.terminal->state=AmmoResourceRequestState::ActorRetired;f.channel.Publish(v);};
    retired.Send();retired.beforeTick={};retired.forceReadReason=AmmoResourceReadReason::Busy;retired.Send();
    CHECK(retired.result.idleAwaitingResource&&!retired.result.idleFireContinuity&&retired.result.blocksWeaponActions&&retired.adapter->BlocksEquipment()&&retired.calls==0);
    ActionOutput out;out.held=out.pressed=Fire|AlternateFire|Reload|Use|NextWeapon|PreviousWeapon;
    ApplyMagazinePhysicalActions(out,retired.profile,retired.result);CHECK(out.held==0&&out.pressed==0);
    return 0;
}
int IdleResourceGapRejectsLifetimeLoss(){
    for(unsigned fault=0;fault<21;++fault){Fixture f;f.Send();f.hideRead=true;
        f.beforeTick=[fault](Fixture& x){switch(fault){
            case 0:++x.s.nativeOwner.equipGeneration;break;
            case 1:++x.s.input.owner.equipGeneration;break;
            case 2:++x.s.weapon.id;break;
            case 3:++x.s.trackingEpoch;break;
            case 4:x.s.family.verified=false;break;
            case 5:++x.s.family.binding.inventory;break;
            case 6:x.s.family.binding.profile=nullptr;break;
            case 7:x.s.family.deadlineNs=x.now;break;
            case 8:x.s.family.observedNs=x.now+1;break;
            case 9:x.s.asset="foreign_configuration";break;
            case 10:x.hands.Release(x.s.input,x.gun->token);break;
            case 11:x.hands.Release(x.s.input,x.gun->token);x.gun=x.hands.Acquire(x.s.input,
                {x.s.input.owner,InteractionHand::Right,HandClaimKind::GunHold,x.s.weapon,{{1002,1},x.sequence,x.s.input.deadlineNs,true},++x.intent,0}).claim;break;
            case 12:x.s.input.deadlineNs=x.now;break;
            case 13:x.s.input.observedNs=x.now+1;break;
            case 14:x.s.cancel=true;break;
            case 15:x.s.input.released[1]=true;break;
            case 16:x.s.input.focused=false;break;
            case 17:x.s.input.tracked[0]=false;break;
            case 18:x.s.family.binding.equipment.data++;break;
            case 19:++x.s.nativeOwner.actorGeneration;break;
            case 20:++x.s.input.owner.actorGeneration;break;
        }};
        f.Send();CHECK(f.result.interaction.phase==DetachableMagazinePhase::Cancelled);
        CHECK(!f.result.idleAwaitingResource&&!f.result.tracking.target&&f.calls==0);
        // Missing observations cannot revive an explicitly cancelled lifetime.
        f.beforeTick={};f.Send();CHECK(f.result.interaction.phase==DetachableMagazinePhase::Cancelled&&!f.result.idleAwaitingResource);
    }
    // No prior attached identity, active removed original, or in-flight native
    // request is eligible for this deliberately idle-only recovery.
    for(unsigned state=0;state<3;++state){Fixture f;
        if(state==1){f.Send();f.Send(true);}
        if(state==2){CHECK(f.Eject());}
        f.hideRead=true;f.Send();CHECK(f.result.interaction.phase==DetachableMagazinePhase::Cancelled);
        CHECK(!f.result.idleAwaitingResource&&!f.result.tracking.resource);
    }
    std::puts("Idle wait rejects 21 lifetime/input/config/claim mutations, cancellation resurrection and non-idle states.");return 0;
}
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
int OriginalMagazineTrackingInterruption(){
    Fixture f(Xm8MagazineEquipment(),true);f.Send();f.Send(true);
    for(float p:{.075f,.05f,.025f,0.f,-.025f,-.05f})f.Send(true,false,p);
    CHECK(f.result.interaction.phase==DetachableMagazinePhase::RemovedHeld);
    const auto original=f.service.View()->original;const auto oldHand=f.hands.Current(InteractionHand::Left)->token;
    CHECK(original&&f.calls==1&&f.counts.loaded==0&&f.counts.reserve==83);
    f.s.input.tracked[0]=false;
    for(unsigned n=0;n<8;++n){f.Send(true,false,-.05f);CHECK(!f.result.tracking.target&&!f.hands.Current(InteractionHand::Left));}
    f.s.input.tracked[0]=true;f.Repeat();
    CHECK(!f.hands.Current(InteractionHand::Left)&&f.service.View()->originalState==MagazineResourceState::Held);
    f.s.raw.valid=false;
    for(unsigned n=0;n<8;++n){f.Send(true,false,-.05f);CHECK(!f.result.tracking.target&&!f.hands.Current(InteractionHand::Left));}
    f.s.raw.valid=true;f.Send(true,false,-.05f);
    CHECK(f.service.View()->original==original&&f.service.View()->originalState==MagazineResourceState::Held);
    CHECK(f.calls==1&&f.counts.loaded==0&&f.counts.reserve==83);
    for(unsigned n=0;n<3;++n)f.Send(true,false,-.05f);
    CHECK(f.result.interaction.phase==DetachableMagazinePhase::RemovedHeld);
    CHECK(f.hands.Current(InteractionHand::Left)&&f.hands.Current(InteractionHand::Left)->token!=oldHand);
    for(float p:{-.025f,0.f,.025f,.05f,.075f,.1f,.1f,.1f,.1f,.1f,.1f,.1f}){
        f.Send(true,false,p);if(f.calls==2)break;}
    CHECK(f.calls==2&&f.counts.loaded==27&&f.counts.reserve==83);f.Send();
    CHECK(f.result.interaction.phase==DetachableMagazinePhase::Attached);
    f.Send();f.Send(true);CHECK(f.calls==3&&f.counts.loaded==0&&f.counts.reserve==83);
    return 0;
}
int OriginalSuspensionRequiresFreshCustodyAndExactResource(){
    for(unsigned fault=0;fault<7;++fault){Fixture f(Xm8MagazineEquipment(),true);f.Send();f.Send(true);
        for(float p:{.075f,.05f,.025f,0.f,-.025f,-.05f})f.Send(true,false,p);
        CHECK(f.result.interaction.phase==DetachableMagazinePhase::RemovedHeld);
        const auto original=f.service.View()->original;const auto raw=f.s.raw;
        f.adapter->Suspend(f.s.input,f.hands);f.adapter->Suspend(f.s.input,f.hands);f.hands.Reset();
        f.beforeTick=[&](Fixture& q){
            if(fault==0){q.hands.Reset();q.gun.reset();}
            if(fault==1){q.s.raw=raw;q.s.originalHandEvidence=raw.inputEvidence;}
            if(fault==2)++q.s.raw.owner.weapon;
            if(fault==3)++q.s.raw.rigFingerprint;
            if(fault==4){q.s.input.deadlineNs=q.now;q.hands.Update(q.s.input);}
            if(fault==5){auto v=*q.service.View();++v.original->rounds;q.channel.Publish(v);}
            if(fault==6)++q.s.input.owner.equipGeneration;
        };
        f.Send(true,false,-.05f);
        CHECK(!f.hands.Current(InteractionHand::Left)&&!f.result.tracking.target&&f.calls==1&&f.counts.loaded==0&&f.counts.reserve==83);
        if(fault<5){
            f.beforeTick={};f.s.raw.owner=f.s.nativeOwner;f.s.raw.rigFingerprint=f.profile->geometry->rigFingerprint;
            f.Send(true,false,-.05f);f.Send(true,false,-.05f);
            CHECK(f.service.View()->original==original&&f.service.View()->originalState==MagazineResourceState::Held);
            CHECK(f.result.interaction.phase==DetachableMagazinePhase::RemovedHeld&&f.hands.Current(InteractionHand::Left));
        }
    }return 0;
}
int SuspendedPendingUnseatNeedsExactCompletionAndFreshPull(){
    Fixture f(Xm8MagazineEquipment(),true);f.completeNative=false;f.Send();f.Send(true);
    CHECK(f.calls==1&&f.outstanding&&f.result.interaction.phase==DetachableMagazinePhase::PreparingRemoval);
    f.adapter->Suspend(f.s.input,f.hands);f.hands.Reset();
    for(unsigned n=0;n<8;++n){f.Send(true);CHECK(f.calls==1&&!f.result.tracking.target&&!f.hands.Current(InteractionHand::Left));}
    const auto call=*f.outstanding;
    // Preserve the helper invocation, but only fresh independently completed
    // branch Updates can prove convergence after this 160ms interruption.
    CHECK(!f.service.Observe(f.Row(2,call),f.now+100));
    for(unsigned branch:{0,1,2}){auto row=f.Row(branch,call);
        row.invocation+=100;row.beginNs=f.now-10;row.endNs=f.now;
        row.loadedBefore=row.loadedAfter;row.reserveBefore=row.reserveAfter;
        CHECK(f.service.Observe(row,f.now+100)==(branch==2));
    }
    const auto terminal=f.service.Outcome();CHECK(terminal&&terminal->receipt);
    CHECK(terminal->request.intent.id!=terminal->receipt->command.id);
    f.outstanding.reset();f.completeNative=true;f.Send(true,false,-.05f);
    CHECK(f.calls==1&&!f.hands.Current(InteractionHand::Left)&&f.service.View()->originalState==MagazineResourceState::Held);
    f.Send(true);CHECK(f.result.interaction.phase==DetachableMagazinePhase::Pulling&&f.hands.Current(InteractionHand::Left));
    for(float p:{.075f,.05f,.025f,0.f,-.025f,-.05f})f.Send(true,false,p);
    CHECK(f.result.interaction.phase==DetachableMagazinePhase::RemovedHeld&&f.calls==1);
    for(float p:{-.025f,0.f,.025f,.05f,.075f,.1f,.1f,.1f,.1f,.1f,.1f,.1f}){
        f.Send(true,false,p);if(f.calls==2)break;}
    CHECK(f.calls==2&&f.counts.loaded==27&&f.counts.reserve==83);return 0;
}
int OriginalMagazineMissingInputInterval(){
    Fixture f(Xm8MagazineEquipment(),true);f.Send();f.Send(true);
    for(float p:{.075f,.05f,.025f,0.f,-.025f,-.05f})f.Send(true,false,p);
    CHECK(f.result.interaction.phase==DetachableMagazinePhase::RemovedHeld);
    const auto original=f.service.View()->original;const auto oldHand=f.hands.Current(InteractionHand::Left)->token;
    f.now+=250*Ms;f.Send(true,false,-.05f);
    CHECK(f.service.View()->original==original&&f.service.View()->originalState==MagazineResourceState::Held);
    CHECK(!f.result.tracking.target&&!f.hands.Current(InteractionHand::Left)&&f.calls==1);
    f.Send(true,false,-.05f);f.Send(true,false,-.05f);
    CHECK(f.result.interaction.phase==DetachableMagazinePhase::RemovedHeld&&f.hands.Current(InteractionHand::Left));
    CHECK(f.hands.Current(InteractionHand::Left)->token!=oldHand&&f.calls==1&&f.counts.loaded==0&&f.counts.reserve==83);
    return 0;
}
int SuspendedOriginalStillHonorsRealRelease(){
    Fixture f(Xm8MagazineEquipment(),true);f.Send();f.Send(true);
    for(float p:{.075f,.05f,.025f,0.f,-.025f,-.05f})f.Send(true,false,p);
    CHECK(f.result.interaction.phase==DetachableMagazinePhase::RemovedHeld);
    const auto original=f.service.View()->original;
    f.adapter->Suspend(f.s.input,f.hands);f.hands.Reset();f.Send(false,false,-.05f);f.Send(false,false,-.05f);
    CHECK(f.service.View()->original==original&&f.service.View()->originalState==MagazineResourceState::Discarded);
    CHECK(!f.hands.Current(InteractionHand::Left)&&f.calls==1&&f.counts.loaded==0&&f.counts.reserve==83);
    f.Insert();f.Send();CHECK(f.calls==2&&f.counts.loaded==30&&f.counts.reserve==53);
    return 0;
}
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
int OriginalCustodySurvivesTypedPublicationGap(){
    for(const auto* profile:{&Xm8MagazineEquipment(),FindMagazineEquipment("AEK971_sp"),FindMagazineEquipment("F2000_sp")}){
        if(!profile||!profile->Ready())continue;
        for(auto reason:{AmmoResourceReadReason::Busy,AmmoResourceReadReason::NoView,AmmoResourceReadReason::Revision,
                         AmmoResourceReadReason::Future,AmmoResourceReadReason::Expired}){
            Fixture f(*profile,true);f.Send();f.Send(true);
            for(float travel:{.075f,.05f,.025f,0.f,-.025f,-.05f})f.Send(true,false,travel);
            CHECK(f.result.interaction.phase==DetachableMagazinePhase::RemovedHeld&&f.calls==1);
            const auto original=f.service.View()->original;const auto hand=f.hands.Current(InteractionHand::Left)->token;
            const auto oldTarget=f.result.tracking.target;f.forceReadReason=reason;
            for(unsigned n=0;n<20;++n){f.Send(true,false,-.05f);
                CHECK(f.result.interaction.phase==DetachableMagazinePhase::RemovedHeld&&f.result.ownsLeftHand);
                CHECK(f.result.blocksWeaponActions&&MagazineBlocksSupport(f.result,f.now));
                CHECK(!f.result.idleAwaitingResource);
                ActionOutput fire;fire.held=fire.pressed=Fire;ApplyMagazinePhysicalActions(fire,profile,f.result);
                CHECK(fire.held==0&&fire.pressed==0);
                CHECK(f.hands.Current(InteractionHand::Left)->token==hand&&f.calls==1);
                CHECK(!f.result.tracking.enabled&&!f.result.tracking.target&&!f.result.tracking.resource&&!f.result.bodyAmmo);
                CHECK(!f.adapter->ResourceProbeState(f.now).resource&&f.service.View()->original==original);
                CHECK(f.service.View()->originalState==MagazineResourceState::Held&&f.counts.loaded==0&&f.counts.reserve==83);
            }
            CHECK(oldTarget&&oldTarget->deadlineNs<f.now);
            f.forceReadReason.reset();f.Send(true,false,-.05f);
            CHECK(f.result.interaction.phase==DetachableMagazinePhase::RemovedHeld&&f.calls==1);
            CHECK(f.hands.Current(InteractionHand::Left)->token==hand);
            f.Send(false,false,-.05f);f.Send(false,false,-.05f);
            CHECK(f.result.interaction.phase==DetachableMagazinePhase::WellEmpty&&f.calls==1);
            CHECK(f.service.View()->originalState==MagazineResourceState::Discarded);
        }
    }return 0;
}
int PendingOriginalAndReleasedMagazineGaps(){
    for(unsigned mode=0;mode<3;++mode){Fixture f(Xm8MagazineEquipment(),true);f.Send();
        f.completeNative=mode!=0;f.Send(true);
        if(mode){for(float travel:{.075f,.05f,.025f,0.f,-.025f,-.05f})f.Send(true,false,travel);
            CHECK(f.result.interaction.phase==DetachableMagazinePhase::RemovedHeld);
            if(mode==2){f.Send(false,false,-.05f);CHECK(f.result.interaction.phase==DetachableMagazinePhase::WellEmpty);}}
        f.forceReadReason=AmmoResourceReadReason::Expired;
        const float heldTravel=mode==0?.1f:-.05f;
        for(unsigned n=0;n<15;++n){f.Send(mode==0,false,heldTravel);
            CHECK(f.result.interaction.phase!=DetachableMagazinePhase::Cancelled&&f.calls==1);
            CHECK(!f.result.interaction.transaction.acknowledged&&!f.result.interaction.transaction.completed);
            CHECK(!f.result.tracking.target&&!f.result.tracking.resource&&f.result.blocksWeaponActions&&!f.result.idleAwaitingResource);
            ActionOutput fire;fire.held=fire.pressed=Fire;ApplyMagazinePhysicalActions(fire,f.profile,f.result);
            CHECK(fire.held==0&&fire.pressed==0);}
        f.completeNative=true;f.forceReadReason.reset();f.Send(mode==0,false,heldTravel);f.Send(mode==0,false,heldTravel);
        CHECK(f.result.interaction.phase!=DetachableMagazinePhase::Cancelled&&f.calls==1);
        if(mode){CHECK(!f.hands.Current(InteractionHand::Left));
            CHECK(f.service.View()->originalState==MagazineResourceState::Discarded);
            const auto discarded=f.service.View()->request;
            for(unsigned n=0;n<5;++n)f.Send(false,false,-.05f);
            CHECK(f.service.View()->request==discarded&&f.calls==1);}
    }return 0;
}
int ReturnOriginalAfterActualExpiry(){
    Fixture f(Xm8MagazineEquipment(),true);f.Send();f.Send(true);
    for(float travel:{.075f,.05f,.025f,0.f,-.025f,-.05f})f.Send(true,false,travel);
    CHECK(f.result.interaction.phase==DetachableMagazinePhase::RemovedHeld);
    const auto original=f.service.View()->original;const auto counts=f.counts;
    f.publishNative=false;
    for(unsigned n=0;n<20;++n)f.Send(true,false,-.05f);
    CHECK(f.result.interaction.phase==DetachableMagazinePhase::RemovedHeld&&f.calls==1);
    CHECK(f.service.View()->original==original&&f.counts==counts);
    CHECK(!f.adapter->ResourceProbeState(f.now).resource);
    f.publishNative=true;f.Send(true,false,-.05f);
    for(float travel:{-.025f,0.f,.025f,.05f,.075f,.1f,.1f,.1f,.1f,.1f,.1f,.1f}){
        f.Send(true,false,travel);if(f.calls==2)break;}
    CHECK(f.calls==2&&f.counts.loaded==27&&f.counts.reserve==83);
    f.Send();CHECK(f.result.interaction.phase==DetachableMagazinePhase::Attached);
    return 0;
}
int PendingDiscardAcrossExpiredPublication(){
    Fixture f(Xm8MagazineEquipment(),true);f.Send();f.Send(true);
    for(float travel:{.075f,.05f,.025f,0.f,-.025f,-.05f})f.Send(true,false,travel);
    f.deferDrain=true;f.Send(false,false,-.05f);
    CHECK(f.result.interaction.phase==DetachableMagazinePhase::WellEmpty);
    f.publishNative=false;
    for(unsigned n=0;n<6;++n)f.Send(false,false,-.05f);
    CHECK(f.result.interaction.phase==DetachableMagazinePhase::WellEmpty&&f.calls==1);
    CHECK(!f.result.tracking.target&&!f.hands.Current(InteractionHand::Left));
    f.publishNative=true;f.deferDrain=false;
    for(unsigned n=0;n<4;++n){f.Send(false,false,-.05f);
        CHECK(f.result.interaction.phase==DetachableMagazinePhase::WellEmpty&&f.calls==1);}
    CHECK(f.service.View()->originalState==MagazineResourceState::Discarded);
    f.Insert();f.Send();CHECK(f.result.interaction.phase==DetachableMagazinePhase::Complete&&f.calls==2);
    CHECK(f.counts.loaded==30&&f.counts.reserve==53);
    return 0;
}
int ActiveGapStillRejectsLifetimeAndTimeout(){
    for(unsigned bad=0;bad<6;++bad){Fixture f(Xm8MagazineEquipment(),true);f.Send();f.completeNative=false;f.Send(true);
        CHECK(f.calls==1);f.forceReadReason=AmmoResourceReadReason::Expired;
        if(bad==0)f.forceReadReason=AmmoResourceReadReason::Owner;
        if(bad==1)f.forceReadReason=AmmoResourceReadReason::Invalid;
        if(bad==2)f.beforeTick=[](Fixture& x){++x.s.family.binding.inventory;};
        if(bad==3)f.beforeTick=[](Fixture& x){++x.s.input.owner.equipGeneration;x.hands.Update(x.s.input);};
        if(bad==4)f.beforeTick=[](Fixture& x){x.s.input.deadlineNs=x.now;x.hands.Update(x.s.input);};
        if(bad<5)f.Send(true);else for(unsigned n=0;n<210;++n)f.Send(true);
        CHECK(f.result.interaction.phase==DetachableMagazinePhase::Cancelled&&f.calls==1);
        CHECK(!f.hands.Current(InteractionHand::Left)&&!f.result.tracking.target);
        CHECK(f.counts.loaded==0&&f.counts.reserve==83);
    }return 0;
}
int ReplacementCustodyAcrossPublicationGaps(){
    unsigned profiles=0;
    for(const auto* profile:{&Xm8MagazineEquipment(),FindMagazineEquipment("AEK971_sp"),FindMagazineEquipment("F2000_sp")}){
        if(!profile||!profile->Ready())continue;++profiles;
        for(bool guided:{false,true})for(unsigned duration:{1u,20u}){
            Fixture f(*profile,true);CHECK(f.Eject());f.Send();f.Send(true,false,-.1f,true);
            if(guided)for(float p:{-.075f,-.04f,0.f,.035f})f.Send(true,false,p);
            CHECK(f.result.interaction.phase==DetachableMagazinePhase::ReplacementHeld||f.result.interaction.phase==DetachableMagazinePhase::Guided);
            const auto phase=f.result.interaction.phase;const auto hand=f.hands.Current(InteractionHand::Left)->token;
            const auto counts=f.counts;const auto old=f.result.tracking;
            f.forceReadReason=AmmoResourceReadReason::Expired;
            for(unsigned n=0;n<duration;++n){f.Send(true,false,guided?.035f:-.1f);
                CHECK(f.result.interaction.phase==phase&&f.result.ownsLeftHand&&f.result.blocksWeaponActions);
                CHECK(f.hands.Current(InteractionHand::Left)->token==hand&&f.counts==counts&&f.calls==1);
                CHECK(!f.result.tracking.enabled&&!f.result.tracking.target&&!f.result.tracking.resource&&!f.result.bodyAmmo);
                CHECK(!f.adapter->ResourceProbeState(f.now).resource&&!f.result.interaction.transaction.request);
            }
            f.forceReadReason.reset();f.Send(true,false,guided?.035f:-.1f);
            CHECK(f.result.interaction.phase!=DetachableMagazinePhase::Cancelled&&f.hands.Current(InteractionHand::Left)->token==hand);
            // After a long geometry gap, re-approach with real fresh samples.
            // Missing poses cannot count as travel or dwell on the magnetic rail.
            for(float p:{.01f,-.02f,-.05f,-.08f,-.1f,-.075f,-.04f,0.f,.035f,.07f,.1f,.1f,.1f,.1f,.1f,.1f}){
                f.Send(true,false,p);if(f.calls==2)break;
            }
            CHECK(f.calls==2&&f.counts.loaded==30&&f.counts.reserve==53);
            f.Send();CHECK(f.result.interaction.phase==DetachableMagazinePhase::Complete&&!f.result.blocksWeaponActions);
        }
    }
    CHECK(profiles>0);std::printf("Replacement custody gaps: %u compiled profiles; no new supply, stale seat, receipt or ammunition.\n",profiles);return 0;
}
int ReplacementGapReleaseAndSafety(){
    for(unsigned bad=0;bad<9;++bad){Fixture f(Xm8MagazineEquipment(),true);CHECK(f.Eject());f.Send();f.Send(true,false,-.1f,true);
        CHECK(f.result.ownsLeftHand);f.forceReadReason=AmmoResourceReadReason::Expired;
        if(bad==1)f.forceReadReason=AmmoResourceReadReason::Owner;
        if(bad==2)f.forceReadReason=AmmoResourceReadReason::Invalid;
        f.beforeTick=[bad](Fixture& x){
            if(bad==3)++x.s.family.binding.inventory;
            if(bad==4)++x.s.trackingEpoch;
            if(bad==5){x.s.input.focused=false;x.hands.Update(x.s.input);}
            if(bad==6){x.s.input.tracked[0]=false;x.hands.Update(x.s.input);}
            if(bad==7){x.s.input.deadlineNs=x.now;x.hands.Update(x.s.input);}
            if(bad==8)x.hands.Release(x.s.input,x.gun->token);
        };
        f.Send(bad!=0,false,-.1f);
        CHECK(!f.hands.Current(InteractionHand::Left)&&f.calls==1&&f.counts.loaded==0&&f.counts.reserve==83);
        if(bad){CHECK(f.result.interaction.phase==DetachableMagazinePhase::Cancelled);continue;}
        CHECK(f.result.interaction.phase==DetachableMagazinePhase::WellEmpty&&!f.result.ownsLeftHand);
        for(unsigned n=0;n<20;++n){f.Send(true,false,-.1f,true);CHECK(!f.result.ownsLeftHand&&f.calls==1);}
        f.forceReadReason.reset();f.beforeTick={};f.Send(false,false,-.1f);f.Insert();f.Send();
        CHECK(f.result.interaction.phase==DetachableMagazinePhase::Complete&&f.calls==2);
    }return 0;
}
int ReplacementAndSeatActualPublicationExpiry(){
    for(unsigned phase=0;phase<3;++phase){Fixture f(Xm8MagazineEquipment(),true);
        CHECK(f.Eject());f.Send();f.Send(true,false,-.1f,true);
        CHECK(f.result.ownsLeftHand);f.completeNative=phase!=2;
        if(phase){for(float p:{-.075f,-.04f,0.f,.035f,.07f,.1f,.1f,.1f,.1f,.1f,.1f}){
            f.Send(true,false,p);if(f.calls==2)break;}
            CHECK(f.calls==2&&f.result.interaction.phase==DetachableMagazinePhase::AwaitingSeat);}
        const auto counts=f.counts;const auto calls=f.calls;f.publishNative=false;
        for(unsigned n=0;n<20;++n){f.Send(true,false,phase?.1f:-.1f);
            CHECK(f.result.interaction.phase!=DetachableMagazinePhase::Cancelled&&f.calls==calls&&f.counts==counts);
            CHECK(!f.result.interaction.transaction.acknowledged&&!f.result.interaction.transaction.completed);
        }
        CHECK(!f.adapter->ResourceProbeState(f.now).resource);
        if(phase==2){
            CHECK(f.outstanding);const auto call=*f.outstanding;
            // Deliver retained helper provenance, then model newly completed
            // branch Updates. Old callback timestamps cannot prove current
            // convergence after 400ms merely because the test delivers them now.
            CHECK(!f.service.Observe(f.Row(2,call),f.now+100));
            for(unsigned branch:{0,1,2}){auto row=f.Row(branch,call);
                row.invocation+=100;row.beginNs=f.now-10;row.endNs=f.now;
                row.loadedBefore=row.loadedAfter;row.reserveBefore=row.reserveAfter;
                const bool completed=f.service.Observe(row,f.now+100);CHECK(completed==(branch==2));
            }
            f.outstanding.reset();
        }
        f.publishNative=true;f.completeNative=true;f.Send(true,false,phase?.1f:-.1f);
        if(!phase){for(float p:{-.1f,-.1f,-.075f,-.04f,0.f,.035f,.07f,.1f,.1f,.1f,.1f,.1f,.1f}){
            f.Send(true,false,p);if(f.calls==2)break;}}
        f.Send();CHECK(f.result.interaction.phase==DetachableMagazinePhase::Complete||f.result.interaction.phase==DetachableMagazinePhase::Attached);
        CHECK(!f.result.blocksWeaponActions&&f.calls==2&&f.counts.loaded==30&&f.counts.reserve==53);
    }return 0;
}
int RenderAdmissionDoesNotRenewAmmunition(){
    for(const auto* profile:{&Xm8MagazineEquipment(),FindMagazineEquipment("AEK971_sp"),FindMagazineEquipment("F2000_sp")}){
        if(!profile||!profile->Ready())continue;
        Fixture f(*profile,true);f.Send();f.publishNative=false;
        for(unsigned n=0;n<4;++n)f.Send();
        const auto t=f.result.tracking;CHECK(t.resource&&t.target&&MagazineTargetFresh(t,f.now));
        const auto sourceDeadline=t.reserve.deadlineNs;const auto later=sourceDeadline+2*Ms;
        CHECK(later<t.inputEvidence.deadlineNs&&later<t.family.deadlineNs&&later<t.target->deadlineNs);
        CHECK(t.resource->presentationObservedNs==f.now);
        CHECK(!MagazineTrackingFresh(t,later)&&!MagazineTargetFresh(t,later));
        CHECK(MagazinePaletteTargetFresh(t,later));
        CHECK(!MagazineResourceReserve(t.resource->resource,later));
        auto input=t.inputEvidence;input.nowNs=later;
        CHECK(!MagazineResourceRequestFor(t.resource->resource,t.family,input,t.family.binding.weapon,90000,AmmunitionOperation::RemoveMagazine));
        std::array<std::array<std::byte,64>,1> posed{},ordinary{};posed[0][0]=std::byte{1};
        const auto choice=SelectMagazinePackedPalette(t,&t,later,posed,ordinary,true);
        CHECK(!choice.fallback&&choice.bytes.data()==posed.data());
        CHECK(t.reserve.deadlineNs==sourceDeadline&&t.resource->resource.snapshot.deadlineNs==sourceDeadline&&f.calls==0);
        for(unsigned fault=0;fault<12;++fault){auto changed=t;auto resource=std::make_shared<MagazineResourcePresentation>(*t.resource);
            changed.resource=resource;
            if(fault==0)changed.inputEvidence.deadlineNs=later;
            if(fault==1)changed.target->deadlineNs=later;
            if(fault==2)changed.family.deadlineNs=later;
            if(fault==3){auto meshes=std::make_shared<SelectedMeshesSnapshot>(*changed.selected);meshes->deadlineNs=later;changed.selected=meshes;}
            if(fault==4)++changed.owner.equipGeneration;
            if(fault==5)++changed.target->gunClaim.id;
            if(fault==6)resource->presentationObservedNs=later+1;
            if(fault==7)resource->presentationObservedNs=sourceDeadline;
            if(fault==8)resource->resource.wellEmpty=true;
            if(fault==9)++resource->resource.snapshot.context.equipGeneration;
            CHECK(SelectMagazinePackedPalette(t,fault==10?nullptr:&changed,later,posed,ordinary,fault!=11).fallback);
        }
        CHECK(!MagazinePaletteTargetFresh(t,t.inputEvidence.deadlineNs));
        auto legacy=t;auto resource=std::make_shared<MagazineResourcePresentation>(*t.resource);
        resource->presentationObservedNs=0;legacy.resource=resource;
        CHECK(!MagazinePaletteTargetFresh(legacy,later));
    }return 0;
}
int LivePhysicalIntentOutlivesItsConstructionPublication(){
    constexpr std::int64_t publication=90593482746000,seat=90593549920800,admission=90593617173700;
    Fixture f;f.Send();f.Send(true);CHECK(f.calls==1&&f.counts.loaded==0);
    f.now=publication;f.Select();const auto oldView=*f.service.View();
    f.publishNative=false;f.now=seat-20*Ms;f.Send(true,false,-.05f);
    const auto request=MagazineResourceRequestFor(oldView,f.s.family,f.s.input,f.s.weapon,++f.intent,AmmunitionOperation::ReturnMagazine);
    CHECK(request&&request->intent.deadlineNs==f.s.input.deadlineNs);
    CHECK(request->binding.deadlineNs==90593582746000&&request->intent.observedNs==seat);
    CHECK(request->sourceProof&&request->sourceProof->constructedNs==seat&&request->sourceProof->inputSequence==f.s.input.sequence);
    CHECK(request->sourceProof->snapshotSequence==oldView.snapshot.sequence&&request->sourceProof->snapshotObservedNs==publication);
    CHECK(f.channel.Submit(*request));f.now=admission;f.Select();
    const auto queued=f.channel.Take();CHECK(queued&&*queued==*request);
    const auto command=f.service.Accept(*queued,f.now);CHECK(command);
    CHECK(command->admissionDeadlineNs==request->intent.deadlineNs);
    CHECK(!f.service.Accept(*queued,f.now));
    CHECK(f.service.Dispatch(f.Snapshot(),f.now));
    const AmmoResourceNativeCall call{f.native,command->id,(f.invocation+=10),f.now+1,f.now+2,command->before,command->after,true,true,true};
    CHECK(f.service.Call(call));CHECK(!f.service.Observe(f.Row(0,call),f.now+100));CHECK(!f.service.Observe(f.Row(1,call),f.now+100));
    CHECK(f.service.Observe(f.Row(2,call),f.now+100));
    CHECK(command->after.loaded==27&&command->after.reserve==83);
    CHECK(f.service.Outcome()->request==*request&&f.service.Outcome()->state==AmmoResourceRequestState::Completed);
    CHECK(!f.service.FirstRejection());
    return 0;
}
}
int RetiredNativeActorReleasesOnlyOldPhysicalCustody(){
    {
        Fixture f;CHECK(f.Eject());f.deferDrain=true;f.Insert();
        CHECK(f.calls==1&&!f.service.Active()&&f.adapter->BlocksEquipment());
        const auto counts=f.counts;
        f.adapter->Cancel(f.s.input,f.hands);CHECK(f.adapter->BlocksEquipment());
        f.now+=Ms;f.s.input.nowNs=f.now;
        Bc2AmmoReserveLease r;r.identity=f.native;++r.identity.owner.actorGeneration;
        r.identity.owner.soldier+=0x100000;r.identity.owner.weak+=0x100000;r.identity.serverSoldier+=0x100000;
        r.sequence=1000;r.observedNs=f.now;r.deadlineNs=f.now+100*Ms;r.loaded=9;r.reserve=42;r.capacity=30;
        r.verified=r.allThreeIdle=true;
        const auto previous=*f.service.ActorIdentity();
        const AmmoResourceActorDrain drain{previous,{},100,2,f.now,r.deadlineNs,true};
        unsigned seen=0;std::optional<AmmoResourceRequest> queued;
        CHECK(!f.channel.RetireQueuedForActor(previous,[&](const auto& q)noexcept{queued=q;++seen;return false;}));
        CHECK(queued&&!f.service.Active()&&f.adapter->BlocksEquipment());
        CHECK(f.channel.RetireQueuedForActor(previous,[&](const auto& q)noexcept{
            ++seen;return q==queued&&f.service.RetireForReplacementActor(r,f.now,drain,q);
        }));
        CHECK(seen==2&&!f.channel.Take()&&!f.service.Active());
        const auto terminal=f.service.Outcome();CHECK(terminal&&terminal->request==*queued);
        CHECK(terminal->state==AmmoResourceRequestState::Rejected&&!terminal->nativeDispatched&&!terminal->receipt);
        CHECK(!f.channel.Submit(*queued)); // consumed request cannot re-enter
        CHECK(f.channel.Publish({},terminal));
        // New native actor may be holding another family. Durable no-call
        // recovery cannot depend on SelectMagazineRequestProfile first, since
        // this exact physical pending reservation was blocking that selection.
        f.s.nativeOwner=r.identity.owner;f.s.asset="SPAS12_sp";
        f.s.input.owner.actor=(std::uint64_t(r.identity.owner.weak)<<32)|r.identity.owner.soldier;
        f.s.input.owner.actorGeneration=r.identity.owner.actorGeneration;
        for(unsigned n=0;n<3;++n)f.adapter->Cancel(f.s.input,f.hands);
        CHECK(!f.adapter->BlocksEquipment()&&f.calls==1&&f.counts==counts);
        // Retirement is not a channel-wide flush. Preserve an unrelated
        // replacement request even if its source will later fail admission.
        AmmoResourceChannel other;auto fresh=*queued;++fresh.intent.id;fresh.binding.owner=r.identity.owner;
        CHECK(other.Submit(fresh));bool absent=false;
        CHECK(other.RetireQueuedForActor(previous,[&](const auto& q)noexcept{absent=!q;return true;}));
        CHECK(absent&&other.Take()==fresh&&!other.Submit(fresh));
    }
    for(unsigned fault=0;fault<7;++fault){
        Fixture f;CHECK(f.Eject());f.completeNative=false;f.Insert();
        CHECK(f.calls==2&&f.outstanding&&f.service.Active()&&f.adapter->BlocksEquipment());
        const auto before=f.counts;const auto calls=f.calls;const auto command=*f.service.Active();
        // A real callback changed native counts, but its exact three-copy
        // completion is absent. Ordinary Cancel must retain the obligation.
        f.adapter->Cancel(f.s.input,f.hands);CHECK(f.adapter->BlocksEquipment());
        f.service.Cancel(f.now);CHECK(f.service.Active());
        f.now+=Ms;f.s.input.nowNs=f.now;
        Bc2AmmoReserveLease r;r.identity=f.native;++r.identity.owner.actorGeneration;
        r.identity.owner.soldier+=0x100000;r.identity.owner.weak+=0x100000;r.identity.serverSoldier+=0x100000;
        r.sequence=1000;r.observedNs=f.now;r.deadlineNs=f.now+100*Ms;r.loaded=9;r.reserve=42;r.capacity=30;
        r.verified=r.allThreeIdle=true;
        const AmmoResourceActorDrain drain{*f.service.ActorIdentity(),f.service.Active(),100,2,f.now,r.deadlineNs,true};
        CHECK(f.service.RetireForReplacementActor(r,f.now,drain));
        auto terminal=*f.service.Outcome();CHECK(terminal.state==AmmoResourceRequestState::ActorRetired);
        if(fault==1)terminal.actorRetirement->event=0;
        if(fault==2)++terminal.actorRetirement->pending->context.resource.actor;
        if(fault==3)++terminal.actorRetirement->previous.owner.weak;
        if(fault==4)terminal.actorRetirement->replacement.serverSoldier=terminal.actorRetirement->previous.serverSoldier;
        if(fault==5)terminal.actorRetirement.reset();
        if(fault==6)terminal.nativeDispatched=false;
        CHECK(f.channel.Publish({},terminal));
        f.adapter->Cancel(f.s.input,f.hands);
        CHECK(f.adapter->BlocksEquipment()==bool(fault));
        CHECK(f.calls==calls&&f.counts==before&&!f.service.Active());
        CHECK(!f.service.Observe(f.Row(2,*f.outstanding),f.now));
        if(!fault){
            // Repeating the durable terminal result cannot spend/refund or
            // consume another object. No fresh new-actor resource is required
            // to stop quarantining a proven retired actor's old reservation.
            f.adapter->Cancel(f.s.input,f.hands);CHECK(!f.adapter->BlocksEquipment());
            CHECK(f.calls==calls&&f.counts==before);
            CHECK(terminal.actorRetirement->pending==command&&!terminal.receipt);
        }
    }
    std::puts("Physical ammo custody: only exact native-retirement terminal releases; six corrupt receipts stay blocked; no native transfer/rollback.");
    return 0;
}

int SupportContactDoesNotUnseatMagazine(){
 Fixture f;f.Send();const auto before=f.counts;
 f.s.raw.supportContactDistanceMeters=0;
 for(unsigned n=0;n<5;++n)f.Send(true);
 CHECK(!f.calls&&f.counts==before&&!f.hands.Current(InteractionHand::Left));
 CHECK(f.result.interaction.phase==DetachableMagazinePhase::Attached&&!f.result.blocksWeaponActions);
 // A deliberate fresh magazine grab at its closer contact remains functional.
 f.Send(false);f.s.raw.supportContactDistanceMeters=.15f;f.Send(true);
 CHECK(f.calls==1&&f.counts.loaded==0&&f.counts.reserve==before.reserve);
 CHECK(f.hands.Current(InteractionHand::Left)&&f.result.ownsLeftHand);
 return 0;
}
int HeldRemovalSurvivesRejectedExpiredAdmission(){
 unsigned profiles=0;
 for(const auto& registered:RegisteredMagazineNativeProfiles()){
  const auto* profile=FindMagazineEquipment(registered.id);
  if(!profile||!profile->Ready())continue;++profiles;
  const auto length=profile->geometry->interaction.insertion.travelMeters;
  for(unsigned fault=0;fault<8;++fault){
   Fixture f(*profile,true);f.Send(false,false,length);f.deferDrain=true;f.Send(true,false,length);
   const auto original=f.channel.Take();
   if(!original)std::printf("Removal fixture did not reach admission: %.*s profile=%llu phase=%u reason=%u fault=%u\n",
      int(profile->geometry->asset.size()),profile->geometry->asset.data(),static_cast<unsigned long long>(profile->nativeId),
      unsigned(f.result.interaction.phase),unsigned(f.result.interaction.reason),fault);
   CHECK(original&&f.calls==0);
   const auto hand=f.hands.Current(InteractionHand::Left)->token;
   const auto before=f.counts;
   // Actual coherent publications and held controller packets continue, while
   // the native admission point cannot take this particular request in time.
   for(unsigned n=0;n<(fault==7?31u:6u);++n)f.Send(true,false,length);
   CHECK(!f.service.Accept(*original,f.now));
   const auto rejected=f.service.Outcome();CHECK(rejected&&rejected->request==*original);
   CHECK(rejected->state==AmmoResourceRequestState::Rejected&&!rejected->nativeDispatched&&!rejected->receipt);
   CHECK(f.calls==0&&f.counts==before);
   if(fault==2)f.s.input.tracked[0]=false;
   if(fault==3)++f.counts.reserve;
   if(fault==6){f.hands.Release(f.s.input,hand);}
   if(fault==4||fault==5)f.beforeTick=[&](Fixture& q){
    auto v=*q.service.View();
    if(fault==4){v.terminal->request.intent.deadlineNs=q.now+Ms;}
    else v.terminal->nativeDispatched=true;
    q.channel.Publish(v);
   };
   f.Send(fault!=1,false,length);
   if(fault){CHECK(!f.channel.Take()&&f.calls==0);continue;}
   CHECK(f.result.interaction.phase==DetachableMagazinePhase::PreparingRemoval);
   CHECK(f.hands.Current(InteractionHand::Left)->token==hand);
   const auto retry=f.channel.Take();CHECK(retry&&retry->intent.id>original->intent.id);
   CHECK(retry->intent.observedNs==f.s.input.observedNs&&retry->sourceProof->inputSequence==f.s.input.sequence);
   CHECK(retry->intent.deadlineNs==f.s.input.deadlineNs&&retry->binding.context==original->binding.context);
   CHECK(!f.service.Accept(*original,f.now)); // old packet cannot dispatch later
   CHECK(f.channel.Submit(AmmoResourceRequest{} )==false);
   const auto command=f.service.Accept(*retry,f.now);CHECK(command);
   CHECK(f.service.Dispatch(f.Snapshot(),f.now));
   ++f.calls;f.outstanding=AmmoResourceNativeCall{f.native,command->id,(f.invocation+=10),f.now+1,f.now+2,command->before,command->after,true,true,true};
   f.counts=command->after;CHECK(f.service.Call(*f.outstanding));
   f.deferDrain=false;f.Drain();f.Send(true,false,length);
   CHECK(f.calls==1&&f.counts.loaded==0&&f.counts.reserve==before.reserve);
   CHECK(f.result.interaction.phase==DetachableMagazinePhase::Pulling);
  }
 }CHECK(profiles>0);std::printf("Expired removal admission: held grab recovery and seven cancellation cases across %u enrolled profiles.\n",profiles);return 0;
}
int main(){if(IdleResourceGapKeepsFireLevel()||IdleFireCannotUseRetiredOrPendingResource())return 1;if(OriginalMagazineTrackingInterruption()||OriginalSuspensionRequiresFreshCustodyAndExactResource()||SuspendedPendingUnseatNeedsExactCompletionAndFreshPull()||OriginalMagazineMissingInputInterval()||SuspendedOriginalStillHonorsRealRelease())return 1;if(RetiredNativeActorReleasesOnlyOldPhysicalCustody())return 1;if(HeldRemovalSurvivesRejectedExpiredAdmission()||SupportContactDoesNotUnseatMagazine())return 1;if(IdleViewFailureSurvivesStartupCancellation()||LivePhysicalIntentOutlivesItsConstructionPublication()||ReplacementAndSeatActualPublicationExpiry()||ReplacementCustodyAcrossPublicationGaps()||ReplacementGapReleaseAndSafety()||RenderAdmissionDoesNotRenewAmmunition()||OriginalCustodySurvivesTypedPublicationGap()||PendingOriginalAndReleasedMagazineGaps()||ReturnOriginalAfterActualExpiry()||PendingDiscardAcrossExpiredPublication()||ActiveGapStillRejectsLifetimeAndTimeout()||IdleResourceGapBlocksAdmission()||IdleResourceGapRejectsLifetimeLoss()||CompletionBetweenInputPackets()||DeferredCompletionCannotRenewOrChangeOwner()||NamespaceProof()||SharedHandAdapter()||ReturnOriginal()||RestoreEmptyAfterSwitch()||SeatPublicationBoundary()||CancelledSeatSurvivesContextChanges()||TerminalDoesNotRequireNewMagazineSelection())return 1;
    std::puts("BC2 resource hand adapter: measured XM8 rail, unequal native/physical identities, chest display, original return, refill and equip restoration passed (mock native Updates).");return 0;}
