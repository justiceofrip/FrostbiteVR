// Independent review of frozen recovery242 + native243, not live evidence.
#define main FrozenRecoveryRegressionMain
#include "Bc2NativeCycleRecoveryServiceTests.cpp"
#undef main
#include "Bc2NativeCycleUpdateRoute.h"

int CompleteRecovery(RecoveryFixture& f){
    CHECK(f.Held()&&f.service.BeginRecovery(f.clock)&&f.Grant());
    const auto authority=*f.service.RecoveryView(f.clock)->authority;
    CHECK(f.Advance());
    const WeaponCycleIdleDebtRelease release{authority,1,f.control.input.sequence,f.control.input.observedNs,
        std::min(f.control.input.deadlineNs,authority.deadlineNs)};
    CHECK(f.service.SubmitRecovery(release,f.control.input));f.Rounds();
    const auto ready=f.service.RecoveryView(f.clock)->ready;
    CHECK(ready&&f.service.AcknowledgeRecovery(*ready,f.clock));
    CHECK(f.service.View(f.clock).phase==Bc2NativeCyclePhase::Complete&&!f.service.View(f.clock).blocksFire);
    for(auto& b:f.input.branches){b.currentState=b.nextState=2;b.previousState=1;b.phaseTimer=0;}
    return 0;
}
int RecoveredServiceCanObserveNextShot(){
    for(bool m95:{false,true}){
        RecoveryFixture f(m95);CHECK(!CompleteRecovery(f));CHECK(f.Advance());
        const auto before=f.input.branches[0].loaded;
        for(auto& b:f.input.branches){--b.loaded;b.currentState=m95?8:7;b.previousState=m95?7:6;b.nextState=m95?1:8;b.phaseTimer=m95?2.2f:.2f;}
        for(unsigned b=0;b<3;++b)f.Shot(b,before);
        for(unsigned pass=0;pass<2;++pass){CHECK(f.Advance());for(unsigned b=0;b<3;++b){f.Begin(b);f.Finish();}}
        const auto view=f.service.View(f.clock);
        CHECK(view.held&&view.cycle==2&&view.shot==2&&view.loaded==before-1&&view.blocksFire);
        CHECK(!f.service.RecoveryView(f.clock));
        CHECK(f.Release());for(unsigned b=0;b<3;++b)f.Resume(b,m95);
        CHECK(f.service.View(f.clock).ready&&f.service.AcknowledgeReady(*f.service.View(f.clock).ready));
        CHECK(!f.service.View(f.clock).blocksFire);
    }
    return 0;
}
int RecoveredServiceCanYieldAndSelect(){
    RecoveryFixture f;CHECK(!CompleteRecovery(f));
    CHECK(f.service.YieldForReload());CHECK(f.service.View(f.clock).phase==Bc2NativeCyclePhase::Watching);
    Bc2NativeCycleSelection next;next.mode=Bc2NativeCycleMode::Spas;next.nowNs=++f.clock;
    next.config=f.input.config;next.reserve.identity=f.input.identity;next.reserve.verified=next.reserve.allThreeIdle=true;
    next.reserve.sequence=1;next.reserve.observedNs=f.clock;next.reserve.deadlineNs=f.clock+100000000;
    next.reserve.capacity=8;next.reserve.loaded=7;next.reserve.reserve=24;
    CHECK(f.service.Select(next)&&f.Advance());CHECK(!f.service.View(f.clock).blocksFire&&!f.service.RecoveryView(f.clock));
    return 0;
}
int QueuedNormalCompletionStaysOnOriginalChannel(){
    RecoveryFixture f;CHECK(f.Held()&&f.FullStroke());
    CHECK(f.service.SuspendCompletion());CHECK(!f.service.BeginRecovery(f.clock));
    for(unsigned b=0;b<3;++b)f.Resume(b);
    const auto ready=f.service.View(f.clock).ready;CHECK(ready&&!f.service.RecoveryView(f.clock));
    f.clock+=61000000000ll;f.service.Cancel(Bc2NativeCycleFailure::Control);
    const auto retained=f.service.View(f.clock).ready;
    CHECK(retained&&retained->release==ready->release&&retained->observedNs==ready->observedNs&&
        retained->deadlineNs==ready->deadlineNs&&f.service.AcknowledgeReady(*ready));
    CHECK(!f.service.View(f.clock).blocksFire&&f.service.YieldForReload());return 0;
}
int RealPumpRecoveryCanPumpNextShot(){
    PumpBridge p;CHECK(p.native.Held());p.Tick(0,false);p.Tick(0,false);p.Tick(0);p.Tick(0);p.Tick(.02f);
    p.tracking=false;p.Tick(.03f,false);CHECK(p.native.service.Recovering());
    p.tracking=true;for(unsigned n=0;n<6;++n)p.Tick(0,false);
    p.Tick(0);p.Tick(0);CHECK(p.result.tracking.recoveryTarget);
    for(float t:{.02f,.04f,.06f,.08f,SpasObservedForeEndStroke,SpasObservedForeEndStroke,SpasObservedForeEndStroke,SpasObservedForeEndStroke,
        .075f,.055f,.035f,.015f,0.f,0.f,0.f,0.f,0.f,0.f})p.Tick(t);
    for(unsigned n=0;n<6;++n)p.Tick(0);
    CHECK(p.acknowledged==1&&!p.result.blocksFire);
    const auto before=p.native.service.View(p.native.clock).loaded;
    for(auto& b:p.native.input.branches){b.loaded=before-1;b.currentState=7;b.previousState=6;b.nextState=8;b.phaseTimer=.2f;}
    CHECK(p.native.Advance());for(unsigned b=0;b<3;++b)p.native.Shot(b,before);
    for(unsigned n=0;n<6;++n)p.Tick(0,false);
    p.Tick(0);p.Tick(0);CHECK(p.result.tracking.target&&!p.result.tracking.recoveryTarget);
    for(float t:{.02f,.04f,.06f,.08f,SpasObservedForeEndStroke,SpasObservedForeEndStroke,SpasObservedForeEndStroke,SpasObservedForeEndStroke,
        .075f,.055f,.035f,.015f,0.f,0.f,0.f,0.f,0.f,0.f})p.Tick(t);
    CHECK(p.native.service.View(p.native.clock).phase==Bc2NativeCyclePhase::Releasing);
    for(unsigned b=0;b<3;++b)p.native.Resume(b);
    for(unsigned n=0;n<6;++n)p.Tick(0);
    CHECK(!p.result.blocksFire&&p.native.service.View(p.native.clock).shot==2&&p.native.service.View(p.native.clock).loaded==before-1);
    return 0;
}
int ConversionBetweenCachedRouteAndDecisionLeavesOriginalUnguarded(){
    RecoveryFixture f;CHECK(f.Held());
    // Match Runtime::Update's separate WithNativeCycle sections: first read
    // the route, then let the real cancellation callback convert held debt,
    // then evaluate the already-chosen normal branch. This is a deterministic
    // ordering witness, not a native scheduling/first-failure claim.
    const bool cachedConverging=f.service.Recovering();CHECK(!cachedConverging);
    CHECK(f.service.BeginRecovery(f.clock)&&f.service.Recovering());
    const auto legacyDecision=f.Begin(0);
    CHECK(!legacyDecision.tracked&&!legacyDecision.hold);
    // The normal runtime branch calls its original with no NativeGuardFrame
    // and no input suppression. An untracked Finish also records no recovery
    // observation. Even a timer-only original is invisible to guard accounting.
    f.Finish();CHECK(f.service.Recovering()&&f.service.RecoveryUpdates()==0);
    // If this original reaches Step (including prediction-corrected idle),
    // the actual no-NativeGuardFrame Step branch invokes Missing(firing,0).
    f.service.Missing(f.input.identity.firing[0],0);
    CHECK(f.service.View(f.clock).phase==Bc2NativeCyclePhase::Cancelled&&
        f.service.View(f.clock).failure==Bc2NativeCycleFailure::Owner&&f.service.View(f.clock).blocksFire);
    return 0;
}
void PrepareRouteUpdate(RecoveryFixture& f,unsigned branch){
    f.input.branch=branch;f.input.contextObservedNs=f.input.nowNs=++f.clock;f.input.leaseDeadlineNs=f.clock+100000000;
    f.update={};f.update.id=++f.event;auto& a=f.update.entry;
    a.kind=ReloadFlowEvent::Update;a.nativeInvocation=a.nativeUpdate=a.update=f.event;a.thread=7;a.depth=1;a.caller=0x6e90b0;a.context=0x24000;
    a.nowNs=f.clock;a.contextCopied=true;a.boundary=f.Boundary(branch);Put(a.copiedContext,0x18,f.input.context.deltaSeconds);Put(a.copiedContext,0x20,1.f);
    a.copiedContext[0x24]=std::byte{1};Put(a.copiedContext,0x2c,f.input.context.inputFlags);
    f.update.exit.thread=a.thread;f.update.exit.nowNs=++f.clock;f.update.exit.boundary=a.boundary;
    f.update.exit.contextCopied=true;f.update.exit.copiedContext=a.copiedContext;f.update.finished=f.update.identityRetained=true;
}
int HeldActionInputConvertsSameUpdateAndPreservesDebt(){
    for(bool m95:{false,true})for(bool empty:{false,true})for(unsigned action:{1u,2u,4u,7u,8u,16u}){
        RecoveryFixture f(m95);
        if(empty){
            for(auto& b:f.input.branches){b.loaded=0;b.currentState=6;b.previousState=5;b.nextState=1;b.phaseTimer=.2f;}
            for(unsigned b=0;b<3;++b)f.Shot(b,1);
            for(unsigned pass=0;pass<2;++pass){CHECK(f.Advance());for(unsigned b=0;b<3;++b){f.Begin(b);f.Finish();}}
            CHECK(f.service.View(f.clock).held);
        }else CHECK(f.Held());
        const auto debt=f.service.View(f.clock);const auto original=*debt.held;
        auto& context=f.input.context;context.inputFlags=action&7u;
        context.fireRequested=bool(action&1u);context.orderRequested=bool(action&2u);context.reloadRequested=bool(action&4u);
        context.flags24Through28[3]=bool(action&8u);context.flags24Through28[4]=bool(action&16u);
        PrepareRouteUpdate(f,0);
        f.update.entry.copiedContext[0x27]=std::byte(context.flags24Through28[3]);
        f.update.entry.copiedContext[0x28]=std::byte(context.flags24Through28[4]);
        f.update.exit.copiedContext=f.update.entry.copiedContext;
        // Reproduce the prior terminal Input path without changing the live
        // service used below. This is modeled native input, not a headset proof.
        auto legacy=f.service;legacy.Evaluate(f.input,f.update.id);
        CHECK(legacy.Phase()==Bc2NativeCyclePhase::Cancelled&&legacy.View(f.clock).failure==Bc2NativeCycleFailure::Input);
        const NativeCycleRecoveryScope scope{f.input.identity,f.input.config};
        const auto route=EvaluateNativeCycleUpdateRoute(f.service,{&f.input,&scope,&f.update.entry,
            f.input.identity.firing[0],f.update.id,m95?5:8,f.input.leaseDeadlineNs});
        CHECK(route.recovering&&route.recovery&&!route.normal.tracked&&!route.normal.hold);
        NativeCycleInputGuard guard;guard.requested=guard.applied=guard.restored=true;
        guard.original={unsigned(context.flags24Through28[4]),context.inputFlags};guard.effective=guard.beforeRestore={1,0};
        f.update.exit.cycleGuard=guard;f.update.exit.cycleGuardEpoch=route.recovery.epoch;
        f.service.FinishRecovery(route.recovery,f.update,guard,0,0);
        CHECK(f.service.RecoveryUpdates()==1&&f.service.Recovering());
        CHECK(!f.service.FirstInputRejection()); // guarded action input is not a failed native callback
        const auto current=f.service.View(f.clock);
        CHECK(current.cycle==debt.cycle&&current.shot==debt.shot&&current.loaded==debt.loaded&&current.reserve==debt.reserve);
        CHECK(current.blocksFire&&!current.held&&!current.ready&&!f.service.YieldForReload());
        f.control.release=WeaponCycleRelease{original,1,f.control.input.sequence,f.clock,original.deadlineNs};
        CHECK(!f.service.Control(f.control));f.control.release.reset(); // old stroke cannot complete recovery
        context.inputFlags=0;context.fireRequested=context.orderRequested=context.reloadRequested=false;
        context.flags24Through28[3]=context.flags24Through28[4]=false;
        f.Rounds();CHECK(!f.service.RecoveryView(f.clock)->authority); // idle alone never forgives pump/bolt
        CHECK(f.Grant());const auto authority=*f.service.RecoveryView(f.clock)->authority;
        CHECK(f.Advance());const WeaponCycleIdleDebtRelease release{authority,2,f.control.input.sequence,
            f.control.input.observedNs,std::min(f.control.input.deadlineNs,authority.deadlineNs)};
        CHECK(f.service.SubmitRecovery(release,f.control.input));f.Rounds();const auto ready=f.service.RecoveryView(f.clock)->ready;
        CHECK(ready&&f.service.AcknowledgeRecovery(*ready,f.clock)&&!f.service.View(f.clock).blocksFire);
        CHECK(f.service.View(f.clock).loaded==debt.loaded&&f.service.View(f.clock).reserve==debt.reserve);
        CHECK(f.service.YieldForReload()); // equipment is no longer trapped after proven completion
    }return 0;
}
int InputRecoveryCannotReclassifyBadNativeEvidence(){
    for(bool m95:{false,true})for(unsigned mutation=0;mutation<15;++mutation){
        RecoveryFixture f(m95);CHECK(f.Held());f.input.context.inputFlags=1;f.input.context.fireRequested=true;
        PrepareRouteUpdate(f,0);
        if(mutation==0)++f.input.identity.owner.weapon;
        if(mutation==1)++f.input.identity.serverItem;
        if(mutation==2)++f.input.config.primaryFire;
        if(mutation==3)--f.input.branches[1].loaded;
        if(mutation==4)++f.input.branches[2].reserve;
        if(mutation==5)++f.input.capacities[1];
        if(mutation==6)f.input.context.flags24Through28[2]=true;
        if(mutation==7)f.input.context.deltaSeconds=-1;
        if(mutation==8)f.input.context.reloadTimeMultiplier=0;
        if(mutation==9)f.input.contextObservedNs=f.input.nowNs-Bc2NativeCycleService::ContextFreshNs-1;
        if(mutation==10)f.input.verified=false;
        if(mutation==11)f.input.context.inputFlags=9;
        if(mutation==12)f.input.context.fireRequested=false;
        if(mutation==13){f.input.context.inputFlags=0;f.input.context.fireRequested=false;} // ordinary neutral stays held
        if(mutation==14){f.service.Cancel(Bc2NativeCycleFailure::Restore);}
        CHECK(!f.service.BeginInputRecovery(f.input,f.update.id));
        CHECK(!f.service.Recovering()&&f.service.View(f.clock).blocksFire&&!f.service.View(f.clock).ready);
    }
    RecoveryFixture pending;CHECK(pending.Held()&&pending.Release());PrepareRouteUpdate(pending,0);
    pending.input.context.inputFlags=1;pending.input.context.fireRequested=true;
    CHECK(!pending.service.BeginInputRecovery(pending.input,pending.update.id));
    CHECK(pending.service.Phase()==Bc2NativeCyclePhase::Releasing);
    return 0;
}
int SerializedRouteAdmitsInterveningRecovery(){
    RecoveryFixture f;CHECK(f.Held());PrepareRouteUpdate(f,0);
    // Native reads already completed before cancellation's serialized turn.
    const NativeCycleRecoveryScope scope{f.input.identity,f.input.config};
    const Bc2NativeCycleUpdateEvidence evidence{&f.input,&scope,&f.update.entry,f.input.identity.firing[0],f.update.id,8,f.input.leaseDeadlineNs};
    CHECK(f.service.BeginRecovery(f.clock));
    const auto route=EvaluateNativeCycleUpdateRoute(f.service,evidence);
    CHECK(route.recovering&&route.recovery&&!route.normal.tracked);
    NativeCycleInputGuard guard;guard.requested=guard.applied=guard.restored=true;
    guard.original={0,0};guard.effective=guard.beforeRestore={1,0};
    f.update.exit.cycleGuard=guard;f.update.exit.cycleGuardEpoch=route.recovery.epoch;
    f.service.FinishRecovery(route.recovery,f.update,guard,0,0);
    CHECK(f.service.Recovering()&&f.service.RecoveryUpdates()==1&&f.Grant());return 0;
}
int SerializedRoutePreservesAlreadyIssuedHoldDrain(){
    RecoveryFixture f;CHECK(f.Held());PrepareRouteUpdate(f,1);
    const NativeCycleRecoveryScope scope{f.input.identity,f.input.config};
    const auto route=EvaluateNativeCycleUpdateRoute(f.service,{&f.input,&scope,&f.update.entry,
        f.input.identity.firing[1],f.update.id,8,f.input.leaseDeadlineNs});
    CHECK(!route.recovering&&route.normal.tracked&&route.normal.hold&&!route.recovery);
    f.decision=route.normal;CHECK(f.service.BeginRecovery(f.clock));f.Finish();
    CHECK(f.service.Recovering()&&f.Grant());return 0;
}
int SerializedRouteKeepsProofsAndReusesCompleteService(){
    for(unsigned mutation=0;mutation<4;++mutation){
        RecoveryFixture f;CHECK(f.Held());PrepareRouteUpdate(f,0);CHECK(f.service.BeginRecovery(f.clock));
        NativeCycleRecoveryScope scope{f.input.identity,f.input.config};
        Bc2NativeCycleUpdateEvidence evidence{&f.input,&scope,&f.update.entry,f.input.identity.firing[0],f.update.id,8,f.input.leaseDeadlineNs};
        if(mutation==0)evidence.recovery=nullptr;
        if(mutation==1)++scope.identity.owner.weapon;
        if(mutation==2)evidence.capacity=9;
        if(mutation==3)evidence.deadlineNs=f.update.entry.nowNs;
        const auto route=EvaluateNativeCycleUpdateRoute(f.service,evidence);
        CHECK(route.recovering&&!route.recovery&&!route.normal.tracked&&f.service.View(f.clock).blocksFire);
    }
    RecoveryFixture complete;CHECK(!CompleteRecovery(complete));PrepareRouteUpdate(complete,0);
    const NativeCycleRecoveryScope scope{complete.input.identity,complete.input.config};
    const auto route=EvaluateNativeCycleUpdateRoute(complete.service,{&complete.input,&scope,&complete.update.entry,
        complete.input.identity.firing[0],complete.update.id,8,complete.input.leaseDeadlineNs});
    CHECK(!route.recovering&&route.normal.tracked&&!route.normal.hold&&!route.recovery);
    complete.decision=route.normal;complete.Finish();CHECK(!complete.service.View(complete.clock).blocksFire);
    return 0;
}

