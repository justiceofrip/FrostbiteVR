// Reuse the exact native callback fixture; these are synthetic observations,
// never claims that a test invoked the game or manufactured a live ready ack.
#define main NativeCycleRegressionMain
#include "Bc2NativeCycleServiceTests.cpp"
#undef main
#include "Bc2BoltCustodyBridge.h"
#include "Bc2BoltPlayerCustody.h"
#include "Bc2BodyHolsterProbeFixture.h"
#include "Bc2BoltRuntimeGeometry.h"
#include "Bc2PumpActions.h"
#include "fvr/interaction/SupportGrip.h"
#include <sstream>
#include <fstream>
#include <iostream>
namespace {
struct ControllerFixture {
    Fixture native{true};HandInteraction hands;SupportGrip support;ControllerActions actions;
    RigSnapshot rig;std::shared_ptr<Bc2BoltCalibration> calibration;std::unique_ptr<Bc2PhysicalBolt> bolt;
    std::unique_ptr<Bc2BoltControllerProbe> probe;Bc2BoltControllerContact raw{};Bc2PhysicalBoltResult result{};
    InputFrame input{};HandInteractionSample sample{};std::uint64_t intent=100;unsigned shots=0,releases=0,acks=0,transfers=0;
    Bc2BoltPackCounters packs{};std::int64_t releaseAt=0;std::array<fvr::math::Matrix4,2> refs{};fvr::math::Matrix4 weapon=Pose();
    bool realPlacement=false,ordinary=false,missingPlayerReferences=false;
    std::unique_ptr<Bc2BoltPlayerCustody> player;InputFrame previousInput;
    ControllerFixture(unsigned count,bool rendererPlacement=false,bool ordinaryPlayer=false):realPlacement(rendererPlacement),ordinary(ordinaryPlayer){
        calibration=std::make_shared<Bc2BoltCalibration>(*M95PrivateBoltCalibration235());
        rig.names={"root","jntWpn_1","jntWpn_3","other"};rig.parents={-1,0,1,0};rig.weaponBone=1;
        rig.identity={0x11000,0x12000,0x50000,0x60000,0x70000,0x80000,0x90000,0xa0000,0xb0000,4,false};
        rig.world.assign(4,Pose());rig.inverseBind.assign(4,Pose());rig.evaluatedWorld.assign(4,Pose());rig.nativeEvaluated.resize(4);
        calibration->rigFingerprint=DeriveBoltPart(rig,"jntWpn_3")->fingerprint; // Explicit synthetic rig only.
        probe=std::make_unique<Bc2BoltControllerProbe>(*calibration,count,ordinary?Bc2BoltControllerProbe::Controls::OrdinaryPlayer:Bc2BoltControllerProbe::Controls::ExplicitCustodyFixture);
        if(ordinary)player=std::make_unique<Bc2BoltPlayerCustody>(calibration);
        Bc2PhysicalBoltApi api{this,
            [](void* p,const Bc2NativeCycleControl& c)noexcept{auto& f=*static_cast<ControllerFixture*>(p);f.native.control=c;
                if(c.release){++f.releases;f.releaseAt=f.native.clock;}return f.native.service.Control(c);},
            [](void* p,std::int64_t now)noexcept->std::optional<Bc2NativeCycleView>{return static_cast<ControllerFixture*>(p)->native.service.View(now);},
            [](void* p,const WeaponCycleReady& r)noexcept{auto& f=*static_cast<ControllerFixture*>(p);const auto okay=f.native.service.AcknowledgeReady(r);if(okay)++f.acks;return okay;},
            [](void* p)noexcept{static_cast<ControllerFixture*>(p)->native.service.Cancel(Bc2NativeCycleFailure::Control);}};
        bolt=std::make_unique<Bc2PhysicalBolt>(calibration,api);
        weapon.values[3][1]=-.2f;weapon.values[3][2]=.5f;
        refs={Pose(),Pose()};refs[0].values[3][0]=-.08f;refs[0].values[3][2]=.1f;
        refs[1].values[3][0]=.08f;refs[1].values[3][2]=-.3f;
        for(auto& b:native.input.branches){b.currentState=b.nextState=2;b.previousState=1;b.phaseTimer=0;b.loaded=5;}
        input.spaceGeneration=native.control.input.owner.space;input.focused=input.headValid=true;
        for(auto& h:input.hands){h.active=Components;h.gripTracked=h.aimTracked=true;}
        input.hands[0].grip.position={-.2f,-.2f,-.5f};input.hands[1].grip.position={.08f,-.2f,-.2f};
    }
    bool Step(bool missingRaw=false){
        native.clock+=10000000;input.generation++;input.predictedNs=native.clock;
        for(auto& h:input.hands){h.trigger=h.squeeze=0;h.held=0;h.stickX=h.stickY=0;}
        auto reserve=Bc2AmmoReserveLease{};reserve.identity=native.input.identity;reserve.verified=true;reserve.sequence=input.generation;
        reserve.observedNs=native.clock;reserve.deadlineNs=native.clock+100000000;reserve.capacity=5;reserve.loaded=native.input.branches[2].loaded;
        reserve.reserve=45;reserve.allThreeIdle=native.input.branches[2].currentState==2;
        const auto observed=native.clock,deadline=observed+100000000;
        if(ordinary){
            if(!native.service.View(native.clock).selectedMode){auto selected=Selection(native,Bc2NativeCycleMode::M95,input.generation);
                if(!native.service.Select(selected))return false;}
            auto body=std::make_shared<BodyHolsterProbeSample>();body->nativeOwner=native.input.identity.owner;
            body->sampledNs=native.clock;body->hand={native.control.input.owner,input.generation,observed,deadline,native.clock,true,{true,true},{true,false}};
            body->input=previousInput;body->input.generation=input.generation;body->input.spaceGeneration=input.spaceGeneration;
            body->physicalGun=native.control.item;body->nativeTick=input.generation;body->phase=BodyHolsterPhase::Held;
            body->selectedSlot=BodySlotAssignment{2,{native.control.item.id,1}};
            body->ordinaryVisible=BodyVisibleRig{body->nativeOwner,body->hand.sequence,body->hand.observedNs,body->hand.deadlineNs};
            body->outcome.allowAutomaticGunHold=true;body->outcome.blockWeaponActions=false;
            HandClaim claim;claim.token={1,body->hand.owner,InteractionHand::Right,HandClaimKind::GunHold,native.control.item,{1,99},0};
            claim.inputSequence=input.generation;claim.deadlineNs=deadline;body->right=claim;
            probe->ObserveBody(body);probe->ObservePlayer(!missingPlayerReferences&&player->ReferencesReady(),true);
        }
        probe->Prepare(input,native.input.identity.owner,"M95_sp",missingRaw?Bc2BoltControllerContact{}:raw,native.service.View(native.clock),reserve,observed,deadline,native.clock);
        auto mapped=actions.Update(input,{native.input.identity.owner.soldier,1,true,true},native.clock);
        if(ordinary&&probe->NeedsNeutralFire(native.clock)){mapped.held&=~Fire;mapped.pressed&=~Fire;}
        sample={native.control.input.owner,input.generation,observed,deadline,native.clock,true,{true,true},
            {input.hands[0].squeeze<=.35f,input.hands[1].squeeze<=.35f}};
        std::optional<Bc2PhysicalBoltSample> ordinarySample;bool early=false;
        if(ordinary){
            ordinarySample=player->Prepare({native.input.identity.owner,native.control.item,calibration->asset,calibration->mesh,&input,sample,
                {native.input.identity.owner,sample,input.referenceHead,Pose(),1},raw,native.service.View(native.clock),reserve},hands,intent);
            if(!ordinarySample){result={};result.tracking={true,native.input.identity.owner,sample,calibration};result.blocksFire=true;}
            if(ordinarySample&&ordinarySample->custodyTransfer&&ordinarySample->custodyTransfer->releaseDepartingGun){
                result=bolt->Tick(*ordinarySample,hands,intent);player->Observe(result);early=true;
            }
        }
        hands.Update(sample);
        if(result.tracking.custody!=BoltCustodyPhase::Manipulating&&!sample.released[1]){
            const auto gun=hands.Current(InteractionHand::Right);HandContactProof contact{{1,99},sample.sequence,sample.deadlineNs,true};
            if(gun)hands.Renew(sample,gun->token,contact);
            else hands.Acquire(sample,{sample.owner,InteractionHand::Right,HandClaimKind::GunHold,native.control.item,contact,++intent,0});
        }
        Bc2PhysicalBoltSample s;s.nativeOwner=native.input.identity.owner;s.input=sample;s.item=native.control.item;
        s.asset=calibration->asset;s.mesh=calibration->mesh;s.raw=raw.raw;s.weaponWorld=weapon;
        for(unsigned n=0;n<2;++n){const auto view=fvr::math::MakeLhViewFromOpenXRPose(input.hands[n].grip);if(!view)return false;
            s.wristWorld[n]=*InverseRigid(*view);s.gunContacts[n]={{n==0?2u:1u,n==0?s.item.id:s.item.generation},sample.sequence,sample.deadlineNs,!sample.released[n]};}
        s.grip=input.hands[1].squeeze>=.7f;
        const auto grips=probe->OriginalGripReferences();
        if(ordinary&&(grips||probe->WantsEnter()||probe->WantsReturn()))return false;
        if(grips)s.custodyTransfer=BuildBoltCustodyTransfer(sample,s.item,raw,*grips,hands,probe->WantsEnter(),probe->WantsReturn(),intent);
        if(ordinary){if(ordinarySample&&!early){result=bolt->Tick(*ordinarySample,hands,intent);player->Observe(result);}}
        else result=bolt->Tick(s,hands,intent);
        if(result.custodyChange&&result.custodyChange->transaction.accepted){++transfers;
            if(result.tracking.custody==BoltCustodyPhase::Returned&&(!hands.Current(InteractionHand::Right)||!hands.Current(InteractionHand::Left)))return false;}
        ApplyPumpActionGate(mapped,result.blocksFire);
        if(ordinary)ApplyPumpResourceHandoffGate(mapped,!player->ReferencesReady()||player->BlocksFire());
        if(mapped.pressed&Fire){++shots;const auto before=native.input.branches[2].loaded;
            for(auto& b:native.input.branches){b.loaded=before-1;b.currentState=8;b.previousState=7;b.nextState=1;b.phaseTimer=2.2f;}
            for(unsigned n=0;n<3;++n)native.Shot(n,before);
            for(unsigned n=0;n<2;++n){auto restore=native.Restore(n);native.service.ObserveRestore(restore);native.input.branches[n].previousState=8;}
        }
        if(native.service.View(native.clock).phase==Bc2NativeCyclePhase::Releasing&&native.clock-releaseAt>=2300000000ll){
            for(unsigned n=0;n<3;++n)native.Resume(n,true);
        }else for(unsigned n=0;n<3;++n){native.Begin(n);native.Finish();}
        probe->Observe(result,native.service.View(native.clock),packs,native.clock);
        const SupportGripOwner supportOwner{sample.owner.actor,sample.owner.actorGeneration,s.item.id};
        SupportGripContact contact;contact.valid=raw.raw.valid;
        const auto supportTarget=Multiply(refs[0],raw.weaponWorldMeters);float squared=0;
        for(unsigned axis=0;axis<3;++axis){const auto d=supportTarget.values[3][axis]-raw.rawWristWorldMeters[0].values[3][axis];squared+=d*d;}
        contact.distanceMeters=std::sqrt(squared);
        auto proposed=support;SupportGripResult supported;
        if(result.custodyChange&&result.custodyChange->transaction.accepted&&result.custodyChange->companion){
            supported=proposed.AdoptHeld(supportOwner,input,contact,sample,raw.raw.input,*result.custodyChange->companion,*hands.Current(InteractionHand::Right));
            if(!supported.holding)std::fprintf(stderr,"return support rejected: reason %u current %llu original %llu gun %llu, released %u%u/%u%u, now %lld deadline %lld\n",unsigned(supported.reason),sample.sequence,raw.raw.input.sequence,hands.Current(InteractionHand::Right)->inputSequence,sample.released[0],sample.released[1],raw.raw.input.released[0],raw.raw.input.released[1],sample.nowNs,raw.raw.input.deadlineNs);
        }else supported=proposed.Update(supportOwner,input,contact,false,result.tracking.custody==BoltCustodyPhase::Manipulating);
        if(supported.holding){const auto gun=hands.Current(InteractionHand::Right),left=hands.Current(InteractionHand::Left);
            if(!gun)return false;const HandContactProof proof{{2,s.item.id},raw.raw.input.sequence,raw.raw.input.deadlineNs,true};
            const auto claimed=left?hands.RenewFrom(sample,raw.raw.input,left->token,proof):hands.AcquireFrom(sample,raw.raw.input,
                {sample.owner,InteractionHand::Left,HandClaimKind::WeaponSupport,s.item,proof,++intent,gun->token.id});
            if(!claimed.accepted)return false;
        }
        support=proposed;probe->ObserveSupport(supported.holding,native.clock);
        if(result.tracking.weapon)weapon=result.tracking.weapon->weaponInWorld;
        else if(realPlacement&&transfers>=2){
            // Actual renderer recomputes ordinary attachment whenever this
            // frame has no current custody target. It never retains the last
            // placed matrix as the earlier synthetic fixture incorrectly did.
            weapon=Pose();weapon.values[3][0]=.365f;weapon.values[3][1]=-.2f;weapon.values[3][2]=.5f;
        }
        if(result.tracking.target){const auto plan=BuildBoltPartPlan(result.tracking,rig,weapon,1,native.clock);
            if(!plan||!BoltCustodyRetained(result.tracking,result.tracking,native.clock))return false;
            ++packs.poses;++packs.pairs;packs.copies+=2;}
        raw={};raw.raw=BuildBoltRawContact(result.tracking,rig,s.wristWorld[1],weapon,1,native.clock);
        raw.mappingValid=raw.raw.valid;raw.bodyWorldMeters=Pose();raw.weaponWorldMeters=weapon;
        raw.rawWristWorldMeters=s.wristWorld;raw.wristToGrip={Pose(),Pose()};raw.nativeWristInWeapon=refs;
        raw.nativePartValid=true;raw.nativePartInWeapon=calibration->profile.closedContact;
        previousInput=input;
        return !probe->Failed();
    }
};
int ActualControllerCustodyCycle(unsigned count,bool realPlacement=false,bool ordinary=false){
    ControllerFixture f(count,realPlacement,ordinary);for(unsigned n=0;n<2400&&!f.probe->Completed();++n){if(!f.Step()){
        std::ostringstream report;f.probe->Report(report);std::fprintf(stderr,"%s\nservice phase %u, failure %u\n",report.str().c_str(),unsigned(f.native.service.View(f.native.clock).phase),unsigned(f.native.service.View(f.native.clock).failure));return 1;}}
    CHECK(f.probe->Completed()&&f.shots==count&&f.releases==count&&f.acks==count&&f.transfers==count*2&&f.packs.pairs>0);
    CHECK(f.native.input.branches[2].loaded==5-int(count)&&f.native.input.branches[2].reserve==45);
    std::ofstream report(ordinary?"bolt-controller-ordinary-synthetic.json":count==1?"bolt-controller-synthetic1.json":"bolt-controller-synthetic2.json");f.probe->Report(report);
    return 0;
}
int PhysicalDiagnosticTimeoutIsFinite(){ControllerFixture f(1);for(unsigned n=0;n<2510&&!f.probe->Failed();++n)f.Step(true);
    CHECK(f.probe->Failed()&&!f.shots&&!f.releases&&!f.acks);return 0;}
int ControllerMappingRequiresOriginalRawPose(){
    ControllerFixture f(1);CHECK(f.Step()&&f.raw.raw.valid);
    const auto desired=f.raw.rawWristWorldMeters[1];
    CHECK(BoltProbeController(f.raw,f.input,InteractionHand::Right,desired));
    for(unsigned mutation=0;mutation<8;++mutation){auto raw=f.raw;auto source=f.input;auto target=desired;
        if(mutation==0)++source.generation;if(mutation==1)++raw.raw.nativeOwner.space;
        if(mutation==2)source.hands[1].grip.position.x+=.01f;
        if(mutation==3)raw.rawWristWorldMeters[1].values[3][0]+=.003f;
        if(mutation==4)raw.wristToGrip[1].values[3][0]=.0002f;
        if(mutation==5)raw.mappingValid=false;
        if(mutation==6)target.values[0][0]=2.f;
        if(mutation==7)target.values[3][0]=10.f;
        CHECK(!BoltProbeController(raw,source,InteractionHand::Right,target));
    }return 0;
}
}
int OrdinaryMissingReferencesNeverFires(){ControllerFixture f(1,false,true);f.missingPlayerReferences=true;
    for(unsigned n=0;n<2510&&!f.probe->Failed();++n)f.Step();
    CHECK(f.probe->Failed()&&!f.shots&&!f.releases&&!f.acks&&!f.transfers);return 0;}
