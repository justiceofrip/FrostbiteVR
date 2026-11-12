#include "Test.h"
#include <algorithm>
#include <iostream>
#include <sstream>
#include "Bc2PhysicalFixedContainer.h"
#include "Bc2BodyAmmo.h"
#include "fvr/interaction/SpentCaseEjection.h"
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
    Bc2FixedContainerObservation observation{};bool observe=false;
    std::optional<AmmoResourceRequest> submitted;std::optional<FixedContainerGate> gate;
    std::optional<Bc2FixedContainerRawContact> previousRaw;
    AmmunitionCounts counts{};bool delayedRaw=false,missingRaw=false,rendererFeedback=false,missingView=false,staleView=false,releaseLevels=false;
    const WeaponModeFamilyProfile* linkedFamily=nullptr;
    std::unique_ptr<Bc2PhysicalFixedContainer> physical;HandClaimToken gun{};
    std::int64_t now=1000000000;bool priorGrip=false,allowGate=true,allowLedger=true;unsigned calls=0,gateCalls=0,busyGateMask=0,rejectedGateMask=0,unavailableGateMask=0;
    explicit Fixture(unsigned index=0):Fixture(&generated::FixedContainerEquipment[index],2,20){}
    Fixture(const FixedContainerEquipmentProfile* profile,int loaded,int reserve,bool typedGate=true):equipment(profile){
        counts={loaded,reserve,equipment->interaction.capacity};
        Bc2PhysicalFixedContainerApi api;api.context=this;
        api.read=[](void* p,const ReloadStateOwner&,std::int64_t)noexcept{
            auto& f=*static_cast<Fixture*>(p);auto view=f.service.View();
            if(f.missingView)view.reset();else if(f.staleView&&view)view->binding.deadlineNs=f.now;
            return view;
        };
        api.withLedger=[](void* p,const AmmoResourceBinding& b,const HandInteractionOwner&,void* arg,
            bool (*fn)(void*,AmmunitionLedger&)noexcept)noexcept {auto& f=*static_cast<Fixture*>(p);
            if(!f.allowLedger)return false;
            return f.service.WithSelectedLedger(b,f.now,[&](AmmunitionLedger& ledger)noexcept{return fn(arg,ledger);});};
        api.submit=[](void* p,const AmmoResourceRequest& request)noexcept {auto& f=*static_cast<Fixture*>(p);++f.calls;
            if(!f.gate||!f.gate->blocksFire)return false;f.submitted=request;return true;};
        api.outcome=[](void* p,std::uint64_t request,const AmmoResourceContext& context)noexcept->std::optional<AmmoResourceOutcome>{
            const auto& outcome=static_cast<Fixture*>(p)->service.Outcome();
            return outcome&&outcome->request.intent.id==request&&outcome->request.intent.context==context?outcome:std::nullopt;
        };
        api.publishGate=[](void* p,const FixedContainerGate& g)noexcept {auto& f=*static_cast<Fixture*>(p);if(!f.allowGate)return false;f.gate=g;return true;};
        if(typedGate)api.publishGateObserved=[](void* p,const FixedContainerGate& g)noexcept {
            auto& f=*static_cast<Fixture*>(p);const auto mask=1u<<(f.gateCalls++%2);
            if(!f.allowGate||(f.rejectedGateMask&mask))return FixedContainerGateResult::Rejected;
            if(f.unavailableGateMask&mask)return FixedContainerGateResult::BindingUnavailable;
            if(f.busyGateMask&mask)return FixedContainerGateResult::Busy;
            f.gate=g;return FixedContainerGateResult::Published;
        };
        api.nextRequest=[](void* p)noexcept{return ++static_cast<Fixture*>(p)->intent;};
        physical=std::make_unique<Bc2PhysicalFixedContainer>(true,api);
    }
    void Prepare(const Matrix& wrist,bool grip,bool pouch=false,bool finish=false){
        now+=10000000;++sequence;binding.observedNs=now;binding.deadlineNs=now+100000000;
        AmmunitionSnapshot ammunition{binding.context,sequence,now,now+100000000,counts,true};
        service.Select(binding,native,ammunition,now);
        sample={};sample.nativeOwner=native.owner;sample.input={owner,sequence,now,now+100000000,now,true,{true,true},{releaseLevels?!grip:priorGrip&&!grip,false}};
        sample.observation=observe?&observation:nullptr;
        sample.weapon={linkedFamily?binding.persistence:binding.context.resource.weapon,owner.equipGeneration};sample.equipment=equipment;
        if(linkedFamily)sample.family=FixedContainerBodyAmmo::FamilyBinding{native.owner,
            WeaponModeCommand{25,0xf0000,0,binding.inventory,binding.persistence,linkedFamily,true,25},now,now+500000000};
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
        if(rendererFeedback){
            // Same renderer feedback boundary as BuildFixedContainerRawContact:
            // disabled/expired tracking produces invalid next-frame contact;
            // valid tracking carries its original input lease, never this Tick's.
            const auto& t=result.tracking;
            if(!FixedContainerRawCaptureFresh(t,now))sample.raw={};
            else{sample.raw.nativeOwner=t.source.binding.owner;sample.raw.equipment=t.source.equipment;sample.raw.input=t.input;
                sample.raw.grip=t.grip;sample.raw.gripPressed=t.gripPressed;sample.raw.pouchContact=t.pouchContact;sample.raw.recoverySequence=t.recoverySequence;}
        }
        if(missingRaw)sample.raw={};
        priorGrip=grip;
    }
    Bc2PhysicalFixedContainerResult Consume(const Bc2FixedContainerRawContact* raw=nullptr){
        if(raw)sample.raw=*raw;
        gateCalls=0;result=physical->Tick(sample,hands,intent);return result;
    }
    Bc2PhysicalFixedContainerResult Send(const Matrix& wrist,bool grip,bool pouch=false,bool finish=false){
        Prepare(wrist,grip,pouch,finish);return Consume();
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
int RetainBusyFeedback(Fixture& f,const Matrix& wrist){
    f.Send(wrist,true); // Finish the insertion's neutral seed before testing a held gesture.
    CHECK(f.result.tracking.hand&&f.gate&&f.gate->blocksFire);
    const auto token=f.result.tracking.hand->token;const auto calls=f.calls;
    f.rendererFeedback=true;
    for(unsigned mask:{1u,2u,3u,0u}){
        const auto previous=f.result.tracking;const auto accepted=f.gate;f.busyGateMask=mask;
        f.Send(wrist,true);
        CHECK(f.result.tracking.enabled&&FixedContainerTrackingFresh(f.result.tracking,f.now));
        CHECK(f.result.tracking.hand&&f.result.tracking.hand->token==token);
        CHECK(f.hands.Current(InteractionHand::Left)&&f.hands.Current(InteractionHand::Left)->token==token);
        CHECK(f.sample.raw.valid&&f.sample.raw.input.sequence==previous.input.sequence);
        CHECK(f.sample.raw.input.observedNs==previous.input.observedNs&&f.sample.raw.input.deadlineNs==previous.input.deadlineNs);
        CHECK(f.result.tracking.hand->deadlineNs<=f.sample.raw.input.deadlineNs);
        CHECK(f.result.tracking.input.sequence==f.sample.input.sequence&&f.result.tracking.input.deadlineNs==f.sample.input.deadlineNs);
        CHECK(f.result.blocksFire&&f.calls==calls);
        if(mask==3)CHECK(f.gate==accepted); // Busy did not publish or extend the accepted gate.
    }
    f.rendererFeedback=false;return 0;
}
int BusyMechanismFeedbackContinuesOpening(){
    Fixture f;const auto& motion=f.equipment->interaction.geometry[0].motion;
    f.Send(At(motion,0),false);f.Send(At(motion,0),true);CHECK(f.result.mechanismOwnsHand);
    CHECK(!RetainBusyFeedback(f,At(motion,0)));
    for(unsigned n=1;n<=10;++n)f.Send(At(motion,n/10.),true);
    for(unsigned n=0;n<8;++n)f.Send(At(motion,1),true);
    CHECK(f.result.tracking.step==FixedContainerStep::EjectSpent&&f.result.blocksFire&&f.calls==0);
    return 0;
}
int BusyCannotStartReviveOrChangeOwner(){
    Fixture seed(1);const auto& insertion=seed.equipment->interaction.geometry[2].motion;
    seed.Send(At(insertion,0),false);seed.Send(At(insertion,0),true,true);
    CHECK(seed.result.ammoOwnsHand&&seed.gate&&!seed.gate->blocksFire);
    seed.busyGateMask=3;seed.Send(At(insertion,0),true);
    CHECK(!seed.result.tracking.enabled&&!seed.submitted); // An accepted open gate cannot become a blocker on Busy.
    for(unsigned mask:{1u,2u}){
        Fixture f;const auto& m=f.equipment->interaction.geometry[0].motion;
        f.Send(At(m,0),false);f.Send(At(m,0),true);CHECK(f.result.mechanismOwnsHand);
        f.rejectedGateMask=mask;f.Send(At(m,0),true);CHECK(!f.result.tracking.enabled);
        f.rejectedGateMask=0;f.busyGateMask=3;f.Send(At(m,0),true);
        CHECK(!f.result.tracking.enabled&&!f.submitted); // Rejection invalidated the previously accepted proof.
    }
    for(unsigned fault=0;fault<13;++fault){
        Fixture f;const auto& m=f.equipment->interaction.geometry[0].motion;
        if(fault!=0){f.Send(At(m,0),false);f.Send(At(m,0),true);CHECK(f.result.mechanismOwnsHand);}
        f.busyGateMask=3;
        switch(fault){
        case 0:break; // No accepted initial gate or hold.
        case 1:f.physical->Cancel(f.sample.input,f.hands);break;
        case 2:f.missingRaw=true;break;
        case 3:f.now+=100000000;break; // Expired hand/input/native binding cannot be restamped.
        case 4:++f.binding.inventory;break;
        case 5:++f.binding.switching;break;
        case 6:++f.binding.data;break;
        case 7:++f.binding.persistence;break;
        case 8:++f.binding.context.resource.weaponGeneration;break;
        case 9:++f.owner.equipGeneration;f.gun={};break;
        case 10:++f.native.owner.space;f.binding.owner=f.native.owner;++f.binding.context.space;++f.owner.space;f.gun={};break;
        case 11:break; // Explicit release below.
        case 12:f.allowLedger=false;break; // Busy cannot skip the native/ledger admission callback.
        }
        f.Send(At(m,0),fault!=11);
        CHECK(!f.result.tracking.enabled&&!f.result.ammoOwnsHand&&!f.result.mechanismOwnsHand&&!f.submitted);
        if(fault==0||fault==1||fault==11)CHECK(!f.hands.Current(InteractionHand::Left));
    }
    Fixture legacy(&generated::FixedContainerEquipment[0],2,20,false);
    const auto& m=legacy.equipment->interaction.geometry[0].motion;
    legacy.Send(At(m,0),false);legacy.Send(At(m,0),true);CHECK(legacy.result.mechanismOwnsHand);
    legacy.allowGate=false;legacy.Send(At(m,0),true);CHECK(!legacy.result.tracking.enabled);
    return 0;
}
int BusyExpiresWithoutGateLeaseExtension(){
    Fixture f;const auto& m=f.equipment->interaction.geometry[0].motion;
    f.Send(At(m,0),false);f.Send(At(m,0),true);CHECK(f.result.mechanismOwnsHand);
    const auto accepted=f.gate;f.busyGateMask=3;f.rendererFeedback=true;
    for(unsigned n=1;n<10;++n){f.Send(At(m,0),true);CHECK(f.result.tracking.enabled&&f.gate==accepted);}
    const auto heldBeforeExpiry=f.hands.Current(InteractionHand::Left);CHECK(heldBeforeExpiry);
    f.Send(At(m,0),true);
    CHECK(f.now==accepted->deadlineNs&&f.sample.raw.valid&&!f.result.tracking.enabled&&f.gate==accepted&&f.calls==0);
    CHECK(f.hands.Current(InteractionHand::Left)&&f.hands.Current(InteractionHand::Left)->deadlineNs>f.now);
    const auto heldAfterExpiry=*f.hands.Current(InteractionHand::Left);
    CHECK(heldAfterExpiry.token==heldBeforeExpiry->token&&heldAfterExpiry.deadlineNs==heldBeforeExpiry->deadlineNs&&
          heldAfterExpiry.inputSequence==heldBeforeExpiry->inputSequence);
    CHECK(!f.hands.MechanismReservation(heldAfterExpiry.token)&&!f.result.tracking.rawCaptureOnly);
    CHECK(!f.result.mechanismOwnsHand&&!f.result.ammoOwnsHand&&f.result.blocksFire);
    f.busyGateMask=0;f.Send(At(m,0),true);
    CHECK(!f.sample.raw.valid&&!f.hands.Current(InteractionHand::Left)&&!f.result.mechanismOwnsHand);
    CHECK(f.calls==0&&!f.submitted&&f.service.View()->snapshot.counts==f.counts);
    return 0;
}
int FailedGateRevokesReservationAndRejectedMechanism(){
    for(bool reserved:{false,true}){
        Fixture f;const auto& motion=f.equipment->interaction.geometry[0].motion;
        f.Send(At(motion,0),false);f.Send(At(motion,0),true);CHECK(f.result.tracking.hand);
        const auto token=f.result.tracking.hand->token;
        if(reserved){
            f.unavailableGateMask=1;f.Send(At(motion,0),true);CHECK(f.hands.MechanismReservation(token));
            f.unavailableGateMask=0;f.busyGateMask=1;
        }else f.rejectedGateMask=1;
        f.Send(At(motion,0),true);
        CHECK(!f.hands.Current(InteractionHand::Left)&&!f.hands.MechanismReservation(token));
        CHECK(!f.result.tracking.enabled&&!f.result.tracking.rawCaptureOnly&&f.result.blocksFire);
        CHECK(!f.result.mechanismOwnsHand&&!f.result.ammoOwnsHand&&f.calls==0&&!f.submitted);
        CHECK(f.service.View()->snapshot.counts==f.counts);
    }return 0;
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
        CHECK(!RetainBusyFeedback(f,At(motion,0)));
        f.Send(At(motion,0),true);for(unsigned n=1;n<=30;++n)f.Send(At(motion,n/30.),true);
        for(unsigned n=0;n<8;++n)f.Send(At(motion,1),true);f.Send(At(motion,1),false);
        CHECK(f.calls==1&&f.submitted&&f.submitted->preparedCommand);CHECK(f.result.tracking.pending&&f.result.blocksFire);
        const auto& command=*f.submitted->preparedCommand;CHECK(command.before.loaded==2&&command.after.loaded==8&&command.after.reserve==14);
        CHECK(f.service.View()->snapshot.counts.loaded==2); // Queuing is no receipt.
        f.physical->Cancel(f.sample.input,f.hands);CHECK(f.calls==1);
    }return 0;
}
int BusyCannotAuthorizeDispatchOrClosure(){
    Fixture clip(1);const auto& insertion=clip.equipment->interaction.geometry[2].motion;
    clip.Send(At(insertion,0),false);clip.Send(At(insertion,0),true,true);
    clip.Send(At(insertion,0),true);for(unsigned n=1;n<=30;++n)clip.Send(At(insertion,n/30.),true);
    for(unsigned n=0;n<8;++n)clip.Send(At(insertion,1),true);
    clip.busyGateMask=2;clip.Send(At(insertion,1),false);
    CHECK(clip.result.tracking.pending&&!clip.result.tracking.enabled&&clip.result.blocksFire);
    CHECK(clip.calls==0&&!clip.submitted&&clip.service.View()->snapshot.counts==clip.counts);
    clip.busyGateMask=0;clip.Send(At(insertion,1),false);
    CHECK(!clip.result.tracking.pending&&clip.calls==0&&!clip.submitted);

    Fixture cylinder;cylinder.Hinge(0);cylinder.Hinge(1);
    cylinder.Send(At(cylinder.equipment->interaction.geometry[2].motion,0),false,false,true);
    CHECK(cylinder.result.tracking.step==FixedContainerStep::Close);
    cylinder.busyGateMask=2;const auto& closing=cylinder.equipment->interaction.geometry[3].motion;
    cylinder.Send(At(closing,0),false);cylinder.Send(At(closing,0),true);
    for(unsigned n=1;n<=10&&cylinder.result.tracking.step!=FixedContainerStep::Complete;++n)cylinder.Send(At(closing,n/10.),true);
    for(unsigned n=0;n<8&&cylinder.result.tracking.step!=FixedContainerStep::Complete;++n)cylinder.Send(At(closing,1),true);
    CHECK(cylinder.result.tracking.step==FixedContainerStep::Complete&&!cylinder.result.tracking.enabled);
    CHECK(cylinder.gate&&cylinder.gate->blocksFire&&!cylinder.gate->closedVerified&&cylinder.calls==0);
    cylinder.busyGateMask=0;const auto& opening=cylinder.equipment->interaction.geometry[0].motion;
    // A released grip while both publications are busy leaves the old native
    // gate open. Its next press must publish that close before a new lifetime.
    const auto lifetime=cylinder.gate->lifetime;cylinder.busyGateMask=3;
    cylinder.Send(At(opening,0),false);cylinder.busyGateMask=0;cylinder.Send(At(opening,0),true);
    CHECK(cylinder.gate->lifetime==lifetime&&cylinder.gate->closedVerified&&!cylinder.gate->blocksFire);
    cylinder.Send(At(opening,0),false);
    const auto closed=cylinder.gate;cylinder.busyGateMask=3;cylinder.Send(At(opening,0),true);
    CHECK(!cylinder.result.tracking.enabled&&!cylinder.result.mechanismOwnsHand&&cylinder.gate==closed);
    CHECK(!cylinder.hands.Current(InteractionHand::Left)); // New lifetime/opening needs a published blocker.
    return 0;
}
int PriorRendererFrameQueuesAndCompletesOnceAfterReequip(){
    Fixture f(1);f.delayedRaw=true;f.observe=true;const auto& motion=f.equipment->interaction.geometry[2].motion;
    f.Send(At(motion,0),false);f.Send(At(motion,0),true,true);f.Send(At(motion,0),true,true);
    CHECK(f.result.ammoOwnsHand);
    f.Send(At(motion,0),true);for(unsigned n=1;n<=30;++n)f.Send(At(motion,n/30.),true);
    for(unsigned n=0;n<8;++n)f.Send(At(motion,1),true);f.Send(At(motion,1),false);
    CHECK(f.calls==1&&f.submitted&&f.submitted->preparedCommand&&f.result.tracking.pending);
    CHECK(f.observation.pendingRequest==f.submitted&&!f.observation.lastCompletion);
    const auto pendingSupply=f.observation.supply;
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
    CHECK(f.observation.lastCompletion&&!f.observation.pendingRequest);
    const auto completion=*f.observation.lastCompletion;
    CHECK(completion.supply==pendingSupply&&completion.lifetime==f.result.tracking.lifetime);
    CHECK(completion.outcome.request==request&&completion.outcome.receipt&&completion.outcome.nativeDispatched);
    CHECK(completion.outcome.state==AmmoResourceRequestState::Completed&&completion.outcome.receipt->command==command);
    CHECK(completion.postSnapshot.context==f.binding.context&&completion.postSnapshot.context!=command.context);
    CHECK(completion.postSnapshot.sequence==f.sequence&&completion.postSnapshot.counts==command.after&&completion.acceptedNs==f.now);
    f.Send(At(motion,1),false);CHECK(f.calls==1&&nativeCalls==1);
    CHECK(f.observation.lastCompletion&&f.observation.lastCompletion->outcome.request==request);
    CHECK(f.observation.lastCompletion->outcome.receipt==completion.outcome.receipt&&
        f.observation.lastCompletion->postSnapshot.sequence==completion.postSnapshot.sequence&&
        f.observation.lastCompletion->postSnapshot.deadlineNs==completion.postSnapshot.deadlineNs&&
        f.observation.lastCompletion->acceptedNs==completion.acceptedNs);
    CHECK(f.service.View()->snapshot.counts==command.after);return 0;
}
int ObservationPreservesSourceLeasesAndRejectsWrongIdentity(){
    Fixture f(1);const auto& motion=f.equipment->interaction.geometry[2].motion;
    f.observation.owner.weapon=123;f.Send(At(motion,0),false);
    CHECK(f.observation.owner.weapon==123); // The normal path never fills diagnostics.
    f.observe=true;f.delayedRaw=true;f.Send(At(motion,0),false);
    const auto captured=f.observation;
    CHECK(captured.raw.input.sequence<captured.input.sequence&&captured.raw.input.deadlineNs<captured.input.deadlineNs);
    CHECK(FixedContainerObservationJoined(captured,f.native.owner,f.sample.input,f.equipment,f.now));
    auto processed=f.sample.input;processed.nowNs+=2000000;
    CHECK(FixedContainerObservationJoined(captured,f.native.owner,processed,f.equipment,processed.nowNs));
    CHECK(captured.input.observedNs==processed.observedNs&&captured.input.deadlineNs==processed.deadlineNs);
    auto restamped=processed;++restamped.deadlineNs;
    CHECK(!FixedContainerObservationJoined(captured,f.native.owner,restamped,f.equipment,processed.nowNs));
    processed.nowNs=captured.input.nowNs-1;
    CHECK(!FixedContainerObservationJoined(captured,f.native.owner,processed,f.equipment,f.now));
    auto wrongOwner=f.native.owner;++wrongOwner.weapon;
    CHECK(!FixedContainerObservationJoined(captured,wrongOwner,f.sample.input,f.equipment,f.now));
    auto wrongInput=f.sample.input;++wrongInput.sequence;
    CHECK(!FixedContainerObservationJoined(captured,f.native.owner,wrongInput,f.equipment,f.now));
    CHECK(!FixedContainerObservationJoined(captured,f.native.owner,f.sample.input,&generated::FixedContainerEquipment[2],f.now));
    CHECK(!FixedContainerObservationJoined(captured,f.native.owner,f.sample.input,f.equipment,captured.input.deadlineNs));
    std::ostringstream staleRaw;
    WriteFixedContainerObservation(staleRaw,&captured,f.native.owner,f.sample.input,captured.raw.rig,f.equipment,captured.raw.input.deadlineNs);
    CHECK(staleRaw.str().find("\"raw\":{\"valid\":true,\"current\":false")!=std::string::npos);
    CHECK(captured.raw.input.deadlineNs==f.observation.raw.input.deadlineNs);
    std::ostringstream wrong;
    WriteFixedContainerObservation(wrong,&captured,wrongOwner,f.sample.input,captured.raw.rig,f.equipment,f.now);
    CHECK(wrong.str().find("\"available\":false")!=std::string::npos&&wrong.str().find("\"profile\":")==std::string::npos);
    ++f.counts.loaded;--f.counts.reserve;f.Send(At(motion,0),false);
    CHECK(f.observation.view&&f.observation.view->snapshot.counts==f.counts);
    CHECK(!f.observation.lastCompletion&&!f.observation.pendingRequest&&f.calls==0); // Counts cannot mint a receipt.
    CHECK(!FixedContainerObservationJoined(captured,f.native.owner,f.sample.input,f.equipment,f.now));
    return 0;
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
int LinearPartsFollowTheirOwnMeasuredTravel(){
    unsigned independent=0;
    for(const auto& e:generated::UnderbarrelFixedContainerEquipment){
        const auto& motion=e.interaction.geometry[0].motion;
        if(motion.kind!=FeedMotionKind::Linear)continue;
        Fixture f(&e,0,1);f.Send(At(motion,0),false);f.Send(At(motion,0),true);
        for(unsigned n=1;n<=10;++n)f.Send(At(motion,n/20.),true);
        CHECK(f.result.mechanismOwnsHand&&f.result.tracking.step==FixedContainerStep::Open);
        const auto barrelDelta=Multiply(feed_interaction_detail::Inverse(At(motion,0)),At(motion,.5));
        for(unsigned n=0;n<e.parts.size();++n)if(e.parts[n].coupledOpening){
            const auto& part=e.parts[n];
            const auto expected=reload_insertion_detail::BlendRigid(part.weaponFromStepStart[0],part.weaponFromStepEnd[0],.5);
            const auto commonDelta=Multiply(part.weaponFromStepStart[0],barrelDelta);
            if(feed_interaction_detail::Distance(expected,commonDelta)>.0001)++independent;
            CHECK(feed_interaction_detail::Distance(f.result.tracking.parts[n],expected)<.00001);
            CHECK(feed_interaction_detail::Angle(f.result.tracking.parts[n],expected)<.001);
        }
    }
    CHECK(independent>0);return 0;
}
int UnderbarrelContactGuidanceAcrossBothFamilies(){
    unsigned breeches=0,muzzles=0;
    for(const auto& e:generated::UnderbarrelFixedContainerEquipment){
        Fixture f(&e,0,2);const auto& insertion=e.interaction.geometry[2].motion;
        const bool breech=e.interaction.kind==FixedContainerKind::SingleRoundBreech;
        if(breech){++breeches;
            // A round at the closed breech cannot acquire insertion custody.
            f.Send(At(insertion,1),false);f.Send(At(insertion,1),true,true);
            CHECK(!f.result.ammoOwnsHand&&f.calls==0&&f.result.tracking.step==FixedContainerStep::Open);
            f.Send(At(insertion,1),false);f.Hinge(0);
        }else{++muzzles;CHECK(e.interaction.kind==FixedContainerKind::SingleRoundMuzzle);}
        f.Send(At(insertion,0),false);f.Send(At(insertion,0),true,true);f.Send(At(insertion,0),true);
        CHECK(f.result.ammoOwnsHand&&f.result.tracking.hand);const auto token=f.result.tracking.hand->token;
        auto turn=Identity();const float angle=breech?1.1f:-1.1f;
        turn.values[0][0]=turn.values[1][1]=std::cos(angle);turn.values[0][1]=std::sin(angle);turn.values[1][0]=-std::sin(angle);
        const auto distance=feed_interaction_detail::Distance(insertion.placement.weaponFromSource,insertion.placement.weaponFromDestination);
        std::array<double,3> outward{};
        for(unsigned n=0;n<3;++n)outward[n]=(insertion.placement.weaponFromSource.values[3][n]-insertion.placement.weaponFromDestination.values[3][n])/distance;
        auto approach=Multiply(turn,insertion.placement.weaponFromDestination);
        for(unsigned n=0;n<3;++n)approach.values[3][n]+=float(outward[n]*.09);
        // This deliberately rotated object follows the player's hand into the
        // measured target's contact volume; no authored insertion axis is used.
        for(unsigned n=1;n<=50;++n){const auto object=reload_insertion_detail::BlendRigid(insertion.placement.weaponFromSource,approach,n/50.);
            f.Send(Multiply(insertion.placement.sourceFromGrip,object),true);}
        CHECK(f.result.ammoOwnsHand&&f.result.tracking.hand->token==token&&f.calls==0);
        auto captured=approach;for(unsigned n=0;n<3;++n)captured.values[3][n]-=float(outward[n]*.035);
        const auto captureWrist=Multiply(insertion.placement.sourceFromGrip,captured);
        f.Send(captureWrist,true);
        for(unsigned n=0;n<9;++n)f.Send(captureWrist,true);
        CHECK(f.calls==0&&f.result.ammoOwnsHand&&f.result.tracking.hand->token==token);
        CHECK(f.result.tracking.target&&feed_interaction_detail::Distance(*f.result.tracking.target,insertion.placement.weaponFromDestination)>.04);
        f.missingRaw=true;f.Send(captureWrist,true);
        CHECK(f.result.ammoOwnsHand&&f.hands.Current(InteractionHand::Left)->token==token&&f.calls==0);
        CHECK(!f.result.tracking.target&&f.result.tracking.carried);
        f.missingRaw=false;f.Send(captureWrist,true);
        auto withdrawn=captured;for(unsigned n=0;n<3;++n)withdrawn.values[3][n]+=float(outward[n]*.04);
        f.Send(Multiply(insertion.placement.sourceFromGrip,withdrawn),true);
        CHECK(f.result.ammoOwnsHand&&f.result.tracking.hand->token==token&&f.calls==0);
        CHECK(f.result.tracking.target&&feed_interaction_detail::Distance(*f.result.tracking.target,insertion.placement.weaponFromDestination)>.08);
        f.Send(captureWrist,true); // Pull-away requires a new contact and inward movement.
        auto seated=captured;for(unsigned n=0;n<3;++n)seated.values[3][n]-=float(outward[n]*.025);
        const auto seatWrist=Multiply(insertion.placement.sourceFromGrip,seated);
        for(unsigned n=0;n<9;++n)f.Send(seatWrist,true);
        CHECK(f.calls==0&&f.result.tracking.target);
        CHECK(feed_interaction_detail::Distance(*f.result.tracking.target,insertion.placement.weaponFromDestination)<.00001);
        CHECK(feed_interaction_detail::Angle(*f.result.tracking.target,insertion.placement.weaponFromDestination)<.001);
        CHECK(f.result.tracking.hand->token==token);
        f.Send(seatWrist,false);CHECK(f.calls==1&&f.submitted&&f.submitted->preparedCommand);
        CHECK((f.submitted->preparedCommand->before==AmmunitionCounts{0,2,1}));
        CHECK((f.submitted->preparedCommand->after==AmmunitionCounts{1,1,1}));
        CHECK((f.service.View()->snapshot.counts==AmmunitionCounts{0,2,1}));
        f.Send(seatWrist,false);CHECK(f.calls==1); // A queued request cannot duplicate the transfer.
    }
    CHECK(breeches&&muzzles&&breeches+muzzles==generated::UnderbarrelFixedContainerEquipment.size());return 0;
}
int MeasuredUnderbarrelLoadsOneRoundAndCloses(){
    unsigned linkedCases=0,nativeItemCases=0;
    for(const auto& e:generated::UnderbarrelFixedContainerEquipment){
        const bool breech=e.interaction.kind==FixedContainerKind::SingleRoundBreech;
        CHECK(breech||e.interaction.kind==FixedContainerKind::SingleRoundMuzzle);
        Fixture f(&e,0,1);f.observe=true;const auto& insertion=e.interaction.geometry[2].motion;
        for(const auto& family:WeaponModeFamilies)if(WeaponModeFamilyEligible(family)&&
            family.secondaryConfiguration==e.configurationPath&&family.secondaryAsset==e.asset){f.linkedFamily=&family;break;}
        // These exact unscoped AN94 variants and their primary have authored
        // AltDeployId 0 (weapons54 enrollment and weapons71 AN94 source).
        // Their measured reload uses native-item custody; it grants no linked alias.
        const bool nativeItem=
            (e.asset=="40mmgl"&&e.configurationPath=="Objects/Weapons/Handheld/BU_rgl_AN94Abakan/BU_rgl_AN94Abakan")||
            (e.asset=="40mmsg"&&e.configurationPath=="Objects/Weapons/Handheld/BU_rgl_AN94Abakan/BU_shg_AN94Abakan")||
            (e.asset=="40mmsmk"&&e.configurationPath=="Objects/Weapons/Handheld/BU_rgl_AN94Abakan/BU_smk_AN94Abakan");
        if(nativeItem){CHECK(!f.linkedFamily);++nativeItemCases;}
        else{CHECK(f.linkedFamily);++linkedCases;}
        const auto physicalItem=nativeItem?f.binding.context.resource.weapon:f.binding.persistence;
        f.Send(At(e.interaction.geometry[breech?0:2].motion,0),false);
        CHECK(f.result.bodyAmmo&&f.result.bodyVisual&&f.result.bodyAmmo->equipment==&e);
        CHECK(f.result.tracking.gun.token.item.id==physicalItem);
        CHECK(f.result.bodyVisual->source.identity.weapon.id==physicalItem);
        CHECK(f.result.tracking.source.family.has_value()==!nativeItem);
        CHECK(f.result.bodyAmmo->family.has_value()==!nativeItem);
        CHECK(f.result.tracking.source.ammunition.context.resource.weapon==f.native.owner.weapon);
        BodyAmmoTracking body;body.fixed=f.result.bodyAmmo;body.visual=*f.result.bodyVisual;
        CHECK(BodyAmmoFresh(body,f.now)&&body.visual.source.objectUnits==1&&body.visual.source.reserveUnits==1);
        if(breech){f.Hinge(0);CHECK(f.result.blocksFire);}
        CHECK(f.result.tracking.step==FixedContainerStep::Insert&&f.calls==0);
        f.Send(At(insertion,0),false);f.Send(At(insertion,0),true,true);CHECK(f.result.ammoOwnsHand);
        CHECK(!RetainBusyFeedback(f,At(insertion,0)));
        f.Send(At(insertion,0),true);for(unsigned n=1;n<=30;++n)f.Send(At(insertion,n/30.),true);
        for(unsigned n=0;n<8;++n)f.Send(At(insertion,1),true);f.Send(At(insertion,1),false);
        CHECK(f.calls==1&&f.submitted&&f.submitted->preparedCommand&&f.result.tracking.pending&&f.result.blocksFire);
        const auto request=*f.submitted;const auto command=*request.preparedCommand;
        CHECK((command.before==AmmunitionCounts{0,1,1})&&(command.after==AmmunitionCounts{1,0,1}));
        CHECK(f.service.View()->snapshot.counts==command.before);
        CHECK(f.service.Accept(request,f.now)==command);AmmoResourceNativeCall actual;unsigned nativeCalls=0;
        CHECK(f.service.InvokeNative(f.service.View()->snapshot,[&]()noexcept{return f.now;},[&](std::int64_t began)noexcept{
            ++nativeCalls;actual={f.native,command.id,900,began,began+1,command.before,command.after,true,true,true};return actual;
        }));
        for(unsigned branch=0;branch<3;++branch){
            AmmoResourceOwnUpdate row;row.beforeOwner=row.afterOwner=f.native.owner;row.firing=f.native.firing[branch];
            row.serverPlayer=f.native.serverPlayer;row.serverSoldier=f.native.serverSoldier;row.serverItem=f.native.serverItem;
            row.invocation=actual.invocation+(branch==2?0:branch+1);row.beginNs=actual.beginNs-1;row.endNs=actual.endNs+10+branch;
            row.branch=branch;row.depth=1;row.loadedBefore=0;row.reserveBefore=1;row.loadedAfter=1;row.reserveAfter=0;
            row.current=row.next=2;row.finished=row.identityRetained=row.neutralContext=true;
            CHECK(f.service.Observe(row,actual.endNs+100)==(branch==2));
        }
        f.counts=command.after;f.Send(At(insertion,1),false);
        CHECK(!f.result.tracking.pending&&!f.result.bodyAmmo);
        if(breech){CHECK(f.result.tracking.step==FixedContainerStep::Close&&f.result.blocksFire);f.Hinge(3);}
        if(f.result.tracking.step!=FixedContainerStep::Complete||f.result.blocksFire)
            std::cerr<<"Measured launcher close: "<<e.configurationPath<<" step="<<unsigned(f.result.tracking.step)
                <<" status="<<unsigned(f.result.status)<<" blocked="<<f.result.blocksFire<<'\n';
        CHECK(f.result.tracking.step==FixedContainerStep::Complete&&!f.result.blocksFire);
        CHECK(f.calls==1&&nativeCalls==1&&f.service.View()->snapshot.counts==command.after);
        CHECK(command.before.loaded+command.before.reserve==command.after.loaded+command.after.reserve);
        CHECK(f.observation.lastCompletion&&f.observation.lastCompletion->outcome.request==request);
        CHECK(f.observation.lastCompletion->postSnapshot.counts==command.after);
        std::ostringstream json;WriteFixedContainerObservation(json,&f.observation,f.native.owner,f.sample.input,f.sample.raw.rig,&e,f.now);
        CHECK(json.str().find("bc2vr.fixed-container-observation.v1")!=std::string::npos);
        CHECK(json.str().find("\"authorityVerified\":true,\"copiesVerified\":true")!=std::string::npos);
        CHECK(json.str().size()<20000); // Leave room for the shared 32 KiB snapshot envelope.

        // Opening an unfired breech preserves its live round and cannot draw
        // another round. The muzzle family likewise cannot top up a full tube.
        Fixture loaded(&e,1,1);loaded.linkedFamily=f.linkedFamily;
        if(breech){loaded.Hinge(0);CHECK(loaded.result.tracking.step==FixedContainerStep::Insert&&loaded.result.mechanismOwnsHand);
            loaded.Send(At(e.interaction.geometry[0].motion,1),false);
            CHECK(loaded.result.tracking.step==FixedContainerStep::Close);loaded.Hinge(3);}
        else{loaded.Send(At(insertion,0),false);loaded.Send(At(insertion,0),true,true);}
        CHECK(loaded.calls==0&&!loaded.submitted&&!loaded.result.ammoOwnsHand);
        CHECK((loaded.service.View()->snapshot.counts==AmmunitionCounts{1,1,1}));
    }
    CHECK(linkedCases==77&&nativeItemCases==3&&linkedCases+nativeItemCases==generated::UnderbarrelFixedContainerEquipment.size());
    return 0;
}
int LinkedLoaderRejectsUnverifiedPhysicalAliases(){
    const auto& e=generated::UnderbarrelFixedContainerEquipment.front();Fixture f(&e,1,7);
    const auto& initial=e.interaction.geometry[e.interaction.kind==FixedContainerKind::SingleRoundMuzzle?2:0].motion;
    for(const auto& family:WeaponModeFamilies)if(WeaponModeFamilyEligible(family)&&
        family.secondaryConfiguration==e.configurationPath&&family.secondaryAsset==e.asset){f.linkedFamily=&family;break;}
    CHECK(f.linkedFamily);f.missingRaw=true;f.Send(At(initial,0),false);
    CHECK(f.result.tracking.enabled&&f.result.tracking.lifetime&&f.result.blocksFire);
    CHECK(FixedContainerTrackingFresh(f.result.tracking,f.now)&&f.gate&&f.gate->blocksFire);
    f.missingRaw=false;f.Send(At(initial,0),false);
    CHECK(f.result.tracking.enabled&&!f.result.blocksFire&&f.gate&&f.gate->closedVerified&&!f.gate->blocksFire);
    CHECK((f.service.View()->snapshot.counts==AmmunitionCounts{1,7,1})&&f.calls==0);
    const auto original=f.sample;CHECK(f.result.tracking.enabled);
    const auto source=f.result.tracking.source;
    CHECK(FixedContainerHandItem(source,f.owner,f.now));
    CHECK(!FixedContainerHandItem(source,f.owner,source.family->deadlineNs));
    for(unsigned fault=0;fault<9;++fault){
        auto bad=original;
        switch(fault){
        case 0:bad.family.reset();break;
        case 1:++bad.family->mode.inventory;break;
        case 2:++bad.family->mode.persistent;break;
        case 3:bad.family->mode.selectedSecondary=false;break;
        case 4:++bad.family->owner.weapon;break;
        case 5:bad.family->deadlineNs=f.now;break;
        case 6:bad.family->mode.family=nullptr;break;
        case 7:++bad.nativeOwner.weapon;break;
        case 8:bad.family->mode.targetWeapon=bad.nativeOwner.weapon;break;
        }
        f.gate.reset();const auto rejected=f.physical->Tick(bad,f.hands,f.intent);
        CHECK(!rejected.tracking.enabled&&!rejected.mechanismOwnsHand&&!rejected.ammoOwnsHand&&!f.submitted&&!f.gate);
    }
    auto wrong=source;
    for(const auto& family:WeaponModeFamilies)if(family.secondaryConfiguration!=e.configurationPath){wrong.family->mode.family=&family;break;}
    CHECK(!FixedContainerHandItem(wrong,f.owner,f.now));
    return 0;
}
void SelectFixedFamily(Fixture& f){
    for(const auto& family:WeaponModeFamilies)if(WeaponModeFamilyEligible(family)&&
        family.secondaryConfiguration==f.equipment->configurationPath&&family.secondaryAsset==f.equipment->asset){
        f.linkedFamily=&family;break;
    }
}
int ArmFixedSupport(Fixture& f,unsigned step,Bc2FixedContainerRawContact& pressed,HandClaim& support,
    HandClaimKind kind=HandClaimKind::WeaponSupport){
    const auto start=At(f.equipment->interaction.geometry[step].motion,0);
    // A real controller release is recorded and consumed by policy. The held
    // packet preceding it need not have reached the renderer/recognizer.
    if(!f.priorGrip)f.Prepare(start,true);
    f.Send(start,false);const auto neutral=f.sample.raw;
    CHECK(neutral.input.released[0]&&!neutral.grip&&!neutral.gripPressed);
    CHECK(f.result.tracking.step==FixedContainerStep(step)&&!f.hands.Current(InteractionHand::Left));
    f.Prepare(start,true,step==2);pressed=f.sample.raw;
    f.Consume(&neutral); // Current press N+1 still sees released raw N.
    const auto gun=f.hands.Current(InteractionHand::Right);CHECK(gun&&!f.result.mechanismOwnsHand&&!f.result.ammoOwnsHand);
    const bool body=kind==HandClaimKind::BodyInventory;
    const auto acquired=f.hands.AcquireFrom(f.sample.input,neutral.input,{f.owner,InteractionHand::Left,kind,
        body?HandInteractionKey{999,1}:f.sample.weapon,{{301,1},neutral.input.sequence,neutral.input.deadlineNs,true},
        ++f.intent,body?0:gun->token.id});
    CHECK(acquired.accepted&&acquired.claim);support=*acquired.claim;
    const auto counts=f.counts;const auto calls=f.calls;
    for(unsigned repeat=0;repeat<2;++repeat){
        f.Consume(&neutral);const auto waiting=f.hands.Current(InteractionHand::Left);
        CHECK(waiting&&waiting->token==support.token&&waiting->inputSequence==support.inputSequence&&waiting->deadlineNs==support.deadlineNs);
        CHECK(!f.result.mechanismOwnsHand&&!f.result.ammoOwnsHand&&!f.result.tracking.target&&!f.result.tracking.hand);
        CHECK(f.service.View()->snapshot.counts==counts&&f.calls==calls&&!f.result.tracking.pending);
    }
    return 0;
}
int FinishFixedStroke(Fixture& f,unsigned step){
    const auto& motion=f.equipment->interaction.geometry[step].motion;
    for(unsigned n=1;n<=30;++n)f.Send(At(motion,n/30.),true);
    for(unsigned n=0;n<8;++n)f.Send(At(motion,1),true);
    if(motion.returnToStart){
        for(unsigned n=1;n<=30;++n)f.Send(At(motion,1-n/30.),true);
        for(unsigned n=0;n<8;++n)f.Send(At(motion,0),true);
    }
    CHECK(f.result.tracking.step!=FixedContainerStep(step)&&f.result.mechanismOwnsHand);
    f.Send(At(motion,motion.returnToStart?0:1),false);
    CHECK(!f.result.mechanismOwnsHand&&!f.hands.Current(InteractionHand::Left));
    return 0;
}
int TransferFixedSupportStroke(Fixture& f,unsigned step,bool sameCurrent){
    Bc2FixedContainerRawContact pressed;HandClaim support;
    CHECK(!ArmFixedSupport(f,step,pressed,support));
    if(!sameCurrent)f.Prepare(pressed.weaponFromWrist,true);
    f.Consume(&pressed);
    const auto hand=f.hands.Current(InteractionHand::Left),gun=f.hands.Current(InteractionHand::Right);
    CHECK(hand&&gun&&hand->token.kind==HandClaimKind::Mechanism&&hand->token.id!=support.token.id);
    CHECK(hand->token.owner==support.token.owner&&hand->token.item==support.token.item&&
        hand->token.prerequisiteClaim==support.token.prerequisiteClaim&&hand->token.prerequisiteClaim==gun->token.id);
    CHECK(hand->inputSequence==pressed.input.sequence&&hand->deadlineNs==pressed.input.deadlineNs);
    CHECK(f.result.mechanismOwnsHand&&f.result.tracking.hand&&f.result.tracking.hand->token==hand->token&&f.result.tracking.target);
    CHECK(f.result.blocksFire&&f.gate&&f.gate->blocksFire);
    f.Consume(&pressed);const auto duplicate=f.hands.Current(InteractionHand::Left);
    CHECK(duplicate&&duplicate->token==hand->token&&duplicate->inputSequence==hand->inputSequence&&duplicate->deadlineNs==hand->deadlineNs);
    CHECK(!FinishFixedStroke(f,step));return 0;
}
int InsertFixedWithOfflineReceipt(Fixture& f,bool freshPickupGap=false){
    const auto& motion=f.equipment->interaction.geometry[2].motion;
    const auto before=f.counts;const auto calls=f.calls;
    f.Send(At(motion,0),false);f.Send(At(motion,0),true,true);CHECK(f.result.ammoOwnsHand);
    const auto held=f.result.tracking.hand;CHECK(held&&held->token.kind==HandClaimKind::AmmoObject);
    CHECK((held->token.item==HandInteractionKey{f.observation.supply.resource.id,f.observation.supply.resource.generation}));
    if(freshPickupGap){
        // No held policy pose has been sent after pouch pickup: the recognizer
        // is armed, with no acquired gesture or insertion target yet.
        CHECK(!f.result.tracking.target);
        const auto supply=f.observation.supply;const auto lifetime=f.result.tracking.lifetime;
        HandClaimDropJournal<4> journal;f.hands.ObserveDrops(journal.Observer());
        f.Prepare(At(motion,0),true);auto delayed=f.sample.raw;f.sample.raw.valid=false;f.Consume();
        for(unsigned n=0;n<40;++n){
            const auto progress=std::min(1.,n/30.);
            f.Prepare(At(motion,progress),true);f.sample.raw.valid=false;f.Consume();
            CHECK(f.result.ammoOwnsHand&&f.result.tracking.hand&&f.result.tracking.hand->token==held->token);
            CHECK(!f.result.tracking.target&&!f.observation.rawAdmitted&&f.calls==calls);
            const auto retained=*f.result.tracking.hand;const bool release=n==39;
            f.Prepare(At(motion,progress),!release);const auto next=f.sample.raw;f.Consume(&delayed);
            CHECK(f.observation.rawAdmitted&&f.sample.input.sequence==delayed.input.sequence+2);
            CHECK(f.observation.raw.input.observedNs==delayed.input.observedNs&&
                  f.observation.raw.input.deadlineNs==delayed.input.deadlineNs&&
                  f.observation.raw.weaponFromWrist.values==delayed.weaponFromWrist.values);
            CHECK(f.observation.supply==supply&&f.result.tracking.lifetime==lifetime&&f.service.View()->snapshot.counts==before);
            if(!release){
                CHECK(f.result.status==FixedContainerStatus::Gesture&&f.result.tracking.target&&journal.count==0);
                CHECK(f.result.ammoOwnsHand&&f.result.tracking.hand->token==held->token&&f.calls==calls);
                CHECK(f.result.tracking.hand->inputSequence==retained.inputSequence&&f.result.tracking.hand->deadlineNs==retained.deadlineNs);
                const auto rawItem=Multiply(feed_interaction_detail::Inverse(motion.placement.sourceFromGrip),delayed.weaponFromWrist);
                // BC2 applies single-round guidance after reading the registered profile.
                const auto kind=f.equipment->interaction.kind;
                const auto guidance=kind==FixedContainerKind::SingleRoundBreech||kind==FixedContainerKind::SingleRoundMuzzle?
                    FeedPlacementGuidance{.06f,.14f,.02f}:f.equipment->interaction.insertionGuidance;
                const auto& destination=motion.placement.weaponFromDestination;
                if(guidance==FeedPlacementGuidance{}||
                   feed_interaction_detail::Distance(rawItem,destination)>guidance.captureRadiusMeters)
                    CHECK(feed_interaction_detail::Distance(*f.result.tracking.target,rawItem)<.00001);
                else {
                    // Contact assistance may move the displayed round away
                    // from rawItem. The original delayed wrist above remains
                    // the geometry authority through every missing-frame gap.
                    CHECK(feed_interaction_detail::Distance(*f.result.tracking.target,destination)<=guidance.captureRadiusMeters+.00001);
                    if(n>=32){
                        CHECK(feed_interaction_detail::Distance(*f.result.tracking.target,destination)<.00001);
                        CHECK(feed_interaction_detail::Angle(*f.result.tracking.target,destination)<.001);
                    }
                }
            }else{
                CHECK(f.submitted&&f.submitted->sourceProof&&f.submitted->sourceProof->inputSequence==delayed.input.sequence);
                CHECK(f.submitted->intent.observedNs==delayed.input.observedNs&&f.submitted->intent.deadlineNs==delayed.input.deadlineNs);
                CHECK(!f.result.ammoOwnsHand);
            }
            delayed=next;
        }
        f.hands.ObserveDrops({});
    }else{
        f.Send(At(motion,0),true);
        for(unsigned n=1;n<=30;++n)f.Send(At(motion,n/30.),true);
        for(unsigned n=0;n<8;++n)f.Send(At(motion,1),true);
        f.Send(At(motion,1),false);
    }
    CHECK(f.calls==calls+1&&f.submitted&&f.submitted->preparedCommand&&f.result.tracking.pending);
    const auto request=*f.submitted;const auto command=*request.preparedCommand;
    CHECK(command.before==before&&command.before.loaded+command.before.reserve==command.after.loaded+command.after.reserve);
    CHECK(f.service.View()->snapshot.counts==before&&f.service.Accept(request,f.now)==command);
    AmmoResourceNativeCall actual;unsigned mockCalls=0;
    CHECK(f.service.InvokeNative(f.service.View()->snapshot,[&]()noexcept{return f.now;},[&](std::int64_t began)noexcept{
        ++mockCalls;actual={f.native,command.id,900,began,began+1,command.before,command.after,true,true,true};return actual;
    }));
    for(unsigned branch=0;branch<3;++branch){
        AmmoResourceOwnUpdate row;row.beforeOwner=row.afterOwner=f.native.owner;row.firing=f.native.firing[branch];
        row.serverPlayer=f.native.serverPlayer;row.serverSoldier=f.native.serverSoldier;row.serverItem=f.native.serverItem;
        row.invocation=actual.invocation+(branch==2?0:branch+1);row.beginNs=actual.beginNs-1;row.endNs=actual.endNs+10+branch;
        row.branch=branch;row.depth=1;row.loadedBefore=command.before.loaded;row.reserveBefore=command.before.reserve;
        row.loadedAfter=command.after.loaded;row.reserveAfter=command.after.reserve;row.current=row.next=2;
        row.finished=row.identityRetained=row.neutralContext=true;
        CHECK(f.service.Observe(row,actual.endNs+100)==(branch==2));
    }
    f.counts=command.after;f.Send(At(motion,1),false);f.Send(At(motion,1),false);
    CHECK(!f.result.tracking.pending&&!f.result.ammoOwnsHand&&f.calls==calls+1&&mockCalls==1);
    CHECK(f.observation.lastCompletion&&f.observation.lastCompletion->outcome.request==request&&
        f.observation.lastCompletion->outcome.receipt&&f.observation.lastCompletion->outcome.receipt->command==command);
    CHECK(f.service.View()->snapshot.counts==command.after);return 0;
}
int InterruptedGateEnrollmentKeepsBothWeaponsUsable(){
    const auto& profiles=generated::UnderbarrelFixedContainerEquipment;
    const auto breech=std::find_if(profiles.begin(),profiles.end(),[](const auto& e){
        return e.interaction.kind==FixedContainerKind::SingleRoundBreech;
    });
    CHECK(breech!=profiles.end());
    for(bool restarting:{false,true}){
        Fixture f;f.observe=true;
        const auto* cylinder=f.equipment;const auto cylinderAmmo=f.counts;
        const auto cylinderResource=f.binding.context.resource;
        auto launcherResource=cylinderResource;launcherResource.weapon+=0x1000;++launcherResource.weaponGeneration;
        const auto select=[&](const FixedContainerEquipmentProfile& equipment,AmmoResourceIdentity resource,AmmunitionCounts counts){
            const auto offset=std::uint32_t(resource.weapon-cylinderResource.weapon);
            f.equipment=&equipment;f.linkedFamily=nullptr;SelectFixedFamily(f);f.counts=counts;
            f.native.owner.weapon=std::uint32_t(resource.weapon);++f.native.owner.equipGeneration;
            f.native.serverItem=0xa0000+offset;f.native.firing={0x50000+offset,0x60000+offset,0x70000+offset};
            f.binding.owner=f.native.owner;f.binding.context.resource=resource;
            f.binding.context.equipGeneration=f.native.owner.equipGeneration;
            f.binding.data=0xd0000+offset;f.binding.persistence=0xe0000+offset;
            ++f.owner.equipGeneration;f.gun={};
        };
        const auto cylinderStart=At(cylinder->interaction.geometry[0].motion,0);
        if(restarting){
            f.Hinge(0);f.Hinge(1);f.Send(At(cylinder->interaction.geometry[2].motion,0),false,false,true);
            f.Hinge(3);f.Send(cylinderStart,false);
            CHECK(f.gate&&f.gate->closedVerified&&f.result.tracking.step==FixedContainerStep::Complete);
        }
        f.busyGateMask=1;f.Send(cylinderStart,true);
        CHECK(!f.result.tracking.enabled&&!f.result.mechanismOwnsHand);
        f.busyGateMask=0;select(*breech,launcherResource,{0,1,1});
        f.Hinge(0);CHECK(f.result.tracking.step==FixedContainerStep::Insert);
        CHECK(!InsertFixedWithOfflineReceipt(f));CHECK(f.result.tracking.step==FixedContainerStep::Close&&f.result.blocksFire);
        const auto launcherGate=*f.gate;const auto launcherAmmo=f.counts;const auto launcherSupply=f.observation.supply;

        select(*cylinder,cylinderResource,cylinderAmmo);f.missingView=true;f.Send(cylinderStart,false);
        CHECK(!f.result.tracking.enabled&&f.gate==launcherGate);
        f.missingView=false;f.Send(cylinderStart,false);
        // The native gate rejects every new enrollment or cycle at/below its
        // latest accepted lifetime, including this launcher's enrollment.
        CHECK(f.result.tracking.enabled&&f.gate&&f.gate->binding.context.resource==cylinderResource);
        CHECK(f.gate->lifetime>launcherGate.lifetime&&f.observation.supply!=launcherSupply);
        f.Hinge(0);CHECK(f.result.tracking.step==FixedContainerStep::EjectSpent&&f.result.blocksFire);
        const auto openCylinderLifetime=f.gate->lifetime;

        select(*breech,launcherResource,launcherAmmo);const auto closing=At(breech->interaction.geometry[3].motion,0);
        f.missingView=true;f.Send(closing,false);CHECK(!f.result.tracking.enabled);
        f.missingView=false;f.Send(closing,false);
        CHECK(f.result.tracking.enabled&&f.result.blocksFire&&f.result.tracking.step==FixedContainerStep::Close);
        CHECK(f.gate->lifetime==launcherGate.lifetime&&f.observation.supply==launcherSupply);
        f.Hinge(3);CHECK(f.result.tracking.step==FixedContainerStep::Complete&&f.gate->closedVerified);
        CHECK(f.service.View()->snapshot.counts==launcherAmmo&&f.calls==1);

        // A replacement at the same native receiver address starts closed.
        auto replacement=cylinderResource;replacement.weaponGeneration=launcherResource.weaponGeneration+1;
        select(*cylinder,replacement,cylinderAmmo);f.Send(cylinderStart,false);
        CHECK(f.result.tracking.enabled&&f.result.tracking.step==FixedContainerStep::Open&&!f.result.blocksFire);
        CHECK(f.gate->lifetime>openCylinderLifetime&&f.service.View()->snapshot.counts==cylinderAmmo&&f.calls==1);
    }
    return 0;
}
int EveryFixedFamilyFreshPickupGapCompletesOnce(){
    unsigned profiles=0,linked=0;std::array<bool,5> kinds{};
    for(const auto registry:{std::span<const FixedContainerEquipmentProfile>(generated::FixedContainerEquipment),
                            std::span<const FixedContainerEquipmentProfile>(generated::UnderbarrelFixedContainerEquipment)})
        for(const auto& e:registry){
            Fixture f(&e,0,e.interaction.roundsPerInsertion);f.observe=true;SelectFixedFamily(f);
            const auto kind=e.interaction.kind;const bool mechanism=kind==FixedContainerKind::Cylinder||kind==FixedContainerKind::SingleRoundBreech;
            if(mechanism)f.Hinge(0);
            if(kind==FixedContainerKind::Cylinder)f.Hinge(1);
            CHECK(!InsertFixedWithOfflineReceipt(f,true));
            const auto committed=f.counts;if(mechanism)f.Hinge(3);
            CHECK(f.result.tracking.step==FixedContainerStep::Complete&&!f.result.blocksFire&&f.gate&&f.gate->closedVerified);
            CHECK(f.calls==1&&f.service.View()->snapshot.counts==committed);
            if(f.result.mechanismOwnsHand)f.Send(At(e.interaction.geometry[3].motion,1),false);
            CHECK(!f.hands.Current(InteractionHand::Left));
            CHECK(committed.loaded+committed.reserve==e.interaction.roundsPerInsertion);
            ++profiles;if(f.linkedFamily)++linked;kinds[static_cast<unsigned>(kind)]=true;
        }
    unsigned kindCount=0;for(bool checked:kinds)if(checked)++kindCount;
    CHECK(profiles==84&&linked==77&&kindCount==4);
    std::printf("Fixed fresh pickup gap: %u profiles, %u linked loaders, %u kinds, one mocked receipt each\n",profiles,linked,kindCount);
    return 0;
}

int EveryFixedFamilyTransfersDelayedSupportThroughFullCycle(){
    unsigned profiles=0,cycles=0,transfers=0,receipts=0,linked=0,nativeItem=0;
    for(const auto registry:{std::span<const FixedContainerEquipmentProfile>(generated::FixedContainerEquipment),
                            std::span<const FixedContainerEquipmentProfile>(generated::UnderbarrelFixedContainerEquipment)})
        for(const auto& e:registry){
            ++profiles;
            for(bool sameCurrent:{false,true}){
                Fixture f(&e,0,e.interaction.roundsPerInsertion);f.observe=true;SelectFixedFamily(f);
                if(f.linkedFamily)++linked;else ++nativeItem;
                const auto kind=e.interaction.kind;const bool mechanism=kind==FixedContainerKind::Cylinder||kind==FixedContainerKind::SingleRoundBreech;
                if(mechanism){CHECK(!TransferFixedSupportStroke(f,0,sameCurrent));++transfers;}
                if(kind==FixedContainerKind::Cylinder){CHECK(!TransferFixedSupportStroke(f,1,sameCurrent));++transfers;}
                CHECK(!InsertFixedWithOfflineReceipt(f));++receipts;
                const auto committed=f.counts;
                if(mechanism){CHECK(f.result.tracking.step==FixedContainerStep::Close);CHECK(!TransferFixedSupportStroke(f,3,sameCurrent));++transfers;}
                CHECK(f.result.tracking.step==FixedContainerStep::Complete&&!f.result.blocksFire&&f.gate&&f.gate->closedVerified);
                CHECK(f.calls==1&&f.counts==committed&&f.service.View()->snapshot.counts==committed);
                CHECK(!f.result.mechanismOwnsHand&&!f.result.ammoOwnsHand&&!f.hands.Current(InteractionHand::Left));++cycles;
            }
        }
    CHECK(profiles==84&&cycles==168&&receipts==168&&linked==154&&nativeItem==14&&transfers==238);
    std::printf("Fixed support arbitration: %u profiles, %u full cycles, %u guarded transfers, %u offline mocked adapter receipts\n",
        profiles,cycles,transfers,receipts);return 0;
}
int EveryFixedFamilyRejectsSupportTransferContradictions(){
    unsigned profiles=0,cases=0,committedPrefixes=0;
    for(const auto registry:{std::span<const FixedContainerEquipmentProfile>(generated::FixedContainerEquipment),
                            std::span<const FixedContainerEquipmentProfile>(generated::UnderbarrelFixedContainerEquipment)})
        for(const auto& e:registry){
            if(e.interaction.kind!=FixedContainerKind::Cylinder&&e.interaction.kind!=FixedContainerKind::SingleRoundBreech)continue;
            ++profiles;
            for(unsigned step:{0u,3u})for(unsigned fault=0;fault<18;++fault){
                Fixture f(&e,0,1);f.observe=true;SelectFixedFamily(f);
                if(step==3){
                    f.Hinge(0);if(e.interaction.kind==FixedContainerKind::Cylinder)f.Hinge(1);
                    CHECK(!InsertFixedWithOfflineReceipt(f));++committedPrefixes;
                }
                const auto counts=f.counts;const auto calls=f.calls;
                const auto completion=f.observation.lastCompletion;
                Bc2FixedContainerRawContact pressed;HandClaim support;
                const auto kind=fault==0?HandClaimKind::Sight:fault==1?HandClaimKind::BodyInventory:HandClaimKind::WeaponSupport;
                CHECK(!ArmFixedSupport(f,step,pressed,support,kind));
                const auto start=pressed.weaponFromWrist;auto rejected=pressed;
                std::optional<HandClaim> expected=support;
                switch(fault){
                case 0:case 1:break; // Unrelated dependent and body-owned claims cannot be stolen.
                case 2:rejected.weaponFromWrist.values[3][0]+=.5f;break; // Held contact miss consumes the edge.
                case 3:f.sample.cancel=true;break;
                case 4:{
                    // Ordinary support may renew from the actual pressed raw,
                    // but that does not renew the recognizer's neutral lease.
                    f.Prepare(start,true);
                    const auto renewed=f.hands.RenewFrom(f.sample.input,pressed.input,support.token,
                        {support.token.contact,pressed.input.sequence,pressed.input.deadlineNs,true});
                    CHECK(renewed.claim);expected=renewed.claim;
                    while(f.now<support.deadlineNs)f.Prepare(start,true);
                    CHECK(f.now==support.deadlineNs&&f.now<pressed.input.deadlineNs);break;
                }
                case 5:rejected.valid=false;break;
                case 6:++rejected.nativeOwner.weapon;break;
                case 7:++rejected.input.owner.equipGeneration;break;
                case 8:--rejected.input.deadlineNs;break; // Restamped raw is absent from arbiter history.
                case 9:f.Prepare(start,false);expected.reset();break; // Current release wins over older pressed raw.
                case 10:rejected.gripPressed=false;break; // Held entry is not the recorded press edge.
                case 11:f.allowGate=false;break;
                case 12:f.allowLedger=false;break;
                case 13:{
                    CHECK(f.hands.Release(f.sample.input,support.token).accepted);f.Prepare(start,true);
                    const auto gun=f.hands.Current(InteractionHand::Right);CHECK(gun);
                    const auto newer=f.hands.Acquire(f.sample.input,{f.owner,InteractionHand::Left,HandClaimKind::WeaponSupport,
                        f.sample.weapon,{support.token.contact,f.sequence,f.sample.input.deadlineNs,true},++f.intent,gun->token.id});
                    CHECK(newer.claim);expected=newer.claim;break; // Raw press cannot take a newer claim.
                }
                case 14:++f.sample.weapon.id;break;
                case 15:CHECK(f.hands.Release(f.sample.input,f.gun).accepted);expected.reset();break;
                case 16:{
                    // A different GunHold root cannot borrow a neutral armed
                    // under its predecessor, even on the same physical item.
                    CHECK(f.hands.Release(f.sample.input,f.gun).accepted);
                    const auto gun=f.hands.Acquire(f.sample.input,{f.owner,InteractionHand::Right,HandClaimKind::GunHold,f.sample.weapon,
                        {{300,1},f.sequence,f.sample.input.deadlineNs,true},++f.intent,0});
                    CHECK(gun.claim);f.gun=gun.claim->token;
                    // Dependency loss invalidates historical evidence through
                    // this packet; a new current-authorized intent can acquire.
                    const auto replacement=f.hands.Acquire(f.sample.input,{f.owner,InteractionHand::Left,
                        HandClaimKind::WeaponSupport,f.sample.weapon,{support.token.contact,f.sample.input.sequence,f.sample.input.deadlineNs,true},
                        ++f.intent,f.gun.id});
                    CHECK(replacement.claim);expected=replacement.claim;break;
                }
                case 17:f.busyGateMask=3;break; // A new transfer still needs a freshly published blocker.
                }
                f.Consume(&rejected);
                CHECK(!f.result.mechanismOwnsHand&&!f.result.ammoOwnsHand&&!f.result.tracking.target&&!f.result.tracking.pending);
                auto remaining=f.hands.Current(InteractionHand::Left);
                CHECK(remaining.has_value()==expected.has_value());
                if(expected)CHECK(remaining->token==expected->token&&remaining->inputSequence==expected->inputSequence&&remaining->deadlineNs==expected->deadlineNs);
                // Restore only the injected fault. Replaying the missed source
                // press cannot refresh support or manufacture a new gesture.
                f.sample.cancel=false;f.allowGate=f.allowLedger=true;f.busyGateMask=0;
                if(fault==14)--f.sample.weapon.id;
                f.Consume(&pressed);
                CHECK(!f.result.mechanismOwnsHand&&!f.result.ammoOwnsHand&&!f.result.tracking.target&&!f.result.tracking.pending);
                remaining=f.hands.Current(InteractionHand::Left);CHECK(remaining.has_value()==expected.has_value());
                if(expected)CHECK(remaining->token==expected->token&&remaining->inputSequence==expected->inputSequence&&remaining->deadlineNs==expected->deadlineNs);
                if(fault==2||fault==10){
                    f.Send(start,true);CHECK(!f.result.mechanismOwnsHand&&!f.result.tracking.target);
                    const auto held=f.hands.Current(InteractionHand::Left);
                    CHECK(held&&held->token==support.token&&held->inputSequence==support.inputSequence&&held->deadlineNs==support.deadlineNs);
                }
                CHECK(f.calls==calls&&f.counts==counts&&f.service.View()->snapshot.counts==counts);
                if(completion){
                    // Invalid-owner/root paths intentionally publish no slot
                    // diagnostics. Fresh ordinary selection exposes the same
                    // retained receipt after those contradictions are removed.
                    if(!f.hands.Current(InteractionHand::Right))f.gun={};
                    f.Send(start,false);
                    CHECK(f.observation.lastCompletion&&f.observation.lastCompletion->outcome.receipt==completion->outcome.receipt&&
                        f.observation.lastCompletion->outcome.request==completion->outcome.request);
                    CHECK(f.calls==calls&&f.service.View()->snapshot.counts==counts);
                }
                ++cases;
            }
        }
    CHECK(profiles==59&&cases==2124&&committedPrefixes==1062);
    std::printf("Fixed support arbitration contradictions: %u mechanism profiles, %u cases, %u retained offline receipt prefixes\n",
        profiles,cases,committedPrefixes);return 0;
}
int EveryFixedFamilyKeepsSupportOutOfAmmoCustody(){
    unsigned profiles=0;
    for(const auto registry:{std::span<const FixedContainerEquipmentProfile>(generated::FixedContainerEquipment),
                            std::span<const FixedContainerEquipmentProfile>(generated::UnderbarrelFixedContainerEquipment)})
        for(const auto& e:registry){
            Fixture f(&e,0,e.interaction.roundsPerInsertion);f.observe=true;SelectFixedFamily(f);
            if(e.interaction.kind==FixedContainerKind::Cylinder||e.interaction.kind==FixedContainerKind::SingleRoundBreech)f.Hinge(0);
            if(e.interaction.kind==FixedContainerKind::Cylinder)f.Hinge(1);
            Bc2FixedContainerRawContact pressed;HandClaim support;CHECK(!ArmFixedSupport(f,2,pressed,support));
            f.Consume(&pressed);const auto hand=f.hands.Current(InteractionHand::Left);
            CHECK(hand&&hand->token==support.token&&hand->inputSequence==support.inputSequence&&hand->deadlineNs==support.deadlineNs);
            CHECK(!f.result.ammoOwnsHand&&!f.result.mechanismOwnsHand&&!f.result.tracking.carried&&!f.result.tracking.pending);
            CHECK(f.calls==0&&f.service.View()->snapshot.counts==f.counts);
            // A fresh ordinary release/regrasp can still pick up the exact body
            // resource and complete its one existing ledger transaction.
            CHECK(!InsertFixedWithOfflineReceipt(f));CHECK(f.calls==1);++profiles;
        }
    CHECK(profiles==84);
    std::printf("Fixed support ammo custody: %u profiles reject support-to-ammo theft and retain ordinary receipt-backed insertion\n",profiles);
    return 0;
}
}
int FixedReleaseCauseDiagnostics(){
    // Actual348 showed that raw.valid=false is not an AmmoObject release.
    // Keep the diagnostic entry point, but correct its former drop contract.
    unsigned profiles=0;
    for(const auto registry:{std::span<const FixedContainerEquipmentProfile>(generated::FixedContainerEquipment),
                            std::span<const FixedContainerEquipmentProfile>(generated::UnderbarrelFixedContainerEquipment)})
        for(const auto& e:registry){
            Fixture f(&e,0,e.interaction.roundsPerInsertion);SelectFixedFamily(f);
            if(e.interaction.kind==FixedContainerKind::Cylinder||e.interaction.kind==FixedContainerKind::SingleRoundBreech)f.Hinge(0);
            if(e.interaction.kind==FixedContainerKind::Cylinder)f.Hinge(1);
            const auto start=At(e.interaction.geometry[2].motion,0);
            f.Send(start,false);f.Send(start,true,true);CHECK(f.result.ammoOwnsHand&&f.result.tracking.hand);
            const auto held=*f.result.tracking.hand;const auto counts=f.counts;const auto gun=f.gun;
            HandClaimDropJournal<4> journal;f.hands.ObserveDrops(journal.Observer());
            f.sample.raw.valid=false;f.Consume();
            CHECK(journal.count==0&&f.result.tracking.hand&&f.result.ammoOwnsHand);
            CHECK(f.result.tracking.hand->token==held.token&&f.result.tracking.hand->deadlineNs==held.deadlineNs);
            CHECK(f.result.tracking.carried&&!f.result.tracking.target&&!f.result.tracking.pending);
            CHECK(f.hands.Current(InteractionHand::Right)->token==gun&&f.service.View()->snapshot.counts==counts&&!f.submitted);
            f.Consume();CHECK(journal.count==0&&f.result.tracking.hand->deadlineNs==held.deadlineNs);++profiles;
        }
    CHECK(profiles==84);
    // Multiple simultaneous failures keep the first existing short-circuit
    // rejection; later cleanup cannot rewrite that original drop record.
    for(unsigned branch=0;branch<6;++branch){
        Fixture f;const auto start=At(f.equipment->interaction.geometry[0].motion,0);
        f.Send(start,false);f.Send(start,true);CHECK(f.result.mechanismOwnsHand);
        HandClaimDropJournal<4> journal;f.hands.ObserveDrops(journal.Observer());
        HandReleaseSite site=HandReleaseSite::Unknown;auto rejection=FixedContainerReleaseRejection::Unknown;
        if(branch==0){f.sample.cancel=true;f.sample.raw.valid=false;site=HandReleaseSite::FixedAdmission;rejection=FixedContainerReleaseRejection::CancelRequested;}
        if(branch==1){++f.sample.weapon.id;f.sample.raw.valid=false;site=HandReleaseSite::FixedNativeView;rejection=FixedContainerReleaseRejection::WeaponMismatch;}
        if(branch==2){++f.sample.raw.nativeOwner.weapon;f.sample.raw.input.owner.actor++;site=HandReleaseSite::FixedRaw;rejection=FixedContainerReleaseRejection::RawNativeOwner;}
        if(branch==4){f.Prepare(start,true);f.sample.raw.weaponFromWrist.values[3][0]+=10;site=HandReleaseSite::FixedPolicy;}
        if(branch==5){f.sample.raw.valid=false;site=HandReleaseSite::FixedRaw;rejection=FixedContainerReleaseRejection::RawInvalid;}
        if(branch==3)f.physical->Cancel(f.sample.input,f.hands);else f.Consume();
        if(branch==4){
            // A discontinuous held pose freezes movement; it grants neither
            // completion nor a native command, and keeps the acquired claim.
            CHECK(journal.count==0&&f.result.mechanismOwnsHand&&f.result.tracking.target);
            CHECK(f.result.tracking.step==FixedContainerStep::Open&&f.calls==0);continue;
        }
        CHECK(journal.count==1&&journal.entries[0].cause.site==site);
        const auto& cause=journal.entries[0].cause;
        CHECK(cause.rejection==(branch==3?HandReleaseCause::Unknown:static_cast<std::uint8_t>(rejection)));
        CHECK(!f.hands.Current(InteractionHand::Left)&&f.calls==0);
    }
    return 0;
}

