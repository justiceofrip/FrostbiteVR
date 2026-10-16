#include "Bc2PhysicalPump.h"
#include "Bc2PumpPosePlan.h"
#include "Bc2PumpCalibration225.h"
#include "Test.h"
#include "fvr/interaction/GripAttachment.h"
#include <cstdio>
#include <limits>
#include <string_view>
#include <cstring>

using namespace fvr;using namespace fvr::bc2;using namespace fvr::interaction;
namespace {
math::Matrix4 Pose(float z=0){math::Matrix4 m{};for(unsigned n=0;n<4;++n)m.values[n][n]=1;m.values[3][2]=z;return m;}
struct Native {
    Bc2NativeCycleView state;unsigned controls=0,released=0,acked=0,cancelled=0;bool unavailable=false,rejectControl=false,deferReady=false;std::optional<WeaponCycleReady> pendingReady;
    unsigned busyControls=0,busyReleases=0;bool precheckExpired=false;std::vector<Bc2NativeCycleControl> releaseAttempts;
    void Finish(std::int64_t now){state.ready=pendingReady;state.ready->observedNs=now;state.ready->deadlineNs=now+50000000;state.phase=Bc2NativeCyclePhase::Complete;}
    Bc2PhysicalPumpApi Api(){auto api=Bc2PhysicalPumpApi{this,
        [](void* p,const Bc2NativeCycleControl& c)noexcept{auto& n=*static_cast<Native*>(p);
            ++n.controls;if(n.rejectControl)return false;
            if(c.release){++n.released;n.state.ready=WeaponCycleReady{*c.release,c.release->cycle.sequence+1,c.input.nowNs,c.input.nowNs+50000000,true,true};
                n.state.held.reset();n.state.phase=Bc2NativeCyclePhase::Complete;
                if(n.deferReady){n.pendingReady=n.state.ready;n.state.ready.reset();n.state.phase=Bc2NativeCyclePhase::Releasing;}}return true;},
        [](void* p,std::int64_t)noexcept->std::optional<Bc2NativeCycleView>{auto& n=*static_cast<Native*>(p);if(n.unavailable)return {};return n.state;},
        [](void* p,const WeaponCycleReady& r)noexcept{auto& n=*static_cast<Native*>(p);if(!n.state.ready||n.state.ready->release!=r.release)return false;
            ++n.acked;n.state.ready.reset();n.state.blocksFire=false;return true;},
        [](void* p)noexcept{++static_cast<Native*>(p)->cancelled;}};
        api.controlResult=[](void* p,const Bc2NativeCycleControl& c)noexcept{auto& n=*static_cast<Native*>(p);
            if(c.release){n.releaseAttempts.push_back(c);if(n.busyReleases){--n.busyReleases;return Bc2NativeCycleSubmitResult::Busy;}
                if(n.precheckExpired){n.precheckExpired=false;return Bc2NativeCycleSubmitResult::NotDispatched;}}
            else if(n.busyControls){--n.busyControls;return Bc2NativeCycleSubmitResult::Busy;}
            return n.Api().control(p,c)?Bc2NativeCycleSubmitResult::Accepted:Bc2NativeCycleSubmitResult::Rejected;};return api;}
};
struct Fixture {
    RigSnapshot rig;std::shared_ptr<Bc2PumpCalibration> calibration=std::make_shared<Bc2PumpCalibration>();Native native;
    HandInteraction hands;Bc2PhysicalPumpSample sample;Bc2PhysicalPump pump;Bc2PhysicalPumpResult result;
    std::uint64_t intent=0,lease=0;Bc2PumpRawContact prior{};std::optional<Bc2PumpTracking> lastTarget;
    MechanismAudioLedger<> audioLedger;std::vector<MechanismAudioEvent> sounds;
    SupportGrip support;SupportGripResult supported;bool useSupport=false,suppressHeld=false,freezeRaw=false;float pressure=.5f;
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
        calibration->revision=1;calibration->asset=SpasReloadAsset;calibration->profileId=0x5350415350554d50ull;calibration->geometry=SpasPumpGeometry;calibration->rigFingerprint=bc2_pump_detail::DerivePart(rig)->fingerprint;
        calibration->closedPart=Pose(-.84f);calibration->closedWrist=Pose();calibration->rearDirection=1;calibration->measured=true;
        sample.nativeOwner={0x10000,0x11000,0x12000,0x13000,1,2,3};sample.item={sample.nativeOwner.weapon,4};
        sample.input.owner={(std::uint64_t(sample.nativeOwner.weak)<<32)|sample.nativeOwner.soldier,1,4,3};sample.asset=SpasReloadAsset;
        sample.input.nowNs=1000000000;sample.input.focused=true;sample.input.tracked={true,true};
    }
    void Shot(){native.state.phase=Bc2NativeCyclePhase::Held;native.state.blocksFire=true;native.state.cycle=native.state.shot=1;native.state.native.owner=sample.nativeOwner;}
    Bc2PhysicalPumpResult Tick(float travel,bool grip=true,math::Vec3 offset={}){
        auto& s=sample.input;++s.sequence;s.nowNs+=10000000;s.observedNs=s.nowNs;s.deadlineNs=s.nowNs+100000000;s.released[0]=!grip;sample.grip=grip;
        hands.Update(s);auto gun=hands.Current(InteractionHand::Right);
        if(gun)hands.Renew(s,gun->token,{{1,4},s.sequence,s.deadlineNs,true});
        else hands.Acquire(s,{s.owner,InteractionHand::Right,HandClaimKind::GunHold,sample.item,{{1,4},s.sequence,s.deadlineNs,true},++intent,0});
        if(suppressHeld)native.state.held.reset();
        else if(native.state.phase==Bc2NativeCyclePhase::Held)native.state.held=WeaponCycleLease{s.owner,sample.item,{calibration->profileId,4},1,1,++lease,s.nowNs,s.deadlineNs,true};
        sample.raw=prior;result=pump.Tick(sample,hands,intent);
        if(result.audio&&audioLedger.Accept(*result.audio,s.nowNs)==MechanismAudioAdmission::Accepted)sounds.push_back(*result.audio);
        if(useSupport)SupportTick();
        auto wrist=calibration->closedWrist;
        for(unsigned n=0;n<3;++n)wrist.values[3][n]+=calibration->geometry.axis[n]*calibration->rearDirection*travel;
        wrist.values[3][0]+=offset.x;wrist.values[3][1]+=offset.y;
        if(!freezeRaw)prior=BuildPumpRawContact(result.tracking,rig,wrist,Pose(),{.02f,0,0},1,s.nowNs);
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
        if(f.native.released)break;
    }
    CHECK(targets>8&&f.native.released==1);CHECK(f.sounds.size()==2&&f.sounds[0].stage==MechanismAudioStage::RearTravel&&f.sounds[1].stage==MechanismAudioStage::Closure);
    CHECK(f.sounds[0].identity==f.sounds[1].identity&&f.sounds[1].sequence>f.sounds[0].sequence);
    f.Tick(0);CHECK(f.native.acked==1&&!f.result.blocksFire&&!f.result.tracking.target&&!f.result.audio&&f.sounds.size()==2);
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
    CHECK(f.Tick(.02f).blocksFire);CHECK(!f.result.tracking.target&&f.hands.Current(InteractionHand::Left));
    f.sample.cancel=true;CHECK(f.Tick(0).blocksFire);CHECK(f.pump.BlocksFire()&&f.native.released==0);return 0;}
int BindingPointCannotChangeDuringCycle(){Fixture f;CHECK(f.Grip());f.prior.pointWrist.x+=.001f;
    CHECK(f.Tick(.02f).blocksFire);CHECK(!f.result.tracking.target&&!f.result.ownsHand&&!f.hands.Current(InteractionHand::Left));
    auto changed=*f.lastTarget;changed.profile.closedContact.values[3][2]+=.001f;
    CHECK(!PumpTargetFresh(changed,f.sample.input.nowNs));changed=*f.lastTarget;changed.pointWrist.x+=.001f;
    CHECK(!PumpTargetFresh(changed,f.sample.input.nowNs));CHECK(f.native.released==0);return 0;}
