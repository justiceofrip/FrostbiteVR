#include "Bc2PumpControllerProbe.h"
#include "Bc2PumpDiagnostic.h"
#include "PhysicalPumpProbeInput.h"
#include "Test.h"
#include <cstdio>
#include <iostream>
using namespace fvr;using namespace fvr::bc2;using namespace fvr::interaction;
namespace {
math::Matrix4 Matrix(float x=0,float y=0,float z=0){auto m=reload_insertion_detail::Identity();m.values[3]={x,y,z,1};return m;}
struct Native {
    Bc2NativeCycleView view{};Bc2NativeCycleControl control{};std::optional<WeaponCycleRelease> pending;
    unsigned fired=0,released=0,acked=0;std::uint64_t sequence=0;std::int64_t fireAt=0,releaseAt=0;bool wasFire=false;
    Bc2PhysicalPumpApi Api(){return {this,
        [](void* p,const Bc2NativeCycleControl& c)noexcept{auto& n=*static_cast<Native*>(p);n.control=c;
            if(c.release){if(n.pending||!n.view.held||c.release->cycle!=*n.view.held)return false;
                n.pending=c.release;n.releaseAt=c.input.nowNs;++n.released;n.view.phase=Bc2NativeCyclePhase::Releasing;n.view.held.reset();}return true;},
        [](void* p,std::int64_t)noexcept->std::optional<Bc2NativeCycleView>{return static_cast<Native*>(p)->view;},
        [](void* p,const WeaponCycleReady& r)noexcept{auto& n=*static_cast<Native*>(p);
            if(!n.view.ready||n.view.ready->release!=r.release)return false;++n.acked;n.view.ready.reset();n.pending.reset();n.view.blocksFire=false;return true;},
        [](void* p)noexcept{auto& n=*static_cast<Native*>(p);if(n.view.blocksFire){n.view.phase=Bc2NativeCyclePhase::Cancelled;n.view.held.reset();}}};}
    void Input(const ActionOutput& in,std::int64_t now){
        const bool fire=in.active&&(in.held&Fire);
        if(fire&&!wasFire&&!view.blocksFire){fireAt=now;++fired;}wasFire=fire;
        if(fireAt&&now-fireAt>=120000000){fireAt=0;view.phase=Bc2NativeCyclePhase::Held;view.blocksFire=true;
            ++view.cycle;++view.shot;view.loaded=8-int(fired);view.reserve=24;view.capacity=8;
            view.native.owner=control.nativeOwner;view.native.firing={0x14000,0x15000,0x16000};}
        if(view.phase==Bc2NativeCyclePhase::Held)view.held=WeaponCycleLease{control.input.owner,control.item,control.mechanism,
            view.cycle,view.shot,++sequence,now-1,now+50000000,true};
        if(pending&&now-releaseAt>=200000000){view.phase=Bc2NativeCyclePhase::Complete;
            if(!view.ready)view.ready=WeaponCycleReady{*pending,++sequence,now,now+50000000,true,true};}
    }
};
struct Fixture {
    RigSnapshot rig;std::shared_ptr<Bc2PumpCalibration> calibration=std::make_shared<Bc2PumpCalibration>();
    Native native;HandInteraction hands;Bc2PhysicalPump pump;Bc2PumpControllerProbe driver;ControllerActions actions;SupportGrip support;unsigned supportReturns=0;bool publishSupport=true,forgeSupport=false;
    Bc2PhysicalPumpSample sample{};InputFrame input{};Bc2PumpRawContact raw{};Bc2PumpPackCounters packs{};
    std::uint64_t intent=0;std::int64_t now=1000000000;bool publishPairs=true,forgeRaw=false,mapBeforePrepare=false;
    static Bc2PumpCalibration Calibration(){Bc2PumpCalibration c;c.revision=1;c.closedPart=Matrix(0,0,-.84f);
        c.closedWrist=Matrix(-.2f,-.25f,.45f);c.rearDirection=1;c.measured=true;return c;}
    static RigSnapshot Rig(){RigSnapshot r;r.names={"root","jntWpn_1","jntWpn_4","LeftHand"};r.parents={-1,0,1,0};r.weaponBone=1;
        r.identity={0x11000,0x12000,0x30000,0x40000,0x50000,0x60000,0x70000,0x80000,0x90000,4,false};
        r.world.assign(4,Matrix());r.inverseBind.assign(4,Matrix());r.evaluatedWorld.assign(4,Matrix());r.nativeEvaluated.resize(4);
        for(auto& b:r.nativeEvaluated)b.fill(std::byte{0x5a});return r;}
    static Bc2PumpCalibration Bound(){auto c=Calibration();c.rigFingerprint=bc2_pump_detail::DerivePart(Rig())->fingerprint;return c;}
    explicit Fixture(unsigned cycles=2):rig(Rig()),calibration(std::make_shared<Bc2PumpCalibration>(Bound())),pump(calibration,native.Api()),driver(*calibration,cycles){
        sample.nativeOwner={0x10000,0x11000,0x12000,0x13000,1,2,3};sample.item={sample.nativeOwner.weapon,4};sample.asset=SpasReloadAsset;
        sample.input.owner={(std::uint64_t(sample.nativeOwner.weak)<<32)|sample.nativeOwner.soldier,1,4,3};
        input.spaceGeneration=3;input.focused=input.headValid=true;
        for(auto& h:input.hands){h.active=Components;h.gripTracked=h.aimTracked=true;}
    }
    void Tick(){
        now+=10000000;++input.generation;input.predictedNs=now;probe::PhysicalPumpInput(input,0,false);
        auto observed=raw;if(forgeRaw)++observed.input.observedNs;
        const InputOwner owner{sample.nativeOwner.soldier,sample.nativeOwner.equipGeneration,true,true};
        if(mapBeforePrepare)actions.Update(input,owner,now);
        driver.Prepare(input,sample.nativeOwner,sample.asset,observed,native.view,now,now+100000000,now);
        // Same mapper as Gather: native Fire must come from the prepared packet,
        // never directly from the fixture's raw trigger field.
        auto safeOwner=owner;safeOwner.playing=!driver.Failed();
        native.Input(actions.Update(input,safeOwner,now),now);
        auto& s=sample.input;s.sequence=input.generation;s.observedNs=s.nowNs=now;s.deadlineNs=now+100000000;
        s.focused=true;s.tracked={true,true};s.released={input.hands[0].squeeze<=.35f,false};
        hands.Update(s);const auto current=hands.Current(InteractionHand::Right);
        if(current)hands.Renew(s,current->token,{{1,4},s.sequence,s.deadlineNs,true});
        else hands.Acquire(s,{s.owner,InteractionHand::Right,HandClaimKind::GunHold,sample.item,{{1,4},s.sequence,s.deadlineNs,true},++intent,0});
        sample.raw=raw;sample.grip=input.hands[0].squeeze>.35f;sample.cancel=driver.Failed();
        auto out=pump.Tick(sample,hands,intent);
        const SupportGripOwner supportOwner{s.owner.actor,s.owner.actorGeneration,sample.nativeOwner.weapon};
        const SupportGripContact contact{true,.01f,{}};
        auto proposed=support;
        auto returned=pump.ContinueSupport(supportOwner,input,contact,sample.raw.input,{2,sample.nativeOwner.weapon},hands,proposed,intent,sample.cancel||out.ownsHand);
        auto supported=returned?*returned:proposed.Update(supportOwner,input,contact,sample.cancel,out.ownsHand);
        if(returned)++supportReturns;
        if(supported.holding){
            const auto left=hands.Current(InteractionHand::Left),gun=hands.Current(InteractionHand::Right);
            const HandContactProof proof{{2,sample.nativeOwner.weapon},sample.raw.input.sequence,sample.raw.input.deadlineNs,true};
            bool owned=false;
            if(left&&left->token.kind==HandClaimKind::WeaponSupport)
                owned=hands.RenewFrom(s,sample.raw.input,left->token,proof).accepted;
            else if(supported.engaged&&gun){
                const HandClaimRequest request{s.owner,InteractionHand::Left,HandClaimKind::WeaponSupport,sample.item,proof,++intent,gun->token.id};
                owned=(left?hands.TransferFrom(s,sample.raw.input,left->token,request):hands.AcquireFrom(s,sample.raw.input,request)).accepted;
            }
            if(!owned){supported=support.Update(supportOwner,input,contact,true);proposed=support;
                const auto claim=hands.Current(InteractionHand::Left);
                if(claim&&claim->token.kind==HandClaimKind::WeaponSupport)hands.Release(s,claim->token);}
        }else {const auto claim=hands.Current(InteractionHand::Left);
            if(claim&&claim->token.kind==HandClaimKind::WeaponSupport)hands.Release(s,claim->token);}
        support=proposed;
        pump.BindSupport(out.tracking,supported,hands);
        if(out.tracking.target&&publishPairs){const auto presentation=BuildPumpPresentation(out.tracking,rig,Matrix(),1,now);
            if(presentation.wrist&&presentation.part){++packs.poses;packs.copies+=2;++packs.pairs;}}
        driver.Observe(out,native.view,packs,now);
        auto observedSupport=out.tracking;if(forgeSupport&&observedSupport.supportCapture)++observedSupport.supportCapture->support.id;
        if(publishSupport)driver.ObserveSupport(observedSupport,supported,now);
        const auto controller=InverseRigid(*math::MakeLhViewFromOpenXRPose(input.hands[0].grip));
        const auto body=Matrix();raw=BuildPumpRawContact(out.tracking,rig,*controller,Matrix(),{.02f,0,0},1,now,&body);
        if(raw.valid)++packs.contacts;
    }
};
int FiniteClosedLoopUsesConsumer(){for(unsigned count:{1u,2u,8u}){Fixture f(count);const auto original=f.rig.nativeEvaluated;
    for(unsigned i=0;i<5600&&!f.driver.Completed()&&!f.driver.Failed();++i)f.Tick();
    if(f.driver.Failed()){f.driver.Report(std::cerr);std::cerr<<'\n';}
    CHECK(f.driver.Completed());CHECK(f.native.fired==count&&f.native.released==count&&f.native.acked==count&&f.supportReturns==count);
    CHECK(f.native.view.loaded==8-int(count)&&f.native.view.reserve==24&&!f.native.view.blocksFire);
    CHECK(f.packs.pairs>50&&f.rig.nativeEvaluated==original);
    for(unsigned i=0;i<40;++i)f.Tick();CHECK(f.native.fired==count&&f.input.hands[1].trigger==0&&f.input.hands[0].squeeze==0);
    }return 0;}
int RendererSourceCannotBeRestamped(){Fixture f;f.forgeRaw=true;for(unsigned i=0;i<400;++i)f.Tick();
    CHECK(f.native.fired==0&&f.native.released==0&&f.driver.Current()==Bc2PumpControllerProbe::Phase::Warmup);return 0;}
int PairedPresentationRequired(){Fixture f;f.publishPairs=false;for(unsigned i=0;i<1600&&!f.driver.Failed();++i)f.Tick();
    CHECK(f.driver.Failed()&&!f.driver.Completed()&&f.native.fired==1&&f.native.released==0);return 0;}
int ControllerInverseRejectsWrongOriginal(){Fixture f;f.Tick();f.Tick();CHECK(f.raw.mappingValid);
    auto raw=f.raw;auto original=f.input;const auto desired=raw.rawWristWorldMeters;
    CHECK(PumpProbeController(raw,original,desired));++original.generation;CHECK(!PumpProbeController(raw,original,desired));
    original=f.input;raw.mappingValid=false;CHECK(!PumpProbeController(raw,original,desired));
    raw=f.raw;raw.rawWristWorldMeters.values[3][0]+=.003f;CHECK(!PumpProbeController(raw,original,desired));return 0;}
int CapabilityAndReceiverAreExplicit(){constexpr unsigned flags=9u|0x197800u;
    for(auto mode:{PumpHoldDiagnostic::SpasPhysicalCycle,PumpHoldDiagnostic::SpasPhysicalCycleOnce}){
        CHECK(ValidPumpHoldDiagnostic(mode,flags,30000));CHECK(ValidPumpHoldDiagnostic(mode,flags|0x200u,30000));
        CHECK(!ValidPumpHoldDiagnostic(mode,flags,15000));
        for(unsigned bit=8;bit<32;++bit)if((1u<<bit)!=0x200u)CHECK(!ValidPumpHoldDiagnostic(mode,flags^(1u<<bit),30000));}
    CHECK(ValidPumpHoldDiagnostic(PumpHoldDiagnostic::SpasPhysicalEmpty,flags,60000));
    CHECK(!ValidPumpHoldDiagnostic(PumpHoldDiagnostic::SpasPhysicalEmpty,flags,30000));
    CHECK(PumpPhysicalCycles(PumpHoldDiagnostic::SpasPhysicalEmpty)==8);
    CHECK(!ValidPumpHoldDiagnostic(PumpHoldDiagnostic(99),flags,30000));CHECK(PumpPhysicalCycles(PumpHoldDiagnostic::SpasOneShot)==0);
    CHECK(probe::PhysicalPumpReceiverOption(L"--physical-pump-probe"));CHECK(!probe::PhysicalPumpReceiverOption(L"--controls-fire"));
    InputFrame in;for(unsigned ms:{0u,399u,400u,499u,500u,3000u,6000u,29999u}){
        probe::PhysicalPumpInput(in,ms,true);CHECK(probe::PhysicalPumpReceiverActions(in,ms,true));
        CHECK(in.hands[1].trigger==0&&in.hands[0].squeeze==0);in.hands[1].trigger=1;CHECK(!probe::PhysicalPumpReceiverActions(in,ms,true));}
    return 0;}
int InvalidRequestedCountCannotFire(){for(unsigned n:{0u,3u,7u,9u,UINT32_MAX}){Fixture f(n);for(unsigned i=0;i<10;++i)f.Tick();
    CHECK(f.driver.Failed()&&f.native.fired==0&&f.native.released==0);}return 0;}
int MappingBeforePreparationCannotBeRepairedByDuplicateUpdate(){Fixture f;f.mapBeforePrepare=true;
    for(unsigned i=0;i<1600&&!f.driver.Failed();++i)f.Tick();
    CHECK(f.driver.Failed()&&!f.driver.Completed()&&f.native.fired==0&&f.native.released==0);return 0;}
int HeldSupportProofIsRequired(){
    for(unsigned mode=0;mode<2;++mode){Fixture f(1);f.publishSupport=mode!=0;f.forgeSupport=mode==1;
        for(unsigned n=0;n<1600&&!f.driver.Failed();++n)f.Tick();
        CHECK(f.driver.Failed()&&!f.driver.Completed()&&f.native.acked==1&&f.supportReturns==1);}
    return 0;
}
}
int main(){if(FiniteClosedLoopUsesConsumer()||RendererSourceCannotBeRestamped()||PairedPresentationRequired()||
    ControllerInverseRejectsWrongOriginal()||CapabilityAndReceiverAreExplicit()||InvalidRequestedCountCannotFire()||
    MappingBeforePreparationCannotBeRepairedByDuplicateUpdate()||HeldSupportProofIsRequired())return 1;
    std::puts("8 pump driver groups passed; real action mapping, controller conversion, renderer contact builder and physical consumer; synthetic native receipts and pack counters.");}
