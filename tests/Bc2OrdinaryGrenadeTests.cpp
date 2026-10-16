#include "Bc2OrdinaryGrenadeProbe.h"
#include "OrdinaryGrenadeProbeInput.h"
#include "SceneRecoveryProbe.h"
#include "Test.h"
#include <sstream>
#include <memory>
#include <limits>
using namespace fvr;
using namespace bc2;
using namespace bc2::private_grenade;
namespace {
struct Fixture {
    std::unique_ptr<Probe> probe=std::make_unique<Probe>();
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
        f.source.input.hands[1].held=held?interaction::StickClick:0;
        f.source.semantic=actions.Update(f.source.input,{n.owner.soldier,n.owner.equipGeneration,true,true},f.source.nowNs);
        f.hand.sequence=f.source.input.generation;f.hand.observedNs=f.source.observedNs;f.hand.deadlineNs=f.source.deadlineNs;f.hand.nowNs=f.source.nowNs;
        if(f.right){f.right->inputSequence=f.hand.sequence;f.right->deadlineNs=f.hand.deadlineNs;}
    }
    std::optional<Request> Issue(){Step(false);probe->Prepare(n,f);Step(true);probe->Prepare(n,f);
        for(unsigned i=0;i<27;++i){Step(true);probe->Prepare(n,f);}Step(false);return probe->Prepare(n,f);}
    void SelectGrenade(){n.owner.weapon=108;++n.owner.equipGeneration;for(unsigned i=0;i<2;++i){n.selectors[i].current=8;n.selectors[i].previous=1;}
        f.right->token.id=78;f.right->token.item={108,3};f.right->token.owner.equipGeneration=3;f.hand.owner=f.right->token.owner;
        f.visible=false;Step(false);probe->Observe(n,f);}
    void Return(){n.owner.weapon=104;++n.owner.equipGeneration;for(unsigned i=0;i<2;++i){n.selectors[i].current=1;n.selectors[i].previous=8;}
        for(unsigned i=0;i<3;++i)(i<2?n.target.client.branches[i]:n.target.server).reserve=2;
        f.right->token.id=79;f.right->token.item={302,4};f.right->token.owner.equipGeneration=4;f.hand.owner=f.right->token.owner;
        f.display->selectedOwner=n.owner;f.handItem=f.right->token.item;f.visible=true;}
    void Finish(){for(unsigned i=0;i<410;++i){Step(false);probe->Observe(n,f);
        auto post=f;post.visible=false;post.custodyObserved=false;probe->Observe(n,post);}}
    std::string Report(){std::ostringstream s;probe->Report(s);return s.str();}
};
int OneReturn(){Fixture x;const auto req=x.Issue();CHECK(req&&x.probe->CanStage(*req,x.n,x.f));x.probe->Commit(*req,x.f,true);
    x.SelectGrenade();x.Return();x.Finish();const auto result=x.Report();if(result.find("\"passed\":1")==std::string::npos)std::fprintf(stderr,"%s\n",result.c_str());CHECK(result.find("\"passed\":1")!=std::string::npos);
    CHECK(result.find("\"observed_native_edges\":1")!=std::string::npos);CHECK(result.find("\"dropped\":0")!=std::string::npos);
    CHECK(!x.Issue());return 0;}
int RejectedReturnEvidence(){for(unsigned failure=0;failure<7;++failure){Fixture x;const auto req=x.Issue();CHECK(req&&x.probe->CanStage(*req,x.n,x.f));x.probe->Commit(*req,x.f,true);
    if(failure!=0)x.SelectGrenade();x.Return();
    if(failure==1)x.f.right->token.id=77;
    if(failure==2)++x.f.display->revision;
    if(failure==3)--x.n.original.client.branches[0].loaded;
    if(failure==4)x.n.target.server.reserve=3;
    if(failure==5)x.f.cacheQuiet=false;
    if(failure==6)x.probe->Missing(x.f.source.nowNs);
    x.Finish();CHECK(x.Report().find("\"passed\":0")!=std::string::npos);}return 0;}