int OrdinaryDuplicateCannotStartOrExtendPulse(){
    ControllerFixture f(1,false,true);
    for(unsigned n=0;n<1000&&f.probe->Current()!=Bc2BoltControllerProbe::Phase::Fire;++n)CHECK(f.Step());
    CHECK(f.probe->Current()==Bc2BoltControllerProbe::Phase::Fire&&!f.shots);
    auto in=f.input;auto ammo=Selection(f.native,Bc2NativeCycleMode::M95,1).reserve;
    f.probe->Prepare(in,f.native.input.identity.owner,"M95_sp",f.raw,f.native.service.View(f.native.clock),ammo,
        f.sample.observedNs,f.sample.deadlineNs,f.native.clock);
    CHECK(in.hands[1].trigger==0);CHECK(f.Step()&&f.shots==1);
    in=f.input;const auto later=f.sample.deadlineNs-1;
    f.probe->Prepare(in,f.native.input.identity.owner,"M95_sp",f.raw,f.native.service.View(later),ammo,
        f.sample.observedNs,f.sample.deadlineNs,later);
    CHECK(in.hands[1].trigger==0&&f.probe->NeedsNeutralFire(later));
    auto cached=f.actions.Update(in,{f.native.input.identity.owner.soldier,1,true,true},f.native.clock);
    if(f.probe->NeedsNeutralFire(later)){cached.held&=~Fire;cached.pressed&=~Fire;}
    CHECK(!(cached.held&Fire)&&!(cached.pressed&Fire)&&f.shots==1);return 0;
}
int StartupDrawUsesBodyPolicy(){
    body_probe_test::Fixture f;CHECK(f.Empty());Bc2BoltInputStartup startup;InputFrame input;
    input.focused=input.headValid=true;input.spaceGeneration=f.s.nativeOwner.space;
    for(auto& h:input.hands){h.active=Components;h.gripTracked=h.aimTracked=true;}
    input.hands[1].grip.position={.15f,-.1f,-.2f};bool pressed=false;unsigned draws=0;std::shared_ptr<BodyHolsterProbeSample> published;
    for(unsigned n=0;n<600&&!startup.Ready()&&!startup.Failed();++n){
        const auto prior=f.out.visibility;f.Advance();f.Render(prior);f.Suppress();
        auto body=std::make_shared<BodyHolsterProbeSample>();body->nativeOwner=f.s.nativeOwner;body->sampledNs=f.s.hand.nowNs;
        body->hand=f.s.hand;body->input=input;body->input.generation=f.s.hand.sequence;body->physicalGun=f.s.gun;
        body->nativeTick=f.s.nativeTick;body->selectedSlot=BodySlotAssignment{1,f.items[0].key};body->phase=f.adapter.Phase();
        body->right=f.hands.Current(InteractionHand::Right);body->left=f.hands.Current(InteractionHand::Left);
        body->ordinaryVisible=f.s.ordinary;body->request=f.adapter.RequestId();body->outcome=f.out;body->visibility=f.s.visibility;body->suppression=f.s.suppression;
        input.generation=f.s.hand.sequence;input.predictedNs=f.s.hand.nowNs;
        startup.Prepare(input,f.s.nativeOwner,published?published:body,f.s.hand.nowNs);
        const auto pose=BodyAnchorHandPose(input,InteractionHand::Right);const bool press=input.hands[1].squeeze>=.7f;
        if(press&&!pressed&&pose&&BodyAnchorContains(body->anchors.shoulders[0],*pose)){
            ++draws;f.s.body.intent={++f.intent,BodyInventoryOperation::Draw,1,f.items[0].key};}
        pressed=press;f.Tick();CHECK(input.hands[1].trigger==0);
        body->input=input;body->phase=f.adapter.Phase();body->right=f.hands.Current(InteractionHand::Right);
        body->outcome=f.out;body->visibility=f.s.visibility;body->suppression=f.s.suppression;body->request=f.adapter.RequestId();published=body;
        if(n==599)std::fprintf(stderr,"body phase=%u right=%llu current=%u held=%u blocks=%u auto=%u free=%u visibility=%u req=%llu show=%llu source=%llu current=%llu deadline=%lld now=%lld\n",unsigned(body->phase),body->right?body->right->token.id:0,Bc2BoltInputStartup::Current(*body,f.s.nativeOwner,f.s.hand.nowNs),Bc2BoltInputStartup::Held(*body,f.s.nativeOwner,f.s.hand.nowNs),body->outcome.blockWeaponActions,body->outcome.allowAutomaticGunHold,bool(body->outcome.freeRight),bool(body->visibility),body->request,body->visibility?body->visibility->request:0,body->right?body->right->inputSequence:0,body->hand.sequence,body->right?body->right->deadlineNs:0,body->hand.nowNs);
    }
    if(!startup.Ready())startup.Report(std::cerr);
    CHECK(startup.Ready()&&!startup.Failed()&&draws==1&&f.hands.Current(InteractionHand::Right));return 0;
}
int StartupNeverAcceptsSelectedItemAlone(){
    for(unsigned bad=0;bad<10;++bad){Bc2BoltInputStartup startup;ControllerFixture f(1);
        InputFrame in=f.input;in.generation=1;in.predictedNs=f.native.clock;
        auto body=std::make_shared<BodyHolsterProbeSample>();const auto owner=f.native.input.identity.owner;
        body->nativeOwner=owner;body->sampledNs=f.native.clock;body->hand=f.native.control.input;
        body->input=in;body->input.generation=body->hand.sequence;body->physicalGun=f.native.control.item;
        body->nativeTick=1;body->selectedSlot=BodySlotAssignment{2,{owner.weapon,1}};body->phase=BodyHolsterPhase::Held;
        HandClaim claim;claim.token={1,body->hand.owner,InteractionHand::Right,HandClaimKind::GunHold,body->physicalGun,{1,99},0};
        claim.inputSequence=body->hand.sequence;claim.deadlineNs=body->hand.deadlineNs;body->right=claim;
        body->ordinaryVisible=BodyVisibleRig{body->nativeOwner,body->hand.sequence,body->hand.observedNs,body->hand.deadlineNs};
            body->outcome.allowAutomaticGunHold=true;body->outcome.blockWeaponActions=false;
        CHECK(Bc2BoltInputStartup::Held(*body,owner,f.native.clock));
        if(bad==0)body->right.reset();if(bad==1)++body->nativeOwner.weapon;if(bad==2)body->hand.deadlineNs=f.native.clock;
        if(bad==3)body->outcome.blockWeaponActions=true;if(bad==4)++body->right->token.item.id;
        if(bad==5)body->selectedSlot.reset();if(bad==6)++body->right->inputSequence;if(bad==7)body->queuedTarget=owner.weapon;
        if(bad==8)body->ordinaryVisible.reset();if(bad==9)++body->ordinaryVisible->owner.weapon;
        CHECK(!Bc2BoltInputStartup::Held(*body,owner,f.native.clock));
    }return 0;
}
int main(){if(OrdinaryDuplicateCannotStartOrExtendPulse()||StartupDrawUsesBodyPolicy()||StartupNeverAcceptsSelectedItemAlone()||ActualControllerCustodyCycle(1,true,true)||ActualControllerCustodyCycle(2,true,true)||OrdinaryMissingReferencesNeverFires()||ActualControllerCustodyCycle(2,true)||ActualControllerCustodyCycle(1)||ActualControllerCustodyCycle(2)||PhysicalDiagnosticTimeoutIsFinite()||ControllerMappingRequiresOriginalRawPose())return 1;
    std::puts("11 controller bolt groups passed: ordinary input-only and finite paths, actual ControllerActions/custody/cycle/part plans/native service, synthetic native callbacks; finite timeout and 8 original-geometry mutations.");return 0;}
