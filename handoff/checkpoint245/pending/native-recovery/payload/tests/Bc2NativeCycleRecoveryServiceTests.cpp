#include "Bc2NativeCycleService.h"
#include "Bc2NativeCycleCallbacks.h"
#include "Bc2NativeCycleDispatch.h"
#include "Bc2ReloadInvocationEntry.h"
#include "Bc2M95StockShotConfig.h"
#include "Bc2M95PhysicalBoltDiagnostic.h"
#include "Bc2NativeCycleEvidence.h"
#include "M95ForwardRestore239Fixture.h"
#include "M95SnapshotProgression241Fixture.h"
#include "Bc2PhysicalPump.h"
#include "Bc2PumpCalibration225.h"
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

struct RecoveryFixture:Fixture {
    RecoveryFixture(bool m95=false):Fixture(m95){
        // Configure is a pre-control capability; restart the fixture service
        // before its first actual selected shot, then re-admit the same source.
        service={};if(m95)service.Configure(Bc2NativeCycleMode::M95);
        service.EnableRecovery();service.Control(control);
    }
    bool Held(){if(service.Mode()==Bc2NativeCycleMode::Spas)return Hold();
        for(unsigned b=0;b<3;++b)Shot(b,5);
        for(unsigned pass=0;pass<2;++pass){Advance();for(unsigned b=0;b<3;++b){Begin(b);Finish();}}
        return service.View(clock).held.has_value();
    }
    void Idle(unsigned branch,bool busy=false,unsigned rejected=0){
        clock+=1000000;const auto d0=Begin(branch);(void)d0; // Recovering normal Evaluate deliberately issues nothing.
        auto& a=update.entry.boundary;a.current=busy?8:2;a.previous=busy?7:1;a.next=busy?1:2;a.timer=busy?.1f:0;
        update.exit.boundary=a;update.exit.nowNs=++clock;
        const auto d=service.EvaluateRecovery({input.identity,input.config},update.entry,input.capacities[branch],clock+99000000);
        NativeCycleInputGuard g;g.requested=g.applied=g.restored=true;g.original={0,0};g.effective=g.beforeRestore={1,0};
        update.exit.cycleGuard=g;update.exit.cycleGuardEpoch=d.epoch;update.exit.cycleGuardSteps=1;update.exit.cycleGuardRejected=rejected;
        service.FinishRecovery(d,update,g,1,rejected);
    }
    void Rounds(){for(unsigned p=0;p<2;++p)for(unsigned b=0;b<3;++b)Idle(b);}
    bool Grant(){Rounds();Advance(false);if(service.Mode()==Bc2NativeCycleMode::M95){control.input.released={false,true};service.Control(control);}
        Rounds();return service.RecoveryView(clock)->authority.has_value();}
};
int NativeIdleRequiresActualGesture(){for(bool m95:{false,true}){RecoveryFixture f(m95);CHECK(f.Held());CHECK(f.service.BeginRecovery(f.clock));
    CHECK(f.service.View(f.clock).blocksFire&&!f.service.View(f.clock).held);CHECK(f.Grant());
    const auto view=f.service.RecoveryView(f.clock);CHECK(view&&view->authority&&!view->ready&&view->blocksFire);
    f.clock+=1000000;f.Advance();auto release=WeaponCycleIdleDebtRelease{*view->authority,1,f.control.input.sequence,
        f.control.input.observedNs,std::min(f.control.input.deadlineNs,view->authority->deadlineNs)};
    CHECK(f.service.SubmitRecovery(release,f.control.input));f.Rounds();const auto ready=f.service.RecoveryView(f.clock)->ready;
    CHECK(ready&&ready->release==release&&f.service.View(f.clock).blocksFire);
    CHECK(f.service.AcknowledgeRecovery(*ready,f.clock));CHECK(!f.service.View(f.clock).blocksFire);
    }return 0;}
