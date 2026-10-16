#include "Bc2NativeCycleExpiredControlProbe.h"
#include "Bc2ReloadConfigDescriptor.h"
#include "Test.h"
#include <bit>
#include <cstring>
#include <sstream>
using namespace fvr::bc2;
using namespace fvr::interaction;
namespace {
struct Fixture {
    Bc2NativeCycleService service;Bc2NativeCycleControl control{};ReloadHoldInput input{};
    std::int64_t now=1000000000;std::uint64_t invocation=0,updates=0;
    Fixture(){
        auto& n=input.identity;n.owner={0x10000,0x11000,0x12000,0x13000,11,12,13};
        n.firing={0x14000,0x15000,0x16000};n.serverPlayer=0x17000;n.serverSoldier=0x18000;n.serverItem=0x19000;
        auto& c=input.config;const auto& d=SpasReloadDescriptor;
        std::memcpy(c.assetName.data(),d.assetName.data(),d.assetName.size());
        std::memcpy(c.assetPath.data(),d.assetPath.data(),d.assetPath.size());
        c.weaponData=0x20000;c.firingData=0x21000;c.primaryFire=0x22000;c.ammoAddress=c.primaryFire+0x170;
        c.fireLogicType=1;c.reloadType=0;c.fireInputAction=8;c.reloadInputAction=29;c.baseCapacity=c.numberOfMagazines=4;
        c.reloadDelay=.06f;c.reloadTime=.72f;c.reloadThreshold=c.postReloadTime=1;c.boltDelay=.5f;
        control.nativeOwner=n.owner;control.input.owner={(std::uint64_t(n.owner.weak)<<32)|n.owner.soldier,11,99,13};
        control.item={n.owner.weapon,99};control.mechanism={0x50554d50,99};control.permitted=true;
        input.verified=true;input.context.deltaSeconds=1.f/60;input.context.reloadTimeMultiplier=1;
        input.context.flags24Through28[0]=true;input.capacities={8,8,8};
        for(unsigned b=0;b<3;++b){auto& v=input.branches[b];v.address=n.firing[b];v.wrapperOffset=b==0?0x3c:b==1?0x40:0x10;
            v.currentState=7;v.previousState=6;v.nextState=8;v.phaseTimer=.2f;v.loaded=7;v.reserve=24;}
    }
    Bc2NativeCycleControl Fresh(){
        now+=10000000;auto& s=control.input;++s.sequence;s.observedNs=s.nowNs=now;s.deadlineNs=now+100000000;
        s.focused=true;s.tracked={true,true};return control;
    }
    bool Update(unsigned b,bool shot=false){
        input.branch=b;input.contextObservedNs=input.nowNs=++now;input.leaseDeadlineNs=now+100000000;
        input.context.inputFlags=shot?1:0;input.context.fireRequested=shot;
        const auto decision=service.Evaluate(input,++invocation);ReloadFlowRecord r{};r.id=invocation;
        auto& e=r.entry;e.kind=ReloadFlowEvent::Update;e.nativeInvocation=e.nativeUpdate=e.update=invocation;
        e.thread=7;e.depth=1;e.caller=0x6e90b0;e.context=0x24000;e.nowNs=now;e.contextCopied=true;
        auto& v=e.boundary;const auto& s=input.branches[b];v.owner=input.identity.owner;v.snapshotSequence=20;
        v.firing=s.address;v.wrapperOffset=s.wrapperOffset;v.branch=std::uint8_t(b);
        v.current=s.currentState;v.previous=s.previousState;v.next=s.nextState;v.timer=s.phaseTimer;v.loaded=s.loaded;v.reserve=s.reserve;
        if(b==2){v.serverPlayer=input.identity.serverPlayer;v.serverSoldier=input.identity.serverSoldier;v.serverItem=input.identity.serverItem;}
        std::memcpy(e.copiedContext.data()+0x18,&input.context.deltaSeconds,4);
        std::memcpy(e.copiedContext.data()+0x20,&input.context.reloadTimeMultiplier,4);
        std::memcpy(e.copiedContext.data()+0x2c,&input.context.inputFlags,4);e.copiedContext[0x24]=std::byte{1};
        r.exit.thread=e.thread;r.exit.nowNs=++now;r.exit.boundary=v;r.exit.contextCopied=true;r.exit.copiedContext=e.copiedContext;
        if(shot){v.current=2;v.previous=1;v.next=2;v.timer=0;v.loaded=8;}
        if(decision.hold){r.exit.holdRequested=true;r.exit.hold={std::bit_cast<unsigned>(input.context.deltaSeconds),0,true,true,false};}
        r.finished=r.identityRetained=true;service.Finish(decision,r);++updates;return shot||decision.hold;
    }
    bool Held(){
        if(!service.Control(Fresh()))return false;for(unsigned b=0;b<3;++b)Update(b,true);
        for(unsigned pass=0;pass<2;++pass){if(!service.Control(Fresh()))return false;for(unsigned b=0;b<3;++b)Update(b);}
        return service.View(now).held.has_value();
    }
    void Progress(){service.Control(Fresh());for(unsigned b=0;b<3;++b)Update(b);}
};
std::string Report(const NativeCycleExpiredControlProbe& p,bool drained=true){std::ostringstream s;p.Report(s,drained);return s.str();}
int ActualServiceRejectsExactlyOneExpiredOriginal(){
    Fixture f;CHECK(f.Held());NativeCycleExpiredControlProbe probe;
    f.control=f.Fresh();CHECK(f.service.Control(f.control));
    probe.AfterControl(f.service,f.control,true,9,f.updates);CHECK(probe.Status()==NativeCycleExpiredControlProbe::State::Armed);
    const auto original=probe.Record().captured;
    // Ordinary accepted input and real service own-Update holds continue.
    while(f.now<original.input.deadlineNs){f.Progress();
        if(f.now<original.input.deadlineNs)probe.BeforeControl(f.service,f.control,f.now,9,f.updates);}
    auto fresh=f.Fresh();const auto before=f.service.LastControlInput();
    probe.BeforeControl(f.service,fresh,f.now,9,f.updates);
    CHECK(probe.Status()==NativeCycleExpiredControlProbe::State::Replayed&&!probe.Passed());
    CHECK(probe.Record().submissions==1&&!probe.Record().replayAccepted&&probe.Record().legacyWouldCancel);
    auto replay=probe.Record().replayed;CHECK(replay.input.nowNs>=original.input.deadlineNs);replay.input.nowNs=original.input.nowNs;
    CHECK(NativeCycleExpiredControlProbe::SameInput(replay.input,original.input));
    CHECK(replay.nativeOwner==original.nativeOwner&&replay.item==original.item&&replay.mechanism==original.mechanism&&
          replay.permitted==original.permitted&&!replay.release);
    CHECK(NativeCycleExpiredControlProbe::SameInput(before,f.service.LastControlInput()));
    const bool accepted=f.service.Control(fresh);probe.AfterControl(f.service,fresh,accepted,9,f.updates);
    CHECK(probe.Passed()&&f.service.View(f.now).blocksFire&&!f.service.View(f.now).ready);
    CHECK(probe.Record().debtPreserved&&probe.Record().inputPreserved&&probe.Record().freshAccepted);
    CHECK(Report(probe).find("\"passed\":true")!=std::string::npos);
    CHECK(Report(probe,false).find("\"passed\":false")!=std::string::npos);
    const auto terminal=Report(probe);
    for(unsigned n=0;n<10;++n){f.Progress();probe.BeforeControl(f.service,f.control,f.now,9,f.updates);probe.AfterControl(f.service,f.control,true,9,f.updates);}
    CHECK(Report(probe)==terminal&&probe.Record().submissions==1);return 0;
}
int UnexercisedAndChangedConditionsNeverPass(){
    NativeCycleExpiredControlProbe empty;CHECK(!empty.Passed());CHECK(Report(empty).find("\"reason\":\"not_exercised\"")!=std::string::npos);
    for(unsigned fault=0;fault<5;++fault){Fixture f;CHECK(f.Held());NativeCycleExpiredControlProbe p;
        auto capture=f.Fresh();CHECK(f.service.Control(capture));p.AfterControl(f.service,capture,true,1,f.updates);
        const auto count=f.updates;while(f.now<capture.input.deadlineNs)f.Progress();
        auto fresh=f.Fresh();auto epoch=1u;auto updates=f.updates;
        if(fault==0)++fresh.nativeOwner.weapon;
        if(fault==1)++epoch;
        if(fault==2)fresh.input.deadlineNs=f.now;
        if(fault==3)updates=count;
        if(fault==4)fresh.release=WeaponCycleRelease{};
        const auto before=f.service.LastControlInput();p.BeforeControl(f.service,fresh,f.now,epoch,updates);
        CHECK(!p.Passed()&&p.Status()==NativeCycleExpiredControlProbe::State::Failed&&p.Record().submissions==0);
        CHECK(NativeCycleExpiredControlProbe::SameInput(before,f.service.LastControlInput()));
        CHECK(f.service.Phase()==Bc2NativeCyclePhase::Held);
    }
    return 0;
}
int FollowingFreshAdmissionIsRequired(){
    Fixture f;CHECK(f.Held());NativeCycleExpiredControlProbe p;
    auto capture=f.Fresh();CHECK(f.service.Control(capture));p.AfterControl(f.service,capture,true,1,f.updates);
    while(f.now<capture.input.deadlineNs)f.Progress();auto fresh=f.Fresh();
    p.BeforeControl(f.service,fresh,f.now,1,f.updates);CHECK(p.Record().submissions==1);
    fresh.input.nowNs=fresh.input.deadlineNs;CHECK(!f.service.Control(fresh));
    p.AfterControl(f.service,fresh,false,1,f.updates);
    CHECK(!p.Passed()&&p.Failure()==NativeCycleExpiredControlProbe::Reason::FreshRejected);
    CHECK(f.service.Phase()==Bc2NativeCyclePhase::Held);return 0;
}
}
int main(){if(ActualServiceRejectsExactlyOneExpiredOriginal()||UnexercisedAndChangedConditionsNeverPass()||FollowingFreshAdmissionIsRequired())return 1;
    std::puts("Private expired-Control diagnostic preserves original input and native debt; unexercised/changed/fresh-rejected cases cannot pass.");}

