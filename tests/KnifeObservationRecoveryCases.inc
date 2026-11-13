namespace knife_observation_recovery_cases {
using namespace fvr::bc2::chestKnifeNative;
struct Held {
    Fixture f;ObservationRecovery recovery;ObservationRecoveryProof proof;View view;ChestKnifeMotionCalibration calibration;
    InputFrame input;fvr::math::Matrix4 measured;KnifeRecognitionCoordinator projection;
    private_knife::KnifeAttackGate attacks;private_knife::StockKnifeMotionSourceRevocation source;
    bool Init(){
        f.s.source.owner.actor=(std::uint64_t(0x30000)<<32)|0x20000;
        f.s.holdInputCurrent=f.s.actionInputCurrent=f.s.stockTriggerInputCurrent=true;
        if(!f.Held())return false;
        view.owned=view.healthy=true;view.anchor.knifeSlot=7;view.anchor.knifeWeapon=42;
        view.currentHoldWork={1,7,1,true,true};view.snapshot.selected.current={7,7,7};
        view.snapshot.selectedOwner={0x10000,0x20000,0x30000,42,f.s.source.owner.actorGeneration,3,4};
        for(auto* b:{&view.snapshot.knife.client.branches[0],&view.snapshot.knife.client.branches[1],&view.snapshot.knife.server}){
            b->currentState=b->nextState=2;b->loaded=b->reserve=-1;}
        measured.values={{{1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,0,1}}};
        input.focused=input.headValid=true;input.trackingBasisGeneration=9;input.hands[1].gripTracked=true;
        recovery.IdleNow();return true;
    }
    void NativeView(){
        view.snapshot.sequence=f.s.source.sequence;view.snapshot.observedNs=f.s.source.nowNs;
        view.observationRecovery=RefreshObservationRecoveryProof(proof,recovery.Read(),1,7);
        view.currentObservationReady=view.observationRecovery.state.ready;
    }
    std::optional<KnifeCommand> Gather(bool advance=true){
        auto& s=f.s;
        if(advance){++s.source.sequence;s.source.observedNs+=10*ms;s.source.nowNs=s.source.observedNs;
            s.source.deadlineNs=s.source.nowNs+100*ms;s.claimIntent=s.source.sequence;
            s.menu.deadlineNs=s.source.deadlineNs;++s.menu.sequence;}
        f.hands.Update(s.source);s.heldContact={{103,1},s.source.sequence,s.source.deadlineNs,true};NativeView();
        const auto captured=view.observationRecovery.state;
        if(RecoveryHoldCurrent(view,s,f.knife,f.hands)&&recovery.Rejoin(captured,s.source.observedNs)){
            proof=RefreshObservationRecoveryProof({captured,1,7,s.source.sequence,s.source.observedNs,s.source.nowNs},recovery.Read(),1,7);
            view.observationRecovery=proof;
            view.currentObservationReady=view.observationRecovery.state.ready;
        }
        const bool ready=ObservationSourceReady(view,f.knife.Phase());s.stockTriggerReady=ready;
        input.generation=s.source.sequence;input.spaceGeneration=s.source.owner.space;
        input.predictedNs=s.source.observedNs;input.menu=s.menu;
        const ChestKnifeMotionIdentity identity{view.snapshot.selectedOwner,*f.knife.Hold(),{42,1},7,1,1,9,1.f,
            view.observationRecovery.state.epoch,view.observationRecovery.state.revision};
        PrepareChestKnifeMotionSource(calibration,f.knife,identity,measured,input,ready,s,&projection);
        if(!ready){calibration.Reset();f.knife.InvalidateMotion();s.nativeAttackReady=s.motionTrackingReady=false;}
        source.Advance(s,f.knife.NeedsNativeCleanup());return f.knife.Prepare(s,f.hands);
    }
    bool Rest(){for(unsigned n=0;n<20;++n)if(Gather())return false;return f.knife.RecognitionReady(f.s.source);}
    std::optional<KnifeCommand> Jab(){
        for(unsigned n=0;n<9;++n){const auto axis=f.s.direction;auto& p=input.hands[1].grip.position;
            p.x+=axis.x*.012f;p.y+=axis.y*.012f;p.z-=axis.z*.012f;
            if(const auto c=Gather())return c;}return {};
    }
    bool Finish(const KnifeCommand& c){
        const auto failed=[](const char* step){std::fprintf(stderr,"Knife recovery Finish: %s\n",step);return false;};
        if(!f.knife.CanCommit(c,f.s,f.hands))return failed("portable admission");
        if(!recovery.TakeReady(f.s.source.nowNs))return failed("native recovery admission");
        if(!attacks.Stage(c,f.s.source.nowNs))return failed("native attack stage");
        view.currentHoldWork.attackPending=view.currentHoldWork.outstanding=true;
        const auto staged=source.Stamp();
        // Actual same-packet gather before the first original consumer must
        // service the pending stage without revoking its unchanged source.
        if(Gather(false))return failed("duplicate source produced a command");
        if(!source.Current(staged))return failed("duplicate source revoked the stage");
        if(!f.s.nativeAttackReady)return failed("duplicate source lost native readiness");
        if(!f.knife.CanCommit(c,f.s,f.hands))return failed("duplicate source lost portable admission");
        if(!attacks.Consume(1,c.source.nowNs,source.Current(staged)))return failed("client consumer");
        if(!attacks.Consume(2,c.source.nowNs,true))return failed("server consumer");
        // Match native BeforePrepare: deliver the accepted gate result before
        // portable commitment and completion; consumption alone is not delivery.
        const auto accepted=attacks.ResolveBeforeSource(f.s.source.sequence,f.s.source.observedNs,f.s.source.nowNs);
        if(accepted!=KnifeCommit::Accepted)return failed("accepted result delivery");
        f.knife.Committed(c,*accepted);
        for(unsigned n=0;n<3;++n)attacks.Observe(n,2,5,6);
        if(!attacks.Complete(c.serial,true,true))return failed("native completion");
        if(!f.knife.Observe(f.Receipt(c)))return failed("portable receipt");
        view.currentHoldWork.attackPending=view.currentHoldWork.outstanding=false;view.currentHoldWork.drawOnly=false;
        recovery.IdleNow();return true;
    }
};
bool NativeProofReady(const ObservationRecoveryProof& proof){
    ChestKnifeLiveObservation live;live.bodyOperation=7;live.input.sequence=proof.input;
    live.input.observedNs=live.input.nowNs=proof.nowNs;
    live.currentObservationReady=proof.state.ready;live.observationRecovery=proof;
    return ChestKnifeCurrentNativeReady(live);
}
int RetiredCompletionCases(){
    // Both legal race orders: the retired callback completes after Rejoin's
    // CAS but before proof creation, or after the new proof is already live.
    for(unsigned beforeProof=0;beforeProof<2;++beforeProof){
        ObservationRecovery r;r.IdleNow();const auto retired=r.Miss();
        r.Reset();r.IdleNow();const auto fresh=r.Miss();r.Returned(fresh,20,true);
        const auto captured=r.Read();CHECK(r.Rejoin(captured,21));const auto joined=r.Read();
        if(beforeProof)r.Returned(retired,90,true);
        const auto proof=RefreshObservationRecoveryProof({captured,1,7,3,21,21},r.Read(),1,7);
        CHECK(proof.state.completedNs==20&&NativeProofReady(proof));
        if(!beforeProof)r.Returned(retired,90,true);
        const auto current=r.Read();const auto refreshed=RefreshObservationRecoveryProof(proof,current,1,7);
        CHECK(current.stamp==joined.stamp&&current.ready&&current.recovered&&!current.pending);
        CHECK(current.completedNs==90&&refreshed.state.stamp==current.stamp&&refreshed.state.completedNs==20);
        CHECK(NativeProofReady(refreshed));
        // Completion retention grants no new lifetime/body/revision authority.
        CHECK(!NativeProofReady(RefreshObservationRecoveryProof(proof,current,2,7)));
        CHECK(!NativeProofReady(RefreshObservationRecoveryProof(proof,current,1,8)));
        const auto next=r.Miss();const auto pending=RefreshObservationRecoveryProof(proof,r.Read(),1,7);
        CHECK(pending.state.pending&&!pending.state.ready&&!pending.state.recovered&&!NativeProofReady(pending));
        r.Returned(next,100,true);CHECK(!r.Rejoin(r.Read(),100));
        CHECK(r.Rejoin(r.Read(),101));
        CHECK(!NativeProofReady(RefreshObservationRecoveryProof(proof,r.Read(),1,7)));
    }
    // A newer timestamp can finish first: the final pending decrement must
    // still expose the maximum completion before a fresh source may rejoin.
    ObservationRecovery ordered;ordered.IdleNow();const auto first=ordered.Miss(),second=ordered.Miss();
    ordered.Returned(second,30,true);CHECK(ordered.Read().pending&&!ordered.Rejoin(ordered.Read(),31));
    ordered.Returned(first,20,true);CHECK(ordered.Read().completedNs==30&&!ordered.Rejoin(ordered.Read(),30));
    CHECK(ordered.Rejoin(ordered.Read(),31));
    // The successful gap proof survives an accepted native attack in this
    // same hold; TakeReady changes epoch and IdleNow restores idle readiness.
    Held h;CHECK(h.Init());const auto retired=h.recovery.Miss();
    h.recovery.Reset();h.recovery.IdleNow();const auto fresh=h.recovery.Miss();
    h.recovery.Returned(fresh,h.f.s.source.nowNs+1,true);CHECK(h.Rest());
    const auto completed=h.proof.state.completedNs;const auto jab=h.Jab();
    CHECK(jab);CHECK(jab->stroke&&!jab->stockTriggerHold&&h.f.s.stockTrigger==0);CHECK(h.Finish(*jab));
    const auto afterAttack=h.recovery.Read();CHECK(afterAttack.epoch!=h.proof.state.epoch&&afterAttack.ready);
    h.recovery.Returned(retired,h.f.s.source.nowNs+100*ms,true);
    CHECK(h.recovery.Read().stamp==afterAttack.stamp&&!h.Gather());
    CHECK(h.view.observationRecovery.state.completedNs==completed&&NativeProofReady(h.view.observationRecovery));
    return 0;
}
int Cases(){
    CHECK(!RetiredCompletionCases());
    Held h;CHECK(h.Init()&&h.Rest());const auto claim=*h.f.knife.Hold();
    // Shared custody, measured motion and native attack gate: repeated idle
    // misses retain the claim, discard partial strokes and rearm from quiet.
    for(unsigned round=0;round<3;++round){
        const auto axis=h.f.s.direction;auto& p=h.input.hands[1].grip.position;
        p.x+=axis.x*.024f;p.y+=axis.y*.024f;p.z-=axis.z*.024f;CHECK(!h.Gather());
        const auto epoch=h.recovery.Miss();CHECK(epoch&&h.recovery.Read().pending);
        CHECK(!h.Gather()&&!h.view.currentObservationReady&&!h.f.knife.RecognitionReady(h.f.s.source));
        CHECK(!h.recovery.Rejoin(h.recovery.Read(),h.f.s.source.observedNs+1));
        h.recovery.Returned(epoch,h.f.s.source.nowNs+1,true);
        CHECK(!h.Gather()&&h.view.currentObservationReady&&!h.projection.calibrationContinuous);
        CHECK(*h.f.knife.Hold()==claim&&!h.f.knife.RecognitionReady(h.f.s.source));
        CHECK(h.Rest());const auto jab=h.Jab();CHECK(jab&&jab->stroke&&!jab->stockTriggerHold&&h.f.s.stockTrigger==0);
        CHECK(h.Finish(*jab));CHECK(h.Rest());
        for(unsigned n=0;n<30;++n)CHECK(!h.Gather()&&*h.f.knife.Hold()==claim);
    }
    ObservationRecovery concurrent;concurrent.IdleNow();const auto one=concurrent.Miss(),two=concurrent.Miss();
    CHECK(one&&two==one);concurrent.Returned(one,10,true);CHECK(!concurrent.Rejoin(concurrent.Read(),11));
    concurrent.Returned(two,12,true);const auto before=concurrent.Read();CHECK(!concurrent.Rejoin(before,12));
    const auto three=concurrent.Miss();CHECK(!concurrent.Rejoin(before,13));concurrent.Returned(three,14,true);
    CHECK(concurrent.Rejoin(concurrent.Read(),15)&&concurrent.Read().ready);
    const auto leave=concurrent.Miss(concurrent.IdleEpoch());concurrent.Returned(leave,16,true);
    CHECK(concurrent.Rejoin(concurrent.Read(),17));
    // Unknown read/unwind, pending native work, crossed idle epoch and retired
    // callbacks cannot be rehabilitated by a later Ready snapshot.
    for(unsigned fault=0;fault<5;++fault){ObservationRecovery r;r.IdleNow();const auto e=r.Miss();
        if(fault==0)r.Unknown();if(fault==1)r.Revoke(10);
        if(fault==2)r.Returned(e,10,false);else r.Returned(e,10,true);
        if(fault==3){r.Revoke(11);CHECK(!r.Miss());}
        if(fault==4){r.Reset();r.IdleNow();const auto fresh=r.Miss();r.Returned(e,12,true);
            CHECK(r.Read().pending&&!r.Rejoin(r.Read(),13));r.Returned(fresh,14,false);}
        r.IdleNow();CHECK(!r.Rejoin(r.Read(),20)&&!r.Read().ready);
    }
    ObservationRecovery stage;stage.IdleNow();const auto missed=stage.Miss();CHECK(!stage.TakeReady(10));
    stage.Returned(missed,11,true);CHECK(stage.Rejoin(stage.Read(),12)&&stage.TakeReady(13));
    CHECK(!stage.Miss()&&stage.Read().blocked);
    // These are the production current-hold/native-work predicates, joined to
    // a real portable claim, rather than independent all-true recovery flags.
    for(unsigned fault=0;fault<24;++fault){Held x;CHECK(x.Init());CHECK(!x.Gather());
        const auto e=x.recovery.Miss();x.recovery.Returned(e,x.f.s.source.nowNs-1,true);x.NativeView();
        CHECK(RecoveryHoldCurrent(x.view,x.f.s,x.f.knife,x.f.hands));
        switch(fault){
        case 0:x.view.currentHoldWork.outstanding=true;break;case 1:x.view.currentHoldWork.commandPending=true;break;
        case 2:x.view.currentHoldWork.attackPending=true;break;case 3:x.view.currentHoldWork.returnReady=true;break;
        case 4:x.view.currentHoldWork.closing=true;break;case 5:x.view.currentHoldWork.attackNative=true;break;
        case 6:x.view.currentHoldWork.bridgePending=true;break;case 7:x.view.currentHoldWork.known=false;break;
        case 8:x.view.healthy=false;break;case 9:++x.view.snapshot.selectedOwner.soldier;break;
        case 10:++x.view.snapshot.selectedOwner.actorGeneration;break;case 11:++x.view.snapshot.selectedOwner.space;break;
        case 12:x.view.snapshot.selected.current[1]=0;break;case 13:x.view.snapshot.knife.server.currentState=5;break;
        case 14:x.view.snapshot.knife.client.branches[1].loaded=0;break;case 15:--x.view.snapshot.sequence;break;
        case 16:x.f.s.stockTrigger=1;break;case 17:x.f.s.source.focused=false;break;
        case 18:x.f.s.source.tracked[1]=false;break;case 19:x.f.s.fireSuppressed=false;break;
        case 20:x.f.s.trackedKnifeOwned=false;break;case 21:x.f.s.source.nowNs=x.f.s.source.deadlineNs;break;
        case 22:++x.f.s.knife.generation;break;case 23:x.f.s.menu.owner=InputMenuOwner::Menu;break;
        }
        CHECK(!RecoveryHoldCurrent(x.view,x.f.s,x.f.knife,x.f.hands));
    }
    std::puts("Knife observation recovery: repeated Held/jab/postattack, pending-source continuity, retired completion proof and ownership negatives PASS.");
    return 0;
}
}