int LongTrackingLossRetainsDebt(){RecoveryFixture f;CHECK(f.Held());CHECK(f.service.BeginRecovery(f.clock));
    for(unsigned second=0;second<61;++second){f.clock+=1000000000;f.Rounds();CHECK(f.service.View(f.clock).blocksFire);}
    CHECK(f.service.ObservationWatch(f.clock));CHECK(!f.service.RecoveryView(f.clock)->authority);CHECK(f.Grant());return 0;}
int MissingGuardAndConservationNeverRecover(){for(unsigned mutation=0;mutation<4;++mutation){RecoveryFixture f;CHECK(f.Held());CHECK(f.service.BeginRecovery(f.clock));
    if(mutation==0)f.Idle(0,false,1);
    if(mutation==1){--f.input.branches[0].loaded;f.Idle(0);}
    if(mutation==2){++f.input.config.weaponData;f.Idle(0);}
    if(mutation==3){f.service.Missing(f.input.identity.firing[0],++f.event);}
    CHECK(f.service.View(f.clock).phase==Bc2NativeCyclePhase::Cancelled&&f.service.View(f.clock).blocksFire);
    CHECK(!f.service.RecoveryView(f.clock)->authority&&!f.service.RecoveryView(f.clock)->ready);
    CHECK(!f.service.BeginRecovery(f.clock));}return 0;}
int NativeBusyRevokesGrantButNotSubmittedReceipt(){RecoveryFixture f;CHECK(f.Held());CHECK(f.service.BeginRecovery(f.clock));CHECK(f.Grant());
    const auto old=*f.service.RecoveryView(f.clock)->authority;f.Idle(0,true);CHECK(!f.service.RecoveryView(f.clock)->authority);
    f.Rounds();CHECK(!f.service.RecoveryView(f.clock)->authority);CHECK(f.Grant());
    const auto next=*f.service.RecoveryView(f.clock)->authority;CHECK(next.grant.nonce!=old.grant.nonce);
    f.Advance();auto release=WeaponCycleIdleDebtRelease{next,1,f.control.input.sequence,f.control.input.observedNs,
        std::min(f.control.input.deadlineNs,next.deadlineNs)};CHECK(f.service.SubmitRecovery(release,f.control.input));f.Rounds();
    const auto ready=*f.service.RecoveryView(f.clock)->ready;f.service.BeginRecovery(f.clock);f.clock+=61000000000;
    CHECK(f.service.RecoveryView(f.clock)->ready->observedNs==ready.observedNs);CHECK(!f.service.AcknowledgeRecovery(ready,f.clock));
    f.Rounds();CHECK(f.service.AcknowledgeRecovery(ready,f.clock));return 0;}
int ExistingHoldDrainsBeforeConvergence(){RecoveryFixture f;CHECK(f.Held());f.Begin(1);CHECK(f.decision.hold);
    CHECK(f.service.BeginRecovery(f.clock));f.Finish();CHECK(f.service.View(f.clock).phase==Bc2NativeCyclePhase::Converging);
    CHECK(f.Grant());return 0;}
int NoConversionOfNormalPendingOrStaleHold(){RecoveryFixture f;CHECK(f.Held());CHECK(f.Release());CHECK(!f.service.BeginRecovery(f.clock));
    CHECK(f.service.View(f.clock).phase==Bc2NativeCyclePhase::Releasing);RecoveryFixture stale;CHECK(stale.Held());stale.clock+=101000000;
    CHECK(!stale.service.BeginRecovery(stale.clock));CHECK(stale.service.View(stale.clock).blocksFire);return 0;}