int ReplacementRawGapRetainsCustodyAndResumes(){
    std::array<bool,5> checked{};unsigned kinds=0;
    for(const auto registry:{std::span<const FixedContainerEquipmentProfile>(generated::FixedContainerEquipment),
                            std::span<const FixedContainerEquipmentProfile>(generated::UnderbarrelFixedContainerEquipment)})
        for(const auto& e:registry){
            const auto kind=static_cast<unsigned>(e.interaction.kind);if(checked[kind])continue;checked[kind]=true;++kinds;
            Fixture f(&e,0,e.interaction.roundsPerInsertion);f.observe=true;SelectFixedFamily(f);
            if(e.interaction.kind==FixedContainerKind::Cylinder||e.interaction.kind==FixedContainerKind::SingleRoundBreech)f.Hinge(0);
            if(e.interaction.kind==FixedContainerKind::Cylinder)f.Hinge(1);
            const auto& motion=e.interaction.geometry[2].motion;const auto start=At(motion,0);
            f.Send(start,false);f.Send(start,true,true);f.Send(start,true);
            for(unsigned n=1;n<=10;++n)f.Send(At(motion,n/30.),true);
            CHECK(f.result.ammoOwnsHand&&f.result.tracking.hand&&f.result.tracking.target);
            const auto held=*f.result.tracking.hand;const auto before=f.counts;const auto raw=f.sample.raw;
            const auto carried=f.result.tracking.carriedSupply;const auto lifetime=f.result.tracking.lifetime;
            HandClaimDropJournal<4> journal;f.hands.ObserveDrops(journal.Observer());
            f.Prepare(At(motion,10/30.),true);f.sample.raw.valid=false;f.Consume();
            CHECK(!f.observation.rawAdmitted&&!f.observation.raw.valid&&journal.count==0);
            CHECK(f.result.ammoOwnsHand&&f.result.tracking.hand->token==held.token&&!f.result.tracking.target);
            CHECK(f.result.tracking.hand->inputSequence==f.sample.input.sequence&&f.result.tracking.hand->deadlineNs==f.sample.input.deadlineNs);
            CHECK(f.result.tracking.carriedSupply->values==carried->values&&f.result.tracking.lifetime==lifetime);
            CHECK(f.calls==0&&!f.submitted&&f.service.View()->snapshot.counts==before);
            const auto retained=*f.result.tracking.hand;
            f.Consume();CHECK(f.result.tracking.hand->deadlineNs==retained.deadlineNs&&journal.count==0);
            // A still-fresh renderer packet older than the safety renewal may
            // neither rewind custody nor restamp its contact lease.
            f.Prepare(At(motion,10/30.),true);f.Consume(&raw);
            CHECK(f.observation.rawAdmitted&&f.observation.raw.input.deadlineNs==raw.input.deadlineNs);
            CHECK(f.result.tracking.hand->inputSequence==retained.inputSequence&&f.result.tracking.hand->deadlineNs==retained.deadlineNs);
            CHECK(!f.result.tracking.target&&journal.count==0&&f.calls==0);
            f.Send(At(motion,10/30.),true);
            CHECK(f.result.ammoOwnsHand&&f.result.tracking.hand->token==held.token&&f.result.tracking.target);
            for(unsigned n=11;n<=30;++n)f.Send(At(motion,n/30.),true);
            for(unsigned n=0;n<8;++n)f.Send(At(motion,1),true);
            CHECK(f.calls==0&&f.result.ammoOwnsHand);
            f.Send(At(motion,1),false);
            CHECK(f.calls==1&&f.submitted&&f.submitted->preparedCommand&&f.result.tracking.pending);
            CHECK(f.submitted->preparedCommand->before==before&&f.service.View()->snapshot.counts==before);
            CHECK(!f.result.ammoOwnsHand); // Dispatch is not a native receipt.
        }
    CHECK(kinds==4);return 0;
}
int ReplacementRawGapStillHonorsInvalidation(){
    for(bool acquireBeforeGap:{false,true})for(unsigned branch=0;branch<13;++branch){
        Fixture f(1);const auto start=At(f.equipment->interaction.geometry[2].motion,0);
        f.Send(start,false);f.Send(start,true,true);if(acquireBeforeGap)f.Send(start,true);CHECK(f.result.ammoOwnsHand);
        const auto held=*f.result.tracking.hand;const auto counts=f.counts;
        if(branch==4)f.now=held.deadlineNs-10000000;
        if(branch==5){++f.owner.equipGeneration;f.gun={};}
        if(branch==12)++f.binding.context.resource.weaponGeneration;
        f.Prepare(start,branch!=0);f.sample.raw.valid=false;
        if(branch==1)f.sample.grip=false;
        if(branch==2)f.sample.input.tracked[0]=false;
        if(branch==3)f.sample.input.focused=false;
        if(branch==6)f.missingView=true;
        if(branch==7)f.staleView=true;
        if(branch==8){f.sample.raw.valid=true;++f.sample.raw.nativeOwner.weapon;}
        if(branch==9)f.sample.cancel=true;
        if(branch==10)(void)f.hands.Release(f.sample.input,f.gun);
        if(branch==11){f.sample.raw.valid=true;f.sample.raw.input.deadlineNs=f.now;}
        f.Consume();
        CHECK(!f.result.ammoOwnsHand&&!f.hands.Current(InteractionHand::Left));
        CHECK(f.calls==0&&!f.submitted&&f.service.View()->snapshot.counts==counts);
    }
    return 0;
}

