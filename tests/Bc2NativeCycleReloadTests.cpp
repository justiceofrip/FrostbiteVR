#include "Bc2NativeCycleService.h"
#include "Bc2BoltActionNativeProfile.h"
#include "Bc2M95StockShotConfig.h"
#include "Test.h"
#include <bit>
#include <cstring>
using namespace fvr::bc2;
using namespace fvr::interaction;
namespace {
constexpr std::int64_t Ms=1000000;
template<std::size_t N,class T>void Put(std::array<std::byte,N>& out,unsigned at,T value){std::memcpy(out.data()+at,&value,sizeof(value));}
ReloadObservedConfig Config(const ReloadConfigDescriptor& d){
    ReloadObservedConfig c;std::memcpy(c.assetName.data(),d.assetName.data(),d.assetName.size());
    std::memcpy(c.assetPath.data(),d.assetPath.data(),d.assetPath.size());
    c.weaponData=0x20000;c.firingData=0x21000;c.primaryFire=0x22000;c.ammoAddress=c.primaryFire+0x170;
    const auto& v=d.values;c.fireLogicType=v.fireLogicType;c.reloadType=v.reloadType;c.fireInputAction=v.fireInputAction;c.reloadInputAction=v.reloadInputAction;
    c.baseCapacity=v.baseCapacity;c.numberOfMagazines=v.numberOfMagazines;c.reloadDelay=v.reloadDelay;c.reloadTime=v.reloadTime;
    c.reloadThreshold=v.reloadThreshold;c.postReloadTime=v.postReloadTime;c.boltDelay=v.boltDelay;c.boltTime=v.boltTime;
    c.holdBoltUntilFireRelease=v.holdBoltUntilFireRelease;c.holdBoltUntilZoomRelease=v.holdBoltUntilZoomRelease;return c;
}
const Bc2CycleNativeProfile& M95(){for(const auto& p:Bc2CycleNativeProfiles)if(p.config->assetName=="M95_sp")return p;return Bc2CycleNativeProfiles[0];}
struct Fixture {
    Bc2NativeCycleService service;Bc2NativeCycleControl control{};ReloadHoldInput input{};AmmunitionLedger ledger;
    std::uint64_t invocation=0,revision=1,sequence=100;std::int64_t now=1000*Ms;
    explicit Fixture(const Bc2CycleNativeProfile& profile=M95()){
        service.Configure(profile.family==Bc2NativeCycleFamily::Bolt?Bc2NativeCycleMode::Bolt:Bc2NativeCycleMode::Pump);service.EnableRecovery();
        auto& n=input.identity;n.owner={0x10000,0x11000,0x12000,0x13000,11,12,13};
        n.firing={0x14000,0x15000,0x16000};n.serverPlayer=0x17000;n.serverSoldier=0x18000;n.serverItem=0x19000;
        input.config=Config(*profile.config);input.verified=true;input.context.deltaSeconds=1.f/60;input.context.reloadTimeMultiplier=1;
        input.context.flags24Through28[0]=true;input.capacities.fill(input.config.baseCapacity);
        const auto held=profile.Held(false);
        for(unsigned b=0;b<3;++b){auto& v=input.branches[b];v.address=n.firing[b];v.wrapperOffset=b==0?0x3c:b==1?0x40:0x10;
            v.currentState=held.current;v.previousState=held.previous;v.nextState=held.next;v.phaseTimer=held.maxTimer-.01f;
            v.loaded=input.config.baseCapacity-1;v.reserve=20;}
        control.nativeOwner=n.owner;control.input.owner={(std::uint64_t(n.owner.weak)<<32)|n.owner.soldier,n.owner.actorGeneration,99,n.owner.space};
        control.item={n.owner.weapon,99};control.mechanism={0x424f4c54,99};control.permitted=true;Advance();
    }
    NativeCycleRecoveryScope Scope()const{return {input.identity,input.config};}
    AmmoResourceContext Resource()const{return {{input.identity.owner.soldier,input.identity.owner.actorGeneration,input.identity.owner.weapon,7},input.identity.owner.equipGeneration,input.identity.owner.space};}
    bool Advance(bool neutral=false){now+=Ms;auto& s=control.input;++s.sequence;s.observedNs=s.nowNs=now;s.deadlineNs=now+100*Ms;
        s.focused=true;s.tracked={true,true};s.released={false,neutral};return service.Control(control);}
    ReloadFlowRecord Record(unsigned b,bool idle=false){
        ReloadFlowRecord r;r.id=++invocation;r.finished=r.identityRetained=true;
        auto& e=r.entry;e.kind=ReloadFlowEvent::Update;e.nativeInvocation=e.nativeUpdate=e.update=r.id;e.depth=1;
        e.thread=7;e.caller=0x6e90b0;e.context=0x24000;e.nowNs=++now;e.contextCopied=true;
        Put(e.copiedContext,0x18,1.f/60);Put(e.copiedContext,0x20,1.f);e.copiedContext[0x24]=std::byte{1};
        auto& z=e.boundary;const auto& v=input.branches[b];z.owner=input.identity.owner;z.snapshotSequence=20;z.branch=std::uint8_t(b);
        z.firing=v.address;z.wrapperOffset=v.wrapperOffset;z.current=idle?2:v.currentState;z.previous=idle?1:v.previousState;
        z.next=idle?2:v.nextState;z.timer=idle?0:v.phaseTimer;z.loaded=idle?service.ReloadDebt().loaded:v.loaded;z.reserve=idle?service.ReloadDebt().reserve:v.reserve;
        if(b==2){z.serverPlayer=input.identity.serverPlayer;z.serverSoldier=input.identity.serverSoldier;z.serverItem=input.identity.serverItem;}
        r.exit.thread=e.thread;r.exit.nowNs=now+1;r.exit.boundary=z;r.exit.contextCopied=true;r.exit.copiedContext=e.copiedContext;return r;
    }
    bool Hold(){
        for(unsigned pass=0;pass<3;++pass){if(!Advance())return false;
            for(unsigned b=0;b<3;++b){auto r=Record(b);input.branch=b;input.nowNs=input.contextObservedNs=r.entry.nowNs;input.leaseDeadlineNs=now+100*Ms;
                input.context.inputFlags=pass?0:1;input.context.fireRequested=pass==0;
                const auto d=service.Evaluate(input,r.id);if(!d.tracked)return false;
                if(!pass){r.entry.boundary.current=2;r.entry.boundary.previous=1;r.entry.boundary.next=2;r.entry.boundary.timer=0;
                    r.entry.boundary.loaded=input.config.baseCapacity;Put(r.entry.copiedContext,0x2c,1u);r.exit.copiedContext=r.entry.copiedContext;}
                if(d.hold){r.exit.holdRequested=true;r.exit.hold={std::bit_cast<unsigned>(input.context.deltaSeconds),0,true,true,false};}
                service.Finish(d,r);now=r.exit.nowNs;
            }
        }return service.View(now).held.has_value();
    }
    WeaponCycleReloadPause PauseValue(){Advance(true);const auto v=service.View(now);
        return {control.input.owner,control.item,control.mechanism,v.cycle,v.shot,1,7,1,control.input.sequence,
            control.input.observedNs,control.input.deadlineNs,now+5000*Ms,now,now+WeaponCycle::MaximumReloadPauseNs,.1f,1.f};}
    bool Pause(){const auto p=PauseValue();return service.PrepareReload(Scope(),p,Resource(),{revision,now},now);}
    bool Round(){
        for(unsigned b=0;b<3;++b){now+=Ms;auto r=Record(b,true);
            const auto d=service.EvaluateRecovery(Scope(),r.entry,service.ReloadDebt().capacity,now+80*Ms);if(!d)return false;
            NativeCycleInputGuard g;g.original={0,0};g.effective=g.beforeRestore={1,0};g.requested=g.applied=g.restored=true;
            r.exit.cycleGuard=g;r.exit.cycleGuardEpoch=d.epoch;r.exit.cycleGuardSteps=1;
            service.FinishRecovery(d,r,g,1,0);now=r.exit.nowNs;
            if(service.Phase()!=Bc2NativeCyclePhase::Converging)return false;
        }return true;
    }
    std::optional<WeaponCycleReloadCommand> Dispatch(AmmunitionOperation operation){
        const auto permit=service.ReloadPermit(Scope(),now);if(!permit)return {};
        ++now;const auto& d=service.ReloadDebt();AmmunitionSnapshot s{Resource(),++sequence,now,now+80*Ms,{d.loaded,d.reserve,d.capacity},true};
        if(!ledger.Bind(s,now))return {};
        const auto command=ledger.Queue(operation,s,now,operation==AmmunitionOperation::ReturnMagazine?ledger.Original():std::nullopt);
        if(!command||!ledger.Dispatch(s,now)||!service.BeginReloadCommand(Scope(),*permit,*command,{++revision,now},now))return {};
        return service.ReloadCommand();
    }
    WeaponCycleReloadAmmoRebase Receipt(const WeaponCycleReloadCommand& c){
        WeaponCycleReloadAmmoRebase r;r.dispatched=c;r.receipt={c.command,900+c.command.id,now+Ms,now+20*Ms,c.command.before,c.command.after,true,true};
        for(unsigned b=0;b<3;++b){const auto start=now+(2+2*b)*Ms;const auto& a=c.command.after;
            r.ownUpdates[b]={b,++invocation,service.ReloadDebt().guardEpoch,start,start+Ms,start+81*Ms,a.loaded,a.reserve,a.capacity};}
        now=r.receipt.completedNs;r.drain={++revision,now};r.guardEpoch=service.ReloadDebt().guardEpoch+1;return r;
    }
    bool Apply(const WeaponCycleReloadAmmoRebase& r){return ledger.Complete(r.receipt,now)&&service.ApplyReloadReceipt(Scope(),r,now);}
};
int ExactCycleProfilesAndTimerBounds(){
    unsigned bolts=0,pumps=0;for(const auto& p:Bc2CycleNativeProfiles){auto c=Config(*p.config);
        CHECK(ResolveNativeCycleProfile(c)==&p);const bool bolt=p.family==Bc2NativeCycleFamily::Bolt;
        bolt?++bolts:++pumps;CHECK(bool(ResolveBoltActionNativeProfile(c))==bolt);
        auto wrong=c;wrong.boltTime+=.01f;CHECK(!ResolveNativeCycleProfile(wrong));
        wrong=c;++wrong.reloadInputAction;CHECK(!ResolveNativeCycleProfile(wrong));wrong=c;++wrong.ammoAddress;CHECK(!ResolveNativeCycleProfile(wrong));
        const auto boundary=p.Held(false);CHECK(boundary.maxTimer==(bolt?c.boltTime:c.boltDelay));
        CHECK(CycleConvergencePhase(boundary.current,boundary.next,boundary.maxTimer,p.Convergence(false)));
        CHECK(!CycleConvergencePhase(boundary.current,boundary.next,boundary.maxTimer+.001f,p.Convergence(false)));
        const auto empty=p.Held(true);CHECK(empty.current==6&&empty.previous==5&&empty.next==1&&empty.emptyAmmunition);
        Fixture f(p);CHECK(f.Hold());CHECK(f.service.View(f.now).capacity==c.baseCapacity);
        Bc2NativeCycleService selected;Bc2AmmoReserveLease reserve{f.input.identity,1,f.now,f.now+80*Ms,c.baseCapacity,20,c.baseCapacity,true,true,true};
        const auto mode=bolt?Bc2NativeCycleMode::Bolt:Bc2NativeCycleMode::Pump;
        if(bolt){auto wrongCapacity=reserve;++wrongCapacity.capacity;
            CHECK(!selected.Select({mode,wrongCapacity,c,f.now}));}
        CHECK(selected.Select({mode,reserve,c,f.now}));
    }CHECK(bolts==17&&pumps==25);
    auto svu=Config(*M95().config);svu.assetName={};svu.assetPath={};
    const char name[]="SVU",path[]="Objects/Weapons/Handheld/UL_sni_SVU/UL_sni_SVU";
    std::memcpy(svu.assetName.data(),name,sizeof(name));std::memcpy(svu.assetPath.data(),path,sizeof(path));svu.fireLogicType=0;svu.boltTime=.5f;
    CHECK(!ResolveNativeCycleProfile(svu));CHECK(M95StockShotConfig(Config(*M95().config)));return 0;
}
int NativeResourceNamespaceAndScopeAreExact(){
    Fixture f;CHECK(f.Hold());const auto pause=f.PauseValue();const auto resource=f.Resource();
    CHECK(resource.resource.actor!=pause.owner.actor&&resource.equipGeneration!=pause.owner.equipGeneration);
    auto wrong=resource;wrong.resource.actor=pause.owner.actor;
    CHECK(!f.service.PrepareReload(f.Scope(),pause,wrong,{1,f.now},f.now));
    wrong=resource;wrong.equipGeneration=pause.owner.equipGeneration;
    CHECK(!f.service.PrepareReload(f.Scope(),pause,wrong,{1,f.now},f.now));
    auto scope=f.Scope();++scope.identity.firing[1];CHECK(!f.service.PrepareReload(scope,pause,resource,{1,f.now},f.now));
    CHECK(f.service.View(f.now).held&&f.service.PrepareReload(f.Scope(),pause,resource,{1,f.now},f.now));
    CHECK(f.service.ReloadPause()&&f.service.View(f.now).blocksFire&&!f.service.YieldForReload());
    CHECK(!f.service.View(f.now).held&&!f.service.ReloadPermit(f.Scope(),f.now));
    CHECK(f.Round()&&!f.service.ReloadPermit(f.Scope(),f.now));CHECK(f.Round()&&f.service.ReloadPermit(f.Scope(),f.now));return 0;
}
int ReceiptRebasesDebtWithoutPayingStroke(){
    Fixture f;CHECK(f.Hold()&&f.Pause()&&f.Round()&&f.Round());
    const auto initial=f.service.ReloadDebt();const auto remove=f.Dispatch(AmmunitionOperation::RemoveMagazine);CHECK(remove);
    auto partial=f.Record(0,true);partial.entry.boundary.loaded=0;
    CHECK(!f.service.EvaluateRecovery(f.Scope(),partial.entry,initial.capacity,f.now+80*Ms));
    CHECK(f.service.Phase()==Bc2NativeCyclePhase::Converging&&f.service.ReloadCommand()==remove);
    CHECK(f.service.BeginRecovery(f.now));CHECK(f.service.ReloadCommand()==remove);
    const auto removed=f.Receipt(*remove);auto wrong=removed;++wrong.ownUpdates[1].loaded;
    CHECK(!f.service.ApplyReloadReceipt(f.Scope(),wrong,f.now));CHECK(f.service.ReloadDebt()==initial);
    auto foreign=f.Scope();++foreign.identity.owner.soldier;CHECK(!f.service.ApplyReloadReceipt(foreign,removed,f.now));
    CHECK(f.Apply(removed));CHECK(!f.service.ApplyReloadReceipt(f.Scope(),removed,f.now));CHECK(f.service.LastReloadReceipt()==removed.receipt);
    CHECK(f.ledger.Original()&&f.ledger.Discard(*f.ledger.Original()));CHECK(f.Round()&&f.Round());
    const auto refill=f.Dispatch(AmmunitionOperation::RefillMagazine);CHECK(refill);CHECK(f.Apply(f.Receipt(*refill)));
    const auto& debt=f.service.ReloadDebt();CHECK(debt.loaded==debt.capacity&&debt.capacity==initial.capacity);
    CHECK(debt.cycle==initial.cycle&&debt.shot==initial.shot&&debt.owner==initial.owner&&debt.ammunitionRebase);
    CHECK(f.service.View(f.now).loaded==debt.loaded&&f.service.View(f.now).blocksFire&&!f.service.View(f.now).ready);
    CHECK(f.Round()&&f.Round());CHECK(f.Advance(true));CHECK(f.Round()&&f.Round());
    const auto view=f.service.RecoveryView(f.now);CHECK(view&&view->authority);
    auto changed=*view->authority;++changed.sequence;CHECK(!f.service.ResumeReload(f.Scope(),changed,f.now));
    CHECK(f.service.ResumeReload(f.Scope(),*view->authority,f.now));CHECK(!f.service.ReloadPause());
    CHECK(f.service.View(f.now).blocksFire&&!f.service.YieldForReload());CHECK(!f.service.ResumeReload(f.Scope(),*view->authority,f.now));
    return 0;
}
}
int main(){return ExactCycleProfilesAndTimerBounds()||NativeResourceNamespaceAndScopeAreExact()||ReceiptRebasesDebtWithoutPayingStroke();}