int RestoreNeverVotesAndBusyRewindsRevokeOnlyUnsubmittedAuthority(){
    for(unsigned mutation=0;mutation<7;++mutation){RecoveryFixture f;CHECK(f.Held());CHECK(f.service.BeginRecovery(f.clock));CHECK(f.Grant());
        auto r=f.Restore(0);auto& before=r.entry.boundary;auto& after=*r.exit.boundary;
        before.current=2;before.previous=1;before.next=2;before.timer=0;
        after=before;after.previous=before.current;after.current=7;after.next=8;after.timer=.2f;
        Put(r.entry.copiedSnapshot,0,after.current);Put(r.entry.copiedSnapshot,4,after.next);Put(r.entry.copiedSnapshot,8,after.timer);
        r.exit.copiedSnapshot=r.entry.copiedSnapshot;
        if(mutation==1)r.exit.copiedSnapshot[0x30]=std::byte{1};
        if(mutation==2)--after.loaded;
        if(mutation==3)++r.entry.nativeInvocation;
        if(mutation==4){before.current=after.current=12;Put(r.entry.copiedSnapshot,0,after.current);r.exit.copiedSnapshot=r.entry.copiedSnapshot;}
        if(mutation==5)++r.exit.thread;
        if(mutation==6)++after.owner.equipGeneration;
        f.service.ObserveRestore(r);CHECK(!f.service.RecoveryView(f.clock)->authority&&!f.service.RecoveryView(f.clock)->ready);
        if(!mutation){CHECK(f.service.Recovering());f.Rounds();CHECK(!f.service.RecoveryView(f.clock)->authority);CHECK(f.Grant());}
        else CHECK(f.service.View(f.clock).phase==Bc2NativeCyclePhase::Cancelled);
    }return 0;
}
int ExactGuardedIdleRecordMutations(){
    RecoveryFixture f;CHECK(f.Held());CHECK(f.service.BeginRecovery(f.clock));f.Idle(0);const auto base=f.update;
    for(unsigned mutation=0;mutation<11;++mutation){auto r=base;auto g=r.exit.cycleGuard;unsigned steps=1,rejected=0;
        if(mutation==0)g.restored=false;if(mutation==1)g.original[1]=1;if(mutation==2)g.beforeRestore[0]=0;
        if(mutation==3)steps=0;if(mutation==4)rejected=1;if(mutation==5)r.exit.copiedContext[0x18]^=std::byte{1};
        if(mutation==6)++r.entry.nativeParent;if(mutation==7)--r.exit.boundary->loaded;
        if(mutation==8)r.exit.boundary->current=10;if(mutation==9)r.entry.boundary.current=12;
        if(mutation==10)r.exit.boundary->flagsA8|=4;
        CHECK(!CycleGuardedUpdate(r,f.input.identity,0,g,steps,rejected,false,false));
    }
    CHECK(CycleGuardedUpdate(base,f.input.identity,0,base.exit.cycleGuard,1,0,false,false));
    auto busy=base;busy.entry.boundary.current=busy.exit.boundary->current=7;
    busy.entry.boundary.next=busy.exit.boundary->next=8;busy.entry.boundary.timer=.2f;busy.exit.boundary->timer=.18f;
    CHECK(CycleGuardedUpdate(busy,f.input.identity,0,busy.exit.cycleGuard,0,0,false,false));return 0;
}

int GuardedCommitsRequireOwnOpenParentAndConservedProjection(){
    RecoveryFixture f;CHECK(f.Held());CHECK(f.service.BeginRecovery(f.clock));f.Idle(0);
    ReloadFlowRecord retainedCommit;
    for(const auto pair:std::array<std::pair<unsigned,unsigned>,4>{{{7,8},{8,1},{1,2},{6,1}}}){
        auto r=f.update;const auto parent=r.id;r.id=++f.event;r.entry.kind=ReloadFlowEvent::Commit;r.entry.depth=2;
        r.entry.nativeInvocation=r.id;r.entry.parent=r.entry.nativeParent=r.entry.update=r.entry.nativeUpdate=parent;
        r.entry.boundary.current=pair.first;r.entry.boundary.previous=pair.first-1;r.entry.boundary.next=pair.second;r.entry.boundary.timer=0;
        r.exit.boundary=r.entry.boundary;r.exit.boundary->current=pair.second;r.exit.boundary->previous=pair.first;
        r.entry.copiedContext[0x28]=r.exit.copiedContext[0x28]=std::byte{1};
        CHECK(CycleGuardedCommit(r,f.input.identity,0,parent,false,pair.first==6));
        for(unsigned mutation=0;mutation<7;++mutation){auto bad=r;
            if(mutation==0)bad.entry.nativeParent=0;if(mutation==1)++bad.exit.thread;
            if(mutation==2)bad.entry.copiedContext[0x28]=bad.exit.copiedContext[0x28]=std::byte{0};
            if(mutation==3)++bad.exit.boundary->next;if(mutation==4)bad.entry.boundary.timer=bad.exit.boundary->timer=.1f;
            if(mutation==5)--bad.exit.boundary->loaded;if(mutation==6){bad.entry.boundary.current=2;bad.entry.boundary.next=3;bad.exit.boundary->current=bad.exit.boundary->next=3;bad.exit.boundary->previous=2;}
            CHECK(!CycleGuardedCommit(bad,f.input.identity,0,parent,false,pair.first==6));
        }
        // A structurally correct direct child is insufficient without its
        // currently open evaluated Update in this service instance.
        retainedCommit=r;
    }f.service.Commit(retainedCommit);CHECK(f.service.View(f.clock).phase==Bc2NativeCyclePhase::Cancelled);return 0;
}

