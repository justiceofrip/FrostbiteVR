#include "Test.h"
#include <iostream>
#include "Bc2PhysicalFixedContainer.h"
#include "Bc2BodyAmmo.h"
using namespace fvr::bc2;using namespace fvr::interaction;
namespace {
using Matrix=fvr::math::Matrix4;
Matrix Identity(){return reload_insertion_detail::Identity();}
Matrix At(const FeedMotionProfile& m,double progress){
    if(m.kind==FeedMotionKind::FreePlacement)return Multiply(m.placement.sourceFromGrip,
        reload_insertion_detail::BlendRigid(m.placement.weaponFromSource,m.placement.weaponFromDestination,progress));
    return Multiply(feed_interaction_detail::Along(m.motionFromContact,m.kind,m.travel*progress),m.weaponFromMotion);
}
struct Fixture {
    AmmoResourceService service;HandInteraction hands;std::uint64_t intent=0,sequence=0;
    ReloadHoldIdentity native{{0x10000,0x20000,0x30000,0x40000,1,2,3},{0x50000,0x60000,0x70000},0x80000,0x90000,0xa0000};
    AmmoResourceBinding binding{native.owner,{{0x20000,1,0x40000,19},2,3},0xb0000,0xc0000,0xd0000,0xe0000,0,0};
    HandInteractionOwner owner{(std::uint64_t(0x30000)<<32)|0x20000,1,71,3};
    const FixedContainerEquipmentProfile* equipment;
    Bc2PhysicalFixedContainerSample sample{};Bc2PhysicalFixedContainerResult result{};
    std::optional<AmmoResourceRequest> submitted;std::optional<FixedContainerGate> gate;
    std::optional<Bc2FixedContainerRawContact> previousRaw;
    AmmunitionCounts counts{};bool delayedRaw=false;
    std::unique_ptr<Bc2PhysicalFixedContainer> physical;HandClaimToken gun{};
    std::int64_t now=1000000000;bool priorGrip=false,allowGate=true;unsigned calls=0;
    explicit Fixture(unsigned index=0):equipment(&generated::FixedContainerEquipment[index]){
        counts={2,20,equipment->interaction.capacity};
        Bc2PhysicalFixedContainerApi api;api.context=this;
        api.read=[](void* p,const ReloadStateOwner&,std::int64_t)noexcept{return static_cast<Fixture*>(p)->service.View();};
        api.withLedger=[](void* p,const AmmoResourceBinding& b,const HandInteractionOwner&,void* arg,
            bool (*fn)(void*,AmmunitionLedger&)noexcept)noexcept {auto& f=*static_cast<Fixture*>(p);
            return f.service.WithSelectedLedger(b,f.now,[&](AmmunitionLedger& ledger)noexcept{return fn(arg,ledger);});};
        api.submit=[](void* p,const AmmoResourceRequest& request)noexcept {auto& f=*static_cast<Fixture*>(p);++f.calls;
            if(!f.gate||!f.gate->blocksFire)return false;f.submitted=request;return true;};
        api.outcome=[](void* p,std::uint64_t request,const AmmoResourceContext& context)noexcept->std::optional<AmmoResourceOutcome>{
            const auto& outcome=static_cast<Fixture*>(p)->service.Outcome();
            return outcome&&outcome->request.intent.id==request&&outcome->request.intent.context==context?outcome:std::nullopt;
        };
        api.publishGate=[](void* p,const FixedContainerGate& g)noexcept {auto& f=*static_cast<Fixture*>(p);if(!f.allowGate)return false;f.gate=g;return true;};
        api.nextRequest=[](void* p)noexcept{return ++static_cast<Fixture*>(p)->intent;};
        physical=std::make_unique<Bc2PhysicalFixedContainer>(true,api);
    }
    Bc2PhysicalFixedContainerResult Send(const Matrix& wrist,bool grip,bool pouch=false,bool finish=false){
        now+=10000000;++sequence;binding.observedNs=now;binding.deadlineNs=now+100000000;
        AmmunitionSnapshot ammunition{binding.context,sequence,now,now+100000000,counts,true};
        service.Select(binding,native,ammunition,now);
        sample={};sample.nativeOwner=native.owner;sample.input={owner,sequence,now,now+100000000,now,true,{true,true},{priorGrip&&!grip,false}};
        sample.weapon={binding.context.resource.weapon,owner.equipGeneration};sample.equipment=equipment;
        sample.configurationPath=equipment->configurationPath;sample.mesh=equipment->mesh;sample.rigFingerprint=equipment->rigFingerprint;
        sample.geometrySequence=sequence;sample.trackingEpoch=owner.space;sample.bodyFromHand=Identity();
        const auto contact=Bc2PhysicalFixedContainer::BodyAmmoContact();for(unsigned n=0;n<3;++n)sample.bodyFromHand.values[3][n]=contact.centerMeters[n];
        sample.grip=grip;sample.gripPressed=grip&&!priorGrip;sample.finishLoading=finish;
        const auto before=hands.Current(InteractionHand::Left);
        const auto update=hands.Update(sample.input);
        if(before&&update.released[0]&&update.released[0]->reason==HandInteractionReason::Released)
            sample.releaseEvidence=FixedContainerReleaseEvidence{*before,*update.released[0],sample.input};
        if(!gun.id){const auto root=hands.Acquire(sample.input,{owner,InteractionHand::Right,HandClaimKind::GunHold,sample.weapon,
            {{300,1},sequence,sample.input.deadlineNs,true},++intent,0});if(root.claim)gun=root.claim->token;}
        else (void)hands.Renew(sample.input,gun,{{300,1},sequence,sample.input.deadlineNs,true});
        sample.raw.nativeOwner=native.owner;sample.raw.rig.soldier=native.owner.soldier;sample.raw.rig.weak=native.owner.weak;
        sample.raw.equipment=equipment;sample.raw.input=sample.input;sample.raw.weaponFromWrist=wrist;sample.raw.weaponWorldMeters=Identity();
        sample.raw.valid=true;sample.raw.grip=grip;sample.raw.gripPressed=sample.gripPressed;sample.raw.pouchContact=pouch;
        const auto currentRaw=sample.raw;if(delayedRaw&&previousRaw)sample.raw=*previousRaw;previousRaw=currentRaw;
        priorGrip=grip;result=physical->Tick(sample,hands,intent);return result;
    }
    void Hinge(unsigned step){const auto& motion=equipment->interaction.geometry[step].motion;
        Send(At(motion,0),false);Send(At(motion,0),true);
        for(unsigned n=1;n<=10;++n)Send(At(motion,n/10.),true);
        for(unsigned n=0;n<8;++n)Send(At(motion,1),true);
        if(motion.returnToStart){for(unsigned n=1;n<=10;++n)Send(At(motion,1-n/10.),true);
            for(unsigned n=0;n<8;++n)Send(At(motion,0),true);}
    }
};
int ExactSelectorsAndSupply(){
    for(const auto& e:generated::FixedContainerEquipment){
        CHECK(FindFixedContainerEquipment(e.configurationPath,e.mesh,e.rigFingerprint)==&e);
        CHECK(!FindFixedContainerEquipment(e.asset,e.mesh,e.rigFingerprint));
        CHECK(!FindFixedContainerEquipment(e.configurationPath,e.mesh,e.rigFingerprint+1));
        CHECK(fixed_container_detail::Profile(e.interaction));unsigned parts=0;
        for(const auto& p:e.parts)if(p.supplyPart){++parts;CHECK(feed_mechanism_detail::Pose(p.supplyFromPart));}
        CHECK(parts==2);CHECK(e.handShapes[2]);
    }
    Fixture f(1);const auto result=f.Send(At(f.equipment->interaction.geometry[2].motion,0),false);
    CHECK(result.bodyAmmo&&result.bodyVisual);BodyAmmoTracking body;body.fixed=result.bodyAmmo;body.visual=*result.bodyVisual;
    CHECK(BodyAmmoFresh(body,f.now));CHECK(body.visual.source.objectUnits==6);
    body.fixed->ammunition.counts.reserve=1;CHECK(!BodyAmmoFresh(body,f.now));
    return 0;
}
int GateBeforePhysicalAndExactOwner(){
    Fixture f;const auto& motion=f.equipment->interaction.geometry[0].motion;
    f.Send(At(motion,0),false);f.allowGate=false;
    CHECK(!f.Send(At(motion,0),true).mechanismOwnsHand);CHECK(!f.submitted);
    f.allowGate=true;f.Send(At(motion,0),false);const auto acquired=f.Send(At(motion,0),true);
    if(!acquired.mechanismOwnsHand)std::cerr<<"Fixed gate reacquire status="<<unsigned(acquired.status)
        <<" enabled="<<acquired.tracking.enabled<<" rawRecorded="<<f.hands.HasRecordedEvidence(f.sample.raw.input,InteractionHand::Left)
        <<" left="<<bool(f.hands.Current(InteractionHand::Left))<<" right="<<bool(f.hands.Current(InteractionHand::Right))<<'\n';
    CHECK(acquired.mechanismOwnsHand);
    CHECK(f.gate&&f.gate->blocksFire);
    f.sample.raw.nativeOwner.weapon++;const auto result=f.physical->Tick(f.sample,f.hands,f.intent);
    CHECK(!result.mechanismOwnsHand);CHECK(result.blocksFire);CHECK(!f.submitted);
    return 0;
}
int CylinderFlickReturnAndClose(){
    Fixture f;f.Hinge(0);CHECK(f.result.tracking.step==FixedContainerStep::EjectSpent);CHECK(f.result.blocksFire);
    f.Hinge(1);CHECK(f.result.tracking.step==FixedContainerStep::Insert);CHECK(f.result.blocksFire);CHECK(f.calls==0);
    for(unsigned n=0;n<f.equipment->parts.size();++n)if(f.equipment->parts[n].coupledOpening)
        CHECK(f.result.tracking.parts[n].values==f.equipment->parts[n].weaponFromStepStart[1].values);
    f.Send(At(f.equipment->interaction.geometry[2].motion,0),false,false,true);
    CHECK(f.result.tracking.step==FixedContainerStep::Close);f.Hinge(3);
    CHECK(f.result.tracking.step==FixedContainerStep::Complete);CHECK(!f.result.blocksFire);CHECK(f.calls==0);
    CHECK(f.service.View()->snapshot.counts.loaded==2&&f.service.View()->snapshot.counts.reserve==20);
    return 0;
}
int ClipQueuesOneSharedCommand(){
    for(unsigned index:{1u,2u,3u}){
        Fixture f(index);const auto& motion=f.equipment->interaction.geometry[2].motion;
        f.Send(At(motion,0),false);f.Send(At(motion,0),true,true);CHECK(f.result.ammoOwnsHand);
        f.Send(At(motion,0),true);for(unsigned n=1;n<=30;++n)f.Send(At(motion,n/30.),true);
        for(unsigned n=0;n<8;++n)f.Send(At(motion,1),true);f.Send(At(motion,1),false);
        CHECK(f.calls==1&&f.submitted&&f.submitted->preparedCommand);CHECK(f.result.tracking.pending&&f.result.blocksFire);
        const auto& command=*f.submitted->preparedCommand;CHECK(command.before.loaded==2&&command.after.loaded==8&&command.after.reserve==14);
        CHECK(f.service.View()->snapshot.counts.loaded==2); // Queuing is no receipt.
        f.physical->Cancel(f.sample.input,f.hands);CHECK(f.calls==1);
    }return 0;
}
int PriorRendererFrameQueuesAndCompletesOnceAfterReequip(){
    Fixture f(1);f.delayedRaw=true;const auto& motion=f.equipment->interaction.geometry[2].motion;
    f.Send(At(motion,0),false);f.Send(At(motion,0),true,true);f.Send(At(motion,0),true,true);
    CHECK(f.result.ammoOwnsHand);
    f.Send(At(motion,0),true);for(unsigned n=1;n<=30;++n)f.Send(At(motion,n/30.),true);
    for(unsigned n=0;n<8;++n)f.Send(At(motion,1),true);f.Send(At(motion,1),false);
    CHECK(f.calls==1&&f.submitted&&f.submitted->preparedCommand&&f.result.tracking.pending);
    CHECK(f.sample.raw.input.observedNs<f.service.View()->snapshot.observedNs);
    const auto request=*f.submitted;const auto command=*request.preparedCommand;
    CHECK(command.admissionDeadlineNs==f.sample.raw.input.deadlineNs);
    CHECK(request.intent.observedNs==f.sample.raw.input.observedNs);
    CHECK(f.service.Accept(request,f.now)==command);AmmoResourceNativeCall actual;unsigned nativeCalls=0;
    CHECK(f.service.InvokeNative(f.service.View()->snapshot,[&]()noexcept{return f.now;},[&](std::int64_t began)noexcept{
        ++nativeCalls;actual={f.native,command.id,700,began,began+1,command.before,command.after,true,true,true};return actual;
    }));
    f.physical->Cancel(f.sample.input,f.hands); // Policy still retains its queued original request.
    for(unsigned branch=0;branch<3;++branch){
        AmmoResourceOwnUpdate row;row.beforeOwner=row.afterOwner=f.native.owner;row.firing=f.native.firing[branch];
        row.serverPlayer=f.native.serverPlayer;row.serverSoldier=f.native.serverSoldier;row.serverItem=f.native.serverItem;
        row.invocation=actual.invocation+(branch==2?0:branch+1);row.beginNs=actual.beginNs-1;row.endNs=actual.endNs+10+branch;
        row.branch=branch;row.depth=1;row.loadedBefore=command.before.loaded;row.reserveBefore=command.before.reserve;
        row.loadedAfter=command.after.loaded;row.reserveAfter=command.after.reserve;row.current=row.next=2;
        row.finished=row.identityRetained=row.neutralContext=true;
        CHECK(f.service.Observe(row,actual.endNs+100)==(branch==2));
    }
    f.counts=command.after;++f.native.owner.equipGeneration;++f.native.owner.space;f.binding.owner=f.native.owner;
    ++f.binding.context.equipGeneration;++f.binding.context.space;++f.owner.equipGeneration;++f.owner.space;f.gun={};
    f.Send(At(motion,1),false); // New counts/context, old raw context: consume receipt without pose authority.
    CHECK(f.result.tracking.step==FixedContainerStep::Complete&&!f.result.tracking.pending);
    CHECK(!f.result.blocksFire&&!f.result.ammoOwnsHand&&f.calls==1&&nativeCalls==1);
    f.Send(At(motion,1),false);CHECK(f.calls==1&&nativeCalls==1);
    CHECK(f.service.View()->snapshot.counts==command.after);return 0;
}
int PartialCylinderMigrationReacquiresAtRetainedContact(){
    Fixture f;const auto& motion=f.equipment->interaction.geometry[0].motion;
    f.Send(At(motion,0),false);f.Send(At(motion,0),true);
    f.Send(At(motion,.1),true);f.Send(At(motion,.2),true);
    CHECK(f.result.tracking.hand&&f.result.tracking.target);
    const auto before=f.result.tracking;f.physical->Cancel(f.sample.input,f.hands);
    ++f.native.owner.equipGeneration;++f.native.owner.space;f.binding.owner=f.native.owner;
    ++f.binding.context.equipGeneration;++f.binding.context.space;++f.owner.equipGeneration;++f.owner.space;f.gun={};
    f.Send(At(motion,.2),false);CHECK(f.result.blocksFire&&!f.result.mechanismOwnsHand);
    CHECK(f.result.tracking.step==FixedContainerStep::Open);
    for(unsigned n=0;n<before.partCount;++n)CHECK(f.result.tracking.parts[n].values==before.parts[n].values);
    f.Send(At(motion,.2),false);f.Send(At(motion,.2),true);
    CHECK(f.result.mechanismOwnsHand&&f.result.tracking.hand&&f.result.tracking.target);
    CHECK(f.result.tracking.hand->token.owner!=before.hand->token.owner);
    CHECK(f.result.tracking.hand->token.id!=before.hand->token.id);
    CHECK(!f.hands.Renew(f.sample.input,before.hand->token,
        {before.hand->token.contact,f.sequence,f.sample.input.deadlineNs,true}).accepted);
    CHECK(f.calls==0&&f.service.View()->snapshot.counts==f.counts);return 0;
}
}
int main(){return ExactSelectorsAndSupply()||GateBeforePhysicalAndExactOwner()||CylinderFlickReturnAndClose()||ClipQueuesOneSharedCommand()||
    PriorRendererFrameQueuesAndCompletesOnceAfterReequip()||PartialCylinderMigrationReacquiresAtRetainedContact();}
