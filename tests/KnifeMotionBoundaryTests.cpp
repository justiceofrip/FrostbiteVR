#include "Test.h"
#include "Bc2KnifeAttackGate.h"
#include "Bc2KnifeMotionGeometry.h"
#include "Bc2OriginalFiringObserver.h"
#include "Bc2KnifeAcceptedOwnership.h"
#include "Bc2ChestKnifeRendering.h"
#include "Bc2ChestKnifeMissing.h"
#include "Bc2KnifeSoundObservation.h"
#include <cstdio>
#include <map>
#include <memory>
#include <sstream>
using namespace fvr::interaction;using namespace fvr::bc2;
#include "KnifeCustodyFixture.inc"
namespace pk=fvr::bc2::private_knife;
KnifeCommand Attack(){KnifeCommand c;c.kind=KnifeCommandKind::Attack;c.serial=1;c.knife={0x52000,3};
    c.source={{1,2,3,4},8,1000000000,1100000000,1000000000,true,{true,true}};
    c.hand=InteractionHand::Right;
    c.stroke=MeleeStrokeIntent{1,{1,c.source.owner,c.hand,HandClaimKind::GunHold,c.knife,{11,1},0},c.source.sequence,
        c.source.observedNs,c.source.deadlineNs,{0,0,0},{0,0,.08f},{0,0,1}};return c;}