bool OffAxisStroke(Fixture& f,float lateral){
    // Fresh original10ms packets, actual whole rail excursion. The endpoint
    // remains displaced laterally; no idealized recenter or squeeze release.
    for(unsigned n=1;n<=6;++n)f.Tick(.095f*float(n)/6,true,{lateral*float(n)/6,0,0});
    for(unsigned n=0;n<3;++n)f.Tick(.095f,true,{lateral,0,0});
    for(unsigned n=1;n<=6;++n)f.Tick(.095f*(1-float(n)/6),true,{lateral,0,0});
    for(unsigned n=0;n<3&&!f.native.released;++n)f.Tick(0,true,{lateral,0,0});
    return f.native.released==1;
}
int BusyControlRetainsOnlyCurrentGrasp(){
    Fixture f;CHECK(f.Grip());const auto left=f.hands.Current(InteractionHand::Left)->token;
    const auto controls=f.native.controls;f.native.busyControls=2;
    for(unsigned n=0;n<2;++n){const auto r=f.Tick(.02f);CHECK(r.blocksFire&&r.ownsHand&&!r.tracking.target&&!r.audio);
        CHECK(f.hands.Current(InteractionHand::Left)->token==left&&f.native.controls==controls&&!f.native.cancelled);}
    CHECK(f.Tick(.02f).ownsHand&&!f.native.released);return 0;
}
int BusyReleaseRetriesExactOriginalPacket(){
    Fixture f;CHECK(f.Grip());f.native.busyReleases=2;CHECK(OffAxisStroke(f,0));
    CHECK(f.native.releaseAttempts.size()==3&&f.native.released==1);
    const auto first=f.native.releaseAttempts.front();
    for(const auto& attempt:f.native.releaseAttempts){CHECK(attempt.release==first.release);
        CHECK(attempt.input.sequence==first.input.sequence&&attempt.input.observedNs==first.input.observedNs&&attempt.input.deadlineNs==first.input.deadlineNs);
        CHECK(attempt.input.nowNs<first.release->deadlineNs);}
    for(unsigned n=0;n<8;++n)f.Tick(0);CHECK(f.native.released==1&&f.native.acked==1&&!f.result.blocksFire);return 0;
}
int ExpiredUndispatchedReleaseNeedsNewWholeStroke(){
    Fixture f;CHECK(f.Grip());f.native.busyReleases=1000;CHECK(!OffAxisStroke(f,0));
    CHECK(!f.native.releaseAttempts.empty());const auto old=*f.native.releaseAttempts.front().release;
    for(unsigned n=0;n<18;++n)f.Tick(0);const auto attempts=f.native.releaseAttempts.size();
    CHECK(!f.native.released&&!f.native.acked&&f.result.blocksFire);
    f.native.busyReleases=0;for(unsigned n=0;n<4;++n)f.Tick(0);
    CHECK(f.native.releaseAttempts.size()==attempts&&!f.native.released);
    f.Tick(0,false);f.Tick(0,false);f.Tick(0);f.Tick(0);CHECK(f.result.ownsHand);
    CHECK(OffAxisStroke(f,0));const auto next=*f.native.releaseAttempts.back().release;
    CHECK(next.request>old.request&&next.cycle.cycle==old.cycle.cycle&&next.cycle.shot==old.cycle.shot);
    f.Tick(0);CHECK(f.native.acked==1&&!f.result.blocksFire);return 0;
}
int PredispatchExpiredReleaseNeedsNewGesture(unsigned busy){
    Fixture f;CHECK(f.Grip());f.native.precheckExpired=true;f.native.busyReleases=busy;
    OffAxisStroke(f,0);for(unsigned n=0;n<5;++n)f.Tick(0);
    CHECK(f.native.releaseAttempts.size()==busy+1&&!f.native.released&&!f.native.acked&&f.result.blocksFire);
    const auto old=*f.native.releaseAttempts.back().release;
    for(unsigned n=0;n<4;++n)f.Tick(0);CHECK(f.native.releaseAttempts.size()==busy+1);
    f.Tick(0,false);f.Tick(0,false);f.Tick(0);f.Tick(0);CHECK(f.result.ownsHand);
    CHECK(OffAxisStroke(f,0));for(unsigned n=0;n<6;++n)f.Tick(0);
    CHECK(f.native.released==1&&f.native.acked==1&&!f.result.blocksFire);
    CHECK(f.native.releaseAttempts.back().release->request>old.request&&
        f.native.releaseAttempts.back().release->cycle.cycle==old.cycle.cycle&&f.native.releaseAttempts.back().release->cycle.shot==old.cycle.shot);return 0;
}
int UndispatchedRequestCannotCrossSourceLossOrRenewCycleDeadline(){
    for(unsigned change=0;change<4;++change){Fixture f;CHECK(f.Grip());f.native.busyReleases=1000;
        CHECK(!OffAxisStroke(f,0));const auto attempts=f.native.releaseAttempts.size();CHECK(attempts);
        if(change==0)++f.sample.nativeOwner.weapon;
        if(change==1)f.sample.input.tracked[0]=false;
        if(change==2)f.sample.cancel=true;
        if(change==3)f.sample.input.nowNs+=31000000000ll;
        f.native.busyReleases=0;f.Tick(0,false);f.Tick(0,false);
        CHECK(f.native.releaseAttempts.size()==attempts&&!f.native.released&&!f.native.acked&&f.result.blocksFire);
        if(change==3){CHECK(!OffAxisStroke(f,0));CHECK(f.native.releaseAttempts.size()==attempts);}
    }return 0;
}
int OffAxisHeldEndpointReturnsSupport(){
    for(float displacement:{.11f,.18f,.27f}){
        Fixture f;CHECK(f.Grip());f.EnableSupport();f.native.deferReady=true;
        CHECK(OffAxisStroke(f,displacement));CHECK(f.result.blocksFire&&!f.native.acked);
        f.contact.distanceMeters=displacement;
        f.Tick(0,true,{displacement,0,0});f.Tick(0,true,{displacement,0,0});
        CHECK(f.supported.holding&&f.result.tracking.supportCapture&&f.result.blocksFire);
        const auto original=f.Input();
        CHECK(!std::memcmp(&f.supported.input.hands[0],&original.hands[0],sizeof(original.hands[0])));
        const auto token=f.supported.token;const auto left=f.hands.Current(InteractionHand::Left);CHECK(left&&left->token.kind==HandClaimKind::WeaponSupport);
        const auto wrist=ResolvePumpSupportWrist(f.result.tracking,f.rig,token,f.sample.input.nowNs);
        CHECK(wrist&&wrist->values==f.calibration->closedWrist.values); // Same authored endpoint, no raw wrist rewrite.
        for(unsigned n=0;n<25;++n){f.Tick(0,true,{displacement,0,0});CHECK(f.supported.holding&&f.supported.token==token&&f.result.blocksFire);}
        f.native.Finish(f.sample.input.nowNs+10000000);f.Tick(0,true,{displacement,0,0});
        CHECK(f.native.acked==1&&f.native.released==1&&!f.result.blocksFire&&f.supported.token==token);
    }
    std::puts("Fresh held off-axis endpoints11/18/27cm retain authored support through native tail");return 0;
}
int OffAxisReturnHonorsActualCancellation(){
    for(unsigned mode=0;mode<8;++mode){
        Fixture f;CHECK(f.Grip());f.EnableSupport();f.native.deferReady=true;CHECK(OffAxisStroke(f,.18f));
        f.contact.distanceMeters=.18f;
        if(mode==0){f.Tick(0,false,{.18f,0,0});f.Tick(0,true,{.18f,0,0});}
        if(mode==1){f.sample.input.focused=false;f.Tick(0,true,{.18f,0,0});f.sample.input.focused=true;}
        if(mode==2){f.sample.input.tracked[0]=false;f.Tick(0,true,{.18f,0,0});f.sample.input.tracked[0]=true;}
        if(mode==3){f.sample.cancel=true;f.Tick(0,true,{.18f,0,0});f.sample.cancel=false;}
        if(mode==4){++f.sample.input.owner.space;++f.sample.nativeOwner.space;f.Tick(0,true,{.18f,0,0});}
        if(mode==5){++f.sample.input.owner.actorGeneration;++f.sample.nativeOwner.actorGeneration;f.Tick(0,true,{.18f,0,0});}
        if(mode==6){++f.sample.nativeOwner.weapon;f.Tick(0,true,{.18f,0,0});}
        if(mode==7){f.sample.input.nowNs+=100000000;f.Tick(0,true,{.18f,0,0});}
        f.Tick(0,true,{.18f,0,0});f.Tick(0,true,{.18f,0,0});
        CHECK(!f.supported.holding&&!f.result.tracking.supportCapture);
        const auto left=f.hands.Current(InteractionHand::Left);CHECK(!left||left->token.kind!=HandClaimKind::WeaponSupport);
        CHECK(f.native.released==1&&!f.native.acked);
    }
    Fixture outside;CHECK(outside.Grip());outside.EnableSupport();outside.native.deferReady=true;CHECK(OffAxisStroke(outside,.18f));
    outside.contact.distanceMeters=.281f;outside.Tick(0,true,{.18f,0,0});outside.Tick(0,true,{.18f,0,0});
    CHECK(!outside.supported.holding&&!outside.result.tracking.supportCapture);
    std::puts("Real release/focus/tracking/menu/lifetime/source expiry and retention limit still reject");return 0;
}
int FreshSupportStillUsesAcquisitionRadius(){
    Fixture f;f.Tick(0,false);const auto owner=f.SupportOwner();auto in=f.Input();
    f.support.Update(owner,in,{true,.18f,{}});f.Tick(0);in=f.Input();in.hands[0].squeeze=1;
    const auto attempted=f.support.Update(owner,in,{true,.18f,{}});CHECK(!attempted.holding&&attempted.reason==SupportRelease::Distance);
    std::puts("Ordinary fresh support acquisition remains16cm");return 0;
}

