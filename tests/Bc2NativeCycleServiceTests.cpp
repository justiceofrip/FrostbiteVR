#include "Bc2NativeCycleService.h"
#include "Bc2NativeCycleSelectionTracker.h"
#include "Bc2NativeCycleCallbacks.h"
#include "Bc2NativeCycleDispatch.h"
#include "Bc2MagazineEmptyControl.h"
#include "Bc2ReloadInvocationEntry.h"
#include "Bc2M95StockShotConfig.h"
#include "Bc2M95PhysicalBoltDiagnostic.h"
#include "Bc2NativeCycleEvidence.h"
#include "M95ForwardRestore239Fixture.h"
#include "SpasForwardRestore287Fixture.h"
#include "SpasForwardRestore289Fixture.h"
#include "M95SnapshotProgression241Fixture.h"
#include "SpasSnapshotProgression251Fixture.h"
#include "M95PredictionReplay253Fixture.h"
#include "M95TailReplay255Fixture.h"
#include "M95Empty281Fixture.h"
#include "M95EmptyReplay292Fixture.h"
#include "Bc2PhysicalPump.h"
#include "fvr/interaction/MechanismAudio.h"
#include <thread>
#include <barrier>
#include "Bc2ReloadConfigDescriptor.h"
#include "Test.h"
#include <bit>
#include <cstring>
#include <cstdio>
using namespace fvr::bc2;using namespace fvr::interaction;
namespace {
template<std::size_t N,class T>void Put(std::array<std::byte,N>& bytes,unsigned at,T value){std::memcpy(bytes.data()+at,&value,sizeof(value));}
fvr::math::Matrix4 Pose(){fvr::math::Matrix4 m{};for(unsigned n=0;n<4;++n)m.values[n][n]=1;return m;}
struct Fixture {
    Bc2NativeCycleService service;Bc2NativeCycleControl control{};ReloadHoldInput input{};
    HandInteraction hands;PhysicalWeaponCycle physical;WeaponCycleProfile profile{};HandClaimToken gun{};
    std::uint64_t event=0,intent=0;std::int64_t clock=1000000000;
    Bc2NativeCycleDecision decision{};ReloadFlowRecord update{};unsigned sent=0;bool begun=false;
    Fixture(bool m95=false){
        auto& n=input.identity;n.owner={0x10000,0x11000,0x12000,0x13000,11,12,13};n.firing={0x14000,0x15000,0x16000};
        n.serverPlayer=0x17000;n.serverSoldier=0x18000;n.serverItem=0x19000;
        auto& c=input.config;const auto& d=SpasReloadDescriptor;
        std::memcpy(c.assetName.data(),d.assetName.data(),d.assetName.size());std::memcpy(c.assetPath.data(),d.assetPath.data(),d.assetPath.size());
        c.weaponData=0x20000;c.firingData=0x21000;c.primaryFire=0x22000;c.ammoAddress=c.primaryFire+0x170;
        c.fireLogicType=1;c.reloadType=0;c.fireInputAction=8;c.reloadInputAction=29;c.baseCapacity=c.numberOfMagazines=4;
        c.reloadDelay=.06f;c.reloadTime=.72f;c.reloadThreshold=c.postReloadTime=1;c.boltDelay=.5f;
        control.nativeOwner=n.owner;control.input.owner={(std::uint64_t(n.owner.weak)<<32)|n.owner.soldier,n.owner.actorGeneration,99,n.owner.space};
        control.item={n.owner.weapon,99};control.mechanism={0x50554d50,99};control.permitted=true;
        input.verified=true;input.context.deltaSeconds=1.f/60;input.context.reloadTimeMultiplier=1;input.context.flags24Through28[0]=true;
        input.capacities={8,8,8};
        for(unsigned branch=0;branch<3;++branch){auto& b=input.branches[branch];b.address=n.firing[branch];b.wrapperOffset=branch==0?0x3c:branch==1?0x40:0x10;
            b.currentState=7;b.previousState=6;b.nextState=8;b.phaseTimer=.2f;b.loaded=7;b.reserve=24;}
        profile.id=1;profile.revision=1;profile.closedContact=Pose();profile.axis={0,0,-1};profile.stroke=.09495844f;
        profile.rearTolerance=profile.frontTolerance=.003f;profile.contactRadius=.06f;profile.lateralTolerance=.03f;
        profile.maxStepMeters=.03f;profile.rotationTolerance=.15f;profile.endpointDwellNs=10000000;profile.maximumCycleNs=2000000000;
        if(m95){
            service.Configure(Bc2NativeCycleMode::M95);c.assetName={};c.assetPath={};
            const char name[]="M95_sp",path[]="Objects/Weapons/Handheld/BU_sni_M95/SP_sni_M95";
            std::memcpy(c.assetName.data(),name,sizeof(name));std::memcpy(c.assetPath.data(),path,sizeof(path));
            c.reloadType=1;c.baseCapacity=c.numberOfMagazines=5;c.reloadDelay=0;c.reloadTime=6.9f;c.reloadThreshold=.67f;
            c.postReloadTime=c.boltDelay=0;c.boltTime=2.3f;c.holdBoltUntilFireRelease=true;c.holdBoltUntilZoomRelease=false;
            input.capacities={5,5,5};for(auto& b:input.branches){b.currentState=8;b.previousState=7;b.nextState=1;b.phaseTimer=2.2f;b.loaded=4;b.reserve=45;}
        }
        Advance();
    }
    bool Advance(bool grip=true){
        clock+=10000000;auto& s=control.input;++s.sequence;s.observedNs=s.nowNs=clock;s.deadlineNs=clock+100000000;
        s.focused=true;s.tracked={true,true};s.released={false,false};s.released[0]=!grip;control.release.reset();return service.Control(control);
    }
    ReloadFlowBoundary Boundary(unsigned branch)const {
        ReloadFlowBoundary b;const auto& source=input.branches[branch];b.owner=input.identity.owner;b.snapshotSequence=20;
        b.firing=source.address;b.wrapperOffset=source.wrapperOffset;b.branch=std::uint8_t(branch);b.current=source.currentState;b.previous=source.previousState;
        b.next=source.nextState;b.timer=source.phaseTimer;b.loaded=source.loaded;b.reserve=source.reserve;
        if(branch==2){b.serverPlayer=input.identity.serverPlayer;b.serverSoldier=input.identity.serverSoldier;b.serverItem=input.identity.serverItem;}return b;
    }
    Bc2NativeCycleDecision Begin(unsigned branch){
        input.branch=branch;input.contextObservedNs=input.nowNs=++clock;input.leaseDeadlineNs=clock+100000000;
        decision=service.Evaluate(input,++event);update={};update.id=event;auto& a=update.entry;
        a.kind=ReloadFlowEvent::Update;a.nativeInvocation=a.nativeUpdate=a.update=event;a.thread=7;a.depth=1;a.caller=0x6e90b0;a.context=0x24000;
        a.nowNs=clock;a.contextCopied=true;a.boundary=Boundary(branch);Put(a.copiedContext,0x18,input.context.deltaSeconds);Put(a.copiedContext,0x20,1.f);
        a.copiedContext[0x24]=std::byte{1};Put(a.copiedContext,0x2c,input.context.inputFlags);
        update.exit.thread=a.thread;update.exit.nowNs=++clock;update.exit.boundary=a.boundary;update.exit.contextCopied=true;update.exit.copiedContext=a.copiedContext;
        update.finished=update.identityRetained=true;return decision;
    }
    void Finish(){
        if(decision.hold){update.exit.holdRequested=true;update.exit.hold={std::bit_cast<unsigned>(input.context.deltaSeconds),0,true,true,false};}
        service.Finish(decision,update);
    }
    void Shot(unsigned branch,int beforeLoaded=8){
        // A newly selected owner starts its next synthetic shot from ready.
        // Preserve an explicitly supplied state-6 post-shot fixture.
        auto& b=input.branches[branch];if(b.currentState==2){b.currentState=7;b.previousState=6;b.nextState=8;b.phaseTimer=.2f;}
        input.context.inputFlags=1;input.context.fireRequested=true;
        const auto d=Begin(branch);(void)d;update.entry.boundary.current=2;update.entry.boundary.previous=1;update.entry.boundary.next=2;update.entry.boundary.timer=0;
        update.entry.boundary.loaded=beforeLoaded;Finish();input.context.inputFlags=0;input.context.fireRequested=false;
    }
    bool Hold(){for(unsigned branch=0;branch<3;++branch)Shot(branch);
        for(unsigned pass=0;pass<2;++pass){Advance();for(unsigned branch=0;branch<3;++branch){Begin(branch);Finish();}}
        return service.View(clock).held.has_value();}
    ReloadFlowRecord Restore(unsigned branch){
        auto r=update;r.id=++event;auto& a=r.entry;a.kind=ReloadFlowEvent::Restore;a.depth=1;
        a.parent=a.nativeParent=a.update=a.nativeUpdate=0;a.nativeInvocation=r.id;a.nowNs=++clock;a.boundary=Boundary(branch);
        r.exit.hold={};r.exit.holdRequested=false;
        a.contextCopied=false;r.exit.contextCopied=false;a.snapshotCopied=r.exit.snapshotCopied=true;
        r.exit.nowNs=++clock;r.exit.boundary=a.boundary;r.exit.boundary->previous=a.boundary.current;
        Put(a.copiedSnapshot,0,a.boundary.current);Put(a.copiedSnapshot,4,a.boundary.next);Put(a.copiedSnapshot,8,a.boundary.timer);
        Put(a.copiedSnapshot,0x18,a.boundary.loaded);Put(a.copiedSnapshot,0x1c,a.boundary.reserve);
        r.exit.copiedSnapshot=a.copiedSnapshot;return r;
    }
    void Commit(unsigned,unsigned from,unsigned to){
        auto r=update;r.id=++event;auto& a=r.entry;a.kind=ReloadFlowEvent::Commit;a.depth=2;a.parent=a.nativeParent=update.id;
        a.update=a.nativeUpdate=update.id;a.nativeInvocation=r.id;a.nowNs=++clock;a.boundary.current=from;a.boundary.previous=from-1;a.boundary.next=to;a.boundary.timer=0;
        r.exit.nowNs=++clock;r.exit.boundary=a.boundary;r.exit.boundary->current=to;r.exit.boundary->previous=from;
        service.Commit(r);
    }
    void Resume(unsigned branch,bool skip=false){Begin(branch);if(!skip)Commit(branch,7,8);Commit(branch,8,1);Commit(branch,1,2);
        update.exit.nowNs=++clock;update.exit.boundary->current=2;update.exit.boundary->previous=1;update.exit.boundary->next=2;update.exit.boundary->timer=0;
        Finish();auto& b=input.branches[branch];b.currentState=2;b.previousState=1;b.nextState=2;b.phaseTimer=0;}
    bool Release(){const auto view=service.View(clock);if(!view.held)return false;
        control.input.nowNs=clock;control.release=WeaponCycleRelease{*view.held,1,control.input.sequence,clock,view.held->deadlineNs};return service.Control(control);}
    PhysicalWeaponCycleResult Physical(float travel,bool grip=true){
        Advance(grip);for(unsigned branch=0;branch<3;++branch){Begin(branch);Finish();}
        control.input.nowNs=++clock;const auto view=service.View(clock);
        hands.Update(control.input);const auto current=hands.Current(InteractionHand::Right);
        if(current){hands.Renew(control.input,current->token,{{1,99},control.input.sequence,control.input.deadlineNs,true});gun=current->token;}
        else {const auto got=hands.Acquire(control.input,{control.input.owner,InteractionHand::Right,HandClaimKind::GunHold,control.item,
            {{1,99},control.input.sequence,control.input.deadlineNs,true},++intent,0});if(got.claim)gun=got.claim->token;}
        if(!view.held)return {};
        if(!begun){begun=physical.Begin(profile,*view.held,clock);}
        PhysicalWeaponCycleSample sample;sample.source=control.input;sample.lease=*view.held;sample.gun=gun;sample.grip=grip;
        sample.rawContact=weapon_cycle_detail::Target(profile,travel,0);sample.contact={control.mechanism,sample.source.sequence,sample.source.deadlineNs,true};sample.acquireIntent=++intent;
        const auto out=physical.Update(sample,hands);if(out.release){++sent;control.release=out.release;service.Control(control);}return out;
    }
    bool FullStroke(){Physical(0,false);Physical(0);
        for(float t:{.02f,.04f,.06f,.08f,profile.stroke,profile.stroke,.075f,.055f,.035f,.015f,0.f,0.f})Physical(t);
        return sent==1&&service.View(clock).phase==Bc2NativeCyclePhase::Releasing;}
};
int ThreeOwnShotsAndHolds(){Fixture f;CHECK(f.Hold());const auto view=f.service.View(f.clock);
    CHECK(view.phase==Bc2NativeCyclePhase::Held&&view.blocksFire&&view.loaded==7&&view.reserve==24&&view.capacity==8);
    CHECK(view.held->owner.equipGeneration==99&&view.native.owner.equipGeneration==12);return 0;}
int PhysicalPumpControlsNativeGate(){Fixture f;CHECK(f.Hold());f.Physical(0,false);f.Physical(0);
    for(float t:{.02f,.04f,.06f,.04f,.02f,0.f})f.Physical(t);CHECK(f.sent==0&&f.service.View(f.clock).phase==Bc2NativeCyclePhase::Held);
    for(float t:{.02f,.04f,.06f,.08f,f.profile.stroke,f.profile.stroke,.075f,.055f,.035f,.015f,0.f,0.f})f.Physical(t);
    CHECK(f.sent==1&&f.service.View(f.clock).phase==Bc2NativeCyclePhase::Releasing);
    f.Advance();for(unsigned branch=0;branch<3;++branch)f.Resume(branch);
    const auto view=f.service.View(f.clock);CHECK(view.ready&&view.blocksFire&&view.phase==Bc2NativeCyclePhase::Complete);
    CHECK(f.physical.ObserveReady(*view.ready,f.clock));CHECK(f.physical.Phase()==WeaponCyclePhase::Complete);
    CHECK(f.service.AcknowledgeReady(*view.ready));CHECK(!f.service.View(f.clock).blocksFire);return 0;}
int MissingShotOrHoldCannotPublish(){for(unsigned missing=0;missing<3;++missing){Fixture f;
    for(unsigned branch=0;branch<3;++branch)if(branch!=missing)f.Shot(branch);
    for(unsigned pass=0;pass<3;++pass){f.Advance();for(unsigned branch=0;branch<3;++branch){f.Begin(branch);CHECK(!f.decision.hold);f.Finish();}}
    CHECK(!f.service.View(f.clock).held&&f.service.View(f.clock).blocksFire);}return 0;}
int ForgedReleaseDoesNotUnhold(){for(unsigned mutation=0;mutation<5;++mutation){Fixture f;CHECK(f.Hold());const auto view=f.service.View(f.clock);
    f.control.input.nowNs=f.clock;f.control.release=WeaponCycleRelease{*view.held,1,f.control.input.sequence,f.clock,view.held->deadlineNs};
    auto& release=*f.control.release;if(mutation==0)++release.cycle.sequence;if(mutation==1)++release.cycle.shot;
    if(mutation==2)++release.cycle.deadlineNs;if(mutation==3)release.deadlineNs=f.clock;if(mutation==4)++release.cycle.owner.equipGeneration;
    CHECK(!f.service.Control(f.control));CHECK(f.service.View(f.clock).phase==Bc2NativeCyclePhase::Held);
    f.control.release.reset();f.Advance();f.Begin(0);CHECK(f.decision.hold);f.Finish();}return 0;}
int RestoreAndStateFailuresBlock(){for(unsigned mutation=0;mutation<4;++mutation){Fixture f;CHECK(f.Hold());f.Advance();f.Begin(0);CHECK(f.decision.hold);
    f.update.exit.holdRequested=true;f.update.exit.hold={std::bit_cast<unsigned>(f.input.context.deltaSeconds),0,true,true,false};
    if(mutation==0)f.update.exit.hold.restored=false;if(mutation==1)f.update.exit.boundary->timer=.1f;
    if(mutation==2)--f.update.exit.boundary->loaded;if(mutation==3)f.update.identityRetained=false;
    f.service.Finish(f.decision,f.update);const auto view=f.service.View(f.clock);CHECK(view.phase==Bc2NativeCyclePhase::Cancelled&&view.blocksFire&&!view.held);
    }return 0;}
int MissingTailCannotAcknowledge(){Fixture f;CHECK(f.Hold());CHECK(f.Release());f.Advance();f.Resume(0,true);
    const auto view=f.service.View(f.clock);CHECK(view.phase==Bc2NativeCyclePhase::Cancelled&&!view.ready&&view.blocksFire);return 0;}
int ContextAndOriginalControllerBounds(){for(unsigned mutation=0;mutation<4;++mutation){Fixture f;CHECK(f.Hold());f.Advance();
    if(mutation==0)f.input.context.reloadRequested=true;
    if(mutation==1)++f.input.identity.owner.equipGeneration;
    if(mutation==2)f.input.config.boltDelay=.6f;
    if(mutation==3)f.clock=f.control.input.deadlineNs;
    f.Begin(0);CHECK(!f.decision.hold);CHECK(f.service.View(f.clock).blocksFire&&!f.service.View(f.clock).held);
    }return 0;}
int ContextStampCannotBeRestamped(){Fixture f;CHECK(f.Hold());f.Advance();f.input.branch=0;f.input.nowNs=++f.clock;
    f.input.contextObservedNs=f.clock-Bc2NativeCycleService::ContextFreshNs-1;f.input.leaseDeadlineNs=f.clock+100000000;
    const auto decision=f.service.Evaluate(f.input,++f.event);CHECK(!decision.hold&&f.service.View(f.clock).blocksFire&&!f.service.View(f.clock).held);return 0;}
int RetainedCompletionSurvivesControllerTimeout(){Fixture f;CHECK(f.Hold());CHECK(f.FullStroke());
    f.Advance();for(unsigned branch=0;branch<3;++branch)f.Resume(branch);
    const auto initial=f.service.View(f.clock);CHECK(initial.ready);const auto exact=*initial.ready;
    f.clock+=3000000000ll;PhysicalWeaponCycleSample lost;lost.source=f.control.input;lost.source.nowNs=f.clock;
    f.physical.Update(lost,f.hands);CHECK(f.physical.Phase()==WeaponCyclePhase::Cancelled);
    const auto retained=f.service.View(f.clock);CHECK(retained.ready&&retained.blocksFire);
    CHECK(retained.ready->observedNs==exact.observedNs&&retained.ready->deadlineNs==exact.deadlineNs);
    CHECK(!f.physical.ObserveReady(exact,f.clock));CHECK(f.physical.ReconcileReady(exact,f.clock));
    CHECK(f.service.AcknowledgeReady(exact));CHECK(!f.service.View(f.clock).ready&&!f.service.View(f.clock).blocksFire);
    CHECK(!f.physical.ReconcileReady(exact,f.clock));CHECK(!f.service.AcknowledgeReady(exact));return 0;}
int RetainedCompletionRejectsWrongOrLateOutcome(){for(unsigned mutation=0;mutation<5;++mutation){Fixture f;
    CHECK(f.Hold());CHECK(f.FullStroke());f.Advance();for(unsigned branch=0;branch<3;++branch)f.Resume(branch);
    const auto exact=*f.service.View(f.clock).ready;auto forged=exact;
    if(mutation==0)++forged.release.request;if(mutation==1)++forged.release.cycle.shot;
    if(mutation==2)forged.sequence=forged.release.cycle.sequence;
    if(mutation==3)forged.nativeReady=false;
    if(mutation==4){forged.observedNs=f.clock+f.profile.maximumCycleNs;forged.deadlineNs=forged.observedNs+100000000;}
    f.clock+=3000000000ll;CHECK(!f.physical.ReconcileReady(forged,f.clock));CHECK(!f.service.AcknowledgeReady(forged));
    CHECK(f.service.View(f.clock).blocksFire);CHECK(f.physical.ReconcileReady(exact,f.clock));CHECK(f.service.AcknowledgeReady(exact));}return 0;}
int StartupCancellationDoesNotCreateCycleDebt(){Fixture f;
    // The runtime may ClearOwner repeatedly while loading/in a vehicle.
    f.service.Cancel(Bc2NativeCycleFailure::Owner);f.service.Cancel(Bc2NativeCycleFailure::Control);
    const auto before=f.service.View(f.clock);CHECK(before.phase==Bc2NativeCyclePhase::Watching&&!before.blocksFire&&!before.ready);
    CHECK(f.Advance());CHECK(f.Hold());CHECK(f.service.View(f.clock).blocksFire);return 0;}
int CancellationInvalidatesPreOwnerLossEvaluation(){Fixture f;f.Begin(0);
    f.update.entry.boundary.loaded=8;f.service.Cancel(Bc2NativeCycleFailure::Owner);f.Finish();
    CHECK(f.service.View(f.clock).phase==Bc2NativeCyclePhase::Watching&&!f.service.View(f.clock).blocksFire);
    CHECK(f.Advance());CHECK(f.Hold());return 0;}
int CompletedOutcomeSurvivesOwnerLossAndNewSelection(){Fixture f;CHECK(f.Hold());CHECK(f.FullStroke());
    f.Advance();for(unsigned branch=0;branch<3;++branch)f.Resume(branch);
    const auto exact=*f.service.View(f.clock).ready;f.service.Cancel(Bc2NativeCycleFailure::Owner);
    CHECK(f.service.View(f.clock).ready&&f.service.View(f.clock).blocksFire);
    CHECK(f.physical.ReconcileReady(exact,f.clock));CHECK(f.service.AcknowledgeReady(exact));
    f.input.identity.owner.weapon+=0x100000;f.control.nativeOwner=f.input.identity.owner;
    ++f.control.input.owner.equipGeneration;f.control.item={f.input.identity.owner.weapon,f.control.input.owner.equipGeneration};
    f.control.mechanism.generation=f.control.input.owner.equipGeneration;
    CHECK(f.Advance());CHECK(!f.service.View(f.clock).blocksFire);CHECK(f.Hold());
    CHECK(f.service.View(f.clock).cycle==2&&f.service.View(f.clock).held->item==f.control.item);return 0;}
int InFlightCancellationCannotRebindOrInventReady(){Fixture f;CHECK(f.Hold());CHECK(f.Release());
    f.service.Cancel(Bc2NativeCycleFailure::Owner);f.control.nativeOwner.weapon+=0x100000;
    f.control.item.id=f.control.nativeOwner.weapon;CHECK(!f.Advance());
    const auto blocked=f.service.View(f.clock);CHECK(blocked.phase==Bc2NativeCyclePhase::Cancelled&&blocked.blocksFire&&!blocked.ready);
    f.service.Cancel(Bc2NativeCycleFailure::Control);const auto repeated=f.service.View(f.clock);
    CHECK(repeated.blocksFire&&repeated.failure==blocked.failure&&repeated.failureInvocation==blocked.failureInvocation);return 0;}
int ClientPredictionRestoresPermitProvenPumpHold(){Fixture f;for(unsigned n=0;n<3;++n)f.Shot(n);
    for(unsigned n=0;n<2;++n){f.service.ObserveRestore(f.Restore(n));f.input.branches[n].previousState=7;}
    for(unsigned pass=0;pass<3;++pass){f.Advance();for(unsigned n=0;n<2;++n)f.service.ObserveRestore(f.Restore(n));
        for(unsigned n=0;n<3;++n){f.Begin(n);f.Finish();}}
    CHECK(f.service.View(f.clock).held);CHECK(f.Release());f.Advance();for(unsigned n=0;n<3;++n)f.Resume(n);
    CHECK(f.service.View(f.clock).ready);return 0;}
int UnprovedOrReplayedRestoreCannotAdmit(){
    {Fixture f;for(unsigned n=0;n<3;++n)f.Shot(n);f.input.branches[0].previousState=7;
        for(unsigned pass=0;pass<3;++pass){f.Advance();for(unsigned n=0;n<3;++n){CHECK(!f.Begin(n).hold);f.Finish();}}
        CHECK(!f.service.View(f.clock).held);}
    for(unsigned mutation=0;mutation<5;++mutation){Fixture f;for(unsigned n=0;n<3;++n)f.Shot(n);auto r=f.Restore(0);
        if(mutation==0)r.entry.nativeInvocation=0;if(mutation==1)r.exit.copiedSnapshot[8]^=std::byte{1};
        if(mutation==2)r.entry.boundary.previous=7;if(mutation==3)r.entry.nowNs=1;
        if(mutation==4){f.service.ObserveRestore(r);CHECK(f.service.View(f.clock).phase!=Bc2NativeCyclePhase::Cancelled);}
        f.service.ObserveRestore(r);CHECK(f.service.View(f.clock).phase==Bc2NativeCyclePhase::Cancelled);}
    return 0;}
int RealPostShotRestoreIntervalDoesNotGrantAHold(){Fixture f;
    for(auto& b:f.input.branches){b.currentState=6;b.previousState=5;b.nextState=7;b.phaseTimer=.687916f;}
    for(unsigned n=0;n<3;++n){f.Shot(n);if(n<2){f.service.ObserveRestore(f.Restore(n));f.input.branches[n].previousState=6;}}
    for(unsigned pass=0;pass<3;++pass){f.Advance();for(unsigned n=0;n<2;++n)f.service.ObserveRestore(f.Restore(n));
        for(unsigned n=0;n<3;++n){CHECK(!f.Begin(n).hold);f.Finish();}}
    CHECK(f.service.View(f.clock).phase==Bc2NativeCyclePhase::ShotObserved&&!f.service.View(f.clock).held);
    for(auto& b:f.input.branches){b.currentState=7;b.previousState=6;b.nextState=8;b.phaseTimer=.24f;}
    for(unsigned n=0;n<3;++n){f.Begin(n);f.Finish();}
    for(unsigned n=0;n<2;++n){f.service.ObserveRestore(f.Restore(n));f.input.branches[n].previousState=7;}
    for(unsigned pass=0;pass<2;++pass){f.Advance();for(unsigned n=0;n<3;++n){f.Begin(n);f.Finish();}}
    CHECK(f.service.View(f.clock).held);return 0;}
int InterleavedReleaseRestoresPreserveOriginalReady(){Fixture f;CHECK(f.Hold());
    for(unsigned n=0;n<2;++n){f.service.ObserveRestore(f.Restore(n));f.input.branches[n].previousState=7;}
    CHECK(f.Release());f.Advance();f.Resume(0);
    f.service.ObserveRestore(f.Restore(1));CHECK(f.service.View(f.clock).phase==Bc2NativeCyclePhase::Releasing);
    f.Resume(1);f.service.ObserveRestore(f.Restore(0));f.Resume(2);
    CHECK(f.service.View(f.clock).ready);const auto exact=*f.service.View(f.clock).ready;
    // A late matching prediction restore must not renew completion timestamps.
    f.clock+=Bc2NativeCycleService::MaximumCycleNs;
    for(unsigned n=0;n<2;++n){f.service.ObserveRestore(f.Restore(n));f.input.branches[n].previousState=2;
        f.service.ObserveRestore(f.Restore(n));}
    const auto retained=f.service.View(f.clock);CHECK(retained.ready);
    CHECK(retained.ready->sequence==exact.sequence&&retained.ready->observedNs==exact.observedNs&&retained.ready->deadlineNs==exact.deadlineNs);
    CHECK(f.service.AcknowledgeReady(exact));f.service.ObserveRestore(f.Restore(0));CHECK(!f.service.View(f.clock).blocksFire);return 0;}
int PredictionRollbackRevokesCompletedAndReleasingCycles(){
    for(unsigned stage=0;stage<3;++stage){Fixture f;CHECK(f.Hold());CHECK(f.Release());f.Advance();f.Resume(0);
        std::optional<WeaponCycleReady> exact;
        if(stage){f.Resume(1);f.Resume(2);exact=f.service.View(f.clock).ready;CHECK(exact);
            if(stage==2)CHECK(f.service.AcknowledgeReady(*exact));}
        auto& b=f.input.branches[0];b.currentState=7;b.previousState=6;b.nextState=8;b.phaseTimer=.2f;
        f.service.ObserveRestore(f.Restore(0));const auto v=f.service.View(f.clock);
        CHECK(v.phase==Bc2NativeCyclePhase::Cancelled&&v.blocksFire&&!v.ready&&!v.held);
        if(exact)CHECK(!f.service.AcknowledgeReady(*exact));}
    return 0;}
int UnsupportedServerAndConcurrentRestoresCancel(){for(unsigned mode=0;mode<3;++mode){Fixture f;CHECK(f.Hold());
    if(mode==1)f.Begin(0);
    if(mode==2){CHECK(f.Release());f.Advance();f.Begin(0);f.Commit(0,7,8);}
    const auto r=f.Restore(mode==0?2:0);f.service.ObserveRestore(r);CHECK(f.service.View(f.clock).phase==Bc2NativeCyclePhase::Cancelled);
    CHECK(f.service.View(f.clock).failureInvocation==r.id);
    f.service.Cancel(Bc2NativeCycleFailure::Control);f.service.Missing(f.input.identity.firing[0]);
    CHECK(f.service.View(f.clock).failure==Bc2NativeCycleFailure::Restore&&f.service.View(f.clock).failureInvocation==r.id);}
    return 0;}
int PreHoldHistoryCannotRewindAfterNativeProgress(){Fixture f;CHECK(f.Hold());
    auto& b=f.input.branches[0];b.currentState=6;b.previousState=5;b.nextState=7;b.phaseTimer=.69f;
    f.service.ObserveRestore(f.Restore(0));CHECK(f.service.View(f.clock).phase==Bc2NativeCyclePhase::Cancelled);return 0;}
int RepeatedSameWeaponCyclesAcrossBranchOrder(){
    constexpr std::array<std::array<unsigned,3>,6> orders{{{0,1,2},{0,2,1},{1,0,2},{1,2,0},{2,0,1},{2,1,0}}};
    for(const auto& shotOrder:orders)for(const auto& readyOrder:orders){Fixture f;
        for(unsigned cycle=1;cycle<=5;++cycle){
            for(auto& b:f.input.branches){b.currentState=6;b.previousState=5;b.nextState=7;b.phaseTimer=.687916f;b.loaded=8-int(cycle);}
            f.Advance();for(const auto branch:shotOrder){f.Shot(branch,9-int(cycle));
                if(branch<2){f.service.ObserveRestore(f.Restore(branch));f.input.branches[branch].previousState=6;}}
            CHECK(f.service.View(f.clock).phase==Bc2NativeCyclePhase::ShotObserved);
            for(auto& b:f.input.branches){b.currentState=7;b.previousState=6;b.nextState=8;b.phaseTimer=.24f;}
            for(unsigned pass=0;pass<3;++pass){f.Advance();for(const auto branch:shotOrder){f.Begin(branch);f.Finish();
                if(branch<2){f.service.ObserveRestore(f.Restore(branch));f.input.branches[branch].previousState=7;}}}
            const auto held=f.service.View(f.clock);CHECK(held.held&&held.cycle==cycle&&held.shot==cycle);
            CHECK(held.loaded==8-int(cycle)&&held.reserve==24);CHECK(f.Release());f.Advance();
            for(const auto branch:readyOrder){f.Resume(branch);if(branch<2)f.service.ObserveRestore(f.Restore(branch));}
            const auto ready=f.service.View(f.clock);CHECK(ready.ready&&ready.blocksFire);
            CHECK(f.service.AcknowledgeReady(*ready.ready));CHECK(!f.service.View(f.clock).blocksFire);
        }
    }
    return 0;
}
int IndependentCallbackBranchesRemainIsolated(){
    NativeCycleCallbackState client,server;
    NativeCycleCallbackScope a(&client,0x10000,1,NativeCycleCallbackKind::Update);a.BindInvocation(1043);
    {NativeCycleCallbackScope b(&server,0x20000,2,NativeCycleCallbackKind::Update);b.BindInvocation(1044);
        CHECK(a.Isolated()&&b.Isolated());}
    CHECK(a.Isolated());return 0;
}
int SameBranchOverlapInvalidatesEarlierSample(){
    for(const auto kind:{NativeCycleCallbackKind::Update,NativeCycleCallbackKind::Restore,NativeCycleCallbackKind::Transfer,NativeCycleCallbackKind::Commit}){
        for(unsigned thread:{1u,2u}){NativeCycleCallbackState state;
            {NativeCycleCallbackScope a(&state,0x10000,1,NativeCycleCallbackKind::Update);a.BindInvocation(10);CHECK(a.Isolated());
                {NativeCycleCallbackScope b(&state,0x10000,thread,kind);b.BindInvocation(11);CHECK(!a.Isolated()&&!b.Isolated());}
                CHECK(!a.Isolated());}
            NativeCycleCallbackScope next(&state,0x10000,1,NativeCycleCallbackKind::Update);next.BindInvocation(12);CHECK(next.Isolated());
        }
    }
    return 0;
}
int DirectCommitNestingHasExactParent(){
    for(unsigned mutation=0;mutation<5;++mutation){NativeCycleCallbackState state;
        NativeCycleCallbackScope a(&state,0x10000,1,NativeCycleCallbackKind::Update);a.BindInvocation(10);
        {NativeCycleCallbackScope b(&state,mutation==1?0x20000:0x10000,mutation==2?2:1,
            mutation==3?NativeCycleCallbackKind::Restore:NativeCycleCallbackKind::Commit,
            mutation==4?nullptr:&a,mutation==0?9:10);b.BindInvocation(11);CHECK(!a.Isolated()&&!b.Isolated());}
        CHECK(!a.Isolated());
    }
    NativeCycleCallbackState state;NativeCycleCallbackScope a(&state,0x10000,1,NativeCycleCallbackKind::Update);a.BindInvocation(10);
    {NativeCycleCallbackScope b(&state,0x10000,1,NativeCycleCallbackKind::Commit,&a,10);b.BindInvocation(11);
        CHECK(a.Isolated()&&b.Isolated());
        // Nested Commit-of-Commit is not a direct original Update child.
        {NativeCycleCallbackScope c(&state,0x10000,1,NativeCycleCallbackKind::Commit,&b,11);c.BindInvocation(12);
            CHECK(!a.Isolated()&&!b.Isolated()&&!c.Isolated());}
    }
    CHECK(!a.Isolated());return 0;
}
int DirectCommitsPreserveParentUntilForeignOverlap(){
    NativeCycleCallbackState state;NativeCycleCallbackScope a(&state,0x10000,1,NativeCycleCallbackKind::Update);a.BindInvocation(10);
    for(std::uint64_t id:{11ull,12ull,13ull}){NativeCycleCallbackScope child(&state,0x10000,1,NativeCycleCallbackKind::Commit,&a,10);
        child.BindInvocation(id);CHECK(child.Isolated()&&a.Isolated());}
    {NativeCycleCallbackScope child(&state,0x10000,1,NativeCycleCallbackKind::Commit,&a,10);child.BindInvocation(14);
        {NativeCycleCallbackScope other(&state,0x10000,2,NativeCycleCallbackKind::Restore);other.BindInvocation(15);
            CHECK(!child.Isolated()&&!a.Isolated()&&!other.Isolated());}
        CHECK(!child.Isolated()&&!a.Isolated());}
    return 0;
}
int ParallelCallbackObservationDoesNotSerializeOriginals(){
    for(bool same:{false,true}){std::array<NativeCycleCallbackState,2> states{};std::barrier both(2);std::atomic<bool> failed=false;
        const auto run=[&](unsigned branch){for(unsigned n=0;n<1000;++n){
            {NativeCycleCallbackScope scope(&states[same?0:branch],same?0x10000:0x10000+branch,branch+1,NativeCycleCallbackKind::Update);
                scope.BindInvocation(n*2+branch+1);both.arrive_and_wait();
                if(scope.Isolated()==same)failed=true;
                both.arrive_and_wait();}
            both.arrive_and_wait();
        }};
        std::thread a(run,0),b(run,1);a.join();b.join();CHECK(!failed);
    }
    return 0;
}
int DuplicateOpenUpdateCannotOverwriteReceipt(){
    for(bool activeCycle:{false,true}){Fixture f;if(activeCycle)CHECK(f.Hold());
        f.Advance();const auto first=f.Begin(0);const auto original=f.update;
        const auto duplicate=f.service.Evaluate(f.input,++f.event);CHECK(!duplicate.tracked&&!duplicate.hold);
        f.service.Finish(first,original);
        const auto view=f.service.View(f.clock);
        CHECK(!view.ready&&!view.held);
        CHECK(view.phase==(activeCycle?Bc2NativeCyclePhase::Cancelled:Bc2NativeCyclePhase::Watching));
        if(activeCycle)CHECK(view.failure==Bc2NativeCycleFailure::Owner&&view.failureInvocation==f.event);
    }
    return 0;
}
int ParallelBranchUpdatesRetainTheirOwnReceipts(){Fixture f;CHECK(f.Hold());f.Advance();
    const auto a=f.Begin(0);const auto ra=f.update;const auto b=f.Begin(2);const auto rb=f.update;
    CHECK(a.tracked&&b.tracked&&a.hold&&b.hold);
    auto first=ra,second=rb;first.exit.holdRequested=second.exit.holdRequested=true;
    first.exit.hold=second.exit.hold={std::bit_cast<unsigned>(f.input.context.deltaSeconds),0,true,true,false};
    f.service.Finish(a,first);f.service.Finish(b,second);
    CHECK(f.service.View(f.clock).phase==Bc2NativeCyclePhase::Held&&f.service.View(f.clock).held);return 0;
}
int MissingCarriesViolatingInvocation(){Fixture f;CHECK(f.Hold());f.service.Missing(f.input.identity.firing[0],1044);
    auto view=f.service.View(f.clock);CHECK(view.failure==Bc2NativeCycleFailure::Owner&&view.failureInvocation==1044);
    f.service.Missing(f.input.identity.firing[1],1045);view=f.service.View(f.clock);
    CHECK(view.failureInvocation==1044);return 0;
}

bool EmptyHold(Fixture& f,float timer=.7f){
    for(auto& b:f.input.branches){b.loaded=0;b.currentState=6;b.previousState=5;b.nextState=1;b.phaseTimer=timer;}
    for(unsigned branch=0;branch<3;++branch)f.Shot(branch,1);
    for(unsigned branch=0;branch<2;++branch){f.service.ObserveRestore(f.Restore(branch));f.input.branches[branch].previousState=6;}
    for(unsigned pass=0;pass<2;++pass){f.Advance();for(unsigned branch=0;branch<3;++branch){f.Begin(branch);f.Finish();}}
    const auto v=f.service.View(f.clock);if(!v.held)std::printf("empty failure phase%u reason%u id%llu shot%llu\n",unsigned(v.phase),unsigned(v.failure),v.failureInvocation,v.shot);
    return v.held.has_value();
}
void EmptyResume(Fixture& f,unsigned branch){
    f.Begin(branch);f.Commit(branch,6,1);f.Commit(branch,1,2);
    f.update.exit.nowNs=++f.clock;f.update.exit.boundary->current=2;f.update.exit.boundary->previous=1;
    f.update.exit.boundary->next=2;f.update.exit.boundary->timer=0;f.Finish();
    auto& b=f.input.branches[branch];b.currentState=2;b.previousState=1;b.nextState=2;b.phaseTimer=0;
}
int LastRoundRequiresPhysicalStrokeAndExactEmptyTail(){Fixture f;CHECK(EmptyHold(f));
    const auto held=f.service.View(f.clock);CHECK(held.blocksFire&&held.loaded==0&&held.reserve==24&&held.shot==1);
    CHECK(f.FullStroke());f.Advance();EmptyResume(f,0);f.service.ObserveRestore(f.Restore(1));
    EmptyResume(f,1);f.service.ObserveRestore(f.Restore(0));EmptyResume(f,2);
    const auto ready=f.service.View(f.clock);CHECK(ready.ready&&ready.blocksFire&&ready.loaded==0&&ready.reserve==24);
    CHECK(f.physical.ReconcileReady(*ready.ready,f.clock));CHECK(f.service.AcknowledgeReady(*ready.ready));
    CHECK(!f.service.View(f.clock).blocksFire);return 0;
}
int LastRoundAfterPriorCompletedCycleIsIndependent(){Fixture f;
    for(auto& b:f.input.branches)b.loaded=1;
    for(unsigned n=0;n<3;++n)f.Shot(n,2);
    for(unsigned pass=0;pass<2;++pass){f.Advance();for(unsigned n=0;n<3;++n){f.Begin(n);f.Finish();}}
    CHECK(f.Release());f.Advance();for(unsigned n=0;n<3;++n)f.Resume(n);
    const auto ready=f.service.View(f.clock).ready;CHECK(ready);CHECK(f.service.AcknowledgeReady(*ready));
    CHECK(EmptyHold(f));CHECK(f.service.View(f.clock).cycle==2&&f.service.View(f.clock).shot==2);
    CHECK(f.Release());f.Advance();for(unsigned n=0;n<3;++n)EmptyResume(f,n);
    CHECK(f.service.View(f.clock).ready);return 0;
}
int EmptyCycleRejectsPositiveTailAndMissingBranch(){
    {Fixture f;CHECK(EmptyHold(f));CHECK(f.Release());f.Advance();f.Resume(0);
        CHECK(f.service.View(f.clock).phase==Bc2NativeCyclePhase::Cancelled&&!f.service.View(f.clock).ready);}
    {Fixture f;CHECK(EmptyHold(f));CHECK(f.Release());f.Advance();EmptyResume(f,0);EmptyResume(f,1);
        CHECK(f.service.View(f.clock).blocksFire&&!f.service.View(f.clock).ready);}
    return 0;
}
int ReleasedEmptyPumpStillUsesStepReloadInhibitor(){
    // Actual282: after6->1->2 in the last shot's original Update, a further
    // state-2 Step immediately committed2->10. Merely having a normal cycle
    // frame had bypassed the manual family's automatic-reload inhibitor.
    // Compare that old dispatch with the connected policy/byte override.
    for(bool bypass:{true,false}){Fixture f;CHECK(EmptyHold(f));CHECK(f.FullStroke());f.Advance();
        unsigned inhibited=0,automatic=0;
        for(unsigned branch=0;branch<3;++branch){
            f.Begin(branch);CHECK(f.decision.tracked&&!f.decision.hold);
            f.Commit(branch,6,1);f.Commit(branch,1,2);
            MagazineEmptyStep step;step.identity=f.input.identity;step.ownerRevision=8;step.sourceSequence=20;
            step.observedNs=f.clock;step.deadlineNs=f.clock+50000000;step.branch=branch;
            step.before=f.input.branches[branch];step.before.currentState=step.before.nextState=2;
            step.before.previousState=1;step.before.phaseTimer=0;
            std::uint8_t byte=0;
            MagazineEmptyByteAccess access{&byte,[](void* p,std::uint8_t expected,std::uint8_t value,std::uint8_t& before){
                auto& x=*static_cast<std::uint8_t*>(p);before=x;if(x!=expected)return false;x=value;return true;}};
            const auto route=SelectNativeCycleStepRoute(true,true,f.decision.hold);
            const bool eligible=!bypass&&route==NativeCycleStepRoute::OrdinaryEmptyControl&&
                ManualEmptyFamilyStepEligible(step,ReloadNativeFamily::SpasTube,f.input.config,ReloadRequestCyclePhase::Idle,f.input.context,f.clock);
            struct Call {Fixture* fixture;std::uint8_t* byte;unsigned branch;unsigned* automatic;} call{&f,&byte,branch,&automatic};
            MagazineEmptyByteOverride patch;
            RunMagazineEmptyByteOverride(access,0,eligible,[](void* p){auto& c=*static_cast<Call*>(p);
                if(!*c.byte){++*c.automatic;c.fixture->Commit(c.branch,2,10);}},&call,patch);
            if(patch.applied)++inhibited;
            CHECK(byte==0&&(!patch.applied||patch.restored));
            f.update.exit.nowNs=++f.clock;auto& after=*f.update.exit.boundary;
            after.current=after.next=2;after.previous=1;after.timer=0;f.Finish();
            auto& b=f.input.branches[branch];b.currentState=b.nextState=2;b.previousState=1;b.phaseTimer=0;
            if(bypass)break;
        }
        const auto outcome=f.service.View(f.clock);
        if(bypass){CHECK(automatic==1&&inhibited==0&&outcome.failure==Bc2NativeCycleFailure::Completion&&!outcome.ready);}
        else {CHECK(automatic==0&&inhibited==3&&outcome.ready&&outcome.loaded==0&&outcome.reserve==24);
            CHECK(f.physical.ReconcileReady(*outcome.ready,f.clock));CHECK(f.service.AcknowledgeReady(*outcome.ready));
            CHECK(!f.service.View(f.clock).blocksFire&&f.service.YieldForReload());}
    }
    CHECK(SelectNativeCycleStepRoute(true,true,true)==NativeCycleStepRoute::IssuedHold);
    CHECK(SelectNativeCycleStepRoute(true,false,false)==NativeCycleStepRoute::GuardedRecovery);
    std::puts("Recorded last-shot2->10 auto-reload reproduced by old dispatch; scoped Step inhibitor permits exact empty-pump completion and reload handoff");return 0;
}
int UnclassifiedLastRoundNeverSilentlyContinues(){
    for(unsigned mutation=0;mutation<4;++mutation){Fixture f;
        auto& b=f.input.branches[0];b.loaded=0;b.currentState=6;b.previousState=5;b.nextState=1;b.phaseTimer=.7f;
        if(mutation==0)b.nextState=7;if(mutation==1)b.previousState=6;
        if(mutation==2)b.phaseTimer=0;if(mutation==3)b.phaseTimer=1.1f;
        f.Shot(0,1);const auto view=f.service.View(f.clock);
        CHECK(view.phase==Bc2NativeCyclePhase::Cancelled&&view.blocksFire&&!view.held&&!view.ready&&view.failure==Bc2NativeCycleFailure::Shot);
    }
    return 0;
}

int SubmittedCompletionOutlivesPhysicalInputLease(){
    for(bool empty:{false,true}){Fixture f;CHECK(empty?EmptyHold(f):f.Hold());CHECK(f.FullStroke());
        const auto release=f.service.CompletionWatch(f.clock);CHECK(release&&release->identity==f.input.identity);
        CHECK(f.service.SuspendCompletion());f.clock+=300000000;
        CHECK(f.clock>f.control.input.deadlineNs);CHECK(f.service.CompletionWatch(f.clock));
        for(unsigned n=0;n<3;++n){if(empty)EmptyResume(f,n);else f.Resume(n);}
        const auto ready=f.service.View(f.clock).ready;CHECK(ready);CHECK(!f.service.CompletionWatch(f.clock));
        CHECK(f.physical.ReconcileReady(*ready,f.clock));CHECK(f.service.AcknowledgeReady(*ready));
        CHECK(!f.service.View(f.clock).blocksFire);
    }return 0;
}
int CompletionObservationCannotHoldRebindOrInventReady(){
    {Fixture f;CHECK(f.Hold());CHECK(!f.service.SuspendCompletion());CHECK(!f.service.CompletionWatch(f.clock));}
    for(unsigned mutation=0;mutation<3;++mutation){Fixture f;CHECK(f.Hold());CHECK(f.Release());CHECK(f.service.SuspendCompletion());
        f.clock+=300000000;
        if(mutation==0)++f.input.identity.owner.weapon;
        if(mutation==1)f.input.config.boltDelay=.6f;
        if(mutation==2)f.clock+=Bc2NativeCycleService::MaximumCompletionNs;
        const auto d=f.Begin(0);CHECK(!d.tracked&&!d.hold);CHECK(f.service.View(f.clock).blocksFire&&!f.service.View(f.clock).ready);
    }
    {Fixture f;CHECK(f.Hold());CHECK(f.Release());f.clock+=300000000;
        for(unsigned n=0;n<3;++n){f.Begin(n);CHECK(!f.decision.hold);f.update.exit.boundary->current=2;
            f.update.exit.boundary->next=2;f.update.exit.boundary->previous=1;f.update.exit.boundary->timer=0;f.Finish();}
        CHECK(!f.service.View(f.clock).ready&&f.service.View(f.clock).blocksFire);}
    return 0;
}

int ReloadHandoffNeedsNoUnresolvedDebtOrOpenCallback(){
    {Fixture f;CHECK(f.service.YieldForReload());CHECK(!f.service.View(f.clock).blocksFire);f.Advance();
        f.Begin(0);CHECK(!f.service.YieldForReload());f.Finish();CHECK(f.service.YieldForReload());}
    for(bool empty:{false,true}){Fixture f;CHECK(empty?EmptyHold(f):f.Hold());
        CHECK(!f.service.YieldForReload());CHECK(f.FullStroke());CHECK(!f.service.YieldForReload());
        for(unsigned n=0;n<3;++n){if(empty)EmptyResume(f,n);else f.Resume(n);}
        const auto ready=f.service.View(f.clock).ready;CHECK(ready&&!f.service.YieldForReload());
        CHECK(f.service.AcknowledgeReady(*ready));const auto prior=f.service.View(f.clock);
        CHECK(f.service.YieldForReload());const auto yielded=f.service.View(f.clock);
        CHECK(!yielded.blocksFire&&!yielded.held&&!yielded.ready&&yielded.phase==Bc2NativeCyclePhase::Watching);
        CHECK(yielded.cycle==prior.cycle&&yielded.shot==prior.shot);
        f.Advance();CHECK(f.service.Control(f.control));
    }
    {Fixture f;CHECK(f.Hold());f.service.Cancel(Bc2NativeCycleFailure::Control);
        CHECK(!f.service.YieldForReload()&&f.service.View(f.clock).blocksFire);}
    return 0;
}
int SubmittedReleaseCannotBeRewoundByOlderHeldUpdate(){
    for(bool empty:{false,true}){Fixture f;CHECK(empty?EmptyHold(f):f.Hold());f.Advance();
        f.Begin(0);CHECK(f.decision.hold);CHECK(f.Release());f.update.exit.nowNs=++f.clock;f.Finish();
        const auto inFlight=f.service.View(f.clock);CHECK(inFlight.phase==Bc2NativeCyclePhase::Releasing&&!inFlight.held);
        for(unsigned n=0;n<3;++n){if(empty)EmptyResume(f,n);else f.Resume(n);}
        CHECK(f.service.View(f.clock).ready);
    }return 0;
}


// Actual BC2 adapter and original renderer packets around the service. Only
// native callbacks are synthetic; the native leases are produced by the service.
struct InterruptedAdapter {
    Fixture f;RigSnapshot rig;std::shared_ptr<Bc2PumpCalibration> calibration=std::make_shared<Bc2PumpCalibration>();
    Bc2PhysicalPump pump;Bc2PumpRawContact prior;Bc2PhysicalPumpResult result;unsigned cancellations=0,submissions=0,acknowledgements=0,audioEvents=0;bool delayedGatherClock=false;
    MechanismAudioLedger<> audioLedger;
    Bc2PhysicalPumpApi Api(){return {this,
        [](void* p,const Bc2NativeCycleControl& c)noexcept {auto& a=*static_cast<InterruptedAdapter*>(p);
            if(c.release)++a.submissions;return a.f.service.Control(c);},
        [](void* p,std::int64_t now)noexcept->std::optional<Bc2NativeCycleView>{return static_cast<InterruptedAdapter*>(p)->f.service.View(now);},
        [](void* p,const WeaponCycleReady& r)noexcept{auto& a=*static_cast<InterruptedAdapter*>(p);
            if(!a.f.service.AcknowledgeReady(r,a.f.clock))return false;++a.acknowledgements;return true;},
        [](void* p)noexcept {auto& a=*static_cast<InterruptedAdapter*>(p);++a.cancellations;
            if(!a.f.service.SuspendCompletion()&&!a.f.service.SuspendHeld(a.f.clock))a.f.service.Cancel(Bc2NativeCycleFailure::Control);}};}
    InterruptedAdapter():pump(calibration,Api()){
        f.control.mechanism={0x5350415350554d50ull,99};f.service.Control(f.control);
        rig.names={"root","jntWpn_1","jntWpn_4","LeftHand"};rig.parents={-1,0,1,0};rig.weaponBone=1;
        rig.identity={0x11000,0x12000,0x30000,0x40000,0x50000,0x60000,0x70000,0x80000,0x90000,4,false};
        rig.world.assign(4,Pose());rig.inverseBind.assign(4,Pose());rig.evaluatedWorld.assign(4,Pose());rig.nativeEvaluated.resize(4);
        calibration->revision=1;calibration->asset=SpasReloadAsset;calibration->profileId=0x5350415350554d50ull;calibration->geometry=SpasPumpGeometry;calibration->rigFingerprint=bc2_pump_detail::DerivePart(rig)->fingerprint;
        calibration->closedPart=Pose();calibration->closedPart.values[3][2]=-.84f;calibration->closedWrist=Pose();
        calibration->rearDirection=1;calibration->measured=true;
    }
    void Tick(float travel,bool grip=true,bool tracked=true){
        f.clock+=10000000;auto& s=f.control.input;++s.sequence;s.observedNs=s.nowNs=f.clock;s.deadlineNs=f.clock+100000000;
        s.focused=true;s.tracked={tracked,true};s.released={!grip,false};f.control.release.reset();
        for(unsigned n=0;n<3;++n){f.Begin(n);f.Finish();}if(!delayedGatherClock)s.nowNs=f.clock;
        f.hands.Update(s);auto gun=f.hands.Current(InteractionHand::Right);
        if(gun)f.hands.Renew(s,gun->token,{{1,99},s.sequence,s.deadlineNs,true});
        else f.hands.Acquire(s,{s.owner,InteractionHand::Right,HandClaimKind::GunHold,f.control.item,
            {{1,99},s.sequence,s.deadlineNs,true},++f.intent,0});
        Bc2PhysicalPumpSample sample;sample.nativeOwner=f.control.nativeOwner;sample.item=f.control.item;
        sample.asset=SpasReloadAsset;sample.input=s;sample.grip=grip;sample.raw=prior;
        result=pump.Tick(sample,f.hands,f.intent);
        if(result.audio&&audioLedger.Accept(*result.audio,s.nowNs)==MechanismAudioAdmission::Accepted)++audioEvents;
        auto wrist=Pose();wrist.values[3][2]=travel;
        prior=BuildPumpRawContact(result.tracking,rig,wrist,Pose(),{.02f,0,0},1,f.clock);
    }
    bool Grip(){if(!f.Hold())return false;Tick(0,false);Tick(0,false);Tick(0);Tick(0);return result.ownsHand&&bool(result.tracking.target);}
    void Stroke(){for(float t:{.02f,.04f,.06f,.08f,SpasObservedForeEndStroke,SpasObservedForeEndStroke,SpasObservedForeEndStroke,
        SpasObservedForeEndStroke,.075f,.055f,.035f,.015f,0.f,0.f,0.f,0.f,0.f}){Tick(t);if(submissions)break;}}
};
int ActualAdapterTrackingLossRegripsOriginalNativeDebt(){
    for(bool empty:{false,true}){auto a=std::make_unique<InterruptedAdapter>();
        if(empty){CHECK(EmptyHold(a->f));a->Tick(0,false);a->Tick(0,false);a->Tick(0);a->Tick(0);CHECK(a->result.ownsHand);}
        else CHECK(a->Grip());
        const auto original=*a->f.service.View(a->f.clock).held;
        a->Tick(.02f);a->Tick(.04f);a->Tick(.06f);a->Tick(.06f,true,false);
        const auto watch=a->f.service.ObservationWatch(a->f.clock);CHECK(watch&&watch->holdsNativeCycle);
        CHECK(!a->result.tracking.target&&!a->f.hands.Current(InteractionHand::Left)&&a->result.blocksFire);
        for(unsigned n=0;n<40;++n){a->Tick(0,true,false);CHECK(!a->f.service.View(a->f.clock).held&&!a->submissions);}
        CHECK(a->f.service.ObservationWatch(a->f.clock)->deadlineNs==watch->deadlineNs);
        // Tracking returns with held squeeze: no fabricated neutral/regrab.
        for(unsigned n=0;n<4;++n){a->Tick(0);CHECK(!a->result.ownsHand&&!a->result.tracking.target&&!a->submissions);}
        a->Tick(0,false);a->Tick(0,false);a->Tick(0,false);a->Tick(0);a->Tick(0);
        CHECK(a->result.ownsHand&&a->result.tracking.target&&Near(a->result.tracking.target->travel,0));
        CHECK(weapon_cycle_detail::Same(original,a->result.tracking.target->lease));
        for(unsigned n=0;n<4;++n)a->Tick(0);CHECK(!a->submissions);
        a->Stroke();CHECK(a->submissions==1&&a->f.service.View(a->f.clock).phase==Bc2NativeCyclePhase::Releasing);
        for(unsigned n=0;n<3;++n){if(empty)EmptyResume(a->f,n);else a->f.Resume(n);}
        a->Tick(0);CHECK(a->acknowledgements==1&&!a->result.blocksFire);
        CHECK(a->f.service.View(a->f.clock).shot==original.shot&&a->f.service.View(a->f.clock).cycle==original.cycle);
    }return 0;
}
int HeldInterruptionUsesNativeOnlyBoundedAuthority(){
    for(bool empty:{false,true}){Fixture f;CHECK(empty?EmptyHold(f):f.Hold());
        const auto old=f.service.View(f.clock);CHECK(f.service.SuspendHeld(f.clock));
        const auto watch=f.service.ObservationWatch(f.clock);CHECK(watch&&watch->holdsNativeCycle);
        CHECK(watch->identity==f.input.identity&&watch->cycle==old.cycle);
        CHECK(watch->deadlineNs==f.clock+Bc2NativeCycleService::MaximumHoldSuspensionNs);
        CHECK(!f.service.View(f.clock).held&&f.service.View(f.clock).heldSuspended);
        f.clock+=500000000;CHECK(f.clock>f.control.input.deadlineNs);
        // Every branch supplies a fresh native context; stale other-branch
        // contexts cannot be restamped or used for a new held receipt.
        for(unsigned pass=0;pass<2;++pass)for(unsigned n=0;n<3;++n){f.Begin(n);if(pass)CHECK(f.decision.hold);f.Finish();}
        const auto suspended=f.service.View(f.clock);
        CHECK(suspended.phase==Bc2NativeCyclePhase::Held&&suspended.blocksFire&&!suspended.held&&!suspended.ready);
        CHECK(suspended.heldSuspensions==1&&!suspended.heldResumptions);
        CHECK(suspended.cycle==old.cycle&&suspended.shot==old.shot&&suspended.loaded==old.loaded&&suspended.reserve==old.reserve);
        CHECK(f.service.SuspendHeld(f.clock));CHECK(f.service.ObservationWatch(f.clock)->deadlineNs==watch->deadlineNs);
        CHECK(!f.service.YieldForReload());
        // Replaying the old valid-looking physical release cannot submit it.
        f.control.input.nowNs=f.clock;f.control.input.observedNs=f.clock;f.control.input.deadlineNs=f.clock+100000000;
        ++f.control.input.sequence;f.control.release=WeaponCycleRelease{*old.held,1,f.control.input.sequence,f.clock,f.clock+10000000};
        CHECK(!f.service.Control(f.control));CHECK(f.service.View(f.clock).heldSuspended);
        CHECK(f.Advance(false));CHECK(!f.service.View(f.clock).heldSuspended&&!f.service.View(f.clock).held);
        for(unsigned n=0;n<3;++n){f.Begin(n);CHECK(f.decision.hold);f.Finish();}
        const auto resumed=f.service.View(f.clock);CHECK(resumed.held&&resumed.heldSuspensions==1&&resumed.heldResumptions==1);
        CHECK(weapon_cycle_detail::Same(*old.held,*resumed.held)&&resumed.held->sequence>old.held->sequence);
        CHECK(resumed.held->observedNs>old.held->deadlineNs);
        CHECK(f.FullStroke());for(unsigned n=0;n<3;++n){if(empty)EmptyResume(f,n);else f.Resume(n);}
        const auto ready=f.service.View(f.clock).ready;CHECK(ready&&f.physical.ReconcileReady(*ready,f.clock));
        CHECK(f.service.AcknowledgeReady(*ready)&&!f.service.View(f.clock).blocksFire);
        CHECK(f.service.View(f.clock).cycle==old.cycle&&f.service.View(f.clock).shot==old.shot);
    }return 0;
}
int HeldSuspensionRequiresEstablishedExactDebt(){
    {Fixture f;CHECK(!f.service.SuspendHeld(f.clock));CHECK(!f.service.ObservationWatch(f.clock));}
    {Fixture f;f.Shot(0);CHECK(!f.service.SuspendHeld(f.clock)&&f.service.View(f.clock).blocksFire);}
    {Fixture f;CHECK(f.Hold());f.clock=f.service.View(f.clock).held->deadlineNs;
        CHECK(!f.service.SuspendHeld(f.clock));CHECK(!f.service.ObservationWatch(f.clock));}
    {Fixture f;CHECK(f.Hold());CHECK(f.Release());CHECK(!f.service.SuspendHeld(f.clock));
        CHECK(f.service.ObservationWatch(f.clock)&&!f.service.ObservationWatch(f.clock)->holdsNativeCycle);}
    {Fixture f;CHECK(f.Hold());f.service.Cancel(Bc2NativeCycleFailure::Owner);
        CHECK(!f.service.SuspendHeld(f.clock)&&!f.Advance(false)&&f.service.View(f.clock).blocksFire);}
    return 0;
}
int SuspendedHoldNeverRenewsItsDeadlineOrOriginalCycle(){
    for(bool originalLimit:{false,true}){Fixture f;CHECK(f.Hold());
        if(originalLimit){f.clock+=Bc2NativeCycleService::MaximumCycleNs-1000000000;
            CHECK(f.Advance());for(unsigned pass=0;pass<2;++pass)for(unsigned n=0;n<3;++n){f.Begin(n);f.Finish();}}
        CHECK(f.service.SuspendHeld(f.clock));const auto watch=f.service.ObservationWatch(f.clock);CHECK(watch);
        if(originalLimit)CHECK(watch->deadlineNs-f.clock<1000000000);
        f.clock=watch->deadlineNs-100000000;CHECK(f.service.SuspendHeld(f.clock));
        CHECK(f.service.ObservationWatch(f.clock)->deadlineNs==watch->deadlineNs);
        f.clock=watch->deadlineNs;CHECK(!f.service.SuspendHeld(f.clock));CHECK(!f.service.ObservationWatch(f.clock));
        CHECK(!f.Begin(0).tracked);const auto v=f.service.View(f.clock);
        CHECK(v.phase==Bc2NativeCyclePhase::Cancelled&&v.failure==Bc2NativeCycleFailure::Expired&&v.blocksFire&&!v.held&&!v.ready);
        CHECK(!f.Advance(false)&&!f.service.YieldForReload());
    }return 0;
}
int HeldSuspensionRequiresFreshSameOwnerNeutral(){
    for(unsigned mutation=0;mutation<7;++mutation){Fixture f;CHECK(f.Hold());CHECK(f.service.SuspendHeld(f.clock));
        auto c=f.control;c.input.nowNs=c.input.observedNs=f.clock+10000000;c.input.deadlineNs=c.input.nowNs+100000000;
        ++c.input.sequence;c.input.released={true,false};
        if(mutation==0)c.input.released[0]=false;
        if(mutation==1)c.input.released[1]=true;
        if(mutation==2){c.input.sequence=f.control.input.sequence;c.input.observedNs=f.control.input.observedNs;c.input.deadlineNs=f.control.input.deadlineNs;}
        if(mutation==3)c.input.tracked[0]=false;
        if(mutation==4)c.input.focused=false;
        if(mutation==5)++c.input.owner.equipGeneration;
        if(mutation==6)c.input.observedNs=f.clock;
        f.service.Control(c);const auto v=f.service.View(c.input.nowNs);
        CHECK(v.blocksFire&&!v.held&&!v.ready);
        CHECK(v.heldSuspended||v.phase==Bc2NativeCyclePhase::Cancelled);
    }return 0;
}
int HeldSuspensionKeepsNativeProofAndReadFailuresStrict(){
    for(unsigned mutation=0;mutation<9;++mutation){Fixture f;CHECK(f.Hold());CHECK(f.service.SuspendHeld(f.clock));
        f.clock+=200000000;
        if(mutation==0)++f.input.identity.owner.weapon;
        if(mutation==1)f.input.config.boltDelay=.6f;
        if(mutation==2)--f.input.branches[1].loaded;
        if(mutation==3)++f.input.branches[2].reserve;
        if(mutation==4)f.input.branches[0].currentState=8;
        if(mutation==5)f.input.context.fireRequested=true;
        if(mutation==6)f.input.context.reloadRequested=true;
        if(mutation==7)f.input.capacities[2]=9;
        if(mutation==8)f.service.Missing(f.input.identity.firing[0],f.event+1);
        CHECK(!f.Begin(0).hold);const auto v=f.service.View(f.clock);
        CHECK(v.phase==Bc2NativeCyclePhase::Cancelled&&v.blocksFire&&!v.held&&!v.ready);
        CHECK(!f.service.ObservationWatch(f.clock)&&!f.Advance(false));
    }
    {Fixture f;CHECK(f.Hold());CHECK(f.service.SuspendHeld(f.clock));f.Begin(0);CHECK(f.decision.hold);
        f.update.exit.holdRequested=true;f.update.exit.hold={std::bit_cast<unsigned>(f.input.context.deltaSeconds),0,true,false,false};
        f.service.Finish(f.decision,f.update);CHECK(f.service.View(f.clock).failure==Bc2NativeCycleFailure::Restore);}
    return 0;
}
int SuspensionDoesNotRepublishInflightControllerLease(){Fixture f;CHECK(f.Hold());f.Begin(0);CHECK(f.decision.hold);
    CHECK(f.service.SuspendHeld(f.clock));f.Finish();CHECK(!f.service.View(f.clock).held&&f.service.View(f.clock).heldSuspended);
    CHECK(f.Advance(false));for(unsigned n=0;n<3;++n){f.Begin(n);f.Finish();}
    CHECK(f.service.View(f.clock).held&&!f.service.View(f.clock).heldSuspended);return 0;
}

int FreshNeutralDoesNotInvalidateInflightNativeOnlyObservation(){Fixture f;CHECK(f.Hold());
    CHECK(!f.service.ObservationAuthority(0,f.clock));CHECK(f.service.SuspendHeld(f.clock));
    const auto authority=f.service.ObservationAuthority(0,f.clock);CHECK(authority==f.service.View(f.clock).cycle);
    f.Begin(0);CHECK(f.decision.hold);const auto originalDeadline=f.decision.deadlineNs;
    CHECK(f.Advance(false));CHECK(!f.service.ObservationWatch(f.clock));
    CHECK(f.service.ObservationAuthority(authority,f.clock)==authority);
    CHECK(!f.service.ObservationAuthority(authority+1,f.clock));
    f.update.exit.nowNs=f.clock;CHECK(f.clock<originalDeadline);f.Finish();
    CHECK(f.service.View(f.clock).phase==Bc2NativeCyclePhase::Held&&f.service.View(f.clock).held);
    // Native observation authority neither renews the expired XR packet nor
    // admits another callback without its own regular input/watch proof.
    f.clock=f.control.input.deadlineNs;CHECK(!f.Begin(1).tracked);
    CHECK(!f.service.ObservationAuthority(authority,f.clock));
    CHECK(f.service.View(f.clock).blocksFire&&f.service.View(f.clock).phase==Bc2NativeCyclePhase::Cancelled);
    return 0;
}

int ConcurrentNativePublicationCannotEraseCurrentGesture(){auto a=std::make_unique<InterruptedAdapter>();CHECK(a->Grip());
    const auto original=*a->result.tracking.target;const auto inputSequence=a->f.control.input.sequence;
    a->delayedGatherClock=true;
    for(unsigned n=0;n<5;++n){a->Tick(0);CHECK(a->result.ownsHand&&a->result.tracking.target);
        CHECK(a->result.tracking.held->observedNs<=a->f.control.input.nowNs);
        CHECK(a->result.tracking.held->deadlineNs>a->f.control.input.nowNs);
        CHECK(weapon_cycle_detail::Same(original.lease,a->result.tracking.target->lease));}
    CHECK(a->f.control.input.sequence>inputSequence&&!a->cancellations&&!a->submissions);
    a->Stroke();CHECK(a->submissions==1);return 0;
}

int FuturePublicationUsesOnlyExactIssuedLiveLease(){Fixture f;CHECK(f.Hold());const auto old=f.service.View(f.clock);const auto at=f.clock;
    f.Advance();for(unsigned n=0;n<3;++n){f.Begin(n);f.Finish();}
    const auto historical=f.service.View(at);CHECK(historical.held&&*historical.held==*old.held);
    CHECK(!f.service.View(1).held);CHECK(!f.service.View(f.service.View(f.clock).held->deadlineNs).held);
    CHECK(f.service.SuspendHeld(f.clock));CHECK(!f.service.View(at).held);
    CHECK(f.Advance(false));for(unsigned n=0;n<3;++n){f.Begin(n);f.Finish();}
    CHECK(!f.service.View(at).held);CHECK(f.Release());CHECK(!f.service.View(at).held);
    f.service.Cancel(Bc2NativeCycleFailure::Owner);CHECK(!f.service.View(at).held);return 0;
}

}
int OriginalSpasRegression(){if(ReleasedEmptyPumpStillUsesStepReloadInhibitor()||ThreeOwnShotsAndHolds()||PhysicalPumpControlsNativeGate()||MissingShotOrHoldCannotPublish()||ForgedReleaseDoesNotUnhold()||
    RestoreAndStateFailuresBlock()||MissingTailCannotAcknowledge()||ContextAndOriginalControllerBounds()||ContextStampCannotBeRestamped()||
    RetainedCompletionSurvivesControllerTimeout()||RetainedCompletionRejectsWrongOrLateOutcome()||StartupCancellationDoesNotCreateCycleDebt()||
    CancellationInvalidatesPreOwnerLossEvaluation()||CompletedOutcomeSurvivesOwnerLossAndNewSelection()||InFlightCancellationCannotRebindOrInventReady()||
    ClientPredictionRestoresPermitProvenPumpHold()||UnprovedOrReplayedRestoreCannotAdmit()||RealPostShotRestoreIntervalDoesNotGrantAHold()||
    InterleavedReleaseRestoresPreserveOriginalReady()||PredictionRollbackRevokesCompletedAndReleasingCycles()||
    UnsupportedServerAndConcurrentRestoresCancel()||PreHoldHistoryCannotRewindAfterNativeProgress()||RepeatedSameWeaponCyclesAcrossBranchOrder()||
    IndependentCallbackBranchesRemainIsolated()||SameBranchOverlapInvalidatesEarlierSample()||DirectCommitNestingHasExactParent()||
    DirectCommitsPreserveParentUntilForeignOverlap()||ParallelCallbackObservationDoesNotSerializeOriginals()||
    DuplicateOpenUpdateCannotOverwriteReceipt()||ParallelBranchUpdatesRetainTheirOwnReceipts()||MissingCarriesViolatingInvocation()||LastRoundRequiresPhysicalStrokeAndExactEmptyTail()||
    LastRoundAfterPriorCompletedCycleIsIndependent()||EmptyCycleRejectsPositiveTailAndMissingBranch()||UnclassifiedLastRoundNeverSilentlyContinues()||SubmittedCompletionOutlivesPhysicalInputLease()||
    CompletionObservationCannotHoldRebindOrInventReady()||SubmittedReleaseCannotBeRewoundByOlderHeldUpdate()||ReloadHandoffNeedsNoUnresolvedDebtOrOpenCallback()||
    HeldInterruptionUsesNativeOnlyBoundedAuthority()||HeldSuspensionRequiresEstablishedExactDebt()||SuspendedHoldNeverRenewsItsDeadlineOrOriginalCycle()||
    HeldSuspensionRequiresFreshSameOwnerNeutral()||HeldSuspensionKeepsNativeProofAndReadFailuresStrict()||SuspensionDoesNotRepublishInflightControllerLease()||ActualAdapterTrackingLossRegripsOriginalNativeDebt()||FreshNeutralDoesNotInvalidateInflightNativeOnlyObservation()||ConcurrentNativePublicationCannotEraseCurrentGesture()||FuturePublicationUsesOnlyExactIssuedLiveLease())return 1;
    std::puts("52 native cycle service groups passed, including 180 repeated same-weapon cycles; synthetic callback receipts.");return 0;}
