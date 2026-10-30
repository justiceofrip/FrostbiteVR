#include "Test.h"
#include "fvr/interaction/PistolAfterFeedPolicy.h"
#include <type_traits>
using namespace fvr::interaction;
namespace {
template<class T> concept HasChamber=requires(T t){t.chamber;};
static_assert(!HasChamber<PistolAfterFeedAdmission>&&!HasChamber<PistolAfterFeedNativeReady>);
static_assert(!std::is_convertible_v<PistolAfterFeedNativeReady,PistolSlideReady>);
static_assert(!std::is_convertible_v<PistolAfterFeedStroke,PistolSlideRequest>);
static_assert(!std::is_copy_constructible_v<PistolAfterFeedPolicy>);
fvr::math::Matrix4 Pose(){fvr::math::Matrix4 p{};for(unsigned i=0;i<4;++i)p.values[i][i]=1;return p;}
struct Fixture {
    PistolAfterFeedPolicy policy;AmmunitionLedger ledger;HandInteraction hands;
    AmmoResourceContext context{{1,2,10,3},4,5};
    AmmunitionSnapshot snapshot{};PistolSlideProfile profile{};
    PistolAfterFeedMotionSample sample{};PistolAfterFeedMotionResult result{};
    AmmunitionIntent intent{};AmmunitionReceipt receipt{};
    std::int64_t now=1000000000;std::uint64_t request=0,input=0,claim=0,nativeSequence=0;
    unsigned strokes=0;bool valid=false;
    explicit Fixture(int loaded=0,int reserve=10,bool initialReady=true){
        snapshot={context,1,now,now+100000000,{loaded,reserve,10},true};
        valid=ledger.Bind(snapshot,now)&&policy.Bind({snapshot,initialReady},now);
        profile.id=11;profile.revision=2;profile.closedContact=Pose();profile.stroke=.04f;
        profile.frontTolerance=.002f;profile.rearTolerance=.002f;profile.hysteresis=.002f;
        profile.contactRadius=.03f;profile.lateralTolerance=.015f;profile.rotationTolerance=.2f;
        profile.maxStepMeters=.015f;profile.endpointDwellNs=10000000;profile.maximumStrokeNs=2000000000;
        valid=valid&&policy.ConfigureMotion(profile,{20,2});
    }
    bool Fresh(){
        now+=1000000;++snapshot.sequence;snapshot.context=context;
        snapshot.observedNs=now;snapshot.deadlineNs=now+100000000;return ledger.Bind(snapshot,now);
    }
    bool Queue(AmmunitionOperation op){
        if(!Fresh())return false;
        const auto original=op==AmmunitionOperation::ReturnMagazine?ledger.Original():std::optional<RemovedMagazineResource>{};
        intent={context,++request,now,now+100000000,op,original};
        const auto command=ledger.Queue(op,snapshot,now,original);
        if(!command||!policy.BeginTransaction(intent,*command,now))return false;
        if(!ledger.Dispatch(snapshot,now))return false;
        receipt={*command,100+request,now+1,now+2,command->before,command->after,true,true};return true;
    }
    bool Complete(){
        now=std::max(now,receipt.completedNs);
        if(!ledger.Complete(receipt,now)||!policy.CompleteTransaction(intent,receipt,now))return false;
        snapshot.counts=receipt.after;return true;
    }
    bool Transfer(AmmunitionOperation op){return Queue(op)&&Complete();}
    bool Feed(){
        return Transfer(AmmunitionOperation::RemoveMagazine)&&ledger.Original()&&ledger.Discard(*ledger.Original())&&
            Transfer(AmmunitionOperation::RefillMagazine);
    }
    void Advance(float travel=0,bool grip=false,std::int64_t elapsed=10000000){
        now+=elapsed;sample.source={{(9ull<<32)|context.resource.actor,context.resource.actorGeneration,context.equipGeneration+40,context.space},
            ++input,now,now+100000000,now,true,{true,true},{false,false}};
        sample.binding={context,sample.source.owner,{context.resource.weapon,sample.source.owner.equipGeneration},input,now,now+100000000,true};
        sample.source.released[weapon_cycle_detail::HandIndex(profile.hands.mechanism)]=!grip;
        sample.context=context;sample.feedRequest=policy.Feed()?policy.Feed()->intent.id:0;
        sample.rawContact=profile.closedContact;for(unsigned k=0;k<3;++k)sample.rawContact.values[3][k]+=profile.axis[k]*travel;
        sample.grip=grip;sample.contact={{20,2},input,now+100000000,true};
        sample.gunContact={sample.gun.contact,input,now+100000000,true};sample.acquireIntent=++claim;
        sample.currentSafety=sample.source;sample.sourceObservation.reset();sample.currentObservation.reset();
    }
    bool AcquireGun(){
        hands.Update(sample.source);
        auto current=hands.Current(profile.hands.gun);
        if(current&&current->token.owner==sample.source.owner){
            sample.gun=current->token;hands.Renew(sample.source,sample.gun,{sample.gun.contact,input,now+100000000,true});
        }else {
            auto acquired=hands.Acquire(sample.source,{sample.source.owner,profile.hands.gun,HandClaimKind::GunHold,
                sample.binding.item,{{90,1},input,now+100000000,true},++claim,0});
            if(!acquired.claim)return false;sample.gun=acquired.claim->token;
        }
        sample.gunContact={sample.gun.contact,input,now+100000000,true};return true;
    }
    auto Run(){result=policy.UpdateMotion(sample,hands);if(result.stroke)++strokes;return result;}
    auto Step(float travel=0,bool grip=true){Advance(travel,grip);return Run();}
    bool Grip(){Advance();if(!AcquireGun())return false;Run();Step(0);return result.ownsMechanism;}
    void Back(){for(float t:{.01f,.02f,.03f,.04f,.04f})Step(t);}
    void Forward(){for(float t:{.03f,.02f,.01f,0.f,0.f})Step(t);}
    void Stroke(){Back();Forward();Step(0,false);}
    PistolAfterFeedNativeReady Ready(){
        PistolAfterFeedNativeReady r{*policy.Feed(),context,++nativeSequence,now+10000,now+100010000,{}};
        for(unsigned i=0;i<3;++i)r.updates[i]={1000+nativeSequence*3+i,now+1000+i*1000,now+1100+i*1000,true};
        return r;
    }
    bool Observe(PistolAfterFeedNativeReady r){now=r.observedNs;return policy.ObserveReady(r,now);}
};
// Gameplay consumes the current input first. The renderer then delivers the
// immutable packet from two inputs earlier, including its original clocks.
struct LaggedFixture:Fixture {
    std::array<PistolAfterFeedMotionSample,64> packets{};std::size_t count=0;
    bool Publish(float travel,bool grip,std::int64_t elapsed=10000000){
        Advance(travel,grip,elapsed);const auto before=hands.Current(profile.hands.mechanism);
        const auto lifecycle=hands.UpdateAndRenewGunHold(sample.source,sample.gun,sample.gunContact).lifecycle;
        if(!hands.Current(profile.hands.gun)&&!AcquireGun())return false;
        sample.gun=hands.Current(profile.hands.gun)->token;
        sample.gunContact={sample.gun.contact,input,sample.source.deadlineNs,true};
        sample.sourceObservation=PistolAfterFeedHandObservation{sample.source,before,lifecycle,hands.Current(profile.hands.gun)};
        sample.currentObservation=sample.sourceObservation;
        if(count==packets.size())return false;packets[count++]=sample;return lifecycle.inputValid;
    }
    PistolAfterFeedMotionResult Consume(std::size_t index){
        sample=packets[index];sample.source.nowNs=now;sample.currentSafety=packets[count-1].source;
        sample.currentSafety.nowNs=now;sample.currentObservation=packets[count-1].currentObservation;
        sample.gun=hands.Current(profile.hands.gun)?hands.Current(profile.hands.gun)->token:HandClaimToken{};
        sample.gunContact={sample.gun.contact,sample.currentSafety.sequence,sample.currentSafety.deadlineNs,true};
        sample.acquireIntent=++claim;return Run();
    }
    bool Tick(float travel,bool grip){if(!Publish(travel,grip))return false;if(count>=3)Consume(count-3);return true;}
    bool Start(){
        return Feed()&&Tick(0,false)&&Tick(0,false)&&Tick(0,false)&&
            Tick(0,true)&&Tick(0,true)&&Tick(0,true)&&result.ownsMechanism;
    }
    bool ReturnToFront(){
        for(float travel:{.01f,.02f,.03f,.04f,.04f,.03f,.02f,.01f,0.f,0.f})
            if(!Tick(travel,true))return false;
        return true;
    }
};
int EmptyFeedNeedsBothReceipts(){Fixture f;CHECK(f.valid&&f.policy.Owed()&&f.Feed());
    CHECK(f.policy.BlocksFire()&&f.policy.Owed());CHECK(f.Grip());f.Stroke();
    CHECK(f.strokes==1&&f.policy.Stroke()&&f.policy.BlocksFire());
    CHECK(!f.policy.Clearance(f.now));CHECK(f.Observe(f.Ready()));
    CHECK(!f.policy.BlocksFire()&&!f.policy.Owed());const auto clear=f.policy.Clearance(f.now);CHECK(clear&&clear->stroke&&!clear->retainedPolicyReady);
    CHECK(clear->stroke->feed==clear->feed&&clear->nativeReady.feed==clear->feed);
    CHECK(f.snapshot.counts==AmmunitionCounts(10,0,10)); // No fictional chamber round.
    for(unsigned i=0;i<3;++i)f.Step(0,false);CHECK(f.strokes==1);return 0;
}
int NativeReadyAloneAndExpiredWitnessCannotClear(){Fixture f;CHECK(f.Feed());CHECK(f.Observe(f.Ready()));
    CHECK(f.policy.BlocksFire()&&f.policy.Owed()&&!f.policy.Clearance(f.now));CHECK(f.Grip());f.Stroke();
    CHECK(f.policy.BlocksFire()&&f.policy.Stroke()); // The early witness expired during the measured stroke.
    CHECK(f.Observe(f.Ready()));CHECK(!f.policy.BlocksFire());return 0;
}
int TacticalReturnAndReplacementNeedNoStroke(){for(bool replacement:{false,true}){Fixture f(6,3);CHECK(f.valid&&!f.policy.Owed());
    CHECK(f.Transfer(AmmunitionOperation::RemoveMagazine));CHECK(f.policy.BlocksFire()&&!f.policy.Owed());
    if(replacement){CHECK(f.ledger.Discard(*f.ledger.Original()));CHECK(f.Transfer(AmmunitionOperation::RefillMagazine));}
    else CHECK(f.Transfer(AmmunitionOperation::ReturnMagazine));
    CHECK(!f.policy.Owed()&&f.policy.BlocksFire());CHECK(f.Observe(f.Ready()));CHECK(!f.policy.BlocksFire());
    const auto c=f.policy.Clearance(f.now);CHECK(c&&!c->stroke&&c->retainedPolicyReady);
    CHECK(f.snapshot.counts.loaded==(replacement?3:6));CHECK(f.strokes==0);
    }return 0;
}
int ActualEmptyRemovalOverridesInitiallyReadyPolicy(){Fixture f(1,10);CHECK(!f.policy.Owed());
    // The exact successful removal receipt supplies authoritative pre-removal
    // ammo after a real shot; no separate or invented chamber receipt is used.
    f.snapshot.counts.loaded=0;CHECK(f.Feed());CHECK(f.policy.Owed());CHECK(f.Observe(f.Ready()));
    CHECK(f.policy.BlocksFire());CHECK(f.Grip());f.Stroke();CHECK(f.policy.BlocksFire());
    CHECK(f.Observe(f.Ready()));CHECK(!f.policy.BlocksFire());return 0;
}
int ExistingDebtSurvivesPositiveRemoval(){Fixture f(4,10,false);CHECK(f.policy.Owed());
    CHECK(f.Transfer(AmmunitionOperation::RemoveMagazine));CHECK(f.Transfer(AmmunitionOperation::ReturnMagazine));
    CHECK(f.policy.Owed()&&f.policy.BlocksFire());CHECK(f.Observe(f.Ready()));CHECK(f.policy.BlocksFire());return 0;
}
int EmptyReturnNeverClearsAndLaterPartialFeedStillOwes(){Fixture f(0,3);CHECK(f.Transfer(AmmunitionOperation::RemoveMagazine));
    CHECK(f.Transfer(AmmunitionOperation::ReturnMagazine));CHECK(f.snapshot.counts.loaded==0&&f.policy.Owed());
    CHECK(!f.Observe(f.Ready()));CHECK(!f.Grip());CHECK(f.strokes==0);
    CHECK(f.Transfer(AmmunitionOperation::RemoveMagazine));CHECK(f.ledger.Discard(*f.ledger.Original()));
    CHECK(f.Transfer(AmmunitionOperation::RefillMagazine));CHECK(f.snapshot.counts==AmmunitionCounts(3,0,10));
    CHECK(f.Grip());f.Stroke();CHECK(f.Observe(f.Ready()));CHECK(!f.policy.BlocksFire());return 0;
}
int MissingRemovalAndUnadmittedCompletionFailClosed(){Fixture f;CHECK(f.Feed());const auto feed=*f.policy.Feed();
    Fixture missing;CHECK(missing.policy.BlocksFire());CHECK(!missing.policy.BeginTransaction(feed.intent,feed.receipt.command,feed.receipt.command.requestedNs));
    CHECK(!missing.policy.CompleteTransaction(feed.intent,feed.receipt,feed.receipt.completedNs));
    CHECK(!missing.policy.ObserveReady(f.Ready(),f.now));CHECK(missing.policy.BlocksFire());
    CHECK(!f.policy.CompleteTransaction(feed.intent,feed.receipt,f.now));return 0;
}
int ForgedCompletionCannotChangePending(){for(unsigned mutation=0;mutation<11;++mutation){Fixture f;CHECK(f.Queue(AmmunitionOperation::RemoveMagazine));
    const auto pending=f.policy.Pending();auto r=f.receipt;auto i=f.intent;
    switch(mutation){case 0:++i.id;break;case 1:++r.command.id;break;case 2:++r.command.sourceSequence;break;
    case 3:++r.command.context.resource.weaponGeneration;break;case 4:++r.command.context.space;break;
    case 5:r.authorityVerified=false;break;case 6:r.copiesVerified=false;break;case 7:r.authorityInvocation=0;break;
    case 8:r.beganNs=r.command.requestedNs-1;break;case 9:r.completedNs=r.command.deadlineNs;break;case 10:++r.after.loaded;break;}
    CHECK(!f.policy.CompleteTransaction(i,r,std::max(f.now,r.completedNs)));CHECK(f.policy.Pending()==pending&&f.policy.BlocksFire());
    CHECK(f.Complete());}return 0;
}
int ForgedMissingAndLateNativeWitnessFailClosed(){for(unsigned mutation=0;mutation<15;++mutation){Fixture f;CHECK(f.Feed()&&f.Grip());f.Stroke();auto r=f.Ready();
    switch(mutation){case 0:++r.feed.intent.id;break;case 1:++r.feed.receipt.command.id;break;
    case 2:++r.feed.receipt.authorityInvocation;break;case 3:++r.feed.receipt.completedNs;break;
    case 4:++r.context.resource.weaponGeneration;break;case 5:++r.context.equipGeneration;break;
    case 6:r.updates[2].invocation=0;break;case 7:r.updates[1].invocation=r.updates[0].invocation;break;
    case 8:r.updates[0].phaseInputReadyVerified=false;break;case 9:r.updates[1].beganNs=r.feed.receipt.completedNs-1;break;
    case 10:r.updates[2].completedNs=r.observedNs+1;break;case 11:r.updates[0].completedNs=r.updates[0].beganNs-1;break;
    case 12:r.sequence=0;break;case 13:r.deadlineNs=r.observedNs;break;
    case 14:r.observedNs+=200000000;r.deadlineNs+=200000000;break;}
    CHECK(!f.Observe(r));CHECK(f.policy.Owed()&&f.policy.BlocksFire());
    CHECK(f.Observe(f.Ready()));CHECK(!f.policy.BlocksFire());}return 0;
}
int PendingSurvivesStowTimeoutAndExactMigration(){Fixture f;CHECK(f.Queue(AmmunitionOperation::RemoveMagazine));const auto original=f.policy.Pending();
    f.Advance();CHECK(f.AcquireGun());f.policy.Suspend(f.sample.source,f.hands);
    const auto old=f.context;f.context.equipGeneration+=1;f.context.space+=1;f.now+=3000000000;
    PistolAfterFeedMigration move{old,f.context,1,f.now,f.now+100000000,true};
    CHECK(f.policy.Migrate(move,f.now));CHECK(f.policy.Pending()==original&&f.policy.BlocksFire());
    // Historical exact completed outcome can arrive late; deadline did not
    // reverse the successful transfer or mutate its original command context.
    CHECK(f.policy.CompleteTransaction(f.intent,f.receipt,f.now));CHECK(f.policy.Removal()->receipt==f.receipt);
    CHECK(f.policy.Context()==f.context&&f.policy.Owed());CHECK(!f.policy.Migrate(move,f.now));
    auto backwards=move;backwards.from=f.context;backwards.to=old;backwards.sequence=2;
    CHECK(!f.policy.Migrate(backwards,f.now));return 0;
}
int CompletedStrokeSurvivesTrackingStowAndMigration(){Fixture f;CHECK(f.Feed()&&f.Grip());f.Stroke();const auto stroke=f.policy.Stroke();
    f.Advance();f.sample.source.tracked[0]=false;f.Run();f.policy.Suspend(f.sample.source,f.hands);
    CHECK(f.policy.Stroke()==stroke&&f.policy.Owed());const auto old=f.context;
    ++f.context.equipGeneration;++f.context.space;
    CHECK(f.policy.Migrate({old,f.context,1,f.now,f.now+100000000,true},f.now));
    CHECK(f.policy.Stroke()==stroke&&f.policy.Feed()->receipt.command.context==old);
    auto stale=f.Ready();stale.context=old;CHECK(!f.Observe(stale));CHECK(f.Observe(f.Ready()));
    CHECK(!f.policy.BlocksFire()&&f.policy.Clearance(f.now)->stroke==stroke);return 0;
}
int PartialTrackingAndPreconsumedReleaseNeedNewFullStroke(){for(unsigned mode=0;mode<4;++mode){Fixture f;CHECK(f.Feed()&&f.Grip());
    if(mode==0){for(float t:{.01f,.02f,.01f,0.f,0.f})f.Step(t);f.Step(0,false);}
    if(mode==1){f.Step(.01f);f.Advance(.02f,true);f.sample.source.tracked[0]=false;f.Run();}
    if(mode==2){f.Back();f.Forward();f.Advance(0,false);f.hands.UpdateAndRenewGunHold(f.sample.source,f.sample.gun,f.sample.gunContact);f.Run();}
    if(mode==3){f.Back();f.policy.Suspend(f.sample.source,f.hands);}
    CHECK(f.strokes==0&&f.policy.Owed()&&!f.result.stroke);f.Advance();CHECK(f.AcquireGun());f.Run();f.Step(0);
    // mode 3 suspended while still held; the new fresh neutral packet rearms.
    CHECK(f.result.ownsMechanism);f.Stroke();CHECK(f.strokes==1&&f.policy.BlocksFire());CHECK(f.Observe(f.Ready()));
    CHECK(!f.policy.BlocksFire());}return 0;
}
int RawGeometryDwellAndDuplicatePacketsCannotInventStroke(){for(unsigned mode=0;mode<4;++mode){Fixture f;CHECK(f.Feed()&&f.Grip());
    f.Advance(.01f,true);if(mode==0)f.sample.rawContact.values[3][0]=.1f;
    if(mode==1)f.sample.rawContact.values[3][2]=-.04f;
    if(mode==2)f.sample.rawContact.values[0][0]=2;
    if(mode<3){f.Run();CHECK(f.result.failure==PistolSlideFailure::Geometry&&!f.result.ownsMechanism);}
    else {f.Run();for(float t:{.02f,.03f,.04f})f.Step(t);for(unsigned n=0;n<10;++n)f.Run();
        CHECK(f.policy.Phase()==PistolSlidePhase::Pull);f.Step(.03f);f.Step(.02f);f.Step(.01f);f.Step(0);f.Step(0,false);}
    CHECK(f.strokes==0&&f.policy.BlocksFire());}return 0;
}
int NonDispatchAndGenuineRetirementAreExact(){Fixture f;CHECK(f.Queue(AmmunitionOperation::RemoveMagazine));
    PistolAfterFeedNonDispatch no{*f.policy.Pending(),f.now,f.now+100000000,false};
    CHECK(!f.policy.RejectUndispatched(no,f.now));no.neverDispatched=true;++no.pending.intent.id;
    CHECK(!f.policy.RejectUndispatched(no,f.now));--no.pending.intent.id;
    CHECK(f.policy.RejectUndispatched(no,f.now));CHECK(!f.policy.CompleteTransaction(f.intent,f.receipt,f.receipt.completedNs));
    CHECK(!f.policy.BeginTransaction(f.intent,f.receipt.command,f.now));
    Fixture pending;CHECK(pending.Queue(AmmunitionOperation::RemoveMagazine));const auto original=pending.policy.Pending();
    PistolAfterFeedRetirement retirement{pending.context.resource,1,pending.now,pending.now+100000000,true,false};
    CHECK(!pending.policy.Retire(retirement,pending.now));retirement.callbacksQuiesced=true;++retirement.resource.actorGeneration;
    CHECK(!pending.policy.Retire(retirement,pending.now));--retirement.resource.actorGeneration;
    CHECK(pending.policy.Retire(retirement,pending.now));CHECK(pending.policy.Retired()&&pending.policy.Pending()==original);
    CHECK(!pending.policy.Bind({pending.snapshot,true},pending.now));CHECK(!pending.policy.Clearance(pending.now));return 0;
}
int ExactOriginalAndPartialReserveNoExtraAmmo(){Fixture f(7,2);CHECK(f.Transfer(AmmunitionOperation::RemoveMagazine));
    CHECK(f.Fresh());const auto original=*f.ledger.Original();auto wrong=original;++wrong.id;
    auto command=AmmunitionCommand{f.context,2,f.snapshot.sequence,f.now,f.now+100000000,f.now+2000000000,
        AmmunitionOperation::ReturnMagazine,f.snapshot.counts,{7,2,10},wrong,true};
    auto intent=AmmunitionIntent{f.context,2,f.now,f.now+100000000,AmmunitionOperation::ReturnMagazine,wrong};
    CHECK(!f.policy.BeginTransaction(intent,command,f.now));CHECK(f.policy.BlocksFire());
    CHECK(f.ledger.Discard(original));CHECK(f.Transfer(AmmunitionOperation::RefillMagazine));
    CHECK(f.snapshot.counts==AmmunitionCounts(2,0,10));CHECK(f.Observe(f.Ready()));CHECK(!f.policy.BlocksFire());return 0;
}
int HandMappingIsExplicitAndCannotChangeWithinContext(){for(unsigned mutation=0;mutation<7;++mutation){Fixture f;CHECK(f.Feed()&&f.Grip());
    CHECK(f.sample.gun.owner.actor!=f.context.resource.actor&&f.sample.gun.item.generation!=f.context.resource.weaponGeneration);
    CHECK(f.sample.gun.owner.equipGeneration!=f.context.equipGeneration);
    f.Advance(.01f,true);
    switch(mutation){case 0:f.sample.binding.exactResourceVerified=false;break;case 1:++f.sample.binding.context.resource.weapon;break;
    case 2:f.sample.binding.owner.actor=f.context.resource.actor;break;case 3:++f.sample.binding.item.generation;break;
    case 4:f.sample.binding.deadlineNs=f.now;break;case 5:++f.sample.binding.owner.equipGeneration;break;
    case 6:--f.sample.binding.sequence;break;}
    f.Run();CHECK(f.result.failure==PistolSlideFailure::Owner&&!f.result.ownsMechanism&&f.policy.Owed());
    CHECK(f.strokes==0);}return 0;
}
int NativeCohortReplayAndRetiredCompletionCannotRelease(){Fixture f;CHECK(f.Feed());auto ready=f.Ready();CHECK(f.Observe(ready));
    CHECK(!f.policy.ObserveReady(ready,f.now));++ready.sequence;
    CHECK(!f.policy.ObserveReady(ready,f.now));CHECK(f.policy.BlocksFire());
    Fixture ended;CHECK(ended.Queue(AmmunitionOperation::RemoveMagazine));
    CHECK(ended.policy.Retire({ended.context.resource,1,ended.now,ended.now+100000000,true,true},ended.now));
    CHECK(!ended.policy.CompleteTransaction(ended.intent,ended.receipt,ended.receipt.completedNs));
    ended.Advance();ended.policy.Suspend(ended.sample.source,ended.hands);CHECK(ended.policy.Phase()==PistolSlidePhase::Retired);return 0;
}
int GameplayAheadOfRawKeepsCurrentGunAndOriginalMechanismBounds(){LaggedFixture f;CHECK(f.Start());
    const auto gun=f.hands.Current(f.profile.hands.gun),mechanism=f.hands.Current(f.profile.hands.mechanism);
    CHECK(gun&&mechanism&&gun->inputSequence==f.input&&mechanism->inputSequence==f.input-2);
    CHECK(gun->deadlineNs==f.packets[f.count-1].source.deadlineNs);
    CHECK(mechanism->deadlineNs==f.packets[f.count-3].source.deadlineNs);
    const auto root=gun->token;CHECK(f.ReturnToFront());CHECK(f.hands.Current(f.profile.hands.gun)->token==root);
    CHECK(f.result.target&&f.result.target->inputSequence==f.input-2);
    CHECK(f.result.target->deadlineNs<=f.packets[f.count-3].source.deadlineNs);
    CHECK(f.strokes==0&&f.policy.Owed());return 0;
}
int RetainedActualReleaseCompletesOnlyAtOriginalRawPacket(){LaggedFixture f;CHECK(f.Start()&&f.ReturnToFront());
    CHECK(f.Tick(0,false));const auto release=f.packets[f.count-1];
    CHECK(release.currentObservation&&release.currentObservation->beforeMechanism);
    CHECK(!f.hands.Current(f.profile.hands.mechanism)&&!f.result.target&&!f.result.ownsMechanism&&f.strokes==0);
    CHECK(f.policy.Phase()==PistolSlidePhase::Return);
    // The next older held packet supplies front dwell after Gameplay consumed
    // the release. It observes motion only, without resurrecting hand custody.
    CHECK(f.Tick(0,false));CHECK(f.policy.Phase()==PistolSlidePhase::AwaitingRelease&&f.strokes==0&&!f.result.target);
    CHECK(f.Tick(0,false));CHECK(f.strokes==1&&f.policy.Stroke()&&f.policy.BlocksFire());
    CHECK(f.policy.Stroke()->inputSequence==release.source.sequence);
    CHECK(f.policy.Stroke()->observedNs==release.source.observedNs);
    CHECK(f.policy.Stroke()->deadlineNs<=release.currentObservation->beforeMechanism->deadlineNs);
    CHECK(f.policy.Stroke()->deadlineNs<=release.source.deadlineNs);
    CHECK(f.Observe(f.Ready())&&!f.policy.BlocksFire());return 0;
}
int ReleaseHistoryNeverSurvivesExpiryTrackingLossOrStolenCustody(){for(unsigned mode=0;mode<4;++mode){
    LaggedFixture f;CHECK(f.Start()&&f.ReturnToFront());
    if(mode==2){
        const auto old=f.hands.Current(f.profile.hands.mechanism);CHECK(old);
        const auto& current=f.packets[f.count-1].source;
        const auto stolen=f.hands.Transfer(current,old->token,{current.owner,f.profile.hands.mechanism,HandClaimKind::AmmoObject,
            {300,1},{{301,1},current.sequence,current.deadlineNs,true},++f.claim,0});
        CHECK(stolen.accepted&&stolen.claim);f.Consume(f.count-3);
        CHECK(f.hands.Current(f.profile.hands.mechanism)->token==stolen.claim->token);
    }else {
        CHECK(f.Tick(0,false));const auto releaseIndex=f.count-1;
        if(mode==0){
            // Root gun stays current while the original released mechanism
            // lease expires. The release packet itself is still fresh.
            for(unsigned n=0;n<7;++n)CHECK(f.Publish(0,false));
            CHECK(f.now<f.packets[releaseIndex].source.deadlineNs);f.Consume(releaseIndex);
            CHECK(f.result.failure==PistolSlideFailure::Expired);
        }else {
            f.Advance(0,false);
            if(mode==1)f.sample.source.tracked[0]=false;else ++f.sample.source.owner.actorGeneration;
            f.sample.currentSafety=f.sample.source;
            const auto before=f.hands.Current(f.profile.hands.mechanism);
            const auto lifecycle=f.hands.UpdateAndRenewGunHold(f.sample.source,f.sample.gun,f.sample.gunContact).lifecycle;
            f.sample.sourceObservation=PistolAfterFeedHandObservation{f.sample.source,before,lifecycle,f.hands.Current(f.profile.hands.gun)};
            f.sample.currentObservation=f.sample.sourceObservation;f.packets[f.count++]=f.sample;
            f.Consume(f.count-3);CHECK(f.result.failure==(mode==1?PistolSlideFailure::Tracking:PistolSlideFailure::Owner));
            if(mode==1){CHECK(f.Publish(0,false));f.Consume(releaseIndex);}
        }
    }
    CHECK(!f.policy.Stroke()&&f.strokes==0&&f.policy.Owed());CHECK(f.Observe(f.Ready())&&f.policy.BlocksFire());
    }return 0;
}
int UnprovenOrRestampedReleaseHistoryCannotComplete(){for(unsigned mode=0;mode<5;++mode){LaggedFixture f;
    CHECK(f.Start()&&f.ReturnToFront());CHECK(f.Publish(0,false));auto& observation=f.packets[f.count-1].currentObservation;
    CHECK(observation&&observation->beforeMechanism);
    if(mode==0)observation.reset();
    if(mode==1)observation->beforeMechanism->token.id+=100;
    if(mode==2)for(auto& event:observation->lifecycle.released)if(event)event->reason=HandInteractionReason::LeaseExpired;
    if(mode==3)++observation->input.observedNs;
    if(mode==4)++observation->beforeMechanism->deadlineNs;
    f.Consume(f.count-3);CHECK(!f.result.ownsMechanism&&!f.policy.Stroke());
    CHECK(f.Tick(0,false)&&f.Tick(0,false));CHECK(f.strokes==0&&f.policy.Owed());
    }return 0;
}
int HistoricalPacketsCannotAcquireOrRenewAcrossReleaseBarrier(){
    {LaggedFixture f;CHECK(f.Feed());CHECK(f.Tick(0,false)&&f.Tick(0,false)&&f.Tick(0,false));
        CHECK(f.Tick(0,true)&&f.Tick(0,true)&&f.Tick(0,false));
        CHECK(!f.hands.Current(f.profile.hands.mechanism)&&!f.result.ownsMechanism&&f.strokes==0);}
    {LaggedFixture f;CHECK(f.Start()&&f.ReturnToFront()&&f.Tick(0,false));
        const auto root=f.hands.Current(f.profile.hands.gun);CHECK(root);
        f.sample=f.packets[f.count-3];f.sample.source.nowNs=f.now;f.sample.currentSafety=f.packets[f.count-1].source;
        f.sample.currentObservation=f.packets[f.count-1].currentObservation;f.sample.gun=root->token;
        f.sample.gunContact={root->token.contact,f.input,f.packets[f.count-1].source.deadlineNs,true};
        // A historical observation is immutable: stamping its geometry with
        // current input cannot supply the missing original front/release path.
        f.sample.source.sequence=f.input;f.sample.binding.sequence=f.input;f.sample.contact.inputSequence=f.input;
        f.Run();CHECK(f.result.failure==PistolSlideFailure::Input&&!f.policy.Stroke());
        CHECK(f.hands.Current(f.profile.hands.gun)->token==root->token&&!f.hands.Current(f.profile.hands.mechanism));}
    return 0;
}
}
int main(){
    if(EmptyFeedNeedsBothReceipts()||NativeReadyAloneAndExpiredWitnessCannotClear()||TacticalReturnAndReplacementNeedNoStroke()||
       ActualEmptyRemovalOverridesInitiallyReadyPolicy()||ExistingDebtSurvivesPositiveRemoval()||EmptyReturnNeverClearsAndLaterPartialFeedStillOwes()||
       MissingRemovalAndUnadmittedCompletionFailClosed()||ForgedCompletionCannotChangePending()||ForgedMissingAndLateNativeWitnessFailClosed()||
       PendingSurvivesStowTimeoutAndExactMigration()||CompletedStrokeSurvivesTrackingStowAndMigration()||
       PartialTrackingAndPreconsumedReleaseNeedNewFullStroke()||RawGeometryDwellAndDuplicatePacketsCannotInventStroke()||
       NonDispatchAndGenuineRetirementAreExact()||ExactOriginalAndPartialReserveNoExtraAmmo()||
       HandMappingIsExplicitAndCannotChangeWithinContext()||NativeCohortReplayAndRetiredCompletionCannotRelease()||
       GameplayAheadOfRawKeepsCurrentGunAndOriginalMechanismBounds()||RetainedActualReleaseCompletesOnlyAtOriginalRawPacket()||
       ReleaseHistoryNeverSurvivesExpiryTrackingLossOrStolenCustody()||UnprovenOrRestampedReleaseHistoryCannotComplete()||
       HistoricalPacketsCannotAcquireOrRenewAcrossReleaseBarrier())return 1;
    std::puts("22 after-feed policy groups passed; deterministic mod policy/ledger/motion, no native chamber or headset claim.");
}