int EmptyShotRecoveryRetainsZeroAndStillRequiresGesture(){RecoveryFixture f;
    for(auto& branch:f.input.branches){branch.currentState=6;branch.previousState=5;branch.nextState=1;branch.phaseTimer=.7f;branch.loaded=0;}
    for(unsigned b=0;b<3;++b)f.Shot(b,1);
    for(unsigned p=0;p<2;++p){f.Advance();for(unsigned b=0;b<3;++b){f.Begin(b);f.Finish();}}
    CHECK(f.service.View(f.clock).held&&f.service.View(f.clock).loaded==0);CHECK(f.service.BeginRecovery(f.clock));CHECK(f.Grant());
    CHECK(f.service.RecoveryView(f.clock)->authority->grant.debt.loaded==0&&f.service.View(f.clock).blocksFire);
    CHECK(!f.service.YieldForReload());f.input.branches[0].reserve=23;f.Idle(0);
    CHECK(f.service.View(f.clock).phase==Bc2NativeCyclePhase::Cancelled&&!f.service.RecoveryView(f.clock)->ready);return 0;}
struct PumpBridge {
    RecoveryFixture native;RigSnapshot rig;std::shared_ptr<Bc2PumpCalibration> calibration=std::make_shared<Bc2PumpCalibration>();
    Bc2PhysicalPump pump;Bc2PhysicalPumpSample sample;Bc2PhysicalPumpResult result;Bc2PumpRawContact prior;
    unsigned acknowledged=0,cancelled=0;bool tracking=true;
    PumpBridge():pump(calibration,Api()){
        native.control.mechanism={0x5350415350554d50ull,native.control.item.generation};native.service.Control(native.control);
        rig.names={"root","jntWpn_1","jntWpn_4","LeftHand"};rig.parents={-1,0,1,0};rig.weaponBone=1;
        rig.identity={0x11000,0x12000,0x30000,0x40000,0x50000,0x60000,0x70000,0x80000,0x90000,4,false};
        rig.world.assign(4,Pose());rig.inverseBind.assign(4,Pose());rig.evaluatedWorld.assign(4,Pose());rig.nativeEvaluated.resize(4);
        calibration->revision=1;calibration->rigFingerprint=bc2_pump_detail::DerivePart(rig)->fingerprint;
        calibration->closedPart=Pose();calibration->closedPart.values[3][2]=-.84f;
        calibration->closedWrist=Pose();calibration->rearDirection=1;calibration->measured=true;
    }
    Bc2PhysicalPumpApi Api(){return {this,
        [](void* p,const Bc2NativeCycleControl& c)noexcept{return static_cast<PumpBridge*>(p)->native.service.Control(c);},
        [](void* p,std::int64_t now)noexcept->std::optional<Bc2NativeCycleView>{return static_cast<PumpBridge*>(p)->native.service.View(now);},
        [](void* p,const WeaponCycleReady& r)noexcept{return static_cast<PumpBridge*>(p)->native.service.AcknowledgeReady(r);},
        [](void* p)noexcept{auto& x=*static_cast<PumpBridge*>(p);++x.cancelled;x.native.service.BeginRecovery(x.native.clock);},
        {[](void* p,std::int64_t now)noexcept{return static_cast<PumpBridge*>(p)->native.service.RecoveryView(now);},
         [](void* p,const WeaponCycleIdleDebtRelease& r,const HandInteractionSample& input)noexcept{return static_cast<PumpBridge*>(p)->native.service.SubmitRecovery(r,input);},
         [](void* p,const WeaponCycleIdleDebtReady& r)noexcept{auto& x=*static_cast<PumpBridge*>(p);
            const auto okay=x.native.service.AcknowledgeRecovery(r,x.native.clock);if(okay)++x.acknowledged;return okay;}}};}
    void Tick(float travel,bool grip=true){
        native.Advance(grip);
        if(native.service.Recovering())native.Rounds();
        else for(unsigned b=0;b<3;++b){native.Begin(b);native.Finish();}
        sample.nativeOwner=native.control.nativeOwner;sample.input=native.control.input;sample.input.nowNs=++native.clock;
        sample.input.tracked[0]=tracking;sample.item=native.control.item;sample.asset=SpasReloadAsset;sample.grip=grip;
        native.hands.Update(sample.input);
        const auto gun=native.hands.Current(InteractionHand::Right);
        if(gun)native.hands.Renew(sample.input,gun->token,{{1,99},sample.input.sequence,sample.input.deadlineNs,true});
        else native.hands.Acquire(sample.input,{sample.input.owner,InteractionHand::Right,HandClaimKind::GunHold,sample.item,
            {{1,99},sample.input.sequence,sample.input.deadlineNs,true},++native.intent,0});
        sample.raw=prior;result=pump.Tick(sample,native.hands,native.intent);
        auto pose=Pose();pose.values[3][2]=travel;
        prior=BuildPumpRawContact(result.tracking,rig,pose,Pose(),{.02f,0,0},1,sample.input.nowNs);
    }
};
int RealPumpAdapterTrackingLossRegrabThroughNativeService(){PumpBridge p;CHECK(p.native.Held());p.Tick(0,false);p.Tick(0,false);p.Tick(0);p.Tick(0);
    CHECK(p.result.tracking.target&&p.result.ownsHand);p.Tick(.02f);p.tracking=false;p.Tick(.03f,false);
    CHECK(p.cancelled==1&&p.native.service.Recovering()&&!p.result.ownsHand&&p.result.blocksFire);
    p.tracking=true;for(unsigned n=0;n<6;++n)p.Tick(0,false);
    p.Tick(0);p.Tick(0);CHECK(p.result.tracking.recoveryTarget&&!p.result.tracking.target);
    for(float t:{.02f,.04f,.06f,.08f,SpasObservedForeEndStroke,SpasObservedForeEndStroke,SpasObservedForeEndStroke,SpasObservedForeEndStroke,
                 .075f,.055f,.035f,.015f,0.f,0.f,0.f,0.f,0.f,0.f})p.Tick(t);
    for(unsigned n=0;n<6;++n)p.Tick(0);
    CHECK(p.acknowledged==1&&!p.result.blocksFire&&p.native.service.View(p.native.clock).shot==1);
    CHECK(p.native.service.View(p.native.clock).loaded==7&&p.native.service.View(p.native.clock).reserve==24);return 0;}

}
int main(){CHECK(!EmptyShotRecoveryRetainsZeroAndStillRequiresGesture());CHECK(!GuardedCommitsRequireOwnOpenParentAndConservedProjection());CHECK(!RestoreNeverVotesAndBusyRewindsRevokeOnlyUnsubmittedAuthority());CHECK(!ExactGuardedIdleRecordMutations());CHECK(!RealPumpAdapterTrackingLossRegrabThroughNativeService());CHECK(!NativeIdleRequiresActualGesture());CHECK(!LongTrackingLossRetainsDebt());CHECK(!MissingGuardAndConservationNeverRecover());
    CHECK(!NativeBusyRevokesGrantButNotSubmittedReceipt());CHECK(!ExistingHoldDrainsBeforeConvergence());CHECK(!NoConversionOfNormalPendingOrStaleHold());return 0;}
