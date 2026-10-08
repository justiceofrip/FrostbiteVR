#include "Bc2BoltPart.h"
#include "Bc2M95BoltCalibration.h"
#include "Test.h"
#include <cstring>
#include "fvr/interaction/WeaponCycleConvergence.h"
using namespace fvr;using namespace fvr::bc2;using namespace fvr::interaction;
namespace {
constexpr std::int64_t ms=1000000;constexpr auto left=InteractionHand::Left,right=InteractionHand::Right;
math::Matrix4 Identity(){math::Matrix4 m{};for(unsigned n=0;n<4;++n)m.values[n][n]=1;return m;}
bool Equal(const math::Matrix4& a,const math::Matrix4& b){for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)if(std::abs(a.values[r][c]-b.values[r][c])>1e-5f)return false;return true;}
struct NativeBase {
    Bc2NativeCycleView view{};unsigned controls=0,releases=0,acks=0,cancels=0;bool rejectRelease=false;
    std::optional<WeaponCycleRelease> release;
    Bc2PhysicalBoltApi Api(){return {this,
        [](void* p,const Bc2NativeCycleControl& c)noexcept{auto& n=*static_cast<NativeBase*>(p);++n.controls;
            if(c.release){++n.releases;n.release=c.release;n.view.phase=Bc2NativeCyclePhase::Releasing;n.view.held.reset();return !n.rejectRelease;}return true;},
        [](void* p,std::int64_t)noexcept->std::optional<Bc2NativeCycleView>{return static_cast<NativeBase*>(p)->view;},
        [](void* p,const WeaponCycleReady& r)noexcept{auto& n=*static_cast<NativeBase*>(p);if(!n.view.ready||n.view.ready->release!=r.release||n.view.ready->sequence!=r.sequence)return false;
            ++n.acks;n.view.ready.reset();n.view.phase=Bc2NativeCyclePhase::Complete;n.view.blocksFire=false;return true;},
        [](void* p)noexcept{++static_cast<NativeBase*>(p)->cancels;}};}
};
struct Native:NativeBase {
    bool recoveryMode=false,blockRecoveryAck=false;unsigned recoveryReleased=0,recoveryAcked=0;
    WeaponCycleConvergence<ReloadHoldIdentity> convergence;
    WeaponCycleDebt debt{};std::optional<WeaponCycleRecoveryGrant> grant;
    std::uint64_t invocation=1000;std::int64_t clock=0;
    void Recover(const Bc2PhysicalBoltSample& s){
        recoveryMode=true;view.native.owner=s.nativeOwner;view.held.reset();view.phase=Bc2NativeCyclePhase::Cancelled;view.blocksFire=true;
        debt={s.input.owner,s.item,{M95AuthoredBoltCalibration(1).profile.id,s.item.generation},1,1,999,4,45,5};
        convergence.Begin(view.native,debt,s.input.nowNs);clock=s.input.nowNs;
    }
    void Originals(const Bc2NativeCycleControl& c){
        clock=c.input.nowNs;
        // Explicit mocked original neutral idle boundary, preserving all three
        // copy counts. This callback never alters the real gesture or grants it.
        for(unsigned round=0;round<2;++round)for(unsigned branch=0;branch<3;++branch){
            const auto at=clock-2000000+round*1000000+branch*100000;
            convergence.ObserveIdle(view.native,{branch,++invocation,debt.guardEpoch,at,at+50000,at+80000000,
                debt.loaded,debt.reserve,debt.capacity},clock);
        }
        if(!grant)grant=convergence.ArmRecovery(view.native,c.input,{right,left});
    }
    Bc2PhysicalBoltApi Api(){auto api=NativeBase::Api();api.context=this;
        api.control=[](void* p,const Bc2NativeCycleControl& c)noexcept{auto& n=*static_cast<Native*>(p);
            if(!n.recoveryMode){const auto base=n.NativeBase::Api();return base.control(base.context,c);}
            ++n.controls;if(c.release)return false;n.Originals(c);return true;};
        api.cancel=[](void* p)noexcept{auto& n=*static_cast<Native*>(p);++n.cancels;
            if(n.recoveryMode){n.convergence.Interrupt(n.view.native,n.clock);n.grant.reset();}};
        api.recovery={
            [](void* p,std::int64_t now)noexcept->std::optional<Bc2NativeCycleRecoveryView>{auto& n=*static_cast<Native*>(p);if(!n.recoveryMode)return {};
                Bc2NativeCycleRecoveryView out;out.native=n.view.native;out.blocksFire=n.convergence.BlocksFire();
                if(n.grant)out.authority=n.convergence.RecoveryAuthority(n.view.native,*n.grant,now);
                if(const auto& ready=n.convergence.Outcome();ready&&std::holds_alternative<WeaponCycleIdleDebtRelease>(ready->release))
                    out.ready=WeaponCycleIdleDebtReady{std::get<WeaponCycleIdleDebtRelease>(ready->release),
                        *std::max_element(ready->native.second.begin(),ready->native.second.end()),ready->native.observedNs,ready->native.deadlineNs,true,true};
                return out;},
            [](void* p,const WeaponCycleIdleDebtRelease& r,const HandInteractionSample& input)noexcept{auto& n=*static_cast<Native*>(p);
                if(!n.convergence.SubmitRecovered(n.view.native,r,input.nowNs))return false;++n.recoveryReleased;return true;},
            [](void* p,const WeaponCycleIdleDebtReady& r)noexcept{auto& n=*static_cast<Native*>(p);const auto& outcome=n.convergence.Outcome();
                if(n.blockRecoveryAck||!outcome||!std::holds_alternative<WeaponCycleIdleDebtRelease>(outcome->release)||
                   std::get<WeaponCycleIdleDebtRelease>(outcome->release)!=r.release||!n.convergence.Acknowledge(*outcome))return false;
                ++n.recoveryAcked;n.view.blocksFire=false;n.view.phase=Bc2NativeCyclePhase::Complete;return true;}};
        return api;
    }
};
struct Fixture {
    Native native;HandInteraction hands;RigSnapshot rig;std::shared_ptr<Bc2BoltCalibration> calibration;
    std::unique_ptr<Bc2PhysicalBolt> bolt;Bc2PhysicalBoltSample sample;Bc2PhysicalBoltResult last;
    HandClaimToken originalGun{},originalSupport{};std::uint64_t intent=10;bool entered=false;
    Fixture(){
        rig.names={"root","jntWpn_1","jntWpn_3","other"};rig.parents={-1,0,1,0};rig.weaponBone=1;
        rig.identity={0x20000,0x30000,0x50000,0x60000,0x70000,0x80000,0x90000,0xa0000,0xb0000,4,false};
        rig.world.assign(4,Identity());rig.inverseBind.assign(4,Identity());rig.evaluatedWorld.assign(4,Identity());rig.nativeEvaluated.resize(4);
        for(auto& bytes:rig.nativeEvaluated)bytes.fill(std::byte{0x5a});
        calibration=std::make_shared<Bc2BoltCalibration>(M95AuthoredBoltCalibration(1));
        // Synthetic callback/rig fixture only: this does not promote the actual
        // M95 authored reference to a runtime calibration or native admission.
        calibration->nativeJoined=true;calibration->rigFingerprint=DeriveBoltPart(rig,"jntWpn_3")->fingerprint;
        sample.nativeOwner={0x10000,0x20000,0x30000,0x40000,2,7,4};
        sample.input={{(std::uint64_t(0x30000)<<32)|0x20000,2,3,4},1,1000*ms,1150*ms,1000*ms,true,{true,true},{false,false}};
        sample.item={0x40000,3};sample.asset=calibration->asset;sample.mesh=calibration->mesh;
        sample.weaponWorld=Identity();sample.weaponWorld.values[3][1]=1.2f;sample.weaponWorld.values[3][2]=-.3f;
        sample.wristWorld={Identity(),Identity()};sample.wristWorld[0].values[3][0]=-.2f;sample.wristWorld[1].values[3][0]=.1f;
        auto gun=hands.Acquire(sample.input,Request(right,HandClaimKind::GunHold,1));originalGun=gun.claim->token;
        auto support=hands.Acquire(sample.input,Request(left,HandClaimKind::WeaponSupport,2,originalGun.id));originalSupport=support.claim->token;
        native.view.phase=Bc2NativeCyclePhase::Held;native.view.blocksFire=true;
        native.view.native.owner=sample.nativeOwner;
        native.view.held=WeaponCycleLease{sample.input.owner,sample.item,{calibration->profile.id,3},1,1,1,sample.input.observedNs,sample.input.deadlineNs,true};
        bolt=std::make_unique<Bc2PhysicalBolt>(calibration,native.Api());
    }
    HandClaimRequest Request(InteractionHand hand,HandClaimKind kind,std::uint64_t contact,std::uint64_t parent=0){return
        {sample.input.owner,hand,kind,sample.item,{{contact,3},sample.input.sequence,sample.input.deadlineNs,true},++intent,parent};}
    void Next(){auto& s=sample.input;++s.sequence;s.nowNs+=10*ms;s.observedNs=s.nowNs;s.deadlineNs=s.nowNs+150*ms;
        if(native.view.held){++native.view.held->sequence;native.view.held->observedNs=s.observedNs;native.view.held->deadlineNs=s.deadlineNs;}}
    Bc2PhysicalBoltResult Tick(const math::Matrix4& part,bool grip=true,bool advance=true){
        if(advance)Next();sample.grip=grip;sample.input.released[1]=!grip;
        sample.raw={sample.nativeOwner,rig.identity,calibration->rigFingerprint,sample.input,M95BoltWristFromPart(part),true};
        const auto hand=last.tracking.custody==BoltCustodyPhase::Returned?right:left;
        const auto gun=hands.Current(entered?hand:right);
        const auto contact=sample.custodyTransfer?sample.custodyTransfer->nextGun.request.contact.key:gun?gun->token.contact:HandInteractionKey{};
        sample.gunContact={contact,sample.input.sequence,sample.input.deadlineNs,true};
        if(!entered&&gun)hands.Renew(sample.input,gun->token,sample.gunContact);
        last=bolt->Tick(sample,hands,intent);sample.custodyTransfer.reset();return last;
    }
    int Enter(){
        sample.custodyTransfer=HandGunCustodyTransfer{originalGun,originalSupport,{Request(left,HandClaimKind::GunHold,2),sample.input},{}};
        const auto r=Tick(calibration->profile.closedContact,true,false);
        CHECK(r.custodyChange&&r.custodyChange->transaction.accepted&&r.ownsGunCustody&&!hands.Current(right)&&hands.Current(left));entered=true;return 0;
    }
    int Stroke(){
        auto r=Tick(calibration->profile.closedContact,false);CHECK(r.blocksFire&&r.tracking.weapon&&!r.ownsMechanism); for(unsigned n=0;n<5;++n)r=Tick(calibration->profile.closedContact,false);
        r=Tick(calibration->profile.closedContact);CHECK(r.ownsMechanism&&(r.tracking.target||r.tracking.recoveryTarget));
        const auto& p=calibration->profile;
        auto step=[&](float travel,float angle){return Tick(weapon_cycle_detail::Target(p,travel,angle));};
        for(unsigned n=1;n<=4;++n)step(0,p.unlockRadians*float(n)/4);
        for(unsigned n=0;n<5;++n)step(0,p.unlockRadians);
        for(unsigned n=1;n<=8;++n)step(p.stroke*float(n)/8,p.unlockRadians);
        for(unsigned n=0;n<5;++n)step(p.stroke,p.unlockRadians);
        for(unsigned n=1;n<=8;++n)step(p.stroke*(1.f-float(n)/8),p.unlockRadians);
        for(unsigned n=0;n<5;++n)step(0,p.unlockRadians);
        for(unsigned n=1;n<=4;++n)step(0,p.unlockRadians*(1.f-float(n)/4));
        for(unsigned n=0;n<5;++n)step(0,0);
        CHECK(native.recoveryReleased==1&&!native.releases&&!hands.Current(right)&&hands.Current(left)&&last.blocksFire);return 0;
    }
    int Return(){
        Tick(calibration->profile.closedContact,false);Next();sample.input.released[1]=false;
        sample.custodyTransfer=HandGunCustodyTransfer{hands.Current(left)->token,{},
            {Request(right,HandClaimKind::GunHold,1),sample.input},HandCustodyTarget{Request(left,HandClaimKind::WeaponSupport,2),sample.input}};
        auto r=Tick(calibration->profile.closedContact,true,false);
        CHECK(r.custodyChange&&r.custodyChange->transaction.accepted&&r.custodyChange->companion&&r.tracking.weapon);
        CHECK(hands.Current(right)->token.kind==HandClaimKind::GunHold&&hands.Current(left)->token.prerequisiteClaim==hands.Current(right)->token.id);return 0;
    }
    void Ready(){native.view.ready=WeaponCycleReady{*native.release,sample.input.sequence+1,sample.input.nowNs,sample.input.nowNs+50*ms,true,true};}
};