namespace {
bool M95Hold(Fixture& f){
    for(unsigned n=0;n<3;++n)f.Shot(n,5);
    for(unsigned n=0;n<2;++n){auto r=f.Restore(n);f.service.ObserveRestore(r);f.input.branches[n].previousState=8;}
    for(unsigned pass=0;pass<2;++pass){f.Advance();for(unsigned n=0;n<3;++n){f.Begin(n);f.Finish();}}
    return f.service.View(f.clock).held.has_value();
}
int QueuedReleaseDoesNotReclassifyAnAlreadyHeldUpdate(){
    for(bool m95:{false,true})for(unsigned variant=0;variant<3;++variant){
        Fixture f(m95);CHECK(m95?M95Hold(f):f.Hold());const auto held=f.service.View(f.clock).held;CHECK(held);
        const auto originalObserved=f.control.input.observedNs;CHECK(f.Begin(0).hold);
        CHECK(f.update.entry.nowNs>originalObserved);
        f.control.input.nowNs=++f.clock;const auto submitted=f.clock;
        f.control.release=WeaponCycleRelease{*held,1,f.control.input.sequence,originalObserved,held->deadlineNs};
        const auto release=*f.control.release;CHECK(f.service.Control(f.control));
        if(variant==1)f.update.entry.nowNs=submitted+1;
        if(variant==2){
            // Idempotent resubmission cannot move the accepted-submission
            // boundary to legitimize a later held callback.
            f.control.input.nowNs=(f.clock+=10);CHECK(f.service.Control(f.control));f.update.entry.nowNs=submitted+1;
        }
        f.update.exit.nowNs=++f.clock;f.Finish();const auto view=f.service.View(f.clock);
        if(variant){CHECK(view.phase==Bc2NativeCyclePhase::Cancelled&&view.blocksFire&&!view.ready&&!view.held);continue;}
        CHECK(view.phase==Bc2NativeCyclePhase::Releasing&&view.blocksFire&&!view.held&&!view.ready);
        for(unsigned n=0;n<3;++n)f.Resume(n,m95);
        const auto ready=f.service.View(f.clock).ready;CHECK(ready&&ready->release==release);
        CHECK(ready->release.observedNs==originalObserved&&ready->release.deadlineNs==held->deadlineNs);
    }return 0;
}
ReloadFlowRecord ForwardReadyRestore(Fixture& f){
    auto r=f.Restore(0);r.entry.caller=0x89216b;
    auto& a=*r.exit.boundary;a.current=2;a.previous=8;a.next=2;a.timer=0;
    Put(r.entry.copiedSnapshot,0,2u);Put(r.entry.copiedSnapshot,4,2u);Put(r.entry.copiedSnapshot,8,0.f);
    r.exit.copiedSnapshot=r.entry.copiedSnapshot;return r;
}
int Actual239ForwardCallbackEvidence(){
    const auto identity=m95_forward239::Identity();const auto restore=m95_forward239::Record(2750);
    CHECK(!CyclePredictionRestore(restore,identity,0)&&CycleM95ForwardReadyRestore(restore,identity));
    CHECK(ValidateCycleUpdate(m95_forward239::Record(2745),identity,1));
    CHECK(CycleCommit(m95_forward239::Record(2746),identity,1,2745)&&CycleCommit(m95_forward239::Record(2747),identity,1,2745));
    CHECK(CycleM95ForwardIdleUpdate(m95_forward239::Record(2752),identity));
    auto idle=m95_forward239::Record(2752);idle.exit.boundary->flagsA8=0;
    CHECK(!CycleM95ForwardIdleUpdate(idle,identity));
    idle=m95_forward239::Record(2752);idle.exit.boundary->flagsA8=134;
    CHECK(!CycleM95ForwardIdleUpdate(idle,identity));
    auto changed=restore;changed.exit.copiedSnapshot[63]^=std::byte{1};
    CHECK(!CycleM95ForwardReadyRestore(changed,identity));return 0;
}
int M95ForwardRestoreRequiresOwnClientAndServerCompletion(){
    Fixture f(true);CHECK(M95Hold(f)&&f.Release());f.Resume(1,true);
    const auto forward=ForwardReadyRestore(f);CHECK(CycleM95ForwardReadyRestore(forward,f.input.identity));
    f.service.ObserveRestore(forward);CHECK(f.service.View(f.clock).phase==Bc2NativeCyclePhase::Releasing&&!f.service.View(f.clock).ready);
    // The copied snapshot cannot complete the client branch by itself.
    f.Resume(2,true);CHECK(!f.service.View(f.clock).ready);
    auto& a=f.input.branches[0];a.currentState=2;a.previousState=8;a.nextState=2;a.phaseTimer=0;
    f.Begin(0);f.Finish();const auto ready=f.service.View(f.clock).ready;CHECK(ready&&ready->unchangedAmmunition);
    // The next exact native2/8->2/2 Restore is legal only after that own idle
    // completion; generic prior8 in a ready restore is still unsupported.
    const auto settled=f.Restore(0);f.service.ObserveRestore(settled);
    CHECK(f.service.View(f.clock).ready&&f.service.AcknowledgeReady(*ready));return 0;
}
int M95ForwardRestoreDoesNotBorrowOrForgeReadiness(){
    for(unsigned variant=0;variant<9;++variant){Fixture f(true);CHECK(M95Hold(f)&&f.Release());
        if(variant!=0)f.Resume(1,true);
        auto r=ForwardReadyRestore(f);
        if(variant==1)r.entry.copiedSnapshot[0]=std::byte{8};
        if(variant==2)++r.exit.boundary->loaded;
        if(variant==3)++r.entry.boundary.owner.equipGeneration;
        if(variant==4)r.entry.boundary.previous=6;
        if(variant==5)r.entry.boundary.timer=0;
        if(variant==6)r.entry.boundary.branch=r.exit.boundary->branch=1;
        if(variant==7){r.entry.nowNs=f.control.release->observedNs-1;r.exit.nowNs=r.entry.nowNs+1;}
        if(variant==8){r.entry.nowNs+=4000000000ll;r.exit.nowNs=r.entry.nowNs+1;}
        f.service.ObserveRestore(r);CHECK(f.service.View(f.clock).phase==Bc2NativeCyclePhase::Cancelled&&!f.service.View(f.clock).ready);
    }
    Fixture f(true);CHECK(M95Hold(f)&&f.Release());f.Resume(1,true);const auto r=ForwardReadyRestore(f);f.service.ObserveRestore(r);
    f.service.ObserveRestore(r);CHECK(f.service.View(f.clock).phase==Bc2NativeCyclePhase::Cancelled);return 0;
}
int ExactM95ThreeBranchHoldAndTail(){
    Fixture f(true);CHECK(M95StockShotConfig(f.input.config)&&M95Hold(f));CHECK(!f.service.Configure(Bc2NativeCycleMode::Spas));
    const auto held=f.service.View(f.clock);CHECK(held.loaded==4&&held.reserve==45&&held.capacity==5);
    CHECK(f.Release());for(unsigned n=0;n<3;++n)f.Resume(n,true);
    const auto ready=f.service.View(f.clock);CHECK(ready.ready&&ready.blocksFire&&ready.loaded==4&&ready.reserve==45);
    CHECK(f.service.AcknowledgeReady(*ready.ready)&&!f.service.View(f.clock).blocksFire);return 0;
}
int M95CannotBorrowSpasBoundaryOrWrongConfig(){
    for(unsigned mutation=0;mutation<4;++mutation){Fixture f(true);
        if(mutation==0)f.input.config.boltTime=.5f;
        if(mutation==1)f.input.config.assetName[0]='x';
        if(mutation==2)f.input.config.holdBoltUntilFireRelease=false;
        if(mutation==3)f.input.config.reloadType=0;
        f.Begin(0);CHECK(!f.decision.tracked&&!f.decision.hold);f.Finish();CHECK(!f.service.View(f.clock).held);
    }
    Fixture f(true);for(unsigned n=0;n<3;++n)f.Shot(n,5);
    for(auto& b:f.input.branches){b.currentState=7;b.previousState=6;b.nextState=8;b.phaseTimer=.2f;}
    for(unsigned pass=0;pass<3;++pass){f.Advance();for(unsigned n=0;n<3;++n){f.Begin(n);CHECK(!f.decision.hold);f.Finish();}}
    CHECK(f.service.View(f.clock).blocksFire&&!f.service.View(f.clock).held);return 0;
}
int M95LastRoundRequiresSameEmptyDebtAndTail(){
    for(unsigned id:{4332u,4337u,4342u}){
        auto r=m95_empty281::Record(id);const auto identity=m95_empty281::Identity();
        CHECK(CycleEmptyShot(r,identity,r.entry.boundary.branch));
        CHECK(!CycleShot(r,identity,r.entry.boundary.branch));
        ++r.exit.boundary->reserve;CHECK(!CycleEmptyShot(r,identity,r.entry.boundary.branch));
    }
    Fixture f(true);CHECK(EmptyHold(f,.0833f));const auto held=f.service.View(f.clock);
    CHECK(held.loaded==0&&held.reserve==45&&held.blocksFire&&held.held);
    CHECK(f.Release());for(unsigned b=0;b<3;++b)EmptyResume(f,b);
    const auto ready=f.service.View(f.clock).ready;CHECK(ready&&ready->unchangedAmmunition);
    CHECK(f.service.AcknowledgeReady(*ready,f.clock));CHECK(!f.service.View(f.clock).blocksFire);
    // Native M95 magazine reload remains an explicitly unsupported manual
    // operation. It must not resurrect a completed bolt's guard or mint Ready.
    for(unsigned b=0;b<3;++b){
        f.input.branches[b].currentState=11;f.input.branches[b].nextState=12;
        f.input.branches[b].phaseTimer=.01f;f.Begin(b);
        f.update.exit.boundary->current=12;f.update.exit.boundary->next=1;
        f.update.exit.boundary->loaded=5;f.update.exit.boundary->reserve=40;f.Finish();
        f.input.branches[b].loaded=5;f.input.branches[b].reserve=40;
        if(b<2)f.service.ObserveRestore(f.Restore(b));
        CHECK(!f.service.View(f.clock).blocksFire&&!f.service.View(f.clock).ready);
    }
    for(auto& b:f.input.branches){b.currentState=8;b.previousState=7;b.nextState=1;b.phaseTimer=2.2f;b.loaded=4;}
    for(unsigned b=0;b<3;++b)f.Shot(b,5);
    for(unsigned pass=0;pass<2;++pass){CHECK(f.Advance());for(unsigned b=0;b<3;++b){f.Begin(b);f.Finish();}}
    const auto next=f.service.View(f.clock);CHECK(next.held&&next.shot==2&&next.loaded==4&&next.reserve==40);
    return 0;
}
int M95CompletionCoversOriginalBoltTimer(){
    Fixture f(true);CHECK(M95Hold(f)&&f.Release());const auto release=*f.control.release;
    f.clock+=2500000000ll;CHECK(f.service.CompletionWatch(f.clock));
    for(unsigned n=0;n<3;++n)f.Resume(n,true);
    auto ready=f.service.View(f.clock);CHECK(ready.ready&&ready.ready->release==release);
    Fixture expired(true);CHECK(M95Hold(expired)&&expired.Release());expired.clock+=3000000000ll;CHECK(!expired.service.CompletionWatch(expired.clock));return 0;
}
ReloadFlowRecord M95Rewind(Fixture& f,unsigned branch){
    auto r=f.Restore(branch);r.exit.boundary->current=6;r.exit.boundary->previous=8;r.exit.boundary->next=7;r.exit.boundary->timer=.000249032f;
    Put(r.entry.copiedSnapshot,0,6u);Put(r.entry.copiedSnapshot,4,7u);Put(r.entry.copiedSnapshot,8,r.exit.boundary->timer);
    r.exit.copiedSnapshot=r.entry.copiedSnapshot;return r;
}
int ExactOriginalPredictionRewindOnlyBeforeHold(){
    Fixture f(true);for(unsigned n=0;n<3;++n)f.Shot(n,5);
    auto rewind=M95Rewind(f,1);CHECK(CycleM95PreHoldRewind(rewind,f.input.identity,1));f.service.ObserveRestore(rewind);
    CHECK(f.service.View(f.clock).phase==Bc2NativeCyclePhase::ShotObserved&&!f.service.View(f.clock).held);
    f.Begin(1);f.Finish(); // Original Update independently reenters8.
    auto restore=f.Restore(1);f.service.ObserveRestore(restore);f.input.branches[1].previousState=8;
    restore=f.Restore(0);f.service.ObserveRestore(restore);f.input.branches[0].previousState=8;
    for(unsigned pass=0;pass<2;++pass){f.Advance();for(unsigned n=0;n<3;++n){f.Begin(n);f.Finish();}}
    CHECK(f.service.View(f.clock).held);
    rewind=M95Rewind(f,1);f.service.ObserveRestore(rewind);CHECK(f.service.View(f.clock).phase==Bc2NativeCyclePhase::Cancelled);return 0;
}
int PredictionRewindRequiresExactOriginalSnapshot(){
    for(unsigned mutation=0;mutation<9;++mutation){Fixture f(true);for(unsigned n=0;n<3;++n)f.Shot(n,5);auto r=M95Rewind(f,1);
        if(mutation==0)++r.exit.boundary->loaded;
        if(mutation==1)++r.exit.thread;
        if(mutation==2)r.entry.snapshotCopied=false;
        if(mutation==3)r.entry.copiedSnapshot[0]=std::byte{8};
        if(mutation==4)r.exit.hold.applied=true;
        if(mutation==5)r.entry.boundary.next=2;
        if(mutation==6)r.exit.boundary->timer=.2f;
        if(mutation==7)r.entry.nativeInvocation++;
        if(mutation==8)r.identityRetained=false;
        CHECK(!CycleM95PreHoldRewind(r,f.input.identity,1));f.service.ObserveRestore(r);CHECK(f.service.View(f.clock).phase==Bc2NativeCyclePhase::Cancelled);
    }return 0;
}
int M95TwoChildTailRejectsSpasExtraCommit(){Fixture f(true);CHECK(M95Hold(f)&&f.Release());f.Begin(0);f.Commit(0,7,8);
    CHECK(f.service.View(f.clock).phase==Bc2NativeCyclePhase::Cancelled&&!f.service.View(f.clock).ready);return 0;}
int M95SuspensionRequiresRightMechanismNeutral(){
    Fixture f(true);CHECK(M95Hold(f)&&f.service.SuspendHeld(f.clock));
    CHECK(f.Advance(false)&&f.service.View(f.clock).heldSuspended); // Left gun release cannot resume.
    f.clock+=10000000;auto& s=f.control.input;++s.sequence;s.observedNs=s.nowNs=f.clock;s.deadlineNs=f.clock+100000000;
    s.released={false,true};CHECK(f.service.Control(f.control));
    CHECK(!f.service.View(f.clock).heldSuspended);for(unsigned n=0;n<3;++n){f.Begin(n);f.Finish();}
    CHECK(f.service.View(f.clock).held);return 0;
}
int PhysicalBoltDiagnosticWireBounds(){
    CHECK(ValidM95PhysicalBoltSession(0,0,0,0,0,0,0,0));
    CHECK(ValidM95PhysicalBoltSession(1,0x197809,30000,0,0,0,0,0)&&ValidM95PhysicalBoltSession(2,0x197809,30000,0,0,0,0,0));
    for(unsigned n=0;n<8;++n){unsigned c=1,f=0x197809,d=30000,m=0,p=0,b=0,h=0,s=0;
        if(n==0)c=3;if(n==1)f^=1;if(n==2)d=29999;if(n==3)m=1;if(n==4)p=1;if(n==5)b=1;if(n==6)h=1;if(n==7)s=1;
        CHECK(!ValidM95PhysicalBoltSession(c,f,d,m,p,b,h,s));}return 0;
}
}
int SelectedModePrereadRequiresOriginalQuietRevision(){
    for(unsigned gap=0;gap<3;++gap){std::atomic<unsigned> active=0;std::atomic<std::uint64_t> revision=0;
        std::atomic_flag exclusive=ATOMIC_FLAG_INIT;
        CHECK(EnterReloadInvocation(active,revision,exclusive));const auto proof=revision.load();ExitReloadInvocation(active,revision);
        if(gap==1){CHECK(EnterReloadInvocation(active,revision,exclusive));ExitReloadInvocation(active,revision);}
        CHECK(EnterReloadInvocation(active,revision,exclusive));
        {ReloadInvocationExclusion boundary(exclusive,active,revision);CHECK(boundary.Quiet());
            if(gap==2){CHECK(!EnterReloadInvocation(active,revision,exclusive));ExitReloadInvocation(active,revision);}
            const bool accepted=boundary.Quiet()&&NativeCycleSelectionReadCurrent(proof,revision.load());
            CHECK(accepted==(gap==0));}
        ExitReloadInvocation(active,revision);
    }
    CHECK(!NativeCycleSelectionReadCurrent(UINT64_MAX-1,0));return 0;
}
Bc2NativeCycleSelection Selection(Fixture& f,Bc2NativeCycleMode mode,std::uint64_t sequence){
    return {mode,{f.input.identity,sequence,f.clock,f.clock+100000000,mode==Bc2NativeCycleMode::M95?5:8,
        mode==Bc2NativeCycleMode::M95?45:24,mode==Bc2NativeCycleMode::M95?5:8,true,true,true},f.input.config,f.clock};
}
Bc2NativeCycleSelection ReplacementActorSelection(Fixture& f,std::uint64_t sequence=100){
    auto selected=Selection(f,Bc2NativeCycleMode::Spas,sequence);auto& native=selected.reserve.identity;
    native.owner.player+=0x1000;native.owner.soldier+=0x1000;native.owner.weak+=0x1000;
    native.owner.weapon+=0x1000;++native.owner.actorGeneration;++native.owner.equipGeneration;
    native.serverPlayer+=0x1000;native.serverSoldier+=0x1000;native.serverItem+=0x1000;
    for(auto& firing:native.firing)firing+=0x1000;return selected;
}
Bc2NativeCycleActorDrain ActorDrain(Fixture& f,const Bc2NativeCycleSelection& selected,std::uint64_t revision=1){
    const auto old=f.service.View(selected.nowNs);
    return {old.native,old.cycle,old.shot,revision,selected.nowNs,selected.nowNs+Bc2NativeCycleService::ContextFreshNs,true};
}
int SameActorSeatEpochCannotRetireCancelledCycleDebt(){
    // Synthetic service composition only: gameplay changes actor/equip epochs
    // at seat boundaries, but this does not establish native vehicle behavior.
    // A newer presentation of the same soldier must not forgive an owed stroke
    // or turn a submitted, incomplete native tail into a completed transaction.
    for(bool m95:{false,true})for(bool submitted:{false,true}){
        Fixture f(m95);CHECK(m95?M95Hold(f):f.Hold());
        const auto held=*f.service.View(f.clock).held;
        if(submitted)CHECK(f.Release());
        const auto debt=f.service.View(f.clock);const auto originalControl=f.control;
        f.service.Cancel(Bc2NativeCycleFailure::Control);
        const auto cancelled=f.service.View(f.clock);
        Bc2NativeCycleSelection returned;
        for(unsigned boundary=1;boundary<=3;++boundary){
            f.clock+=10000000;
            f.service.Cancel(Bc2NativeCycleFailure::Owner);
            returned=Selection(f,m95?Bc2NativeCycleMode::M95:Bc2NativeCycleMode::Spas,100+boundary);
            returned.reserve.loaded=debt.loaded;returned.reserve.reserve=debt.reserve;
            auto& owner=returned.reserve.identity.owner;
            owner.actorGeneration+=2*boundary;owner.equipGeneration+=2*boundary;owner.space+=boundary;
            const auto drain=ActorDrain(f,returned,boundary);
            CHECK(!NativeCycleClientActorReplaced(debt.native.owner,owner));
            CHECK(!f.service.RetireForReplacementActor(returned.reserve,f.clock,drain));
            CHECK(!f.service.SelectReplacementActor(returned,drain)&&!f.service.Select(returned));
            auto control=originalControl;control.nativeOwner=owner;control.release.reset();
            control.input.owner.actorGeneration=owner.actorGeneration;control.input.owner.space=owner.space;
            control.input.owner.equipGeneration+=2*boundary;control.item.generation=control.input.owner.equipGeneration;
            control.mechanism.generation=control.item.generation;control.input.sequence+=boundary;
            control.input.observedNs=control.input.nowNs=f.clock;control.input.deadlineNs=f.clock+100000000;
            CHECK(!f.service.Control(control)&&!f.service.RebaseTracking(control,drain));
            control.release=WeaponCycleRelease{held,1,originalControl.input.sequence,held.observedNs,held.deadlineNs};
            CHECK(!f.service.Control(control)&&!f.service.YieldForReload());
            const auto after=f.service.View(f.clock);
            CHECK(after.phase==Bc2NativeCyclePhase::Cancelled&&after.blocksFire&&!after.held&&!after.ready);
            CHECK(after.native==debt.native&&after.cycle==debt.cycle&&after.shot==debt.shot&&
                after.loaded==debt.loaded&&after.reserve==debt.reserve&&after.capacity==debt.capacity);
            CHECK(after.failure==cancelled.failure&&after.failureInvocation==cancelled.failureInvocation&&
                !f.service.LastActorRetirement());
        }
        // The same fresh/drained proof becomes eligible only after actual
        // client and server actor replacement, and archives the old outcome.
        returned.reserve.identity.owner.soldier+=0x1000;returned.reserve.identity.owner.weak+=0x1000;
        returned.reserve.identity.serverSoldier+=0x1000;
        const auto retired=f.service.RetireForReplacementActor(returned.reserve,f.clock,ActorDrain(f,returned,4));
        CHECK(retired&&retired->outcome.native==debt.native&&retired->outcome.blocksFire&&
            retired->outcome.cycle==debt.cycle&&retired->outcome.shot==debt.shot);
        CHECK(retired->release.has_value()==submitted);
        if(submitted)CHECK(retired->release==originalControl.release);
        CHECK(!f.service.View(f.clock).blocksFire&&!f.service.View(f.clock).ready);
    }
    std::puts("CHAT06: 4 synthetic same-actor seat-epoch debt boundaries passed; no native vehicle acceptance.");
    return 0;
}
int ReplacementActorRifleFirstCanYieldWithoutArmingPump(){
    Fixture f;CHECK(f.Hold());const auto before=f.service.View(f.clock);
    f.service.Cancel(Bc2NativeCycleFailure::Owner);
    CHECK(!f.service.YieldForReload());
    auto rifle=ReplacementActorSelection(f);rifle.reserve.loaded=rifle.reserve.capacity=30;
    rifle.reserve.reserve=90;rifle.config.reloadType=1;rifle.config.baseCapacity=30;
    const auto drain=ActorDrain(f,rifle);
    // A rifle is not a selected SPAS operating class. Its exact fresh idle
    // owner can retire the old actor without granting pump/bolt authority.
    CHECK(!f.service.Select(rifle)&&!f.service.SelectReplacementActor(rifle,drain));
    const auto retired=f.service.RetireForReplacementActor(rifle.reserve,rifle.nowNs,drain);
    CHECK(retired&&retired->outcome.phase==Bc2NativeCyclePhase::Cancelled);
    CHECK(retired->outcome.native==before.native&&retired->outcome.cycle==before.cycle&&retired->outcome.shot==before.shot);
    const auto current=f.service.View(f.clock);
    CHECK(current.phase==Bc2NativeCyclePhase::Watching&&!current.selectedMode&&!current.native.owner.player);
    CHECK(!current.blocksFire&&!current.ready&&!current.held);
    CHECK(current.cycle==before.cycle&&current.shot==before.shot);
    CHECK(!f.service.Control(f.control));
    auto newControl=f.control;newControl.nativeOwner=rifle.reserve.identity.owner;
    const auto& n=newControl.nativeOwner;newControl.item.id=n.weapon;
    newControl.input.owner.actor=(std::uint64_t(n.weak)<<32)|n.soldier;
    newControl.input.owner.actorGeneration=n.actorGeneration;
    CHECK(!f.service.Control(newControl)); // Fresh actor still has no operating class.
    CHECK(!f.Begin(0).tracked);f.Finish(); // A delayed old callback grants no authority.
    CHECK(f.service.YieldForReload()); // The resource family can now take ownership.
    CHECK(!f.service.RetireForReplacementActor(rifle.reserve,rifle.nowNs,drain));
    auto pump=rifle;pump.config=f.input.config;pump.reserve.loaded=pump.reserve.capacity=8;
    pump.reserve.reserve=24;
    CHECK(!f.service.Select(pump)); // Retirement consumed this reserve publication.
    ++pump.reserve.sequence;CHECK(f.service.Select(pump));
    CHECK(f.service.View(f.clock).selectedOwner==rifle.reserve.identity.owner);
    CHECK(f.service.LastActorRetirement()->event==retired->event);return 0;
}
int FamilyNeutralRetirementRequiresExactFreshActorAndDrain(){
    for(unsigned fault=0;fault<27;++fault){Fixture f;CHECK(f.Hold());
        auto replacement=ReplacementActorSelection(f);auto& r=replacement.reserve;
        r.capacity=r.loaded=30;r.reserve=90;
        auto drain=ActorDrain(f,replacement);const auto before=f.service.View(f.clock);
        if(fault==0)drain.callbacksDrained=false;
        if(fault==1)drain.deadlineNs=replacement.nowNs;
        if(fault==2)drain.observedNs=replacement.nowNs+1;
        if(fault==3)++drain.expected.owner.weak;
        if(fault==4)++drain.cycle;
        if(fault==5)++drain.shot;
        if(fault==6){r.identity.owner.soldier=before.native.owner.soldier;r.identity.owner.weak=before.native.owner.weak;}
        if(fault==7)r.identity.owner.actorGeneration=before.native.owner.actorGeneration;
        if(fault==8)r.identity.serverSoldier=before.native.serverSoldier;
        if(fault==9)r.allThreeIdle=false;
        if(fault==10)r.verified=false;
        if(fault==11)++drain.deadlineNs;
        if(fault==12)--drain.observedNs;
        if(fault==13)r.deadlineNs=drain.deadlineNs-1;
        if(fault==14)drain.callbackRevision=0;
        if(fault==15)replacement.nowNs=r.deadlineNs;
        if(fault==16)r.sequence=0;
        if(fault==17)r.identity.firing[1]=r.identity.firing[0];
        if(fault==18)r.identity.serverItem=0;
        if(fault==19)r.loaded=r.capacity+1;
        if(fault==20)r.reserve=-1;
        if(fault==21)r.capacity=0;
        if(fault==22)r.identity.owner.equipGeneration=0;
        if(fault==23)r.identity.owner.space=0;
        if(fault==24)r.identity.owner.actorGeneration=0;
        if(fault==25)r.deadlineNs=r.observedNs+200000001;
        if(fault==26)r.observedNs=replacement.nowNs+1;
        CHECK(!f.service.RetireForReplacementActor(r,replacement.nowNs,drain));
        const auto after=f.service.View(f.clock);
        CHECK(after.phase==before.phase&&after.native==before.native&&after.cycle==before.cycle&&after.shot==before.shot&&
            after.held&&after.held->sequence==before.held->sequence&&after.blocksFire&&!f.service.LastActorRetirement());
    }
    // Monotonic sequence and time bounds apply equally to neutral retirement;
    // another client/server lifetime does not make an old proof fresh again.
    Fixture f;CHECK(f.Hold());auto first=ReplacementActorSelection(f);
    auto firstDrain=ActorDrain(f,first,10);CHECK(f.service.SelectReplacementActor(first,firstDrain));
    f.input.identity=first.reserve.identity;++f.clock;auto next=ReplacementActorSelection(f,101);
    auto nextDrain=ActorDrain(f,next,11);
    auto stale=next.reserve;stale.sequence=first.reserve.sequence;
    CHECK(!f.service.RetireForReplacementActor(stale,next.nowNs,nextDrain));
    auto oldRevision=nextDrain;oldRevision.callbackRevision=firstDrain.callbackRevision;
    CHECK(!f.service.RetireForReplacementActor(next.reserve,next.nowNs,oldRevision));
    auto reversed=next;reversed.nowNs=first.nowNs-1;reversed.reserve.observedNs=reversed.nowNs;
    auto reversedDrain=nextDrain;reversedDrain.observedNs=reversed.nowNs;
    CHECK(!f.service.RetireForReplacementActor(reversed.reserve,reversed.nowNs,reversedDrain));
    const auto retired=f.service.RetireForReplacementActor(next.reserve,next.nowNs,nextDrain);
    CHECK(retired&&retired->event==2&&!f.service.View(f.clock).selectedMode);
    CHECK(!f.service.SelectReplacementActor(first,firstDrain));return 0;
}
int FamilyNeutralRetirementArchivesOldReceipts(){
    for(unsigned stage=0;stage<4;++stage){Fixture f;
        if(stage==3){f.service=Bc2NativeCycleService{};CHECK(f.service.EnableRecovery());CHECK(f.Advance());}
        CHECK(f.Hold());
        if(stage==1)CHECK(f.Release());
        if(stage==2){CHECK(f.FullStroke());for(unsigned branch=0;branch<3;++branch)f.Resume(branch);CHECK(f.service.View(f.clock).ready);}
        if(stage==3){CHECK(f.service.BeginRecovery(f.clock));f.service.Cancel(Bc2NativeCycleFailure::Owner);}
        const auto before=f.service.View(f.clock);const auto release=f.control.release;
        auto replacement=ReplacementActorSelection(f);replacement.reserve.loaded=replacement.reserve.capacity=30;
        const auto retired=f.service.RetireForReplacementActor(replacement.reserve,replacement.nowNs,ActorDrain(f,replacement));
        CHECK(retired&&retired->outcome.phase==before.phase&&retired->outcome.native==before.native);
        CHECK(retired->outcome.cycle==before.cycle&&retired->outcome.shot==before.shot);
        CHECK(retired->outcome.ready.has_value()==before.ready.has_value());
        if(release)CHECK(retired->release==release);
        if(before.ready){CHECK(retired->outcome.ready->release==before.ready->release);
            CHECK(!f.service.AcknowledgeReady(*before.ready));}
        if(stage==3)CHECK(retired->recovery&&retired->recoveryUncertain);
        CHECK(!f.service.Control(f.control)&&f.service.YieldForReload());
        CHECK(f.service.LastActorRetirement()->event==retired->event);
        const auto after=f.service.View(f.clock);CHECK(!after.ready&&!after.held&&!after.selectedMode&&!after.blocksFire);
        CHECK(after.cycle==before.cycle&&after.shot==before.shot);
    }return 0;
}
int ConfirmedReplacementActorDoesNotInheritOldPhysicalPumpDebt(){
    auto a=std::make_unique<InterruptedAdapter>();CHECK(a->Grip());
    const auto old=a->f.service.View(a->f.clock);CHECK(old.held&&a->pump.BlocksFire());
    a->Tick(.02f);a->Tick(.02f,true,false);
    // A same-actor tracking gap must retain the owed action. This is not the
    // replacement-actor evidence used below, and must never clear its debt.
    CHECK(a->pump.BlocksFire()&&a->f.service.ObservationWatch(a->f.clock));
    CHECK(!a->submissions&&!a->acknowledgements);
    a->f.service.Cancel(Bc2NativeCycleFailure::Owner);
    CHECK(a->f.service.View(a->f.clock).phase==Bc2NativeCyclePhase::Cancelled);
    Fixture replacement;replacement.clock=a->f.clock+10000000;
    auto selected=ReplacementActorSelection(replacement);const auto& native=selected.reserve.identity;
    // Ordinary selection must still refuse unresolved old debt. Only a fresh
    // replacement actor plus the runtime's callback-drain proof may retire it.
    CHECK(!a->f.service.Select(selected));
    CHECK(NativeCycleClientActorReplaced(old.native.owner,native.owner));
    const auto drain=ActorDrain(a->f,selected);
    const auto retired=a->f.service.SelectReplacementActor(selected,drain);CHECK(retired);
    CHECK(retired->outcome.native==old.native&&retired->outcome.cycle==old.cycle&&retired->outcome.shot==old.shot);
    CHECK(retired->outcome.phase==Bc2NativeCyclePhase::Cancelled&&!retired->outcome.ready&&!retired->release);
    CHECK(a->f.service.View(selected.nowNs).selectedOwner==native.owner);
    CHECK(!a->f.service.View(selected.nowNs).blocksFire);
    Bc2PhysicalPumpSample sample;sample.nativeOwner=native.owner;sample.asset=SpasReloadAsset;
    sample.item={native.owner.weapon,a->f.control.item.generation+1};sample.input=a->f.control.input;
    sample.input.owner={(std::uint64_t(native.owner.weak)<<32)|native.owner.soldier,
        native.owner.actorGeneration,sample.item.generation,native.owner.space};
    ++sample.input.sequence;sample.input.nowNs=sample.input.observedNs=selected.nowNs;
    sample.input.deadlineNs=selected.nowNs+100000000;sample.input.focused=true;
    sample.input.tracked={true,true};sample.input.released={true,false};
    a->f.hands.Reset();a->f.hands.Update(sample.input);
    const auto gun=a->f.hands.Acquire(sample.input,{sample.input.owner,InteractionHand::Right,
        HandClaimKind::GunHold,sample.item,{{1,sample.item.generation},sample.input.sequence,sample.input.deadlineNs,true},++a->f.intent,0});
    CHECK(gun.claim);
    // Gameplay reconstructs the physical adapter only after native retirement
    // succeeds. It never ticks the old adapter under the replacement selection.
    CHECK(a->pump.BlocksFire());Bc2PhysicalPump current(a->calibration,a->Api());
    const auto result=current.Tick(sample,a->f.hands,a->f.intent);
    CHECK(!result.ownsHand&&!result.tracking.target&&!a->submissions&&!a->acknowledgements);
    CHECK(!result.blocksFire&&!current.BlocksFire());
    CHECK(!a->f.service.SelectReplacementActor(selected,drain));
    CHECK(!a->f.service.Control(a->f.control)); // Old actor cannot return.
    // Real service cycle/shot/lease counters must advance beyond the retired
    // actor even though fresh native identity and physical claims were rebuilt.
    replacement.service=a->f.service;replacement.input.identity=native;
    for(unsigned branch=0;branch<3;++branch)replacement.input.branches[branch].address=native.firing[branch];
    replacement.control.nativeOwner=native.owner;replacement.control.input=sample.input;
    replacement.control.item=sample.item;replacement.control.mechanism={0x5350415350554d50ull,sample.item.generation};
    replacement.event=a->f.event;CHECK(replacement.Advance());CHECK(replacement.Hold());
    const auto next=replacement.service.View(replacement.clock);
    CHECK(next.held&&next.cycle>old.cycle&&next.shot>old.shot&&next.held->sequence>old.held->sequence);
    CHECK(replacement.service.LastActorRetirement()->event==retired->event);return 0;
}
int ReplacementActorRequiresExactFreshDrainedProof(){
    CHECK(!ReplacementActorRifleFirstCanYieldWithoutArmingPump());
    CHECK(!FamilyNeutralRetirementRequiresExactFreshActorAndDrain());
    CHECK(!FamilyNeutralRetirementArchivesOldReceipts());
    for(unsigned fault=0;fault<16;++fault){Fixture f;CHECK(f.Hold());
        auto selected=ReplacementActorSelection(f);auto drain=ActorDrain(f,selected);const auto before=f.service.View(f.clock);
        if(fault==0)drain.callbacksDrained=false;
        if(fault==1)drain.deadlineNs=selected.nowNs;
        if(fault==2)drain.observedNs=selected.nowNs+1;
        if(fault==3)++drain.expected.owner.weak;
        if(fault==4)++drain.cycle;
        if(fault==5)++drain.shot;
        if(fault==6){selected.reserve.identity.owner.soldier=before.native.owner.soldier;selected.reserve.identity.owner.weak=before.native.owner.weak;}
        if(fault==7)selected.reserve.identity.owner.actorGeneration=before.native.owner.actorGeneration;
        if(fault==8)selected.reserve.identity.serverSoldier=before.native.serverSoldier;
        if(fault==9)selected.reserve.allThreeIdle=false;
        if(fault==10)selected.config.boltTime=42;
        if(fault==11)++drain.deadlineNs;
        if(fault==12)--drain.observedNs;
        if(fault==13)selected.reserve.deadlineNs=drain.deadlineNs-1;
        if(fault==14)drain.callbackRevision=0;
        if(fault==15)selected.nowNs=selected.reserve.deadlineNs;
        CHECK(!f.service.SelectReplacementActor(selected,drain));const auto after=f.service.View(f.clock);
        CHECK(after.phase==before.phase&&after.native==before.native&&after.cycle==before.cycle&&after.shot==before.shot&&
            after.held&&after.held->sequence==before.held->sequence&&after.blocksFire&&!f.service.LastActorRetirement());
    }
    Fixture f;auto same=f.input.identity.owner;
    ++same.actorGeneration;++same.equipGeneration;++same.space;++same.weapon;++same.player;
    CHECK(!NativeCycleClientActorReplaced(f.input.identity.owner,same));
    CHECK(!NativeCycleClientActorReplaced({},same));
    ++same.weak;CHECK(NativeCycleClientActorReplaced(f.input.identity.owner,same));return 0;
}
int ReplacementActorArchivesPendingAndCompletedOutcomes(){
    for(unsigned stage=0;stage<3;++stage){Fixture f;CHECK(f.Hold());
        if(stage==0)CHECK(f.Release());else {CHECK(f.FullStroke());for(unsigned branch=0;branch<3;++branch)f.Resume(branch);CHECK(f.service.View(f.clock).ready);}
        const auto before=f.service.View(f.clock);const auto release=stage==0?*f.control.release:before.ready->release;
        if(stage==1){f.service.Cancel(Bc2NativeCycleFailure::Owner);CHECK(f.service.View(f.clock).native==before.native&&f.service.View(f.clock).ready);}
        auto selected=ReplacementActorSelection(f);auto drain=ActorDrain(f,selected,10);
        const auto retired=f.service.SelectReplacementActor(selected,drain);CHECK(retired&&retired->release);
        CHECK(*retired->release==release&&retired->outcome.phase==before.phase&&retired->outcome.native==before.native);
        CHECK(retired->outcome.ready.has_value()==before.ready.has_value());
        if(before.ready){CHECK(retired->outcome.ready->release==before.ready->release&&
            retired->outcome.ready->observedNs==before.ready->observedNs&&retired->outcome.ready->deadlineNs==before.ready->deadlineNs);
            CHECK(!f.service.AcknowledgeReady(*before.ready));}
        const auto current=f.service.View(f.clock);CHECK(!current.blocksFire&&!current.ready&&!current.held);
        CHECK(current.cycle==before.cycle&&current.shot==before.shot);
        if(stage==2){
            // Even a correctly rebound second replacement cannot reuse the
            // prior callback-drain revision or selection publication sequence.
            f.input.identity=selected.reserve.identity;++f.clock;
            auto again=ReplacementActorSelection(f,101);auto nextDrain=ActorDrain(f,again,10);
            CHECK(!f.service.SelectReplacementActor(again,nextDrain));
            nextDrain.callbackRevision=11;CHECK(f.service.SelectReplacementActor(again,nextDrain));
            CHECK(f.service.LastActorRetirement()->event==2);
        }
    }return 0;
}
int SelectedModeRequiresExactOriginalIdleProof(){
    for(unsigned mutation=0;mutation<15;++mutation){Fixture f;auto selected=Selection(f,Bc2NativeCycleMode::Spas,1);
        switch(mutation){case 0: selected.reserve.verified=false;break;case 1:selected.reserve.allThreeIdle=false;break;
        case 2:selected.nowNs=selected.reserve.deadlineNs;break;case 3:selected.reserve.observedNs=selected.nowNs+1;break;
        case 4:selected.reserve.deadlineNs=selected.reserve.observedNs+200000001;break;case 5:selected.reserve.identity.firing[1]=selected.reserve.identity.firing[0];break;
        case 6:selected.reserve.identity.serverItem=0;break;case 7:selected.config.boltTime=2.3f;break;case 8:selected.mode=Bc2NativeCycleMode::M95;break;
        case 9:selected.reserve.sequence=0;break;case 10:selected.reserve.loaded=9;break;case 11:selected.reserve.reserve=-1;break;
        case 12:selected.reserve.identity.owner.equipGeneration=0;break;case 13:selected.reserve.capacity=0;break;case 14:selected.mode=static_cast<Bc2NativeCycleMode>(2);break;}
        CHECK(!f.service.Select(selected));CHECK(f.service.Mode()==Bc2NativeCycleMode::Spas&&!f.service.View(f.clock).blocksFire);
    }
    Fixture f;auto selected=Selection(f,Bc2NativeCycleMode::Spas,1);CHECK(f.service.Select(selected));
    CHECK(!f.service.Select(selected));CHECK(f.Advance());CHECK(f.Begin(0).tracked);f.Finish();
    auto other=f.control;++other.nativeOwner.weapon;CHECK(!f.service.Control(other));
    CHECK(f.service.YieldForReload());CHECK(!f.Advance());selected=Selection(f,Bc2NativeCycleMode::Spas,2);
    CHECK(f.service.Select(selected)&&f.Advance());return 0;
}
int SelectedModeCannotErasePendingDebt(){
    for(unsigned phase=0;phase<5;++phase){Fixture f;Fixture target(true);CHECK(f.Hold());
        if(phase>=1){CHECK(f.Release());for(unsigned n=0;n<3;++n)f.Resume(n);}
        if(phase==2){const auto ready=f.service.View(f.clock).ready;CHECK(ready&&f.service.AcknowledgeReady(*ready));f.Begin(0);}
        if(phase==3)f.service.Cancel(Bc2NativeCycleFailure::State);
        if(phase==4){const auto ready=f.service.View(f.clock).ready;CHECK(ready&&f.service.AcknowledgeReady(*ready));
            auto corrupt=f.Restore(0);corrupt.exit.copiedSnapshot[0]=std::byte{3};f.service.ObserveRestore(corrupt);}
        target.clock=f.clock+1;const auto before=f.service.View(f.clock);CHECK(!f.service.Select(Selection(target,Bc2NativeCycleMode::M95,100)));
        const auto after=f.service.View(f.clock);CHECK(before.phase==after.phase&&before.cycle==after.cycle&&before.shot==after.shot&&
            before.ready.has_value()==after.ready.has_value()&&f.service.Mode()==Bc2NativeCycleMode::Spas);
    }return 0;
}
int SelectedModeRoundTripPreservesCycleAuthority(){
    Fixture pump;CHECK(pump.service.Select(Selection(pump,Bc2NativeCycleMode::Spas,1))&&pump.Advance());
    CHECK(pump.Hold());const auto first=*pump.service.View(pump.clock).held;CHECK(pump.Release());for(unsigned n=0;n<3;++n)pump.Resume(n);
    CHECK(pump.service.AcknowledgeReady(*pump.service.View(pump.clock).ready));
    Fixture bolt(true);bolt.clock=pump.clock+100;bolt.event=pump.event;
    ++bolt.input.identity.owner.weapon;++bolt.input.identity.owner.equipGeneration;
    bolt.control.nativeOwner=bolt.input.identity.owner;bolt.control.item.id=bolt.input.identity.owner.weapon;
    bolt.service=pump.service;CHECK(bolt.service.Select(Selection(bolt,Bc2NativeCycleMode::M95,2))&&bolt.Advance());
    CHECK(M95Hold(bolt));const auto second=*bolt.service.View(bolt.clock).held;
    CHECK(second.cycle>first.cycle&&second.shot>first.shot&&second.sequence>first.sequence);
    CHECK(bolt.Release());for(unsigned n=0;n<3;++n)bolt.Resume(n,true);
    CHECK(bolt.service.AcknowledgeReady(*bolt.service.View(bolt.clock).ready));
    Fixture returned;returned.clock=bolt.clock+100;returned.event=bolt.event;returned.service=bolt.service;
    CHECK(returned.service.Select(Selection(returned,Bc2NativeCycleMode::Spas,3))&&returned.Advance()&&returned.Hold());
    const auto third=*returned.service.View(returned.clock).held;
    CHECK(third.cycle>second.cycle&&third.shot>second.shot&&third.sequence>second.sequence);
    // The old exact release belongs to the prior physical and native owner.
    returned.control.release=bolt.control.release;CHECK(!returned.service.Control(returned.control));
    CHECK(returned.service.View(returned.clock).phase==Bc2NativeCyclePhase::Held);return 0;
}
ReloadFlowRecord ForwardToHold(Fixture& f,unsigned branch){
    const bool m95=f.service.Mode()==Bc2NativeCycleMode::M95;
    auto r=f.Restore(branch);r.exit.boundary->current=m95?8:7;r.exit.boundary->previous=6;r.exit.boundary->next=m95?1:8;r.exit.boundary->timer=m95?2.29903f:.498921f;
    Put(r.entry.copiedSnapshot,0,r.exit.boundary->current);Put(r.entry.copiedSnapshot,4,r.exit.boundary->next);Put(r.entry.copiedSnapshot,8,r.exit.boundary->timer);
    r.exit.copiedSnapshot=r.entry.copiedSnapshot;return r;
}
void M95PostShotWait(Fixture& f){
    for(auto& b:f.input.branches){b.currentState=6;b.previousState=5;b.nextState=7;b.phaseTimer=.05f;}
    for(unsigned n=0;n<3;++n)f.Shot(n,f.input.capacities[n]);
}
int Actual241SnapshotProgressionEvidence(){
    const auto id=m95_progression241::Identity();const auto restore=m95_progression241::Record(2903);
    const NativeCycleHeldBoundary boundary{8,7,1,.1f,2.3f};
    CHECK(!CyclePredictionRestore(restore,id,0)&&CyclePreHoldSnapshotProgression(restore,id,0,boundary,.1f));
    CHECK(ValidateCycleUpdate(m95_progression241::Record(2900),id,1));
    CHECK(CycleCommit(m95_progression241::Record(2901),id,1,2900)&&CycleCommit(m95_progression241::Record(2902),id,1,2900));
    CHECK(ValidateCycleUpdate(m95_progression241::Record(2904),id,0));
    for(unsigned mutation=0;mutation<8;++mutation){auto bad=restore;
        if(mutation==0)bad.exit.copiedSnapshot[63]^=std::byte{1};
        if(mutation==1)++bad.exit.boundary->reserve;
        if(mutation==2)bad.exit.boundary->previous=7;
        if(mutation==3)bad.entry.boundary.current=2;
        if(mutation==4)bad.entry.boundary.timer=.2f;
        if(mutation==5)bad.entry.nativeInvocation++;
        if(mutation==6)bad.exit.boundary->owner.weapon++;
        if(mutation==7)bad.exit.hold.applied=true;
        CHECK(!CyclePreHoldSnapshotProgression(bad,id,0,boundary,.1f));
    }return 0;
}
int SnapshotProgressionRetainsMeasuredPreviousUntilOwnHold(){
    Fixture f(true);M95PostShotWait(f);auto r=ForwardToHold(f,0);f.service.ObserveRestore(r);
    CHECK(f.service.View(f.clock).phase==Bc2NativeCyclePhase::ShotObserved&&!f.service.View(f.clock).held&&!f.service.View(f.clock).ready);
    auto& client=f.input.branches[0];client.currentState=8;client.previousState=6;client.nextState=1;client.phaseTimer=2.29903f;
    for(unsigned n=1;n<3;++n){f.Begin(n);auto& after=*f.update.exit.boundary;
        after.current=8;after.previous=7;after.next=1;after.timer=2.29903f;f.Finish();
        auto& b=f.input.branches[n];b.currentState=8;b.previousState=7;b.nextState=1;b.phaseTimer=after.timer;}
    for(unsigned pass=0;pass<2;++pass){f.Advance();for(unsigned n=0;n<3;++n){f.Begin(n);f.Finish();}}
    CHECK(f.service.View(f.clock).held&&f.service.View(f.clock).blocksFire);
    auto normalize=f.Restore(0);f.service.ObserveRestore(normalize);client.previousState=8;
    CHECK(f.service.View(f.clock).held);f.Advance();f.Begin(0);CHECK(f.decision.hold);f.Finish();
    CHECK(f.service.View(f.clock).held);return 0;
}
int SnapshotProgressionCannotEraseStartedInterception(){
    for(bool m95:{false,true}){
    {Fixture f(m95);M95PostShotWait(f);auto r=ForwardToHold(f,0);f.service.ObserveRestore(r);f.service.ObserveRestore(r);
        CHECK(f.service.View(f.clock).phase==Bc2NativeCyclePhase::Cancelled);}
    {Fixture f(m95);CHECK(m95?M95Hold(f):f.Hold());auto r=f.Restore(0);r.exit.boundary->current=6;r.exit.boundary->next=7;r.exit.boundary->timer=.01f;
        Put(r.entry.copiedSnapshot,0,6u);Put(r.entry.copiedSnapshot,4,7u);Put(r.entry.copiedSnapshot,8,.01f);r.exit.copiedSnapshot=r.entry.copiedSnapshot;
        f.service.ObserveRestore(r);CHECK(f.service.View(f.clock).phase==Bc2NativeCyclePhase::Cancelled&&!f.service.View(f.clock).ready);}
    {Fixture f(m95);M95PostShotWait(f);f.Begin(0);const auto r=ForwardToHold(f,0);f.service.ObserveRestore(r);
        CHECK(f.service.View(f.clock).phase==Bc2NativeCyclePhase::Cancelled);}
    }
    return 0;
}
int SpasSnapshotProgressionUsesSharedCyclePolicy(){
    const auto actual=spas_progression251::Record();const auto identity=spas_progression251::Identity();
    const NativeCycleHeldBoundary boundary{7,6,8,.1f,.5f};
    CHECK(CyclePreHoldSnapshotProgression(actual,identity,1,boundary,.1f));
    CHECK(!CyclePredictionRestore(actual,identity,1));
    for(unsigned branch=0;branch<2;++branch){Fixture f;
        for(auto& b:f.input.branches){b.currentState=6;b.previousState=5;b.nextState=7;b.phaseTimer=.05f;}
        for(unsigned n=0;n<3;++n)f.Shot(n);
        auto r=f.Restore(branch);r.entry.boundary.timer=.000529684f;
        r.exit.boundary->current=7;r.exit.boundary->previous=6;r.exit.boundary->next=8;r.exit.boundary->timer=.498921f;
        Put(r.entry.copiedSnapshot,0,7u);Put(r.entry.copiedSnapshot,4,8u);Put(r.entry.copiedSnapshot,8,.498921f);
        r.exit.copiedSnapshot=r.entry.copiedSnapshot;f.service.ObserveRestore(r);
        CHECK(f.service.View(f.clock).phase==Bc2NativeCyclePhase::ShotObserved);
        CHECK(!f.service.View(f.clock).held&&!f.service.View(f.clock).ready&&f.service.View(f.clock).blocksFire);
        auto& client=f.input.branches[branch];client.currentState=7;client.previousState=6;client.nextState=8;client.phaseTimer=.498921f;
        for(unsigned n=0;n<3;++n)if(n!=branch){f.Begin(n);auto& after=*f.update.exit.boundary;
            after.current=7;after.previous=6;after.next=8;after.timer=.498921f;f.Finish();
            auto& b=f.input.branches[n];b.currentState=7;b.previousState=6;b.nextState=8;b.phaseTimer=.498921f;}
        for(unsigned pass=0;pass<2;++pass){f.Advance();for(unsigned n=0;n<3;++n){f.Begin(n);f.Finish();}}
        CHECK(f.service.View(f.clock).held&&f.service.View(f.clock).blocksFire);
        CHECK(f.Release());for(unsigned n=0;n<3;++n)f.Resume(n);
        const auto ready=f.service.View(f.clock).ready;CHECK(ready&&f.service.AcknowledgeReady(*ready));
        CHECK(!f.service.View(f.clock).blocksFire&&f.service.View(f.clock).loaded==7&&f.service.View(f.clock).reserve==24);
    }
    for(unsigned mutation=0;mutation<8;++mutation){auto bad=actual;
        if(mutation==0)++bad.exit.boundary->loaded;if(mutation==1)++bad.exit.boundary->reserve;
        if(mutation==2)bad.exit.boundary->previous=7;if(mutation==3)bad.entry.boundary.timer=.2f;
        if(mutation==4)bad.exit.copiedSnapshot[63]^=std::byte{1};if(mutation==5)++bad.entry.nativeInvocation;
        if(mutation==6)++bad.exit.boundary->owner.weapon;if(mutation==7)bad.exit.hold.applied=true;
        CHECK(!CyclePreHoldSnapshotProgression(bad,identity,1,boundary,.1f));
    }return 0;
}

