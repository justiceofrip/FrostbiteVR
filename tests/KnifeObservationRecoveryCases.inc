namespace knife_observation_recovery_cases {
using namespace fvr::bc2::chestKnifeNative;
struct Held {
    Fixture f;ObservationRecovery recovery;ObservationRecoveryProof proof;View view;ChestKnifeMotionCalibration calibration;
    InputFrame input;fvr::math::Matrix4 measured;KnifeRecognitionCoordinator projection;
    private_knife::KnifeAttackGate attacks;private_knife::StockKnifeMotionSourceRevocation source;
    ChestKnifeRepeatedMotion samples;bool skipped=false;unsigned preparations=0;
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
    std::optional<KnifeCommand> Gather(bool advance=true,InspectReason inspection=InspectReason::Available,
        bool measuredCurrent=true,BeforePrepareReason preparation=BeforePrepareReason::Available,std::int64_t renderDeadline=0){
        auto& s=f.s;
        if(advance){++s.source.sequence;s.source.observedNs+=10*ms;s.source.nowNs=s.source.observedNs;
            s.source.deadlineNs=s.source.nowNs+100*ms;s.claimIntent=s.source.sequence;
            s.menu.deadlineNs=s.source.deadlineNs;++s.menu.sequence;}
        f.hands.Update(s.source);NativeView();skipped=false;projection={};
        const bool native=inspection==InspectReason::Available;
        s.onFoot=native;s.knifeSelected=native&&view.snapshot.selected.current==std::array<unsigned,3>{7,7,7};
        s.mechanicsSettled=native&&view.healthy&&InspectReady(view.snapshot.knife).reason==ReadyReason::Ready;
        s.trackedKnifeOwned=native&&measuredCurrent&&view.owned&&view.healthy&&s.knifeSelected;
        s.heldContact=s.trackedKnifeOwned?HandContactProof{{103,1},s.source.sequence,
            renderDeadline?(std::min)(renderDeadline,s.source.deadlineNs):s.source.deadlineNs,true}:HandContactProof{};
        if(native&&!measuredCurrent){
            const auto contact=ChestKnifeRetainedStockContact(f.knife,f.hands,s,true,view.owned&&s.knifeSelected);
            if(contact){s.heldContact=*contact;s.trackedKnifeOwned=true;}
        }
        const auto captured=view.observationRecovery.state;
        if(RecoveryHoldCurrent(view,s,f.knife,f.hands)&&recovery.Rejoin(captured,s.source.observedNs)){
            proof=RefreshObservationRecoveryProof({captured,1,7,s.source.sequence,s.source.observedNs,s.source.nowNs},recovery.Read(),1,7);
            view.observationRecovery=proof;
            view.currentObservationReady=view.observationRecovery.state.ready;
        }
        const bool ready=native&&ObservationSourceReady(view,f.knife.Phase());s.stockTriggerReady=ready;
        input.generation=s.source.sequence;input.spaceGeneration=s.source.owner.space;
        input.predictedNs=s.source.observedNs;input.menu=s.menu;
        std::optional<ChestKnifeMotionIdentity> identity;
        if(native&&view.owned&&s.knifeSelected&&s.trackedKnifeOwned&&s.mechanicsSettled&&
           ChestKnifeRenderPoseCustody(f.knife,f.hands,s.source)&&f.knife.Hold())
            identity=ChestKnifeMotionIdentity{view.snapshot.selectedOwner,*f.knife.Hold(),{42,1},7,view.currentHoldWork.lifetime,
                s.menu.epoch,input.trackingBasisGeneration,input.worldUnitsPerMeter,
                view.observationRecovery.state.epoch,view.observationRecovery.state.revision};
        const bool bounded=samples.AcrossSamples(view.snapshot.selectedOwner,{42,1},7,input,s,f.knife,f.hands);
        if(samples.HasSampleGap()&&!bounded){calibration.Reset();f.knife.InvalidateMotion();samples.Reset();}
        const bool unavailable=!native&&InspectionLockMiss(inspection)&&bounded;
        const bool measurementMissing=native&&bounded&&!measuredCurrent&&samples.SameNative(identity)&&
            view.currentObservationReady&&CurrentHoldWorkSettled(view.currentHoldWork);
        s.motionTrackingReady=s.nativeAttackReady=false;
        if(!unavailable&&!measurementMissing)PrepareChestKnifeMotionSource(calibration,f.knife,identity,
            measuredCurrent?std::optional(measured):std::nullopt,input,ready,s,&projection);
        if(native&&!ready){calibration.Reset();f.knife.InvalidateMotion();samples.Reset();s.nativeAttackReady=s.motionTrackingReady=false;}
        source.Advance(s,f.knife.NeedsNativeCleanup());
        if(preparation!=BeforePrepareReason::Available||unavailable||measurementMissing){
            const bool keep=bounded&&((measurementMissing&&(preparation==BeforePrepareReason::Available||preparation==BeforePrepareReason::Busy))||
                samples.TemporaryMiss(inspection,preparation,identity,projection.calibrationContinuous,
                    native&&view.currentObservationReady&&CurrentHoldWorkSettled(view.currentHoldWork)));
            if(keep){samples.SkipSample(input,s);skipped=true;}
            else {calibration.Reset();f.knife.InvalidateMotion();samples.Reset();}
            return {};
        }
        ++preparations;const auto command=f.knife.Prepare(s,f.hands);samples.Reset();
        const auto held=f.hands.Current(InteractionHand::Right);
        if(!command&&identity&&ready&&s.nativeAttackReady&&s.motionTrackingReady&&f.knife.Phase()==KnifePhase::Held&&held)
            samples.Remember(*identity,input,s,*held,view.currentHoldWork);
        return command;
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
int PendingContentionCases(){
    // Actual91/04: recognition 4.0663ms after the original input, followed by
    // CaptureBusy on that same packet 7.0815ms later. Stage's actual clock is
    // not recorded; this assembly places Stage before that observed duplicate.
    for(unsigned fault=0;fault<28;++fault){
        Held h;CHECK(h.Init()&&h.Rest());const auto jab=h.Jab();CHECK(jab&&jab->stroke);
        auto& s=h.f.s;s.source.nowNs+=4066300;
        h.f.hands.Update(s.source);CHECK(h.f.knife.CanCommit(*jab,s,h.f.hands));
        CHECK(!h.source.Advance(s)&&h.recovery.TakeReady(s.source.nowNs)&&h.attacks.Stage(*jab,s.source.nowNs));
        const auto held=h.f.hands.Current(InteractionHand::Right);CHECK(held);
        const auto identity=ChestKnifeMotionIdentity{h.view.snapshot.selectedOwner,held->token,{42,1},7,1,1,9,1.f};
        const PendingMotionAdmission admission{jab->serial,1,7,11,h.source.Stamp(),
            (std::min)({s.source.deadlineNs,s.heldContact.deadlineNs,held->deadlineNs})};
        ChestKnifePendingMotion pending;
        CHECK(pending.Remember(admission,identity,h.input,s,*held,h.view.currentHoldWork));
        const auto nativeSource=s;auto duplicate=s;duplicate.source.nowNs+=7081500;
        duplicate.onFoot=duplicate.mechanicsSettled=duplicate.knifeSelected=duplicate.trackedKnifeOwned=false;
        duplicate.nativeAttackReady=duplicate.motionTrackingReady=false;duplicate.heldContact={};duplicate.tip={};duplicate.direction={0,0,1};
        auto input=h.input;auto owner=identity.owner;auto inspection=fault==1?InspectReason::RejoinBusy:InspectReason::CaptureBusy;
        std::uint64_t operation=7,lifetime=1,epoch=11;bool nativeSelection=true;
        switch(fault){
        case 2:inspection=InspectReason::NativeRead;break;case 3:inspection=InspectReason::RejoinChanged;break;
        case 4:++owner.weapon;break;case 5:++operation;break;case 6:++lifetime;break;case 7:++epoch;break;
        case 8:++duplicate.source.sequence;break;case 9:++duplicate.source.observedNs;break;
        case 10:++duplicate.source.deadlineNs;break;case 11:input.hands[1].grip.position.x+=.001f;break;
        case 12:duplicate.gripHeld[1]=false;break;case 13:duplicate.source.released[1]=true;break;
        case 14:duplicate.source.focused=false;break;case 15:duplicate.source.tracked[1]=false;break;
        case 16:++duplicate.menu.epoch;break;case 17:duplicate.fireSuppressed=false;break;
        case 18:duplicate.holdInputCurrent=duplicate.controlsNeutral=false;break;
        case 19:h.f.hands.Reset();break;case 20:duplicate.source.nowNs=admission.deadlineNs;break;
        case 21:h.recovery.Unknown();break;
        case 22:{auto lost=s;lost.gripHeld[1]=false;h.source.Advance(lost);h.source.Advance(s);break;}
        case 23:pending.Reset();break;case 24:nativeSelection=false;break;
        case 26:duplicate.source.nowNs=s.source.nowNs-1;break;case 27:duplicate.stockTrigger=.5f;break;
        }
        h.f.hands.Update(duplicate.source);
        if(fault==0){
            // Before this correction a busy observation alone erased the
            // pending stamp despite unchanged packet, claim and native owner.
            private_knife::StockKnifeMotionSourceRevocation oldSource;
            CHECK(!oldSource.Advance(nativeSource));const auto oldStamp=oldSource.Stamp();
            private_knife::KnifeAttackGate oldGate;CHECK(oldGate.Stage(*jab,s.source.nowNs));
            CHECK(oldSource.Advance(duplicate)&&!oldGate.Consume(1,duplicate.source.nowNs,oldSource.Current(oldStamp)));
        }
        const auto proof=pending.Unavailable(inspection,owner,{42,1},operation,input,duplicate,h.f.knife,h.f.hands);
        const bool preserve=proof&&h.source.PreservePending(duplicate,proof->motionStamp)&&
            PendingMotionCurrent(*proof,*jab,lifetime,operation,epoch,admission.motionStamp,duplicate.source.nowNs);
        if(!preserve)h.source.Advance(duplicate,h.f.knife.NeedsNativeCleanup());
        const auto callbackNow=fault==25?admission.deadlineNs:duplicate.source.nowNs;
        const bool current=nativeSelection&&!h.recovery.Read().blocked&&h.source.Current(admission.motionStamp)&&
            PendingMotionCurrent(admission,*jab,lifetime,operation,epoch,admission.motionStamp,callbackNow)&&
            private_knife::DrawReturnGate::Fresh(*jab,callbackNow)&&KnifeCommandInputCurrent(*jab,nativeSource);
        if(fault<2){
            CHECK(preserve&&h.attacks.Consume(1,callbackNow,current));
            CHECK(!h.attacks.Consume(1,callbackNow,current));
            CHECK(h.attacks.ResolveBeforeSource(s.source.sequence,s.source.observedNs,callbackNow)==KnifeCommit::Accepted);
            h.f.knife.Committed(*jab,KnifeCommit::Accepted);
            CHECK(!pending.Unavailable(inspection,owner,{42,1},operation,input,duplicate,h.f.knife,h.f.hands));
            // Only the accepted sibling may drain past the original deadline.
            CHECK(h.attacks.Consume(2,admission.deadlineNs+1,false));
            for(unsigned branch=0;branch<3;++branch)h.attacks.Observe(branch,2,5,6);
            CHECK(h.attacks.Complete(jab->serial,true,true)&&!h.attacks.Stage(*jab,callbackNow));
            CHECK(h.f.hands.Current(InteractionHand::Right)->deadlineNs==held->deadlineNs);
        }else CHECK(!h.attacks.Consume(1,callbackNow,current));
    }
    return 0;
}
void GapSource(Held& h,std::uint64_t sequence,std::int64_t observed,std::int64_t now,float travel){
    auto& s=h.f.s;if(s.source.sequence!=sequence)++s.menu.sequence;
    s.source.sequence=sequence;s.source.observedNs=observed;s.source.nowNs=now;
    s.source.deadlineNs=observed+100*ms;s.menu.deadlineNs=s.source.deadlineNs;s.claimIntent=sequence;
    const auto axis=private_knife::KnifeMotionFromGrip({},h.measured)->direction;
    h.input.hands[1].grip.position={axis.x*travel,axis.y*travel,-axis.z*travel};
}
bool PrimeSampleGap(Held& h){
    h.f.s.source.sequence=11847;h.f.s.source.observedNs=h.f.s.source.nowNs=662412615658900;
    h.f.s.source.deadlineNs=h.f.s.source.nowNs+100*ms;
    if(!h.Init())return false;
    for(unsigned n=0;n<20;++n){const auto t=662412855658900ll-(20-n)*10*ms;
        GapSource(h,11851+n,t,t,0);if(h.Gather(false))return false;}
    // Actual92/01 original source clocks and retained fixed-space travel. The
    // axis is the fixture's measured blade axis; angles/thresholds are unchanged.
    struct Point {std::uint64_t input;std::int64_t observed,now,renderDeadline;float travel;};
    constexpr Point points[]{
        {11871,662412855658900,662412856847300,662412940859100,0},
        {11871,662412855658900,662412863191600,662412955658900,0},
        {11872,662412871185500,662412877333600,662412955658900,.005160125f},
        {11872,662412871185500,662412882330100,662412971185500,.005160125f},
        {11873,662412886368400,662412888012400,662412971185500,.019887443f},
        {11873,662412886368400,662412893849400,662412986368400,.019887443f}};
    for(const auto& p:points){GapSource(h,p.input,p.observed,p.now,p.travel);
        if(h.Gather(false,InspectReason::Available,true,BeforePrepareReason::Available,p.renderDeadline))return false;}
    return h.f.knife.Phase()==KnifePhase::Held;
}
int SampleGapCases(){
    Held h;CHECK(PrimeSampleGap(h));const auto held=*h.f.hands.Current(InteractionHand::Right);
    CHECK(!h.samples.AcrossSamples(h.view.snapshot.selectedOwner,{42,1},8,h.input,h.f.s,h.f.knife,h.f.hands));
    CHECK(!h.samples.AcrossSamples(h.view.snapshot.selectedOwner,{42,2},7,h.input,h.f.s,h.f.knife,h.f.hands));
    const auto count=h.preparations;const auto stamp=h.source.Stamp();
    // Missing11874 and its duplicate never reach policy or renew contact. The
    // production native source revoker still receives their unavailable source.
    GapSource(h,11874,662412902380400,662412907902900,.04f);
    CHECK(!h.Gather(false,InspectReason::CaptureBusy,false,BeforePrepareReason::Busy)&&h.skipped);
    CHECK(!h.source.Current(stamp)&&h.preparations==count);
    GapSource(h,11874,662412902380400,662412913918600,.04f);
    CHECK(!h.Gather(false,InspectReason::CaptureBusy,false)&&h.skipped);
    GapSource(h,11875,662412917129600,662412920487100,.063005759f);
    CHECK(!h.Gather(false,InspectReason::Available,false)&&h.skipped&&h.preparations==count);
    CHECK(h.f.hands.Current(InteractionHand::Right)->deadlineNs==held.deadlineNs);
    CHECK(h.f.hands.Current(InteractionHand::Right)->inputSequence==11873);
    CHECK(!h.f.s.nativeAttackReady&&!h.f.s.motionTrackingReady&&ChestKnifeRenderPoseCustody(h.f.knife,h.f.hands,h.f.s.source));
    // A current measured11875 has .0578456m from the original11872 start,
    // below the unchanged .06m threshold. It is not a replay of skipped11874.
    GapSource(h,11875,662412917129600,662412926758400,.063005759f);
    CHECK(!h.Gather(false)&&!h.skipped&&h.projection.calibrationContinuous);
    const auto resumed=*h.f.hands.Current(InteractionHand::Right);CHECK(resumed.token==held.token&&resumed.inputSequence==11875);
    GapSource(h,11875,662412917129600,662412933227500,.063005759f);
    CHECK(!h.Gather(false,InspectReason::CaptureBusy,false,BeforePrepareReason::Busy)&&h.skipped);
    GapSource(h,11876,662412932722500,662412939561800,.086193388f);
    CHECK(!h.Gather(false,InspectReason::Available,false)&&h.skipped);
    CHECK(h.f.hands.Current(InteractionHand::Right)->deadlineNs==resumed.deadlineNs);
    GapSource(h,11876,662412932722500,662412945560600,.086193388f);
    const auto jab=h.Gather(false);CHECK(jab&&jab->stroke&&!jab->stockTriggerHold&&!h.skipped);
    CHECK(jab->source.sequence==11876&&jab->source.observedNs==662412932722500&&jab->source.deadlineNs==662413032722500);
    CHECK(jab->stroke->sourceSequence==11876&&h.f.knife.CanCommit(*jab,h.f.s,h.f.hands));
    CHECK(h.Finish(*jab)); // Shared native Stage/consumer path, including1088 duplicate preservation.
    for(auto inspection:{InspectReason::RejoinBusy,InspectReason::Available}){Held x;CHECK(PrimeSampleGap(x));
        GapSource(x,11874,662412902380400,662412907902900,.04f);
        CHECK(!x.Gather(false,inspection,inspection==InspectReason::Available,BeforePrepareReason::Busy)&&x.skipped);
        GapSource(x,11875,662412917129600,662412926758400,.063005759f);CHECK(!x.Gather(false));
        GapSource(x,11876,662412932722500,662412945560600,.086193388f);CHECK(x.Gather(false));
    }
    for(unsigned fault=0;fault<2;++fault){Held x;CHECK(PrimeSampleGap(x));
        GapSource(x,11874,662412902380400,662412907902900,.04f);
        CHECK(!x.Gather(false,InspectReason::CaptureBusy,false,BeforePrepareReason::Busy)&&x.skipped);
        x.f.s.source.nowNs=662412913918600;
        if(fault==0)x.input.hands[1].grip.position.x+=.001f;else ++x.f.s.source.deadlineNs;
        CHECK(!x.Gather(false,InspectReason::CaptureBusy,false)&&!x.skipped&&!x.samples.HasSampleGap());
    }

    // Local contradictions during an unavailable observation reset the same
    // stroke, including clock/lease expiry and both tracking generations.
    for(unsigned fault=0;fault<23;++fault){Held x;CHECK(PrimeSampleGap(x));
        GapSource(x,11874,662412902380400,662412907902900,.04f);
        auto inspection=InspectReason::CaptureBusy;auto preparation=BeforePrepareReason::Busy;auto& s=x.f.s;
        switch(fault){
        case 0:inspection=InspectReason::NativeRead;break;case 1:inspection=InspectReason::RejoinChanged;break;
        case 2:preparation=BeforePrepareReason::Closing;break;case 3:preparation=BeforePrepareReason::Unsupported;break;
        case 4:s.gripHeld[1]=false;s.source.released[1]=true;break;case 5:s.source.focused=false;break;
        case 6:s.source.tracked[0]=false;break;case 7:s.source.tracked[1]=false;break;
        case 8:++s.source.owner.actorGeneration;break;case 9:++s.source.owner.equipGeneration;break;
        case 10:++s.source.owner.space;break;case 11:++x.input.trackingBasisGeneration;break;
        case 12:x.input.worldUnitsPerMeter=2;break;case 13:s.holdInputCurrent=s.controlsNeutral=false;break;
        case 14:s.actionInputCurrent=s.controlsNeutral=false;break;case 15:s.fireSuppressed=false;break;
        case 16:++s.menu.epoch;break;case 17:x.f.hands.Reset();break;
        case 18:++x.view.snapshot.selectedOwner.weapon;break;case 19:x.input.hands[1].gripTracked=false;break;
        case 20:s.source.nowNs=662412893849399;break;
        case 21:s.source.observedNs=s.source.nowNs=662412961368401;s.source.deadlineNs=s.source.nowNs+100*ms;break;
        case 22:s.source.nowNs=662412971185500;break;
        }
        CHECK(!x.Gather(false,inspection,false,preparation)&&!x.skipped&&!x.samples.HasSampleGap());
        GapSource(x,11875,662412917129600,662412926758400,.063005759f);CHECK(!x.Gather(false));
        GapSource(x,11876,662412932722500,662412945560600,.086193388f);CHECK(!x.Gather(false));
    }
    // A later native/measurement contradiction cannot turn an earlier valid
    // skip into authority. Lifetime/recovery changes reset calibration normally.
    for(unsigned fault=0;fault<8;++fault){Held x;CHECK(PrimeSampleGap(x));
        const auto axis=private_knife::KnifeMotionFromGrip({},x.measured)->direction;
        GapSource(x,11874,662412902380400,662412907902900,.04f);
        CHECK(!x.Gather(false,InspectReason::CaptureBusy,false,BeforePrepareReason::Busy)&&x.skipped);
        switch(fault){
        case 0:++x.view.currentHoldWork.lifetime;break;case 1:x.view.owned=false;break;
        case 2:x.view.healthy=false;break;case 3:x.view.snapshot.selected.current[1]=0;break;
        case 4:x.recovery.Unknown();break;case 5:x.view.snapshot.knife.server.currentState=5;break;
        case 6:x.measured.values[0][0]=2;break;case 7:x.measured.values[0][0]=x.measured.values[1][1]=-1;break;
        }
        // Keep the original physical axis even for changed/invalid calibration;
        // these packets never borrow a past native view.
        x.f.s.source.sequence=11875;x.f.s.source.observedNs=662412917129600;x.f.s.source.nowNs=662412926758400;
        x.f.s.source.deadlineNs=662413017129600;x.f.s.menu.deadlineNs=x.f.s.source.deadlineNs;++x.f.s.menu.sequence;
        x.input.hands[1].grip.position={axis.x*.063005759f,axis.y*.063005759f,-axis.z*.063005759f};
        CHECK(!x.Gather(false)&&!x.skipped);
        x.f.s.source.sequence=11876;x.f.s.source.observedNs=662412932722500;x.f.s.source.nowNs=662412945560600;
        x.f.s.source.deadlineNs=662413032722500;x.f.s.menu.deadlineNs=x.f.s.source.deadlineNs;++x.f.s.menu.sequence;
        x.input.hands[1].grip.position={axis.x*.086193388f,axis.y*.086193388f,-axis.z*.086193388f};
        CHECK(!x.Gather(false));
    }
    return 0;
}
int Cases(){
    CHECK(!RetiredCompletionCases());
    CHECK(!PendingContentionCases());
    CHECK(!SampleGapCases());
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
    std::puts("Knife observation recovery: Held/jab, bounded native92 sample gaps, pending-source contention/expiry, retired completion and ownership negatives PASS.");
    return 0;
}
}
