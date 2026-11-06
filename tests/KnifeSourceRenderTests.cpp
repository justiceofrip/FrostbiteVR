#include "Test.h"
#include "Bc2KnifeDrawReturnGate.h"
#include "Bc2ChestKnifeRendering.h"
#include "Bc2ChestKnifeLiveObservation.h"
#include <cstdio>
#include <sstream>
using namespace fvr;using namespace fvr::bc2;using namespace fvr::interaction;
namespace {
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
int LiveObservationCases(){
    const auto v=LiveFixture();const auto now=v.input.nowNs;
    CHECK(ChestKnifeLiveRejection(v,now).empty());
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
        case 12:bad.motionEnabled=true;break;case 13:bad.attackBridgeEnabled=true;break;
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
    std::puts("Passive live knife observation: exact owner/source/claim/native joins, mode separation, continuity and bounded serialization negatives.");
    return 0;
}
}
int main(){
    CHECK(!LiveObservationCases());
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
