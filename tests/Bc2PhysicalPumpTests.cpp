#include "Bc2PhysicalPump.h"
#include "Bc2PumpCalibration225.h"
#include "Test.h"
#include "fvr/interaction/GripAttachment.h"
#include <cstdio>
#include <limits>

using namespace fvr;using namespace fvr::bc2;using namespace fvr::interaction;
namespace {
math::Matrix4 Pose(float z=0){math::Matrix4 m{};for(unsigned n=0;n<4;++n)m.values[n][n]=1;m.values[3][2]=z;return m;}
struct Native {
    Bc2NativeCycleView state;unsigned controls=0,released=0,acked=0,cancelled=0;bool unavailable=false,rejectControl=false,deferReady=false;std::optional<WeaponCycleReady> pendingReady;
    void Finish(std::int64_t now){state.ready=pendingReady;state.ready->observedNs=now;state.ready->deadlineNs=now+50000000;state.phase=Bc2NativeCyclePhase::Complete;}
    Bc2PhysicalPumpApi Api(){return {this,
        [](void* p,const Bc2NativeCycleControl& c)noexcept{auto& n=*static_cast<Native*>(p);
            ++n.controls;if(n.rejectControl)return false;
            if(c.release){++n.released;n.state.ready=WeaponCycleReady{*c.release,c.release->cycle.sequence+1,c.input.nowNs,c.input.nowNs+50000000,true,true};
                n.state.held.reset();n.state.phase=Bc2NativeCyclePhase::Complete;
                if(n.deferReady){n.pendingReady=n.state.ready;n.state.ready.reset();n.state.phase=Bc2NativeCyclePhase::Releasing;}}return true;},
        [](void* p,std::int64_t)noexcept->std::optional<Bc2NativeCycleView>{auto& n=*static_cast<Native*>(p);if(n.unavailable)return {};return n.state;},
        [](void* p,const WeaponCycleReady& r)noexcept{auto& n=*static_cast<Native*>(p);if(!n.state.ready||n.state.ready->release!=r.release)return false;
            ++n.acked;n.state.ready.reset();n.state.blocksFire=false;return true;},
        [](void* p)noexcept{++static_cast<Native*>(p)->cancelled;}};}
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
int ActualMeasuredCalibrationIsExplicit(){const auto measured=MeasuredSpasPump225();CHECK(PumpCalibrationValid(measured));
    CHECK(measured.rearDirection==1&&Near(measured.closedPart.values[3][2],-.846984863f));
    auto rejected=measured;rejected.measured=false;CHECK(!PumpCalibrationValid(rejected));rejected=measured;rejected.rearDirection=0;CHECK(!PumpCalibrationValid(rejected));return 0;}
int RendererToPhysicalToNativeAndPart(){Fixture f;CHECK(f.Grip());unsigned targets=0;const auto source=f.rig.nativeEvaluated;
    for(float t:{.02f,.04f,.06f,.08f,SpasObservedForeEndStroke,SpasObservedForeEndStroke,SpasObservedForeEndStroke,SpasObservedForeEndStroke,
                 .075f,.055f,.035f,.015f,0.f,0.f,0.f,0.f,0.f}){
        const auto r=f.Tick(t);CHECK(r.blocksFire);
        if(r.tracking.target){++targets;const auto plan=BuildPumpPresentation(r.tracking,f.rig,Pose(),1,f.sample.input.nowNs);
            CHECK(plan.wrist&&plan.part&&plan.part->index==2);CHECK(Near(plan.part->transform.values[3][2],-.84f+r.tracking.target->travel));
            CHECK(Near(plan.wrist->values[3][2],r.tracking.target->travel));}
    }
    CHECK(targets>8&&f.native.released==1);f.Tick(0);CHECK(f.native.acked==1&&!f.result.blocksFire&&!f.result.tracking.target);
    CHECK(f.rig.nativeEvaluated==source);return 0;}
int ReleaseImmediatelyRevokesPresentation(){Fixture f;CHECK(f.Grip());const auto old=*f.lastTarget;
    auto released=f.Tick(.02f,false);CHECK(!released.tracking.target&&!released.ownsHand&&released.blocksFire);
    CHECK(!PumpTargetRetained(old,released.tracking,f.sample.input.nowNs));CHECK(f.native.released==0);return 0;}
int OldPaletteCannotBeRenewed(){Fixture f;CHECK(f.Grip());const auto old=*f.lastTarget;f.Tick(.02f);
    CHECK(PumpTargetRetained(old,f.result.tracking,f.sample.input.nowNs));
    CHECK(!PumpTargetRetained(old,f.result.tracking,old.target->deadlineNs));
    auto changed=f.result.tracking;++changed.input.owner.space;CHECK(!PumpTargetRetained(old,changed,f.sample.input.nowNs));
    changed=f.result.tracking;changed.target.reset();CHECK(!PumpTargetRetained(old,changed,f.sample.input.nowNs));return 0;}
int WrongNativeOrGeometryCannotAcquire(){for(unsigned mutation=0;mutation<4;++mutation){Fixture f;f.Tick(0,false);f.Tick(0,false);f.Shot();
    if(mutation==0)f.prior.nativeOwner.weapon++;
    if(mutation==1)f.prior.rigFingerprint++;
    if(mutation==2)f.prior.input.observedNs++;
    if(mutation==3)f.sample.asset="unsupported";
    f.Tick(0);CHECK(!f.result.ownsHand&&!f.native.released);CHECK(f.result.blocksFire);}
    return 0;}
int FailedControlSuppressesFireAndDropsOnlyMechanism(){Fixture f;CHECK(f.Grip());f.native.rejectControl=true;
    CHECK(f.Tick(.02f).blocksFire);CHECK(!f.result.ownsHand&&!f.result.tracking.target);
    CHECK(!f.hands.Current(InteractionHand::Left));CHECK(f.hands.Current(InteractionHand::Right));CHECK(f.native.released==0);
    Fixture watching;watching.native.rejectControl=true;CHECK(watching.Tick(0,false).blocksFire);return 0;}
int MissingNativeViewPreservesObligation(){Fixture f;CHECK(f.Grip());f.native.unavailable=true;
    CHECK(f.Tick(.02f).blocksFire);CHECK(!f.result.tracking.target&&!f.hands.Current(InteractionHand::Left));
    f.sample.cancel=true;CHECK(f.Tick(0).blocksFire);CHECK(f.pump.BlocksFire()&&f.native.released==0);return 0;}
int BindingPointCannotChangeDuringCycle(){Fixture f;CHECK(f.Grip());f.prior.pointWrist.x+=.001f;
    CHECK(f.Tick(.02f).blocksFire);CHECK(!f.result.tracking.target&&!f.result.ownsHand&&!f.hands.Current(InteractionHand::Left));
    auto changed=*f.lastTarget;changed.profile.closedContact.values[3][2]+=.001f;
    CHECK(!PumpTargetFresh(changed,f.sample.input.nowNs));changed=*f.lastTarget;changed.pointWrist.x+=.001f;
    CHECK(!PumpTargetFresh(changed,f.sample.input.nowNs));CHECK(f.native.released==0);return 0;}
bool Stroke(Fixture& f){
    for(float t:{.02f,.04f,.06f,.08f,SpasObservedForeEndStroke,SpasObservedForeEndStroke,SpasObservedForeEndStroke,SpasObservedForeEndStroke,
                 .075f,.055f,.035f,.015f,0.f,0.f,0.f,0.f,0.f}){f.Tick(t);if(f.native.released)return true;}return false;
}
int HeldPumpReturnsOnlyAfterReadyWithNewContact(){
    Fixture f;CHECK(f.Grip());f.EnableSupport();f.native.deferReady=true;CHECK(Stroke(f));
    CHECK(!f.supported.holding&&!f.hands.Current(InteractionHand::Left));
    for(unsigned n=0;n<30;++n){f.Tick(0);CHECK(!f.supported.holding&&!f.native.acked);}
    f.native.Finish(f.sample.input.nowNs+10000000);f.Tick(0);CHECK(f.native.acked==1&&!f.supported.holding); // raw source predates Ready
    f.Tick(0);CHECK(f.supported.holding&&f.supported.engaged&&Near(f.Input().hands[0].squeeze,.5f));
    const auto left=f.hands.Current(InteractionHand::Left);CHECK(left&&left->token.kind==HandClaimKind::WeaponSupport);
    CHECK(left->inputSequence==f.sample.raw.input.sequence&&left->deadlineNs==f.sample.raw.input.deadlineNs);
    CHECK(left->deadlineNs<f.sample.input.deadlineNs&&left->token.prerequisiteClaim==f.hands.Current(InteractionHand::Right)->token.id);
    const auto token=f.supported.token;f.Tick(0);CHECK(f.supported.holding&&!f.supported.engaged&&f.supported.token==token);
    return 0;
}
int ReturnedSupportCaptureIsClosedAndTokenBound(){
    Fixture f;CHECK(f.Grip());f.EnableSupport();CHECK(Stroke(f));f.Tick(0);CHECK(!f.supported.holding);f.Tick(0);CHECK(f.supported.holding);
    const auto original=f.rig.nativeEvaluated;const auto& t=f.result.tracking;
    auto wrist=ResolvePumpSupportWrist(t,f.rig,f.supported.token,f.sample.input.nowNs);CHECK(wrist&&wrist->values==f.calibration->closedWrist.values);
    GripAttachment capture;const GripAttachmentOwner owner{1,2,3,4,5};
    const auto locked=capture.Update(owner,f.supported.token,*wrist);CHECK(locked);
    const auto later=capture.Update(owner,f.supported.token,Pose(.095f));CHECK(later&&later->values==locked->values);
    CHECK(!ResolvePumpSupportWrist(t,f.rig,f.supported.token+1,f.sample.input.nowNs));
    CHECK(!ResolvePumpSupportWrist(t,f.rig,f.supported.token,t.mechanism->deadlineNs));
    auto changed=t;changed.input.released[0]=true;CHECK(!ResolvePumpSupportWrist(changed,f.rig,f.supported.token,f.sample.input.nowNs));
    changed=t;changed.supportCapture->gun.id++;CHECK(!ResolvePumpSupportWrist(changed,f.rig,f.supported.token,f.sample.input.nowNs));
    changed=t;changed.mechanism->token.kind=HandClaimKind::Mechanism;CHECK(!ResolvePumpSupportWrist(changed,f.rig,f.supported.token,f.sample.input.nowNs));
    CHECK(f.rig.nativeEvaluated==original);f.Tick(0,false);CHECK(!f.result.tracking.supportCapture&&!f.supported.holding);return 0;
}
int ActualReleaseAndLossConsumePumpReturn(){
    for(unsigned mode=0;mode<6;++mode){
        Fixture f;CHECK(f.Grip());f.EnableSupport();f.native.deferReady=true;CHECK(Stroke(f));
        if(mode==0){f.Tick(0,false);f.Tick(0);}
        if(mode==1){f.sample.input.focused=false;f.Tick(0);f.sample.input.focused=true;}
        if(mode==2){f.sample.input.tracked[0]=false;f.Tick(0);f.sample.input.tracked[0]=true;}
        if(mode==3){f.sample.cancel=true;f.Tick(0);f.sample.cancel=false;}
        if(mode==4){f.hands.Release(f.sample.input,f.hands.Current(InteractionHand::Right)->token);f.Tick(0);}
        if(mode==5){f.sample.input.nowNs+=100000000;f.Tick(0);}
        f.native.Finish(f.sample.input.nowNs);f.Tick(0);f.Tick(0);CHECK(f.native.acked==1);
        CHECK(!f.result.tracking.supportCapture&&!f.hands.Current(InteractionHand::Left));
    }return 0;
}
int ReturnNeedsActualOriginalHistoryAndMatchingGeometry(){
    for(unsigned mode=0;mode<9;++mode){
        Fixture f;CHECK(f.Grip());f.EnableSupport();f.native.deferReady=true;CHECK(Stroke(f));
        f.native.Finish(f.sample.input.nowNs);f.Tick(0);CHECK(!f.supported.holding);
        if(mode==0)++f.prior.input.observedNs;
        if(mode==1)++f.prior.input.deadlineNs;
        if(mode==2)f.prior.input.sequence+=10;
        if(mode==3)f.prior.contact=Pose(.2f);
        if(mode==4)++f.prior.rigFingerprint;
        if(mode==5){auto& s=f.sample.input;f.hands.Acquire(s,{s.owner,InteractionHand::Left,HandClaimKind::Sight,f.sample.item,{{3,4},s.sequence,s.deadlineNs,true},++f.intent,f.hands.Current(InteractionHand::Right)->token.id});}
        if(mode==6)++f.prior.nativeOwner.weapon;
        if(mode==7)f.prior.contact.values[3][0]=std::numeric_limits<float>::quiet_NaN();
        if(mode==8){auto& s=f.sample.input;f.hands.Acquire(s,{s.owner,InteractionHand::Left,HandClaimKind::AmmoObject,{991,4},{{4,4},s.sequence,s.deadlineNs,true},++f.intent,0});}
        f.Tick(0);CHECK(!f.result.tracking.supportCapture&&!f.supported.holding);
        auto left=f.hands.Current(InteractionHand::Left);CHECK(!left||left->token.kind==HandClaimKind::Sight||left->token.kind==HandClaimKind::AmmoObject);
    }return 0;
}

int PendingReleaseDoesNotRequestNewControllerAuthority(){Fixture f;CHECK(f.Grip());f.native.deferReady=true;CHECK(Stroke(f));
    const auto original=f.native.pendingReady;CHECK(original);const auto controls=f.native.controls;
    f.native.rejectControl=true;
    for(unsigned n=0;n<30;++n){f.Tick(0);CHECK(f.result.blocksFire&&!f.result.tracking.target&&!f.result.ownsHand);}
    CHECK(f.native.controls==controls&&f.native.released==1&&f.native.acked==0);
    CHECK(f.native.pendingReady->release==original->release);
    // Tracking loss changes hand custody, never the submitted native request.
    f.sample.input.focused=false;f.Tick(0);f.sample.input.focused=true;
    ++f.sample.input.owner.equipGeneration;++f.sample.item.generation;
    f.Tick(0);CHECK(f.native.controls==controls&&f.native.pendingReady->release==original->release);
    f.native.Finish(f.sample.input.nowNs);f.Tick(0);CHECK(f.native.acked==1&&f.native.released==1);
    CHECK(!f.hands.Current(InteractionHand::Left));return 0;
}
int IdleYieldAllowsShellCustodyWithoutCancellingDebt(){Fixture f;
    f.Tick(0,false);f.sample.cancel=true;f.Tick(0,false);
    CHECK(!f.result.blocksFire&&!f.result.ownsHand&&f.native.released==0);
    f.sample.cancel=false;CHECK(f.Grip());f.sample.cancel=true;f.Tick(0,false);
    CHECK(f.result.blocksFire&&f.native.released==0);return 0;
}
int OrdinaryForeEndReleaseRegripsSameDebtAndRequiresFullStroke(){
    for(float interrupted:{0.f,.04f,SpasObservedForeEndStroke}){Fixture f;CHECK(f.Grip());
        for(float travel:{.02f,.04f,.06f,.08f,SpasObservedForeEndStroke}){
            if(travel>interrupted)break;f.Tick(travel);}
        const auto original=f.native.state.held;CHECK(original);
        f.Tick(interrupted,false);CHECK(!f.result.ownsHand&&!f.result.tracking.target&&f.result.blocksFire);
        CHECK(!f.native.cancelled&&!f.native.released&&f.native.state.phase==Bc2NativeCyclePhase::Held);
        // A genuine new neutral packet at the closed contact restarts the
        // gesture. The old partial rear travel cannot count toward completion.
        f.Tick(0,false);f.Tick(0,false);f.Tick(0);f.Tick(0);
        CHECK(f.result.ownsHand&&f.result.tracking.target&&Near(f.result.tracking.target->travel,0));
        CHECK(weapon_cycle_detail::Same(*original,*f.native.state.held));
        for(unsigned n=0;n<8;++n)f.Tick(0);CHECK(!f.native.released&&f.result.blocksFire);
        CHECK(Stroke(f));f.Tick(0);CHECK(f.native.released==1&&f.native.acked==1&&!f.result.blocksFire);
        CHECK(!f.native.cancelled&&f.native.state.shot==1&&f.native.state.cycle==1);
    }return 0;
}

}
int main(){if(ActualMeasuredCalibrationIsExplicit()||RendererToPhysicalToNativeAndPart()||ReleaseImmediatelyRevokesPresentation()||
    OldPaletteCannotBeRenewed()||WrongNativeOrGeometryCannotAcquire()||FailedControlSuppressesFireAndDropsOnlyMechanism()||
    MissingNativeViewPreservesObligation()||BindingPointCannotChangeDuringCycle()||
    HeldPumpReturnsOnlyAfterReadyWithNewContact()||ReturnedSupportCaptureIsClosedAndTokenBound()||
    ActualReleaseAndLossConsumePumpReturn()||ReturnNeedsActualOriginalHistoryAndMatchingGeometry()||PendingReleaseDoesNotRequestNewControllerAuthority()||
    IdleYieldAllowsShellCustodyWithoutCancellingDebt()||OrdinaryForeEndReleaseRegripsSameDebtAndRequiresFullStroke())return 1;
    std::puts("15 physical pump adapter groups passed; original renderer packets, real shared consumer/private part, mocked native service.");}