struct Ownership {
    pk::Anchor anchor;std::map<unsigned,unsigned> words;unsigned fail=0,flip=0,reads=0;
    Ownership(){auto& o=anchor.original.client.owner;auto& v=anchor.original.links;
        o.player=0x10000;o.soldier=0x12000;o.weak=0x14000;anchor.original.client.soldierFlags=1;
        v.player=0x16000;v.soldier=0x18000;v.clientInventory=0x1a000;v.inventory=0x1c000;
        v.clientBegin=0x1e000;v.clientEnd=0x1e020;v.begin=0x20000;v.end=0x20020;
        anchor.knifeSlot=7;anchor.knifeWeapon=0x22000;v.items[7]=0x24000;anchor.knifeData=0x26000;
        anchor.knifeFiringData=0x28000;anchor.knifeFiring={0x2a000,0x2c000,0x2e000};
        words={{0x10c54,0x14000},{0x14000,0x12004},{0x10c68,0x12000},{0x12220,0x10000},
            {0x1224c,0x1a000},{0x12260,0x1e000},{0x12264,0x1e020},{0x16c3c,0x18000},{0x16c6c,0x18000},
            {0x18220,0x16000},{0x182b4,0x1c000},{0x182b8,0x20000},{0x182bc,0x20020},
            {0x1e01c,0x22000},{0x2001c,0x24000},{0x22004,0x26000},{0x24004,0x26000},
            {0x26098,0x28000},{0x2203c,0x2a000},{0x22040,0x2c000},{0x24010,0x2e000},
            {0x2a000,0x400000},{0x2a008,0x28000},{0x2c000,0x400000},{0x2c008,0x28000},
            {0x2e000,0x400000},{0x2e008,0x28000},{0x12114,1}};
    }
    ReloadStateMemory Memory(){return {this,[](void* p,unsigned at,void* out,std::size_t size)noexcept{
        auto& f=*static_cast<Ownership*>(p);if(at==f.fail||!f.words.contains(at)||(size!=1&&size!=4))return false;
        auto value=f.words[at];if(at==f.flip&&++f.reads==2)++value;std::memcpy(out,&value,size);return true;},nullptr};}
    auto Read(){return pk::ReadAcceptedKnifeOwnership(Memory(),anchor,0x400000);}
};
int MissingSelectionBoundary(){
    // Same coordinator chain as input_apply_rejected: accepted chooser first,
    // BeforePrepare delivers Accepted, then Missing supplies no hand evidence.
    for(const auto gap:{1*ms,5000*ms}){
        Fixture f;const auto draw=f.Draw();CHECK(draw&&f.knife.CanCommit(*draw,f.s,f.hands));
        pk::DrawReturnGate gate;CHECK(gate.Stage(*draw,f.s.source.nowNs));
        CHECK(gate.Choose(0,f.s.source.nowNs,true));const auto original=gate.Pending()->source;
        bool called=false,unknown=false;
        PrepareChestKnifeMissing(f.s.source.nowNs+gap,f.hands,f.knife,[&](const KnifeSample& missing,ChestKnifeCustody& policy)noexcept{
            called=true;unknown=missing.source.sequence==0&&missing.source.observedNs==0&&missing.source.deadlineNs==0;
            const auto accepted=gate.ResolveBeforeSource(missing.source.sequence,missing.source.observedNs,missing.source.nowNs);
            if(accepted)policy.Committed(*draw,*accepted);return true;
        });
        CHECK(called&&unknown&&f.knife.Phase()==KnifePhase::Drawing&&f.knife.NeedsInitialHeldPose()&&!f.knife.NeedsNativeCleanup());
        CHECK(gate.Pending()->source.observedNs==original.observedNs&&gate.Pending()->source.deadlineNs==original.deadlineNs);
        CHECK(!gate.Choose(0,f.s.source.nowNs+gap,true)); // Never replay accepted branch.
        CHECK(gate.Choose(1,f.s.source.nowNs+gap,false)); // Existing accepted native sibling only.
        // Actual draw receipt plus current selection are still necessary. No
        // old rendered contact or expiry bypass can create the first GunHold.
        ++f.s.source.owner.equipGeneration;f.s.knifeSelected=true;f.s.trackedKnifeOwned=false;
        CHECK(!f.Step(gap));CHECK(f.knife.Observe(f.Receipt(*draw)));CHECK(gate.Complete(draw->serial));
        CHECK(!f.Step(110*ms)&&!f.hands.Current(InteractionHand::Right)&&f.knife.NeedsInitialHeldPose());
        PrepareChestKnifeMissing(f.s.source.nowNs+1,f.hands,f.knife,[](const KnifeSample&,ChestKnifeCustody&)noexcept{return true;});
        CHECK(f.knife.Phase()==KnifePhase::Held&&f.knife.NeedsInitialHeldPose()&&!f.knife.NeedsNativeCleanup());
        f.s.trackedKnifeOwned=true;CHECK(!f.Step());
        const auto held=f.hands.Current(InteractionHand::Right);CHECK(held&&held->token.kind==HandClaimKind::GunHold);
        CHECK(held->inputSequence==f.s.source.sequence&&held->deadlineNs==f.s.source.deadlineNs);
        CHECK(ChestKnifePoseCustody(f.knife,f.hands,f.s.source)&&!f.knife.NeedsInitialHeldPose());
    }
    return 0;
}
int MissingNeverGrantsAttackOrReacquiresLostHold(){
    Fixture f;CHECK(f.Held());const auto held=f.hands.Current(InteractionHand::Right);CHECK(held);
    PrepareChestKnifeMissing(f.s.source.nowNs+5000*ms,f.hands,f.knife,[](const KnifeSample&,ChestKnifeCustody&)noexcept{return true;});
    CHECK(!f.knife.NeedsNativeCleanup()&&!f.knife.NeedsInitialHeldPose());
    CHECK(f.hands.Current(InteractionHand::Right)->deadlineNs==held->deadlineNs);
    CHECK(!f.Step(5000*ms)&&!f.hands.Current(InteractionHand::Right));
    CHECK(f.knife.NeedsNativeCleanup()&&!f.knife.NeedsInitialHeldPose());
    f.Grip(false);const auto back=f.Step();CHECK(back&&back->kind==KnifeCommandKind::Return);
    CHECK(f.knife.CanCommit(*back,f.s,f.hands));return 0;
}
int UnknownObservationVersusReleaseAndLifetime(){
    for(unsigned unknown=0;unknown<4;++unknown){
        Fixture f;const auto draw=f.Draw();CHECK(draw);f.knife.Committed(*draw,KnifeCommit::Accepted);
        CHECK(f.knife.Observe(f.Receipt(*draw)));++f.s.source.owner.equipGeneration;
        f.s.knifeSelected=f.s.trackedKnifeOwned=true;
        if(unknown==0)f.s.onFoot=false;if(unknown==1)f.s.fireSuppressed=false;
        if(unknown==2)f.s.controlsNeutral=false;if(unknown==3)f.s.source.tracked[1]=false;
        CHECK(!f.Step());CHECK(f.knife.NeedsInitialHeldPose()&&!f.knife.NeedsNativeCleanup());
        CHECK(!f.hands.Current(InteractionHand::Right));
        f.s.onFoot=f.s.fireSuppressed=f.s.controlsNeutral=f.s.source.tracked[1]=true;
        CHECK(!f.Step());CHECK(f.knife.Hold()&&f.knife.Hold()->kind==HandClaimKind::GunHold);
    }
    for(const bool nativeUnknown:{false,true}){Fixture f;const auto draw=f.Draw();CHECK(draw);f.knife.Committed(*draw,KnifeCommit::Accepted);
        f.Grip(false);f.s.onFoot=!nativeUnknown;CHECK(!f.Step());CHECK(f.knife.NeedsNativeCleanup()&&!f.knife.NeedsInitialHeldPose());
        f.s.onFoot=true;CHECK(f.knife.Observe(f.Receipt(*draw)));f.s.knifeSelected=true;
        const auto back=f.Step();CHECK(back&&back->kind==KnifeCommandKind::Return&&f.knife.CanCommit(*back,f.s,f.hands));}
    {Fixture f;const auto draw=f.Draw();CHECK(draw);f.knife.Committed(*draw,KnifeCommit::Accepted);
        ++f.s.source.owner.actorGeneration;CHECK(!f.Step());
        CHECK(f.knife.Phase()==KnifePhase::NeedNeutral&&!f.knife.Hold()&&!f.knife.NeedsInitialHeldPose());}
    {Fixture f;const auto draw=f.Draw();CHECK(draw);f.knife.Committed(*draw,KnifeCommit::Accepted);
        ++f.s.source.owner.space;CHECK(!f.Step());CHECK(f.knife.NeedsNativeCleanup()&&!f.knife.NeedsInitialHeldPose());}
    return 0;
}
int MissingCases(){CHECK(!MissingSelectionBoundary());CHECK(!MissingNeverGrantsAttackOrReacquiresLostHold());
    CHECK(!UnknownObservationVersusReleaseAndLifetime());
    std::puts("Three knife missing-observation groups PASS: actual boundary ordering, fresh first hold, expired hold/release/lifetime negatives.");return 0;}