int RecurringRawGapsConsumeDelayedGeometry(){
    for(bool expireGesture:{false,true}){
        Fixture f(1);f.observe=true;const auto& motion=f.equipment->interaction.geometry[2].motion;
        const auto start=At(motion,0);f.Send(start,false);f.Send(start,true,true);f.Send(start,true);
        CHECK(f.result.ammoOwnsHand&&f.result.tracking.hand&&f.result.tracking.target);
        const auto token=f.result.tracking.hand->token;const auto acquiredNs=f.now;
        const auto counts=f.counts;const auto supply=f.observation.supply;const auto lifetime=f.result.tracking.lifetime;
        HandClaimDropJournal<4> journal;f.hands.ObserveDrops(journal.Observer());
        f.Send(At(motion,.1),true);
        f.Prepare(At(motion,.1),true);auto delayed=f.sample.raw;f.sample.raw.valid=false;f.Consume();
        const auto cycles=expireGesture?motion.maximumGestureNs/20000000+3:40;
        bool expired=false;
        for(std::int64_t n=0;n<cycles;++n){
            const double progress=std::min(expireGesture?.4:1.,.1+(n+1)/30.);
            f.Prepare(At(motion,progress),true);f.sample.raw.valid=false;f.Consume();
            CHECK(f.result.ammoOwnsHand&&f.result.tracking.hand->token==token&&!f.result.tracking.target);
            CHECK(!f.observation.rawAdmitted&&!f.submitted&&f.calls==0);
            const auto retained=*f.result.tracking.hand;
            const bool release=!expireGesture&&n+1==cycles;
            f.Prepare(At(motion,progress),!release);const auto next=f.sample.raw;f.Consume(&delayed);
            CHECK(f.observation.rawAdmitted&&f.sample.input.sequence==delayed.input.sequence+2);
            CHECK(f.observation.raw.input.observedNs==delayed.input.observedNs&&
                  f.observation.raw.input.deadlineNs==delayed.input.deadlineNs);
            CHECK(f.observation.supply==supply&&f.result.tracking.lifetime==lifetime&&f.service.View()->snapshot.counts==counts);
            if(release){
                CHECK(f.calls==1&&f.submitted&&f.submitted->preparedCommand&&f.result.tracking.pending);
                CHECK(f.submitted->sourceProof&&f.submitted->sourceProof->inputSequence==delayed.input.sequence);
                CHECK(f.submitted->intent.observedNs==delayed.input.observedNs&&f.submitted->intent.deadlineNs==delayed.input.deadlineNs);
                CHECK(f.submitted->preparedCommand->before==counts&&!f.result.ammoOwnsHand);
            }else if(f.result.status==FixedContainerStatus::Suspended){
                CHECK(expireGesture&&f.now-acquiredNs>motion.maximumGestureNs);
                CHECK(journal.count==1&&journal.entries[0].cause.site==HandReleaseSite::FixedPolicy&&
                      journal.entries[0].cause.feedStatus==static_cast<std::uint8_t>(FeedInteractionStatus::Expired));
                CHECK(!f.result.ammoOwnsHand&&f.result.tracking.carried&&f.result.tracking.carriedSupply);
                CHECK(f.calls==0&&!f.submitted&&!f.result.tracking.pending);expired=true;break;
            }else{
                CHECK(f.calls==0&&!f.submitted&&journal.count==0&&f.result.tracking.target);
                CHECK(f.result.tracking.hand->token==token&&f.result.tracking.hand->inputSequence==retained.inputSequence&&
                      f.result.tracking.hand->deadlineNs==retained.deadlineNs);
                const auto expected=Multiply(feed_interaction_detail::Inverse(motion.placement.sourceFromGrip),delayed.weaponFromWrist);
                CHECK(feed_interaction_detail::Distance(*f.result.tracking.target,expected)<.00001);
            }
            delayed=next;
        }
        if(!expireGesture){CHECK(f.calls==1);continue;}
        CHECK(expired);const auto carried=*f.result.tracking.carriedSupply;
        const auto contact=Multiply(motion.placement.sourceFromGrip,carried);
        f.Send(contact,true);CHECK(!f.result.ammoOwnsHand&&f.calls==0); // Held input cannot reacquire the expired gesture.
        f.Send(contact,false);f.Send(contact,false);f.Send(contact,true);
        CHECK(f.result.ammoOwnsHand&&f.result.tracking.hand->token.id!=token.id&&f.observation.supply==supply);
        for(unsigned n=1;n<=30;++n)f.Send(At(motion,.4+.6*n/30.),true);
        for(unsigned n=0;n<8;++n)f.Send(At(motion,1),true);
        f.Send(At(motion,1),false);CHECK(f.calls==1&&f.submitted&&f.submitted->preparedCommand->before==counts);
        CHECK(f.service.View()->snapshot.counts==counts); // Dispatch still is not a native receipt.
    }
    return 0;
}

