#include <cstring>
#include "Bc2ReloadNativePolicy.h"
#include "Bc2ReloadAbort.h"
#include "Bc2ReloadRetirement.h"
#include "Bc2ReloadInvocationEntry.h"
#include "Bc2MagazineReloadSession.h"
#include "Test.h"
using namespace fvr::bc2;
namespace {
constexpr std::int64_t Now=1000000000;
ReloadHoldIdentity Identity(){ReloadHoldIdentity i;i.owner={0x10000,0x20000,0x30000,0x40000,1,2,3};
    i.firing={0x50000,0x60000,0x70000};i.serverPlayer=0x80000;i.serverSoldier=0x90000;i.serverItem=0xa0000;return i;}
ReloadCycleControl Control(std::uint64_t cycle=1){return {Identity(),cycle,1,Now,Now+100000000,true};}
fvr::interaction::ManualReloadRequest Unseat(){const auto o=Identity().owner;return {1,{o.soldier,o.actorGeneration,o.weapon,o.equipGeneration,o.space},fvr::interaction::ReloadOperation::UnseatMagazine,0,0};}
int ExplicitRetirement(){
    Bc2ReloadNativePolicy p;CHECK(p.Family()==ReloadNativeFamily::SpasTube);
    CHECK(!p.SelectFamily(ReloadNativeFamily::Xm8Magazine,false,false));
    CHECK(!p.SelectFamily(static_cast<ReloadNativeFamily>(255),true,true));
    CHECK(p.Start(Control(),Now));CHECK(!p.SelectFamily(ReloadNativeFamily::Xm8Magazine,true,true));
    p.Cancel();CHECK(!p.SelectFamily(ReloadNativeFamily::Xm8Magazine,true,false));
    CHECK(!CancelAndDrainReloadCycle(p,Identity(),1,false));CHECK(CancelAndDrainReloadCycle(p,Identity(),1,true));
    ReloadRetirementReceipts receipts;const auto retired=receipts.Observe(p,Identity(),1,Now+1,true);CHECK(retired&&retired->verified);
    CHECK(p.SelectFamily(ReloadNativeFamily::Xm8Magazine,true,true));CHECK(p.IsMagazine());
    CHECK(!p.Start(Control(),Now));CHECK(p.StartMagazine(Control(),Unseat(),Now));
    CHECK(!p.SelectFamily(ReloadNativeFamily::SpasTube,true,true));
    CHECK(CancelAndDrainReloadCycle(p,Identity(),1,true));CHECK(receipts.Observe(p,Identity(),1,Now+2,true));
    CHECK(p.SelectFamily(ReloadNativeFamily::SpasTube,true,true));
    CHECK(!p.Start(Control(),Now));CHECK(p.Start(Control(2),Now));
    return 0;
}
int IdleAndTypedRouting(){
    Bc2ReloadNativePolicy p;CHECK(p.SelectFamily(ReloadNativeFamily::Xm8Magazine,true,false));
    CHECK(!p.Lease(Identity(),1,Now)&&!p.Submit({},Now)&&!p.TakeAcknowledgement(Identity(),1,Now));
    CHECK(p.SelectFamily(ReloadNativeFamily::SpasTube,true,false));
    CHECK(!p.MagazineLease(Identity(),1,Now)&&!p.SubmitMagazine({},Now)&&!p.TakeMagazineAcknowledgement(Identity(),1,Now));
    CHECK(p.SelectFamily(ReloadNativeFamily::SpasTube,true,false));return 0;
}
ReloadRoundLease Lease(){return {Identity(),1,1,Now,Now+80000000,5,18,8,true,true};}
ReloadHoldInput Input(){ReloadHoldInput i;i.identity=Identity();i.verified=true;i.branch=0;i.nowNs=Now+1000;i.leaseDeadlineNs=Now+80000000;
    i.context.deltaSeconds=.005f;i.context.reloadTimeMultiplier=1;
    for(unsigned b=0;b<3;++b){auto& x=i.branches[b];x.address=i.identity.firing[b];x.wrapperOffset=b==0?0x3c:b==1?0x40:0x10;
        x.currentState=11;x.nextState=12;x.phaseTimer=.3f;x.loaded=5;x.reserve=18;}return i;}
int AbortNamespaces(){
    ReloadAbortCleanup p;CHECK(p.Arm(Lease(),7,Now,true));
    const auto old=p.Claim(Input(),7,1,true,10,true,Now+1000);CHECK(old.call&&old.dispatchEpoch==0);
    CHECK(!p.BeginDispatchEpoch(2,true));p.Abandon(ReloadAbortFailure::NewCycle,Now+2000);
    CHECK(!p.BeginDispatchEpoch(2,false));CHECK(p.BeginDispatchEpoch(2,true));CHECK(!p.BeginDispatchEpoch(2,true));
    CHECK(p.Arm(Lease(),7,Now+3000,true));auto in=Input();in.nowNs=Now+4000;
    const auto fresh=p.Claim(in,7,1,true,10,true,in.nowNs);CHECK(fresh.call&&fresh.dispatchEpoch==2);
    auto after=fresh.before;after.currentState=after.nextState=2;after.phaseTimer=0;
    CHECK(!p.Allows(old,7,1,true,Now+5000));CHECK(!p.Finish(old,after,true,true,Now+5000));
    CHECK(p.Allows(fresh,7,1,true,Now+5000));CHECK(p.Finish(fresh,after,true,true,Now+5000));
    CHECK(p.HistoryCount()==2&&p.History(0).dispatchEpoch==0&&p.History(1).dispatchEpoch==2);
    CHECK(p.History(0).failure==ReloadAbortFailure::NewCycle);return 0;
}
int CallbackExclusion(){
    std::atomic<unsigned> active=0;std::atomic<std::uint64_t> revision=0;std::atomic_flag gate=ATOMIC_FLAG_INIT;
    CHECK(EnterReloadInvocation(active,revision,gate));
    {ReloadInvocationExclusion exclusion(gate,active,revision);CHECK(exclusion.held&&exclusion.Quiet());
        CHECK(!EnterReloadInvocation(active,revision,gate));CHECK(!exclusion.Quiet());
        ExitReloadInvocation(active,revision);CHECK(!exclusion.Quiet());}
    ExitReloadInvocation(active,revision);CHECK(active.load()==0);return 0;
}
int SessionIsolation(){
    constexpr auto physical=9u|0x197800u|0x2000000u;
    CHECK(ValidMagazineReloadSession(3,physical,3000));CHECK(ValidMagazineReloadSession(3,physical|0x10000000u|0x200000u|0x600u,40984));
    CHECK(!ValidMagazineReloadSession(3,physical,999)&&!ValidMagazineReloadSession(3,physical,60001));
    CHECK(!ValidMagazineReloadSession(3,physical|0x400u,0));
    CHECK(ValidMagazineReloadSession(4,physical|0x200u,30000));CHECK(!ValidMagazineReloadSession(4,physical,15000));
    for(auto bad:{0x400u,0x200000u,0x10000000u})CHECK(!ValidMagazineReloadSession(4,physical|bad,30000));
    for(auto mode:{3u,4u,5u,6u}){
        for(auto bad:{0x100u,0x8000u,0x20000u,0x40000u,0x400000u,0x800000u,0x1000000u,0x4000000u,0x8000000u,0x20000000u,0x40000000u,0x80000000u})
            CHECK(!ValidMagazineReloadSession(mode,physical|bad,30000));
        for(auto required:{0x800u,0x1000u,0x2000u,0x4000u,0x10000u,0x80000u,0x100000u,0x2000000u})
            CHECK(!ValidMagazineReloadSession(mode,physical&~required,30000));
    }
    CHECK(ValidMagazineReloadSession(5,physical,30000)&&ValidMagazineReloadSession(5,physical|0x200u,30000));
    for(auto duration:{0u,15000u,29999u,30001u,60000u})CHECK(!ValidMagazineReloadSession(5,physical,duration));
    for(auto bad:{0x400u,0x200000u,0x10000000u})CHECK(!ValidMagazineReloadSession(5,physical|bad,30000));
    CHECK(ValidMagazineReloadSession(6,physical,30000)&&ValidMagazineReloadSession(6,physical|0x200u,30000));
    for(auto duration:{0u,15000u,29999u,30001u,60000u})CHECK(!ValidMagazineReloadSession(6,physical,duration));
    for(auto bad:{0x400u,0x200000u,0x10000000u})CHECK(!ValidMagazineReloadSession(6,physical|bad,30000));
    for(unsigned mode=0;mode<=9;++mode)CHECK(MagazineDetachedSessionEnabled(mode)==(mode==3||mode==6||mode==7||mode==8));
    CHECK(!ValidMagazineReloadSession(9,physical|0x10000000u,60000));
    CHECK(!ValidMagazineReloadSession(7,physical,30000));CHECK(ValidMagazineReloadSession(0,0,0));return 0;
}
}
// Models the actual runtime's single dispatch-epoch receipt. A successful
// selection consumes it; only the policy object can retain an old stopped cycle.
struct Dispatch {
    Bc2ReloadNativePolicy policy;
    ReloadRetirementReceipts receipts;
    struct Stamp {ReloadNativeFamily family;ReloadHoldIdentity identity;std::uint64_t cycle,epoch;};
    std::optional<Stamp> retired;
    std::uint64_t epoch=0;
    std::int64_t now=Now;
    bool Select(ReloadNativeFamily family,NativeMagazineProfileId profile=NativeMagazineProfileId::ScopedXm8,bool quiet=true){
        const bool proof=retired&&retired->family==policy.Family()&&retired->identity==policy.Identity()&&
            retired->cycle==policy.Cycle()&&retired->epoch==epoch;
        const bool okay=family==ReloadNativeFamily::SpasTube?policy.SelectFamily(family,quiet,proof):
            policy.SelectMagazineProfile(profile,quiet,proof);
        if(okay){retired.reset();++epoch;}return okay;
    }
    bool Start(std::uint64_t cycle,ReloadHoldIdentity id=Identity()){
        ReloadCycleControl control{id,cycle,1,now,now+100000000,true};
        const auto o=id.owner;
        const bool okay=policy.IsMagazine()?policy.StartMagazine(control,{cycle,{o.soldier,o.actorGeneration,o.weapon,o.equipGeneration,o.space},
            fvr::interaction::ReloadOperation::UnseatMagazine,0,0},now):policy.Start(control,now);
        if(okay)retired.reset();return okay;
    }
    bool Retire(bool quiet=true){
        if(!CancelAndDrainReloadCycle(policy,policy.Identity(),policy.Cycle(),quiet))return false;
        const auto receipt=receipts.Observe(policy,policy.Identity(),policy.Cycle(),++now,quiet);
        if(!receipt)return false;
        retired=Stamp{policy.Family(),receipt->identity,receipt->cycle,epoch};return true;
    }
};
int PreviouslyRetiredFamilyCanBeRevisited(){
    Dispatch f;CHECK(f.Start(1));CHECK(f.Retire());
    CHECK(f.Select(ReloadNativeFamily::Xm8Magazine));CHECK(f.Start(1));CHECK(f.Retire());
    CHECK(f.Select(ReloadNativeFamily::SpasTube));CHECK(!f.retired);
    // This fails with the old canonical policy. No user reload happened during
    // the revisit, so the consumer has no reason to retire this old cycle again.
    CHECK(f.Select(ReloadNativeFamily::Xm8Magazine));
    for(unsigned n=0;n<4;++n){CHECK(f.Select(ReloadNativeFamily::SpasTube));CHECK(f.Select(ReloadNativeFamily::Xm8Magazine));}
    CHECK(f.policy.Cycle()==1&&f.policy.Phase()==ReloadRequestCyclePhase::Cancelled);
    return 0;
}
int SamePolicyProfileRevisitsPreserveRetirement(){
    Dispatch f;CHECK(f.Select(ReloadNativeFamily::Xm8Magazine,NativeMagazineProfileId::AuthoredAek));
    CHECK(f.Start(1));CHECK(f.Retire());
    CHECK(f.Select(ReloadNativeFamily::Xm8Magazine,NativeMagazineProfileId::ScopedXm8));
    CHECK(f.Select(ReloadNativeFamily::Xm8Magazine,NativeMagazineProfileId::AuthoredAek));
    CHECK(f.Select(ReloadNativeFamily::SpasTube));CHECK(f.Start(1));CHECK(f.Retire());
    CHECK(f.Select(ReloadNativeFamily::Xm8Magazine,NativeMagazineProfileId::ScopedXm8));
    CHECK(&f.policy.MagazineProfile()==&Xm8MagazineNativeProfile);
    CHECK(f.Select(ReloadNativeFamily::SpasTube));
    CHECK(f.Select(ReloadNativeFamily::Xm8Magazine,NativeMagazineProfileId::AuthoredAek));
    CHECK(&f.policy.MagazineProfile()==&AekMagazineNativeProfile);
    CHECK(!f.Select(ReloadNativeFamily::Xm8Magazine,static_cast<NativeMagazineProfileId>(255)));
    CHECK(f.policy.MagazineProfileId()==NativeMagazineProfileId::AuthoredAek);return 0;
}
int NewCycleAndOwnerCannotBorrowOldRetirement(){
    Dispatch f;CHECK(f.Start(1));CHECK(f.Retire());CHECK(f.Select(ReloadNativeFamily::Xm8Magazine));
    auto magazineOwner=Identity();magazineOwner.owner.weapon+=0x100;magazineOwner.serverItem+=0x100;
    CHECK(f.Start(1,magazineOwner));CHECK(f.Retire());CHECK(f.Select(ReloadNativeFamily::SpasTube));
    auto replacement=Identity();++replacement.owner.actorGeneration;++replacement.owner.equipGeneration;++replacement.owner.space;
    CHECK(f.Start(2,replacement));f.policy.Cancel();
    CHECK(!f.Select(ReloadNativeFamily::Xm8Magazine));
    CHECK(!CancelAndDrainReloadCycle(f.policy,Identity(),2,true));
    CHECK(!f.Select(ReloadNativeFamily::Xm8Magazine));
    CHECK(f.Retire());CHECK(f.Select(ReloadNativeFamily::Xm8Magazine));
    CHECK(f.policy.Identity()==magazineOwner);
    ++magazineOwner.owner.equipGeneration;++magazineOwner.owner.space;
    CHECK(f.Start(2,magazineOwner));f.policy.Cancel();
    CHECK(!f.Select(ReloadNativeFamily::SpasTube));
    CHECK(!f.Select(ReloadNativeFamily::Xm8Magazine,NativeMagazineProfileId::AuthoredAek));
    CHECK(f.Retire());CHECK(f.Select(ReloadNativeFamily::SpasTube));return 0;
}
int RetiredDispatchFactIsNotFreshAmmoOrControl(){
    Dispatch f;CHECK(f.Start(1));CHECK(f.Retire());CHECK(f.Select(ReloadNativeFamily::Xm8Magazine));
    CHECK(f.Start(1));CHECK(f.Retire());CHECK(f.Select(ReloadNativeFamily::SpasTube));
    // A vehicle/focus interval may invalidate every old live lease; only the
    // stop fact remains. Runtime still separately requires a fresh selected owner.
    f.now+=1000000000;
    CHECK(!f.Select(ReloadNativeFamily::Xm8Magazine,NativeMagazineProfileId::ScopedXm8,false));
    CHECK(!f.policy.Lease(Identity(),1,f.now));CHECK(!f.policy.KeepAlive(Control(),f.now));
    CHECK(!f.Start(1));CHECK(!f.policy.TakeAcknowledgement(Identity(),1,f.now));
    CHECK(f.Select(ReloadNativeFamily::Xm8Magazine));
    CHECK(!f.policy.MagazineLease(Identity(),1,f.now));CHECK(!f.policy.TakeUnseatAcknowledgement(Identity(),1,f.now));
    CHECK(!f.policy.TakeMagazineAcknowledgement(Identity(),1,f.now));CHECK(!f.Start(1));
    CHECK(!f.policy.KeepAlive(Control(),f.now));
    CHECK(f.Select(ReloadNativeFamily::SpasTube));CHECK(f.Start(2));
    CHECK(f.policy.Phase()==ReloadRequestCyclePhase::Arming);
    CHECK(!f.Select(ReloadNativeFamily::Xm8Magazine));return 0;
}