ReloadFlowRecord PredictedShotRollback(Fixture& f,unsigned branch){
    auto r=f.Restore(branch);auto& z=*r.exit.boundary;
    z.current=z.next=2;z.previous=6;z.timer=0;z.loaded=r.entry.boundary.loaded+1;
    Put(r.entry.copiedSnapshot,0,2u);Put(r.entry.copiedSnapshot,4,2u);Put(r.entry.copiedSnapshot,8,0.f);
    Put(r.entry.copiedSnapshot,0x18,z.loaded);r.exit.copiedSnapshot=r.entry.copiedSnapshot;return r;
}
int PredictedShotReplayPreservesOneDebt(){
    const auto actual=m95_replay253::Record();
    CHECK(CyclePredictedShotRollback(actual,m95_replay253::Identity(),1));
    const auto emptyActual=m95_empty_replay292::Record(8295);
    const auto emptyIdentity=m95_empty_replay292::Identity();
    CHECK(!CyclePredictedShotRollback(emptyActual,emptyIdentity,1));
    CHECK(CyclePredictedShotRollback(emptyActual,emptyIdentity,1,true));
    CHECK(CycleEmptyShot(m95_empty_replay292::Record(8290),emptyIdentity,1));
    CHECK(CycleEmptyShot(m95_empty_replay292::Record(8296),emptyIdentity,1));
    for(unsigned variant=0;variant<4;++variant){auto bad=emptyActual;
        if(variant==0)bad.entry.boundary.next=7;
        if(variant==1)++bad.entry.boundary.loaded;
        if(variant==2)++bad.exit.boundary->reserve;
        if(variant==3)bad.exit.copiedSnapshot[63]^=std::byte{1};
        CHECK(!CyclePredictedShotRollback(bad,emptyIdentity,1,true));
    }
    for(bool m95:{false,true})for(bool empty:{false,true})for(unsigned branch=0;branch<2;++branch){
        Fixture f(m95);const int initial=empty?1:f.input.capacities[0];
        for(auto& b:f.input.branches){b.loaded=initial-1;b.currentState=6;b.previousState=5;b.nextState=empty?1:7;b.phaseTimer=.116971f;}
        f.Shot(0,initial);f.Shot(1,initial);
        const auto original=f.service.View(f.clock);CHECK(original.cycle==1&&original.shot==1&&!original.held);
        const auto rollback=PredictedShotRollback(f,branch);f.service.ObserveRestore(rollback);
        CHECK(f.service.View(f.clock).phase==Bc2NativeCyclePhase::ShotObserved);
        CHECK(f.service.View(f.clock).cycle==1&&f.service.View(f.clock).blocksFire&&!f.service.View(f.clock).ready);
        // Same native client replays the original shot; no new physical debt.
        f.Shot(branch,initial);
        const auto again=PredictedShotRollback(f,branch);f.service.ObserveRestore(again);
        CHECK(f.service.View(f.clock).phase==Bc2NativeCyclePhase::ShotObserved);
        f.Shot(branch,initial);f.Shot(2,initial);
        CHECK(f.service.View(f.clock).phase==Bc2NativeCyclePhase::ShotObserved);
        for(auto& b:f.input.branches){b.currentState=empty?6:m95?8:7;b.previousState=empty?5:m95?7:6;b.nextState=empty||m95?1:8;b.phaseTimer=empty?.2f:m95?2.2f:.2f;}
        for(unsigned pass=0;pass<2;++pass){f.Advance();for(unsigned n=0;n<3;++n){f.Begin(n);f.Finish();}}
        const auto held=f.service.View(f.clock);CHECK(held.held&&held.cycle==1&&held.shot==1&&held.loaded==initial-1);
        CHECK(!held.ready&&held.blocksFire);
        // A rollback after interception has started cannot retire those holds.
        auto late=PredictedShotRollback(f,branch);f.service.ObserveRestore(late);
        CHECK(f.service.View(f.clock).phase==Bc2NativeCyclePhase::Cancelled&&!f.service.View(f.clock).ready);
    }
    for(bool m95:{false,true})for(bool empty:{false,true})for(unsigned mutation=0;mutation<9;++mutation){
        Fixture f(m95);const int initial=empty?1:f.input.capacities[0];
        for(auto& b:f.input.branches){b.loaded=initial-1;b.currentState=6;b.previousState=5;b.nextState=empty?1:7;b.phaseTimer=.116971f;}
        f.Shot(0,initial);auto r=PredictedShotRollback(f,0);
        if(mutation==0)++r.exit.boundary->reserve;
        if(mutation==1)++r.exit.boundary->loaded;
        if(mutation==2)++r.exit.boundary->owner.weapon;
        if(mutation==3)r.exit.copiedSnapshot[63]^=std::byte{1};
        if(mutation==4)r.exit.holdRequested=true;
        if(mutation==5)r.exit.boundary->current=1;
        if(mutation==6)f.Begin(0); // Restore cannot overwrite an open own Update.
        if(mutation==7){++r.exit.boundary->loaded;Put(r.entry.copiedSnapshot,0x18,r.exit.boundary->loaded);r.exit.copiedSnapshot=r.entry.copiedSnapshot;}
        if(mutation==8){++r.exit.boundary->reserve;Put(r.entry.copiedSnapshot,0x1c,r.exit.boundary->reserve);r.exit.copiedSnapshot=r.entry.copiedSnapshot;}
        f.service.ObserveRestore(r);CHECK(f.service.View(f.clock).phase==Bc2NativeCyclePhase::Cancelled);
    }
    return 0;
}

