#include "Test.h"
#include "Bc2KnifeDrawReturnGate.h"
#include "Bc2KnifeAttackGate.h"
#include "Bc2ChestKnifeRendering.h"
#include "Bc2ChestKnifeLiveObservation.h"
#include "Bc2ChestKnifeCurrentHoldObservation.h"
#include <cstdio>
#include <sstream>
#include <limits>
using namespace fvr;using namespace fvr::bc2;using namespace fvr::interaction;
#include "KnifeCustodyFixture.inc"
namespace {
int ReturnObservationCases(){
    Fixture f;f.s.source.owner.actor=(std::uint64_t(0x30000)<<32)|0x20000;
    const auto draw=f.Draw();CHECK(draw);f.knife.Committed(*draw,KnifeCommit::Accepted);
    const auto drawn=f.Receipt(*draw);CHECK(f.knife.Observe(drawn));
    f.s.knifeSelected=f.s.trackedKnifeOwned=true;f.s.emptyHands=false;CHECK(!f.Step());
    f.Grip(false);const auto returned=f.Step();CHECK(returned&&returned->kind==KnifeCommandKind::Return);
    f.knife.Committed(*returned,KnifeCommit::Accepted);CHECK(f.knife.Phase()==KnifePhase::Returning);
    ChestKnifeReturnPublication history;
    // Actual544's boundary: draw1/return0 and an outstanding Return. Missing
    // Held publication, expiry or independent Empty cannot create completion.
    chestKnifeNative::ReturnPendingObservation pending;pending.command=*returned;pending.bodyOperation=17;pending.lifetime=4;
    // Synthetic rejection detail: actual544 did not expose its first blocker.
    pending.chooserMask=3;pending.returnReady=true;pending.lastObserveReason=chestKnifeNative::ReturnPendingReason::BodyRehide;
    pending.lastObservedNs=f.s.source.nowNs;
    ChestKnifeLivePublication held;held.Begin();held.Invalidate();CHECK(!held.Read(f.s.source.deadlineNs));
    std::ostringstream waiting;WriteChestKnifeReturnObservation(waiting,{history.Read(),pending});
    CHECK(waiting.str().find("\"completed\":null")!=std::string::npos);
    CHECK(waiting.str().find("\"lastObserveReason\":6")!=std::string::npos);
    CHECK(!history.Read());

    auto receipt=f.Receipt(*returned);receipt.selectedKnife=receipt.drawDidNotAttack=receipt.holdGatesOwned=false;
    private_knife::Anchor anchor;anchor.original.client.owner={0x10000,0x20000,0x30000,0x51000,2,5,4};
    anchor.original.client.sequence=3;anchor.original.client.observedNs=1000000000;anchor.original.client.selectedSlot=2;
    anchor.knifeWeapon=unsigned(returned->knife.id);anchor.knifeSlot=7;
    private_knife::Snapshot snapshot;snapshot.selectedOwner=anchor.original.client.owner;
    snapshot.selectedOwner.equipGeneration=8;snapshot.sequence=41;snapshot.observedNs=receipt.nowNs;snapshot.selected.current={2,2,2};
    for(auto* state:{&snapshot.original.client.branches[0],&snapshot.original.client.branches[1],&snapshot.original.server})
        {state->currentState=state->nextState=2;state->loaded=30;state->reserve=210;}
    BodyFreeRightEvidence empty;empty.visibility.enabled=empty.visibility.hide=true;empty.visibility.request=19;
    empty.visibility.nativeOwner=snapshot.selectedOwner;empty.visibility.input=empty.input=f.s.source;
    empty.receipt.nativeOwner=snapshot.selectedOwner;empty.receipt.request=19;empty.receipt.inputSequence=f.s.source.sequence;
    empty.receipt.physicalEquipGeneration=6;empty.receipt.drawSerial=23;empty.receipt.verifiedCopyMask=3;empty.receipt.hidden=true;
    empty.receipt.observedNs=receipt.nowNs;empty.receipt.deadlineNs=f.s.source.deadlineNs;
    auto plan=std::make_shared<WeaponVisibilityPlan>();plan->nativeOwner=snapshot.selectedOwner;plan->request=19;
    plan->inputSequence=f.s.source.sequence;plan->physicalEquipGeneration=6;plan->meshSequence=29;plan->hidden=true;
    plan->observedNs=receipt.nowNs;plan->deadlineNs=plan->inputDeadlineNs=f.s.source.deadlineNs;
    empty.receipt.evidence=plan;
    empty.suppression.owner=snapshot.selectedOwner;empty.suppression.input=f.s.source;empty.suppression.request=19;empty.suppression.nativeTick=43;
    const auto completion=chestKnifeNative::CaptureReturnCompletion(*returned,receipt,anchor,snapshot,empty,4,17,3,returned->source.nowNs);
    // Expired release source is still copied verbatim; only the real receipt
    // can pass policy.Observe. A rejected or wrong serial remains nonmatching.
    auto wrong=receipt;++wrong.serial;CHECK(!f.knife.Observe(wrong));
    const BodyItemKey original{anchor.original.client.owner.weapon,6};
    history.Publish(completion,wrong,false,original,*draw,drawn,17,1,0,receipt.nowNs);
    CHECK(history.Read()&&!history.Read()->policyMatched&&!history.Read()->policyAccepted);
    CHECK(f.knife.Observe(receipt));CHECK(f.knife.Phase()==KnifePhase::NeedNeutral);
    history.Publish(completion,receipt,true,original,*draw,drawn,17,1,1,receipt.nowNs);
    const auto first=history.Read();CHECK(first&&first->policyMatched);
    CHECK(first->native.command.source.deadlineNs==returned->source.deadlineNs);
    CHECK(first->native.freeRight.drawSerial==23&&first->native.freeRight.verifiedCopyMask==3);
    // Destruction/reset of the original successful values cannot erase or
    // restamp the copied historical proof. No current Held/armed frame exists.
    anchor={};snapshot={};empty={};plan->request=99;held.Begin();held.Invalidate();
    CHECK(first->native.freeRight.planRequest==19&&first->native.freeRight.planMeshSequence==29);
    std::ostringstream complete;WriteChestKnifeReturnObservation(complete,{history.Read(),{}});
    const auto section=complete.str();CHECK(section.size()<14000);
    CHECK(section.find("\"historical\":true,\"nativeCompleted\":true")!=std::string::npos);
    CHECK(section.find("\"policyMatched\":true")!=std::string::npos);
    CHECK(section.find("\"actionAuthority\":false,\"freshEmptyRequired\":true")!=std::string::npos);
    CHECK(section.find("\"validUntilNs\"")==std::string::npos&&section.find("\"stockTriggerArmed\"")==std::string::npos);
    CHECK(section.find("\"selected\":[2,2,2]")!=std::string::npos);
    const auto invalid=std::string("{\"valid\":false,\"invalid_reason\":\"shot_expired\",\"chestKnifeReturn\":")+section+"}";
    CHECK(invalid.starts_with("{\"valid\":false,"));
    CHECK(first->native.receipt.deadlineNs==receipt.deadlineNs&&history.Read()==first);
    for(unsigned fault=0;fault<10;++fault){auto n=completion;auto r=receipt;auto g=original;auto d=*draw;auto dr=drawn;std::uint64_t operation=17;
        if(fault==0)++r.serial;if(fault==1)++n.bodyOperation;if(fault==2)++g.id;if(fault==3)g.generation=0;
        if(fault==4)++d.knife.generation;if(fault==5)++dr.serial;if(fault==6)++n.receipt.deadlineNs;
        if(fault==7)++operation;if(fault==8)n.chooserMask=1;if(fault==9)n.lifetime=0;
        history.Publish(n,r,true,g,d,dr,operation,1,1,receipt.nowNs);CHECK(history.Read()&&!history.Read()->policyMatched);
    }
    // Capacity one; a second event replaces the slot without altering a reader's
    // retained immutable first record. Neither a count nor Empty is an input.
    auto later=completion;later.command.serial+=2;later.receipt.serial+=2;++later.bodyOperation;
    history.Publish(later,later.receipt,true,original,*draw,drawn,18,2,2,receipt.nowNs);
    CHECK(history.Read()!=first&&first->native.bodyOperation==17&&history.Read()->native.bodyOperation==18);
    std::puts("Historical knife Return: pending failure boundary, policy join, immutable original clocks, independent publication and one-slot retention.");
    return 0;
}

ChestKnifeLiveObservation LiveFixture(){
    ChestKnifeLiveObservation v;v.owner={0x10000,0x20000,0x30000,0x52000,4,7,9};
    v.originalOwner=v.owner;v.originalOwner.weapon=0x51000;v.originalOwner.equipGeneration=6;
    v.knife={v.owner.weapon,3};v.originalGun={v.originalOwner.weapon,2};
    v.input={{(std::uint64_t(v.owner.weak)<<32)|v.owner.soldier,4,99,9},20,1000000000,1100000000,1010000000,true,{true,true},{true,false}};
    auto& c=v.controls;c.generation=v.input.sequence;c.spaceGeneration=v.input.owner.space;c.predictedNs=v.input.observedNs;
    c.focused=c.headValid=true;c.trackingBasisGeneration=1;c.menu={InputMenuOwner::Gameplay,5,8,v.input.deadlineNs};
    for(auto& hand:c.hands){hand.active=Stick|Trigger|Squeeze|Primary|Secondary|StickClick;hand.gripTracked=hand.aimTracked=true;}
    c.hands[1].squeeze=1;
    v.hold={{12,v.input.owner,InteractionHand::Right,HandClaimKind::GunHold,{v.knife.id,v.knife.generation},{13,3},0},v.input.deadlineNs,v.input.sequence};
    v.bodyOperation=17;v.rendered={{v.owner,v.input,v.knife,v.bodyOperation},ChestKnifeGripProfiles()[0].rigFingerprint,{}};
    v.continuity=5;v.draws=1;v.nativeSequence=v.input.sequence;v.nativeObservedNs=v.input.nowNs;v.nativeTick=41;v.suppressionRequest=17;
    v.knifeSlot=7;v.selectedSlots={7,7,7};v.phase=KnifePhase::Held;
    v.nativeOwned=v.nativeHealthy=v.nativeObservationComplete=v.stockTriggerEnabled=true;
    v.held=v.stockTriggerReady=v.stockTriggerArmed=v.fireSuppressed=v.mechanicsSettled=true;return v;
}
#include "KnifeCurrentHoldCases.inc"
int LiveObservationCases(){
    const auto v=LiveFixture();const auto now=v.input.nowNs;
    CHECK(ChestKnifeLiveRejection(v,now).empty());
    auto jab=v;jab.motionEnabled=true;CHECK(ChestKnifeLiveRejection(jab,now).empty());
    std::ostringstream jabJson;WriteChestKnifeLiveObservation(jabJson,&jab,jab.owner,jab.input,jab.controls,now);
    CHECK(jabJson.str().find("\"motionEnabled\":true")!=std::string::npos);
    CHECK(ChestKnifeLiveJoinRejection(v,v.owner,v.input,v.controls,now).empty());
    CHECK(ChestKnifeLiveDeadline(v)==v.input.deadlineNs);
    for(unsigned fault=0;fault<49;++fault){auto bad=v;
        switch(fault){
        case 0:++bad.originalOwner.soldier;break;case 1:bad.originalOwner.equipGeneration=0;break;
        case 2:bad.originalGun.generation=0;break;case 3:++bad.knife.id;break;
        case 4:++bad.input.owner.equipGeneration;break;case 5:bad.continuity=0;break;
        case 6:bad.bodyOperation=0;break;case 7:bad.phase=KnifePhase::Attacking;break;
        case 8:bad.nativeHealthy=false;break;case 9:bad.nativeOwned=false;break;
        case 10:bad.nativeObservationComplete=false;break;case 11:bad.stockTriggerEnabled=false;break;
        case 12:bad.motionEnabled=true;bad.attackBridgeEnabled=true;break;case 13:bad.attackBridgeEnabled=true;break;
        case 14:bad.held=false;break;case 15:bad.stockTriggerReady=false;break;
        case 16:bad.stockTriggerArmed=false;break;case 17:bad.fireSuppressed=false;break;
        case 18:bad.mechanicsSettled=false;break;case 19:bad.input.observedNs=now+1;break;
        case 20:bad.rendered.source.input.deadlineNs=now;break;case 21:bad.controls.hands[1].trigger=1;break;
        case 22:bad.controls.hands[1].squeeze=0;break;case 23:bad.controls.hands[0].squeeze=.5f;break;
        case 24:bad.hold.token.id=0;break;case 25:bad.hold.token.hand=InteractionHand::Left;break;
        case 26:++bad.hold.token.owner.equipGeneration;break;case 27:++bad.hold.inputSequence;break;
        case 28:++bad.hold.deadlineNs;break;case 29:++bad.rendered.rigFingerprint;break;
        case 30:++bad.rendered.source.bodyOperation;break;case 31:++bad.rendered.source.knife.generation;break;
        case 32:--bad.rendered.source.input.deadlineNs;break;case 33:++bad.rendered.source.owner.weapon;break;
        case 34:bad.selectedSlots[2]=0;break;case 35:++bad.nativeSequence;break;
        case 36:bad.nativeObservedNs=now+1;break;case 37:bad.nativeTick=0;break;
        case 38:bad.suppressionRequest=0;break;case 39:bad.controls.trackingBasisGeneration=0;break;
        case 40:bad.input.focused=false;break;case 41:bad.input.tracked[0]=false;break;
        case 42:bad.input.released[1]=true;break;case 43:bad.controls.menu.owner=InputMenuOwner::Menu;break;
        case 44:++bad.controls.generation;break;case 45:++bad.controls.spaceGeneration;break;
        case 46:bad.input.nowNs=bad.input.observedNs-1;break;case 47:++bad.input.deadlineNs;break;
        case 48:bad.controls.menu.deadlineNs=now;break;
        }
        CHECK(!ChestKnifeLiveRejection(bad,now).empty());
        std::ostringstream denied;WriteChestKnifeLiveObservation(denied,&bad,v.owner,v.input,v.controls,now);
        CHECK(denied.str().find("\"available\":false")!=std::string::npos);
        CHECK(denied.str().find("\"stockTriggerArmed\":true")==std::string::npos);
    }
    for(unsigned fault=0;fault<11;++fault){auto owner=v.owner;auto input=v.input;auto controls=v.controls;
        if(fault==0)++owner.equipGeneration;if(fault==1)++input.owner.space;
        if(fault==2)--input.deadlineNs;if(fault==3)++controls.menu.epoch;
        if(fault==4)++controls.trackingBasisGeneration;if(fault==5)controls.hands[1].trigger=1;
        if(fault==6){++input.sequence;++controls.generation;} // Newer sequence cannot reuse the old observed time.
        if(fault==7){--input.sequence;--controls.generation;}
        if(fault==8)controls.hands[1].squeeze=0;if(fault==9)controls.hands[0].squeeze=.5f;
        if(fault==10)controls.worldUnitsPerMeter*=2;
        CHECK(!ChestKnifeLiveJoinRejection(v,owner,input,controls,now).empty());
    }
    auto next=v;next.input.sequence++;next.input.observedNs+=10000000;next.input.nowNs+=10000000;next.input.deadlineNs+=10000000;
    next.controls.generation=next.input.sequence;next.controls.predictedNs=next.input.observedNs;
    next.hold.inputSequence=next.input.sequence;next.nativeSequence=next.input.sequence;next.nativeObservedNs=next.input.nowNs;
    CHECK(ChestKnifeLiveRejection(next,next.input.nowNs).empty());
    CHECK(ChestKnifeLiveJoinRejection(v,next.owner,next.input,next.controls,next.input.nowNs).empty());
    CHECK(ChestKnifeLiveContinues(v,next,next.input.nowNs)); // New input keeps the old render/claim expiry.
    CHECK(ChestKnifeLiveDeadline(next)==v.input.deadlineNs);
    auto recovered=next;++recovered.continuity;CHECK(!ChestKnifeLiveContinues(v,recovered,next.input.nowNs));
    recovered=next;++recovered.originalGun.generation;CHECK(!ChestKnifeLiveContinues(v,recovered,next.input.nowNs));
    recovered=next;++recovered.controls.trackingBasisGeneration;CHECK(!ChestKnifeLiveContinues(v,recovered,next.input.nowNs));
    CHECK(!ChestKnifeLiveContinues(v,next,v.input.deadlineNs)); // Serializing later cannot renew original proof.
    std::ostringstream out;WriteChestKnifeLiveObservation(out,&v,v.owner,v.input,v.controls,now);
    CHECK(out.str().size()<6000&&out.str().find("\"available\":true")!=std::string::npos);
    CHECK(out.str().find("\"validUntilNs\":1100000000")!=std::string::npos&&out.str().find("\"stockTriggerArmed\":true")!=std::string::npos);
    CHECK(out.str().find("\"originalOwner\":")!=std::string::npos&&out.str().find("\"selectedSlots\":[7,7,7]")!=std::string::npos);
    std::ostringstream expired;WriteChestKnifeLiveObservation(expired,&v,v.owner,v.input,v.controls,v.input.deadlineNs);
    CHECK(expired.str().find("\"available\":false")!=std::string::npos&&expired.str().find("\"held\":true")==std::string::npos);
    std::ostringstream missing;WriteChestKnifeLiveObservation(missing,nullptr,v.owner,v.input,v.controls,now);
    CHECK(missing.str().find("observation_unavailable")!=std::string::npos&&missing.str().find("\"held\":true")==std::string::npos);
    ChestKnifeLivePublication publication;
    CHECK(!publication.Publish(v,now)&&!publication.Read(now));
    publication.Request(now);CHECK(publication.Publish(v,now));const auto first=publication.Read(now);CHECK(first);
    publication.Begin();CHECK(!publication.Read(now)); // No preceding gather while current gates are being evaluated.
    CHECK(publication.Publish(next,next.input.nowNs));const auto continuous=publication.Read(next.input.nowNs);CHECK(continuous);
    CHECK(ChestKnifeLiveContinues(*first,*continuous,next.input.nowNs));
    publication.Invalidate();CHECK(!publication.Read(next.input.nowNs));
    CHECK(publication.Publish(next,next.input.nowNs));const auto renewed=publication.Read(next.input.nowNs);CHECK(renewed);
    CHECK(!ChestKnifeLiveContinues(*first,*renewed,next.input.nowNs)); // Missing input permanently retires old admission.
    auto rejected=next;rejected.nativeHealthy=false;CHECK(!publication.Publish(rejected,next.input.nowNs));
    CHECK(!publication.Read(next.input.nowNs));CHECK(publication.Publish(next,next.input.nowNs));
    CHECK(!ChestKnifeLiveContinues(*renewed,*publication.Read(next.input.nowNs),next.input.nowNs));
    publication.Request(next.input.nowNs);CHECK(!publication.Read(v.input.deadlineNs)); // Demand renewal is not source renewal.
    CHECK(!publication.Requested(next.input.nowNs+1000000000));
    for(unsigned race=0;race<4;++race){ChestKnifeLivePublication p;p.Request(now);CHECK(p.Publish(v,now));
        unsigned reads=0;auto clock=now;
        const auto section=ObserveChestKnifeLiveJson([&](std::int64_t sampled){
            if(++reads==2){
                if(race==0)p.Begin();
                if(race==1)p.Invalidate();
                if(race==2)clock=v.input.deadlineNs;
                if(race==3){p.Invalidate();p.Publish(v,now);}
            }
            return p.Read((std::max)(sampled,clock));
        },[&](){return clock;},v.owner,v.input,v.controls);
        const auto outer=std::string("{\"valid\":true,\"nonce\":\"42\",\"chestKnife\":")+section+"}";
        CHECK(outer.starts_with("{\"valid\":true,\"nonce\":\"42\""));
        CHECK(section.find("\"available\":false")!=std::string::npos&&section.find("\"held\":true")==std::string::npos);
        CHECK(reads==2); // No retries/waits and no cancellation of the enclosing snapshot.
    }

    // Independent body observation cannot promote the invalid firearm frame.
    {
        ChestKnifeLivePublication p;p.Request(now);CHECK(p.Publish(v,now));unsigned reads=0;
        const auto section=ObserveOwnedChestKnifeLiveJson([&](std::int64_t sampled){++reads;return p.Read(sampled);},[&](){return now;});
        const auto outer=std::string("{\"valid\":false,\"invalid_reason\":\"selected_configuration_or_rig_missing_changed_or_expired\",\"chestKnife\":")+section+"}";
        CHECK(outer.starts_with("{\"valid\":false,"));CHECK(reads==2);
        CHECK(section.find("\"available\":true")!=std::string::npos);
        CHECK(section.find("\"validUntilNs\":1100000000")!=std::string::npos);
        CHECK(section.find("\"originalOwner\":")!=std::string::npos&&section.find("\"selectedSlots\":[7,7,7]")!=std::string::npos);
    }
    for(unsigned fault=0;fault<8;++fault){ChestKnifeLivePublication p;p.Request(now);CHECK(p.Publish(v,now));
        unsigned reads=0;auto clock=now;
        const auto section=ObserveOwnedChestKnifeLiveJson([&](std::int64_t sampled){
            if(++reads==2){
                if(fault==0)p.Begin();
                if(fault==1)p.Invalidate();
                if(fault==2)clock=v.input.deadlineNs;
                if(fault==3){p.Invalidate();p.Publish(v,now);}
                if(fault==4){auto changed=v;changed.owner.equipGeneration++;changed.rendered.source.owner=changed.owner;p.Publish(changed,now);}
                if(fault==5){auto changed=v;changed.hold.token.id++;p.Publish(changed,now);}
                if(fault==6){auto changed=v;changed.phase=KnifePhase::Returning;p.Publish(changed,now);}
                if(fault==7){auto changed=v;changed.hold.deadlineNs=now;p.Publish(changed,now);}
            }
            return p.Read((std::max)(sampled,clock));
        },[&](){return clock;});
        CHECK(reads==2);CHECK(section.find("\"available\":false")!=std::string::npos);
        CHECK(section.find("\"held\":true")==std::string::npos);
    }
    {
        ChestKnifeLivePublication p;p.Request(now);unsigned reads=0;
        const auto section=ObserveOwnedChestKnifeLiveJson([&](std::int64_t sampled){++reads;return p.Read(sampled);},[&](){return now;});
        CHECK(reads==1);CHECK(section.find("observation_unavailable")!=std::string::npos);
    }
    std::puts("Passive live knife observation: exact owner/source/claim/native joins, mode separation, continuity and bounded serialization negatives.");
    return 0;
}
}
#include "KnifeNativeReadDiagnosticCases.inc"
#include "KnifeInspectionCases.inc"
int main(){
    CHECK(!knife_read_diagnostic_cases::Cases());
    CHECK(!knife_inspection_cases::Cases());
    {
        private_knife::Item item;
        for(auto* s:{&item.client.branches[0],&item.client.branches[1],&item.server})s->currentState=s->nextState=2;
        CHECK(chestKnifeNative::InspectReady(item).reason==chestKnifeNative::ReadyReason::Ready);
        for(unsigned branch=0;branch<3;++branch)for(unsigned field=0;field<6;++field){
            auto bad=item;auto& s=branch<2?bad.client.branches[branch]:bad.server;
            if(field==0)s.currentState=3;if(field==1)s.nextState=3;
            if(field==2)s.phaseTimer=std::numeric_limits<float>::quiet_NaN();
            if(field==3)s.phaseTimer=-1;if(field==4)s.phaseTimer=1000001;if(field==5)s.flagsA8=8;
            const auto r=chestKnifeNative::InspectReady(bad);CHECK(r.branch==branch);
            CHECK(unsigned(r.reason)==unsigned(chestKnifeNative::ReadyReason::Current)+field);
            std::ostringstream json;chestKnifeNative::WriteReadyObservation(json,r);
            CHECK(json.str().find("nan")==std::string::npos);
            if(field==2)CHECK(json.str().find("\"phaseTimer\":null")!=std::string::npos);
        }
        auto v=LiveFixture();ChestKnifeLivePublication p;const auto now=v.input.nowNs;p.Request(now);
        CHECK(p.Publish(v,now));v.nativeObservationComplete=false;v.firstObservationGap=5*4+1;
        CHECK(!p.Publish(v,now));CHECK(!p.Read(now));const auto first=p.ReadDiagnostic();CHECK(first);
        CHECK(first->first.branch=="native_observation_complete");
        p.Begin();p.Invalidate();CHECK(p.ReadDiagnostic()->first.observedNs==now);
        auto later=v;later.input.nowNs++;later.stockTriggerReady=false;
        CHECK(!p.Publish(later,now+1));const auto retained=p.ReadDiagnostic();CHECK(retained);
        CHECK(retained->first.branch=="native_observation_complete"&&retained->latest.branch=="stock_trigger_ready");
        CHECK(retained->first.value.input.deadlineNs==v.input.deadlineNs);
        std::ostringstream out;WriteChestKnifePublicationDiagnostic(out,retained);
        CHECK(out.str().size()<8192&&out.str().find("\"actionAuthority\":false")!=std::string::npos);
        CHECK(out.str().find("\"held\":true")==std::string::npos&&out.str().find("validUntilNs")==std::string::npos);
        CHECK(out.str().find("\"reason\":5,\"branch\":1")!=std::string::npos);
        p.Begin();CHECK(!p.Read(now));CHECK(p.ReadDiagnostic());
        p.Reject(later,now+2,"producer_prerequisite","render_receipt");
        CHECK(p.ReadDiagnostic()->latest.branch=="render_receipt");
        CHECK(p.ReadDiagnostic()->first.branch=="native_observation_complete");
        ++later.bodyOperation;++later.draws;p.Reject(later,now+3,"producer_prerequisite","native_view");
        CHECK(p.ReadDiagnostic()->first.branch=="native_view");
        p.Request(v.input.deadlineNs);CHECK(!p.Read(v.input.deadlineNs));
        CHECK(p.ReadDiagnostic()->latest.value.input.deadlineNs==v.input.deadlineNs);
    }
    {
        // Actual593 exhibited this dependency: a real held claim stopped
        // renewing after input7356, then expired on7360. A missed receipt must
        // not forbid rendering while that SAME claim is still live. Fixture
        // contact below is a test-owned renderer stand-in, not native evidence.
        Fixture f;CHECK(f.Held());const auto before=*f.hands.Current(InteractionHand::Right);
        f.s.trackedKnifeOwned=false;CHECK(!f.Step());
        CHECK(!ChestKnifePoseCustody(f.knife,f.hands,f.s.source));
        CHECK(ChestKnifeRenderPoseCustody(f.knife,f.hands,f.s.source));
        const auto suspended=*f.hands.Current(InteractionHand::Right);
        CHECK(suspended.token==before.token&&suspended.inputSequence==before.inputSequence&&suspended.deadlineNs==before.deadlineNs);
        // Reading render custody never mints/renews anything or enables attack.
        CHECK(!f.knife.StockTriggerArmedFor(f.s,f.hands));
        CHECK(!f.knife.RecognitionReady(f.s.source));
        CHECK(!f.knife.NeedsInitialHeldPose());
        auto future=f.s.source;future.sequence=before.inputSequence-1;
        CHECK(!ChestKnifeRenderPoseCustody(f.knife,f.hands,future));
        future.sequence=0;CHECK(!ChestKnifeRenderPoseCustody(f.knife,f.hands,future));
        for(unsigned owner=0;owner<4;++owner){auto wrong=f.s.source;
            if(owner==0)++wrong.owner.actor;if(owner==1)++wrong.owner.actorGeneration;
            if(owner==2)++wrong.owner.equipGeneration;if(owner==3)++wrong.owner.space;
            CHECK(!ChestKnifeRenderPoseCustody(f.knife,f.hands,wrong));}
        // Only a later actually supplied receipt permits the ordinary Prepare
        // path to renew. There is no receipt construction in the helper.
        f.s.trackedKnifeOwned=true;CHECK(!f.Step());
        const auto recovered=*f.hands.Current(InteractionHand::Right);
        CHECK(recovered.token==before.token&&recovered.inputSequence==f.s.source.sequence);
        CHECK(ChestKnifePoseCustody(f.knife,f.hands,f.s.source));
        CHECK(ChestKnifeRenderPoseCustody(f.knife,f.hands,f.s.source));
    }
    {
        Fixture f;CHECK(f.Held());const auto before=*f.hands.Current(InteractionHand::Right);
        f.s.trackedKnifeOwned=false;CHECK(!f.Step(100*ms));
        CHECK(!f.hands.Current(InteractionHand::Right));
        CHECK(!ChestKnifeRenderPoseCustody(f.knife,f.hands,f.s.source));
        f.s.trackedKnifeOwned=true;CHECK(!f.Step());
        CHECK(!f.hands.Current(InteractionHand::Right)); // no expired reacquire
        CHECK(!ChestKnifeRenderPoseCustody(f.knife,f.hands,f.s.source));
        CHECK(!f.knife.Hold()||f.knife.Hold()->id==before.token.id);
    }
    for(unsigned fault=0;fault<7;++fault){Fixture f;CHECK(f.Held());f.s.trackedKnifeOwned=false;
        if(fault==0)f.Grip(false);if(fault==1)f.s.source.focused=false;if(fault==2)f.s.source.tracked[1]=false;
        if(fault==3)++f.s.source.owner.actor;if(fault==4)++f.s.source.owner.actorGeneration;
        if(fault==5)++f.s.source.owner.equipGeneration;if(fault==6)++f.s.source.owner.space;
        (void)f.Step();CHECK(!f.hands.Current(InteractionHand::Right));
        CHECK(!ChestKnifeRenderPoseCustody(f.knife,f.hands,f.s.source));
    }
    {
        Fixture f;CHECK(f.Held());const auto old=*f.knife.Hold();
        auto replacement=f.hands.Transfer(f.s.source,old,{f.s.source.owner,InteractionHand::Right,
            HandClaimKind::BodyInventory,{999,1},f.s.heldContact,f.s.claimIntent+1,0});
        CHECK(replacement.claim&&replacement.claim->token!=old);
        CHECK(!ChestKnifeRenderPoseCustody(f.knife,f.hands,f.s.source));
    }
    CHECK(!ReturnObservationCases());
    CHECK(!LiveObservationCases());
    CHECK(!CurrentHoldCases());
    {private_knife::OriginalKnifeSource source;HandInteractionSample value{{1,2,3,4},9,1000000000,1100000000,1000000000,true,{true,true}};
     CHECK(source.Advance(value));CHECK(!source.Advance(value));++value.nowNs;CHECK(!source.Advance(value));
     ++value.sequence;CHECK(source.Advance(value));value.focused=false;CHECK(source.Advance(value));
     value.focused=true;CHECK(source.Advance(value));value.tracked[1]=false;CHECK(source.Advance(value));
     value.released[1]=true;CHECK(source.Advance(value));++value.owner.space;CHECK(source.Advance(value));
     ++value.deadlineNs;CHECK(source.Advance(value));value={};CHECK(source.Advance(value));CHECK(!source.Advance(value));}
    {ChestKnifeRenderSource source;source.owner={0x10000,0x20000,0x30000,0x52000,4,7,9};
     source.input={{(std::uint64_t(0x30000)<<32)|0x20000,4,99,9},2,1000000000,1100000000,1000000000,true,{true,true}};
     source.knife={0x52000,3};source.bodyOperation=17;auto current=source.input;
     CHECK(ChestKnifeRenderCurrent(source,source.owner,current,current.nowNs));
     ++current.sequence;current.observedNs+=10000000;current.deadlineNs+=10000000;current.nowNs=current.observedNs;
     CHECK(ChestKnifeRenderCurrent(source,source.owner,current,current.nowNs));
     CHECK(!ChestKnifeRenderCurrent(source,source.owner,current,source.input.deadlineNs)); // no old lease renewal
     CHECK(!ChestKnifeRenderCurrent(source,source.owner,current,current.observedNs-1));
     for(unsigned fault=0;fault<7;++fault){auto bad=source;auto now=current;
       if(fault==0)++bad.owner.equipGeneration;if(fault==1)++now.owner.equipGeneration;if(fault==2)++now.owner.space;
       if(fault==3)now.released[1]=true;if(fault==4)bad.input.focused=false;if(fault==5)bad.input.tracked[1]=false;
       if(fault==6)bad.bodyOperation=0;
       CHECK(!ChestKnifeRenderCurrent(bad,source.owner,now,current.nowNs));}}
    {const auto& profile=ChestKnifeGripProfiles()[0];CHECK(authored_grip_detail::Profile(profile));CHECK(!profile.authoredRifleSupport);
     CHECK(profile.authoredAxisEvidence.empty()&&!private_knife::LeftHandGraspSupported);
     auto mesh=std::make_shared<SelectedMeshesSnapshot>();WeaponEquipmentIdentity equipment{15,16,17,{}};
     std::copy(profile.assetName.begin(),profile.assetName.end(),equipment.asset.begin());
     mesh->owner={9,10,11,15,12,18,14};mesh->sequence=19;mesh->observedNs=990000000;mesh->deadlineNs=1200000000;
     mesh->weaponData=16;mesh->stateTypeInfo=1;mesh->meshTypeInfo=2;mesh->inventory=3;mesh->selectedSlot=7;
     std::copy(profile.assetName.begin(),profile.assetName.end(),mesh->weaponName.begin());
     mesh->stateCount=1;mesh->soleConfiguredArray=5;mesh->states[0].array=5;mesh->states[0].state=6;mesh->states[0].count=1;
     auto& selected=mesh->states[0].meshes[0];selected.address=7;selected.typeInfo=2;selected.namePointer=8;
     std::copy(profile.meshPath.begin(),profile.meshPath.end(),selected.assetPath.begin());
     const AuthoredGripOwner owner{10,11,12,13,14,20,1090000000};
     const auto bind=[&](std::uint64_t rig){return BindAuthoredGrip(ChestKnifeGripProfiles(),mesh,equipment,owner,rig,1000000000,false);};
     const auto bound=bind(profile.rigFingerprint);CHECK(bound.binding&&!AuthoredRifleSupport(*bound.binding));
     CHECK(!bind(profile.rigFingerprint+1).binding);selected.assetPath[0]='?';CHECK(!bind(profile.rigFingerprint).binding);
     CHECK(!AuthoredGripBinding::aimCapability&&!AuthoredGripBinding::muzzleCapability&&!AuthoredGripBinding::reloadCapability);}
    std::puts("Three knife source/render groups PASS; original epoch invalidation, immutable render custody/expiry, measured grip exact binding.");
}