#if !defined(FVR_BASELINE)
ReloadObservedConfig ObservedConfig(const ReloadConfigDescriptor& d){
    ReloadObservedConfig c;c.weaponData=0xb0000;c.firingData=0xc0000;c.primaryFire=0xd0000;c.ammoAddress=c.primaryFire+0x170;
    std::memcpy(c.assetName.data(),d.assetName.data(),d.assetName.size());
    std::memcpy(c.assetPath.data(),d.assetPath.data(),d.assetPath.size());
    const auto& v=d.values;c.fireLogicType=v.fireLogicType;c.reloadType=v.reloadType;
    c.fireInputAction=v.fireInputAction;c.reloadInputAction=v.reloadInputAction;c.baseCapacity=v.baseCapacity;c.numberOfMagazines=v.numberOfMagazines;
    c.reloadDelay=v.reloadDelay;c.reloadTime=v.reloadTime;c.reloadThreshold=v.reloadThreshold;c.postReloadTime=v.postReloadTime;
    c.boltDelay=v.boltDelay;c.boltTime=v.boltTime;c.holdBoltUntilFireRelease=v.holdBoltUntilFireRelease;c.holdBoltUntilZoomRelease=v.holdBoltUntilZoomRelease;
    return c;
}
int CurrentConfigGateFollowsRevisitedSelection(){
    Dispatch f;const auto spas=ObservedConfig(SpasReloadDescriptor),xm8=ObservedConfig(Xm8ReloadDescriptor),
        aek=ObservedConfig(AekMagazineNativeProfile.configuration);
    CHECK(f.Start(1));CHECK(f.Retire());CHECK(f.Select(ReloadNativeFamily::Xm8Magazine));
    CHECK(f.Start(1));CHECK(f.Retire());
    for(unsigned pass=0;pass<3;++pass){
        CHECK(f.Select(ReloadNativeFamily::SpasTube));
        CHECK(f.policy.MatchesSelectedConfig(spas));CHECK(!f.policy.MatchesSelectedConfig(xm8));CHECK(!f.policy.MatchesSelectedConfig(aek));
        CHECK(f.Select(ReloadNativeFamily::Xm8Magazine,NativeMagazineProfileId::AuthoredAek));
        CHECK(f.policy.MatchesSelectedConfig(aek));CHECK(!f.policy.MatchesSelectedConfig(xm8));CHECK(!f.policy.MatchesSelectedConfig(spas));
        CHECK(f.Select(ReloadNativeFamily::Xm8Magazine,NativeMagazineProfileId::ScopedXm8));
        CHECK(f.policy.MatchesSelectedConfig(xm8));CHECK(!f.policy.MatchesSelectedConfig(aek));CHECK(!f.policy.MatchesSelectedConfig(spas));
    }
    auto changed=xm8;changed.reloadTime+=.1f;CHECK(!f.policy.MatchesSelectedConfig(changed));
    changed=xm8;changed.weaponData=0;CHECK(!f.policy.MatchesSelectedConfig(changed));
    // Failed rebinds cannot erase the existing selected configuration or stop fact.
    CHECK(!f.Select(ReloadNativeFamily::Xm8Magazine,NativeMagazineProfileId::AuthoredAek,false));
    CHECK(f.policy.MatchesSelectedConfig(xm8));CHECK(f.Select(ReloadNativeFamily::SpasTube));return 0;
}
#else
int CurrentConfigGateFollowsRevisitedSelection(){return 0;}
#endif