ReloadFlowRecord TailReplay(Fixture& f,unsigned branch,bool empty=false){
    auto r=f.Restore(branch);const bool m95=f.service.Mode()==Bc2NativeCycleMode::M95;
    auto& z=*r.exit.boundary;z.current=m95?8:empty?6:7;z.previous=2;z.next=m95||empty?1:8;z.timer=.0241789f;
    Put(r.entry.copiedSnapshot,0,z.current);Put(r.entry.copiedSnapshot,4,z.next);Put(r.entry.copiedSnapshot,8,z.timer);
    r.exit.copiedSnapshot=r.entry.copiedSnapshot;return r;
}
void ApplyTailReplay(Fixture& f,const ReloadFlowRecord& r){
    const auto& z=*r.exit.boundary;auto& b=f.input.branches[z.branch];
    b.currentState=z.current;b.previousState=z.previous;b.nextState=z.next;b.phaseTimer=z.timer;
}
int AcknowledgedClientTailReplayRetainsPaidStroke(){
    // Actual413 invocation17625 restores ready2/1/2 to tail7/2/8 with the
    // same2/24 ammunition. Exercise that observed transition after all three
    // branches and the physical consumer have already acknowledged readiness.
    // This ordering is emulated; the actual416 invocation8748 payload was lost.
    for(unsigned family=0;family<3;++family)for(unsigned client=0;client<2;++client){
        const bool m95=family==1,empty=family==2;Fixture f(m95);
        CHECK(m95?M95Hold(f):empty?EmptyHold(f):f.Hold());CHECK(f.Release());
        const auto submitted=*f.control.release;
        const auto resume=[&](unsigned b){if(empty)EmptyResume(f,b);else f.Resume(b,m95);};
        for(unsigned b=0;b<3;++b)resume(b);
        const auto ready=f.service.View(f.clock).ready;CHECK(ready);
        CHECK(f.service.AcknowledgeReady(*ready,++f.clock));
        const auto paid=f.service.View(f.clock);CHECK(!paid.blocksFire&&!paid.ready);
        for(unsigned replay=0;replay<2;++replay){
            f.service.ObserveRestore(f.Restore(client)); // Ordinary idle publication retains payment.
            // Construct AFTER global completion/ACK, unlike the deliberately
            // out-of-order records in ReleasedClientTailReplayRejectsUnprovenHistory.
            const auto r=TailReplay(f,client,empty);
            f.service.ObserveRestore(r);ApplyTailReplay(f,r);
            const auto pending=f.service.View(f.clock);
            CHECK(pending.phase==Bc2NativeCyclePhase::Releasing&&pending.blocksFire);
            CHECK(!pending.ready&&!pending.held&&pending.cycle==paid.cycle&&pending.shot==paid.shot);
            CHECK(!f.service.YieldForReload());
            resume(client);
            const auto settled=f.service.View(f.clock);
            CHECK(settled.phase==Bc2NativeCyclePhase::Complete&&!settled.blocksFire);
            CHECK(!settled.ready&&!settled.held&&settled.failure==Bc2NativeCycleFailure::None);
            CHECK(settled.loaded==paid.loaded&&settled.reserve==paid.reserve&&settled.capacity==paid.capacity);
            CHECK(settled.cycle==paid.cycle&&settled.shot==paid.shot);
            CHECK(!f.service.AcknowledgeReady(*ready,f.clock)); // No duplicate receipt.
            CHECK(f.control.release&&*f.control.release==submitted); // No new gesture.
        }
        CHECK(f.service.YieldForReload());
    }
    return 0;
}
int ActualPumpAdapterPaidReplayDoesNotRepeatGestureOrAudio(){
    auto a=std::make_unique<InterruptedAdapter>();CHECK(a->Grip());a->Stroke();
    CHECK(a->submissions==1&&a->audioEvents==2);
    for(unsigned b=0;b<3;++b)a->f.Resume(b);
    a->Tick(0);CHECK(a->acknowledgements==1&&!a->result.blocksFire);
    const auto paid=a->f.service.View(a->f.clock);
    for(unsigned client=0;client<2;++client){
        const auto r=TailReplay(a->f,client);a->f.service.ObserveRestore(r);ApplyTailReplay(a->f,r);
        a->Tick(0); // Squeeze remains held; neither neutral nor another stroke.
        CHECK(a->result.blocksFire&&!a->result.tracking.held&&!a->result.tracking.target);
        CHECK(!a->result.settled&&!a->result.audio&&!a->f.service.YieldForReload());
        a->f.Resume(client);a->Tick(0);
        CHECK(!a->result.blocksFire&&!a->pump.BlocksFire()&&!a->result.settled&&!a->result.audio);
        CHECK(a->acknowledgements==1&&a->submissions==1&&a->audioEvents==2);
        const auto state=a->f.service.View(a->f.clock);
        CHECK(state.loaded==paid.loaded&&state.reserve==paid.reserve&&state.cycle==paid.cycle&&state.shot==paid.shot);
    }
    CHECK(a->f.service.YieldForReload()); // The actual shell/resource handoff is free again.
    return 0;
}
int PaidTailReplayStillRejectsUnprovenOrIncompleteNativeWork(){
    for(bool m95:{false,true})for(unsigned mutation=0;mutation<15;++mutation){
        Fixture f(m95);CHECK(m95?M95Hold(f):f.Hold());CHECK(f.Release());
        for(unsigned b=0;b<3;++b)f.Resume(b,m95);
        const auto ready=*f.service.View(f.clock).ready;
        std::optional<ReloadFlowRecord> beforeAck;
        if(mutation==1)beforeAck=TailReplay(f,1);
        if(mutation!=0)CHECK(f.service.AcknowledgeReady(ready,++f.clock));
        auto r=beforeAck?*beforeAck:TailReplay(f,1);
        if(mutation==2)++r.exit.boundary->loaded;
        if(mutation==3)++r.exit.boundary->reserve;
        if(mutation==4)++r.exit.boundary->owner.weapon;
        if(mutation==5)r.exit.copiedSnapshot[63]^=std::byte{1};
        if(mutation==6)--r.entry.nativeInvocation;
        if(mutation==7)r.entry.nowNs=ready.observedNs-1;
        if(mutation==8)r.exit.holdRequested=true;
        if(mutation==9){r.entry.nowNs+=4000000000ll;r.exit.nowNs=r.entry.nowNs+1;}
        if(mutation==10)f.Begin(1);
        if(mutation==11){++f.control.input.owner.equipGeneration;f.control.item.generation=f.control.input.owner.equipGeneration;
            f.control.mechanism.generation=f.control.input.owner.equipGeneration;CHECK(f.Advance());}
        f.service.ObserveRestore(r);
        if(mutation>=12){
            CHECK(f.service.View(f.clock).phase==Bc2NativeCyclePhase::Releasing);ApplyTailReplay(f,r);
            if(mutation==12){ // Original tail cannot skip its first proven Commit.
                f.Begin(1);f.Commit(1,1,2);f.Finish();
            }else if(mutation==13){
                f.Begin(1);--f.update.exit.boundary->loaded;f.Finish();
            }else{
                f.clock=ready.release.observedNs+4000000000ll;f.Begin(1);
            }
        }
        const auto rejected=f.service.View(f.clock);
        CHECK(rejected.phase==Bc2NativeCyclePhase::Cancelled&&rejected.blocksFire&&!rejected.held&&!rejected.ready);
    }
    {Fixture f;CHECK(f.Hold());CHECK(f.Release());for(unsigned b=0;b<3;++b)f.Resume(b);
        const auto old=TailReplay(f,1);CHECK(f.service.AcknowledgeReady(*f.service.View(f.clock).ready,++f.clock));
        const auto fresh=TailReplay(f,0);f.service.ObserveRestore(fresh);ApplyTailReplay(f,fresh);
        CHECK(f.service.View(f.clock).phase==Bc2NativeCyclePhase::Releasing);
        // Reopening client0 cannot legitimize a different client's queued
        // pre-ACK record merely because the phase now reads Releasing.
        f.service.ObserveRestore(old);
        CHECK(f.service.View(f.clock).phase==Bc2NativeCyclePhase::Cancelled&&f.service.View(f.clock).blocksFire);
    }
    return 0;
}
int PaidReplayCannotForgiveTheNextRealShot(){
    Fixture f;CHECK(f.Hold());CHECK(f.Release());for(unsigned b=0;b<3;++b)f.Resume(b);
    CHECK(f.service.AcknowledgeReady(*f.service.View(f.clock).ready,++f.clock));
    const auto r=TailReplay(f,0);f.service.ObserveRestore(r);ApplyTailReplay(f,r);f.Resume(0);
    CHECK(!f.service.View(f.clock).blocksFire);
    for(auto& b:f.input.branches)b.loaded=6;
    for(unsigned b=0;b<3;++b)f.Shot(b,7);
    for(unsigned pass=0;pass<2;++pass){f.Advance();for(unsigned b=0;b<3;++b){f.Begin(b);f.Finish();}}
    const auto next=f.service.View(f.clock);CHECK(next.phase==Bc2NativeCyclePhase::Held&&next.blocksFire&&next.held);
    CHECK(next.cycle==2&&next.shot==2&&next.loaded==6&&next.reserve==24&&!next.ready);
    CHECK(f.service.Diagnostic().acknowledgedAtNs==0);
    CHECK(!f.service.YieldForReload());return 0;
}
int ReleasedClientTailReplayRetainsOnePhysicalTransaction(){
    const auto actual=m95_tail255::Record();
    CHECK(CycleConvergenceRestore(actual,m95_tail255::Identity(),1,true,false));
    // Both selected mechanisms and both client branches share the same policy.
    // The empty SPAS has a distinct, explicitly selected native tail.
    for(unsigned family=0;family<3;++family)for(unsigned branch=0;branch<2;++branch){
        const bool m95=family==1,empty=family==2;Fixture f(m95);
        CHECK(m95?M95Hold(f):empty?EmptyHold(f):f.Hold());CHECK(f.Release());
        const auto submitted=*f.control.release;const auto watch=f.service.CompletionWatch(f.clock);CHECK(watch);
        const auto resume=[&](unsigned b){if(empty)EmptyResume(f,b);else f.Resume(b,m95);};
        resume(branch);
        for(unsigned replay=0;replay<2;++replay){
            const auto r=TailReplay(f,branch,empty);f.service.ObserveRestore(r);ApplyTailReplay(f,r);
            const auto pending=f.service.View(f.clock);CHECK(pending.phase==Bc2NativeCyclePhase::Releasing);
            CHECK(pending.blocksFire&&!pending.held&&!pending.ready&&pending.cycle==1&&pending.shot==1);
            CHECK(f.service.CompletionWatch(f.clock)->deadlineNs==watch->deadlineNs);
            resume(branch);CHECK(!f.service.View(f.clock).ready);
        }
        for(unsigned b=0;b<3;++b)if(b!=branch)resume(b);
        const auto ready=f.service.View(f.clock);CHECK(ready.ready&&ready.ready->release==submitted);
        CHECK(ready.loaded==f.input.branches[0].loaded&&ready.reserve==f.input.branches[0].reserve);
        CHECK(f.service.AcknowledgeReady(*ready.ready)&&!f.service.View(f.clock).blocksFire);
    }
    // Actual255 interleaves the other client's independently confirmed forward
    // snapshot with the replaying client's tail. Neither is a second gesture.
    Fixture f(true);CHECK(M95Hold(f)&&f.Release());f.Resume(1,true);
    f.service.ObserveRestore(ForwardReadyRestore(f));
    auto& a=f.input.branches[0];a.currentState=2;a.previousState=8;a.nextState=2;a.phaseTimer=0;
    f.Begin(0);f.Finish();
    const auto r=TailReplay(f,1);f.service.ObserveRestore(r);ApplyTailReplay(f,r);
    f.Resume(2,true);CHECK(!f.service.View(f.clock).ready); // Replayed client must finish again.
    f.Resume(1,true);CHECK(f.service.View(f.clock).ready);
    return 0;
}
int ReleasedClientTailReplayRejectsUnprovenHistory(){
    for(bool m95:{false,true})for(unsigned mutation=0;mutation<13;++mutation){
        Fixture f(m95);CHECK(m95?M95Hold(f):f.Hold());CHECK(f.Release());
        if(mutation!=0)f.Resume(1,m95);
        auto r=TailReplay(f,1);
        if(mutation==1){f.Resume(0,m95);f.Resume(2,m95);}
        if(mutation==2){f.Resume(0,m95);f.Resume(2,m95);CHECK(f.service.AcknowledgeReady(*f.service.View(f.clock).ready));}
        if(mutation==3)++r.exit.boundary->loaded;
        if(mutation==4)++r.exit.boundary->reserve;
        if(mutation==5)++r.exit.boundary->owner.weapon;
        if(mutation==6)r.exit.copiedSnapshot[63]^=std::byte{1};
        if(mutation==7){r.entry.nowNs+=4000000000ll;r.exit.nowNs=r.entry.nowNs+1;}
        if(mutation==8)f.Begin(1);
        if(mutation==9)r.entry.nowNs=f.control.release->observedNs-1;
        if(mutation==10)--r.entry.nativeInvocation;
        if(mutation==11)r.exit.holdRequested=true;
        if(mutation==12){r.exit.boundary->previous=7;}
        f.service.ObserveRestore(r);const auto v=f.service.View(f.clock);
        CHECK(v.phase==Bc2NativeCyclePhase::Cancelled&&v.blocksFire&&!v.ready&&!v.held);
    }
    return 0;
}

