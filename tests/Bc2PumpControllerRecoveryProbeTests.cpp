#define main CanonicalRecoveryServiceMain
#include "Bc2NativeCycleRecoveryServiceTests.cpp"
#undef main
#include "Bc2PumpControllerProbe.h"
#include "Bc2ResourceInventoryProbe.h"
#include "PhysicalPumpProbeInput.h"
#include <iostream>
#include <sstream>
using namespace fvr;using namespace fvr::bc2;using namespace fvr::interaction;
namespace {
math::Matrix4 Matrix(float x=0,float y=0,float z=0){auto m=reload_insertion_detail::Identity();m.values[3]={x,y,z,1};return m;}
// Synthetic original callbacks drive the ACTUAL native service. The input
// driver has no access to this API and never constructs its authorities.
struct RecoveryMachine {
    RecoveryFixture f;Bc2NativeCycleView view{};
    unsigned fired=0,released=0,acked=0,cancelled=0,normalReleased=0,normalAcked=0;bool wasFire=false;
    RecoveryMachine(){for(auto& b:f.input.branches){b.currentState=b.nextState=2;b.previousState=1;b.phaseTimer=0;b.loaded=8;}
        view=f.service.View(f.clock);}
    Bc2PhysicalPumpApi Api(){return {this,
        [](void* p,const Bc2NativeCycleControl& c)noexcept{auto& n=*static_cast<RecoveryMachine*>(p);n.f.control=c;
            const bool okay=n.f.service.Control(c);if(okay&&c.release)++n.normalReleased;return okay;},
        [](void* p,std::int64_t now)noexcept->std::optional<Bc2NativeCycleView>{return static_cast<RecoveryMachine*>(p)->f.service.View(now);},
        [](void* p,const WeaponCycleReady& ready)noexcept{auto& n=*static_cast<RecoveryMachine*>(p);const bool okay=n.f.service.AcknowledgeReady(ready);if(okay)++n.normalAcked;return okay;},
        [](void* p)noexcept{auto& n=*static_cast<RecoveryMachine*>(p);++n.cancelled;
            if(!n.f.service.BeginRecovery(n.f.clock)&&!n.f.service.SuspendCompletion())n.f.service.Cancel(Bc2NativeCycleFailure::Control);},
        {[](void* p,std::int64_t now)noexcept{return static_cast<RecoveryMachine*>(p)->f.service.RecoveryView(now);},
         [](void* p,const WeaponCycleIdleDebtRelease& release,const HandInteractionSample& input)noexcept{
            auto& n=*static_cast<RecoveryMachine*>(p);const bool okay=n.f.service.SubmitRecovery(release,input);if(okay)++n.released;return okay;},
         [](void* p,const WeaponCycleIdleDebtReady& ready)noexcept{auto& n=*static_cast<RecoveryMachine*>(p);
            const bool okay=n.f.service.AcknowledgeRecovery(ready,n.f.clock);if(okay)++n.acked;return okay;}}};}
    void Input(const ActionOutput& out,std::int64_t now){
        f.clock=now;const bool fire=out.active&&(out.held&Fire);
        if(fire&&!wasFire&&!f.service.View(now).blocksFire){++fired;
            for(auto& b:f.input.branches){b.loaded=8-int(fired);b.currentState=7;b.previousState=6;b.nextState=8;b.phaseTimer=.2f;}
            for(unsigned b=0;b<3;++b)f.Shot(b,9-int(fired));}
        wasFire=fire;
        if(f.service.Recovering()){for(auto& b:f.input.branches){b.currentState=b.nextState=2;b.previousState=1;b.phaseTimer=0;}
            for(unsigned b=0;b<3;++b)f.Idle(b);}
        else if(f.service.Phase()==Bc2NativeCyclePhase::Releasing){for(unsigned b=0;b<3;++b)f.Resume(b);}
        else for(unsigned b=0;b<3;++b){f.Begin(b);f.Finish();}
        view=f.service.View(f.clock);
    }
};
struct RecoveryDriverFixture {
    RigSnapshot rig;std::shared_ptr<Bc2PumpCalibration> calibration=std::make_shared<Bc2PumpCalibration>();
    RecoveryMachine native;HandInteraction hands;Bc2PhysicalPump pump;Bc2PumpControllerProbe driver;ControllerActions actions;SupportGrip support;unsigned supportReturns=0;bool publishSupport=true,forgeSupport=false;
    Bc2PhysicalPumpSample sample{};InputFrame input{};Bc2PumpRawContact raw{};Bc2PumpPackCounters packs{};
    std::uint64_t intent=0;std::int64_t now=1000000000;bool publishPairs=true,forgeRaw=false,mapBeforePrepare=false;
    unsigned lostPackets=0;std::int64_t firstLost=0,lastLost=0;
    unsigned corruption=0;bool hideRecovery=false,hideRecovered=false,duplicate=false,delayedNative=false;
    bool receiverNeutralDwell=false;
    ActionOutput mapped{};unsigned releasedDuringInitialSupport=0;bool supportStarted=false,normalMechanism=false;
    std::uint64_t gunAtLoss=0;bool retainedGun=true;
    static Bc2PumpCalibration Calibration(){Bc2PumpCalibration c;c.revision=1;c.asset=SpasReloadAsset;c.profileId=0x5350415350554d50ull;c.geometry=SpasPumpGeometry;c.closedPart=Matrix(0,0,-.84f);
        c.closedWrist=Matrix(-.2f,-.25f,.45f);c.rearDirection=1;c.measured=true;return c;}
    static RigSnapshot Rig(){RigSnapshot r;r.names={"root","jntWpn_1","jntWpn_4","LeftHand"};r.parents={-1,0,1,0};r.weaponBone=1;
        r.identity={0x11000,0x12000,0x30000,0x40000,0x50000,0x60000,0x70000,0x80000,0x90000,4,false};
        r.world.assign(4,Matrix());r.inverseBind.assign(4,Matrix());r.evaluatedWorld.assign(4,Matrix());r.nativeEvaluated.resize(4);
        for(auto& b:r.nativeEvaluated)b.fill(std::byte{0x5a});return r;}
    static Bc2PumpCalibration Bound(){auto c=Calibration();c.rigFingerprint=bc2_pump_detail::DerivePart(Rig())->fingerprint;return c;}
    explicit RecoveryDriverFixture(unsigned cycles=1,Bc2PumpControllerProbe::Scenario scenario=Bc2PumpControllerProbe::Scenario::LeftTrackingLoss):rig(Rig()),calibration(std::make_shared<Bc2PumpCalibration>(Bound())),pump(calibration,native.Api()),driver(*calibration,cycles,scenario){
        sample.nativeOwner=native.f.input.identity.owner;sample.item=native.f.control.item;sample.asset=SpasReloadAsset;
        sample.input.owner=native.f.control.input.owner;
        input.spaceGeneration=sample.nativeOwner.space;input.focused=input.headValid=true;
        for(auto& h:input.hands){h.active=Components;h.gripTracked=h.aimTracked=true;}
    }
    void Tick(){
        now=std::max(now,native.f.clock)+10000000;
        if(!delayedNative){native.Input(mapped,now);now=native.f.clock;}
        ++input.generation;input.predictedNs=now;const auto observedAt=now;
        input.hands[0].gripTracked=input.hands[0].aimTracked=true;
        probe::PhysicalPumpInput(input,0,false);
        auto observed=raw;if(forgeRaw)++observed.input.observedNs;
        const InputOwner owner{sample.nativeOwner.soldier,sample.nativeOwner.equipGeneration,true,true};
        if(mapBeforePrepare)actions.Update(input,owner,now);
        if(receiverNeutralDwell){input.hands[0].squeeze=input.hands[1].squeeze=0;}
        else driver.Prepare(input,sample.nativeOwner,sample.asset,observed,native.view,observedAt,observedAt+100000000,now);
        if(duplicate){auto repeated=input;repeated.hands[0].gripTracked=repeated.hands[0].aimTracked=true;
            repeated.hands[0].squeeze=0;driver.Prepare(repeated,sample.nativeOwner,sample.asset,observed,native.view,
                observedAt,observedAt+100000000,now);
            if(repeated.hands[0].gripTracked!=input.hands[0].gripTracked||repeated.hands[0].squeeze!=input.hands[0].squeeze)
                retainedGun=false;}
        if(!input.hands[0].gripTracked){++lostPackets;if(!firstLost)firstLost=now;lastLost=now;}
        // Same mapper as Gather: native Fire must come from the prepared packet,
        // never directly from the fixture's raw trigger field.
        auto safeOwner=owner;safeOwner.playing=!driver.Failed();
        mapped=actions.Update(input,safeOwner,now);
        if(delayedNative){native.Input(mapped,now);now=native.f.clock;}
        auto& s=sample.input;s.sequence=input.generation;s.observedNs=observedAt;s.nowNs=now;s.deadlineNs=observedAt+100000000;
        s.focused=true;s.tracked={input.hands[0].gripTracked,input.hands[1].gripTracked};s.released={input.hands[0].squeeze<=.35f,false};
        hands.Update(s);const auto current=hands.Current(InteractionHand::Right);
        if(current)hands.Renew(s,current->token,{{1,4},s.sequence,s.deadlineNs,true});
        else hands.Acquire(s,{s.owner,InteractionHand::Right,HandClaimKind::GunHold,sample.item,{{1,4},s.sequence,s.deadlineNs,true},++intent,0});
        sample.raw=raw;sample.grip=input.hands[0].squeeze>.35f;sample.cancel=driver.Failed();
        auto out=pump.Tick(sample,hands,intent);native.view=native.f.service.View(now);
        if(lostPackets){const auto gun=hands.Current(InteractionHand::Right);
            if(!gunAtLoss&&gun)gunAtLoss=gun->token.id;
            if(!gun||gun->token.id!=gunAtLoss)retainedGun=false;}
        const SupportGripOwner supportOwner{s.owner.actor,s.owner.actorGeneration,sample.nativeOwner.weapon};
        const SupportGripContact contact{true,.01f,{}};
        auto proposed=support;
        auto returned=pump.ContinueSupport(supportOwner,input,contact,sample.raw.input,{2,sample.nativeOwner.weapon},hands,proposed,intent,sample.cancel||out.ownsHand);
        auto supported=returned?*returned:proposed.Update(supportOwner,input,contact,sample.cancel,out.ownsHand);
        if(returned)++supportReturns;
        if(supported.holding){
            const auto left=hands.Current(InteractionHand::Left),gun=hands.Current(InteractionHand::Right);
            const HandContactProof proof{{2,sample.nativeOwner.weapon},sample.raw.input.sequence,sample.raw.input.deadlineNs,true};
            bool owned=false;
            if(left&&left->token.kind==HandClaimKind::WeaponSupport)
                owned=hands.RenewFrom(s,sample.raw.input,left->token,proof).accepted;
            else if(supported.engaged&&gun){
                const HandClaimRequest request{s.owner,InteractionHand::Left,HandClaimKind::WeaponSupport,sample.item,proof,++intent,gun->token.id};
                owned=(left?hands.TransferFrom(s,sample.raw.input,left->token,request):hands.AcquireFrom(s,sample.raw.input,request)).accepted;
            }
            if(!owned){supported=support.Update(supportOwner,input,contact,true);proposed=support;
                const auto claim=hands.Current(InteractionHand::Left);
                if(claim&&claim->token.kind==HandClaimKind::WeaponSupport)hands.Release(s,claim->token);}
        }else {const auto claim=hands.Current(InteractionHand::Left);
            if(claim&&claim->token.kind==HandClaimKind::WeaponSupport)hands.Release(s,claim->token);}
        support=proposed;
        if(driver.Current()==Bc2PumpControllerProbe::Phase::SupportBeforeFire&&supported.holding)supportStarted=true;
        if(supportStarted&&!normalMechanism&&input.hands[0].squeeze<=.35f)++releasedDuringInitialSupport;
        if(out.tracking.target)normalMechanism=true;
        driver.ObserveHeldSupport(supported,s,hands.Current(InteractionHand::Left),hands.Current(InteractionHand::Right),now);
        pump.BindSupport(out.tracking,supported,hands);
        if((out.tracking.target||out.tracking.recoveryTarget)&&publishPairs){const auto presentation=BuildPumpPresentation(out.tracking,rig,Matrix(),1,now);
            if(presentation.wrist&&presentation.part){++packs.poses;packs.copies+=2;++packs.pairs;}}
        auto reported=out;
        if(hideRecovery)reported.tracking.recovery.reset();
        if(hideRecovered)reported.recovered.reset();
        if(corruption&&reported.tracking.recovery){
            if(corruption==1)++reported.tracking.recovery->grant.debt.loaded;
            if(corruption==2)++reported.tracking.recovery->item.id;
            if(corruption==3)reported.tracking.recovery->grant.neutral.sequence=1;
            if(corruption==4&&reported.tracking.gun)++reported.tracking.gun->token.id;
        }
        driver.Observe(reported,native.view,packs,now);
        auto observedSupport=out.tracking;if(forgeSupport&&observedSupport.supportCapture)++observedSupport.supportCapture->support.id;
        if(publishSupport)driver.ObserveSupport(observedSupport,supported,now);
        const auto controller=InverseRigid(*math::MakeLhViewFromOpenXRPose(input.hands[0].grip));
        const auto body=Matrix();raw=BuildPumpRawContact(out.tracking,rig,*controller,Matrix(),{.02f,0,0},1,now,&body);
        if(raw.valid)++packs.contacts;
    }
};

int WholeReceiverGapKeepsDebtAndRequiresFreshStroke(bool corruptNeutral=false){
    RecoveryDriverFixture f(3,Bc2PumpControllerProbe::Scenario::HeldSupportNaturalReceiverGap);
    for(unsigned n=0;n<1800&&!f.driver.ReceiverGapReady(f.now)&&!f.driver.Failed();++n)f.Tick();
    const auto witness=f.driver.ReceiverGapReady(f.now);CHECK(witness&&f.native.fired==1&&!f.lostPackets);
    const auto old=f.native.f.control;const auto oldView=f.native.view;
    PumpReceiverGapProbe receiver;CHECK(receiver.Begin(*witness,f.now));
    bool expired=false;unsigned nativeSamples=0;
    while(receiver.Withhold(f.now)){
        f.now=std::max(f.now,f.native.f.clock)+10000000;
        f.native.Input({},f.now);f.now=f.native.f.clock;++nativeSamples;
        if(f.now>=old.input.deadlineNs){
            // Real physical cancellation invokes the same existing guarded
            // native adapter as gameplay loss; the diagnostic never owns it.
            f.sample.input.nowNs=f.now;f.hands.Reset();f.support.Reset();f.raw={};f.mapped={};
            if(!expired){const auto out=f.pump.Tick(f.sample,f.hands,f.intent);CHECK(out.blocksFire&&!out.ownsHand);expired=true;}
            CHECK(f.driver.ObserveMissingReceiver(f.now));
            f.driver.ObserveMissingReceiverNative(f.native.f.service.View(f.now),true,f.now);
            CHECK(f.native.f.service.Recovering());
        }
        const auto current=f.native.f.service.View(f.now);
        CHECK(current.cycle==oldView.cycle&&current.shot==oldView.shot&&current.loaded==7&&current.reserve==24&&current.blocksFire);
        CHECK(f.native.f.control.input.sequence==old.input.sequence&&f.native.f.control.input.observedNs==old.input.observedNs&&
              f.native.f.control.input.deadlineNs==old.input.deadlineNs&&f.native.f.control.input.released==old.input.released);
        CHECK(!f.native.released&&!f.native.acked&&!f.native.normalReleased&&f.native.fired==1&&!f.driver.Failed());
    }
    CHECK(expired);CHECK(nativeSamples>300);CHECK(f.now-old.input.deadlineNs>=5000000000ll);
    CHECK(!f.native.f.service.RecoveryView(f.now)->authority);
    auto c=RecenterInput(f.native.f);c.input.sequence=++f.input.generation;
    f.now=f.native.f.clock;f.input.spaceGeneration=c.nativeOwner.space;
    f.input.hands[0].squeeze=f.input.hands[1].squeeze=0;
    const auto returnedInput=f.input.generation;
    const auto returnedObserved=c.input.observedNs,returnedDeadline=c.input.deadlineNs;
#ifndef FVR_TEST_ORIGINAL_DRIVER
    // First new-space input is historical evidence, not a replacement lease.
    for(unsigned fault=0;fault<7;++fault){auto in=f.input;auto owner=c.nativeOwner;
        auto observed=c.input.observedNs,deadline=c.input.deadlineNs;
        if(fault==0){--owner.space;--in.spaceGeneration;}
        if(fault==1)in.generation=witness->input;
        if(fault==2){observed=old.input.observedNs;deadline=old.input.deadlineNs;}
        if(fault==3)observed=f.now+1;
        if(fault==4)deadline=f.now;
        if(fault==5)in.hands[0].squeeze=1;
        if(fault==6)in.hands[1].squeeze=1;
        CHECK(!f.driver.ObserveReceiverReturnNeutral(in,owner,observed,deadline,f.now));
    }
    CHECK(f.driver.ObserveReceiverReturnNeutral(f.input,c.nativeOwner,c.input.observedNs,c.input.deadlineNs,f.now));
#endif
    CHECK(f.native.f.service.RebaseTracking(c,DrainedAt(f.native.f)));
    f.native.f.control=c;f.native.f.input.identity.owner=c.nativeOwner;
    f.sample.nativeOwner=c.nativeOwner;f.sample.input.owner=c.input.owner;
    // Reproduce actual396: native own Updates and ordinary physical consumers
    // run during the outer driver's200ms neutral dwell BEFORE its handoff.
    f.receiverNeutralDwell=true;
    for(unsigned n=0;n<22;++n){f.Tick();CHECK(f.native.fired==1&&!f.native.released&&!f.native.acked);
#ifndef FVR_TEST_ORIGINAL_DRIVER
        CHECK(f.driver.ObserveReceiverReturnNeutral(f.input,c.nativeOwner,f.sample.input.observedNs,f.sample.input.deadlineNs,f.now));
#endif
    }
    const auto grantBefore=f.native.f.service.RecoveryView(f.now);
    CHECK(grantBefore&&grantBefore->authority);
    CHECK(grantBefore->authority->grant.neutral.sequence>=returnedInput&&
          grantBefore->authority->grant.neutral.sequence<f.input.generation);
    const auto originalNeutral=grantBefore->authority->grant.neutral;
    CHECK(originalNeutral.deadlineNs<f.now); // Source expires; guarded native lease remains current.
    CHECK(weapon_cycle_detail::Lease(*grantBefore->authority,f.now));
    f.receiverNeutralDwell=false;f.input.hands[1].squeeze=1;
    f.native.view=f.native.f.service.View(f.now);
    CHECK(f.driver.FollowTrackingRebase(f.input,c.nativeOwner,f.native.view,f.now));
#ifndef FVR_TEST_ORIGINAL_DRIVER
    std::ostringstream boundary;f.driver.Report(boundary);
    CHECK(boundary.str().find("\"neutral_sequence\":"+std::to_string(returnedInput))!=std::string::npos);
    CHECK(boundary.str().find("\"observed_ns\":"+std::to_string(returnedObserved))!=std::string::npos);
    CHECK(boundary.str().find("\"deadline_ns\":"+std::to_string(returnedDeadline))!=std::string::npos);
    const auto retainedNeutral=f.native.f.service.RecoveryView(f.now)->authority->grant.neutral;
    CHECK(retainedNeutral.owner==originalNeutral.owner&&retainedNeutral.sequence==originalNeutral.sequence&&
          retainedNeutral.observedNs==originalNeutral.observedNs&&retainedNeutral.deadlineNs==originalNeutral.deadlineNs&&
          retainedNeutral.nowNs==originalNeutral.nowNs&&retainedNeutral.released==originalNeutral.released);
#endif
    CHECK(f.native.view.blocksFire&&f.native.fired==1&&!f.native.released);
    if(corruptNeutral)f.corruption=3; // A forged pre-return grant remains rejected.
    for(unsigned n=0;n<2200&&!f.driver.Completed()&&!f.driver.Failed();++n){
        if(!f.native.acked)CHECK(f.native.fired==1);
        f.Tick();
    }
    if(corruptNeutral){
        CHECK(f.driver.Failed()&&f.native.fired==1&&!f.native.released&&!f.native.acked&&f.native.view.blocksFire);
        std::ostringstream report;f.driver.Report(report);
        CHECK(report.str().find("\"failure_name\":\"recovery_authority\"")!=std::string::npos);
#ifndef FVR_TEST_ORIGINAL_DRIVER
        const auto key=report.str().find("\"predicates\":");CHECK(key!=std::string::npos);
        CHECK((std::stoul(report.str().substr(key+13))&16)!=0);
#endif
        return 0;
    }
    if(f.driver.Failed()){f.driver.Report(std::cerr);f.pump.Report(std::cerr);}
    CHECK(f.driver.Completed()&&f.native.fired==3&&f.native.released==1&&f.native.acked==1);
    CHECK(f.native.normalReleased==2&&f.native.normalAcked==2&&f.supportReturns==3);
    CHECK(f.native.view.loaded==5&&f.native.view.reserve==24&&!f.native.view.blocksFire);
    return 0;
}

int OriginReceiptResumesInputOnlyFullStroke(){
    RecoveryDriverFixture f(1,Bc2PumpControllerProbe::Scenario::HeldSupportNaturalTrackingLoss);
    for(unsigned n=0;n<1800&&!f.lostPackets&&!f.driver.Failed();++n)f.Tick();
    CHECK(f.lostPackets&&f.native.f.service.Recovering());
    auto c=RecenterInput(f.native.f);c.input.sequence=++f.input.generation;
    f.now=f.native.f.clock;f.input.spaceGeneration=c.nativeOwner.space;
    f.input.hands[0].gripTracked=f.input.hands[0].aimTracked=true;
    f.input.hands[0].squeeze=0;f.input.hands[1].squeeze=1;
    CHECK(f.native.f.service.RebaseTracking(c,DrainedAt(f.native.f)));
    f.native.f.control=c;f.native.f.input.identity.owner=c.nativeOwner;
    f.sample.nativeOwner=c.nativeOwner;f.sample.input.owner=c.input.owner;
    f.native.view=f.native.f.service.View(f.now);
    auto wrong=f.native.view;++wrong.trackingRebase->cycle;
    CHECK(!f.driver.FollowTrackingRebase(f.input,c.nativeOwner,wrong,f.now));
    CHECK(f.driver.FollowTrackingRebase(f.input,c.nativeOwner,f.native.view,f.now));
    CHECK(!f.driver.FollowTrackingRebase(f.input,c.nativeOwner,f.native.view,f.now));
    f.hands.Reset();f.support.Reset();f.raw={};
    for(unsigned n=0;n<2200&&!f.driver.Completed()&&!f.driver.Failed();++n)f.Tick();
    if(f.driver.Failed()){f.driver.Report(std::cerr);f.pump.Report(std::cerr);}
    CHECK(f.driver.Completed()&&f.native.fired==1&&f.native.released==1&&f.native.acked==1&&f.supportReturns==1);
    CHECK(f.native.view.loaded==7&&f.native.view.reserve==24&&!f.native.view.blocksFire);
    std::ostringstream report;f.driver.Report(report);CHECK(report.str().find("\"followed_tracking_rebase\":true")!=std::string::npos);
    return 0;
}
int InputOnlyRecoveryCompletesAndReturnsSupport(){
    for(bool duplicate:{false,true})for(bool held:{false,true}){RecoveryDriverFixture f(1,held?
        Bc2PumpControllerProbe::Scenario::HeldSupportTrackingLoss:Bc2PumpControllerProbe::Scenario::LeftTrackingLoss);f.duplicate=duplicate;
        for(unsigned n=0;n<2600&&!f.driver.Completed()&&!f.driver.Failed();++n)f.Tick();
        if(f.driver.Failed()){f.driver.Report(std::cerr);std::cerr<<"\n";f.pump.Report(std::cerr);const auto v=f.native.f.service.RecoveryView(f.now);std::cerr<<" recoveryView="<<bool(v)<<" authority="<<bool(v&&v->authority)<<" guarded="<<f.native.f.service.RecoveryUpdates()<<" phase="<<unsigned(f.native.f.service.Phase())<<"\n";}
        CHECK(f.driver.Completed()&&f.native.fired==1&&f.native.released==1&&f.native.acked==1);
        CHECK(f.native.cancelled&&f.lostPackets>=30&&f.lastLost-f.firstLost>=480000000&&f.retainedGun&&f.supportReturns==1);
        CHECK(f.native.view.loaded==7&&f.native.view.reserve==24&&!f.native.view.blocksFire);
        CHECK(f.native.f.service.RecoveryUpdates()>100&&f.packs.pairs>50);
        if(held)CHECK(f.supportStarted&&f.normalMechanism&&!f.releasedDuringInitialSupport);
        std::ostringstream report;f.driver.Report(report);
        CHECK(report.str().find("\"recovery\":true")!=std::string::npos);
        CHECK(report.str().find("\"support_returned\":true")!=std::string::npos);
        if(!duplicate)std::cout<<report.str()<<'\n';
        for(unsigned n=0;n<40;++n)f.Tick();CHECK(f.native.fired==1&&f.native.released==1&&f.native.acked==1);
    }return 0;
}
int HeldSupportShotAdoptsWithoutNeutral(){RecoveryDriverFixture f(1,Bc2PumpControllerProbe::Scenario::HeldSupport);
    for(unsigned n=0;n<2600&&!f.driver.Failed()&&!f.driver.Completed();++n)f.Tick();
    if(f.driver.Failed()){f.driver.Report(std::cerr);std::cerr<<'\n';}
    CHECK(f.driver.Completed()&&f.supportStarted&&f.normalMechanism&&!f.releasedDuringInitialSupport);
    CHECK(f.native.fired==1&&f.native.normalReleased==1&&f.native.normalAcked==1&&f.native.released==0&&f.native.acked==0);
    CHECK(f.lostPackets==0&&f.supportReturns==1);return 0;
}
int NativePauseNeedsRealRegripAndNewStroke(){
    RecoveryDriverFixture f(1,Bc2PumpControllerProbe::Scenario::HeldSupport);
    for(unsigned n=0;n<2000&&f.driver.Current()!=Bc2PumpControllerProbe::Phase::Rear&&!f.driver.Failed();++n)f.Tick();
    CHECK(f.driver.Current()==Bc2PumpControllerProbe::Phase::Rear&&f.native.view.held);
    // Reproduce the native-first resume after a controller publication gap.
    // No synthetic release, authority or native cancellation is injected.
    f.now=f.native.f.control.input.deadlineNs+41000000;
    f.Tick();CHECK(f.native.f.service.Recovering());
    for(unsigned n=0;n<2600&&!f.driver.Completed()&&!f.driver.Failed();++n)f.Tick();
    if(f.driver.Failed()){f.driver.Report(std::cerr);f.pump.Report(std::cerr);}
    CHECK(f.driver.Completed()&&f.native.fired==1&&f.native.released==1&&f.native.acked==1);
    CHECK(!f.native.normalReleased&&!f.lostPackets&&f.supportReturns==1);
    CHECK(f.native.view.loaded==7&&f.native.view.reserve==24&&!f.native.view.blocksFire);
    std::ostringstream report;f.driver.Report(report);
    CHECK(report.str().find("\"native_pause_regrips\":1")!=std::string::npos);
    return 0;
}
int DelayedNativeIdleMustNotStarveOriginalNeutral(){RecoveryDriverFixture f;f.delayedNative=true;
    for(unsigned n=0;n<2600&&!f.driver.Failed()&&!f.driver.Completed();++n)f.Tick();
    if(f.driver.Failed()){f.driver.Report(std::cerr);std::cerr<<'\n';}
    CHECK(f.driver.Completed()&&f.native.released==1&&f.native.acked==1);return 0;
}
int FreshTypedRecoveryProofRequired(){
    for(unsigned bad=0;bad<7;++bad){RecoveryDriverFixture f;
        if(bad==0)f.hideRecovery=true;if(bad==1)f.hideRecovered=true;
        if(bad>=2&&bad<=5)f.corruption=bad-1;if(bad==6)f.publishPairs=false;
        for(unsigned n=0;n<2600&&!f.driver.Failed()&&!f.driver.Completed();++n)f.Tick();
        CHECK(f.driver.Failed()&&!f.driver.Completed()&&f.native.fired==1);
        if(bad!=1)CHECK(f.native.acked==0);
    }return 0;
}
int RecoveryStillRequiresObservedSupport(){RecoveryDriverFixture f;f.publishSupport=false;
    for(unsigned n=0;n<2600&&!f.driver.Failed()&&!f.driver.Completed();++n)f.Tick();
    CHECK(f.driver.Failed()&&!f.driver.Completed()&&f.native.acked==1&&f.supportReturns==1);return 0;}
int RecoveryScenarioIsExplicitAndBounded(){
    for(unsigned count:{2u,8u}){RecoveryDriverFixture f(count);f.Tick();CHECK(f.driver.Failed()&&f.native.fired==0);}
    Bc2ResourceInventoryProbe probe;CHECK(!probe.EnablePump(RecoveryDriverFixture::Bound(),Bc2PumpControllerProbe::Scenario(99)));
    CHECK(probe.EnablePump(RecoveryDriverFixture::Bound(),Bc2PumpControllerProbe::Scenario::LeftTrackingLoss));
    CHECK(!probe.EnablePump(RecoveryDriverFixture::Bound()));return 0;
}
int NaturalRecoveryStrokeUsesContinuousCrossings(){
    RecoveryDriverFixture f(1,Bc2PumpControllerProbe::Scenario::HeldSupportNaturalTrackingLoss);
    for(unsigned n=0;n<2600&&!f.driver.Failed()&&!f.driver.Completed();++n)f.Tick();
    CHECK(f.driver.Completed()&&!f.driver.Failed());CHECK(f.native.released==1&&f.native.acked==1);
    CHECK(f.supportReturns==1&&f.normalMechanism);return 0;
}
int PreShotInputGapNeedsFreshNeutralAndRegrip(){
    RecoveryDriverFixture f(1,Bc2PumpControllerProbe::Scenario::HeldSupportNaturalTrackingLoss);
    for(unsigned n=0;n<900&&!f.supportStarted&&!f.driver.Failed();++n)f.Tick();
    CHECK(f.supportStarted&&f.native.fired==0);
    for(unsigned n=0;n<30;++n)f.Tick();
    CHECK(f.driver.Current()==Bc2PumpControllerProbe::Phase::SupportBeforeFire);
    const auto oldSupport=f.hands.Current(InteractionHand::Left);CHECK(oldSupport);
    // Recorded271 receiver gap:125ms exceeds the100ms source lease. Gameplay
    // clears support/hand claims and skips the driver until fresh input returns.
    // Do not invent a controller release or alter ammunition/native cycle state.
    f.now+=125000000;f.hands.Reset();f.support.Reset();f.raw={};f.mapped={};
    for(unsigned n=0;n<2600&&!f.driver.Failed()&&!f.driver.Completed();++n)f.Tick();
    if(f.driver.Failed()){f.driver.Report(std::cerr);std::cerr<<'\n';}
    CHECK(f.driver.Completed()&&f.native.fired==1&&f.native.released==1&&f.native.acked==1);
    CHECK(f.releasedDuringInitialSupport>0&&f.supportReturns==1);
    CHECK(f.native.view.loaded==7&&f.native.view.reserve==24&&!f.native.view.blocksFire);
    std::ostringstream report;f.driver.Report(report);
    CHECK(report.str().find("\"pre_shot_regrips\":1")!=std::string::npos);
    return 0;
}
int PreShotRegripIsBoundedAndCannotEraseDebt(){
    for(unsigned fault=0;fault<6;++fault){
        RecoveryDriverFixture f(1,Bc2PumpControllerProbe::Scenario::HeldSupportNaturalTrackingLoss);
        for(unsigned n=0;n<900&&!f.supportStarted&&!f.driver.Failed();++n)f.Tick();
        CHECK(f.supportStarted&&f.native.fired==0);
        const auto first=f.hands.Current(InteractionHand::Left);CHECK(first);
        f.now+=125000000;f.hands.Reset();f.support.Reset();f.raw={};f.mapped={};f.Tick();
        if(fault){
            auto native=f.native.view;
            if(fault==1)native.blocksFire=true;
            if(fault==2)++native.cycle;
            if(fault==3)++native.shot;
            if(fault==4)native.phase=Bc2NativeCyclePhase::Held;
            if(fault==5)++native.native.owner.weapon;
            auto input=f.input;++input.generation;input.predictedNs=f.now+10000000;
            f.driver.Prepare(input,f.sample.nativeOwner,f.sample.asset,f.raw,native,
                input.predictedNs,input.predictedNs+100000000,input.predictedNs);
        }else {
            bool regripped=false;
            for(unsigned n=0;n<600&&!f.driver.Failed();++n){f.Tick();
                const auto left=f.hands.Current(InteractionHand::Left);
                if(left&&left->token.kind==HandClaimKind::WeaponSupport&&left->token.id!=first->token.id){regripped=true;break;}}
            CHECK(regripped&&f.native.fired==0);
            f.now+=125000000;f.hands.Reset();f.support.Reset();f.raw={};f.mapped={};
            for(unsigned n=0;n<600&&!f.driver.Failed();++n)f.Tick();
        }
        CHECK(f.driver.Failed()&&!f.driver.Completed()&&f.native.fired==0&&f.native.released==0&&f.native.acked==0);
        std::ostringstream report;f.driver.Report(report);
        CHECK(report.str().find("\"failure_name\":\"pre_shot_regrip_rejected\"")!=std::string::npos);
    }
    return 0;
}
}
int main(int argc,char** argv){
    if(argc==2&&std::string(argv[1])=="--receiver-return-neutral"){
        CHECK(!WholeReceiverGapKeepsDebtAndRequiresFreshStroke());
        CHECK(!WholeReceiverGapKeepsDebtAndRequiresFreshStroke(true));
        std::puts("Receiver return neutral PASS: original fresh source retained across200ms observer delay; complete fresh stroke,3shots, no debt or lease restamping.");return 0;
    }
    CHECK(!WholeReceiverGapKeepsDebtAndRequiresFreshStroke());CHECK(!WholeReceiverGapKeepsDebtAndRequiresFreshStroke(true));CHECK(!NativePauseNeedsRealRegripAndNewStroke());CHECK(!OriginReceiptResumesInputOnlyFullStroke());CHECK(!InputOnlyRecoveryCompletesAndReturnsSupport());CHECK(!FreshTypedRecoveryProofRequired());
    CHECK(!RecoveryStillRequiresObservedSupport());CHECK(!RecoveryScenarioIsExplicitAndBounded());
    CHECK(!HeldSupportShotAdoptsWithoutNeutral());CHECK(!DelayedNativeIdleMustNotStarveOriginalNeutral());
    CHECK(!NaturalRecoveryStrokeUsesContinuousCrossings());
    CHECK(!PreShotInputGapNeedsFreshNeutralAndRegrip());
    CHECK(!PreShotRegripIsBoundedAndCannotEraseDebt());
    std::puts("12 recovery input-driver groups PASS; actual ControllerActions, HandInteraction, native service, physical consumer and support; synthetic original callbacks and paired palette counts.");return 0;}
