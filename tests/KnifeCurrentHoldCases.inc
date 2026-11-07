// Deterministic observations only. Native scalar fixtures are not game evidence.
struct CurrentHoldFixture {
    Fixture policy;
    ChestKnifeCurrentHoldObservation value;
    bool SetUp(){
        auto& c=value.current;c=LiveFixture();policy.s.source=c.input;policy.s.knife={c.knife.id,c.knife.generation};
        policy.Grip(false);policy.s.stockTriggerReady=policy.s.stockTriggerInputCurrent=true;
        const auto draw=policy.Draw();if(!draw||draw->kind!=KnifeCommandKind::Draw)return false;
        policy.knife.Committed(*draw,KnifeCommit::Accepted);
        auto receipt=policy.Receipt(*draw);receipt.attackObserved=receipt.attackSettled=false;
        receipt.originalSelectedHidden=receipt.fireSuppressed=false;
        if(!policy.knife.Observe(receipt))return false;
        value.drawCommand=*draw;value.drawReceipt=receipt;value.drawOriginalGun=c.originalGun;
        policy.s.knifeSelected=policy.s.trackedKnifeOwned=true;policy.s.emptyHands=false;
        // Receipt expiry is historical; each current source/claim remains fresh.
        if(policy.Step(80*ms)||policy.Step(80*ms)||policy.Step())return false;
        c.input=policy.s.source;c.controls.generation=c.input.sequence;c.controls.predictedNs=c.input.observedNs;
        c.controls.menu=policy.s.menu;c.hold=*policy.hands.Current(InteractionHand::Right);
        c.rendered.source.input=c.input;c.nativeSequence=c.input.sequence;c.nativeObservedNs=c.input.nowNs;
        c.phase=policy.knife.Phase();c.held=ChestKnifePoseCustody(policy.knife,policy.hands,c.input);
        c.stockTriggerReady=policy.s.stockTriggerReady;c.stockTriggerArmed=policy.knife.StockTriggerArmedFor(policy.s,policy.hands);
        c.nativeObservationComplete=false;c.firstObservationGap=7; // Historical enter_busy / unknown branch.
        c.nativeInspection=chestKnifeNative::InspectReason::Available;c.readinessKnife=true;
        private_knife::Item item;for(auto* state:{&item.client.branches[0],&item.client.branches[1],&item.server})
            state->currentState=state->nextState=2;
        c.readiness=chestKnifeNative::InspectReady(item);
        value.nativeWork={4,c.bodyOperation,draw->serial,true,true};
        value.drawOnlyIssued=true;value.needsNativeCleanup=policy.knife.NeedsNativeCleanup();
        value.leftEmpty=!policy.hands.Current(InteractionHand::Left);value.bodyOperationMatches=true;
        return c.held&&c.phase==KnifePhase::Held&&c.stockTriggerArmed&&receipt.deadlineNs<c.input.nowNs;
    }
};
int CurrentHoldCases(){
    CurrentHoldFixture fixture;CHECK(fixture.SetUp());const auto v=fixture.value;const auto now=v.current.input.nowNs;
    CHECK(!ChestKnifeCurrentHoldObservation::actionAuthority);
    CHECK(ChestKnifeCurrentHoldRejection(v,now).empty());
    CHECK(!ChestKnifeLiveRejection(v.current,now).empty());
    CHECK(ChestKnifePublicationBranch(v.current)=="native_observation_complete");
    ChestKnifeLivePublication strict;strict.Request(now);CHECK(!strict.Publish(v.current,now)&&!strict.Read(now));
    CHECK(strict.ReadDiagnostic()->latest.value.firstObservationGap==7);
    auto unarmed=v;unarmed.current.stockTriggerReady=unarmed.current.stockTriggerArmed=false;
    CHECK(ChestKnifeCurrentHoldRejection(unarmed,now).empty());CHECK(!ChestKnifeLiveRejection(unarmed.current,now).empty());
    auto complete=v;complete.current.nativeObservationComplete=true;complete.current.firstObservationGap=0;
    CHECK(ChestKnifeLiveRejection(complete.current,now).empty());CHECK(strict.Publish(complete.current,now));
    CHECK(ChestKnifeCurrentHoldRejection(complete,now).empty());
    CHECK(ChestKnifeCurrentHoldJoinRejection(v,v.current.owner,v.current.input,v.current.controls,now).empty());
    for(unsigned fault=0;fault<11;++fault){auto owner=v.current.owner;auto input=v.current.input;auto controls=v.current.controls;
        if(fault==0)++owner.equipGeneration;if(fault==1)++input.owner.space;if(fault==2)--input.deadlineNs;
        if(fault==3)++controls.menu.epoch;if(fault==4)++controls.trackingBasisGeneration;if(fault==5)controls.hands[1].trigger=1;
        if(fault==6){++input.sequence;++controls.generation;}if(fault==7){--input.sequence;--controls.generation;}
        if(fault==8)controls.hands[1].squeeze=0;if(fault==9)controls.hands[0].squeeze=.5f;if(fault==10)controls.worldUnitsPerMeter*=2;
        CHECK(!ChestKnifeCurrentHoldJoinRejection(v,owner,input,controls,now).empty());
    }

    // Every later join runs despite the earlier historical veto in strict Held.
    for(unsigned fault=0;fault<85;++fault){auto b=v;auto& c=b.current;
        switch(fault){
        case 0:++c.originalOwner.soldier;break;case 1:++c.originalOwner.weak;break;
        case 2:++c.originalOwner.actorGeneration;break;case 3:++c.originalOwner.space;break;
        case 4:c.originalOwner.equipGeneration=0;break;case 5:c.originalGun.generation=0;break;
        case 6:++c.knife.id;break;case 7:++c.input.owner.equipGeneration;break;
        case 8:c.continuity=0;break;case 9:c.bodyOperation=0;break;
        case 10:c.phase=KnifePhase::AttackStaged;break;case 11:c.phase=KnifePhase::Attacking;break;
        case 12:c.phase=KnifePhase::ReturnStaged;break;case 13:c.phase=KnifePhase::Returning;break;
        case 14:c.phase=KnifePhase::Unknown;break;case 15:c.nativeHealthy=false;break;
        case 16:c.nativeOwned=false;break;case 17:c.stockTriggerEnabled=false;break;
        case 18:c.motionEnabled=true;break;case 19:c.attackBridgeEnabled=true;break;
        case 20:c.held=false;break;case 21:c.fireSuppressed=false;break;case 22:c.mechanicsSettled=false;break;
        case 23:c.input.observedNs=now+1;break;case 24:c.rendered.source.input.deadlineNs=now;break;
        case 25:c.controls.hands[0].trigger=.2f;break;case 26:c.controls.hands[1].trigger=.2f;break;
        case 27:c.controls.hands[1].squeeze=0;break;case 28:c.controls.hands[0].squeeze=.5f;break;
        case 29:c.hold.token.id=0;break;case 30:c.hold.token.hand=InteractionHand::Left;break;
        case 31:++c.hold.token.owner.equipGeneration;break;case 32:++c.hold.inputSequence;break;
        case 33:++c.hold.deadlineNs;break;case 34:++c.rendered.rigFingerprint;break;
        case 35:++c.rendered.source.bodyOperation;break;case 36:++c.rendered.source.knife.generation;break;
        case 37:--c.rendered.source.input.deadlineNs;break;case 38:++c.rendered.source.owner.weapon;break;
        case 39:c.selectedSlots[0]=0;break;case 40:c.selectedSlots[1]=0;break;case 41:c.selectedSlots[2]=0;break;
        case 42:++c.nativeSequence;break;case 43:c.nativeObservedNs=now+1;break;
        case 44:c.nativeTick=0;break;case 45:c.suppressionRequest=0;break;
        case 46:c.controls.trackingBasisGeneration=0;break;case 47:c.input.focused=false;break;
        case 48:c.input.tracked[0]=false;break;case 49:c.input.tracked[1]=false;break;
        case 50:c.input.released[1]=true;break;case 51:c.controls.menu.owner=InputMenuOwner::Menu;break;
        case 52:++c.controls.generation;break;case 53:++c.controls.spaceGeneration;break;
        case 54:c.input.nowNs=c.input.observedNs-1;break;case 55:++c.input.deadlineNs;break;
        case 56:c.controls.menu.deadlineNs=now;break;case 57:c.controls.hands[1].held=Primary;break;
        case 58:c.controls.hands[0].stickX=.2f;break;case 59:c.nativeInspection=chestKnifeNative::InspectReason::Busy;break;
        case 60:c.readinessKnife=false;break;case 61:b.leftEmpty=false;break;case 62:b.needsNativeCleanup=true;break;
        case 63:b.bodyOperationMatches=false;break;case 64:b.drawOnlyIssued=false;break;
        case 65:b.nativeWork.known=false;break;case 66:b.nativeWork.drawOnly=false;break;
        case 67:b.nativeWork.outstanding=true;break;case 68:b.nativeWork.commandPending=true;break;
        case 69:b.nativeWork.attackPending=true;break;case 70:b.nativeWork.returnReady=true;break;
        case 71:b.nativeWork.closing=true;break;case 72:b.nativeWork.attackNative=true;break;
        case 73:b.nativeWork.bridgePending=true;break;case 74:b.nativeWork.lifetime=0;break;
        case 75:++b.nativeWork.bodyOperation;break;case 76:++b.nativeWork.drawSerial;break;
        case 77:b.drawCommand={};break;case 78:b.drawReceipt={};break;
        case 79:++b.drawReceipt.actor;break;case 80:++b.drawReceipt.actorGeneration;break;
        case 81:++b.drawReceipt.space;break;case 82:++b.drawReceipt.knife.generation;break;
        case 83:++b.drawOriginalGun.generation;break;case 84:c.draws=0;break;
        }
        CHECK(!ChestKnifeCurrentHoldRejection(b,now).empty());
        ChestKnifeCurrentHoldPublication p;p.Request(now);CHECK(!p.Publish(b,now)&&!p.Read(now));
    }
    for(unsigned field=0;field<11;++field){auto b=v;auto& r=b.drawReceipt;
        if(field==0)r.coherent=false;if(field==1)r.originalAmmoUnchanged=false;if(field==2)r.knifeSentinelUnchanged=false;
        if(field==3)r.selectedKnife=false;if(field==4)r.drawDidNotAttack=false;if(field==5)r.holdGatesOwned=false;
        if(field==6)r.attackObserved=true;if(field==7)r.attackSettled=true;if(field==8)r.attackQuiescentOnly=true;
        if(field==9)++r.serial;if(field==10)b.drawCommand.kind=KnifeCommandKind::Return;
        CHECK(!ChestKnifeCurrentHoldRejection(b,now).empty());
    }
    for(unsigned branch=0;branch<3;++branch)for(unsigned field=0;field<6;++field){
        private_knife::Item item;for(auto* s:{&item.client.branches[0],&item.client.branches[1],&item.server})s->currentState=s->nextState=2;
        auto* s=branch<2?&item.client.branches[branch]:&item.server;
        if(field==0)s->currentState=0;if(field==1)s->nextState=0;if(field==2)s->phaseTimer=std::numeric_limits<float>::quiet_NaN();
        if(field==3)s->phaseTimer=-1;if(field==4)s->phaseTimer=1000001;if(field==5)s->flagsA8=8;
        auto b=v;b.current.readiness=chestKnifeNative::InspectReady(item);
        CHECK(!ChestKnifeCurrentHoldRejection(b,now).empty());
    }
    // Native gates retain partial work past the original source deadline.
    private_knife::DrawReturnGate commands;CHECK(commands.Stage(v.drawCommand,v.drawCommand.source.nowNs));
    CHECK(commands.Choose(0,v.drawCommand.source.nowNs,true));
    CHECK(commands.ResolveBeforeSource(v.current.input.sequence,v.current.input.observedNs,now)==KnifeCommit::Accepted);
    auto pending=v;pending.nativeWork.outstanding=pending.nativeWork.commandPending=bool(commands.Pending());
    CHECK(!ChestKnifeCurrentHoldRejection(pending,now).empty());
    CHECK(commands.Choose(1,now,false)&&commands.Complete(v.drawCommand.serial));
    pending.nativeWork.outstanding=pending.nativeWork.commandPending=bool(commands.Pending());
    CHECK(ChestKnifeCurrentHoldRejection(pending,now).empty()); // Completed Draw alone is admissible.
    auto attackCommand=v.drawCommand;attackCommand.kind=KnifeCommandKind::Attack;++attackCommand.serial;
    attackCommand.source=v.current.input;attackCommand.stockTriggerHold=v.current.hold.token;
    private_knife::KnifeAttackGate attacks;CHECK(attacks.Stage(attackCommand,now));CHECK(attacks.Consume(1,now,true));
    CHECK(attacks.ResolveBeforeSource(v.current.input.sequence,v.current.input.observedNs,now)==KnifeCommit::Accepted);
    pending=v;pending.drawOnlyIssued=pending.nativeWork.drawOnly=false;
    pending.nativeWork.outstanding=pending.nativeWork.attackPending=bool(attacks.Pending());
    CHECK(!attacks.Complete(attackCommand.serial,true,true,true));
    CHECK(!ChestKnifeCurrentHoldRejection(pending,now).empty());
    CHECK(attacks.Consume(2,now,false)&&attacks.Complete(attackCommand.serial,true,true,true));
    pending.nativeWork.outstanding=pending.nativeWork.attackPending=bool(attacks.Pending());
    CHECK(!ChestKnifeCurrentHoldRejection(pending,now).empty()); // Quiescence cannot erase this Draw's attack attempt.
    // A real policy quiescent-only attack receipt returns to Held solely for cleanup.
    Fixture cleanup;CHECK(cleanup.Held());CHECK(cleanup.Rest());const auto attack=cleanup.Stab();CHECK(attack);
    cleanup.knife.Committed(*attack,KnifeCommit::Accepted);auto unknown=cleanup.Receipt(*attack);
    unknown.attackObserved=false;unknown.attackQuiescentOnly=true;CHECK(cleanup.knife.Observe(unknown));
    CHECK(cleanup.knife.Phase()==KnifePhase::Held&&cleanup.knife.NeedsNativeCleanup());
    auto b=v;b.current.phase=cleanup.knife.Phase();b.needsNativeCleanup=cleanup.knife.NeedsNativeCleanup();
    CHECK(!ChestKnifeCurrentHoldRejection(b,now).empty());

    ChestKnifeCurrentHoldPublication p;CHECK(!p.Publish(v,now));p.Request(now);CHECK(p.Publish(v,now));
    const auto first=p.Read(now);CHECK(first);p.Begin();CHECK(!p.Read(now));CHECK(p.Publish(v,now));
    CHECK(ChestKnifeCurrentHoldContinues(*first,*p.Read(now),now));
    auto lost=v;lost.current.rendered.source.input.deadlineNs=now;CHECK(!p.Publish(lost,now)&&!p.Read(now));
    CHECK(p.Publish(v,now));CHECK(!ChestKnifeCurrentHoldContinues(*first,*p.Read(now),now));
    auto laterJoin=v;++laterJoin.current.rendered.rigFingerprint;
    CHECK(!p.Publish(laterJoin,now));const auto failure=p.ReadDiagnostic();CHECK(failure);
    CHECK(failure->reason=="render_not_joined"&&failure->bodyOperation==v.current.bodyOperation&&
        failure->input.sequence==v.current.input.sequence&&failure->input.deadlineNs==v.current.input.deadlineNs);
    p.Begin();CHECK(p.ReadDiagnostic()->reason=="render_not_joined");CHECK(p.Publish(v,now));
    std::ostringstream diagnostic;WriteChestKnifeCurrentHoldDiagnostic(diagnostic,p.ReadDiagnostic());
    CHECK(diagnostic.str().find("\"historical\":true")!=std::string::npos&&
        diagnostic.str().find("\"actionAuthority\":false")!=std::string::npos&&diagnostic.str().find("render_not_joined")!=std::string::npos);
    const auto deadline=ChestKnifeLiveDeadline(v.current);p.Request(deadline);CHECK(!p.Read(deadline));
    CHECK(!p.Requested(deadline+1000000000)); // Request never renews proof deadlines.
    for(unsigned race=0;race<4;++race){ChestKnifeCurrentHoldPublication q;q.Request(now);CHECK(q.Publish(v,now));
        unsigned reads=0;auto clock=now;
        const auto json=ObserveChestKnifeCurrentHoldJson([&](std::int64_t sampled){
            if(++reads==2){if(race==0)q.Begin();if(race==1)q.Invalidate();if(race==2)clock=deadline;
                if(race==3){q.Invalidate();q.Publish(v,now);}}
            return q.Read((std::max)(sampled,clock));},[&](){return clock;});
        CHECK(reads==2&&json.find("\"available\":false")!=std::string::npos);
    }
    const auto held=fixture.policy.knife.Hold();const auto phase=fixture.policy.knife.Phase();
    std::ostringstream out;WriteChestKnifeCurrentHoldObservation(out,&v,now);const auto json=out.str();
    CHECK(json.size()<7000&&json.find("\"available\":true")!=std::string::npos);
    CHECK(json.find("\"actionAuthority\":false")!=std::string::npos&&json.find("\"nativeObservationComplete\":false")!=std::string::npos);
    CHECK(json.find("\"scope\":\"current_nonfiring_hold\"")!=std::string::npos&&json.find("\"reason\":1,\"branch\":3")!=std::string::npos);
    CHECK(json.find("\"deadlineNs\":"+std::to_string(v.drawReceipt.deadlineNs))!=std::string::npos);
    CHECK(held==fixture.policy.knife.Hold()&&phase==fixture.policy.knife.Phase()&&!fixture.policy.knife.NeedsNativeCleanup());
    CHECK(!ChestKnifeLiveRejection(v.current,now).empty()); // Observation did not repair history or strict Held.
    return 0;
}