int SelectionReadGapRetainsIdentityOnly(){
    for(bool m95:{false,true}){
        Fixture f(m95);CHECK(m95?M95Hold(f):f.Hold());
        const auto mode=m95?Bc2NativeCycleMode::M95:Bc2NativeCycleMode::Spas;
        const auto owner=f.control.nativeOwner;
        Bc2NativeCycleSelectionTracker selection;
        CHECK(!selection.Observe(mode,owner,std::nullopt));
        auto view=f.service.View(f.clock);view.selectedMode=mode;view.selectedOwner=owner;
        CHECK(view.held&&view.blocksFire&&!view.ready);
        CHECK(selection.Observe(mode,owner,view));
        // A renderer/native callback owns the gate for one or more polls while
        // the real physical gesture still owns its original hold/deadline.
        const auto lease=*view.held;
        for(unsigned n=0;n<3;++n)CHECK(selection.Observe(mode,owner,std::nullopt));
        const auto unchanged=f.service.View(f.clock);
        CHECK(unchanged.held&&weapon_cycle_detail::Same(lease,*unchanged.held)&&
              unchanged.held->deadlineNs==lease.deadlineNs&&!unchanged.ready&&unchanged.blocksFire);
        auto foreign=owner;++foreign.weapon;
        CHECK(!selection.Observe(mode,foreign,std::nullopt));
        foreign=owner;++foreign.space;CHECK(!selection.Observe(mode,foreign,std::nullopt));
        CHECK(selection.RoutesTrackingRecovery(mode,foreign)==!m95);
        // Routing is not selection acceptance and does not retire native debt.
        CHECK(!selection.Observe(mode,foreign,std::nullopt));
        auto changed=foreign;++changed.weapon;CHECK(!selection.RoutesTrackingRecovery(mode,changed));
        changed=foreign;++changed.actorGeneration;CHECK(!selection.RoutesTrackingRecovery(mode,changed));
        changed=foreign;++changed.equipGeneration;CHECK(!selection.RoutesTrackingRecovery(mode,changed));
        changed=owner;--changed.space;CHECK(!selection.RoutesTrackingRecovery(mode,changed));
        CHECK(!selection.RoutesTrackingRecovery(mode,owner));
        foreign=owner;++foreign.equipGeneration;CHECK(!selection.Observe(mode,foreign,std::nullopt));
        const auto other=m95?Bc2NativeCycleMode::Spas:Bc2NativeCycleMode::M95;
        CHECK(!selection.Observe(other,owner,std::nullopt));
        view.selectedMode.reset();CHECK(!selection.Observe(mode,owner,view));
        CHECK(!selection.Observe(mode,owner,std::nullopt));
        selection.Selected(mode,owner);CHECK(selection.Observe(mode,owner,std::nullopt));
        view.selectedMode=mode;view.selectedOwner=foreign;
        CHECK(!selection.Observe(mode,owner,view));CHECK(!selection.Observe(mode,owner,std::nullopt));
        CHECK(selection.Observe(mode,foreign,std::nullopt));
    }
    return 0;
}


