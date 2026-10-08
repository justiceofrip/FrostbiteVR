#pragma once
namespace fvr::bc2::test {
using namespace fvr::interaction;
ReloadHoldInput Input(int loaded=20,int reserve=90){
    ReloadHoldInput i;i.verified=true;i.branch=0;i.nowNs=1000000000;i.leaseDeadlineNs=i.nowNs+100000000;
    i.identity.owner={0x10000,0x20000,0x30000,0x40000,1,2,3};i.identity.firing={0x50000,0x60000,0x70000};
    i.identity.serverPlayer=0x80000;i.identity.serverSoldier=0x90000;i.identity.serverItem=0xa0000;
    auto& c=i.config;std::memcpy(c.assetName.data(),"XM8_sp_s",sizeof("XM8_sp_s"));
    std::memcpy(c.assetPath.data(),"Objects/Weapons/Handheld/US_rgl_XM8/SP_rgl_XM8_Scoped",sizeof("Objects/Weapons/Handheld/US_rgl_XM8/SP_rgl_XM8_Scoped"));
    c.weaponData=0xb0000;c.firingData=0xc0000;c.primaryFire=0xd0000;c.ammoAddress=c.primaryFire+0x170;
    c.fireLogicType=2;c.reloadType=1;c.fireInputAction=8;c.reloadInputAction=29;
    c.baseCapacity=30;c.numberOfMagazines=4;c.reloadTime=2.8f;c.reloadThreshold=.75f;
    i.context.deltaSeconds=.005f;i.context.reloadTimeMultiplier=1;i.context.flags24Through28[0]=true;
    for(unsigned n=0;n<3;++n){auto& b=i.branches[n];b.address=i.identity.firing[n];
        b.wrapperOffset=n==0?0x3c:n==1?0x40:0x10;b.currentState=11;b.nextState=12;
        b.phaseTimer=1.8f;b.loaded=loaded;b.reserve=reserve;i.capacities[n]=30;}
    return i;
}
ManualReloadOwner Owner(const ReloadHoldIdentity& i){const auto& o=i.owner;return {o.soldier,o.actorGeneration,o.weapon,o.equipGeneration,o.space};}
struct Simulation {
    Bc2MagazineReloadCycle policy{true};ReloadHoldInput input;ReloadCycleControl control{};
    std::uint64_t update=0,invocation=1000;unsigned originals=0;float tailTimer=.65f;
    Simulation(int loaded=20,int reserve=90,MagazineNativeProfile profile=Xm8MagazineNativeProfile):policy(true,profile),input(Input(loaded,reserve)){
        control={input.identity,93,1,input.nowNs,input.leaseDeadlineNs,true};
    }
    bool Tick(std::int64_t delta=1000){input.nowNs+=delta;input.leaseDeadlineNs=input.nowNs+100000000;
        ++control.sequence;control.observedNs=input.nowNs;control.deadlineNs=input.leaseDeadlineNs;
        return policy.KeepAlive(control,input.nowNs);}
    ReloadRequestDecision Begin(unsigned branch,bool stable=true){input.branch=branch;return policy.Evaluate(input,true,stable,++update);}
    bool End(const ReloadRequestDecision& d,bool restore=true){++originals;ReloadDeltaOverride patch;
        if(d.hold){patch.applied=true;patch.restored=restore;patch.original=std::bit_cast<unsigned>(input.context.deltaSeconds);}
        input.nowNs+=100;return policy.Finish(d,input.branches[d.branch],input.nowNs,true,patch);}
    bool Start(){return policy.Start(control,{1,Owner(input.identity),ReloadOperation::UnseatMagazine,0,0},input.nowNs);}
    bool Arm(){
        if(!Start())return false;
        for(unsigned n=0;n<3;++n){if(!Tick())return false;const auto d=Begin(n);
            if(n<2){if(d.tracked)return false;}else if(!d.hold||!End(d))return false;}
        return HoldAll();
    }
    bool HoldAll(){for(unsigned n=0;n<3;++n){if(!Tick())return false;auto d=Begin(n);
        if(!d.hold||!policy.Allows(d,input.nowNs)||!End(d))return false;}return true;}
    std::optional<ReloadMagazineLease> Lease(){return policy.Lease(input.identity,control.cycle,input.nowNs);}
    std::optional<ReloadMagazineGateAcknowledgement> Unseat(){return policy.TakeUnseatAcknowledgement(input.identity,control.cycle,input.nowNs);}
    ReloadMagazineNativeRequest Request(){
        const auto& o=input.identity.owner;HandInteractionOwner physical{(std::uint64_t(o.weak)<<32)|o.soldier,o.actorGeneration,17,o.space};
        HandInteractionKey item{0xe0000,1};HandClaimToken claim{24,physical,InteractionHand::Left,HandClaimKind::AmmoObject,item,{7,1},10};
        return {{2,Owner(input.identity),ReloadOperation::SeatMagazine,1,0},Lease().value_or(ReloadMagazineLease{}),
            {item,claim,7,2,control.cycle},unsigned(std::min(30-input.branches[0].loaded,input.branches[0].reserve))};
    }
    bool Transfer(unsigned branch,int amount,bool record=true,bool tail=true){
        if(!Tick())return false;const auto d=Begin(branch);if(!d.tracked||d.hold)return false;
        auto& b=input.branches[branch];ReloadMagazineTransfer event{input.identity,control.cycle,++invocation,input.nowNs,input.nowNs+1,
            branch,b.loaded,b.reserve,b.loaded+amount,b.reserve-amount,true,true};
        if(record&&!policy.Transfer(event,d.update))return false;
        b.loaded+=amount;b.reserve-=amount;b.currentState=tail?12:2;b.nextState=tail?1:2;b.phaseTimer=tail?tailTimer:0;
        return End(d);
    }
    bool Settle(){for(auto& b:input.branches){b.currentState=b.nextState=2;b.phaseTimer=0;}
        if(!Tick())return false;const auto d=Begin(0);return !d.tracked&&policy.Phase()==ReloadRequestCyclePhase::Finished;}
};
}