int TargetAmmoExcursions(){for(unsigned copy=0;copy<3;++copy)for(unsigned gain=0;gain<2;++gain){
    Fixture x;const auto req=x.Issue();CHECK(req&&x.probe->CanStage(*req,x.n,x.f));x.probe->Commit(*req,x.f,true);x.SelectGrenade();
    auto& ammo=copy<2?x.n.target.client.branches[copy]:x.n.target.server;
    ammo.reserve=gain?4:1; // Baseline total4: neither5 nor2 is a one-grenade result.
    x.Step(false);x.probe->Observe(x.n,x.f);ammo.reserve=3;x.Return();x.Finish();
    CHECK(x.Report().find("\"failed\":1")!=std::string::npos);CHECK(x.Report().find("\"passed\":0")!=std::string::npos);
    }return 0;}
int InitialAmmoConvergence(){Fixture x;const auto req=x.Issue();CHECK(req&&x.probe->CanStage(*req,x.n,x.f));x.probe->Commit(*req,x.f,true);x.SelectGrenade();
    // The three replicas may consume the one grenade in different observations.
    for(unsigned copy=0;copy<3;++copy){(copy<2?x.n.target.client.branches[copy]:x.n.target.server).reserve=2;
        x.Step(false);x.probe->Observe(x.n,x.f);}
    x.Return();x.Finish();CHECK(x.Report().find("\"passed\":1")!=std::string::npos);return 0;}
int SettledReturnLoss(){for(unsigned failure=0;failure<14;++failure){Fixture x;const auto req=x.Issue();CHECK(req&&x.probe->CanStage(*req,x.n,x.f));
    x.probe->Commit(*req,x.f,true);x.SelectGrenade();x.Return();x.Step(false);x.probe->Observe(x.n,x.f);
    CHECK(x.Report().find("\"return_settled\":1")!=std::string::npos);CHECK(x.Report().find("\"failed\":0")!=std::string::npos);
    const auto savedNative=x.n;const auto savedFacts=x.f;
    if(failure==0)x.f.right.reset();
    if(failure==1)++x.f.right->token.id; // Fresh replacement cannot hide lost custody.
    if(failure==2)++x.f.display->revision;
    if(failure==3)++x.f.display->slots[0].assignment.item.generation;
    if(failure==4)++x.f.display->physicalSelected.generation;
    if(failure==5)x.f.visible=false;
    if(failure==6)x.f.left=x.f.right;
    if(failure==7)x.f.hand.tracked[1]=false;
    if(failure==8)x.f.holsterPhase=2;
    if(failure==9){x.n.owner.weapon=108;for(unsigned k=0;k<2;++k){x.n.selectors[k].current=8;x.n.selectors[k].previous=1;}}
    if(failure==10)++x.n.selectors[0].previous;
    if(failure==11)++x.n.selectors[2].current;
    if(failure==12)x.n.target.server.reserve=3;
    if(failure==13)++x.n.owner.equipGeneration;
    x.Step(false);auto bad=x.f;
    // Native-only post-Update observations must catch selector/owner/ammo loss,
    // without treating the absence of a custody publication as a custody loss.
    if(failure>=9)bad.custodyObserved=false;
    x.probe->Observe(x.n,bad);x.n=savedNative;x.f=savedFacts;x.Step(false);x.Finish();
    CHECK(x.Report().find("\"failed\":1")!=std::string::npos);CHECK(x.Report().find("\"passed\":0")!=std::string::npos);
    }return 0;}
int FinalReadChanges(){Fixture x;const auto req=x.Issue();CHECK(req);++x.n.target.client.config.firingData;CHECK(!x.probe->CanStage(*req,x.n,x.f));
    CHECK(x.Report().find("\"observed_native_edges\":0")!=std::string::npos);return 0;}
int Receiver(){runtime::TrackingFrame t;t.spaceGeneration=1;t.focused=t.headValid=true;bool held=false;unsigned presses=0,releases=0;
    for(unsigned ms=0;ms<19000;ms+=10){auto input=probe::NeutralSceneInput(t,ms+1);probe::OrdinaryGrenadeInput(input,ms);
        CHECK(probe::OrdinaryGrenadeControls(input,ms));CHECK(input.hands[0].trigger==0&&input.hands[1].trigger==0);
        const bool now=bool(input.hands[1].held);presses+=now&&!held;releases+=!now&&held;held=now;}
    CHECK(presses==1&&releases==1);CHECK(!probe::OrdinaryGrenadeOption(L"--controls-death-probe"));CHECK(!probe::OrdinaryGrenadeOption(L"--body-look-down"));return 0;}
