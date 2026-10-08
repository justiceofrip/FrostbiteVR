#include "Test.h"
#include "Bc2BodyHolsterProbe.h"
#include "Bc2BodyHolsterProbeFixture.h"
#include <bit>
#include "Bc2InputBinding.h"
#include <sstream>
#include <iostream>
#include "fvr/interaction/BodyGripRelease.h"
using namespace body_probe_test;
namespace {
void Put(std::span<std::byte> b,unsigned at,unsigned x){std::memcpy(b.data()+at,&x,4);}
unsigned Get(std::span<const std::byte> b,unsigned at){unsigned x;std::memcpy(&x,b.data()+at,4);return x;}
HolsterInputOwner Owner(Fixture& f){return {&f,[](void* p,const HolsterSuppressionRequest&)noexcept{return static_cast<Fixture*>(p)->ownerCurrent;}};}
int Flags(){constexpr auto flags=BodyHolsterProbeFlag|0x12197809u;
    CHECK(ValidBodyHolsterProbeConfig(0,0));CHECK(ValidBodyHolsterProbeConfig(flags,15000));CHECK(ValidBodyHolsterProbeConfig(flags|0x200u,15000));
    CHECK(!ValidBodyHolsterProbeConfig(flags,14999));CHECK(!ValidBodyHolsterProbeConfig(flags&~0x2000000u,15000));
    for(auto bit:{0x400u,0x100u,0x8000u,0x20000u,0x40000u,0x200000u,0x400000u,0x800000u,0x1000000u,0x4000000u,0x8000000u,0x20000000u,0x40000000u})
        CHECK(!ValidBodyHolsterProbeConfig(flags|bit,15000));return 0;}
int ActualCacheSuppression(){Fixture f;CHECK(f.Empty());const auto request=f.adapter.Demand(f.s);CHECK(request);
    Put(f.cache,0x98,1u<<15);Put(f.cache,0x9c,1u<<2);const auto ordinary=f.cache;
    BodyHolsterCacheChallenge challenge;CHECK(challenge.Stage(f.cache,*request,Owner(f),*f.out.freeRight,f.hands,f.s.hand.nowNs+1000000000));
    CHECK(Get(f.cache,36)==std::bit_cast<unsigned>(1.f)&&Get(f.cache,40)==std::bit_cast<unsigned>(1.f));
    HolsterInputOverride actual;CHECK(actual.Apply(f.cache,*request,Owner(f)));const auto receipt=actual.Commit();
    CHECK(challenge.Complete(receipt)&&challenge.Evidence().committed&&challenge.Evidence().unrelatedPreserved);
    CHECK(f.cache==ordinary);return 0;}
int ChallengeFailuresRetire(){for(unsigned bad=0;bad<5;++bad){Fixture f;CHECK(f.Empty());const auto request=f.adapter.Demand(f.s);CHECK(request);
        Put(f.cache,0x98,1u<<15);const auto ordinary=f.cache;BodyHolsterCacheChallenge challenge;
        CHECK(challenge.Stage(f.cache,*request,Owner(f),*f.out.freeRight,f.hands,f.s.hand.nowNs+1000000000));
        if(bad==0){f.ownerCurrent=false;CHECK(!challenge.Complete({}));}
        if(bad==1){HolsterInputOverride patch;CHECK(patch.Apply(f.cache,*request,Owner(f)));auto r=patch.Commit();CHECK(r);++r->nativeTick;CHECK(!challenge.Complete(r));}
        if(bad==2){HolsterInputOverride patch;CHECK(patch.Apply(f.cache,*request,Owner(f)));auto r=patch.Commit();Put(f.cache,0x9c,0x70);CHECK(!challenge.Complete(r));}
        if(bad==3){CHECK(challenge.Restore());}
        if(bad==4){HolsterInputOverride patch;CHECK(patch.Apply(f.cache,*request,Owner(f)));f.ownerCurrent=false;CHECK(!patch.Commit());CHECK(challenge.Restore());}
        CHECK(f.cache==ordinary&&!challenge.Evidence().committed);
    }return 0;}
int MissingAuthority(){for(unsigned bad=0;bad<4;++bad){Fixture f;CHECK(f.Empty());auto request=*f.adapter.Demand(f.s);auto evidence=*f.out.freeRight;
        auto deadline=f.s.hand.nowNs+1000000000;
        if(bad==0)deadline=request.input.nowNs;if(bad==1)++request.owner.weapon;
        if(bad==2)evidence.receipt.verifiedCopyMask=1;
        if(bad==3)CHECK(f.hands.Acquire(f.s.hand,{f.s.hand.owner,InteractionHand::Right,HandClaimKind::GunHold,f.s.gun,{{1,99},f.s.hand.sequence,f.s.hand.deadlineNs,true},++f.intent,0}).accepted);
        BodyHolsterCacheChallenge c;auto before=f.cache;CHECK(!c.Stage(f.cache,request,Owner(f),evidence,f.hands,deadline));CHECK(f.cache==before);
    }return 0;}
struct Driver {
    Fixture f;Bc2BodyHolsterProbe probe{true};BodyGripRelease release;std::shared_ptr<BodyHolsterProbeSample> state;
    bool previousPress=false,fireMode=false,earlyActions=false,dropRead=false,dropAmmo=false,configuredMode=false;std::uint64_t paired=0,fireEdges=0,firePackets=0,releaseStows=0;InputFrame original;ControllerActions actions;ActionOutput action;
    explicit Driver(bool xm8=false,bool fire=false,BodyHolsterDiagnosticProfile configured=BodyHolsterDiagnosticProfile::Disabled):f(xm8),probe(true,configured!=BodyHolsterDiagnosticProfile::Disabled?configured:fire?BodyHolsterDiagnosticProfile::ScopedXm8Fire:xm8?BodyHolsterDiagnosticProfile::ScopedXm8:BodyHolsterDiagnosticProfile::Spas),fireMode(fire||configured==BodyHolsterDiagnosticProfile::ExactConfiguredTableFire){
        if(configured!=BodyHolsterDiagnosticProfile::Disabled){configuredMode=true;f.adapter=Bc2BodyHolster{};f.adapter.AdmitDiagnostic(1000000000,16000000000ll,configured);ExactMetadata();f.Tick();f.Advance();ExactMetadata();f.Tick();}
        original.focused=original.headValid=true;original.predictedNs=1000000000;original.spaceGeneration=f.s.nativeOwner.space;
        for(auto& hand:original.hands){hand.gripTracked=hand.aimTracked=true;hand.active=Components;}
        original.hands[0].grip.position={-.2f,-.25f,-.45f};original.hands[1].grip.position={.2f,-.25f,-.45f};Publish(original);}
    void ExactMetadata(){if(!configuredMode)return;auto copy=std::make_shared<SelectedMeshesSnapshot>(*f.s.selected);copy->stateTypeInfo=0xc0000;copy->meshTypeInfo=0xa0000;
        for(unsigned n=0;n<copy->states[0].count;++n)copy->states[0].meshes[n].typeInfo=copy->meshTypeInfo;f.s.selected=copy;}
    void Publish(const InputFrame& input){state=std::make_shared<BodyHolsterProbeSample>();state->sampledNs=f.s.hand.nowNs;
        state->trialStartNs=1000000000;state->trialDeadlineNs=16000000000ll;state->nativeOwner=f.s.nativeOwner;state->input=input;
        state->input.generation=f.s.hand.sequence;state->hand=f.s.hand;state->physicalGun=f.s.gun;state->nativeTick=f.s.nativeTick;state->request=f.adapter.RequestId();state->phase=f.adapter.Phase();
        state->selectedSlot=BodySlotAssignment{1,f.items[0].key};state->left=f.hands.Current(InteractionHand::Left);state->right=f.hands.Current(InteractionHand::Right);
        state->outcome=f.out;state->visibility=f.s.visibility;state->suppression=f.s.suppression;
        if(fireMode){state->fireTickMs=std::uint64_t(f.s.hand.nowNs/1000000);state->fireRequested=(action.held&Fire)!=0;
            state->fireCacheRead=!dropRead;state->fireCache=std::bit_cast<float>(Get(f.cache,8+4*unsigned(EntryAction::Fire)));}
        if(f.out.freeRight)paired+=2;state->pack={paired/2,paired*2,paired,0};}
    bool Tick(bool challenge=true){const auto prior=f.out.visibility;f.Advance();ExactMetadata();original.generation=f.s.hand.sequence;
        auto input=original;input.predictedNs=f.s.hand.nowNs;
        if(earlyActions)action=actions.Update(input,{f.s.nativeOwner.soldier,1,true,true},input.predictedNs);
        Bc2AmmoReserveLease ammo{{f.s.nativeOwner,{0x71000,0x72000,0x73000},0x74000,0x75000,0x76000},f.s.hand.sequence,f.s.hand.observedNs,f.s.hand.deadlineNs,8,22,8,true,false,true};
        probe.Prepare(input,f.s.nativeOwner,f.xm8?"XM8_sp_s":"SPAS12_sp",state,f.s.hand.observedNs,f.s.hand.deadlineNs,f.s.hand.nowNs,dropAmmo?std::nullopt:std::optional{ammo});
        if(!earlyActions)action=actions.Update(input,{f.s.nativeOwner.soldier,1,!probe.Failed(),true},input.predictedNs);
        if(probe.NeedsNeutralFire()){action.held&=~Fire;action.pressed&=~Fire;}
        fireEdges+=(action.pressed&Fire)!=0;firePackets+=(action.held&Fire)!=0;
        f.cache={}; // The genuine original gather rebuilds every native action.
        InputOverride ordinary;if(action.active){if(!ordinary.Apply(f.cache,action))return false;ordinary.Commit();}
        // Gesture cancellation is tested with the actual BodyInventory adapter
        // in Bc2BodyInventoryTests. This fixture supplies its retained Held state.
        f.s.cancel=false;
        f.Render(prior);f.Suppress();const auto body=BodyAnchorHandPose(input,InteractionHand::Right);
        const bool press=input.hands[1].squeeze>=.75f;
        // Synthetic native-memory/renderer unit fixture. The executable driver
        // itself NEVER creates these contact intents or renderer receipts.
        const bool contact=body&&BodyAnchorContains(state->anchors.shoulders[0],*body);
        const auto right=f.hands.Current(InteractionHand::Right);
        const auto released=release.Update(f.s.hand,f.s.gun,1,right?right->token.id:0,
            press,input.hands[1].squeeze<=.35f,f.adapter.Phase()==BodyHolsterPhase::Held&&right&&input.hands[1].trigger<=.1f,
            contact?std::optional(BodySlotAssignment{1,f.items[0].key}):std::nullopt);
        if(released)++releaseStows;
        if(((press&&!previousPress)||released)&&contact)
            f.s.body.intent={++f.intent,f.adapter.Phase()==BodyHolsterPhase::Held?BodyInventoryOperation::Holster:BodyInventoryOperation::Draw,1,f.items[0].key};
        previousPress=press;f.Tick();
        if(press&&contact&&f.adapter.Phase()==BodyHolsterPhase::Held){const auto acquired=f.hands.Current(InteractionHand::Right);
            if(acquired)release.Update(f.s.hand,f.s.gun,1,acquired->token.id,true,false,true,BodySlotAssignment{1,f.items[0].key});}
        Publish(input);probe.Observe(state,f.s.hand.nowNs);
        if(challenge&&probe.ChallengeWanted(*state,f.s.hand.nowNs)){auto r=f.adapter.Demand(f.s);if(!r||!f.out.freeRight)return false;
            BodyHolsterCacheChallenge c;if(!c.Stage(f.cache,*r,Owner(f),*f.out.freeRight,f.hands,f.s.hand.nowNs+1000000000))return false;
            HolsterInputOverride actual;if(!actual.Apply(f.cache,*r,Owner(f)))return false;auto receipt=actual.Commit();c.Complete(receipt);probe.RecordChallenge(c.Evidence(),f.s.hand.nowNs);}
        return true;}
};
int DistinctPhysicalIdentityBaseline(){Driver d(true);
    CHECK(d.f.s.gun.id!=d.f.s.nativeOwner.weapon);
    for(unsigned n=0;n<300&&d.probe.Phase()==BodyHolsterFixturePhase::Warmup;++n)CHECK(d.Tick());
    if(d.probe.Phase()!=BodyHolsterFixturePhase::Baseline)d.probe.Report(std::cerr);
    CHECK(d.probe.Phase()==BodyHolsterFixturePhase::Baseline);return 0;
}
int PhysicalIdentityMustStayExact(){for(unsigned bad=0;bad<7;++bad){Driver d(true);CHECK(d.Tick());
    CHECK(d.probe.Phase()==BodyHolsterFixturePhase::Baseline);
    if(bad==0)++d.state->physicalGun.id;
    if(bad==1)++d.state->right->token.item.id;
    if(bad==2)++d.state->physicalGun.generation;
    if(bad==3){++d.state->physicalGun.id;d.state->right->token.item=d.state->physicalGun;}
    if(bad==4)++d.state->right->token.owner.equipGeneration;
    if(bad==5)d.state->right->token.hand=InteractionHand::Left;
    if(bad==6)++d.state->selectedSlot->item.id;
    d.probe.Observe(d.state,d.f.s.hand.nowNs);CHECK(d.probe.Failed());
    }return 0;}
int WholeDiagnosticSequence(){Driver d;for(unsigned n=0;n<1100&&d.probe.Phase()!=BodyHolsterFixturePhase::Done&&!d.probe.Failed();++n)CHECK(d.Tick());
    if(d.probe.Phase()!=BodyHolsterFixturePhase::Done)d.probe.Report(std::cerr);
    CHECK(d.probe.Phase()==BodyHolsterFixturePhase::Done);CHECK(d.f.hands.Current(InteractionHand::Right));CHECK(d.paired>4);
    std::ostringstream out;d.probe.Report(out);CHECK(out.str().find("\"committed\":true")!=std::string::npos);CHECK(out.str().find("\"production_input_accepted\":false")!=std::string::npos);return 0;}
int WholeScopedXm8DiagnosticSequence(){Driver d(true);
    CHECK(d.f.adapter.DiagnosticProfile()==BodyHolsterDiagnosticProfile::ScopedXm8);
    for(unsigned n=0;n<1100&&d.probe.Phase()!=BodyHolsterFixturePhase::Done&&!d.probe.Failed();++n)CHECK(d.Tick());
    if(d.probe.Phase()!=BodyHolsterFixturePhase::Done)d.probe.Report(std::cerr);
    CHECK(d.probe.Phase()==BodyHolsterFixturePhase::Done&&d.f.hands.Current(InteractionHand::Right)&&d.paired>4);
    std::ostringstream out;d.probe.Report(out);const auto text=out.str();
    CHECK(text.find("\"diagnostic_profile\":2")!=std::string::npos&&text.find("\"asset\":\"XM8_sp_s\"")!=std::string::npos);
    CHECK(text.find("\"committed\":true")!=std::string::npos&&text.find("\"production_input_accepted\":false")!=std::string::npos);
    return 0;
}
int ExplicitDiagnosticConfig(){constexpr auto flags=BodyHolsterProbeFlag|0x12197809u;
    CHECK(ValidBodyHolsterDiagnosticConfig(BodyHolsterDiagnosticProfile::Disabled,0));
    CHECK(!ValidBodyHolsterDiagnosticConfig(BodyHolsterDiagnosticProfile::ScopedXm8,0));
    CHECK(!ValidBodyHolsterDiagnosticConfig(BodyHolsterDiagnosticProfile::Disabled,flags));
    CHECK(ValidBodyHolsterDiagnosticConfig(BodyHolsterDiagnosticProfile::Spas,flags));
    CHECK(ValidBodyHolsterDiagnosticConfig(BodyHolsterDiagnosticProfile::ScopedXm8,flags));
    CHECK(ValidBodyHolsterDiagnosticConfig(BodyHolsterDiagnosticProfile::ExactConfiguredTableFire,flags));
    CHECK(!ValidBodyHolsterDiagnosticConfig(static_cast<BodyHolsterDiagnosticProfile>(6),flags));
    CHECK(ValidBodyHolsterDiagnosticConfig(BodyHolsterDiagnosticProfile::ExactConfiguredTable,flags));
    CHECK(!ValidBodyHolsterDiagnosticConfig(BodyHolsterDiagnosticProfile::ExactConfiguredTable,0));
    CHECK(BodyHolsterDiagnosticMask(BodyHolsterDiagnosticProfile::ExactConfiguredTable)==0);
    CHECK(BodyHolsterDiagnosticMask(BodyHolsterDiagnosticProfile::ScopedXm8)==2);
    CHECK(ValidBodyHolsterDiagnosticConfig(BodyHolsterDiagnosticProfile::ScopedXm8Fire,flags));
    CHECK(!ValidBodyHolsterDiagnosticConfig(BodyHolsterDiagnosticProfile::ScopedXm8Fire,0));
    CHECK(BodyHolsterDiagnosticMask(BodyHolsterDiagnosticProfile::ScopedXm8Fire)==2);
    Driver d(true);auto input=d.original;input.generation=1;
    d.probe.Prepare(input,d.f.s.nativeOwner,"SPAS12_sp",{},1000000000,1100000000,1000000000);CHECK(d.probe.Failed());
    return 0;
}
int RestoredActionBlockCannotPass(){for(unsigned bad=0;bad<2;++bad){Driver d(true);
    for(unsigned n=0;n<1100&&d.probe.Phase()!=BodyHolsterFixturePhase::Restored&&!d.probe.Failed();++n)CHECK(d.Tick());
    CHECK(d.probe.Phase()==BodyHolsterFixturePhase::Restored);
    if(bad==0)d.state->outcome.blockWeaponActions=true;else d.state->outcome.allowAutomaticGunHold=false;
    d.probe.Observe(d.state,d.f.s.hand.nowNs);CHECK(d.probe.Failed());
    }return 0;}
int OnePostDrawOrdinaryPulse(){Driver d(true,true);
    for(unsigned n=0;n<1200&&d.probe.Phase()!=BodyHolsterFixturePhase::Done&&!d.probe.Failed();++n){CHECK(d.Tick());
        if(d.action.held&Fire){CHECK(d.f.adapter.Phase()==BodyHolsterPhase::Held);CHECK(!d.f.out.blockWeaponActions);}}
    if(d.probe.Phase()!=BodyHolsterFixturePhase::Done)d.probe.Report(std::cerr);
    CHECK(d.probe.Phase()==BodyHolsterFixturePhase::Done&&d.fireEdges==1&&d.firePackets>=1&&d.firePackets<=10);
    CHECK(d.action.held==0&&Get(d.f.cache,8+4*unsigned(EntryAction::Fire))==0);
    std::ostringstream out;d.probe.Report(out);CHECK(out.str().find("\"shot_verified\":false")!=std::string::npos);return 0;
}
int OriginalEarlyActionOrderCannotFire(){Driver d(true,true);d.earlyActions=true;
    for(unsigned n=0;n<1200&&!d.probe.Failed()&&d.probe.Phase()!=BodyHolsterFixturePhase::Done;++n)CHECK(d.Tick());
    CHECK(d.probe.Failed()&&d.fireEdges==0&&d.probe.Sample().failure==20);return 0;
}
int NeutralProfilesNeverPulse(){for(bool xm8:{false,true}){Driver d(xm8);
    for(unsigned n=0;n<1200&&d.probe.Phase()!=BodyHolsterFixturePhase::Done&&!d.probe.Failed();++n)CHECK(d.Tick());
    CHECK(d.probe.Phase()==BodyHolsterFixturePhase::Done&&d.firePackets==0&&d.fireEdges==0);}
    return 0;
}
int LostRestoredProofNeverPulses(){for(unsigned bad=0;bad<6;++bad){Driver d(true,true);
    for(unsigned n=0;n<1200&&d.probe.Phase()!=BodyHolsterFixturePhase::FirePulse&&!d.probe.Failed();++n)CHECK(d.Tick());
    CHECK(d.probe.Phase()==BodyHolsterFixturePhase::FirePulse&&d.firePackets==0);
    if(bad==0)d.state->outcome.blockWeaponActions=true;if(bad==1)++d.state->nativeOwner.weapon;
    if(bad==2)++d.state->right->token.id;if(bad==3)d.state->hand.focused=false;
    if(bad==4)d.state->suppression=HolsterSuppressionReceipt{};if(bad==5)d.state->right.reset();
    CHECK(d.Tick());CHECK(d.probe.Failed()&&d.firePackets==0);}
    return 0;
}
int MissingCacheReceiptFails(){Driver d(true,true);d.dropRead=true;
    for(unsigned n=0;n<1200&&!d.probe.Failed()&&d.probe.Phase()!=BodyHolsterFixturePhase::Done;++n)CHECK(d.Tick());
    CHECK(d.probe.Failed()&&d.probe.Sample().failure==20);CHECK(d.Tick());CHECK(d.action.held==0);return 0;
}
int DuplicateProofLossDoesNotReissueFire(){Driver d(true,true);
    while(!d.firePackets&&!d.probe.Failed())CHECK(d.Tick());if(d.probe.Failed())std::cerr<<"probe_failure="<<d.probe.Sample().failure<<" phase="<<unsigned(d.probe.Phase())<<"\n";CHECK(!d.probe.Failed());
    d.state->outcome.blockWeaponActions=true;auto input=d.original;input.predictedNs=d.f.s.hand.nowNs;
    d.probe.Prepare(input,d.f.s.nativeOwner,"XM8_sp_s",d.state,d.f.s.hand.observedNs,d.f.s.hand.deadlineNs,d.f.s.hand.nowNs);
    CHECK(d.probe.Failed()&&input.hands[1].trigger==0);
    const auto out=d.actions.Update(input,{d.f.s.nativeOwner.soldier,1,!d.probe.Failed(),true},input.predictedNs);
    CHECK(!(out.held&Fire));d.f.cache={};InputOverride patch;CHECK(!patch.Apply(d.f.cache,out));
    CHECK(Get(d.f.cache,8+4*unsigned(EntryAction::Fire))==0);return 0;
}
int DuplicateBeforePulseStaysNeutral(){Driver d(true,true);
    while(d.probe.Phase()!=BodyHolsterFixturePhase::FirePulse&&!d.probe.Failed())CHECK(d.Tick());CHECK(!d.probe.Failed());
    auto input=d.original;input.predictedNs=d.f.s.hand.nowNs;
    d.probe.Prepare(input,d.f.s.nativeOwner,"XM8_sp_s",d.state,d.f.s.hand.observedNs,d.f.s.hand.deadlineNs,d.f.s.hand.nowNs);
    const auto out=d.actions.Update(input,{d.f.s.nativeOwner.soldier,1,true,true},input.predictedNs);
    CHECK(input.hands[1].trigger==0&&!(out.held&Fire));CHECK(d.Tick());CHECK(d.fireEdges==1);return 0;
}
int DuplicateDuringPulseDoesNotAddEdge(){Driver d(true,true);
    while(!d.firePackets&&!d.probe.Failed())CHECK(d.Tick());if(d.probe.Failed())std::cerr<<"probe_failure="<<d.probe.Sample().failure<<" phase="<<unsigned(d.probe.Phase())<<"\n";CHECK(!d.probe.Failed());
    auto input=d.original;input.predictedNs=d.f.s.hand.nowNs;
    d.probe.Prepare(input,d.f.s.nativeOwner,"XM8_sp_s",d.state,d.f.s.hand.observedNs,d.f.s.hand.deadlineNs,d.f.s.hand.nowNs);
    const auto out=d.actions.Update(input,{d.f.s.nativeOwner.soldier,1,true,true},input.predictedNs);
    CHECK(out.held&Fire);CHECK(!(out.pressed&Fire));return 0;
}
int NoChallengeNoSuccess(){Driver d;for(unsigned n=0;n<1300&&!d.probe.Failed();++n)CHECK(d.Tick(false));CHECK(d.probe.Failed());return 0;}
int ImmutableInputAndIdentity(){for(unsigned bad=0;bad<4;++bad){Driver d;CHECK(d.Tick());auto input=d.original;auto owner=d.f.s.nativeOwner;
        auto observed=d.f.s.hand.observedNs,deadline=d.f.s.hand.deadlineNs;if(bad==0)++deadline;if(bad==1)--input.generation;if(bad==2)++owner.weapon;if(bad==3)input.focused=false;
        d.probe.Prepare(input,owner,"SPAS12_sp",d.state,observed,deadline,d.f.s.hand.nowNs);CHECK(d.probe.Failed());}return 0;}
int WrongWeaponAndWarmup(){Driver d;auto input=d.original;input.generation=1;
    d.probe.Prepare(input,d.f.s.nativeOwner,"XM8_sp_s",{},1000000000,1100000000,1000000000);CHECK(d.probe.Failed());
    Bc2BodyHolsterProbe warming(true);warming.Prepare(input,{},"",{},1000000000,1100000000,1000000000);CHECK(!warming.Failed());
    input.generation=2;warming.Prepare(input,{},"",{},4000000000,4100000000,4000000000);CHECK(warming.Failed());return 0;}
int ConfiguredFireUsesActualAmmoGate(){
    Driver d(true,false,BodyHolsterDiagnosticProfile::ExactConfiguredTableFire);
    for(unsigned n=0;n<1200&&d.probe.Phase()!=BodyHolsterFixturePhase::Done&&!d.probe.Failed();++n)CHECK(d.Tick());
    if(d.probe.Phase()!=BodyHolsterFixturePhase::Done)d.probe.Report(std::cerr);
    CHECK(d.probe.Phase()==BodyHolsterFixturePhase::Done&&d.fireEdges==1&&d.firePackets>0);CHECK(d.releaseStows==0);
    std::ostringstream out;d.probe.Report(out);CHECK(out.str().find("\"fire_ammo_preflight\":{\"armed\":true")!=std::string::npos);
    CHECK(out.str().find("\"native_owner\":{")!=std::string::npos&&out.str().find("\"physical_owner\":{")!=std::string::npos);
    Driver missing(true,false,BodyHolsterDiagnosticProfile::ExactConfiguredTableFire);missing.dropAmmo=true;
    for(unsigned n=0;n<1200&&!missing.probe.Failed();++n)CHECK(missing.Tick());
    CHECK(missing.probe.Failed()&&missing.fireEdges==0&&missing.firePackets==0);return 0;
}
void PresentationGap(Driver& d){d.state->outcome.blockWeaponActions=true;d.state->outcome.allowAutomaticGunHold=false;
    d.state->outcome.inventoryEvaluation=BodyInventoryEvaluation::NotEvaluated;d.state->outcome.visibility={};d.state->outcome.freeRight.reset();d.state->outcome.inventory.request.reset();d.state->outcome.ordinaryDraw.command.reset();}
int MissingAmmoDuplicateCannotKeepCachedFire(){
    Driver d(true,false,BodyHolsterDiagnosticProfile::ExactConfiguredTableFire);
    while(!d.firePackets&&!d.probe.Failed())CHECK(d.Tick());CHECK(!d.probe.Failed());
    auto input=d.original;input.predictedNs=d.f.s.hand.nowNs;
    d.probe.Prepare(input,d.f.s.nativeOwner,"XM8_sp_s",d.state,d.f.s.hand.observedNs,d.f.s.hand.deadlineNs,d.f.s.hand.nowNs+1000000,{});
    CHECK(!d.probe.Failed()&&input.hands[1].trigger==0&&d.probe.NeedsNeutralFire()&&d.probe.Phase()==BodyHolsterFixturePhase::FireReleased);
    auto cached=d.actions.Update(input,{d.f.s.nativeOwner.soldier,1,true,true},input.predictedNs);
    CHECK(cached.held&Fire); // Actual immutable generation router retains Fire.
    if(d.probe.NeedsNeutralFire()){cached.held&=~Fire;cached.pressed&=~Fire;}
    CHECK(!(cached.held&Fire));d.f.cache={};InputOverride actual;CHECK(actual.Apply(d.f.cache,cached));actual.Commit();
    CHECK(Get(d.f.cache,8+4*unsigned(EntryAction::Fire))==0);
    for(unsigned n=0;n<120&&!d.probe.Failed()&&d.probe.Phase()!=BodyHolsterFixturePhase::Done;++n)CHECK(d.Tick());
    CHECK(!d.probe.Failed()&&d.probe.Phase()==BodyHolsterFixturePhase::Done&&d.fireEdges==1);
    CHECK(d.probe.NeedsNeutralFire()); // Closed pulse cannot resume cached Fire.
    return 0;
}
int FirePresentationDefersNeutralWithoutReissuing(){
    Driver d(true,true);while(!d.firePackets&&!d.probe.Failed())CHECK(d.Tick());CHECK(!d.probe.Failed());
    PresentationGap(d);auto input=d.original;input.predictedNs=d.f.s.hand.nowNs;
    d.probe.Prepare(input,d.f.s.nativeOwner,"XM8_sp_s",d.state,d.f.s.hand.observedNs,d.f.s.hand.deadlineNs,d.f.s.hand.nowNs+1000000);
    CHECK(!d.probe.Failed()&&d.probe.NeedsNeutralFire()&&input.hands[1].trigger==0&&d.probe.Phase()==BodyHolsterFixturePhase::FireReleased);
    auto cached=d.actions.Update(input,{d.f.s.nativeOwner.soldier,1,true,true},input.predictedNs); // Router duplicate still has old Fire.
    if(d.probe.NeedsNeutralFire()){cached.held&=~Fire;cached.pressed&=~Fire;}
    CHECK(!(cached.held&Fire));d.f.cache={};InputOverride actual;CHECK(actual.Apply(d.f.cache,cached));actual.Commit();CHECK(Get(d.f.cache,8+4*unsigned(EntryAction::Fire))==0);
    for(unsigned n=0;n<120&&!d.probe.Failed()&&d.probe.Phase()!=BodyHolsterFixturePhase::Done;++n)CHECK(d.Tick());
    CHECK(!d.probe.Failed()&&d.probe.Phase()==BodyHolsterFixturePhase::Done&&d.fireEdges==1);
    std::ostringstream out;d.probe.Report(out);CHECK(out.str().find("\"fire_unavailable\":{\"active\":false,\"count\":1")!=std::string::npos);
    return 0;
}
int FirePresentationGapBoundAndUnsafeOwners(){
    for(unsigned mode=0;mode<6;++mode){Driver d(true,true);while(d.probe.Phase()!=BodyHolsterFixturePhase::FirePulse&&!d.probe.Failed())CHECK(d.Tick());CHECK(!d.probe.Failed());
        PresentationGap(d);auto input=d.original;input.predictedNs=d.f.s.hand.nowNs;const auto start=d.f.s.hand.nowNs;
        if(mode==1)++d.state->nativeOwner.weapon;if(mode==2)++d.state->right->token.id;if(mode==3)d.state->hand.focused=false;
        if(mode==4)d.state->selectedSlot.reset();if(mode==5)d.state->suppression=HolsterSuppressionReceipt{};
        d.probe.Prepare(input,d.f.s.nativeOwner,"XM8_sp_s",d.state,d.f.s.hand.observedNs,d.f.s.hand.deadlineNs,start);
        CHECK(input.hands[1].trigger==0);
        if(mode){CHECK(d.probe.Failed()&&!d.firePackets);continue;}
        CHECK(!d.probe.Failed()&&d.probe.NeedsNeutralFire());
        for(unsigned n=1;n<=20&&!d.probe.Failed();++n){auto current=std::make_shared<BodyHolsterProbeSample>(*d.state);current->sampledNs=current->hand.nowNs=current->hand.observedNs=start+n*10000000;current->hand.deadlineNs=current->hand.observedNs+100000000;current->right->deadlineNs=current->hand.deadlineNs;current->hand.sequence=current->input.generation=1000+n;
            d.probe.Observe(current,current->hand.nowNs);}
        CHECK(d.probe.Failed()&&d.probe.Sample().failure==24&&!d.firePackets);
    }return 0;
}
int ExactFireAmmoInterruptions(){
    const ReloadStateOwner owner{0x10000,0x20000,0x30000,0x40000,1,2,3};
    const auto sample=[&](std::uint64_t seq,std::int64_t at){return Bc2AmmoReserveLease{{owner,{0x51000,0x52000,0x53000},0x54000,0x55000,0x56000},seq,at,at+100000000,8,22,8,true,false,true};};
    constexpr auto now=1000000000ll;
    DiagnosticFireAmmo okay;auto first=sample(1,now);CHECK(!okay.Tick(first,owner,now,false));CHECK(!okay.Tick(first,owner,now+1,false));
    auto second=sample(2,now+10000000);CHECK(okay.Tick(second,owner,now+10000000,false));
    CHECK(!okay.Tick({},owner,now+11000000,false));auto fresh=sample(3,now+20000000);CHECK(okay.Tick(fresh,owner,now+20000000,false));
    auto inFlight=sample(4,now+30000000);inFlight.loaded=7;inFlight.allThreeIdle=false;CHECK(okay.Tick(inFlight,owner,now+30000000,true));
    for(unsigned bad=0;bad<6;++bad){DiagnosticFireAmmo d;CHECK(!d.Tick(first,owner,now,false));CHECK(d.Tick(second,owner,now+10000000,false));
        auto a=sample(3,now+20000000);auto current=owner;
        if(bad==0)++a.identity.serverItem;if(bad==1)++current.weapon;if(bad==2)++a.reserve;if(bad==3)--a.loaded;
        if(bad==4){a.sequence=second.sequence;++a.deadlineNs;a.observedNs=second.observedNs;}
        if(bad==5){CHECK(!d.Tick({},owner,now+11000000,false));CHECK(!d.Tick({},owner,now+211000000,false));CHECK(d.Failed());continue;}
        CHECK(!d.Tick(a,current,now+20000000,false)&&d.Failed());
    }
    DiagnosticFireAmmo insufficient;auto shortMag=first;shortMag.loaded=2;CHECK(!insufficient.Tick(shortMag,owner,now,false));
    DiagnosticFireAmmo nonidle;first.allThreeIdle=false;CHECK(!nonidle.Tick(first,owner,now,false));return 0;
}
}
int main(){if(MissingAmmoDuplicateCannotKeepCachedFire()||FirePresentationDefersNeutralWithoutReissuing()||FirePresentationGapBoundAndUnsafeOwners()||ExactFireAmmoInterruptions()||ConfiguredFireUsesActualAmmoGate()||DuplicateProofLossDoesNotReissueFire()||DuplicateBeforePulseStaysNeutral()||OnePostDrawOrdinaryPulse()||OriginalEarlyActionOrderCannotFire()||NeutralProfilesNeverPulse()||LostRestoredProofNeverPulses()||MissingCacheReceiptFails()||DuplicateDuringPulseDoesNotAddEdge()||DistinctPhysicalIdentityBaseline()||PhysicalIdentityMustStayExact()||RestoredActionBlockCannotPass()||WholeScopedXm8DiagnosticSequence()||ExplicitDiagnosticConfig()||Flags()||ActualCacheSuppression()||ChallengeFailuresRetire()||MissingAuthority()||WholeDiagnosticSequence()||NoChallengeNoSuccess()||ImmutableInputAndIdentity()||WrongWeaponAndWarmup())return 1;
    std::puts("26 body holster diagnostic groups passed; synthetic tests, no native acceptance");}