// Actual270: client Update10379 was still open when server Update10372 finished
// the last tail at102436366099200. Its missing final observation then left a
// stale openUpdate marker, rejecting unchanged idle Restore10382 14.72ms later.
int CompletedCycleDrainsExactMissingObservation(){
    for(bool m95:{false,true})for(bool ackFirst:{false,true}){
        Fixture f(m95);CHECK(m95?M95Hold(f):f.Hold());CHECK(f.Release());
        f.Resume(0,m95);f.Resume(1,m95);
        f.Begin(2);if(!m95)f.Commit(2,7,8);f.Commit(2,8,1);f.Commit(2,1,2);
        auto server=f.update;const auto serverDecision=f.decision;
        f.Begin(0);const auto lost=f.decision;
        server.exit.nowNs=++f.clock;server.exit.boundary->current=2;
        server.exit.boundary->previous=1;server.exit.boundary->next=2;server.exit.boundary->timer=0;
        f.service.Finish(serverDecision,server);
        const auto ready=f.service.View(f.clock).ready;CHECK(ready);
        if(ackFirst)CHECK(f.service.AcknowledgeReady(*ready));
        f.service.Missing(f.input.identity.firing[0],lost.invocation);
        f.clock+=14720500;
        auto restore=f.Restore(0);restore.entry.boundary.previous=2;
        CHECK(CyclePredictionRestore(restore,f.input.identity,0));
        f.service.ObserveRestore(restore);
        const auto retained=f.service.View(f.clock);
        CHECK(retained.phase==Bc2NativeCyclePhase::Complete);
        CHECK(retained.loaded==(m95?4:7));
        CHECK(retained.reserve==(m95?45:24)&&retained.blocksFire==!ackFirst);
        if(!ackFirst){CHECK(retained.ready&&retained.ready->sequence==ready->sequence);
            CHECK(retained.ready->observedNs==ready->observedNs&&retained.ready->deadlineNs==ready->deadlineNs);
            CHECK(f.service.AcknowledgeReady(*ready));}
        CHECK(!f.service.View(f.clock).blocksFire);
        CHECK(f.Advance());CHECK(f.Begin(0).tracked);f.Finish();
        CHECK(f.service.YieldForReload());
    }
    return 0;
}
int MissingCompletedObservationCannotHideContradictions(){
    for(unsigned mutation=0;mutation<5;++mutation){
        Fixture f;CHECK(f.Hold()&&f.Release());for(unsigned b=0;b<3;++b)f.Resume(b);
        const auto ready=*f.service.View(f.clock).ready;CHECK(f.service.AcknowledgeReady(ready));
        f.Begin(0);const auto lost=f.decision;
        // Only the exact branch/invocation can drain. Wrong IDs and another
        // firing object must leave the outstanding callback visible.
        f.service.Missing(mutation==0?f.input.identity.firing[1]:f.input.identity.firing[0],
            mutation==1?lost.invocation-1:mutation==2?0:lost.invocation);
        auto restore=f.Restore(0);
        if(mutation==3){--restore.exit.boundary->loaded;Put(restore.entry.copiedSnapshot,0x18,6);restore.exit.copiedSnapshot=restore.entry.copiedSnapshot;}
        if(mutation==4)++restore.exit.boundary->owner.equipGeneration;
        f.service.ObserveRestore(restore);
        const auto view=f.service.View(f.clock);
        CHECK(view.phase==Bc2NativeCyclePhase::Cancelled&&view.blocksFire&&view.failure==Bc2NativeCycleFailure::Restore);
    }
    Fixture active;CHECK(active.Hold());active.Begin(0);
    active.service.Missing(active.input.identity.firing[0],active.decision.invocation);
    CHECK(active.service.View(active.clock).phase==Bc2NativeCyclePhase::Cancelled);
    return 0;
}