int ReadyForDuplicateRelease(Fixture& f){
    SelectFixedFamily(f);f.observe=true;f.releaseLevels=true;
    const auto kind=f.equipment->interaction.kind;
    if(kind==FixedContainerKind::Cylinder||kind==FixedContainerKind::SingleRoundBreech)f.Hinge(0);
    if(kind==FixedContainerKind::Cylinder)f.Hinge(1);
    const auto& motion=f.equipment->interaction.geometry[2].motion;
    f.Send(At(motion,0),false);f.Send(At(motion,0),true,true);f.Send(At(motion,0),true);
    for(unsigned n=1;n<=30;++n)f.Send(At(motion,n/30.),true);
    for(unsigned n=0;n<8;++n)f.Send(At(motion,1),true);
    CHECK(f.result.tracking.carried&&f.result.tracking.hand&&f.result.tracking.target&&f.calls==0);
    return 0;
}
int DuplicateRawRetainsExactReleaseUntilEligibleGeometry(){
    unsigned profiles=0;
    for(const auto registry:{std::span<const FixedContainerEquipmentProfile>(generated::FixedContainerEquipment),
                            std::span<const FixedContainerEquipmentProfile>(generated::UnderbarrelFixedContainerEquipment)})
        for(const auto& e:registry)for(bool newerSafety:{false,true}){
            Fixture f(&e,0,e.interaction.roundsPerInsertion);CHECK(!ReadyForDuplicateRelease(f));
            const auto held=f.sample.raw;const auto token=f.result.tracking.hand->token;
            const auto supply=f.observation.supply;const auto lifetime=f.result.tracking.lifetime;const auto before=f.counts;
            f.Prepare(At(e.interaction.geometry[2].motion,1),false);const auto released=f.sample.raw;
            CHECK(f.sample.releaseEvidence);const auto event=*f.sample.releaseEvidence;
            // Actual452's boundary shape: central release removes the exact
            // claim; an already consumed held raw packet reaches Gather first.
            f.Consume(&held);
            CHECK(!f.hands.Current(InteractionHand::Left)&&!f.result.ammoOwnsHand&&!f.result.tracking.hand);
            CHECK(f.result.tracking.carried&&f.calls==0&&!f.result.tracking.pending);
            CHECK(f.observation.lastRelease&&f.observation.lastRelease->stage==Bc2FixedContainerReleaseStage::DuplicateRaw);
            CHECK(!f.observation.lastRelease->policyUpdated);
            // Next Gather of the same packet has no central lifecycle event.
            f.sample.releaseEvidence.reset();++f.now;f.sample.input.nowNs=f.now;
            CHECK(f.hands.Update(f.sample.input).inputValid);f.Consume(&held);
            CHECK(f.calls==0&&!f.result.tracking.hand&&f.observation.lastRelease->retainedEvidence);
            if(newerSafety)f.Prepare(At(e.interaction.geometry[2].motion,1),false);
            f.Consume(&released);
            CHECK(f.calls==1&&f.submitted&&f.submitted->preparedCommand&&f.result.tracking.pending);
            CHECK(!f.result.ammoOwnsHand&&!f.hands.Current(InteractionHand::Left));
            CHECK(f.observation.supply==supply&&f.result.tracking.lifetime==lifetime);
            CHECK(f.submitted->preparedCommand->before==before&&f.service.View()->snapshot.counts==before);
            CHECK(f.submitted->intent.observedNs==released.input.observedNs&&f.submitted->intent.deadlineNs==released.input.deadlineNs);
            CHECK(f.submitted->sourceProof&&f.submitted->sourceProof->inputSequence==released.input.sequence);
            const auto attempt=*f.observation.lastRelease;
            CHECK(attempt.stage==Bc2FixedContainerReleaseStage::Submitted&&attempt.policyUpdated&&attempt.retainedEvidence);
            CHECK(attempt.evidence.before.token==token&&attempt.evidence.before.deadlineNs==event.before.deadlineNs);
            CHECK(attempt.evidence.input.sequence==event.input.sequence&&attempt.evidence.input.deadlineNs==event.input.deadlineNs);
            CHECK(attempt.status==FixedContainerStatus::NativeQueued&&attempt.dispatch);
            f.Consume(&released);f.Consume(&released);
            CHECK(f.calls==1&&f.observation.lastRelease->stage==attempt.stage&&f.observation.lastRelease->policyUpdated);
            CHECK(f.observation.lastRelease->input.sequence==attempt.input.sequence);
            std::ostringstream json;WriteFixedContainerObservation(json,&f.observation,f.native.owner,f.sample.input,f.sample.raw.rig,&e,f.now);
            CHECK(json.str().find("\"lastRelease\":{\"historical\":true")!=std::string::npos);
            CHECK(json.str().find("\"retainedEvidence\":true,\"policyUpdated\":true")!=std::string::npos);
            CHECK(json.str().size()<20000);++profiles;
        }
    CHECK(profiles==168);return 0;
}
int DuplicateReleaseCannotOutliveItsEvidence(){
    for(unsigned branch=0;branch<9;++branch){
        Fixture f(1);CHECK(!ReadyForDuplicateRelease(f));const auto held=f.sample.raw;
        const auto& motion=f.equipment->interaction.geometry[2].motion;const auto before=f.counts;
        f.Prepare(At(motion,1),false);auto released=f.sample.raw;CHECK(f.sample.releaseEvidence);
        const auto event=*f.sample.releaseEvidence;
        if(branch==0)f.sample.releaseEvidence.reset(); // No captured event may be reconstructed from neutral input.
        f.Consume(&held);CHECK(f.calls==0);f.sample.releaseEvidence.reset();
        if(branch>=1&&branch<=3){
            if(branch==1)++f.owner.actorGeneration;
            if(branch==2)++f.owner.equipGeneration;
            if(branch==3)++f.owner.space;
            f.gun={};f.Prepare(At(motion,1),false);
        }
        if(branch==4){f.now=event.before.deadlineNs;f.sample.input.nowNs=f.now;(void)f.hands.Update(f.sample.input);}
        if(branch==5){f.Prepare(At(motion,1),false);released=f.sample.raw;} // Raw newer than the eligible release is not replay authority.
        if(branch==6){f.Prepare(At(motion,1),true);} // A regrasp ends the neutral interval.
        if(branch==7){released.weaponFromWrist.values[3][0]+=10;}
        if(branch==8){
            CHECK(f.hands.Release(f.sample.input,f.gun).accepted);
            const auto replacement=f.hands.Acquire(f.sample.input,{f.owner,InteractionHand::Right,HandClaimKind::GunHold,f.sample.weapon,
                {{300,1},f.sequence,f.sample.input.deadlineNs,true},++f.intent,0});
            CHECK(replacement.claim);f.gun=replacement.claim->token;
        }
        f.Consume(&released);CHECK(f.calls==0&&!f.submitted&&!f.result.ammoOwnsHand);
        CHECK(f.service.View()->snapshot.counts==before&&!f.result.tracking.pending);
        if(branch==7){
            CHECK(f.observation.lastRelease&&f.observation.lastRelease->policyUpdated);
            const auto attempt=*f.observation.lastRelease;
            CHECK(attempt.stage==Bc2FixedContainerReleaseStage::Policy&&attempt.rejection==FixedContainerRejection::Gesture);
            CHECK(attempt.feedStatus==FeedInteractionStatus::MotionJump);
            f.Consume(&released);CHECK(f.result.status==FixedContainerStatus::Invalid);
            CHECK(f.observation.lastRelease->feedStatus==FeedInteractionStatus::MotionJump);
        }
    }
    return 0;
}