int PredictionRoleCases(){
    // Retained knife336/native.json copy_rows347/348/349: the same 64-byte
    // +40 export is restored into an idle +3c object, changing 2/2 to5/6.
    // Current input roles must not require an extra Fire in that destination.
    {pk::KnifeAttackGate gate;const auto c=Attack();CHECK(gate.Stage(c,c.source.nowNs));
        CHECK(gate.Consume(1,c.source.nowNs,true));
        CHECK(gate.ResolveBeforeSource(9,1020000000,1020000000)==KnifeCommit::Accepted);
        gate.Observe(0,2,5,6);CHECK(gate.AttackMask()==1); // Old gate fails here.
        CHECK(gate.ConsumedMask()==2&&!gate.Consume(0,1600000000,false));
        gate.Observe(1,2,5,6);CHECK(gate.AttackMask()==3);
        CHECK(gate.Consume(2,1600000000,false));gate.Observe(2,2,5,6);
        CHECK(gate.ConsumedMask()==6&&gate.AttackMask()==7);
        CHECK(!gate.Complete(c.serial,true,false));CHECK(!gate.Complete(c.serial,false,true));
        CHECK(!gate.Complete(c.serial+1,true,true));
        CHECK(gate.Pending()->source.observedNs==c.source.observedNs&&gate.Pending()->source.deadlineNs==c.source.deadlineNs);
        CHECK(gate.Complete(c.serial,true,true));}
    // Uncorrelated state never fabricates source consumption/acknowledgement.
    {pk::KnifeAttackGate gate;auto c=Attack();CHECK(gate.Stage(c,c.source.nowNs));
        CHECK(!gate.ExpectedObservation(0,c.serial));
        gate.Observe(0,2,5,6);gate.Observe(1,2,5,6);CHECK(!gate.AttackMask());
        CHECK(!gate.Consume(0,c.source.nowNs,true));
        CHECK(!gate.Consume(1,c.source.deadlineNs,true));CHECK(!gate.Consume(1,c.source.nowNs,false));
        CHECK(gate.Consume(2,c.source.nowNs,true));gate.Observe(0,2,5,6);CHECK(!gate.AttackMask());
        CHECK(!gate.ExpectedObservation(0,c.serial)&&gate.ExpectedObservation(2,c.serial));
        CHECK(!gate.ExpectedObservation(2,c.serial+1)&&!gate.ExpectedObservation(3,c.serial));
        CHECK(!gate.Complete(c.serial,true,true,true));
        gate.LifetimeEnded();gate.Observe(0,5,6,2);CHECK(!gate.AttackMask()&&!gate.Consume(1,c.source.nowNs,true));
        CHECK(!gate.ExpectedObservation(2,c.serial));
        CHECK(!gate.Stage(c,c.source.nowNs));++c.serial;++c.stroke->hold.owner.actorGeneration;
        CHECK(!gate.Stage(c,c.source.nowNs));c.stroke->hold.owner=c.source.owner;++c.stroke->sourceSequence;
        CHECK(!gate.Stage(c,c.source.nowNs));}
    // The prediction observation is still necessary; two real copied calls
    // alone do not produce an observed-attack receipt. Cleanup stays unknown.
    {pk::KnifeAttackGate gate;const auto c=Attack();CHECK(gate.Stage(c,c.source.nowNs));
        CHECK(gate.Consume(1,c.source.nowNs,true));CHECK(gate.Consume(2,c.source.nowNs,true));
        CHECK(gate.ResolveBeforeSource(9,1020000000,1020000000)==KnifeCommit::Accepted);
        gate.Observe(1,2,5,6);gate.Observe(2,2,5,6);CHECK(gate.AttackMask()==6);
        CHECK(!gate.Complete(c.serial,true,true));CHECK(!gate.Complete(c.serial,true,false,true));
        CHECK(gate.Complete(c.serial,true,true,true));}
    // Actual native identity remains a separate prerequisite from branch role.
    {Ownership f;CHECK(f.Read()==pk::AcceptedKnifeOwnership::Same);
        f.words[0x22040]=0x30000;CHECK(f.Read()==pk::AcceptedKnifeOwnership::Foreign);
        f.words[0x22040]=0x2c000;f.fail=0x2e008;CHECK(f.Read()==pk::AcceptedKnifeOwnership::Unknown);}
    std::puts("Four knife prediction-role groups PASS: retained direct-copy transition, original authority, two returns/three observations, exact ownership.");return 0;
}