int ServerForwardCompletionStillRequiresClientIdle(){
    const auto native=spas_forward287::Identity();
    CHECK(ValidateCycleUpdate(spas_forward287::Record(7018),native,2));
    for(auto id:{7020u,7023u,7025u})CHECK(CycleCommit(spas_forward287::Record(id),native,2,7018));
    const auto restored=spas_forward287::Record(7027);
    CHECK(!CyclePredictionRestore(restored,native,1));
    CHECK(CycleForwardReadyRestore(restored,native,1,{7,6,8,.1f,.5f}));
    CHECK(CycleForwardIdleUpdate(spas_forward287::Record(7028),native,1,7));
    CHECK(spas_forward287::Record(7018).exit.nowNs<=restored.entry.nowNs);
    const auto clientSource=spas_forward289::Identity();
    CHECK(ValidateCycleUpdate(spas_forward289::Record(8313),clientSource,1));
    for(auto id:{8314u,8315u,8316u})CHECK(CycleCommit(spas_forward289::Record(id),clientSource,1,8313));
    CHECK(CycleForwardReadyRestore(spas_forward289::Record(8317),clientSource,0,{7,6,8,.1f,.5f}));
    CHECK(CycleForwardIdleUpdate(spas_forward289::Record(8318),clientSource,0,7));
    CHECK(spas_forward289::Record(8313).exit.nowNs<=spas_forward289::Record(8317).entry.nowNs);
    // Actual287: the server completed its own tail before clientB restored
    // that idle snapshot. Each client still needs its own neutral idle Update.
    for(bool empty:{false,true})for(unsigned client:{0u,1u}){
        Fixture f;CHECK(empty?EmptyHold(f):f.Hold());CHECK(f.Release());
        const unsigned source=client==0?1u:2u;
        if(empty)EmptyResume(f,source);else f.Resume(source);CHECK(!f.service.View(f.clock).ready);
        auto r=f.Restore(client);const auto prior=r.entry.boundary.current;
        r.exit.boundary->current=2;r.exit.boundary->previous=prior;r.exit.boundary->next=2;r.exit.boundary->timer=0;
        Put(r.entry.copiedSnapshot,0,2u);Put(r.entry.copiedSnapshot,4,2u);Put(r.entry.copiedSnapshot,8,0.f);
        r.exit.copiedSnapshot=r.entry.copiedSnapshot;f.service.ObserveRestore(r);
        CHECK(f.service.View(f.clock).phase==Bc2NativeCyclePhase::Releasing&&!f.service.View(f.clock).ready);
        const unsigned remaining=3-client-source;
        if(empty)EmptyResume(f,remaining);else f.Resume(remaining);CHECK(!f.service.View(f.clock).ready);
        auto& state=f.input.branches[client];state.currentState=2;state.previousState=prior;state.nextState=2;state.phaseTimer=0;
        f.Begin(client);f.Finish();const auto ready=f.service.View(f.clock).ready;CHECK(ready&&ready->unchangedAmmunition);
        f.service.ObserveRestore(f.Restore(client));CHECK(f.service.AcknowledgeReady(*ready));
    }
    for(unsigned variant=0;variant<12;++variant){
        Fixture f;CHECK(f.Hold());if(variant!=11)CHECK(f.Release());
        if(variant!=0&&variant!=11)f.Resume(2);
        if(variant==1)f.clock+=Bc2NativeCycleService::ContextFreshNs+1;
        if(variant==2)f.Begin(1);
        auto r=f.Restore(1);
        r.exit.boundary->current=2;r.exit.boundary->previous=7;r.exit.boundary->next=2;r.exit.boundary->timer=0;
        Put(r.entry.copiedSnapshot,0,2u);Put(r.entry.copiedSnapshot,4,2u);Put(r.entry.copiedSnapshot,8,0.f);
        r.exit.copiedSnapshot=r.entry.copiedSnapshot;
        if(variant==3)r.exit.copiedSnapshot[63]^=std::byte{1};
        if(variant==4)++r.exit.boundary->loaded;
        if(variant==5)++r.entry.boundary.owner.equipGeneration;
        if(variant==6)r.entry.boundary.previous=5;
        if(variant==7)r.entry.boundary.timer=0;
        if(variant==8){r.entry.nowNs=f.control.release->observedNs-1;r.exit.nowNs=r.entry.nowNs+1;}
        if(variant==9){r.entry.nowNs+=4000000000ll;r.exit.nowNs=r.entry.nowNs+1;}
        f.service.ObserveRestore(r);
        if(variant==10){CHECK(f.service.View(f.clock).phase==Bc2NativeCyclePhase::Releasing);f.service.ObserveRestore(r);}
        CHECK(f.service.View(f.clock).phase==Bc2NativeCyclePhase::Cancelled&&!f.service.View(f.clock).ready);
    }
    // The prediction graph is directed: server -> clientB -> clientA.
    // A forwarded client vote requires its own idle Update and cannot become
    // a source for another branch before that Update actually finishes.
    for(bool empty:{false,true})for(bool premature:{false,true}){
        Fixture f;CHECK(empty?EmptyHold(f):f.Hold());CHECK(f.Release());
        if(empty)EmptyResume(f,2);else f.Resume(2);
        const auto forward=[&](unsigned client){auto r=f.Restore(client);const auto prior=r.entry.boundary.current;
            r.exit.boundary->current=2;r.exit.boundary->previous=prior;r.exit.boundary->next=2;r.exit.boundary->timer=0;
            Put(r.entry.copiedSnapshot,0,2u);Put(r.entry.copiedSnapshot,4,2u);Put(r.entry.copiedSnapshot,8,0.f);
            r.exit.copiedSnapshot=r.entry.copiedSnapshot;f.service.ObserveRestore(r);
            auto& b=f.input.branches[client];b.currentState=2;b.previousState=prior;b.nextState=2;b.phaseTimer=0;};
        forward(1);CHECK(f.service.View(f.clock).phase==Bc2NativeCyclePhase::Releasing&&!f.service.View(f.clock).ready);
        if(!premature){f.Begin(1);f.Finish();}
        forward(0);
        if(premature){CHECK(f.service.View(f.clock).phase==Bc2NativeCyclePhase::Cancelled);continue;}
        CHECK(!f.service.View(f.clock).ready);f.Begin(0);f.Finish();
        const auto ready=f.service.View(f.clock).ready;CHECK(ready&&ready->unchangedAmmunition);
        CHECK(f.service.AcknowledgeReady(*ready));
    }
    return 0;
}
int CompletedCyclePreservesIssuedReadAcrossOtherBranchFinish(){
    // Actual285: clientA's idle Update12603 began under completion observation
    // just before another branch published Complete. Its Step lost that read,
    // entered stock reload, then prediction Restore12608 revoked the cycle.
    for(unsigned mode=0;mode<3;++mode)for(bool ackFirst:{false,true}){
        Fixture f(mode==2);CHECK(mode==2?M95Hold(f):mode==1?EmptyHold(f):f.Hold());CHECK(f.Release());
        const auto resume=[&](unsigned b){if(mode==1)EmptyResume(f,b);else f.Resume(b,mode==2);};
        resume(0);CHECK(f.Begin(0).tracked&&!f.decision.hold);
        const auto issued=f.service.ObservationAuthority(0,f.clock);CHECK(issued);
        auto pending=f.update;const auto decision=f.decision;const auto originalDeadline=f.input.leaseDeadlineNs;
        resume(1);resume(2);const auto ready=f.service.View(f.clock).ready;CHECK(ready);
        if(ackFirst)CHECK(f.service.AcknowledgeReady(*ready));
        CHECK(!f.service.ObservationWatch(f.clock)&&!f.service.ObservationAuthority(0,f.clock));
        CHECK(f.service.ObservationAuthority(issued,f.clock)==issued);
        CHECK(!f.service.ObservationAuthority(issued+1,f.clock));
        CHECK(f.clock<originalDeadline);pending.exit.nowNs=++f.clock;
        f.service.Finish(decision,pending);
        auto restore=f.Restore(0);f.service.ObserveRestore(restore);
        CHECK(f.service.View(f.clock).phase==Bc2NativeCyclePhase::Complete);
        CHECK(f.service.View(f.clock).loaded==(mode==1?0:mode==2?4:7));
        f.service.Cancel(Bc2NativeCycleFailure::Owner);
        CHECK(!f.service.ObservationAuthority(issued,f.clock));
        if(!ackFirst){CHECK(f.service.View(f.clock).ready);CHECK(f.service.AcknowledgeReady(*ready));}
    }
    return 0;
}