namespace {
Matrix NaturalFixedWrist(const FeedMotionProfile& motion,double progress,double twist=0,double drift=0){
    auto rotation=Identity();const auto angle=2.1+twist;
    rotation.values[0][0]=rotation.values[1][1]=float(std::cos(angle));
    rotation.values[0][1]=float(std::sin(angle));rotation.values[1][0]=float(-std::sin(angle));
    auto grasp=Multiply(rotation,motion.motionFromContact);
    if(motion.kind==FeedMotionKind::Hinge){
        const auto radius=std::hypot(grasp.values[3][0],grasp.values[3][1]);
        for(unsigned n=0;n<2;++n)grasp.values[3][n]*=float(1+.09/radius);
    }else grasp.values[3][1]+=.09f;
    auto local=feed_interaction_detail::Along(grasp,motion.kind,motion.travel*progress);
    local.values[3][2]+=float(drift);
    return Multiply(local,motion.weaponFromMotion);
}
int FixedMechanicalGraspAndHeldEndpointAcrossRegistry(){
    unsigned profiles=0,mechanical=0,linear=0,hinged=0;
    for(const auto registry:{std::span<const FixedContainerEquipmentProfile>(generated::FixedContainerEquipment),
                            std::span<const FixedContainerEquipmentProfile>(generated::UnderbarrelFixedContainerEquipment)})
        for(const auto& e:registry){
            ++profiles;const auto policy=FixedContainerInteractionProfile(e);
            CHECK(fixed_container_detail::Profile(policy));
            CHECK(feed_interaction_detail::Same(policy.geometry[2].motion,e.interaction.geometry[2].motion));
            CHECK(policy.geometry[2].contact==e.interaction.geometry[2].contact);
            if(e.interaction.kind!=FixedContainerKind::Cylinder&&e.interaction.kind!=FixedContainerKind::SingleRoundBreech)continue;
            ++mechanical;Fixture f(&e,0,e.interaction.roundsPerInsertion);SelectFixedFamily(f);
            const auto& motion=e.interaction.geometry[0].motion;
            if(motion.kind==FeedMotionKind::Linear)++linear;else ++hinged;
            CHECK(!motion.retainWhileGripped&&motion.acquisitionRadiusMeters==0);
            const auto start=NaturalFixedWrist(motion,0);
            f.Send(start,false);f.Send(start,true);
            CHECK(f.result.mechanismOwnsHand&&f.result.tracking.hand&&f.result.tracking.target);
            const auto token=f.result.tracking.hand->token;
            CHECK(feed_interaction_detail::Distance(*f.result.tracking.target,start)<.00001);
            CHECK(feed_interaction_detail::Angle(*f.result.tracking.target,start)<.001);
            for(unsigned n=1;n<=24;++n)f.Send(NaturalFixedWrist(motion,n/24.,n/24.,.025*n/24.),true);
            for(unsigned n=0;n<8;++n)f.Send(NaturalFixedWrist(motion,1,1,.025),true);
            const auto next=e.interaction.kind==FixedContainerKind::Cylinder?FixedContainerStep::EjectSpent:FixedContainerStep::Insert;
            CHECK(f.result.tracking.step==next&&f.result.mechanismOwnsHand&&f.result.tracking.hand->token==token);
            const auto target=f.result.tracking.target;const auto parts=f.result.tracking.parts;
            const auto life=f.result.tracking.lifetime;
            // A held hand at the next operation's source cannot recognize it.
            for(unsigned n=0;n<20;++n){
                f.Send(At(e.interaction.geometry[unsigned(next)].motion,0),true,true,true);
                CHECK(f.result.tracking.step==next&&f.result.mechanismOwnsHand&&f.result.tracking.hand->token==token);
                CHECK(feed_interaction_detail::Same(f.result.tracking.target,target)&&f.result.tracking.lifetime==life);
                for(unsigned i=0;i<f.result.tracking.partCount;++i)CHECK(f.result.tracking.parts[i].values==parts[i].values);
                CHECK(f.calls==0&&!f.result.tracking.pending&&f.service.View()->snapshot.counts==f.counts);
            }
            // Even a repeated old renderer packet cannot conceal real release.
            const auto raw=f.sample.raw;f.Prepare(*target,false);f.Consume(&raw);
            CHECK(!f.result.mechanismOwnsHand&&!f.result.tracking.hand&&!f.result.tracking.target);
            CHECK(f.result.tracking.step==next&&f.calls==0);
            for(unsigned i=0;i<f.result.tracking.partCount;++i)CHECK(f.result.tracking.parts[i].values==parts[i].values);
            if(next==FixedContainerStep::EjectSpent)f.Hinge(1);
            // Existing full registry tests cover exact insertion receipts.
            f.Send(At(e.interaction.geometry[2].motion,0),false,false,true);
            CHECK(f.result.tracking.step==FixedContainerStep::Close);f.Hinge(3);
            CHECK(f.result.tracking.step==FixedContainerStep::Complete&&f.result.mechanismOwnsHand&&!f.result.blocksFire);
            const auto closed=f.result.tracking.target;const auto closedToken=f.result.tracking.hand->token;
            for(unsigned n=0;n<12;++n){
                f.Send(start,true);CHECK(f.result.tracking.step==FixedContainerStep::Complete&&!f.result.blocksFire);
                CHECK(f.result.tracking.hand&&f.result.tracking.hand->token==closedToken&&feed_interaction_detail::Same(f.result.tracking.target,closed));
                CHECK(f.result.tracking.lifetime==life&&f.calls==0&&f.gate->closedVerified);
            }
            f.Send(start,false);CHECK(!f.result.tracking.hand&&!f.result.tracking.target);
        }
    CHECK(profiles==84&&mechanical==59&&linear&&hinged);
    std::printf("Fixed grasp: %u registered profiles, %u mechanical (%u rail/%u hinge), held open/close and explicit release\n",profiles,mechanical,linear,hinged);
    return 0;
}
int FixedPartialRecoveryPreservesNaturalWristAndMeasuredParts(){
    unsigned released=0,trackingGaps=0,rails=0,hinges=0;
    for(const auto registry:{std::span<const FixedContainerEquipmentProfile>(generated::FixedContainerEquipment),
                            std::span<const FixedContainerEquipmentProfile>(generated::UnderbarrelFixedContainerEquipment)})
        for(const auto& e:registry){
            if(e.interaction.kind!=FixedContainerKind::Cylinder&&e.interaction.kind!=FixedContainerKind::SingleRoundBreech)continue;
            const auto& motion=e.interaction.geometry[0].motion;
            if(motion.kind==FeedMotionKind::Linear)++rails;else ++hinges;
            for(bool trackingGap:{false,true}){
                Fixture f(&e,0,e.interaction.roundsPerInsertion);SelectFixedFamily(f);
                const auto wrist=[&](double progress,double twist=0){return NaturalFixedWrist(motion,progress,twist,.025);};
                f.Send(wrist(0),false);f.Send(wrist(0),true);
                for(unsigned n=1;n<=7;++n)f.Send(wrist(n/20.),true);
                CHECK(f.result.mechanismOwnsHand&&f.result.tracking.hand&&f.result.tracking.target);
                CHECK(f.result.tracking.step==FixedContainerStep::Open);
                const auto before=f.result.tracking;
                if(trackingGap){
                    f.Prepare(wrist(.35),true);f.sample.input.tracked[0]=false;f.Consume();++trackingGaps;
                }else {f.Send(wrist(.35),false);++released;}
                CHECK(!f.result.mechanismOwnsHand&&!f.hands.Current(InteractionHand::Left)&&!f.result.tracking.target);
                // Preparation and arming use fresh neutral packets; a new
                // claim may adopt a different natural wrist angle at regrasp.
                const auto regrasp=wrist(.35,.4);
                for(unsigned n=0;n<2;++n){
                    f.Send(regrasp,false);
                    CHECK(!f.result.mechanismOwnsHand&&!f.result.tracking.target);
                    CHECK(f.result.tracking.step==FixedContainerStep::Open&&f.result.tracking.lifetime==before.lifetime);
                    for(unsigned part=0;part<before.partCount;++part)
                        CHECK(f.result.tracking.parts[part].values==before.parts[part].values);
                }
                f.Send(regrasp,true);
                CHECK(f.result.mechanismOwnsHand&&f.result.tracking.hand&&f.result.tracking.target);
                CHECK(f.result.tracking.hand->token.id!=before.hand->token.id);
                CHECK(f.result.tracking.hand->token.owner==before.hand->token.owner);
                CHECK(f.result.tracking.lifetime==before.lifetime);
                CHECK(feed_interaction_detail::Distance(*f.result.tracking.target,regrasp)<.00001);
                CHECK(feed_interaction_detail::Angle(*f.result.tracking.target,regrasp)<.001);
                for(unsigned part=0;part<before.partCount;++part){
                    CHECK(feed_interaction_detail::Distance(f.result.tracking.parts[part],before.parts[part])<.00001);
                    CHECK(feed_interaction_detail::Angle(f.result.tracking.parts[part],before.parts[part])<.001);
                }
                const auto token=f.result.tracking.hand->token;
                const auto handFromContact=Multiply(regrasp,feed_interaction_detail::Inverse(At(motion,.35)));
                // The first ordinary update must not snap back to the authored
                // contact; subsequent motion retains that acquired wrist offset.
                for(unsigned n=7;n<=13;++n){
                    const auto progress=n/20.;f.Send(wrist(progress,.4),true);
                    CHECK(f.result.mechanismOwnsHand&&f.result.tracking.hand->token==token&&f.result.tracking.target);
                    CHECK(f.result.tracking.step==FixedContainerStep::Open&&f.result.tracking.targetStep==FixedContainerStep::Open);
                    const auto target=Multiply(handFromContact,At(motion,progress));
                    CHECK(feed_interaction_detail::Distance(*f.result.tracking.target,target)<.00001);
                    CHECK(feed_interaction_detail::Angle(*f.result.tracking.target,target)<.001);
                    const auto delta=Multiply(feed_interaction_detail::Inverse(At(motion,0)),At(motion,progress));
                    for(unsigned part=0;part<e.parts.size();++part)if(e.parts[part].coupledOpening){
                        const auto expected=motion.kind==FeedMotionKind::Linear?
                            reload_insertion_detail::BlendRigid(e.parts[part].weaponFromStepStart[0],e.parts[part].weaponFromStepEnd[0],progress):
                            Multiply(e.parts[part].weaponFromStepStart[0],delta);
                        CHECK(feed_interaction_detail::Distance(f.result.tracking.parts[part],expected)<.00001);
                        CHECK(feed_interaction_detail::Angle(f.result.tracking.parts[part],expected)<.001);
                    }
                    CHECK(f.calls==0&&!f.result.tracking.pending&&f.service.View()->snapshot.counts==f.counts);
                }
            }
        }
    CHECK(released==59&&trackingGaps==59&&rails==47&&hinges==12);
    std::printf("Fixed recovery: %u angled/off-axis partial releases, %u tracking gaps, %u rails/%u hinges preserve wrist and measured parts\n",released,trackingGaps,rails,hinges);
    return 0;
}
int FixedCompletedHoldKeepsSafetyBoundaries(){
    unsigned cases=0;
    for(const auto& e:generated::FixedContainerEquipment){
        if(e.interaction.kind!=FixedContainerKind::Cylinder)continue;
        for(unsigned fault=0;fault<8;++fault){
            Fixture f(&e,0,2);f.Hinge(0);CHECK(f.result.mechanismOwnsHand&&f.result.tracking.hand);
            const auto step=f.result.tracking.step;const auto counts=f.counts;
            f.Prepare(At(e.interaction.geometry[0].motion,1),true);
            if(fault==0)f.sample.input.focused=false;
            if(fault==1)f.sample.input.tracked[0]=false;
            if(fault==2)f.sample.input.nowNs=f.sample.input.deadlineNs;
            if(fault==3)++f.sample.raw.nativeOwner.weapon;
            if(fault==4)++f.sample.raw.input.owner.actor;
            if(fault==5)f.sample.raw.valid=false;
            if(fault==6)f.staleView=true;
            if(fault==7)CHECK(f.hands.Release(f.sample.input,f.gun).accepted);
            f.Consume();CHECK(!f.result.mechanismOwnsHand&&!f.hands.Current(InteractionHand::Left));
            CHECK(f.calls==0&&f.service.View()->snapshot.counts==counts);++cases;
        }
        Fixture miss(&e,0,2);auto far=NaturalFixedWrist(e.interaction.geometry[0].motion,0);
        far.values[3][0]+=1;miss.Send(far,false);miss.Send(far,true);
        CHECK(!miss.result.mechanismOwnsHand);miss.Send(At(e.interaction.geometry[0].motion,0),true);
        CHECK(!miss.result.mechanismOwnsHand&&miss.calls==0);
    }
    CHECK(cases==8);return 0;
}
}