ReloadHoldInput HoldingInput()
{
    ReloadHoldInput i;
    i.verified = true;
    i.branch = 0;
    i.nowNs = 1000000000;
    i.leaseDeadlineNs = i.nowNs + 100000000;
    i.identity.owner = {0x10000, 0x20000, 0x30000, 0x40000, 1, 2, 3};
    i.identity.firing = {0x50000, 0x60000, 0x70000};
    i.identity.serverPlayer = 0x80000;
    i.identity.serverSoldier = 0x90000;
    i.identity.serverItem = 0xa0000;
    auto &c = i.config;
    std::memcpy(c.assetName.data(), "SPAS12_sp", sizeof("SPAS12_sp"));
    std::memcpy(c.assetPath.data(), "Objects/Weapons/Handheld/UL_shg_SPAS12/SP_shg_SPAS12",
                sizeof("Objects/Weapons/Handheld/UL_shg_SPAS12/SP_shg_SPAS12"));
    c.weaponData = 0xb0000;
    c.firingData = 0xc0000;
    c.primaryFire = 0xd0000;
    c.ammoAddress = c.primaryFire + 0x170;
    c.fireLogicType = 1;
    c.reloadType = 0;
    c.fireInputAction = 8;
    c.reloadInputAction = 29;
    c.baseCapacity = c.numberOfMagazines = 4;
    c.reloadDelay = .06f;
    c.reloadTime = .72f;
    c.reloadThreshold = c.postReloadTime = 1;
    c.boltDelay = .5f;
    i.context.deltaSeconds = .005f;
    i.context.reloadTimeMultiplier = 1;
    i.context.flags24Through28[0] = true;
    for (unsigned n = 0; n < 3; ++n)
    {
        auto &b = i.branches[n];
        b.address = i.identity.firing[n];
        b.wrapperOffset = n == 0 ? 0x3c : n == 1 ? 0x40 : 0x10;
        b.currentState = 11;
        b.nextState = 12;
        b.phaseTimer = .2f;
        b.loaded = 6;
        b.reserve = 21;
        i.capacities[n] = 8;
    }
    return i;
}
int CancellationOriginIsExact(){
    for(bool magazine:{false,true}){
        Bc2ReloadNativePolicy p;if(magazine)CHECK(p.ConfigureMagazine());
        CHECK(!p.CancellationOrigin()&&!p.CancelledFromArming());
        CHECK(magazine?p.StartMagazine(Control(),Unseat(),Now):p.Start(Control(),Now));
        CHECK(p.Phase()==ReloadRequestCyclePhase::Arming);p.Cancel();
        CHECK(p.CancelledFromArming());auto origin=p.CancellationOrigin();CHECK(origin);
        CHECK(origin->cycle==1&&origin->identity==Identity());
        CHECK(origin->family==(magazine?ReloadNativeFamily::Xm8Magazine:ReloadNativeFamily::SpasTube));
        p.Cancel(ReloadRequestCycleFailure::Owner);
        CHECK(p.CancelledFromArming()&&p.CancellationOrigin()->prior==ReloadRequestCyclePhase::Arming);
        CHECK(p.DrainCancelledInvocations(true));
        auto second=Unseat();second.id=2;CHECK(magazine?p.StartMagazine(Control(2),second,Now):p.Start(Control(2),Now));
        CHECK(!p.CancellationOrigin());
        auto bad=Control(2);bad.identity.owner.equipGeneration++;
        CHECK(!p.KeepAlive(bad,Now+1));CHECK(p.CancelledFromArming());
        CHECK(p.CancellationOrigin()->cycle==2&&p.CancellationOrigin()->identity==Identity());
    }
    Bc2ReloadNativePolicy held;CHECK(held.Start(Control(),Now));auto in=HoldingInput();
    for(unsigned branch=0;branch<3;++branch){
        in.branch=branch;in.nowNs+=1000;for(auto& capacity:in.capacities)capacity=8;
        const auto d=held.Evaluate(in,true,true,branch+1,in.nowNs);if(branch<2){CHECK(!d.tracked);continue;}CHECK(d.tracked&&d.hold);
        ReloadDeltaOverride patch;patch.applied=patch.restored=true;patch.original=1;
        CHECK(held.Finish(d,in.branches[branch],in.nowNs+100,true,patch));
    }
    CHECK(held.Phase()==ReloadRequestCyclePhase::Holding);held.Cancel();
    CHECK(!held.CancelledFromArming());CHECK(held.CancellationOrigin()->prior==ReloadRequestCyclePhase::Holding);
    held.Cancel();CHECK(!held.CancelledFromArming());
    Bc2ReloadNativePolicy idle;idle.Cancel();CHECK(!idle.CancelledFromArming());return 0;
}

int main(){if(CancellationOriginIsExact()||ExplicitRetirement()||IdleAndTypedRouting()||AbortNamespaces()||CallbackExclusion()||SessionIsolation()||PreviouslyRetiredFamilyCanBeRevisited()||SamePolicyProfileRevisitsPreserveRetirement()||NewCycleAndOwnerCannotBorrowOldRetirement()||RetiredDispatchFactIsNotFreshAmmoOrControl()||CurrentConfigGateFollowsRevisitedSelection())return 1;
    std::puts("10 reload family retirement, revisit, profile, owner, callback and session groups passed");return 0;}

