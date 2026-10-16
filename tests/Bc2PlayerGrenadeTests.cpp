#include "Bc2PlayerGrenade.h"
#include "fvr/interaction/InputMenuOwnership.h"
#include "Test.h"
#include <memory>
#include <sstream>
#include <limits>
using namespace fvr;using namespace bc2;using namespace bc2::private_grenade;
using PlayerPhase=player_grenade::Phase;using player_grenade::Fault;
namespace {
struct Fixture {
    std::unique_ptr<player_grenade::Controller> probe=std::make_unique<player_grenade::Controller>();
    interaction::ControllerActions actions;Native n;Facts f;
    Fixture(){
        n.owner={101,102,103,104,1,2,3};n.originalSlot=1;n.targetSlot=8;n.carried.inventory=200;n.carried.switching=201;n.carried.count=9;
        n.carried.items[1]={104,301,302,0,1};n.carried.items[8]={108,303,304,6,8};
        n.original.client={n.owner,104,200,1};n.target.client={n.owner,108,200,8};
        n.original.client.config.weaponData=301;n.original.client.config.firingData=401;
        n.target.client.config.weaponData=303;n.target.client.config.firingData=403;
        for(auto* item:{&n.original,&n.target})for(unsigned i=0;i<3;++i){auto& state=i<2?item->client.branches[i]:item->server;
            state.address=(item==&n.original?500:600)+i;state.currentState=state.nextState=2;state.loaded=item==&n.original?22:1;state.reserve=item==&n.original?191:3;}
        for(auto& s:n.selectors){s.inventory=200;s.switching=201;s.current=1;s.previous=0;s.primary=1;}
        f.source.input.spaceGeneration=3;f.source.input.focused=f.source.input.headValid=true;
        for(auto& h:f.source.input.hands){h.gripTracked=h.aimTracked=true;h.active=interaction::Components;}
        f.source.admission={true,true,true,true,true,true,true,true,true,true,true,true,true,true,true,true,true,true,true,true};
        f.source.observedNs=1000000000;f.holsterPhase=1;f.visible=f.cacheQuiet=true;
        f.hand.owner={999,1,2,3};f.hand.focused=true;f.hand.tracked={true,true};f.handItem={302,2};
        f.right=interaction::HandClaim{};f.right->token={77,f.hand.owner,interaction::InteractionHand::Right,interaction::HandClaimKind::GunHold,f.handItem};
        f.display=BodyInventoryDisplay{};f.display->selectedOwner=n.owner;f.display->carried=n.carried;f.display->revision=9;f.display->count=1;
        f.display->slots[0]={{1,{104,5}},n.carried.items[1]};f.display->physicalSelected={104,5};
        for(unsigned i=0;i<5;++i)Step(false);
    }
    void Step(bool held,std::int64_t dt=20000000){
        ++f.source.input.generation;f.source.observedNs+=dt;f.source.nowNs=f.source.observedNs+1000;
        f.source.deadlineNs=f.source.observedNs+100000000;f.source.input.predictedNs=f.source.observedNs;
        f.source.input.menu={interaction::InputMenuOwner::Gameplay,f.source.input.generation,1,f.source.deadlineNs};
        f.source.input.hands[1].held=held?interaction::StickClick:0;
        f.source.semantic=actions.Update(f.source.input,{n.owner.soldier,n.owner.equipGeneration,true,true},f.source.nowNs);
        f.hand.sequence=f.source.input.generation;f.hand.observedNs=f.source.observedNs;f.hand.deadlineNs=f.source.deadlineNs;f.hand.nowNs=f.source.nowNs;
        if(f.right){f.right->inputSequence=f.hand.sequence;f.right->deadlineNs=f.hand.deadlineNs;}
        if(f.left){f.left->inputSequence=f.hand.sequence;f.left->deadlineNs=f.hand.deadlineNs;}
    }
    std::optional<Request> Issue(){Step(false);probe->Prepare(n,f);Step(true);probe->Prepare(n,f);
        for(unsigned i=0;i<27;++i){Step(true);probe->Prepare(n,f);}Step(false);return probe->Prepare(n,f);}
    void SelectGrenade(){n.owner.weapon=108;++n.owner.equipGeneration;for(unsigned i=0;i<2;++i){n.selectors[i].current=8;n.selectors[i].previous=1;}
        ++f.right->token.id;f.right->token.item={108,3};f.right->token.owner.equipGeneration=n.owner.equipGeneration;f.hand.owner=f.right->token.owner;
        f.visible=false;Step(false);probe->Observe(n,f);}
    void Return(){n.owner.weapon=104;++n.owner.equipGeneration;for(unsigned i=0;i<2;++i){n.selectors[i].current=1;n.selectors[i].previous=8;}
        for(unsigned i=0;i<3;++i)(i<2?n.target.client.branches[i]:n.target.server).reserve--;
        ++f.right->token.id;f.right->token.item={302,4};f.right->token.owner.equipGeneration=n.owner.equipGeneration;f.hand.owner=f.right->token.owner;
        f.display->selectedOwner=n.owner;f.handItem=f.right->token.item;f.visible=true;}
    void Finish(){for(unsigned i=0;i<4;++i){Step(false);probe->Observe(n,f);}}
    std::string Report(){std::ostringstream s;probe->Report(s);return s.str();}
};

int RepeatAndObservationGaps(){Fixture x;
    for(unsigned throwIndex=0;throwIndex<2;++throwIndex){const auto q=x.Issue();CHECK(q&&x.probe->CanStage(*q,x.n,x.f));x.probe->Commit(*q,x.f,true);
        CHECK(x.probe->Started());x.probe->Missing(x.f.source.nowNs);x.probe->Missing(x.f.source.nowNs);x.SelectGrenade();
        x.probe->Missing(x.f.source.nowNs);x.Return();for(unsigned k=0;k<3;++k)(k<2?x.n.original.client.branches[k]:x.n.original.server).phaseTimer=.015f;
        x.Finish();CHECK(x.probe->Completed()==throwIndex+1);CHECK(x.probe->Current()==PlayerPhase::AwaitNeutral);
        CHECK(x.probe->NativeEdges()==throwIndex+1);CHECK(!x.probe->Prepare(x.n,x.f)); // same generation is not neutral rearm
    }
    CHECK(x.probe->ObservationGaps()==4);CHECK(!x.probe->LastReturnHadNoReadGaps());CHECK(x.probe->Failure()==Fault::None);return 0;
}
int MenuEachStage(){for(unsigned stage=0;stage<4;++stage)for(unsigned kind=0;kind<4;++kind){Fixture x;
    const auto block=[&]{auto& m=x.f.source.input.menu;if(kind==0)m={};if(kind==1)m.owner=interaction::InputMenuOwner::Pending;
        if(kind==2)m.owner=interaction::InputMenuOwner::Menu;if(kind==3)m.deadlineNs=x.f.source.nowNs;};
    if(stage==0){x.Step(false);x.probe->Prepare(x.n,x.f);x.Step(true);block();CHECK(!x.probe->Prepare(x.n,x.f));}
    if(stage==1){x.Step(false);x.probe->Prepare(x.n,x.f);x.Step(true);x.probe->Prepare(x.n,x.f);
        for(unsigned i=0;i<27;++i){x.Step(true);x.probe->Prepare(x.n,x.f);}x.Step(false);block();CHECK(!x.probe->Prepare(x.n,x.f));}
    if(stage>=2){const auto q=x.Issue();CHECK(q);if(stage==2){block();CHECK(!x.probe->CanStage(*q,x.n,x.f));}
        else {CHECK(x.probe->CanStage(*q,x.n,x.f));block();x.probe->Commit(*q,x.f,true);CHECK(x.probe->Current()==PlayerPhase::Quarantined);CHECK(x.probe->NativeEdges()==1);}}
    if(stage<3)CHECK(x.probe->NativeEdges()==0);
}return 0;}
int ConflictsAndReconnect(){for(unsigned failure=0;failure<7;++failure){Fixture x;x.Step(false);x.probe->Prepare(x.n,x.f);x.Step(true);x.probe->Prepare(x.n,x.f);x.Step(true);
    if(failure==0)x.f.source.input.hands[0].held=interaction::StickClick;
    if(failure==1)x.f.source.input.hands[1].stickX=.5f;
    if(failure==2)x.f.source.input.hands[1].trigger=.5f;
    if(failure==3)x.f.source.input.focused=false;
    if(failure==4)++x.f.source.input.spaceGeneration;
    if(failure==5)--x.f.right->inputSequence;
    if(failure==6)x.f.source.admission.shellSettled=false;
    CHECK(!x.probe->Prepare(x.n,x.f));x.f.source.input.focused=true;x.f.source.input.spaceGeneration=3;x.f.source.input.hands[0].held=0;
    x.f.source.input.hands[1].trigger=x.f.source.input.hands[1].stickX=0;x.f.source.admission.shellSettled=true;
    for(unsigned i=0;i<35;++i){x.Step(true);CHECK(!x.probe->Prepare(x.n,x.f));}x.Step(false);CHECK(!x.probe->Prepare(x.n,x.f));CHECK(x.probe->NativeEdges()==0);
    CHECK(x.Issue());
}return 0;}
int QuarantineAndLifetime(){for(unsigned failure=0;failure<11;++failure){Fixture x;const auto q=x.Issue();CHECK(q&&x.probe->CanStage(*q,x.n,x.f));x.probe->Commit(*q,x.f,true);x.SelectGrenade();
    if(failure<6){const unsigned copy=failure%3;auto& g=copy<2?x.n.target.client.branches[copy]:x.n.target.server;g.reserve=failure<3?1:4;}
    if(failure==6)--x.n.original.server.loaded;
    if(failure==7)x.n.selectors[2].current=7;
    if(failure==8)++x.f.display->revision;
    if(failure==9)++x.n.target.client.config.firingData;
    if(failure==10){x.Return();x.f.right->token.kind=interaction::HandClaimKind::AmmoObject;}
    for(unsigned k=0;k<4;++k){x.Step(false);x.probe->Observe(x.n,x.f);}CHECK(x.probe->Current()==PlayerPhase::Quarantined);CHECK(!x.Issue());
    auto next=x.n.owner;++next.actorGeneration;++next.soldier;x.probe->Lifetime(next);CHECK(x.probe->Current()==PlayerPhase::Gesture);CHECK(x.probe->Failure()==Fault::None);
}return 0;}
int CompletedObservationEnds(){Fixture x;const auto q=x.Issue();CHECK(q&&x.probe->CanStage(*q,x.n,x.f));x.probe->Commit(*q,x.f,true);x.SelectGrenade();x.Return();x.Finish();CHECK(x.probe->Completed()==1);
    CHECK(x.probe->LastReturnHadNoReadGaps());
    ++x.f.display->revision;++x.f.display->slots[0].assignment.item.generation;x.f.display->physicalSelected.generation++;
    x.f.visible=false;x.f.holsterPhase=2;x.f.right.reset();x.Step(false);x.probe->Observe(x.n,x.f);CHECK(x.probe->Failure()==Fault::None);
    CHECK(x.probe->Current()==PlayerPhase::AwaitNeutral);return 0;
}
int CommitUnknownAndSuppressed(){for(unsigned i=0;i<2;++i){Fixture x;const auto q=x.Issue();CHECK(q&&x.probe->CanStage(*q,x.n,x.f));
    x.probe->Commit(*q,x.f,i?std::optional<bool>{false}:std::nullopt);CHECK(x.probe->NativeEdges()==0);
    CHECK(x.probe->Current()==(i?PlayerPhase::AwaitNeutral:PlayerPhase::Quarantined));CHECK(!x.probe->Prepare(x.n,x.f));
}return 0;}
int ReturnNeedsKnownCustody(){Fixture x;const auto q=x.Issue();CHECK(q&&x.probe->CanStage(*q,x.n,x.f));x.probe->Commit(*q,x.f,true);x.SelectGrenade();x.Return();
    const auto claim=x.f.right;x.f.right.reset();x.Finish();CHECK(x.probe->Current()==PlayerPhase::AwaitReturn);CHECK(x.probe->Completed()==0);
    x.f.right=claim;x.Finish();CHECK(x.probe->Completed()==1);return 0;
}
int UnappliedRequestAndNewLifetime(){Fixture x;const auto q=x.Issue();CHECK(q&&x.probe->CanStage(*q,x.n,x.f));
    x.probe->Missing(x.f.source.nowNs);CHECK(x.probe->Current()==PlayerPhase::AwaitNeutral);CHECK(!x.probe->Prepare(x.n,x.f));
    const auto retry=x.Issue();CHECK(retry&&x.probe->CanStage(*retry,x.n,x.f));x.probe->Commit(*retry,x.f,std::nullopt);
    CHECK(x.probe->Current()==PlayerPhase::Quarantined);auto next=x.n.owner;++next.space;x.probe->Lifetime(next);
    CHECK(x.probe->Current()==PlayerPhase::Quarantined); // Recenter cannot clear an uncertain edge.
    ++x.n.owner.soldier;++x.n.owner.actorGeneration;x.f.display->selectedOwner=x.n.owner;
    ++x.f.hand.owner.actorGeneration;x.f.right->token.owner=x.f.hand.owner;x.probe->Lifetime(x.n.owner);
    for(unsigned i=0;i<35;++i){x.Step(true);CHECK(!x.probe->Prepare(x.n,x.f));}
    x.Step(false);CHECK(!x.probe->Prepare(x.n,x.f));CHECK(x.Issue());CHECK(x.probe->NativeEdges()==0);return 0;
}
int UnobservedSelectionAndSupportedReturn(){for(unsigned scenario=0;scenario<3;++scenario){Fixture x;const auto q=x.Issue();CHECK(q&&x.probe->CanStage(*q,x.n,x.f));x.probe->Commit(*q,x.f,true);
    // No intermediate selection observation at all; stock final facts remain.
    x.probe->Missing(x.f.source.nowNs);x.Return();
    if(scenario==1){x.f.left=*x.f.right;x.f.left->token.id=987;x.f.left->token.hand=interaction::InteractionHand::Left;
        x.f.left->token.kind=interaction::HandClaimKind::WeaponSupport;x.f.left->token.prerequisiteClaim=x.f.right->token.id;x.f.source.input.hands[0].squeeze=.8f;}
    if(scenario==2)x.n.selectors[2].current=8;
    x.Finish();if(scenario==2){CHECK(x.probe->Completed()==0);x.n.selectors[2].current=1;x.Finish();}
    CHECK(x.probe->Completed()==1);CHECK(!x.probe->LastReturnHadNoReadGaps());CHECK(x.probe->Failure()==Fault::None);
    if(scenario==1){CHECK(!x.Issue());x.f.left.reset();x.f.source.input.hands[0].squeeze=0;CHECK(x.Issue());}
}return 0;}
int ReconnectedPacketNumbers(){Fixture x;x.Step(false);x.probe->Prepare(x.n,x.f);x.Step(true);x.probe->Prepare(x.n,x.f);
    x.probe->Missing(x.f.source.nowNs);const auto old=x.f.source.observedNs;x.f.source.input.generation=0;
    for(unsigned i=0;i<35;++i){x.Step(true);CHECK(!x.probe->Prepare(x.n,x.f));}
    x.Step(false);CHECK(x.f.source.observedNs>old);CHECK(!x.probe->Prepare(x.n,x.f));CHECK(x.Issue());CHECK(x.probe->NativeEdges()==0);return 0;
}

int DeathReadiness(){Fixture x;
    CHECK(!x.probe->SecondDeathReadiness(x.n,x.f));
    const auto q=x.Issue();CHECK(q&&x.probe->CanStage(*q,x.n,x.f));x.probe->Commit(*q,x.f,true);
    x.probe->Missing(x.f.source.nowNs);x.SelectGrenade();x.Return();x.Finish();
    CHECK(x.probe->Completed()==1&&!x.probe->SecondDeathReadiness(x.n,x.f));
    x.Step(false);CHECK(!x.probe->Prepare(x.n,x.f));
    auto w=x.probe->SecondDeathReadiness(x.n,x.f);CHECK(w&&w->edges==1&&w->completed==1&&w->attempts==1);
    CHECK(w->gaps==1&&!w->returnNoGaps&&w->neutralInput>w->returnInput&&w->neutralObservedNs>w->returnObservedNs);
    const auto okay=x.f;
    x.f.source.input.menu.owner=interaction::InputMenuOwner::Menu;CHECK(!x.probe->SecondDeathReadiness(x.n,x.f));x.f=okay;
    x.f.source.nowNs=x.f.source.deadlineNs;CHECK(!x.probe->SecondDeathReadiness(x.n,x.f));x.f=okay;
    x.f.right.reset();CHECK(!x.probe->SecondDeathReadiness(x.n,x.f));x.f=okay;
    auto selected=x.n;++selected.owner.weapon;CHECK(!x.probe->SecondDeathReadiness(selected,x.f));
    selected=x.n;++selected.owner.equipGeneration;CHECK(!x.probe->SecondDeathReadiness(selected,x.f));
    selected=x.n;++selected.owner.space;CHECK(!x.probe->SecondDeathReadiness(selected,x.f));
    CHECK(x.probe->SecondDeathReadiness(x.n,x.f)); // Observation rejection changes no controller policy.
    auto replacement=x.n;++replacement.owner.soldier;CHECK(!x.probe->SecondDeathReadiness(replacement,x.f));
    ++replacement.owner.actorGeneration;x.probe->Lifetime(replacement.owner);CHECK(!x.probe->SecondDeathReadiness(replacement,x.f));
    return 0;
}
int SourceReceipts(){Fixture x;
    const auto receipt=[&](const char* key,std::int64_t value){return x.Report().find(std::string("\"")+key+"\":"+std::to_string(value))!=std::string::npos;};
    CHECK(receipt("last_return_input",0)&&receipt("last_neutral_input",0));
    const auto q=x.Issue();CHECK(q&&x.probe->CanStage(*q,x.n,x.f));x.probe->Commit(*q,x.f,true);x.SelectGrenade();x.Return();
    // The existing action owner handoff takes a neutral update before fresh custody.
    for(unsigned i=0;i<4&&!x.probe->Completed();++i){x.Step(false);x.probe->Observe(x.n,x.f);}CHECK(x.probe->Completed()==1);
    const auto returnedInput=x.f.source.input.generation;const auto returnedAt=x.f.source.observedNs;
    CHECK(receipt("last_return_input",returnedInput)&&receipt("last_return_observed_ns",returnedAt));
    CHECK(receipt("last_neutral_input",0)&&receipt("last_neutral_observed_ns",0));
    CHECK(!x.probe->Prepare(x.n,x.f));CHECK(receipt("last_neutral_input",0)); // same original source cannot rearm
    // Packet numbering can restart after reconnect; held input still cannot rearm.
    x.f.source.input.generation=0;x.Step(true);CHECK(!x.probe->Prepare(x.n,x.f));CHECK(receipt("last_neutral_input",0));
    x.Step(false);CHECK(!x.probe->Prepare(x.n,x.f));x.Step(false);CHECK(!x.probe->Prepare(x.n,x.f));
    const auto neutralInput=x.f.source.input.generation;const auto neutralAt=x.f.source.observedNs;
    CHECK(neutralInput<returnedInput&&neutralAt>returnedAt);CHECK(x.probe->Current()==PlayerPhase::Gesture);
    CHECK(receipt("last_neutral_input",neutralInput)&&receipt("last_neutral_observed_ns",neutralAt));
    x.f.source.nowNs=x.f.source.deadlineNs;x.probe->Missing(x.f.source.nowNs);
    CHECK(x.probe->Current()==PlayerPhase::AwaitNeutral);
    CHECK(receipt("last_return_input",returnedInput)&&receipt("last_return_observed_ns",returnedAt));
    CHECK(receipt("last_neutral_input",neutralInput)&&receipt("last_neutral_observed_ns",neutralAt));
    auto next=x.n.owner;++next.actorGeneration;++next.soldier;x.probe->Lifetime(next);
    CHECK(receipt("last_return_input",0)&&receipt("last_return_observed_ns",0));
    CHECK(receipt("last_neutral_input",0)&&receipt("last_neutral_observed_ns",0));return 0;
}
}
int main(int argc,char** argv){if(argc==2&&std::string(argv[1])=="--death-readiness"){if(DeathReadiness())return 1;std::puts("Death readiness PASS: coherent return, fresh neutral/menu/custody, expiry, gap retained, owner replacement.");return 0;}if(argc==2&&std::string(argv[1])=="--receipts"){if(SourceReceipts())return 1;std::printf("Player grenade source receipt PASS: return, fresh rearm, reconnect, expiry retention and lifetime reset. No native actions.\n");return 0;}
if(DeathReadiness()||SourceReceipts()||RepeatAndObservationGaps()||MenuEachStage()||ConflictsAndReconnect()||QuarantineAndLifetime()||CompletedObservationEnds()||CommitUnknownAndSuppressed()||ReturnNeedsKnownCustody()||UnappliedRequestAndNewLifetime()||UnobservedSelectionAndSupportedReturn()||ReconnectedPacketNumbers())return 1;
    std::printf("Player grenade PASS: eleven focused policy groups; sizeof(Controller)=%zu. No native actions.\n",sizeof(player_grenade::Controller));return 0;}