// These are behavioral checks of the actual service, policy and hand arbiter;
// the fixture reproduces the renderer's original-input/raw-capture boundary.
namespace {
int BeforeUnavailablePreservesReleasedOpenBlocker(){
    for(bool migrate:{false,true}){
        Fixture f;const auto& opening=f.equipment->interaction.geometry[0].motion;
        f.Hinge(0);CHECK(f.result.tracking.step==FixedContainerStep::EjectSpent&&f.result.blocksFire);
        f.Send(At(opening,1),false);
        CHECK(!f.hands.Current(InteractionHand::Left)&&!f.result.tracking.hand&&f.result.blocksFire);
        const auto committed=f.result.tracking;const auto counts=f.service.View()->snapshot.counts;
        if(migrate){
            ++f.native.owner.equipGeneration;f.binding.owner=f.native.owner;
            ++f.binding.context.equipGeneration;++f.owner.equipGeneration;f.gun={};
        }
        f.unavailableGateMask=1;
        for(unsigned n=0;n<2;++n){
            f.Send(At(opening,1),false);
            CHECK(f.result.blocksFire&&!f.result.tracking.enabled&&!f.result.tracking.rawCaptureOnly);
            CHECK(!f.result.mechanismOwnsHand&&!f.result.ammoOwnsHand&&!f.result.tracking.hand&&!f.result.tracking.target);
            CHECK(!f.result.bodyAmmo&&!f.result.bodyVisual&&!f.hands.Current(InteractionHand::Left));
            CHECK(f.gateCalls==1&&f.calls==0&&f.service.View()->snapshot.counts==counts);
        }
        f.unavailableGateMask=0;f.Send(At(opening,1),false);
        CHECK(f.result.blocksFire&&f.result.tracking.step==committed.step&&f.result.tracking.lifetime==committed.lifetime);
        for(unsigned n=0;n<committed.partCount;++n)CHECK(f.result.tracking.parts[n].values==committed.parts[n].values);
        f.Hinge(1);f.Send(At(f.equipment->interaction.geometry[2].motion,0),false,false,true);f.Hinge(3);
        CHECK(f.result.tracking.step==FixedContainerStep::Complete&&!f.result.blocksFire);
        f.Send(At(opening,0),false);f.unavailableGateMask=1;f.Send(At(opening,0),false);
        CHECK(!f.result.blocksFire&&f.calls==0&&f.service.View()->snapshot.counts==counts);
    }
    return 0;
}
int BeforeUnavailablePreservesPendingAndAmmoBlockers(){
    Fixture pending(1);pending.observe=true;const auto& insertion=pending.equipment->interaction.geometry[2].motion;
    pending.Send(At(insertion,0),false);pending.Send(At(insertion,0),true,true);pending.Send(At(insertion,0),true);
    for(unsigned n=1;n<=30;++n)pending.Send(At(insertion,n/30.),true);
    for(unsigned n=0;n<8;++n)pending.Send(At(insertion,1),true);
    pending.Send(At(insertion,1),false);
    CHECK(pending.calls==1&&pending.submitted&&pending.result.tracking.pending&&pending.result.blocksFire);
    CHECK(pending.observation.pendingRequest==pending.submitted&&!pending.hands.Current(InteractionHand::Left));
    const auto request=pending.submitted;pending.unavailableGateMask=1;
    for(unsigned n=0;n<2;++n){
        pending.Send(At(insertion,1),false);
        CHECK(pending.result.blocksFire&&!pending.result.tracking.enabled&&!pending.result.tracking.rawCaptureOnly);
        CHECK(!pending.result.mechanismOwnsHand&&!pending.result.ammoOwnsHand&&!pending.result.bodyAmmo&&!pending.result.bodyVisual);
        CHECK(pending.observation.pendingRequest==request&&pending.submitted==request&&pending.calls==1&&pending.gateCalls==1);
        CHECK(pending.service.View()->snapshot.counts==pending.counts);
    }
    Fixture ammo;ammo.Hinge(0);ammo.Hinge(1);const auto& loading=ammo.equipment->interaction.geometry[2].motion;
    ammo.Send(At(loading,0),false);ammo.Send(At(loading,0),true,true);ammo.Send(At(loading,0),true);
    CHECK(ammo.result.blocksFire&&ammo.result.ammoOwnsHand&&ammo.result.tracking.hand);
    const auto token=ammo.result.tracking.hand->token;CHECK(token.kind==HandClaimKind::AmmoObject);
    ammo.unavailableGateMask=1;ammo.Send(At(loading,0),true);
    CHECK(ammo.result.blocksFire&&!ammo.result.tracking.enabled&&!ammo.result.tracking.rawCaptureOnly);
    CHECK(!ammo.result.ammoOwnsHand&&!ammo.result.mechanismOwnsHand&&!ammo.result.tracking.target);
    CHECK(!ammo.result.bodyAmmo&&!ammo.result.bodyVisual&&!ammo.hands.Current(InteractionHand::Left));
    CHECK(!ammo.hands.MechanismReservation(token)&&ammo.calls==0&&ammo.service.View()->snapshot.counts==ammo.counts);
    return 0;
}
int RecoveryFailurePreservesCommittedOpenBlocker(){
    for(unsigned fault=0;fault<4;++fault){
        Fixture f;const auto& opening=f.equipment->interaction.geometry[0].motion;
        f.Hinge(0);CHECK(f.result.tracking.step==FixedContainerStep::EjectSpent&&f.result.tracking.hand&&f.result.blocksFire);
        const auto token=f.result.tracking.hand->token;
        f.unavailableGateMask=1;f.Send(At(opening,1),true);
        CHECK(f.hands.MechanismReservation(token)&&f.result.blocksFire);
        f.unavailableGateMask=0;f.Send(At(opening,1),true);
        CHECK(f.result.tracking.rawCaptureOnly&&f.result.blocksFire);
        const auto capture=f.result.tracking;
        f.Prepare(At(opening,1),true);f.sample.raw.recoverySequence=capture.recoverySequence;
        if(fault==0)++f.sample.raw.nativeOwner.weapon;
        if(fault==1)f.allowLedger=false;
        if(fault==2)f.rejectedGateMask=2;
        if(fault==3)f.busyGateMask=2;
        f.Consume();
        CHECK(f.result.blocksFire&&!f.result.tracking.enabled&&!f.result.tracking.rawCaptureOnly);
        CHECK(!f.result.mechanismOwnsHand&&!f.result.ammoOwnsHand&&!f.result.bodyAmmo&&!f.result.bodyVisual);
        CHECK(!f.hands.MechanismReservation(token)&&!f.hands.Current(InteractionHand::Left));
        CHECK(f.calls==0&&f.service.View()->snapshot.counts==f.counts);
        f.allowLedger=true;f.rejectedGateMask=f.busyGateMask=0;f.Send(At(opening,1),false);
        CHECK(f.result.blocksFire&&f.result.tracking.step==FixedContainerStep::EjectSpent);
        for(unsigned n=0;n<capture.partCount;++n)CHECK(f.result.tracking.parts[n].values==capture.parts[n].values);
    }
    return 0;
}
int RetiredOwnerDoesNotExportOpenBlocker(){
    Fixture f;const auto& opening=f.equipment->interaction.geometry[0].motion;
    f.Hinge(0);CHECK(f.result.blocksFire&&f.result.tracking.hand);
    const auto token=f.result.tracking.hand->token;
    f.unavailableGateMask=1;f.Send(At(opening,1),true);CHECK(f.hands.MechanismReservation(token));
    f.Prepare(At(opening,1),true);++f.sample.nativeOwner.actorGeneration;f.Consume();
    CHECK(!f.result.blocksFire&&!f.result.tracking.enabled&&f.gateCalls==0);
    CHECK(!f.hands.MechanismReservation(token)&&!f.hands.Current(InteractionHand::Left));
    ++f.native.owner.actorGeneration;f.binding.owner=f.native.owner;
    ++f.binding.context.resource.actorGeneration;++f.owner.actorGeneration;f.gun={};f.unavailableGateMask=0;
    f.Send(At(opening,0),false);
    CHECK(f.result.tracking.enabled&&f.result.tracking.step==FixedContainerStep::Open&&!f.result.blocksFire&&f.calls==0);
    return 0;
}
int BindingGapReservationsAcrossMechanicalRegistry(){
    unsigned cases=0;
    for(const auto registry:{std::span<const FixedContainerEquipmentProfile>(generated::FixedContainerEquipment),
                            std::span<const FixedContainerEquipmentProfile>(generated::UnderbarrelFixedContainerEquipment)})
        for(const auto& e:registry){
            if(e.interaction.kind!=FixedContainerKind::Cylinder&&e.interaction.kind!=FixedContainerKind::SingleRoundBreech)continue;
            for(unsigned position:{1u,2u})for(unsigned gaps:{1u,2u}){
                Fixture f(&e,0,e.interaction.roundsPerInsertion);SelectFixedFamily(f);
                const auto& motion=e.interaction.geometry[0].motion;
                f.Send(At(motion,0),false);f.Send(At(motion,0),true);f.Send(At(motion,.25),true);
                CHECK(f.result.mechanismOwnsHand&&f.result.tracking.hand);
                const auto before=f.result.tracking;const auto entry=*before.hand;
                f.unavailableGateMask=position;f.Send(At(motion,.30),true);
                CHECK(!f.result.tracking.enabled&&!f.result.tracking.rawCaptureOnly&&!f.result.mechanismOwnsHand);
                CHECK(!f.result.bodyAmmo&&!f.result.bodyVisual&&!f.result.tracking.target&&!f.result.tracking.hand);
                CHECK(!f.hands.Current(InteractionHand::Left)&&f.calls==0);
                auto reserved=f.hands.MechanismReservation(entry.token);
                CHECK(reserved&&reserved->deadlineNs==entry.deadlineNs);
                // Ordinary renew cannot use a reservation.
                const auto renewed=f.hands.Renew(f.sample.input,entry.token,
                    {entry.token.contact,f.sequence,f.sample.input.deadlineNs,true});
                CHECK(!renewed.claim&&!f.hands.Current(InteractionHand::Left));
                const auto transfer=f.hands.Transfer(f.sample.input,entry.token,
                    {f.owner,InteractionHand::Left,HandClaimKind::WeaponSupport,f.sample.weapon,
                     {{301,1},f.sequence,f.sample.input.deadlineNs,true},++f.intent,f.gun.id});
                CHECK(!transfer.claim&&f.hands.MechanismReservation(entry.token));
                if(gaps==2){f.unavailableGateMask=1;f.Send(At(motion,.30),true);
                    CHECK(f.hands.MechanismReservation(entry.token)->deadlineNs==entry.deadlineNs);}
                f.unavailableGateMask=0;f.rendererFeedback=true;f.Send(At(motion,position==1?.25:.30),true);
                CHECK(f.result.tracking.rawCaptureOnly&&!f.result.tracking.enabled&&!f.result.mechanismOwnsHand);
                CHECK(!FixedContainerTrackingFresh(f.result.tracking,f.now)&&FixedContainerRawCaptureFresh(f.result.tracking,f.now));
                CHECK(!f.result.tracking.target&&!f.result.tracking.hand&&!f.hands.Current(InteractionHand::Left));
                const auto committed=f.result.tracking;bool changed=false;
                for(unsigned n=0;n<before.partCount;++n)changed|=before.parts[n].values!=committed.parts[n].values;
                CHECK(changed==(position==2)); // Before: no progress. Final: explicit prior-gate commit.
                CHECK(committed.step==before.step&&committed.lifetime==before.lifetime&&f.calls==0);
                f.Send(At(motion,position==1?.25:.30),true);
                CHECK(f.result.tracking.enabled&&!f.result.tracking.rawCaptureOnly&&f.result.mechanismOwnsHand);
                CHECK(f.result.tracking.hand&&f.result.tracking.hand->token==entry.token);
                CHECK(f.result.tracking.hand->deadlineNs==entry.deadlineNs&&!f.hands.MechanismReservation(entry.token));
                for(unsigned n=0;n<before.partCount;++n)CHECK(f.result.tracking.parts[n].values==committed.parts[n].values);
                CHECK(f.result.tracking.step==committed.step&&f.result.tracking.lifetime==before.lifetime&&f.calls==0);
                // An actual newer movement, after the recovery baseline, may progress.
                f.Send(At(motion,position==1?.30:.35),true);
                CHECK(f.result.mechanismOwnsHand&&f.result.tracking.hand->token==entry.token&&f.calls==0);
                ++cases;
            }
        }
    CHECK(cases==236);return 0;
}
int BindingGapRequiresNewContactAndExpires(){
    for(unsigned fault=0;fault<6;++fault){
        Fixture f;const auto& motion=f.equipment->interaction.geometry[0].motion;
        f.Send(At(motion,0),false);f.Send(At(motion,0),true);
        const auto entry=*f.result.tracking.hand;const auto oldRaw=f.sample.raw;
        f.unavailableGateMask=1;f.Send(At(motion,0),true);
        f.unavailableGateMask=0;f.Send(At(motion,0),true);
        CHECK(f.result.tracking.rawCaptureOnly&&!f.hands.Current(InteractionHand::Left));
        const auto capture=f.result.tracking;
        f.Prepare(At(motion,0),true);
        switch(fault){
        case 0:f.sample.raw={};break;
        case 1:f.sample.raw=oldRaw;break;
        case 2:f.sample.raw.recoverySequence=capture.recoverySequence+1;break;
        case 3:f.sample.raw.recoverySequence=capture.recoverySequence;
            f.sample.raw.weaponFromWrist.values[3][0]+=1;break;
        case 4:f.sample.raw.recoverySequence=capture.recoverySequence;
            f.sample.raw.input.sequence+=100;break;
        case 5:f.sample.raw.recoverySequence=capture.recoverySequence;
            f.sample.raw.input.observedNs-=1;break; // Unrecorded counterfeit packet.
        }
        f.Consume();CHECK(!f.result.tracking.enabled&&!f.hands.Current(InteractionHand::Left));
        CHECK(f.hands.MechanismReservation(entry.token)&&f.hands.MechanismReservation(entry.token)->deadlineNs==entry.deadlineNs);
        f.now=entry.deadlineNs-10000000;f.Send(At(motion,0),true);
        CHECK(!f.hands.MechanismReservation(entry.token)&&!f.hands.Current(InteractionHand::Left)&&f.calls==0);
        f.Send(At(motion,0),true);CHECK(!f.result.mechanismOwnsHand); // Held input cannot resurrect expiry.
    }
    return 0;
}
int BindingGapRevokesObservableContradictions(){
    for(unsigned fault=0;fault<15;++fault){
        Fixture f;const auto& motion=f.equipment->interaction.geometry[0].motion;
        f.Send(At(motion,0),false);f.Send(At(motion,0),true);
        const auto token=f.result.tracking.hand->token;
        f.unavailableGateMask=1;f.Send(At(motion,0),true);
        CHECK(f.hands.MechanismReservation(token));
        f.unavailableGateMask=0;
        switch(fault){
        case 0:++f.binding.inventory;break;
        case 1:++f.binding.switching;break;
        case 2:++f.binding.data;break;
        case 3:++f.binding.persistence;break;
        case 4:++f.binding.context.resource.weaponGeneration;break;
        case 5:++f.owner.equipGeneration;break;
        case 6:f.missingView=true;break;
        case 7:f.staleView=true;break;
        case 8:f.allowLedger=false;break;
        case 9:f.rejectedGateMask=1;break;
        }
        f.Prepare(At(motion,0),fault!=10);
        switch(fault){
        case 11:f.sample.cancel=true;break;
        case 12:(void)f.hands.Release(f.sample.input,f.gun);break;
        case 13:f.sample.input.focused=false;break;
        case 14:++f.sample.raw.nativeOwner.weapon;break;
        }
        f.Consume();CHECK(!f.hands.MechanismReservation(token)&&!f.hands.Current(InteractionHand::Left)&&f.calls==0);
        if(fault==9){f.rejectedGateMask=0;f.busyGateMask=3;f.Send(At(motion,0),true);
            CHECK(!f.result.mechanismOwnsHand&&!f.result.tracking.enabled);}
    }
    return 0;
}
int FinalUnavailablePreservesQueuedObligationWithoutDispatch(){
    Fixture f(1);const auto& insertion=f.equipment->interaction.geometry[2].motion;
    f.Send(At(insertion,0),false);f.Send(At(insertion,0),true,true);f.Send(At(insertion,0),true);
    for(unsigned n=1;n<=30;++n)f.Send(At(insertion,n/30.),true);
    for(unsigned n=0;n<8;++n)f.Send(At(insertion,1),true);
    f.unavailableGateMask=2;f.Send(At(insertion,1),false);
    CHECK(f.result.tracking.pending&&!f.result.tracking.enabled&&f.result.blocksFire);
    CHECK(f.calls==0&&!f.submitted&&f.service.View()->snapshot.counts==f.counts);
    f.unavailableGateMask=0;f.Send(At(insertion,1),false);
    CHECK(!f.result.tracking.pending&&f.calls==0&&!f.submitted&&f.service.View()->snapshot.counts==f.counts);
    return 0;
}
int BindingGapResetsDwellAndPreservesCommittedStep(){
    for(bool finalCommit:{false,true}){
        Fixture f;const auto& motion=f.equipment->interaction.geometry[0].motion;
        f.Send(At(motion,0),false);f.Send(At(motion,0),true);
        for(unsigned n=1;n<=10;++n)f.Send(At(motion,n/10.),true);
        CHECK(f.result.tracking.step==FixedContainerStep::Open&&motion.endpointDwellNs==60000000);
        if(finalCommit)for(unsigned n=0;n<5;++n)f.Send(At(motion,1),true);
        CHECK(f.result.tracking.step==FixedContainerStep::Open);
        const auto entry=*f.result.tracking.hand;
        f.unavailableGateMask=finalCommit?2:1;f.Send(At(motion,1),true);
        CHECK(f.hands.MechanismReservation(entry.token)&&!f.hands.Current(InteractionHand::Left));
        // Gap time exceeds dwell but stays inside the original 100 ms lease.
        if(!finalCommit)f.now+=50000000;
        f.unavailableGateMask=0;f.rendererFeedback=true;f.Send(At(motion,1),true);
        CHECK(f.result.tracking.rawCaptureOnly&&f.result.tracking.step==
            (finalCommit?FixedContainerStep::EjectSpent:FixedContainerStep::Open));
        const auto committed=f.result.tracking.parts;
        f.Send(At(motion,1),true);
        CHECK(f.result.mechanismOwnsHand&&f.result.tracking.hand->token==entry.token&&
            f.result.tracking.hand->deadlineNs==entry.deadlineNs);
        f.Send(At(motion,1),true);
        CHECK(f.result.tracking.step==(finalCommit?FixedContainerStep::EjectSpent:FixedContainerStep::Open));
        for(unsigned n=0;n<f.result.tracking.partCount;++n)CHECK(f.result.tracking.parts[n].values==committed[n].values);
        CHECK(f.calls==0);
    }
    return 0;
}
}
// The spent-case checks share this fixture and native-resource service mock.
#include "Bc2SpentCaseChecks.inc"
int main(){
    if(SpentCaseConsumptionDoesNotInventOrTransferRounds()||SpentCaseTiltNeedsFreshNeutralOpenPackets()||
       SpentCaseMeasuredAnchorAndBoundedFall()||SpentCaseRetainedRenderingRejectsDifferentCase()||
       SpentCaseOpeningFamiliesEjectBeforeExactInsertion())return 1;
    if(UnderbarrelContactGuidanceAcrossBothFamilies())return 1;
    if(BeforeUnavailablePreservesReleasedOpenBlocker()||BeforeUnavailablePreservesPendingAndAmmoBlockers()||
       RecoveryFailurePreservesCommittedOpenBlocker()||RetiredOwnerDoesNotExportOpenBlocker())return 1;
    if(BindingGapReservationsAcrossMechanicalRegistry()||BindingGapRequiresNewContactAndExpires()||
       BindingGapRevokesObservableContradictions()||FinalUnavailablePreservesQueuedObligationWithoutDispatch()||
       BindingGapResetsDwellAndPreservesCommittedStep())return 1;
    if(FixedPartialRecoveryPreservesNaturalWristAndMeasuredParts())return 1;
    if(FixedMechanicalGraspAndHeldEndpointAcrossRegistry()||FixedCompletedHoldKeepsSafetyBoundaries())return 1;if(DuplicateRawRetainsExactReleaseUntilEligibleGeometry()||DuplicateReleaseCannotOutliveItsEvidence())return 1;
    if(FixedReleaseCauseDiagnostics())return 1;
    if(InterruptedGateEnrollmentKeepsBothWeaponsUsable())return 1;
    if(ReplacementRawGapRetainsCustodyAndResumes()||ReplacementRawGapStillHonorsInvalidation())return 1;
    if(RecurringRawGapsConsumeDelayedGeometry()||EveryFixedFamilyFreshPickupGapCompletesOnce())return 1;
    return ExactSelectorsAndSupply()||GateBeforePhysicalAndExactOwner()||BusyMechanismFeedbackContinuesOpening()||
    BusyCannotStartReviveOrChangeOwner()||BusyExpiresWithoutGateLeaseExtension()||
    FailedGateRevokesReservationAndRejectedMechanism()||BusyCannotAuthorizeDispatchOrClosure()||
    CylinderFlickReturnAndClose()||ClipQueuesOneSharedCommand()||
    PriorRendererFrameQueuesAndCompletesOnceAfterReequip()||PartialCylinderMigrationReacquiresAtRetainedContact()||
    ObservationPreservesSourceLeasesAndRejectsWrongIdentity()||LinearPartsFollowTheirOwnMeasuredTravel()||MeasuredUnderbarrelLoadsOneRoundAndCloses()||
    LinkedLoaderRejectsUnverifiedPhysicalAliases()||EveryFixedFamilyTransfersDelayedSupportThroughFullCycle()||
    EveryFixedFamilyRejectsSupportTransferContradictions()||EveryFixedFamilyKeepsSupportOutOfAmmoCustody();}