int RecognitionObservationCases(){
    auto diagnostic=std::make_unique<KnifeRecognitionDiagnostic>();diagnostic->Enable();Fixture f;f.knife.ObserveRecognition(diagnostic.get());
    CHECK(f.Held());CHECK(f.Rest());CHECK(f.knife.RecognitionReady(f.s.source));
    const auto source=f.s.source;auto changed=source;++changed.sequence;CHECK(!f.knife.RecognitionReady(changed));
    changed=source;++changed.deadlineNs;CHECK(!f.knife.RecognitionReady(changed));
    changed=source;changed.nowNs=changed.deadlineNs;CHECK(!f.knife.RecognitionReady(changed));
    f.s.nativeAttackReady=false;CHECK(!f.Step());CHECK(!f.knife.RecognitionReady(f.s.source));
    f.s.nativeAttackReady=true;CHECK(!f.Step());CHECK(!f.knife.RecognitionReady(f.s.source));
    CHECK(f.Rest());CHECK(f.knife.RecognitionReady(f.s.source));
    f.s.onFoot=false;CHECK(!f.Step());CHECK(!f.knife.RecognitionReady(f.s.source));f.s.onFoot=true;
    CHECK(f.Rest());const auto command=f.Stab();CHECK(command&&command->kind==KnifeCommandKind::Attack);
    CHECK(command->source.observedNs==f.s.source.observedNs&&command->source.deadlineNs==f.s.source.deadlineNs);
    std::ostringstream out;diagnostic->Report(out);const auto report=out.str();
    CHECK(report.find("\"gate\":2")!=std::string::npos); // Existing native/on-foot admission miss.
    CHECK(report.find("\"decision\":1")!=std::string::npos); // Actual recognizer invalidated by unavailable attack.
    CHECK(report.find("\"invalid\":2")!=std::string::npos); // First evaluated failure is attackAvailable.
    CHECK(report.find("\"decision\":16")!=std::string::npos); // Actual measured stroke emits intent.
    CHECK(report.find("\"command\":2")!=std::string::npos&&report.find("\"metrics\":1")!=std::string::npos);
    CHECK(report.find("\"observed_ns\":"+std::to_string(command->source.observedNs))!=std::string::npos);
    CHECK(diagnostic->Kept()>5&&!diagnostic->Dropped());
    // Small moving settle polls must not exhaust the ledger before the actual stroke.
    auto settling=std::make_unique<KnifeRecognitionDiagnostic>();settling->Enable();
    KnifeRecognitionRecord quiet;quiet.source=source;quiet.phaseBefore=quiet.phaseAfter=4;
    quiet.gate=KnifeRecognitionGate::Motion;quiet.motion.metrics=true;quiet.motion.distance=.00001f;
    quiet.motion.before=quiet.motion.after=MeleeStrokePhase::Settling;quiet.motion.decision=MeleeStrokeDecision::Settling;
    for(unsigned n=0;n<KnifeRecognitionDiagnostic::Capacity+32;++n){++quiet.source.sequence;++quiet.source.observedNs;settling->Record(quiet);}
    CHECK(settling->Kept()==1&&settling->Dropped()==0);
    quiet.motion.decision=MeleeStrokeDecision::SampleGap;settling->Record(quiet);CHECK(settling->Kept()==2);
    quiet.motion.before=quiet.motion.after=MeleeStrokePhase::Travelling;quiet.motion.decision=MeleeStrokeDecision::Travel;
    for(unsigned n=0;n<3;++n){++quiet.source.sequence;quiet.motion.travel+=.01f;settling->Record(quiet);}
    CHECK(settling->Kept()==5&&settling->Dropped()==0);
    quiet.motion.decision=MeleeStrokeDecision::Intent;quiet.motion.after=MeleeStrokePhase::Spent;settling->Record(quiet);
    CHECK(settling->Kept()==6&&settling->Dropped()==0);
    auto bounded=std::make_unique<KnifeRecognitionDiagnostic>();bounded->Enable();KnifeRecognitionRecord row;
    row.phaseBefore=row.phaseAfter=4;row.gate=KnifeRecognitionGate::NativeGate;row.source=source;
    bounded->Record(row);bounded->Record(row);CHECK(bounded->Kept()==1); // Identical unavailable polls collapse.
    for(unsigned n=1;n<KnifeRecognitionDiagnostic::Capacity+2;++n){row.prerequisites=n;bounded->Record(row);}
    CHECK(bounded->Kept()==KnifeRecognitionDiagnostic::Capacity&&bounded->Seen()==KnifeRecognitionDiagnostic::Capacity+2&&bounded->Dropped()==2);
    std::ostringstream full;bounded->Report(full);CHECK(full.str().find("\"deadline_ns\":"+std::to_string(source.deadlineNs))!=std::string::npos);
    std::puts("Knife recognition diagnostics PASS: exact current readiness, native/recognizer reset distinction, actual metrics/intent, bounded immutable rows.");return 0;
}
int KnifeAudioObservationCases(){
    using namespace fvr::bc2::selectedKnife;
    KnifeSoundDispatchContext dispatch;
    const KnifeSoundDispatchContext acquisition{KnifeSoundDispatchPhase::Acquisition,1,8,2,3,0x12000,0x13000,0x14000};
    auto attack=acquisition;attack.phase=KnifeSoundDispatchPhase::Attack;attack.serial=2;
    const auto matches=[&](const KnifeSoundDispatchContext& context){
        return KnifeSoundDispatchOwnerMatches(context,0x12000,0x13000,0x14000,2,3);};
    CHECK(!matches(dispatch));
    {KnifeSoundDispatchScope outer(dispatch,true);outer.Bind(acquisition);CHECK(matches(dispatch));
        {KnifeSoundDispatchScope unadmitted(dispatch,true);CHECK(dispatch.phase==KnifeSoundDispatchPhase::None&&!matches(dispatch));}
        CHECK(dispatch==acquisition);
        {KnifeSoundDispatchScope nested(dispatch,true);nested.Bind(attack);CHECK(dispatch==attack&&matches(dispatch));
            nested.Clear();CHECK(!matches(dispatch));}
        CHECK(dispatch==acquisition);
        {KnifeSoundDispatchScope disabled(dispatch,false);disabled.Bind(attack);disabled.Clear();CHECK(dispatch==acquisition);}
        CHECK(dispatch==acquisition);outer.Clear();CHECK(!matches(dispatch));}
    CHECK(dispatch==KnifeSoundDispatchContext{});
    for(unsigned fault=0;fault<10;++fault){auto invalid=attack;
        if(fault==0)invalid.phase=KnifeSoundDispatchPhase::None;if(fault==1)invalid.serial=0;
        if(fault==2)invalid.input=0;if(fault==3)++invalid.actorGeneration;if(fault==4)++invalid.itemGeneration;
        if(fault==5)++invalid.owner;if(fault==6)++invalid.weapon;if(fault==7)++invalid.effects;
        if(fault==8)invalid.phase=static_cast<KnifeSoundDispatchPhase>(99);if(fault==9)invalid.owner=0;
        CHECK(!matches(invalid));}
    CHECK(MatchKnifeSound(KnifeFireSoundGuid)==KnifeSoundKind::Attack);
    CHECK(MatchKnifeSound(KnifeFireKillSoundGuid)==KnifeSoundKind::AttackKill);
    for(unsigned i=0;i<16;++i){auto wrong=KnifeFireSoundGuid;wrong[i]^=1;
        CHECK(MatchKnifeSound(wrong)==KnifeSoundKind::Unknown);}
    CHECK(KnifeSoundLocalBoundary(true,0x12000,0x12000,1,1,true));
    CHECK(!KnifeSoundLocalBoundary(true,0x13000,0x12000,1,1,true));
    CHECK(!KnifeSoundLocalBoundary(true,0x12000,0x12000,0,1,true));
    CHECK(!KnifeSoundLocalBoundary(true,0x12000,0x12000,1,0,true));
    CHECK(!KnifeSoundLocalBoundary(true,0x12000,0x12000,1,1,false));
    CHECK(!KnifeSoundLocalBoundary(false,0x12000,0x12000,1,1,true));
    KnifeSoundObservation row;row.kind=KnifeSoundKind::AttackKill;row.ownerVerified=true;row.observedNs=2000000000;
    KnifeSoundCandidate recent{1,2,3,1000000000,1100000000,1200000000,1600000000,0,true};
    CHECK(AddKnifeSoundCandidate(row,recent));CHECK(!AddKnifeSoundCandidate(row,recent));
    auto newer=recent;newer.serial=2;newer.completed=false;newer.publishedNs=1900000000;
    CHECK(AddKnifeSoundCandidate(row,newer)&&row.candidateCount==2); // ambiguity retained
    row.originalScope=attack;row.originalScopeOwnerMatched=matches(attack);
    CHECK(row.originalScopeOwnerMatched&&row.candidateCount==2); // A scope does not erase temporal ambiguity.
    CHECK(row.candidates[0].damageCalls==0); // FireKill cannot invent a native hit.
    auto third=newer;third.serial=3;CHECK(!AddKnifeSoundCandidate(row,third));
    auto stale=row;stale.candidateCount=0;stale.observedNs=recent.publishedNs+2000000001;
    CHECK(!AddKnifeSoundCandidate(stale,recent));stale.observedNs=recent.publishedNs-1;
    CHECK(!AddKnifeSoundCandidate(stale,recent));stale=row;stale.candidateCount=0;stale.ownerVerified=false;
    CHECK(!AddKnifeSoundCandidate(stale,recent));
    KnifeSoundJournal<4> journal;CHECK(journal.Observe(row));row.start=true;
    CHECK(journal.Rows()[0].originalScope==attack&&journal.Rows()[0].originalScopeOwnerMatched);
    for(unsigned n=0;n<20;++n){++row.observedNs;CHECK(journal.Observe(row));}
    CHECK(journal.Total()==21&&journal.Rows()[0].ordinal==1&&journal.Rows()[1].ordinal==2);
    CHECK(journal.Rows()[2].ordinal==21&&journal.Rows()[3].ordinal==20);
    row.ownerVerified=false;CHECK(!journal.Observe(row)&&journal.Total()==21);
    row.ownerVerified=true;row.kind=KnifeSoundKind::Unknown;CHECK(!journal.Observe(row));
    return 0;
}
int main(int argc,char** argv){
    if(argc==2&&std::string_view(argv[1])=="--recognition-observation")return RecognitionObservationCases();
    if(argc==2&&std::string_view(argv[1])=="--prediction")return PredictionRoleCases();
    if(argc==2&&std::string_view(argv[1])=="--missing")return MissingCases();
    CHECK(!PredictionRoleCases());
    CHECK(!MissingCases());
    {pk::KnifeAttackGate gate;auto c=Attack();CHECK(gate.Stage(c,c.source.nowNs));
        CHECK(!gate.Consume(1,c.source.nowNs,false));CHECK(!gate.Consume(0,c.source.nowNs,true));CHECK(!gate.Consume(3,c.source.nowNs,true));
        CHECK(gate.Consume(1,c.source.nowNs,true));CHECK(!gate.Consume(1,c.source.nowNs,true));
        CHECK(gate.ResolveBeforeSource(9,1020000000,1020000000)==KnifeCommit::Accepted);
        CHECK(!gate.Consume(0,1600000000,false));CHECK(gate.Consume(2,1600000000,false));
        gate.Observe(0,2,5,5);gate.Observe(1,5,6,6);CHECK(!gate.Complete(c.serial,true,true));
        gate.Observe(2,6,2,2);CHECK(!gate.Complete(c.serial,false,true));
        CHECK(!gate.Complete(c.serial,true,false));CHECK(gate.Complete(c.serial,true,true));
        CHECK(!gate.Stage(c,c.source.nowNs));CHECK(!gate.Consume(0,c.source.nowNs,true));}
    {pk::KnifeAttackGate gate;auto c=Attack();CHECK(gate.Stage(c,c.source.nowNs));
        CHECK(!gate.Consume(1,c.source.deadlineNs,true));
        CHECK(gate.ResolveBeforeSource(9,1020000000,1020000000)==KnifeCommit::RejectedNoEffect);
        CHECK(!gate.Consume(0,c.source.nowNs,true));++c.serial;CHECK(gate.Stage(c,c.source.nowNs));
        gate.LifetimeEnded();CHECK(!gate.Consume(1,c.source.nowNs,true));CHECK(!gate.Stage(c,c.source.nowNs));
        ++c.serial;++c.stroke->sourceSequence;CHECK(!gate.Stage(c,c.source.nowNs));}
    {std::array<std::byte,0x30> original{};const float dt=.016f,multiplier=1.25f;
        std::memcpy(original.data()+0x18,&dt,4);std::memcpy(original.data()+0x20,&multiplier,4);
        original[0x24]=std::byte{1};original[3]=std::byte{0xa5};const auto saved=original;
        const auto copy=pk::CopyKnifeAttackInput(original);CHECK(copy&&original==saved);
        for(unsigned n=0;n<original.size();++n)CHECK((*copy)[n]==(n==0x2c?std::byte{1}:original[n]));
        original[0x2c]=std::byte{1};CHECK(!pk::CopyKnifeAttackInput(original));
        original=saved;std::fill(original.begin()+0x18,original.begin()+0x1c,std::byte{0});CHECK(!pk::CopyKnifeAttackInput(original));}
    {Ownership f;CHECK(f.Read()==pk::AcceptedKnifeOwnership::Same);
        // No selector/timer fields exist in this fixture: accepted ownership
        // neither depends on convergence nor manufactures an observation.
        f.fail=0x2c008;CHECK(f.Read()==pk::AcceptedKnifeOwnership::Unknown);f.fail=0;
        f.flip=0x2203c;CHECK(f.Read()==pk::AcceptedKnifeOwnership::Unknown);f.flip=0;
        f.words[0x2203c]=0x30000;CHECK(f.Read()==pk::AcceptedKnifeOwnership::Foreign);
        f.words[0x2203c]=0x2a000;f.words[0x12114]=0x19;CHECK(f.Read()==pk::AcceptedKnifeOwnership::Same);
        f.words[0x12114]=0x18;CHECK(f.Read()==pk::AcceptedKnifeOwnership::Foreign);}
    {for(auto at:{0x14000u,0x2001cu,0x2e000u,0x2e008u}){Ownership f;++f.words[at];CHECK(f.Read()==pk::AcceptedKnifeOwnership::Foreign);}}
    {fvr::math::Pose grip;auto calibration=*InverseRigid(*fvr::math::MakeLhViewFromOpenXRPose(grip));
        const auto first=pk::KnifeMotionFromGrip(grip,calibration);CHECK(first);
        grip.position={.2f,.1f,-.3f};const auto next=pk::KnifeMotionFromGrip(grip,calibration);CHECK(next);
        CHECK(std::abs(next->tip.x-first->tip.x-.2f)<.0001f);
        CHECK(std::abs(next->tip.y-first->tip.y-.1f)<.0001f);
        CHECK(std::abs(next->tip.z-first->tip.z-.3f)<.0001f);
        CHECK(next->direction==first->direction);grip.orientation.w=0;CHECK(!pk::KnifeMotionFromGrip(grip,calibration));
        InputFrame input;input.hands[1].gripTracked=true;
        auto body=calibration;body.values[3]={1000,50,-2000,1};
        calibration.values[0]={0,0,1,0};calibration.values[2]={-1,0,0,0};
        const auto tracked=Multiply(calibration,body);const auto recovered=pk::KnifeWristCalibration(input,body,tracked);
        CHECK(recovered&&recovered->values==calibration.values);
        input.head.position={1,2,3};input.head.orientation={0,.70710678f,0,.70710678f};
        CHECK(pk::KnifeWristCalibration(input,body,tracked)->values==calibration.values);
        const auto directed=pk::KnifeMotionFromGrip(input.hands[1].grip,*recovered);CHECK(directed&&directed->direction!=first->direction);}
    {struct Count{unsigned copies=0,leaves=0;};Count count;
        OriginalFiringObserver observer{&count,[](void*,const OriginalFiringUpdate&)noexcept{return OriginalFiringToken{};},
            [](void* p,const OriginalFiringUpdate&,const OriginalFiringToken&)noexcept{++static_cast<Count*>(p)->leaves;},
            [](void* p,const OriginalFiringUpdate&,OriginalFiringToken&)noexcept->std::optional<std::array<std::byte,0x30>>{
                ++static_cast<Count*>(p)->copies;std::array<std::byte,0x30> out{};out[0x2c]=std::byte{1};return out;}};
        {OriginalFiringScope scope(observer,{});const auto copy=scope.InputCopy();CHECK(copy&&(*copy)[0x2c]==std::byte{1});
            CHECK(!scope.InputCopy());}CHECK(count.copies==1&&count.leaves==1);
        {OriginalFiringScope scope({},{});CHECK(!scope.InputCopy());}}
    {Fixture f;const auto draw=f.Draw();CHECK(draw);f.knife.Committed(*draw,KnifeCommit::Accepted);
        CHECK(ChestKnifePoseCustody(f.knife,f.hands,f.s.source));CHECK(f.knife.Observe(f.Receipt(*draw)));
        CHECK(ChestKnifePoseCustody(f.knife,f.hands,f.s.source));f.s.knifeSelected=f.s.trackedKnifeOwned=true;
        ++f.s.source.owner.equipGeneration;CHECK(!f.Step());CHECK(!f.knife.NeedsInitialHeldPose());
        CHECK(ChestKnifePoseCustody(f.knife,f.hands,f.s.source));
        f.hands.Update({});CHECK(!ChestKnifePoseCustody(f.knife,f.hands,f.s.source));
        CHECK(!f.Step());CHECK(!ChestKnifePoseCustody(f.knife,f.hands,f.s.source));
        CHECK(!f.hands.Current(InteractionHand::Right));}
    {Fixture f;CHECK(f.Held());CHECK(f.Rest());const auto attack=f.Stab();CHECK(attack);
        f.knife.Committed(*attack,KnifeCommit::Accepted);auto quiet=f.Receipt(*attack);
        quiet.attackObserved=false;quiet.attackQuiescentOnly=true;CHECK(f.knife.Observe(quiet));
        CHECK(f.knife.NeedsNativeCleanup()&&!f.knife.NeedsInitialHeldPose());CHECK(f.Rest());CHECK(!f.Stab());
        f.Grip(false);const auto back=f.Step();CHECK(back&&back->kind==KnifeCommandKind::Return);
        CHECK(f.knife.CanCommit(*back,f.s,f.hands));
        pk::KnifeAttackGate gate;const auto c=Attack();CHECK(gate.Stage(c,c.source.nowNs));
        for(unsigned branch=1;branch<3;++branch)CHECK(gate.Consume(branch,c.source.nowNs,true));
        CHECK(gate.ResolveBeforeSource(9,1020000000,1020000000)==KnifeCommit::Accepted);
        CHECK(!gate.Complete(c.serial,true,true));CHECK(!gate.Complete(c.serial,true,false,true));
        CHECK(gate.Complete(c.serial,true,true,true));
    }
    CHECK(!RecognitionObservationCases());
    CHECK(!KnifeAudioObservationCases());
    std::puts("Ten focused knife motion/boundary/accepted-ownership/display groups PASS.");
}