bool Stroke(Fixture& f){
    for(float t:{.02f,.04f,.06f,.08f,SpasObservedForeEndStroke,SpasObservedForeEndStroke,SpasObservedForeEndStroke,SpasObservedForeEndStroke,
                 .075f,.055f,.035f,.015f,0.f,0.f,0.f,0.f,0.f}){f.Tick(t);if(f.native.released)return true;}return false;
}
int FinishedStrokeReturnsSupportBeforeNativeReady(){
    Fixture f;CHECK(f.Grip());f.EnableSupport();f.native.deferReady=true;CHECK(Stroke(f));
    const auto submitted=f.native.pendingReady;CHECK(submitted);
    const auto gun=f.hands.Current(InteractionHand::Right);CHECK(gun);
    // Submission occurs on the current consumer tick; release evidence came
    // from the preceding rendered contact and has an older observed time.
    CHECK(f.pump.SupportReturnStarted(f.sample.input,*gun)==f.sample.input.nowNs);
    auto released=f.sample.input;released.released[0]=true;CHECK(!f.pump.SupportReturnStarted(released,*gun));
    auto stale=f.sample.input;stale.nowNs=stale.deadlineNs;CHECK(!f.pump.SupportReturnStarted(stale,*gun));
    auto replaced=*gun;++replaced.token.id;CHECK(!f.pump.SupportReturnStarted(f.sample.input,replaced));
    // The real hand has finished its stroke. Native completion may take many
    // frames, but must not make the squeezed hand let go of the closed pump.
    f.Tick(0);f.Tick(0);CHECK(f.supported.holding&&f.result.blocksFire);
    CHECK(!f.pump.SupportReturnStarted(f.sample.input,*f.hands.Current(InteractionHand::Right)));
    const auto token=f.supported.token;CHECK(token&&!f.native.acked);
    for(unsigned n=0;n<40;++n){f.Tick(0);CHECK(f.supported.holding&&f.supported.token==token);
        CHECK(f.result.blocksFire&&!f.result.tracking.target&&!f.result.ownsHand);}
    CHECK(f.native.pendingReady->release==submitted->release&&f.native.released==1&&!f.native.acked);
    f.native.Finish(f.sample.input.nowNs+10000000);f.Tick(0);
    CHECK(f.native.acked==1&&!f.result.blocksFire&&f.supported.holding&&f.supported.token==token);
    return 0;
}
int HeldPumpReturnsOnlyAfterNewClosedContact(){
    Fixture f;CHECK(f.Grip());f.EnableSupport();f.native.deferReady=true;CHECK(Stroke(f));
    CHECK(!f.supported.holding&&!f.hands.Current(InteractionHand::Left));
    f.Tick(0);CHECK(!f.supported.holding&&!f.native.acked); // raw source predates physical submission
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

int ReturnedContactKeepsClosedAxisAndHeldRadius(){
    const std::array<std::array<float,3>,3> axes{{{0,0,1},{1,0,0},{.6f,.8f,0}}};
    struct Case {float axial,lateral;bool accepted;};
    constexpr std::array<Case,6> cases{{{0,.27f,true},{.09f,.26f,true},
        {.101f,0,false},{-.101f,0,false},{.20f,0,false},{.09f,.27f,false}}};
    for(const auto axis:axes)for(const float direction:{-1.f,1.f})for(const auto test:cases){
        Fixture f;f.calibration->geometry.axis=axis;f.calibration->rearDirection=direction;
        CHECK(f.Grip());f.EnableSupport();f.native.deferReady=true;CHECK(Stroke(f));
        f.native.Finish(f.sample.input.nowNs);f.Tick(0);CHECK(!f.supported.holding);
        const std::array<float,3> lateral=axis[2]==1?std::array<float,3>{1,0,0}:std::array<float,3>{0,0,1};
        for(unsigned n=0;n<3;++n)f.prior.contact.values[3][n]+=axis[n]*direction*test.axial+lateral[n]*test.lateral;
        f.contact.distanceMeters=std::hypot(test.axial,test.lateral);
        f.Tick(0);
        CHECK(f.supported.holding==test.accepted);
        CHECK(bool(f.result.tracking.supportCapture)==test.accepted);
        CHECK(f.native.released==1&&f.native.acked==1);
    }
    std::puts("Closed-axis10cm and held total28cm remain independent across authored axes/signs");
    return 0;
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
int ContinuousStrokeReversesWithoutEndpointPause(){
    Fixture f;CHECK(f.Grip());
    // A person crosses the rear stop then immediately pushes forward. There
    // is no physical stop in the controller, so allow modest overtravel.
    for(float travel:{.025f,.055f,.085f,.108f,.077f,.043f,.012f,-.012f,-.012f})f.Tick(travel);
    CHECK(f.native.released==1);f.Tick(0);
    CHECK(f.native.acked==1&&!f.result.blocksFire);
    for(unsigned n=0;n<10;++n)f.Tick(0);CHECK(f.native.released==1);return 0;
}
int BriefUnavailableNativeReadDoesNotDestroyExistingGrasp(){
    Fixture f;CHECK(f.Grip());f.Tick(.02f);f.Tick(.02f);
    const auto claim=f.hands.Current(InteractionHand::Left);CHECK(claim);
    f.native.unavailable=true;f.Tick(.02f);
    CHECK(f.result.blocksFire&&!f.result.tracking.target);
    CHECK(f.hands.Current(InteractionHand::Left)&&f.hands.Current(InteractionHand::Left)->token==claim->token);
    f.native.unavailable=false;f.Tick(.02f);f.Tick(.02f);
    CHECK(f.result.ownsHand&&f.result.tracking.target);
    CHECK(Stroke(f));f.Tick(0);CHECK(f.native.released==1&&f.native.acked==1);return 0;
}
int BriefMissingHeldLeasePreservesOnlyOriginalGrasp(){
    Fixture f;CHECK(f.Grip());f.Tick(.04f);f.Tick(.04f);
    const auto claim=f.hands.Current(InteractionHand::Left);CHECK(claim);
    f.suppressHeld=true;f.freezeRaw=true;f.prior=f.sample.raw;f.Tick(.04f);f.Tick(.04f);
    CHECK(f.result.ownsHand&&f.result.blocksFire&&!f.result.tracking.target&&!f.result.tracking.held);
    const auto retained=f.hands.Current(InteractionHand::Left);CHECK(retained&&retained->token==claim->token);
    CHECK(retained->deadlineNs==f.sample.input.deadlineNs&&retained->inputSequence==f.sample.input.sequence);
    CHECK(!f.native.released&&!f.native.acked&&!f.native.cancelled);
    f.suppressHeld=false;f.freezeRaw=false;f.Tick(.04f);CHECK(f.result.ownsHand&&f.result.tracking.target);
    CHECK(Stroke(f));f.Tick(0);CHECK(f.native.released==1&&f.native.acked==1);return 0;
}
int MissingHeldCannotPreserveChangedOrExpiredCustody(){
    for(unsigned mode=0;mode<7;++mode){Fixture f;CHECK(f.Grip());f.Tick(.04f);f.Tick(.04f);
        f.suppressHeld=true;
        if(mode==0)++f.native.state.native.owner.weapon;
        if(mode==1)++f.native.state.cycle;
        if(mode==2)++f.native.state.shot;
        if(mode==3)f.native.state.heldSuspended=true;
        if(mode==4)f.native.state.phase=Bc2NativeCyclePhase::Cancelled;
        if(mode==5)f.sample.input.nowNs+=100000000;
        f.Tick(.04f,mode!=6);
        CHECK(!f.result.ownsHand&&!f.result.tracking.target&&!f.hands.Current(InteractionHand::Left));
        CHECK(f.result.blocksFire&&!f.native.released&&!f.native.acked);
    }
    // Live grip custody survives a missing render packet. A long pose gap
    // still cannot complete a gesture: when geometry returns it requires a
    // fresh neutral/regrip and full stroke of the same native debt.
    Fixture f;CHECK(f.Grip());f.Tick(.04f);f.suppressHeld=true;f.freezeRaw=true;
    for(unsigned n=0;n<20;++n)f.Tick(.04f);
    CHECK(f.result.ownsHand&&!f.native.released&&f.result.blocksFire&&!f.result.tracking.target);
    f.suppressHeld=false;f.freezeRaw=false;f.Tick(.04f);f.Tick(.04f);
    CHECK(!f.result.ownsHand&&!f.native.released);
    f.Tick(0,false);f.Tick(0,false);f.Tick(0);f.Tick(0);f.Tick(0);
    CHECK(f.result.ownsHand&&f.result.tracking.target&&Near(f.result.tracking.target->travel,0));
    CHECK(Stroke(f));f.Tick(0);CHECK(f.native.released==1&&f.native.acked==1&&!f.native.cancelled);return 0;
}
int FreshControllerCustodySurvivesNativeLeaseGapWithoutMotionAuthority(){
    Fixture f;CHECK(f.Grip());f.Tick(.04f);f.Tick(.04f);
    const auto original=f.hands.Current(InteractionHand::Left);CHECK(original);f.suppressHeld=true;f.freezeRaw=true;
    for(unsigned n=0;n<11;++n){f.Tick(.04f);
        const auto current=f.hands.Current(InteractionHand::Left);CHECK(current&&current->token==original->token);
        CHECK(f.result.ownsHand&&f.result.blocksFire&&!f.result.tracking.target&&!f.result.tracking.held);
        CHECK(current->inputSequence==f.sample.input.sequence&&current->deadlineNs==f.sample.input.deadlineNs);
        CHECK(!f.native.released&&!f.native.acked);
    }
    CHECK(f.sample.input.nowNs>original->deadlineNs);f.suppressHeld=false;f.freezeRaw=false;f.Tick(.04f);f.Tick(.04f);
    CHECK(f.result.ownsHand&&f.result.tracking.target);
    CHECK(Stroke(f));f.Tick(0);CHECK(f.native.released==1&&f.native.acked==1&&!f.native.cancelled);
    // A changed controller timestamp is not a recorded original observation.
    Fixture forged;CHECK(forged.Grip());forged.suppressHeld=true;forged.freezeRaw=true;
    const auto claim=forged.hands.Current(InteractionHand::Left);CHECK(claim);
    forged.native.state.held.reset();++forged.sample.input.observedNs;++forged.sample.input.nowNs;
    forged.result=forged.pump.Tick(forged.sample,forged.hands,forged.intent);
    CHECK(forged.hands.Current(InteractionHand::Left)->deadlineNs==claim->deadlineNs);
    CHECK(!forged.result.tracking.target&&!forged.native.released);return 0;
}
int RailRejectsPartialStrokeTeleportAndLostOwnership(){
    for(unsigned mode=0;mode<5;++mode){Fixture f;CHECK(f.Grip());
        if(mode==0){for(float p:{.025f,.055f,.075f,.043f,.012f,0.f,0.f})f.Tick(p);}
        if(mode==1){f.Tick(.30f);f.Tick(.30f);}
        if(mode==2){f.Tick(.08f,false);f.Tick(0);f.Tick(0);}
        if(mode==3){f.sample.input.tracked[0]=false;f.Tick(.10f);f.Tick(0);}
        if(mode==4){++f.sample.nativeOwner.equipGeneration;f.Tick(.10f);f.Tick(0);}
        CHECK(f.native.released==0&&f.native.acked==0&&f.result.blocksFire);
    }return 0;
}
int RailAllowsWristRollWithoutRotatingMeasuredPart(){
    Fixture f;CHECK(f.Grip());
    for(float t:{.025f,.055f,.085f,.108f,.077f,.043f,.012f,-.012f,-.012f}){
        // Original controller evidence rolls around the rail. The physical
        // part has one translation axis, so it must not inherit this roll.
        const float a=.8f;f.prior.contact.values[0][0]=std::cos(a);f.prior.contact.values[0][1]=std::sin(a);
        f.prior.contact.values[1][0]=-std::sin(a);f.prior.contact.values[1][1]=std::cos(a);
        f.Tick(t);if(f.result.tracking.target)CHECK(Near(f.result.tracking.target->rotation,0));
    }
    CHECK(f.native.released==1);f.Tick(0);CHECK(f.native.acked==1);return 0;
}
int Headset274MeasuredPullKeepsCustodyThroughVirtualStop(){
    // Original weapon-local contact positions from headset274. The first pull
    // lost custody at input2083 (6cm lateral limit); the second at2553 (10cm
    // overtravel limit). The log is sampled: interpolate these outbound points
    // at10ms, then supply a synthetic return. This is NOT a full input replay.
    const std::array<math::Vec3,3> lateral{{{-.00804333f,-.0856963f,-.810238f},
        {.0261178f,-.053712f,-.798077f},{.0455431f,-.0499521f,-.792034f}}};
    const std::array<math::Vec3,6> overshoot{{{-.0036383f,-.0640515f,-.811084f},
        {-.00595697f,-.0568113f,-.799832f},{-.0079426f,-.0502928f,-.786524f},
        {-.0137175f,-.0407747f,-.759087f},{-.0228139f,-.0336997f,-.718246f},
        {-.0336922f,-.023733f,-.606903f}}};
    const auto run=[](const auto& points){
        Fixture f;if(!f.Grip())return false;const auto token=f.hands.Current(InteractionHand::Left)->token;
        math::Vec3 prior{};const auto origin=points.front();
        for(const auto& point:points){
            const math::Vec3 relative{point.x-origin.x,point.y-origin.y,point.z-origin.z};
            for(unsigned n=1;n<=6;++n){const float a=float(n)/6;
                const math::Vec3 next{prior.x+(relative.x-prior.x)*a,prior.y+(relative.y-prior.y)*a,prior.z+(relative.z-prior.z)*a};
                f.Tick(next.z,true,next);
                if(!f.result.ownsHand||!f.result.tracking.target||f.hands.Current(InteractionHand::Left)->token!=token||
                   f.native.released||f.result.tracking.target->travel>SpasObservedForeEndStroke)return false;
            }prior=relative;
        }
        // Finish a genuine rear/forward stroke. Lateral movement alone must
        // not count; presentation remains on the measured part's single axis.
        for(float z:{.20f,.20f,.17f,.14f,.11f,.08f,.05f,.02f,0.f,0.f}){
            // The shorter first measured pull needs a continuous extension.
            const auto from=prior;
            for(unsigned n=1;n<=6;++n){auto next=from;next.z=from.z+(z-from.z)*float(n)/6;
                f.Tick(next.z,true,next);
                if(f.result.tracking.target){const auto p=BuildPumpPresentation(f.result.tracking,f.rig,Pose(),1,f.sample.input.nowNs);
                    if(!p.part||!Near(p.part->transform.values[3][0],0))return false;}
            }prior.z=z;
        }
        f.Tick(0);return f.native.released==1&&f.native.acked==1&&!f.result.blocksFire;
    };
    CHECK(run(lateral));CHECK(run(overshoot));return 0;
}
int SlidingGraspStillRejectsDistantReachAndCannotCompleteByLateralMotion(){
    Fixture lateral;CHECK(lateral.Grip());
    for(float x:{.025f,.05f,.075f,.1f,.1f,.075f,.05f,.025f,0.f,0.f})lateral.Tick(0,true,{x,0,0});
    CHECK(lateral.result.ownsHand&&lateral.native.released==0&&lateral.result.blocksFire);
    Fixture distant;CHECK(distant.Grip());
    for(unsigned n=1;n<=14;++n)distant.Tick(0,true,{float(n)*.025f,0,0});
    CHECK(!distant.result.ownsHand&&!distant.native.released&&distant.result.blocksFire);return 0;
}
int ClosedForeEndDoesNotReplayNativeStrokeBeforeManualGrip(){
    Fixture f;f.Tick(0,false);f.Shot();const auto nativeBytes=f.rig.nativeEvaluated;
    // The real symptom is a changing native child before a hand target exists.
    // Check multiple original stroke positions, then acquisition of the real rail.
    for(float nativeTravel:{0.f,.02f,.06f,.095f,.05f,0.f}){
        f.rig.evaluatedWorld[2]=Pose(-.84f+nativeTravel);f.Tick(0,false);
        CHECK(PumpRestFresh(f.result.tracking,f.sample.input.nowNs));
        const auto shown=BuildPumpPresentation(f.result.tracking,f.rig,Pose(2),2,f.sample.input.nowNs);
        CHECK(shown.part&&!shown.wrist&&shown.part->index==2);
        CHECK(Near(shown.part->transform.values[3][2],2-1.68f));
        CHECK(!f.result.ownsHand&&f.result.blocksFire&&!f.hands.Current(InteractionHand::Left));
        CHECK(!f.native.released&&!f.native.acked&&f.rig.nativeEvaluated==nativeBytes);
    }
    f.Tick(0);f.Tick(0);CHECK(f.result.tracking.target);
    f.Tick(.03f);f.Tick(.03f);
    const auto moving=BuildPumpPresentation(f.result.tracking,f.rig,Pose(),1,f.sample.input.nowNs);
    CHECK(moving.part&&moving.wrist&&moving.part->transform.values[3][2]>-.84f);
    CHECK(!PumpRestFresh(f.result.tracking,f.sample.input.nowNs));return 0;
}
int ClosedForeEndRequiresOriginalLifetimeAndMeasuredBinding(){
    Fixture f;f.Tick(0,false);const auto old=f.result.tracking;f.Tick(0,false);
    const auto now=f.sample.input.nowNs;
    CHECK(PumpPresentationRetained(old,f.result.tracking,now));
    CHECK(!PumpPresentationRetained(old,f.result.tracking,old.input.deadlineNs));
    for(unsigned mutation=0;mutation<9;++mutation){
        auto t=f.result.tracking;auto rig=f.rig;auto placed=Pose();float units=1;
        if(mutation==0)t.enabled=false;
        if(mutation==1)t.input.released[1]=true;
        if(mutation==2)++t.nativeOwner.soldier;
        if(mutation==3)++rig.identity.weak;
        if(mutation==4)rig.names[2]="unrelated-part";
        if(mutation==5)t.input.deadlineNs=now;
        if(mutation==6)units=std::numeric_limits<float>::quiet_NaN();
        if(mutation==7)placed.values[0][0]=2;
        if(mutation==8)t.target=PhysicalWeaponCycleTarget{}; // malformed target cannot become a rest pose
        CHECK(!BuildPumpPresentation(t,rig,placed,units,now).part);
    }
    auto changed=f.result.tracking;++changed.nativeOwner.weapon;
    CHECK(!PumpPresentationRetained(old,changed,now));
    changed=f.result.tracking;changed.calibration=std::make_shared<Bc2PumpCalibration>(*f.calibration);
    CHECK(!PumpPresentationRetained(old,changed,now));return 0;
}
int ClosedAndMovingPumpPalettesCannotCrossGestureBoundary(){
    Fixture f;f.Tick(0,false);const auto closed=f.result.tracking;
    CHECK(f.Grip());const auto moving=f.result.tracking;
    CHECK(!PumpPresentationRetained(closed,moving,f.sample.input.nowNs));
    f.Tick(.02f,false);
    CHECK(PumpRestFresh(f.result.tracking,f.sample.input.nowNs));
    CHECK(!PumpPresentationRetained(moving,f.result.tracking,f.sample.input.nowNs));
    const auto shown=BuildPumpPresentation(f.result.tracking,f.rig,Pose(),1,f.sample.input.nowNs);
    CHECK(shown.part&&!shown.wrist&&f.result.blocksFire&&!f.native.released);return 0;
}

void ConfigurePumpPoseFixture(Fixture& f){
    f.rig.names={"root","jntWpn_1","jntWpn_4","fixedRing","LeftShoulder","LeftElbow","LeftHand",
                 "RightShoulder","RightElbow","RightHand","unrelated"};
    f.rig.parents={-1,0,1,1,0,4,5,0,7,8,0};f.rig.identity.count=11;
    f.rig.left={4,5,6};f.rig.right={7,8,9};
    f.rig.world.assign(11,Pose());f.rig.inverseBind.assign(11,Pose());f.rig.evaluatedWorld.assign(11,Pose());
    f.rig.evaluatedWorld[4]=Pose(-2);f.rig.evaluatedWorld[5]=Pose(-1.9f);f.rig.evaluatedWorld[6]=Pose(-1.8f);
    f.rig.evaluatedWorld[7]=Pose(1);f.rig.evaluatedWorld[8]=Pose(1.1f);f.rig.evaluatedWorld[9]=Pose(1.2f);
    f.rig.nativeEvaluated.resize(11);for(auto& bytes:f.rig.nativeEvaluated)bytes.fill(std::byte{0x5a});
    static const std::array<Bc2PumpFixedPart,1> fixed{{{"fixedRing","jntWpn_1",Pose(.27f)}}};
    f.calibration->fixedParts=fixed;f.calibration->rigFingerprint=bc2_pump_detail::DerivePart(f.rig)->fingerprint;
}
std::vector<BoneWrite> PumpOrdinaryWrites(const Fixture& f,const math::Matrix4& placed){
    auto result=*RetargetRigSubtree(f.rig.parents,f.rig.evaluatedWorld,f.rig.weaponBone,placed);
    for(unsigned n=4;n<f.rig.identity.count;++n)result.push_back({n,f.rig.evaluatedWorld[n]});
    return result;
}
const BoneWrite* FindPumpPoseWrite(std::span<const BoneWrite> writes,unsigned index){
    const auto found=std::find_if(writes.begin(),writes.end(),[&](const auto& w){return w.index==index;});
    return found==writes.end()?nullptr:&*found;
}
auto PreparePumpPoseForTest(const Fixture& f,const math::Matrix4& placed,float units,std::span<const BoneWrite> ordinary,
             const ArmAnchor& anchor){
    auto result=BuildActivePumpPosePlan(f.result.tracking,f.rig,placed,units,f.sample.input.nowNs,
                                      ordinary,f.rig.evaluatedWorld,anchor);
#ifdef FVR_TEST_LEGACY_PUMP_IK_GATE
    // Source-derived 392 gate: pump/fixed writes existed only after successful
    // moving-hand IK. This is a pure counterfactual, not an old game execution.
    if(result&&!result->hand)result.reset();
#endif
    return result;
}
int FreshMechanicsSurviveUnreachableLeftHand(){
    for(float units:{1.f,2.f})for(float nativeStretch:{0.f,.15f}){
        Fixture f;ConfigurePumpPoseFixture(f);CHECK(f.Grip());f.Tick(.02f);f.Tick(.02f);
        CHECK(f.result.tracking.target&&PumpTargetFresh(f.result.tracking,f.sample.input.nowNs));
        f.rig.evaluatedWorld[2]=Pose(-.5f+nativeStretch);f.rig.evaluatedWorld[3]=Pose(nativeStretch);
        const auto native=f.rig.nativeEvaluated;const auto placed=Pose(5);const auto ordinary=PumpOrdinaryWrites(f,placed);
        const auto beforeControls=f.native.controls,beforeReleased=f.native.released,beforeAcked=f.native.acked;
        const auto right=f.hands.Current(InteractionHand::Right),left=f.hands.Current(InteractionHand::Left);
        const ArmAnchor anchor{{0,0,-2},{0,1,0}};
        const auto presented=BuildPumpPresentation(f.result.tracking,f.rig,placed,units,f.sample.input.nowNs);
        CHECK(presented.part&&presented.wrist&&presented.fixedAssembly.size()==1);
        const auto ik=SolveTrackedArms(f.rig.parents,f.rig.evaluatedWorld,f.rig.left,f.rig.right,
            {*presented.wrist,anchor.poleDirection,anchor.shoulder,true},{{},{},{},false});
        CHECK(ik&&ik->reachClamped[0]&&ik->targetError[0]>.001f*units);
        const auto plan=PreparePumpPoseForTest(f,placed,units,ordinary,anchor);
        CHECK(plan);CHECK(!plan->hand);
        const auto part=FindPumpPoseWrite(plan->mechanicalWrites,2),fixed=FindPumpPoseWrite(plan->mechanicalWrites,3);
        CHECK(part&&fixed&&part->transform.values==presented.part->transform.values);
        CHECK(Near(fixed->transform.values[3][2],5+.27f*units));
        CHECK(!Near(fixed->transform.values[3][2],FindPumpPoseWrite(ordinary,3)->transform.values[3][2]));
        for(unsigned n=4;n<11;++n)CHECK(FindPumpPoseWrite(plan->mechanicalWrites,n)->transform.values==FindPumpPoseWrite(ordinary,n)->transform.values);
        CHECK(plan->mechanicalPlan.identity==f.rig.identity&&f.rig.nativeEvaluated==native);
        CHECK(f.hands.Current(InteractionHand::Right)->token==right->token&&f.hands.Current(InteractionHand::Left)->token==left->token);
        CHECK(f.native.controls==beforeControls&&f.native.released==beforeReleased&&f.native.acked==beforeAcked);
    }
    return 0;
}
int CurrentPumpWristSurvivesAdmittedOverreach(){
    Fixture f;ConfigurePumpPoseFixture(f);CHECK(f.Grip());f.Tick(.02f);f.Tick(.02f);
    const auto placed=Pose(5);const auto shown=BuildPumpPresentation(f.result.tracking,f.rig,placed,1,f.sample.input.nowNs);CHECK(shown.wrist&&shown.part);
    const auto ordinary=PumpOrdinaryWrites(f,placed);const auto before=f.rig.nativeEvaluated;
    const auto left=f.hands.Current(InteractionHand::Left),right=f.hands.Current(InteractionHand::Right);
    const auto controls=f.native.controls,released=f.native.released;
    // Human390 shared arm drift reached0.20233804m;393 genuine active pump
    // rejection retained mechanical parts but no pump hand. Use that gap here.
    const ArmAnchor anchor{{0,0,shown.wrist->values[3][2]-.2f-.20233804f},{0,1,0}};
    const auto legacy=BuildActivePumpPosePlan(f.result.tracking,f.rig,placed,1,f.sample.input.nowNs,ordinary,f.rig.evaluatedWorld,anchor);
    CHECK(legacy&&!legacy->hand);
    const auto exact=BuildActivePumpPosePlan(f.result.tracking,f.rig,placed,1,f.sample.input.nowNs,ordinary,f.rig.evaluatedWorld,anchor,ArmReachPolicy::ExtendToAdmittedWrist);
    CHECK(exact&&exact->hand&&!exact->hand->reachClamped[0]&&exact->hand->targetError[0]==0&&exact->hand->segmentScale[0]>1);
    const auto wrist=FindPumpPoseWrite(exact->hand->writes,f.rig.left.wrist);CHECK(wrist&&wrist->transform.values==shown.wrist->values);
    CHECK(exact->mechanicalWrites.size()==legacy->mechanicalWrites.size());
    for(unsigned n=0;n<exact->mechanicalWrites.size();++n)CHECK(exact->mechanicalWrites[n].transform.values==legacy->mechanicalWrites[n].transform.values);
    CHECK(f.rig.nativeEvaluated==before&&f.native.controls==controls&&f.native.released==released);
    CHECK(f.hands.Current(InteractionHand::Left)->token==left->token&&f.hands.Current(InteractionHand::Right)->token==right->token);
    auto expired=f.result.tracking;expired.input.deadlineNs=f.sample.input.nowNs;
    CHECK(!BuildActivePumpPosePlan(expired,f.rig,placed,1,f.sample.input.nowNs,ordinary,f.rig.evaluatedWorld,anchor,ArmReachPolicy::ExtendToAdmittedWrist));
    return 0;
}
int ReachableHandStillHasIndependentCurrentSolution(){
    Fixture f;ConfigurePumpPoseFixture(f);CHECK(f.Grip());f.Tick(.02f);f.Tick(.02f);
    const auto placed=Pose(5);const auto p=BuildPumpPresentation(f.result.tracking,f.rig,placed,1,f.sample.input.nowNs);CHECK(p.wrist);
    const float target=p.wrist->values[3][2];
    f.rig.evaluatedWorld[4]=Pose(target-.15f);f.rig.evaluatedWorld[5]=Pose(target-.05f);
    f.rig.evaluatedWorld[5].values[3][0]=.08f;f.rig.evaluatedWorld[6]=Pose(target+.05f);
    const auto ordinary=PumpOrdinaryWrites(f,placed);const ArmAnchor anchor{{0,0,target-.15f},{1,0,0}};
    auto plan=PreparePumpPoseForTest(f,placed,1,ordinary,anchor);CHECK(plan&&plan->hand&&!plan->hand->reachClamped[0]);
    CHECK(plan->hand->targetError[0]<=.001f);
    CHECK(std::none_of(plan->hand->writes.begin(),plan->hand->writes.end(),[](const auto& w){return w.index==2||w.index==3||w.index>=7;}));
    CHECK(FindPumpPoseWrite(plan->mechanicalWrites,6)->transform.values==FindPumpPoseWrite(ordinary,6)->transform.values);
    CHECK(FindPumpPoseWrite(plan->hand->writes,6)->transform.values!=FindPumpPoseWrite(ordinary,6)->transform.values);
    return 0;
}
int FreshIdentityAndLeaseStillRequired(){
    Fixture f;ConfigurePumpPoseFixture(f);CHECK(f.Grip());const auto placed=Pose(5);const auto ordinary=PumpOrdinaryWrites(f,placed);
    const ArmAnchor anchor{{0,0,-2},{0,1,0}};
    for(unsigned mutation=0;mutation<10;++mutation){
        auto tracking=f.result.tracking;auto rig=f.rig;auto now=f.sample.input.nowNs;
        if(mutation==0)now=tracking.input.deadlineNs;
        if(mutation==1)tracking.input.tracked[0]=false;
        if(mutation==2)tracking.input.released[1]=true;
        if(mutation==3)++tracking.nativeOwner.weak;
        if(mutation==4)++rig.identity.weak;
        if(mutation==5)++tracking.mechanism->token.id;
        if(mutation==6)tracking.held->deadlineNs=now;
        if(mutation==7)tracking.target.reset();
        if(mutation==8)rig.nativeHiddenLeaves={2};
        if(mutation==9)tracking.gun.reset();
        CHECK(!BuildActivePumpPosePlan(tracking,rig,placed,1,now,ordinary,rig.evaluatedWorld,anchor));
    }
    CHECK(!PumpPresentationRetained(f.result.tracking,f.result.tracking,f.result.tracking.input.deadlineNs));
    return 0;
}

void AddMeasuredCompanions(Fixture& f){
    for(const auto name:{"jntWpn_3","jntWpn_5","jntWpn_2","jntWpn_7"}){f.rig.names.push_back(name);f.rig.parents.push_back(1);}
    f.rig.world.resize(8,Pose());f.rig.inverseBind.resize(8,Pose());f.rig.evaluatedWorld.resize(8,Pose());f.rig.nativeEvaluated.resize(8);
    f.rig.identity.count=8;
    f.calibration->companions=bc2_pump_detail::SpasCycleCompanions;
    f.calibration->fixedParts=std::span(&bc2_pump_detail::SpasFixedParts.back(),1);
    f.calibration->rigFingerprint=bc2_pump_detail::DerivePart(f.rig)->fingerprint;
}
int RecordedNativeCycleCannotMoveClosedCompanions(){
    Fixture f;AddMeasuredCompanions(f);
    // Actual406 sources100..127 retain a zero-travel rest plan while the
    // visible ring/ejection region detaches. The installed authored clip's
    // measured rear poses reproduce the omitted rigid companions here.
    f.rig.evaluatedWorld[4]=math::Matrix4{{{{{0.999999967f,5.39226403e-06f,-0.000258583245f,0.f}},{{-5.39365856e-06f,1.f,-5.39226403e-06f,0.f}},{{0.000258583216f,5.39365856e-06f,0.999999967f,-0.f}},{{0.00401766328f,0.010607865f,-0.445424186f,1.f}}}}};
    f.rig.evaluatedWorld[5]=math::Matrix4{{{{{1.f,5.39297563e-06f,5.39294655e-06f,0.f}},{{-5.39294655e-06f,1.f,-5.39297563e-06f,0.f}},{{-5.39297563e-06f,5.39294655e-06f,1.f,-0.f}},{{0.0223097206f,-0.0223100893f,-0.253024652f,1.f}}}}};
    const auto nativeBefore=f.rig.evaluatedWorld;const auto rawBefore=f.rig.nativeEvaluated;
    f.Tick(0,false);const auto tracking=f.result.tracking;
    const auto shown=BuildPumpPresentation(tracking,f.rig,Pose(),1,f.sample.input.nowNs);
    CHECK(shown.part&&!shown.wrist&&shown.fixedAssembly.size()==1&&shown.cycleAssembly.size()==1);
    for(unsigned n=0;n<1;++n){const auto& data=f.calibration->companions[n];const auto& write=shown.cycleAssembly[n];
        CHECK(write.index==n+4&&write.transform.values==data.closed.values);
        // The406 renderer kept this native delta. It exceeds the observed
        // visible defect's minimum practical magnitude without a manual pull.
        CHECK(reload_insertion_detail::Distance(nativeBefore[n+4],data.closed)>.06f);
    }
    CHECK(shown.fixedAssembly[0].index==5&&shown.fixedAssembly[0].transform.values==bc2_pump_detail::SpasFixedParts.back().inWeapon.values);
    CHECK(reload_insertion_detail::Distance(nativeBefore[5],shown.fixedAssembly[0].transform)>.08f);
    for(unsigned n=0;n<8;++n)CHECK(f.rig.evaluatedWorld[n].values==nativeBefore[n].values);CHECK(f.rig.nativeEvaluated==rawBefore);
    CHECK(std::none_of(shown.cycleAssembly.begin(),shown.cycleAssembly.end(),[](const auto& w){return w.index==6||w.index==7;}));
    auto stale=tracking;stale.input.deadlineNs=f.sample.input.nowNs;
    CHECK(!BuildPumpPresentation(stale,f.rig,Pose(),1,f.sample.input.nowNs).part);
    auto owner=tracking;++owner.nativeOwner.weak;CHECK(!BuildPumpPresentation(owner,f.rig,Pose(),1,f.sample.input.nowNs).part);
    auto released=tracking;released.input.released[1]=true;CHECK(!BuildPumpPresentation(released,f.rig,Pose(),1,f.sample.input.nowNs).part);
    return 0;
}
int ActualPhysicalLegDrivesMeasuredCompanions(){
    Fixture f;AddMeasuredCompanions(f);CHECK(f.Grip());
    for(float x:{.02f,.04f,.06f,.06f})f.Tick(x);
    CHECK(f.result.tracking.target&&!f.result.tracking.companionReturning);
    const auto outward=f.result.tracking;const auto shown=BuildPumpPresentation(outward,f.rig,Pose(),1,f.sample.input.nowNs);
    CHECK(shown.part&&shown.wrist&&shown.cycleAssembly.size()==1);
    const auto expected=BuildPumpCompanions(f.rig,Pose(),1,f.calibration->geometry,f.calibration->companions,outward.target->travel,false);
    CHECK(expected&&expected->size()==1&&shown.cycleAssembly[0].transform.values==(*expected)[0].transform.values);
    for(float x:{.08f,SpasObservedForeEndStroke,SpasObservedForeEndStroke,.08f,.06f,.06f})f.Tick(x);
    CHECK(f.result.tracking.target&&f.result.tracking.companionReturning);
    const auto returned=BuildPumpPresentation(f.result.tracking,f.rig,Pose(),1,f.sample.input.nowNs);
    const auto forward=BuildPumpCompanions(f.rig,Pose(),1,f.calibration->geometry,f.calibration->companions,f.result.tracking.target->travel,true,f.result.tracking.profile.rearTolerance);
    CHECK(forward&&returned.cycleAssembly.size()==1&&returned.cycleAssembly[0].transform.values==(*forward)[0].transform.values);
    CHECK(reload_insertion_detail::Distance(returned.cycleAssembly[0].transform,shown.cycleAssembly[0].transform)>.005f);
    // The source clock alone cannot animate the retained original target.
    auto later=f.result.tracking;const auto now=f.sample.input.nowNs+1;
    const auto repeated=BuildPumpPresentation(later,f.rig,Pose(),1,now);
    CHECK(repeated.cycleAssembly[0].transform.values==returned.cycleAssembly[0].transform.values);
    // Jitter back toward the rear stop cannot re-enter the outward leg.
    for(float x:{.075f,.085f,.08f,.08f}){
        f.Tick(x);CHECK(f.result.tracking.target&&f.result.tracking.companionReturning);
    }
    f.Tick(0,false);CHECK(!f.result.tracking.target&&!f.result.ownsHand&&f.native.released==0);
    return 0;
}
int Captured407DetachmentCannotBecomeMechanicalMotion(){
    Fixture f;AddMeasuredCompanions(f);CHECK(f.Grip());
    const auto& closed=bc2_pump_detail::SpasCycleCompanions[0].closed;
    const auto& ring=bc2_pump_detail::SpasFixedParts.back().inWeapon;
    // The installed407 path used by actual pair125 displaced this fixed loop
    //83.9mm and moved the bolt off-axis. Preserve that failed data explicitly.
    const auto rejectedRing=math::Matrix4{{{{{1.f,5.39297563e-06f,5.39294655e-06f,0.f}},{{-5.39294655e-06f,1.f,-5.39297563e-06f,0.f}},{{-5.39297563e-06f,5.39294655e-06f,1.f,-0.f}},{{0.0223097206f,-0.0223100893f,-0.253024652f,1.f}}}}};
    const auto rejectedBolt=math::Matrix4{{{{{0.999999967f,5.39226403e-06f,-0.000258583245f,0.f}},{{-5.39365856e-06f,1.f,-5.39226403e-06f,0.f}},{{0.000258583216f,5.39365856e-06f,0.999999967f,-0.f}},{{0.00401766328f,0.010607865f,-0.445424186f,1.f}}}}};
    CHECK(reload_insertion_detail::Distance(rejectedRing,ring)>.08f);
    CHECK(std::hypot(rejectedBolt.values[3][0]-closed.values[3][0],rejectedBolt.values[3][1]-closed.values[3][1])>.029f);
    for(bool returning:{false,true})for(unsigned n=0;n<=256;++n){
        const auto travel=SpasObservedForeEndStroke*n/256;
        const auto writes=BuildPumpCompanions(f.rig,Pose(),1,f.calibration->geometry,f.calibration->companions,travel,returning,.006f);
        CHECK(writes&&writes->size()==1&&writes->front().index==4);
        const auto& bolt=writes->front().transform;
        for(unsigned row=0;row<4;++row)for(unsigned col=0;col<4;++col)
            if(row!=3||col!=2)CHECK(Near(bolt.values[row][col],closed.values[row][col],.000001f));
        CHECK(bolt.values[3][2]>=closed.values[3][2]-.000001f&&bolt.values[3][2]<=closed.values[3][2]+.067596f);
        const auto fixed=bc2_pump_detail::FixedAssembly(f.rig,Pose(),1,f.calibration->geometry,f.calibration->fixedParts);
        CHECK(fixed&&fixed->size()==1&&fixed->front().index==5&&fixed->front().transform.values==ring.values);
    }
    // Missing, hidden or differently parented ring cannot be silently skipped.
    for(unsigned fault=0;fault<3;++fault){auto rig=f.rig;
        if(fault==0)rig.names[5]="unbound_ring";
        if(fault==1)rig.parents[5]=2;
        if(fault==2)rig.nativeHiddenLeaves={5};
        CHECK(!BuildPumpPresentation(f.result.tracking,rig,Pose(),1,f.sample.input.nowNs).part);
    }
    CHECK(f.native.released==0&&f.native.acked==0);
    return 0;
}
int CompanionGeometryIsRigidBoundedAndIndependent(){
    Fixture f;AddMeasuredCompanions(f);const auto& data=bc2_pump_detail::SpasCycleCompanions;
    CHECK(PumpCompanionsValid(f.calibration->geometry,data));
    for(bool returning:{false,true})for(unsigned n=0;n<=256;++n){
        const auto writes=BuildPumpCompanions(f.rig,Pose(),2,f.calibration->geometry,data,SpasObservedForeEndStroke*n/256,returning);
        CHECK(writes&&writes->size()==1);for(const auto& w:*writes)CHECK(reload_insertion_detail::Rigid(w.transform));
    }
    const auto rear=BuildPumpCompanions(f.rig,Pose(),1,f.calibration->geometry,data,SpasObservedForeEndStroke,false);
    const auto reverse=BuildPumpCompanions(f.rig,Pose(),1,f.calibration->geometry,data,SpasObservedForeEndStroke,true);
    CHECK(rear&&reverse&&(*rear)[0].transform.values==(*reverse)[0].transform.values);
    for(unsigned mutation=0;mutation<5;++mutation){auto rig=f.rig;
        if(mutation==0)rig.parents[4]=2;
        if(mutation==1)rig.nativeHiddenLeaves={4};
        if(mutation==2)rig.names[4]="unknown_part";
        if(mutation==3)rig.names[5]=rig.names[4];
        if(mutation==4)rig.parents[7]=4;
        CHECK(!BuildPumpCompanions(rig,Pose(),1,f.calibration->geometry,data,.04f,false));
    }
    const auto threshold=SpasObservedForeEndStroke-.006f;
    const auto atTurnRear=BuildPumpCompanions(f.rig,Pose(),1,f.calibration->geometry,data,threshold,false,.006f);
    const auto atTurnForward=BuildPumpCompanions(f.rig,Pose(),1,f.calibration->geometry,data,threshold,true,.006f);
    const auto justReturned=BuildPumpCompanions(f.rig,Pose(),1,f.calibration->geometry,data,threshold-.000001f,true,.006f);
    CHECK(atTurnRear&&atTurnForward&&justReturned);
    for(unsigned n=0;n<1;++n){
        CHECK((*atTurnRear)[n].transform.values==(*atTurnForward)[n].transform.values);
        CHECK(reload_insertion_detail::Distance((*atTurnForward)[n].transform,(*justReturned)[n].transform)<.00001f);
    }
    const std::array<Bc2PumpCompanionPart,2> duplicate{{data[0],data[0]}};CHECK(!PumpCompanionsValid(f.calibration->geometry,duplicate));
    auto malformed=data;malformed[0].parent="other";CHECK(!PumpCompanionsValid(f.calibration->geometry,malformed));
    const std::array<Bc2PumpFixedPart,1> collision{{{data[0].name,data[0].parent,data[0].closed}}};
    CHECK(!PumpCompanionsValid(f.calibration->geometry,data,collision));
    CHECK(!BuildPumpCompanions(f.rig,Pose(),1,f.calibration->geometry,data,std::numeric_limits<float>::quiet_NaN(),false));
    CHECK(!BuildPumpCompanions(f.rig,Pose(),1,f.calibration->geometry,data,SpasObservedForeEndStroke+.001f,false));
    return 0;
}

}
#include "PumpGeometry375Checks.inc"
int main(int argc,char** argv){
    if(PredispatchExpiredReleaseNeedsNewGesture(0)||PredispatchExpiredReleaseNeedsNewGesture(2))return 1;
    if(BusyControlRetainsOnlyCurrentGrasp()||BusyReleaseRetriesExactOriginalPacket()||ExpiredUndispatchedReleaseNeedsNewWholeStroke()||UndispatchedRequestCannotCrossSourceLossOrRenewCycleDeadline())return 1;
    if(argc==2&&std::string_view(argv[1])=="--companions")return Captured407DetachmentCannotBecomeMechanicalMotion()||RecordedNativeCycleCannotMoveClosedCompanions()||ActualPhysicalLegDrivesMeasuredCompanions()||CompanionGeometryIsRigidBoundedAndIndependent();
    if(Captured407DetachmentCannotBecomeMechanicalMotion()||RecordedNativeCycleCannotMoveClosedCompanions()||ActualPhysicalLegDrivesMeasuredCompanions()||CompanionGeometryIsRigidBoundedAndIndependent())return 1;
    if(argc==2&&std::string_view(argv[1])=="--held-endpoint")return OffAxisHeldEndpointReturnsSupport()||OffAxisReturnHonorsActualCancellation()||FreshSupportStillUsesAcquisitionRadius();
    if(OffAxisHeldEndpointReturnsSupport()||OffAxisReturnHonorsActualCancellation()||FreshSupportStillUsesAcquisitionRadius())return 1;
    if(CurrentPumpWristSurvivesAdmittedOverreach()||FreshMechanicsSurviveUnreachableLeftHand()||ReachableHandStillHasIndependentCurrentSolution()||FreshIdentityAndLeaseStillRequired()||
    FinishedStrokeReturnsSupportBeforeNativeReady()||SharedPumpUsesExplicitFamilyGeometry()||ClosedForeEndDoesNotReplayNativeStrokeBeforeManualGrip()||ClosedForeEndRequiresOriginalLifetimeAndMeasuredBinding()||
    ClosedAndMovingPumpPalettesCannotCrossGestureBoundary()||ActualMeasuredCalibrationIsExplicit()||RendererToPhysicalToNativeAndPart()||ReleaseImmediatelyRevokesPresentation()||
    OldPaletteCannotBeRenewed()||WrongNativeOrGeometryCannotAcquire()||FailedControlSuppressesFireAndDropsOnlyMechanism()||
    MissingNativeViewPreservesObligation()||BindingPointCannotChangeDuringCycle()||
    HeldPumpReturnsOnlyAfterNewClosedContact()||ReturnedSupportCaptureIsClosedAndTokenBound()||
    ActualReleaseAndLossConsumePumpReturn()||ReturnNeedsActualOriginalHistoryAndMatchingGeometry()||PendingReleaseDoesNotRequestNewControllerAuthority()||
    IdleYieldAllowsShellCustodyWithoutCancellingDebt()||OrdinaryForeEndReleaseRegripsSameDebtAndRequiresFullStroke()||
    ContinuousStrokeReversesWithoutEndpointPause()||BriefUnavailableNativeReadDoesNotDestroyExistingGrasp()||
    BriefMissingHeldLeasePreservesOnlyOriginalGrasp()||MissingHeldCannotPreserveChangedOrExpiredCustody()||
    FreshControllerCustodySurvivesNativeLeaseGapWithoutMotionAuthority()||
    RailRejectsPartialStrokeTeleportAndLostOwnership()||RailAllowsWristRollWithoutRotatingMeasuredPart()||
    Headset274MeasuredPullKeepsCustodyThroughVirtualStop()||SlidingGraspStillRejectsDistantReachAndCannotCompleteByLateralMotion())return 1;
    if(ReturnedContactKeepsClosedAxisAndHeldRadius())return 1;
    std::puts("39 physical pump adapter groups passed; original renderer packets, real shared consumer/private part, mocked native service.");}


