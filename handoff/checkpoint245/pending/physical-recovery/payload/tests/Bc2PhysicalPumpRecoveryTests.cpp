#include "Bc2PhysicalPump.h"
#include "Bc2PumpCalibration225.h"
#include "Test.h"
#include "fvr/interaction/WeaponCycleConvergence.h"
#include "fvr/interaction/GripAttachment.h"
#include <cstdio>
#include <limits>

using namespace fvr;using namespace fvr::bc2;using namespace fvr::interaction;
namespace {
math::Matrix4 Pose(float z=0){math::Matrix4 m{};for(unsigned n=0;n<4;++n)m.values[n][n]=1;m.values[3][2]=z;return m;}
struct NativeBase {
    Bc2NativeCycleView state;unsigned controls=0,released=0,acked=0,cancelled=0;bool unavailable=false,rejectControl=false,deferReady=false;std::optional<WeaponCycleReady> pendingReady;
    void Finish(std::int64_t now){state.ready=pendingReady;state.ready->observedNs=now;state.ready->deadlineNs=now+50000000;state.phase=Bc2NativeCyclePhase::Complete;}
    Bc2PhysicalPumpApi Api(){return {this,
        [](void* p,const Bc2NativeCycleControl& c)noexcept{auto& n=*static_cast<NativeBase*>(p);
            ++n.controls;if(n.rejectControl)return false;
            if(c.release){++n.released;n.state.ready=WeaponCycleReady{*c.release,c.release->cycle.sequence+1,c.input.nowNs,c.input.nowNs+50000000,true,true};
                n.state.held.reset();n.state.phase=Bc2NativeCyclePhase::Complete;
                if(n.deferReady){n.pendingReady=n.state.ready;n.state.ready.reset();n.state.phase=Bc2NativeCyclePhase::Releasing;}}return true;},
        [](void* p,std::int64_t)noexcept->std::optional<Bc2NativeCycleView>{auto& n=*static_cast<NativeBase*>(p);if(n.unavailable)return {};return n.state;},
        [](void* p,const WeaponCycleReady& r)noexcept{auto& n=*static_cast<NativeBase*>(p);if(!n.state.ready||n.state.ready->release!=r.release)return false;
            ++n.acked;n.state.ready.reset();n.state.blocksFire=false;return true;},
        [](void* p)noexcept{++static_cast<NativeBase*>(p)->cancelled;}};}
};
struct Native:NativeBase {
    bool recoveryMode=false,stopEvidence=false,wrongOwner=false;unsigned recoveryReleased=0,recoveryAcked=0;
    WeaponCycleConvergence<ReloadHoldIdentity> convergence;
    WeaponCycleDebt debt{};std::optional<WeaponCycleRecoveryGrant> grant;
    std::uint64_t invocation=1000;std::int64_t clock=0;
    void Recover(const Bc2PhysicalPumpSample& s){
        recoveryMode=true;state.native.owner=s.nativeOwner;state.held.reset();state.phase=Bc2NativeCyclePhase::Cancelled;state.blocksFire=true;
        debt={s.input.owner,s.item,{0x5350415350554d50ull,s.item.generation},1,1,999,7,24,8};
        convergence.Begin(state.native,debt,s.input.nowNs);clock=s.input.nowNs;
    }
    void Originals(const Bc2NativeCycleControl& c){
        clock=c.input.nowNs;if(stopEvidence)return;
        // Explicit mocked original neutral idle boundary, preserving all three
        // copy counts. This callback never alters the real gesture or grants it.
        for(unsigned round=0;round<2;++round)for(unsigned branch=0;branch<3;++branch){
            const auto at=clock-2000000+round*1000000+branch*100000;
            convergence.ObserveIdle(state.native,{branch,++invocation,debt.guardEpoch,at,at+50000,at+80000000,
                debt.loaded,debt.reserve,debt.capacity},clock);
        }
        if(!grant)grant=convergence.ArmRecovery(state.native,c.input);
    }
    Bc2PhysicalPumpApi Api(){auto api=NativeBase::Api();api.context=this;
        api.control=[](void* p,const Bc2NativeCycleControl& c)noexcept{auto& n=*static_cast<Native*>(p);
            if(!n.recoveryMode){const auto base=n.NativeBase::Api();return base.control(base.context,c);}
            ++n.controls;if(c.release)return false;n.Originals(c);return true;};
        api.cancel=[](void* p)noexcept{auto& n=*static_cast<Native*>(p);++n.cancelled;
            if(n.recoveryMode){n.convergence.Interrupt(n.state.native,n.clock);n.grant.reset();}};
        api.recovery={
            [](void* p,std::int64_t now)noexcept->std::optional<Bc2NativeCycleRecoveryView>{auto& n=*static_cast<Native*>(p);if(!n.recoveryMode)return {};
                Bc2NativeCycleRecoveryView out;out.native=n.state.native;if(n.wrongOwner)++out.native.owner.weapon;out.blocksFire=n.convergence.BlocksFire();
                if(n.grant)out.authority=n.convergence.RecoveryAuthority(n.state.native,*n.grant,now);
                if(const auto& ready=n.convergence.Outcome();ready&&std::holds_alternative<WeaponCycleIdleDebtRelease>(ready->release))
                    out.ready=WeaponCycleIdleDebtReady{std::get<WeaponCycleIdleDebtRelease>(ready->release),
                        *std::max_element(ready->native.second.begin(),ready->native.second.end()),ready->native.observedNs,ready->native.deadlineNs,true,true};
                return out;},
            [](void* p,const WeaponCycleIdleDebtRelease& r,const HandInteractionSample& input)noexcept{auto& n=*static_cast<Native*>(p);
                if(!n.convergence.SubmitRecovered(n.state.native,r,input.nowNs))return false;++n.recoveryReleased;return true;},
            [](void* p,const WeaponCycleIdleDebtReady& r)noexcept{auto& n=*static_cast<Native*>(p);const auto& outcome=n.convergence.Outcome();
                if(!outcome||!std::holds_alternative<WeaponCycleIdleDebtRelease>(outcome->release)||
                   std::get<WeaponCycleIdleDebtRelease>(outcome->release)!=r.release||!n.convergence.Acknowledge(*outcome))return false;
                ++n.recoveryAcked;n.state.blocksFire=false;n.state.phase=Bc2NativeCyclePhase::Complete;return true;}};
        return api;
    }
};
struct Fixture {
    RigSnapshot rig;std::shared_ptr<Bc2PumpCalibration> calibration=std::make_shared<Bc2PumpCalibration>();Native native;
    HandInteraction hands;Bc2PhysicalPumpSample sample;Bc2PhysicalPump pump;Bc2PhysicalPumpResult result;
    std::uint64_t intent=0,lease=0;Bc2PumpRawContact prior{};std::optional<Bc2PumpTracking> lastTarget;
    SupportGrip support;SupportGripResult supported;bool useSupport=false;float pressure=.5f;
    SupportGripContact contact{true,.01f,{}};
    InputFrame Input()const {InputFrame in;in.generation=sample.input.sequence;in.spaceGeneration=sample.input.owner.space;in.predictedNs=sample.input.nowNs;in.focused=in.headValid=sample.input.focused;
        for(unsigned h=0;h<2;++h){in.hands[h].gripTracked=in.hands[h].aimTracked=sample.input.tracked[h];in.hands[h].active=Components;}
        in.hands[0].grip.position.z=-.4f;in.hands[0].squeeze=sample.input.released[0]?0:pressure;return in;}
    SupportGripOwner SupportOwner()const{return {sample.input.owner.actor,sample.input.owner.actorGeneration,sample.nativeOwner.weapon};}
    void SupportTick(){
        auto continued=pump.ContinueSupport(SupportOwner(),Input(),contact,sample.raw.input,{2,sample.nativeOwner.weapon},hands,support,intent,sample.cancel||result.ownsHand);
        supported=continued?*continued:support.Update(SupportOwner(),Input(),contact,sample.cancel,result.ownsHand);
        if(supported.holding){auto held=hands.Current(InteractionHand::Left);if(held&&held->token.kind==HandClaimKind::WeaponSupport)
            hands.RenewFrom(sample.input,sample.raw.input,held->token,{{2,sample.nativeOwner.weapon},sample.raw.input.sequence,sample.raw.input.deadlineNs,true});}
        pump.BindSupport(result.tracking,supported,hands);
    }
    void EnableSupport(){useSupport=true;support.Update(SupportOwner(),Input(),contact,false,true);}
    Fixture():pump(calibration,native.Api()){
        rig.names={"root","jntWpn_1","jntWpn_4","LeftHand"};rig.parents={-1,0,1,0};rig.weaponBone=1;
        rig.identity={0x11000,0x12000,0x30000,0x40000,0x50000,0x60000,0x70000,0x80000,0x90000,4,false};
        rig.world.assign(4,Pose());rig.inverseBind.assign(4,Pose());rig.evaluatedWorld.assign(4,Pose());rig.nativeEvaluated.resize(4);
        for(auto& b:rig.nativeEvaluated)b.fill(std::byte{0x5a});
        calibration->revision=1;calibration->rigFingerprint=bc2_pump_detail::DerivePart(rig)->fingerprint;
        calibration->closedPart=Pose(-.84f);calibration->closedWrist=Pose();calibration->rearDirection=1;calibration->measured=true;
        sample.nativeOwner={0x10000,0x11000,0x12000,0x13000,1,2,3};sample.item={sample.nativeOwner.weapon,4};
        sample.input.owner={(std::uint64_t(sample.nativeOwner.weak)<<32)|sample.nativeOwner.soldier,1,4,3};sample.asset=SpasReloadAsset;
        sample.input.nowNs=1000000000;sample.input.focused=true;sample.input.tracked={true,true};
    }
    void Shot(){native.state.phase=Bc2NativeCyclePhase::Held;native.state.blocksFire=true;native.state.cycle=native.state.shot=1;}
    Bc2PhysicalPumpResult Tick(float travel,bool grip=true){
        auto& s=sample.input;++s.sequence;s.nowNs+=10000000;s.observedNs=s.nowNs;s.deadlineNs=s.nowNs+100000000;s.released[0]=!grip;sample.grip=grip;
        hands.Update(s);auto gun=hands.Current(InteractionHand::Right);
        if(gun)hands.Renew(s,gun->token,{{1,4},s.sequence,s.deadlineNs,true});
        else hands.Acquire(s,{s.owner,InteractionHand::Right,HandClaimKind::GunHold,sample.item,{{1,4},s.sequence,s.deadlineNs,true},++intent,0});
        if(native.state.phase==Bc2NativeCyclePhase::Held)native.state.held=WeaponCycleLease{s.owner,sample.item,{0x5350415350554d50ull,4},1,1,++lease,s.nowNs,s.deadlineNs,true};
        sample.raw=prior;result=pump.Tick(sample,hands,intent);
        if(useSupport)SupportTick();
        prior=BuildPumpRawContact(result.tracking,rig,Pose(travel),Pose(),{.02f,0,0},1,s.nowNs);
        if(result.tracking.target)lastTarget=result.tracking;return result;
    }
    bool Grip(){Tick(0,false);Tick(0,false);Shot();Tick(0,false);Tick(0);Tick(0);return result.ownsHand&&result.tracking.target.has_value();}
};
int DisconnectedPumpRetainsDebtThenRealRecoveredStroke(){
    Fixture f;CHECK(f.Grip());f.Tick(.02f);f.sample.input.tracked[0]=false;f.Tick(.04f);
    CHECK(f.result.blocksFire&&!f.result.ownsHand&&!f.native.released);
    f.sample.input.nowNs+=61000000000ll;f.Tick(0,false);CHECK(f.result.blocksFire&&!f.native.acked);
    f.sample.input.tracked[0]=true;f.native.Recover(f.sample);
    for(unsigned n=0;n<6;++n)f.Tick(0,false);
    CHECK(f.native.grant&&f.native.convergence.Recovery(f.sample.input.nowNs));
    CHECK(!f.native.recoveryReleased&&!f.native.recoveryAcked&&!f.result.ownsHand&&f.result.blocksFire);
    f.Tick(0);f.Tick(0);CHECK(f.result.ownsHand&&f.result.tracking.recoveryTarget&&!f.result.tracking.target&&!f.result.tracking.held);
    f.EnableSupport();unsigned targets=0;const auto original=f.rig.nativeEvaluated;
    for(float t:{.02f,.04f,.06f,.08f,SpasObservedForeEndStroke,SpasObservedForeEndStroke,SpasObservedForeEndStroke,SpasObservedForeEndStroke,
                 .075f,.055f,.035f,.015f,0.f,0.f,0.f,0.f,0.f,0.f}){
        f.Tick(t);
        if(f.result.tracking.recoveryTarget){++targets;CHECK(!f.result.tracking.target&&!f.result.tracking.held);
            const auto part=BuildPumpPresentation(f.result.tracking,f.rig,Pose(),1,f.sample.input.nowNs);
            CHECK(part.part&&part.wrist&&part.part->index==2);
            CHECK(Near(part.part->transform.values[3][2],-.84f+f.result.tracking.recoveryTarget->travel));}
    }
    for(unsigned n=0;n<6;++n)f.Tick(0);
    CHECK(targets>8&&f.native.recoveryReleased==1&&f.native.recoveryAcked==1&&!f.native.released&&!f.native.acked);
    CHECK(!f.result.blocksFire&&f.supported.holding&&f.hands.Current(InteractionHand::Left));
    CHECK(f.native.debt.loaded==7&&f.native.debt.reserve==24&&f.native.debt.shot==1);
    CHECK(f.rig.nativeEvaluated==original);return 0;
}
int RecoveryCannotBorrowSqueezedOrExpiredOrForeignAuthority(){
    for(unsigned failure=0;failure<3;++failure){Fixture f;CHECK(f.Grip());
        f.sample.input.tracked[0]=false;f.Tick(.02f);f.sample.input.tracked[0]=true;f.native.Recover(f.sample);
        if(failure==0){for(unsigned n=0;n<10;++n)f.Tick(0);
            CHECK(!f.native.grant&&!f.result.ownsHand&&!f.result.tracking.recoveryTarget);}
        else {
            for(unsigned n=0;n<6;++n)f.Tick(0,false);f.Tick(0);f.Tick(0);
            CHECK(f.result.tracking.recoveryTarget);
            const auto original=f.result.tracking;
            if(failure==1){f.native.stopEvidence=true;f.sample.input.nowNs+=100000000;}
            else f.native.wrongOwner=true;
            f.Tick(.02f);CHECK(!f.result.tracking.recoveryTarget);
            if(failure==1)CHECK(!BuildPumpPresentation(original,f.rig,Pose(),1,f.sample.input.nowNs).part);
        }
        CHECK(!f.native.recoveryReleased&&!f.native.recoveryAcked&&!f.native.released&&f.result.blocksFire);
    }return 0;
}
int RecoveryInterruptionRequiresNewGrantAndFreshStroke(){
    Fixture f;CHECK(f.Grip());f.sample.input.tracked[0]=false;f.Tick(.02f);
    f.sample.input.tracked[0]=true;f.native.Recover(f.sample);
    for(unsigned n=0;n<6;++n)f.Tick(0,false);f.Tick(0);f.Tick(0);CHECK(f.result.tracking.recoveryTarget);
    const auto old=f.result.tracking;const auto nonce=f.native.grant->nonce;
    f.Tick(.03f);f.Tick(.04f,false);CHECK(!f.result.ownsHand&&!f.native.recoveryReleased);
    for(unsigned n=0;n<6;++n)f.Tick(0,false);
    CHECK(f.native.grant&&f.native.grant->nonce>nonce);
    f.Tick(0);f.Tick(0);CHECK(f.result.tracking.recoveryTarget);
    CHECK(!PumpTargetRetained(old,f.result.tracking,f.sample.input.nowNs));
    for(float t:{.02f,.04f,.06f,.08f,SpasObservedForeEndStroke,SpasObservedForeEndStroke,SpasObservedForeEndStroke,SpasObservedForeEndStroke,
                 .075f,.055f,.035f,.015f,0.f,0.f,0.f,0.f,0.f,0.f})f.Tick(t);
    for(unsigned n=0;n<6;++n)f.Tick(0);
    CHECK(f.native.recoveryReleased==1&&f.native.recoveryAcked==1&&f.native.debt.shot==1);return 0;
}
int OriginalNativeIdleWithoutAnyHeldLeaseStillOwesRealStroke(){
    Fixture f;f.Tick(0,false);f.Tick(0,false);
    // Mock a genuine shot debt whose native work already converged normally.
    // No WeaponCycleLease{held=true} is supplied anywhere on this path.
    f.native.Recover(f.sample);
    for(unsigned n=0;n<6;++n)f.Tick(0,false);
    CHECK(f.native.grant&&f.result.blocksFire&&!f.native.recoveryAcked&&!f.native.state.held);
    f.Tick(0);f.Tick(0);CHECK(f.result.tracking.recoveryTarget&&!f.result.tracking.held);
    for(float t:{.02f,.04f,.06f,.08f,SpasObservedForeEndStroke,SpasObservedForeEndStroke,SpasObservedForeEndStroke,SpasObservedForeEndStroke,
                 .075f,.055f,.035f,.015f,0.f,0.f,0.f,0.f,0.f,0.f})f.Tick(t);
    for(unsigned n=0;n<6;++n)f.Tick(0);
    CHECK(f.native.recoveryReleased==1&&f.native.recoveryAcked==1&&!f.native.released&&!f.native.acked&&!f.result.blocksFire);
    CHECK(f.native.debt.shot==1&&f.native.debt.loaded==7&&f.native.debt.reserve==24);return 0;
}

}
int main(){if(OriginalNativeIdleWithoutAnyHeldLeaseStillOwesRealStroke()||DisconnectedPumpRetainsDebtThenRealRecoveredStroke()||RecoveryCannotBorrowSqueezedOrExpiredOrForeignAuthority()||
    RecoveryInterruptionRequiresNewGrantAndFreshStroke())return 1;
    std::puts("Pump disconnect recovery: 4 groups; shared gesture, hand arbitration, private part and support return; native convergence mocked.");}