int main(){if(SameActorSeatEpochCannotRetireCancelledCycleDebt()||AcknowledgedClientTailReplayRetainsPaidStroke()||ActualPumpAdapterPaidReplayDoesNotRepeatGestureOrAudio()||PaidTailReplayStillRejectsUnprovenOrIncompleteNativeWork()||PaidReplayCannotForgiveTheNextRealShot()||ServerForwardCompletionStillRequiresClientIdle()||CompletedCyclePreservesIssuedReadAcrossOtherBranchFinish()||CompletedCycleDrainsExactMissingObservation()||MissingCompletedObservationCannotHideContradictions()||ReplacementActorRequiresExactFreshDrainedProof()||ReplacementActorArchivesPendingAndCompletedOutcomes()||ConfirmedReplacementActorDoesNotInheritOldPhysicalPumpDebt()||SelectionReadGapRetainsIdentityOnly()||ReleasedClientTailReplayRetainsOnePhysicalTransaction()||ReleasedClientTailReplayRejectsUnprovenHistory()||PredictedShotReplayPreservesOneDebt()||SpasSnapshotProgressionUsesSharedCyclePolicy()||Actual241SnapshotProgressionEvidence()||SnapshotProgressionRetainsMeasuredPreviousUntilOwnHold()||SnapshotProgressionCannotEraseStartedInterception()||SelectedModePrereadRequiresOriginalQuietRevision()||SelectedModeRequiresExactOriginalIdleProof()||SelectedModeCannotErasePendingDebt()||SelectedModeRoundTripPreservesCycleAuthority()||Actual239ForwardCallbackEvidence()||M95ForwardRestoreRequiresOwnClientAndServerCompletion()||M95ForwardRestoreDoesNotBorrowOrForgeReadiness()||QueuedReleaseDoesNotReclassifyAnAlreadyHeldUpdate()||OriginalSpasRegression()||ExactM95ThreeBranchHoldAndTail()||M95CannotBorrowSpasBoundaryOrWrongConfig()||
    M95LastRoundRequiresSameEmptyDebtAndTail()||M95CompletionCoversOriginalBoltTimer()||ExactOriginalPredictionRewindOnlyBeforeHold()||
    PredictionRewindRequiresExactOriginalSnapshot()||M95TwoChildTailRejectsSpasExtraCommit()||M95SuspensionRequiresRightMechanismNeutral()||PhysicalBoltDiagnosticWireBounds())return 1;
    std::puts("15 M95 operating-class +1 shared queued-release groups passed; exact8/7/1 hold,2-child tail, pre-hold original rewind, right-mechanism neutral and bounded30s wire mode.");return 0;}
