#include "Test.h"
#include "fvr/interaction/BoltWeaponCustody.h"
#include "fvr/interaction/WeaponCycleConvergence.h"
using namespace fvr;
using namespace fvr::interaction;
namespace {
constexpr std::int64_t Ms=1000000;
constexpr auto Left=InteractionHand::Left,Right=InteractionHand::Right;
struct Scope {std::array<std::uint64_t,3> firing{101,102,103};std::uint64_t descriptor=9,owner=8;
    bool operator==(const Scope&)const=default;};
struct NativeFixture {
    Scope scope{};WeaponCycleConvergence<Scope> convergence;AmmunitionLedger ledger;
    WeaponCycleDebt initial{{10,2,3,4},{20,3},{30,3},40,50,60,4,10,5};
    WeaponCycleReloadPause pause{initial.owner,initial.item,initial.mechanism,40,50,1,7,1,90,
        100*Ms,200*Ms,5000*Ms,110*Ms,30110*Ms,.1f,1.f};
    AmmoResourceContext resource{{10,2,20,6},3,4};
    std::uint64_t invocation=100,revision=1,sequence=100;
    std::int64_t now=110*Ms;
    NativeFixture(){convergence.Begin(scope,initial,Ms);}
    bool Pause(){return convergence.PrepareReload(scope,pause,resource,{revision,now},now);}
    bool Round(std::int64_t begin){const auto d=convergence.Debt();
        for(unsigned b=0;b<3;++b){WeaponCycleIdleUpdate u{b,++invocation,d.guardEpoch,begin,begin+Ms,begin+81*Ms,d.loaded,d.reserve,d.capacity};
            now=u.endNs;if(!convergence.ObserveIdle(scope,u,now))return false;begin+=2*Ms;}return true;}
    std::optional<WeaponCycleReloadCommand> Dispatch(AmmunitionOperation operation){
        const auto permit=convergence.ReloadPermit(scope,now);if(!permit)return {};
        ++now;const auto d=convergence.Debt();
        AmmunitionSnapshot s{resource,++sequence,now,now+80*Ms,{d.loaded,d.reserve,d.capacity},true};
        if(!ledger.Bind(s,now))return {};
        const auto command=ledger.Queue(operation,s,now,operation==AmmunitionOperation::ReturnMagazine?ledger.Original():std::nullopt);
        if(!command||!ledger.Dispatch(s,now)||!convergence.BeginReloadCommand(scope,*permit,*command,{++revision,now},now))return {};
        return convergence.ReloadCommand();
    }
    WeaponCycleReloadAmmoRebase Receipt(const WeaponCycleReloadCommand& command){
        WeaponCycleReloadAmmoRebase r;r.dispatched=command;
        r.receipt={command.command,900+command.command.id,now+Ms,now+20*Ms,command.command.before,command.command.after,true,true};
        for(unsigned b=0;b<3;++b){const auto begin=now+(2+b*2)*Ms;const auto a=command.command.after;
            r.ownUpdates[b]={b,++invocation,convergence.Debt().guardEpoch,begin,begin+Ms,begin+81*Ms,a.loaded,a.reserve,a.capacity};}
        now=r.receipt.completedNs;r.drain={++revision,now};r.guardEpoch=convergence.Debt().guardEpoch+1;return r;
    }
    bool Apply(const WeaponCycleReloadAmmoRebase& r){return ledger.Complete(r.receipt,now)&&convergence.ApplyReloadReceipt(scope,r,now);}
    HandInteractionSample Neutral(){return {initial.owner,++sequence,now+Ms,now+81*Ms,now+Ms,true,{true,true},{false,true}};}
    std::optional<WeaponCycleIdleDebtLease> Authority(){
        if(!Round(now+10*Ms)||!Round(now+10*Ms))return {};
        const auto n=Neutral();const auto g=convergence.ArmRecovery(scope,n,{Right,Left});if(!g)return {};
        if(!Round(now+10*Ms)||!Round(now+10*Ms))return {};
        return convergence.RecoveryAuthority(scope,*g,now);
    }
};
int FullReloadKeepsExactDebtAndRequiresFreshVotes(){
    NativeFixture f;CHECK(f.Pause());CHECK(!f.convergence.ReloadPermit(f.scope,f.now));
    CHECK(f.Round(120*Ms));CHECK(!f.convergence.ReloadPermit(f.scope,f.now));CHECK(f.Round(140*Ms));
    const auto remove=f.Dispatch(AmmunitionOperation::RemoveMagazine);CHECK(remove);
    const auto removed=f.Receipt(*remove);CHECK(f.Apply(removed));
    CHECK(f.convergence.Debt().loaded==0&&f.convergence.BlocksFire()&&!f.convergence.Outcome());
    CHECK(!f.convergence.ApplyReloadReceipt(f.scope,removed,f.now));CHECK(!f.convergence.ReloadPermit(f.scope,f.now));
    CHECK(f.ledger.Original()&&f.ledger.Discard(*f.ledger.Original()));
    CHECK(f.Round(180*Ms)&&f.Round(200*Ms));
    const auto refill=f.Dispatch(AmmunitionOperation::RefillMagazine);CHECK(refill);
    const auto filled=f.Receipt(*refill);CHECK(f.Apply(filled));
    const auto d=f.convergence.Debt();CHECK(d.loaded==5&&d.reserve==5&&d.capacity==5);
    CHECK(d.owner==f.initial.owner&&d.item==f.initial.item&&d.mechanism==f.initial.mechanism&&d.cycle==40&&d.shot==50);
    CHECK(d.ammunitionRebase&&d.ammunitionRebase->receipt.command.id==refill->command.id);
    CHECK(f.convergence.BlocksFire()&&!f.convergence.Outcome()&&!f.convergence.Pending());
    const auto authority=f.Authority();CHECK(authority&&weapon_cycle_detail::Lease(*authority,f.now));
    auto forged=*authority;forged.grant.debt.ammunitionRebase.reset();CHECK(!weapon_cycle_detail::Lease(forged,f.now));
    CHECK(f.convergence.ResumeReload(f.scope,*authority,f.now));CHECK(f.convergence.BlocksFire()&&!f.convergence.Outcome());
    WeaponCycleRecoveredRelease release{*authority,1,f.sequence+1,f.now+Ms,authority->deadlineNs};
    CHECK(f.convergence.SubmitRecovered(f.scope,release,f.now+Ms));
    CHECK(f.Round(f.now+10*Ms)&&!f.convergence.Outcome());CHECK(f.Round(f.now+10*Ms)&&f.convergence.Outcome());
    CHECK(f.convergence.Outcome()->native.debt.loaded==5&&f.convergence.Acknowledge(*f.convergence.Outcome()));
    CHECK(!f.convergence.BlocksFire());return 0;
}
int LegacyAdmissionRemainsPostShotOnly(){
    NativeFixture f;auto full=f.initial;full.loaded=full.capacity;
    WeaponCycleConvergence<Scope> fresh;CHECK(!fresh.Begin(f.scope,full,Ms));
    WeaponCycleLease legacy{full.owner,full.item,full.mechanism,full.cycle,full.shot,9,100*Ms,200*Ms,false,
        WeaponCycleGuardedAdvance{60,{1,2,3},5,10,5}};
    CHECK(!weapon_cycle_detail::Lease(legacy,110*Ms));return 0;
}
int LossDuringDispatchRetainsReceiptAndRejectsWrongScope(){
    NativeFixture f;CHECK(f.Pause()&&f.Round(120*Ms)&&f.Round(140*Ms));
    const auto command=f.Dispatch(AmmunitionOperation::RemoveMagazine);CHECK(command);
    auto receipt=f.Receipt(*command);auto foreign=f.scope;++foreign.firing[1];
    f.convergence.Interrupt(f.scope,f.now);CHECK(f.convergence.ReloadCommand()==command);
    CHECK(!f.convergence.ApplyReloadReceipt(foreign,receipt,f.now));
    auto changed=receipt;++changed.receipt.command.context.resource.weaponGeneration;
    CHECK(!f.convergence.ApplyReloadReceipt(f.scope,changed,f.now));
    // A partially propagated count never runs through old cycle comparisons.
    WeaponCycleIdleUpdate partial{0,++f.invocation,60,f.now,f.now+Ms,f.now+80*Ms,0,10,5};
    CHECK(!f.convergence.ObserveIdle(f.scope,partial,f.now+Ms));
    CHECK(f.convergence.Phase()==WeaponCycleConvergencePhase::Debt&&f.convergence.BlocksFire());
    CHECK(f.Apply(receipt));CHECK(f.convergence.LastReloadReceipt()==receipt.receipt);
    CHECK(!f.convergence.ReloadCommand()&&!f.convergence.Outcome());
    auto otherOwner=f.initial;otherOwner.owner.actorGeneration++;
    CHECK(!f.convergence.Begin(foreign,otherOwner,f.now));return 0;
}
int BadOwnUpdateAndRepeatedCommandCannotRebase(){
    NativeFixture f;CHECK(f.Pause()&&f.Round(120*Ms)&&f.Round(140*Ms));
    const auto command=f.Dispatch(AmmunitionOperation::RemoveMagazine);CHECK(command);
    const auto valid=f.Receipt(*command);
    for(unsigned variant=0;variant<7;++variant){auto r=valid;
        switch(variant){case 0:r.ownUpdates[2].invocation=r.ownUpdates[0].invocation;break;
        case 1:r.ownUpdates[1].branch=0;break;case 2:r.ownUpdates[1].loaded=4;break;
        case 3:r.receipt.authorityVerified=false;break;case 4:r.receipt.completedNs=r.receipt.command.deadlineNs;break;
        case 5:r.guardEpoch=f.initial.guardEpoch;break;case 6:++r.dispatched.permit.debt.shot;break;}
        CHECK(!f.convergence.ApplyReloadReceipt(f.scope,r,f.now));CHECK(f.convergence.Debt()==f.initial);
    }
    CHECK(f.Apply(valid));CHECK(!f.convergence.ApplyReloadReceipt(f.scope,valid,f.now));
    CHECK(f.Round(180*Ms)&&f.Round(200*Ms));const auto permit=f.convergence.ReloadPermit(f.scope,f.now);CHECK(permit);
    CHECK(!f.convergence.BeginReloadCommand(f.scope,*permit,command->command,{++f.revision,f.now},f.now));return 0;
}
int OriginalReturnDoesNotSpendOwedStroke(){
    NativeFixture f;CHECK(f.Pause()&&f.Round(120*Ms)&&f.Round(140*Ms));
    const auto remove=f.Dispatch(AmmunitionOperation::RemoveMagazine);CHECK(remove);CHECK(f.Apply(f.Receipt(*remove)));
    CHECK(f.Round(180*Ms)&&f.Round(200*Ms));const auto back=f.Dispatch(AmmunitionOperation::ReturnMagazine);CHECK(back);
    CHECK(f.Apply(f.Receipt(*back)));CHECK(f.convergence.Debt().loaded==4&&f.convergence.Debt().reserve==10);
    CHECK(f.convergence.BlocksFire()&&!f.convergence.Outcome()&&f.ledger.ResourceState()==MagazineResourceState::Returned);return 0;
}
math::Matrix4 Identity(){math::Matrix4 m{};for(unsigned n=0;n<4;++n)m.values[n][n]=1;return m;}
WeaponCycleProfile Profile(){WeaponCycleProfile p;p.id=7;p.revision=1;p.family=WeaponCycleFamily::Bolt;
    p.closedContact=Identity();p.stroke=.1f;p.rearTolerance=p.frontTolerance=.002f;p.contactRadius=.02f;
    p.lateralTolerance=.01f;p.unlockRadians=1.f;p.rotationTolerance=.03f;p.maxStepMeters=.04f;
    p.endpointDwellNs=10*Ms;p.maximumCycleNs=1000*Ms;p.hands={Right,Left};return p;}
struct HandsFixture {
    HandInteraction hands;BoltWeaponCustody custody;WeaponCycleProfile profile=Profile();
    HandInteractionSample source{{10,2,3,4},1,100*Ms,200*Ms,100*Ms,true,{true,true},{false,false}};
    WeaponCycleLease lease{source.owner,{20,3},{30,3},40,50,1,100*Ms,200*Ms,true};
    std::uint64_t intent=0;math::Matrix4 wrist=Identity();
    HandClaimRequest Request(InteractionHand hand,HandClaimKind kind,std::uint64_t contact,std::uint64_t parent=0){
        const auto item=kind==HandClaimKind::AmmoObject?HandInteractionKey{contact,3}:lease.item;
        return {source.owner,hand,kind,item,{{contact,3},source.sequence,source.deadlineNs,true},++intent,parent};}
    void Next(){source.sequence++;source.nowNs+=10*Ms;source.observedNs=source.nowNs;source.deadlineNs=source.nowNs+100*Ms;
        lease.sequence++;lease.observedNs=source.observedNs;lease.deadlineNs=source.deadlineNs;}
    bool Enter(){const auto gun=hands.Acquire(source,Request(Right,HandClaimKind::GunHold,1));if(!gun.claim)return false;
        const auto support=hands.Acquire(source,Request(Left,HandClaimKind::WeaponSupport,2,gun.claim->token.id));if(!support.claim)return false;
        if(!custody.Arm(profile,lease,Identity(),source.nowNs))return false;
        HandGunCustodyTransfer t{gun.claim->token,support.claim->token,{Request(Left,HandClaimKind::GunHold,2),source}};
        return custody.EnterCustody(source,t,wrist,wrist,hands).transaction.accepted;}
    BoltCustodyResult Step(float travel,float angle,bool grip=true){Next();source.released[1]=!grip;
        BoltCustodySample s;s.source=source;s.lease=lease;s.gunWristInWorld=wrist;
        s.mechanismWristInWeapon=weapon_cycle_detail::Target(profile,travel,angle);s.mechanismGrip=grip;s.acquireIntent=++intent;
        s.gunContact={custody.Gun()->contact,source.sequence,source.deadlineNs,true};s.mechanismContact={lease.mechanism,source.sequence,source.deadlineNs,true};
        return custody.Update(s,hands);}
    bool Open(){if(!Enter())return false;Step(0,0,false);Step(0,0);
        Step(0,1);Step(0,1);Step(.03f,1);Step(.06f,1);Step(.09f,1);Step(.1f,1);Step(.1f,1);
        return custody.CyclePhase()==WeaponCyclePhase::Forward;}
    std::optional<WeaponCycleReloadPause> Pause(){Next();source.released[1]=true;return custody.PauseForReload(source,hands);}
    bool ReturnForMagazine(){Next();source.released[1]=false;
        HandGunCustodyTransfer t{*custody.Gun(),std::nullopt,{Request(Right,HandClaimKind::GunHold,1),source}};
        return custody.PauseCustody(source,t,wrist,wrist,hands).transaction.accepted;}
};
int OpenPauseRequiresExplicitReturnAndFreshSupport(){
    HandsFixture f;CHECK(f.Open());const auto pause=f.Pause();CHECK(pause);
    CHECK(f.custody.Custody()==BoltCustodyPhase::ReloadPaused&&f.custody.PausedContact());
    CHECK(!f.hands.Current(Right)&&f.hands.Current(Left));CHECK(f.ReturnForMagazine());
    CHECK(f.custody.Custody()==BoltCustodyPhase::Reloading&&!f.hands.Current(Left)&&f.hands.Current(Right));
    f.Next();const auto mag=f.hands.Acquire(f.source,f.Request(Left,HandClaimKind::AmmoObject,99));CHECK(mag.claim);
    f.Next();f.source.released[1]=true;
    HandGunCustodyTransfer bad{*f.custody.Gun(),mag.claim->token,{f.Request(Left,HandClaimKind::GunHold,2),f.source},std::nullopt,true};
    CHECK(!f.custody.ResumeCustody(f.source,bad,f.wrist,f.wrist,f.hands).transaction.accepted);
    CHECK(f.custody.Custody()==BoltCustodyPhase::Reloading&&f.custody.ReloadPause()==pause);
    f.source.released[1]=false;f.Next();CHECK(f.hands.Release(f.source,mag.claim->token).accepted);
    f.Next();const auto support=f.hands.Acquire(f.source,f.Request(Left,HandClaimKind::WeaponSupport,2,f.custody.Gun()->id));CHECK(support.claim);
    f.Next();f.source.released[1]=true;
    HandGunCustodyTransfer resume{*f.custody.Gun(),support.claim->token,{f.Request(Left,HandClaimKind::GunHold,2),f.source},std::nullopt,true};
    CHECK(f.custody.ResumeCustody(f.source,resume,f.wrist,f.wrist,f.hands).transaction.accepted);
    CHECK(f.custody.Custody()==BoltCustodyPhase::ReloadResumePending&&f.custody.CyclePhase()==WeaponCyclePhase::ReloadPaused);
    CHECK(!f.hands.Current(Right)&&f.hands.Current(Left));return 0;
}
int TrackingLossCannotClearPausedDebtOrBorrowNewGun(){
    HandsFixture f;CHECK(f.Open());const auto pause=f.Pause();CHECK(pause&&f.ReturnForMagazine());
    f.Next();f.source.tracked[0]=false;f.hands.Update(f.source);
    CHECK(f.custody.ReloadPause()==pause&&f.custody.CyclePhase()==WeaponCyclePhase::ReloadPaused);
    f.Next();f.source.tracked[0]=true;f.source.owner.actorGeneration++;
    CHECK(!f.custody.PauseForReload(f.source,f.hands));
    CHECK(f.custody.ReloadPause()==pause&&f.custody.PausedContact());return 0;
}
int ResumeRetainsOpenMilestoneAndRemainingActiveBudget(){
    WeaponCycle cycle;const auto profile=Profile();
    HandInteractionSample input{{10,2,3,4},1,100*Ms,200*Ms,100*Ms,true,{true,true},{false,true}};
    WeaponCycleLease held{input.owner,{20,3},{30,3},40,50,1,100*Ms,200*Ms,true};CHECK(cycle.Begin(profile,held,input.nowNs));
    const auto step=[&](float travel,float rotation,bool grip){input.sequence++;input.observedNs+=10*Ms;
        input.nowNs=input.observedNs;input.deadlineNs=input.nowNs+100*Ms;input.released[1]=!grip;
        held.sequence++;held.observedNs=input.observedNs;held.deadlineNs=input.deadlineNs;
        return cycle.Update({input,weapon_cycle_detail::Target(profile,travel,rotation),grip,grip},held);};
    step(0,0,false);step(0,0,true);step(0,1,true);step(0,1,true);
    step(.03f,1,true);step(.06f,1,true);step(.09f,1,true);step(.1f,1,true);step(.1f,1,true);
    CHECK(cycle.Phase()==WeaponCyclePhase::Forward);
    input.sequence++;input.nowNs+=10*Ms;input.observedNs=input.nowNs;input.deadlineNs=input.nowNs+100*Ms;input.released[1]=true;
    const auto pause=cycle.PauseForReload({input,{},false,false});CHECK(pause);
    CHECK(!cycle.PauseForReload({input,{},false,false}));
    CHECK(cycle.Update({input,Identity(),true,true},held).phase==WeaponCyclePhase::ReloadPaused);
    NativeFixture f;f.pause=*pause;f.now=input.nowNs;CHECK(f.Pause());
    CHECK(f.Round(f.now+10*Ms)&&f.Round(f.now+10*Ms));auto remove=f.Dispatch(AmmunitionOperation::RemoveMagazine);CHECK(remove);
    CHECK(f.Apply(f.Receipt(*remove)));CHECK(f.ledger.Discard(*f.ledger.Original()));
    CHECK(f.Round(f.now+10*Ms)&&f.Round(f.now+10*Ms));auto refill=f.Dispatch(AmmunitionOperation::RefillMagazine);CHECK(refill);
    CHECK(f.Apply(f.Receipt(*refill)));
    // Spend longer reloading than the one-second active stroke limit. Native
    // observations are new; neither the old input nor held lease is restamped.
    f.now=5000*Ms;f.sequence=input.sequence+100;const auto authority=f.Authority();CHECK(authority);
    auto neutral=f.Neutral();neutral.nowNs=neutral.observedNs;
    WeaponCycleIdleDebt resumed;auto wrong=*authority;++wrong.grant.debt.ammunitionRebase->pause;
    CHECK(!resumed.ResumeAfterReload(cycle,wrong,neutral));
    CHECK(resumed.ResumeAfterReload(cycle,*authority,neutral));CHECK(resumed.Phase()==WeaponCyclePhase::AwaitingGrip);
    CHECK(weapon_cycle_detail::Distance(resumed.AcquisitionContact(),weapon_cycle_detail::Target(profile,.1f,1))<.0001f);
    auto duplicate=resumed.Update({neutral,weapon_cycle_detail::Target(profile,.1f,1),true,true},*authority);
    CHECK(duplicate.phase==WeaponCyclePhase::AwaitingGrip&&!duplicate.release);
    const auto move=[&](float travel,float rotation){neutral.sequence++;neutral.observedNs+=5*Ms;
        neutral.nowNs=neutral.observedNs;neutral.deadlineNs=neutral.nowNs+80*Ms;neutral.released[1]=false;
        return resumed.Update({neutral,weapon_cycle_detail::Target(profile,travel,rotation),true,true},*authority);};
    CHECK(move(.1f,1).phase==WeaponCyclePhase::Forward);
    move(.07f,1);move(.04f,1);move(.01f,1);move(0,1);move(0,1);CHECK(move(0,1).phase==WeaponCyclePhase::Lock);
    move(0,0);move(0,0);const auto released=move(0,0);
    CHECK(released.phase==WeaponCyclePhase::AwaitingNative&&released.release&&released.release->cycle.grant.debt.loaded==5);
    CHECK(released.release->cycle.cycle==held.cycle&&released.release->cycle.shot==held.shot);
    // An expired native lease cannot be reused just because pause extended the
    // active gesture budget, and the completed gesture still blocks Fire.
    CHECK(released.blocksFire&&!weapon_cycle_detail::Lease(*authority,authority->deadlineNs));return 0;
}
}
int main(){if(FullReloadKeepsExactDebtAndRequiresFreshVotes()||LegacyAdmissionRemainsPostShotOnly()||
    LossDuringDispatchRetainsReceiptAndRejectsWrongScope()||BadOwnUpdateAndRepeatedCommandCannotRebase()||
    OriginalReturnDoesNotSpendOwedStroke()||OpenPauseRequiresExplicitReturnAndFreshSupport()||
    TrackingLossCannotClearPausedDebtOrBorrowNewGun()||ResumeRetainsOpenMilestoneAndRemainingActiveBudget())return 1;return 0;}