int OwnerRearmsHeldTrigger(){interaction::ControllerActions a;interaction::InputFrame i;i.spaceGeneration=1;i.focused=i.headValid=true;
    for(auto& h:i.hands){h.gripTracked=h.aimTracked=true;h.active=interaction::Components;}
    const auto step=[&](unsigned epoch,float trigger){++i.generation;i.predictedNs+=20000000;i.hands[1].trigger=trigger;return a.Update(i,{10,epoch,true,true},i.predictedNs);};
    for(unsigned n=0;n<4;++n)step(1,0);CHECK(step(1,1).held&interaction::Fire);
    for(unsigned n=0;n<4;++n)CHECK(!(step(2,1).held&interaction::Fire));
    for(unsigned n=0;n<4;++n)CHECK(!(step(3,1).held&interaction::Fire));
    step(3,0);step(3,0);CHECK(step(3,1).held&interaction::Fire);return 0;}
int AdmissionMaskMapping(){
    // Check the public journal bit order independently of All()/FailureMask().
    constexpr bool Admission::* fields[]{&Admission::onFoot,&Admission::nativePlaying,&Admission::bindingVerified,&Admission::ownerCurrent,
        &Admission::routeCurrent,&Admission::configCurrent,&Admission::ammoAvailable,&Admission::visibleHeld,&Admission::nativeGrenadeNeutral,
        &Admission::nativeFireNeutral,&Admission::currentGunHold,&Admission::leftFree,&Admission::bodySettled,&Admission::shellSettled,
        &Admission::magazineSettled,&Admission::pumpSettled,&Admission::boltSettled,&Admission::sightSettled,&Admission::menuOwnershipKnown,&Admission::menuClear};
    constexpr const char* names[]{"on_foot","native_playing","binding_verified","owner_current","route_current","config_current","ammo_available",
        "visible_held","native_grenade_neutral","native_fire_neutral","current_gun_hold","left_free","body_settled","shell_settled","magazine_settled",
        "pump_settled","bolt_settled","sight_settled","menu_ownership_known","menu_clear"};
    Admission a;CHECK(a.FailureMask()==0xfffff);CHECK(!a.All());
    for(auto field:fields)a.*field=true;
    CHECK(a.All()&&a.FailureMask()==0);
    for(unsigned i=0;i<20;++i){a.*fields[i]=false;CHECK(a.FailureMask()==(1u<<i));CHECK(!a.All());
        CHECK(std::string(Admission::Names[i])==names[i]);a.*fields[i]=true;}
    a.ownerCurrent=a.currentGunHold=a.menuClear=false;CHECK(a.FailureMask()==((1u<<3)|(1u<<10)|(1u<<19)));return 0;
}
std::string LastEvent(const std::string& report){const auto at=report.rfind("{\"kind\":");return at==std::string::npos?std::string{}:report.substr(at);}
int AdmissionJournalEvidence(){
    // Lose the exact current hand lease after press: the journal must identify
    // this predicate even though the same visible token remains present.
    {Fixture x;x.Step(false);x.probe->Prepare(x.n,x.f);x.Step(true);x.probe->Prepare(x.n,x.f);
        x.Step(true);--x.f.right->inputSequence;CHECK(!x.probe->Prepare(x.n,x.f));
        const auto e=LastEvent(x.Report());CHECK(e.find("\"phase\":5,\"reason\":2")!=std::string::npos);
        CHECK(e.find("\"admission_evaluated\":1,\"admission_failures\":1024")!=std::string::npos);
        CHECK(e.find("\"right_input\":"+std::to_string(x.f.right->inputSequence))!=std::string::npos);
        CHECK(e.find("\"right_deadline\":"+std::to_string(x.f.right->deadlineNs))!=std::string::npos);}
    // Capture the unreported parts of ammo admission at the actual rejection.
    for(unsigned failure=0;failure<3;++failure){Fixture x;x.Step(false);x.probe->Prepare(x.n,x.f);x.Step(true);x.probe->Prepare(x.n,x.f);x.Step(true);
        if(failure==0)x.n.original.server.nextState=3;
        if(failure==1)x.n.original.client.branches[1].phaseTimer=-.25f;
        if(failure==2)x.n.target.server.flagsA8=8;
        CHECK(!x.probe->Prepare(x.n,x.f));const auto e=LastEvent(x.Report());
        CHECK(e.find("\"admission_evaluated\":1,\"admission_failures\":64")!=std::string::npos);
        if(failure==0)CHECK(e.find("\"original_next\":[2,2,3]")!=std::string::npos);
        if(failure==1)CHECK(e.find("\"original_timers\":[0,-0.25,0]")!=std::string::npos);
        if(failure==2)CHECK(e.find("\"firing_flags_a8\":[0,0,0,0,0,8]")!=std::string::npos);}
    // Pre-press arming loss has Reason::None; its mask must still identify why.
    {Fixture x;x.Step(false);x.probe->Prepare(x.n,x.f);x.Step(false);x.f.source.admission.bodySettled=false;x.f.bodySettledFailures=1u<<8;
        CHECK(!x.probe->Prepare(x.n,x.f));const auto e=LastEvent(x.Report());CHECK(e.find("\"phase\":0,\"reason\":0")!=std::string::npos);
        CHECK(e.find("\"admission_failures\":4096,\"body_settled_failures\":256")!=std::string::npos);}
    // Staging uses the fully recomputed native/hand admission, not caller defaults.
    {Fixture x;const auto request=x.Issue();CHECK(request);x.n.target.server.flagsA8=8;
        CHECK(!x.probe->CanStage(*request,x.n,x.f));const auto e=LastEvent(x.Report());
        CHECK(e.find("\"admission_evaluated\":1,\"admission_failures\":64")!=std::string::npos);}
    // Missing native data never reports an evaluated all-passed mask.
    {Fixture x;const auto request=x.Issue();CHECK(request);CHECK(!x.probe->CanStage(*request,std::nullopt,x.f));
        CHECK(LastEvent(x.Report()).find("\"admission_evaluated\":0")!=std::string::npos);}
    // New fields must not expand native-change events on each renewed input.
    {Fixture x;const auto request=x.Issue();CHECK(request&&x.probe->CanStage(*request,x.n,x.f));x.probe->Commit(*request,x.f,true);
        CHECK(LastEvent(x.Report()).find("\"admission_evaluated\":1,\"admission_failures\":0")!=std::string::npos);
        x.SelectGrenade();x.Return();x.Finish();const auto r=x.Report();
        CHECK(r.find("\"passed\":1")!=std::string::npos);CHECK(r.find("\"dropped\":0")!=std::string::npos);
        CHECK(LastEvent(r).find("\"admission_evaluated\":0")!=std::string::npos);}
    return 0;
}
int StockReadyAdmission(){
    // Each original replica may be observed inside the native ready update.
    for(unsigned copy=0;copy<3;++copy){Fixture x;
        auto& state=copy<2?x.n.original.client.branches[copy]:x.n.original.server;
        state.phaseTimer=.0193585f;const auto req=x.Issue();CHECK(req);
        state.phaseTimer=.010061f;CHECK(x.probe->CanStage(*req,x.n,x.f));
        x.probe->Commit(*req,x.f,true);CHECK(x.Report().find("\"commits\":1")!=std::string::npos);}
    // Current/next non-ready state and malformed timer remain real rejection.
    for(unsigned copy=0;copy<3;++copy)for(unsigned failure=0;failure<8;++failure){Fixture x;
        x.Step(false);x.probe->Prepare(x.n,x.f);x.Step(true);x.probe->Prepare(x.n,x.f);x.Step(true);
        auto& state=copy<2?x.n.original.client.branches[copy]:x.n.original.server;
        if(failure==0)state.currentState=10;
        if(failure==1)state.nextState=10;
        if(failure==2)state.phaseTimer=-.01f;
        if(failure==3)state.phaseTimer=std::numeric_limits<float>::infinity();
        if(failure==4)state.phaseTimer=std::numeric_limits<float>::quiet_NaN();
        if(failure==5)state.phaseTimer=1000001;
        if(failure==6)state.flagsA8=8;
        if(failure==7)--state.loaded;
        CHECK(!x.probe->Prepare(x.n,x.f));const auto r=x.Report();
        CHECK(r.find("\"semantic_phase\":5,\"reason\":2")!=std::string::npos);
        CHECK(LastEvent(r).find("\"admission_failures\":64")!=std::string::npos);}
    return 0;
}
int StockReadyReturnOscillation(){Fixture x;const auto req=x.Issue();CHECK(req&&x.probe->CanStage(*req,x.n,x.f));x.probe->Commit(*req,x.f,true);
    x.SelectGrenade();x.Return();
    // First proof may itself fall within a positive-timer ready update.
    for(unsigned copy=0;copy<3;++copy)(copy<2?x.n.original.client.branches[copy]:x.n.original.server).phaseTimer=.0193585f;
    x.Step(false);x.probe->Observe(x.n,x.f);CHECK(x.Report().find("\"return_settled\":1")!=std::string::npos);
    for(unsigned i=0;i<410;++i){for(unsigned copy=0;copy<3;++copy)
            (copy<2?x.n.original.client.branches[copy]:x.n.original.server).phaseTimer=((i+copy)%3)?0:.010061f;
        x.Step(false);x.probe->Observe(x.n,x.f);auto nativeOnly=x.f;nativeOnly.custodyObserved=false;x.probe->Observe(x.n,nativeOnly);}
    const auto r=x.Report();CHECK(r.find("\"passed\":1")!=std::string::npos);CHECK(r.find("\"dropped\":0")!=std::string::npos);return 0;
}
int NonReadyReturnedRifleRemainsFailed(){
    for(unsigned copy=0;copy<3;++copy)for(unsigned failure=0;failure<6;++failure){Fixture x;const auto req=x.Issue();
        CHECK(req&&x.probe->CanStage(*req,x.n,x.f));x.probe->Commit(*req,x.f,true);x.SelectGrenade();x.Return();
        x.Step(false);x.probe->Observe(x.n,x.f);CHECK(x.Report().find("\"return_settled\":1")!=std::string::npos);
        auto& state=copy<2?x.n.original.client.branches[copy]:x.n.original.server;const auto old=state;
        if(failure==0)state.currentState=10;
        if(failure==1)state.nextState=10;
        if(failure==2)state.phaseTimer=-.01f;
        if(failure==3)state.phaseTimer=std::numeric_limits<float>::infinity();
        if(failure==4)state.phaseTimer=std::numeric_limits<float>::quiet_NaN();
        if(failure==5)state.phaseTimer=1000001;
        x.Step(false);auto nativeOnly=x.f;nativeOnly.custodyObserved=false;x.probe->Observe(x.n,nativeOnly);
        state=old;x.Finish();const auto r=x.Report();CHECK(r.find("\"failed\":1")!=std::string::npos);CHECK(r.find("\"passed\":0")!=std::string::npos);}
    return 0;
}

}
int main(int argc,char** argv){
    if(StockReadyAdmission()||StockReadyReturnOscillation()||NonReadyReturnedRifleRemainsFailed())return 1;
    if(argc==2&&std::string(argv[1])=="--stock-ready"){
        std::printf("Stock ready PASS:3 positive-timer admissions,24 invalid admission cases,410 returned timer oscillations across3 replicas,18 sticky non-ready return cases.\n");return 0;}
    if(AdmissionMaskMapping()||AdmissionJournalEvidence())return 1;
    if(argc==2&&std::string(argv[1])=="--admission-journal"){
        std::printf("Admission mapping20 bits and eight journal scenarios PASS; sizeof(Probe)=%zu\n",sizeof(Probe));return 0;}
    if(OneReturn()||RejectedReturnEvidence()||TargetAmmoExcursions()||InitialAmmoConvergence()||SettledReturnLoss()||FinalReadChanges()||Receiver()||OwnerRearmsHeldTrigger())return 1;
    std::printf("Eight focused integration groups PASS: one stock-return observation, seven failed evidence cases, six transient per-copy ammo excursions, allowed initial ammo convergence, fourteen transient settled-return losses, final target change, 1900 receiver samples, trigger owner rearming. sizeof(Probe)=%zu\n",sizeof(Probe));return 0;}