int HeldInputAfterNativePauseMustNotBecomeTerminalTimeout(){
    for(bool m95:{false,true}){
        RecoveryFixture f(m95);CHECK(f.Held());const auto debt=f.service.View(f.clock);
        f.clock=f.control.input.deadlineNs+41000000;
        CHECK(!f.service.BeginRecovery(f.clock)); // Old holds alone are stale.
        for(unsigned pass=0;pass<2;++pass)for(unsigned b=0;b<3;++b){
            PrepareRouteUpdate(f,b);const NativeCycleRecoveryScope scope{f.input.identity,f.input.config};
            const auto route=EvaluateNativeCycleUpdateRoute(f.service,{&f.input,&scope,&f.update.entry,
                f.input.identity.firing[b],f.update.id,m95?5:8,f.input.leaseDeadlineNs});
            CHECK(route.normal.tracked&&route.normal.hold&&!route.recovering);
            f.decision=route.normal;f.Finish();
        }
        CHECK(f.service.View(f.clock).heldSuspended);
        CHECK(f.Advance());CHECK(f.service.Recovering());
        // A player's still-squeezed hand supplies no new grab. Native guarded
        // updates retain debt beyond the former three-second terminal timer.
        for(unsigned n=0;n<400;++n){
            CHECK(f.Advance());f.Rounds();const auto current=f.service.View(f.clock);
            const auto recovery=f.service.RecoveryView(f.clock);
            CHECK(current.phase==Bc2NativeCyclePhase::Converging&&current.blocksFire&&!current.held&&!current.ready);
            CHECK(current.cycle==debt.cycle&&current.shot==debt.shot&&current.loaded==debt.loaded&&current.reserve==debt.reserve);
            CHECK(recovery&&!recovery->authority&&!recovery->ready);
        }
        // Only an actual neutral input and fresh guarded idle receipts allow
        // a replacement full gesture; the old physical lease stays revoked.
        CHECK(f.Grant());const auto grant=*f.service.RecoveryView(f.clock)->authority;
        CHECK(f.Advance());const WeaponCycleIdleDebtRelease release{grant,1,f.control.input.sequence,
            f.control.input.observedNs,std::min(f.control.input.deadlineNs,grant.deadlineNs)};
        CHECK(f.service.SubmitRecovery(release,f.control.input));f.Rounds();
        const auto ready=f.service.RecoveryView(f.clock)->ready;CHECK(ready);
        CHECK(f.service.AcknowledgeRecovery(*ready,f.clock));CHECK(!f.service.View(f.clock).blocksFire);
        CHECK(f.service.YieldForReload());
    }
    return 0;
}
int ExpiredControllerKeepsExactNativeDebtUntilNeutral(){
    for(bool m95:{false,true}){
        RecoveryFixture f(m95);CHECK(f.Held());
        const auto original=f.service.View(f.clock);const auto packet=f.control.input;
        //309: server own-Update follows a 123ms gap, with fresh native owner
        // publication but the previous controller packet expired by 41ms.
        f.clock=packet.deadlineNs+41000000;
        CHECK(!f.service.BeginRecovery(f.clock));
        for(unsigned pass=0;pass<2;++pass)for(unsigned b=0;b<3;++b){
            PrepareRouteUpdate(f,b);const NativeCycleRecoveryScope scope{f.input.identity,f.input.config};
            const auto route=EvaluateNativeCycleUpdateRoute(f.service,{&f.input,&scope,&f.update.entry,
                f.input.identity.firing[b],f.update.id,m95?5:8,f.input.leaseDeadlineNs});
            CHECK(route.normal.tracked&&route.normal.hold&&!route.recovering);
            f.decision=route.normal;f.Finish();
        }
        const auto paused=f.service.View(f.clock);const auto watch=f.service.ObservationWatch(f.clock);
        CHECK(paused.phase==Bc2NativeCyclePhase::Held&&paused.heldSuspended&&paused.blocksFire);
        CHECK(!paused.held&&!paused.ready&&watch&&paused.heldSuspensions==1);
        CHECK(paused.cycle==original.cycle&&paused.shot==original.shot&&paused.loaded==original.loaded&&paused.reserve==original.reserve);
        CHECK(f.service.LastControlInput().observedNs==packet.observedNs&&f.service.LastControlInput().deadlineNs==packet.deadlineNs);
        // Immediate actual neutral still resumes the original held route.
        // A held-input return instead uses the recovery regression above.
        f.clock+=10000000;++f.control.input.sequence;
        f.control.input.nowNs=f.control.input.observedNs=f.clock;f.control.input.deadlineNs=f.clock+100000000;
        f.control.input.released=m95?std::array<bool,2>{false,true}:std::array<bool,2>{true,false};
        CHECK(f.service.Control(f.control));
        CHECK(!f.service.View(f.clock).heldSuspended);
        for(unsigned pass=0;pass<2;++pass)for(unsigned b=0;b<3;++b){f.Begin(b);f.Finish();}
        const auto resumed=f.service.View(f.clock);
        CHECK(resumed.held&&weapon_cycle_detail::Same(*original.held,*resumed.held)&&resumed.blocksFire&&!resumed.ready);
        CHECK(f.Release());for(unsigned b=0;b<3;++b)f.Resume(b,m95);
        CHECK(f.service.View(f.clock).ready&&f.service.AcknowledgeReady(*f.service.View(f.clock).ready));
        CHECK(!f.service.View(f.clock).blocksFire);
    }return 0;
}
int ExpiredControllerCannotHideChangedNativeEvidence(){
    for(unsigned mutation=0;mutation<12;++mutation){
        RecoveryFixture f;CHECK(f.Held());f.clock=f.control.input.deadlineNs+41000000;
        PrepareRouteUpdate(f,2);
        if(mutation==0)++f.input.identity.owner.weapon;
        if(mutation==1)++f.input.identity.serverItem;
        if(mutation==2)f.input.config.boltDelay=.6f;
        if(mutation==3)--f.input.branches[1].loaded;
        if(mutation==4)++f.input.branches[2].reserve;
        if(mutation==5)f.input.branches[0].currentState=8;
        if(mutation==6)f.input.context.fireRequested=true;
        if(mutation==7)f.input.context.reloadRequested=true;
        if(mutation==8)++f.input.capacities[2];
        if(mutation==9)f.input.verified=false;
        if(mutation==10)f.input.contextObservedNs=f.input.nowNs-Bc2NativeCycleService::ContextFreshNs-1;
        if(mutation==11)f.input.nowNs=f.control.input.deadlineNs+Bc2NativeCycleService::MaximumHoldSuspensionNs;
        CHECK(!f.service.Evaluate(f.input,f.update.id).tracked);
        const auto view=f.service.View(f.clock);
        CHECK(view.phase==Bc2NativeCyclePhase::Cancelled&&view.blocksFire&&!view.held&&!view.ready&&!view.heldSuspended);
    }return 0;
}
int ControllerGapBetweenOwnShotReceiptsCannotDiscardDebt(){
    for(bool m95:{false,true})for(unsigned first=0;first<3;++first){
        RecoveryFixture f(m95);f.Shot(first,m95?5:8);
        CHECK(f.service.View(f.clock).phase==Bc2NativeCyclePhase::ShotObserved);
        const auto debt=f.service.View(f.clock);const auto packet=f.control.input;
        f.clock=packet.deadlineNs+31000000;
        for(unsigned b=0;b<3;++b)if(b!=first)f.Shot(b,m95?5:8);
        CHECK(f.service.View(f.clock).phase==Bc2NativeCyclePhase::ShotObserved);
        CHECK(f.service.View(f.clock).heldSuspended&&!f.service.View(f.clock).held);
        for(unsigned pass=0;pass<2;++pass)for(unsigned b=0;b<3;++b){f.Begin(b);f.Finish();}
        const auto held=f.service.View(f.clock);
        CHECK(held.phase==Bc2NativeCyclePhase::Held&&held.heldSuspended&&!held.held&&!held.ready&&held.blocksFire);
        CHECK(held.cycle==debt.cycle&&held.shot==debt.shot&&held.loaded==debt.loaded&&held.reserve==debt.reserve);
        CHECK(f.service.LastControlInput().observedNs==packet.observedNs);
        f.clock+=10000000;++f.control.input.sequence;f.control.input.nowNs=f.control.input.observedNs=f.clock;
        f.control.input.deadlineNs=f.clock+100000000;f.control.input.released=m95?std::array<bool,2>{false,true}:std::array<bool,2>{true,false};
        CHECK(f.service.Control(f.control));
        for(unsigned pass=0;pass<2;++pass)for(unsigned b=0;b<3;++b){f.Begin(b);f.Finish();}
        CHECK(f.service.View(f.clock).held&&f.Release());
        for(unsigned b=0;b<3;++b)f.Resume(b,m95);
        CHECK(f.service.View(f.clock).ready&&f.service.AcknowledgeReady(*f.service.View(f.clock).ready));
        CHECK(!f.service.View(f.clock).blocksFire);
    }return 0;
}
int main(){
    CHECK(!HeldActionInputConvertsSameUpdateAndPreservesDebt());
    CHECK(!InputRecoveryCannotReclassifyBadNativeEvidence());
    CHECK(!HeldInputAfterNativePauseMustNotBecomeTerminalTimeout());
    CHECK(!ControllerGapBetweenOwnShotReceiptsCannotDiscardDebt());
    CHECK(!ExpiredControllerKeepsExactNativeDebtUntilNeutral());
    CHECK(!ExpiredControllerCannotHideChangedNativeEvidence());
    CHECK(!SerializedRouteAdmitsInterveningRecovery());
    CHECK(!SerializedRoutePreservesAlreadyIssuedHoldDrain());
    CHECK(!SerializedRouteKeepsProofsAndReusesCompleteService());
    std::puts("serialized route conversion/drain/negative/reuse regressions PASS");
    CHECK(!FrozenRecoveryRegressionMain());
    CHECK(!RecoveredServiceCanObserveNextShot());std::puts("post-recovery SPAS/M95 next normal shot PASS");
    CHECK(!RecoveredServiceCanYieldAndSelect());std::puts("post-recovery Yield/Select PASS");
    CHECK(!QueuedNormalCompletionStaysOnOriginalChannel());std::puts("queued original native completion PASS");
    CHECK(!RealPumpRecoveryCanPumpNextShot());std::puts("real pump recovery then next normal shot/stroke PASS");
    CHECK(!ConversionBetweenCachedRouteAndDecisionLeavesOriginalUnguarded());
    std::puts("REPRODUCED cached normal route + intervening BeginRecovery -> original has neither decision nor recovery guard");
    return 0;
}