int RecoveryRetainsCurrentLeftGunAndRequiresWholeBoltGesture(){
    Fixture f;CHECK(f.Enter()==0);f.Tick(f.calibration->profile.closedContact,false);f.Tick(f.calibration->profile.closedContact);
    const auto originalLeft=f.hands.Current(left)->token;
    f.sample.input.tracked[1]=false;f.Tick(f.calibration->profile.closedContact);
    CHECK(f.last.blocksFire&&!f.last.tracking.target&&!f.hands.Current(right)&&f.hands.Current(left));
    f.sample.input.tracked[1]=true;f.native.Recover(f.sample);
    for(unsigned n=0;n<5;++n)f.Tick(f.calibration->profile.closedContact,false);
    CHECK(f.native.grant&&f.last.ownsGunCustody&&f.hands.Current(left)->token==originalLeft&&!f.native.recoveryReleased);
    auto r=f.Tick(f.calibration->profile.closedContact);CHECK(r.ownsMechanism&&r.tracking.recoveryTarget&&!r.tracking.target&&!r.tracking.held);
    const auto before=f.rig.nativeEvaluated;
    CHECK(BuildBoltPartPlan(r.tracking,f.rig,r.tracking.weapon->weaponInWorld,1,f.sample.input.nowNs));
    CHECK(f.rig.nativeEvaluated==before);
    // A partial rear motion without real unlocking cannot discharge debt.
    r=f.Tick(weapon_cycle_detail::Target(f.calibration->profile,.01f,0));
    CHECK(!f.native.recoveryReleased&&!f.native.recoveryAcked&&r.blocksFire);
    CHECK(f.Stroke()==0);
    CHECK(f.Return()==0);
    for(unsigned n=0;n<8;++n)f.Tick(f.calibration->profile.closedContact);
    CHECK(f.native.recoveryReleased==1&&f.native.recoveryAcked==1&&!f.native.releases&&!f.native.acks&&!f.last.blocksFire);
    CHECK(f.last.tracking.weapon&&f.last.tracking.custody==BoltCustodyPhase::Returned&&f.native.debt.loaded==4&&f.native.debt.reserve==45);
    const auto returned=f.last.tracking.weapon->weaponInWorld;
    for(unsigned n=0;n<25;++n){f.sample.wristWorld[1].values[3][0]+=.001f;r=f.Tick(f.calibration->profile.closedContact);
        CHECK(r.tracking.weapon&&!r.tracking.recoveryTarget&&!r.tracking.target&&!r.blocksFire);
        CHECK(Near(r.tracking.weapon->weaponInWorld.values[3][0],returned.values[3][0]+.001f*float(n+1)));}
    f.native.recoveryMode=false;f.native.view.blocksFire=true;f.native.view.phase=Bc2NativeCyclePhase::Held;
    f.native.view.held=WeaponCycleLease{f.sample.input.owner,f.sample.item,{f.calibration->profile.id,3},2,2,200,
        f.sample.input.observedNs,f.sample.input.deadlineNs,true};
    r=f.Tick(f.calibration->profile.closedContact);
    CHECK(r.blocksFire&&r.tracking.weapon&&r.tracking.custody==BoltCustodyPhase::Returned&&!r.tracking.target&&!r.tracking.recoveryTarget);
    const auto rightToken=f.hands.Current(right)->token;
    f.sample.custodyTransfer=HandGunCustodyTransfer{rightToken,f.originalSupport,
        {f.Request(right,HandClaimKind::GunHold,1),f.sample.input},{}};
    r=f.Tick(f.calibration->profile.closedContact);
    CHECK(r.tracking.weapon&&r.tracking.custody==BoltCustodyPhase::Returned&&f.hands.Current(right)->token==rightToken&&r.blocksFire);
    f.Next();f.hands.Update(f.sample.input);
    auto support=f.hands.Acquire(f.sample.input,f.Request(left,HandClaimKind::WeaponSupport,2,rightToken.id));CHECK(support.accepted&&support.claim);
    f.originalGun=rightToken;f.originalSupport=support.claim->token;f.entered=false;
    CHECK(f.Enter()==0);CHECK(f.last.tracking.held&&f.last.tracking.held->cycle==2&&!f.last.tracking.recovery);
    return 0;
}
int ExpiredCustodyRequiresActualFreshGunAndAtomicExchange(){
    Fixture f;CHECK(f.Enter()==0);f.Tick(f.calibration->profile.closedContact,false);f.Tick(f.calibration->profile.closedContact);
    const auto old=f.hands.Current(left)->token;f.sample.input.tracked={false,false};f.Tick(f.calibration->profile.closedContact,false);
    f.sample.input.nowNs+=61000000000ll;f.Tick(f.calibration->profile.closedContact,false);
    CHECK(!f.hands.Current(left)&&!f.hands.Current(right)&&f.last.blocksFire);
    f.sample.input.tracked={true,true};f.native.Recover(f.sample);
    for(unsigned n=0;n<5;++n)f.Tick(f.calibration->profile.closedContact,false);
    CHECK(!f.last.ownsGunCustody&&!f.last.tracking.weapon&&!f.native.recoveryReleased);
    // This is ordinary physical reacquisition, not a synthetic restoration of
    // the expired token. New claim ids and real exchange are mandatory.
    f.Next();f.sample.input.released={false,false};f.hands.Update(f.sample.input);
    auto gun=f.hands.Acquire(f.sample.input,f.Request(right,HandClaimKind::GunHold,1));CHECK(gun.accepted&&gun.claim);
    auto support=f.hands.Acquire(f.sample.input,f.Request(left,HandClaimKind::WeaponSupport,2,gun.claim->token.id));CHECK(support.accepted&&support.claim);
    f.originalGun=gun.claim->token;f.originalSupport=support.claim->token;f.entered=false;
    CHECK(f.Enter()==0);CHECK(f.hands.Current(left)->token!=old);
    CHECK(f.Stroke()==0&&f.Return()==0);
    for(unsigned n=0;n<8;++n)f.Tick(f.calibration->profile.closedContact);
    CHECK(f.native.recoveryReleased==1&&f.native.recoveryAcked==1&&!f.last.blocksFire&&f.native.debt.shot==1);return 0;
}
int SubmittedOutcomeSurvivesLongTrackingLossWithoutRevivingClaims(){
    Fixture f;CHECK(f.Enter()==0);f.Tick(f.calibration->profile.closedContact,false);
    f.sample.input.tracked[1]=false;f.Tick(f.calibration->profile.closedContact);f.sample.input.tracked[1]=true;
    f.native.Recover(f.sample);for(unsigned n=0;n<5;++n)f.Tick(f.calibration->profile.closedContact,false);
    f.native.blockRecoveryAck=true;CHECK(f.Stroke()==0);CHECK(f.native.convergence.Outcome()&&!f.native.recoveryAcked);
    const auto outcome=*f.native.convergence.Outcome();
    f.sample.input.tracked={false,false};f.sample.input.nowNs+=61000000000ll;f.Tick(f.calibration->profile.closedContact);
    CHECK(f.native.convergence.Outcome()->native.observedNs==outcome.native.observedNs&&!f.hands.Current(left)&&!f.hands.Current(right));
    f.native.blockRecoveryAck=false;auto result=f.Tick(f.calibration->profile.closedContact);
    CHECK(result.recovered&&f.native.recoveryAcked==1&&result.recovered->observedNs==outcome.native.observedNs);
    CHECK(!result.ownsGunCustody&&!result.tracking.weapon&&!f.hands.Current(left)&&!f.hands.Current(right));
    CHECK(f.native.convergence.Pending()==std::optional<WeaponCycleConvergenceRelease>{outcome.release});return 0;
}
int RecoveryNeedsActualRightNeutralAndExactTypedPrivateTarget(){
    Fixture f;CHECK(f.Enter()==0);f.Tick(f.calibration->profile.closedContact,false);f.Tick(f.calibration->profile.closedContact);
    f.sample.input.tracked[1]=false;f.Tick(f.calibration->profile.closedContact);f.sample.input.tracked[1]=true;f.native.Recover(f.sample);
    for(unsigned n=0;n<8;++n)f.Tick(f.calibration->profile.closedContact);
    CHECK(!f.native.grant&&!f.last.tracking.recoveryTarget&&!f.native.recoveryReleased);
    for(unsigned n=0;n<5;++n)f.Tick(f.calibration->profile.closedContact,false);
    auto result=f.Tick(f.calibration->profile.closedContact);CHECK(result.tracking.recoveryTarget&&result.tracking.weapon);
    for(unsigned mutation=0;mutation<5;++mutation){auto bad=result.tracking;
        switch(mutation){case 0:++bad.recoveryTarget->lease.grant.nonce;break;
        case 1:++bad.recovery->grant.debt.shot;break;case 2:bad.held=WeaponCycleLease{};break;
        case 3:bad.input.tracked[1]=false;break;case 4:bad.gun->token.hand=right;break;}
        CHECK(!BuildBoltPartPlan(bad,f.rig,bad.weapon->weaponInWorld,1,f.sample.input.nowNs));}
    CHECK(!BuildBoltPartPlan(result.tracking,f.rig,result.tracking.weapon->weaponInWorld,1,result.tracking.recovery->deadlineNs));
    return 0;
}

}
int main(){if(SubmittedOutcomeSurvivesLongTrackingLossWithoutRevivingClaims()||RecoveryNeedsActualRightNeutralAndExactTypedPrivateTarget()||RecoveryRetainsCurrentLeftGunAndRequiresWholeBoltGesture()||ExpiredCustodyRequiresActualFreshGunAndAtomicExchange())return 1;
    std::puts("Bolt recovery: 4 groups; exact live custody or fresh physical exchange, full ordered gesture, private part, native ack and returned placement; native convergence mocked.");}
