#include "SceneRecoveryProbe.h"
#include "TriggerInputSmoke.h"
#include "BodyInspectionProbe.h"
#include <cassert>
#include <iostream>
#include <limits>
#include <sstream>

int main(){
    using namespace fvr;using namespace fvr::probe;
    for(const auto* option:{L"--pairs",L"--request-lifetime",L"--static-pose",L"--async",L"--scene-recovery-probe",
        L"--scene-controls-neutral",L"--controls-observe",L"--capture-poses"})assert(NeutralSceneOption(option));
    for(const auto* option:{L"--controls-fire",L"--controls-pulse",L"--controls-turn",L"--controls-buttons",L"--controls-aim",
        L"--controls-roomscale",L"--controls-hands",L"--controls-hand-independence",L"--controls-tracking-recovery",
        L"--controls-equip",L"--controls-weapon-cycle",L"--controls-weapon-mode",L"--controls-sight-flip",L"--controls-use",
        L"--controls-vehicle",L"--boat-head-aim-probe",L"--boat-fire-probe",L"--controls-shot-probe",L"--controls-death-probe",
        L"--controls-support-grip",L"--controls-support-reload",L"--physical-reload-probe",L"--reload-request-probe",
        L"--weapon-visibility-probe",L"--body-holster-probe",L"--xm8-magazine-probe",L"--magazine-physical-probe",
        L"--asymmetric",L"--fov-sweep",L"--future-action-script"})assert(!NeutralSceneOption(option));
    assert(NeutralSceneArguments(true,true,true,240,true));
    assert(!NeutralSceneArguments(false,true,true,240,true));
    assert(!NeutralSceneArguments(true,false,true,240,true));
    assert(!NeutralSceneArguments(true,true,false,240,true));
    assert(!NeutralSceneArguments(true,true,true,239,true));
    assert(!NeutralSceneArguments(true,true,true,240,false));
    for(const auto* option:{L"--selected-knife-neutral",L"--pairs",L"--request-lifetime",L"--static-pose",L"--async",L"--capture-poses"})
        assert(SelectedKnifeNeutralOption(option));
    for(const auto* option:{L"--scene-controls-neutral",L"--scene-recovery-probe",L"--controls-observe",L"--trigger-input-smoke",
        L"--selected-knife-trigger-smoke",L"--selected-knife-motion-smoke",L"--controls-fire",L"--no-image-files",L"--future-action-script"})
        assert(!SelectedKnifeNeutralOption(option));
    assert(!NeutralSceneOption(L"--selected-knife-neutral"));
    assert(SelectedKnifeNeutralArguments(true,true,true,240,true));
    assert(!SelectedKnifeNeutralArguments(false,true,true,240,true)&&!SelectedKnifeNeutralArguments(true,false,true,240,true)&&
        !SelectedKnifeNeutralArguments(true,true,false,240,true)&&!SelectedKnifeNeutralArguments(true,true,true,239,true)&&
        !SelectedKnifeNeutralArguments(true,true,true,240,false));
    runtime::TrackingFrame tracking{};tracking.spaceGeneration=987;tracking.predictedNs=1234;
    tracking.focused=tracking.headValid=true;tracking.worldUnitsPerMeter=2;
    tracking.referenceHead.position={1,2,3};tracking.head.position={4,5,6};
    for(std::uint64_t sequence:{1ull,55ull,999999ull}){
        const auto input=NeutralSceneInput(tracking,sequence);
        assert(input.generation==sequence&&input.spaceGeneration==987&&input.predictedNs==1234);
        assert(input.focused&&input.headValid&&!input.floorRelative&&input.worldUnitsPerMeter==2);
        assert(input.referenceHead.position.y==2&&input.head.position.z==6&&NeutralSceneActions(input));
        for(const auto& hand:input.hands){assert(hand.gripTracked&&hand.aimTracked&&hand.active==interaction::Components);
            assert(hand.grip.position.x==hand.aim.position.x&&hand.grip.position.y==hand.aim.position.y&&
                   hand.grip.position.z==hand.aim.position.z&&hand.grip.orientation.w==1&&hand.aim.orientation.w==1);}
        assert(input.hands[0].grip.position.x==-.2f&&input.hands[1].grip.position.z==-.20f);
    }
    for(unsigned hand=0;hand<2;++hand)for(unsigned mutation=0;mutation<8;++mutation){
        auto input=NeutralSceneInput(tracking,1);auto& h=input.hands[hand];
        switch(mutation){case 0:h.held=interaction::Primary;break;case 1:h.touched=1;break;
        case 2:h.touchActive=1;break;case 3:h.stickX=.1f;break;case 4:h.stickY=-.1f;break;
        case 5:h.trigger=.1f;break;case 6:h.squeeze=.1f;break;case 7:h.trigger=std::numeric_limits<float>::quiet_NaN();break;}
        assert(!NeutralSceneActions(input));
    }
    tracking.focused=tracking.headValid=false;const auto absent=NeutralSceneInput(tracking,3);
    assert(!absent.focused&&!absent.headValid&&NeutralSceneActions(absent));
    std::ostringstream out;WriteNeutralSceneInput(out,NeutralSceneInput(tracking,3),4000);
    assert(out.str().find("\"tick_ms\":4000")!=std::string::npos&&out.str().find("\"generation\":3")!=std::string::npos);
    assert(out.str().find("\"axes\":[0,0,0,0]")!=std::string::npos&&out.str().back()=='\n');
    auto owned=NeutralSceneInput(tracking,4);owned.trackingBasisGeneration=7;
    owned.menu={interaction::InputMenuOwner::Gameplay,12,3,456789};
    std::ostringstream ownedOut;WriteNeutralSceneInput(ownedOut,owned,4001);
    assert(ownedOut.str().find("\"tracking_basis\":7")!=std::string::npos&&
        ownedOut.str().find("\"menu\":{\"owner\":1,\"sequence\":12,\"epoch\":3,\"deadline_ns\":456789}")!=std::string::npos);
    for(const math::Vec3 contact: {math::Vec3{-.2f,-.12f,.18f},math::Vec3{.2f,-.12f,.18f},math::Vec3{.22f,-.65f,0.f}}){
        unsigned edges=0;float previous=0;math::Vec3 last{.2f,-.25f,-.45f};
        for(unsigned ms=0;ms<14000;++ms){
            auto input=NeutralSceneInput(tracking,100+ms);
            assert(BodyInspectionInput(input,contact,ms)&&BodyInspectionActions(input));
            assert(input.generation==100+ms&&input.spaceGeneration==987&&input.worldUnitsPerMeter==2);
            const auto& hand=input.hands[1];const auto p=hand.grip.position;
            assert(std::abs(p.x-last.x)<.003f&&std::abs(p.y-last.y)<.003f&&std::abs(p.z-last.z)<.003f);
            assert(p.x==hand.aim.position.x&&p.y==hand.aim.position.y&&p.z==hand.aim.position.z);
            if(hand.squeeze>previous){++edges;assert(p.x==contact.x&&p.y==contact.y&&p.z==contact.z);}
            if(ms>=4000)assert(p.x==.2f&&p.y==-.25f&&p.z==-.45f&&hand.squeeze==1);
            previous=hand.squeeze;last=p;
            for(unsigned h=0;h<2;++h){auto bad=input;bad.hands[h].trigger=.1f;assert(!BodyInspectionActions(bad));}
        }
        assert(edges==1);
    }
    for(float bad:{1.01f,-1.01f,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()}){
        for(unsigned axis=0;axis<3;++axis){math::Vec3 p{};if(axis==0)p.x=bad;else if(axis==1)p.y=bad;else p.z=bad;
            auto input=NeutralSceneInput(tracking,5);input.hands[1].trigger=.4f;
            assert(!ValidBodyInspectionContact(p)&&!BodyInspectionInput(input,p,2000)&&input.hands[1].trigger==.4f);}
    }
    // Independent of pair target: five seconds, one80ms pulse, and a real
    // neutral tail. Successful IPC publication is not native consumption.
    for(const auto* option:{L"--trigger-input-smoke",L"--selected-knife-trigger-smoke",L"--selected-knife-motion-smoke",L"--no-image-files",L"--pairs",L"--static-pose",L"--async",L"--request-lifetime",L"--capture-poses"})
        assert(TriggerInputSmokeOption(option));
    for(const auto* option:{L"--controls-fire",L"--controls-hands",L"--controls-observe",L"--normal-knife-held-close",
        L"--scene-controls-neutral",L"--selected-knife-neutral",L"--body-props-cache",L"--future-action-script"})assert(!TriggerInputSmokeOption(option));
    assert(TriggerInputSmokeArguments(true,true,true,8,true)&&TriggerInputSmokeArguments(true,true,true,240,true));
    assert(!TriggerInputSmokeArguments(false,true,true,240,true)&&!TriggerInputSmokeArguments(true,false,true,240,true)&&
        !TriggerInputSmokeArguments(true,true,false,240,true)&&!TriggerInputSmokeArguments(true,true,true,240,false)&&
        !TriggerInputSmokeArguments(true,true,true,7,true)&&!TriggerInputSmokeArguments(true,true,true,241,true));
    tracking.focused=tracking.headValid=true;
    const auto sample=[&](TriggerInputSmoke& smoke,std::uint64_t ms,std::uint64_t seq,bool delivered=true,bool busy=false){
        const auto input=smoke.Input(tracking,seq,ms);
        assert(input.hands[0].grip.position.x==-.2f&&input.hands[0].grip.position.y==-.25f&&input.hands[0].grip.position.z==-.45f);
        assert(input.hands[1].grip.position.x==.15f&&input.hands[1].grip.position.y==-.10f&&input.hands[1].grip.position.z==-.20f);
        for(const auto& hand:input.hands)assert(hand.aim.position.x==hand.grip.position.x&&hand.aim.position.z==hand.grip.position.z&&
            hand.squeeze==0&&hand.stickX==0&&hand.stickY==0&&hand.held==0&&hand.touched==0);
        const auto before=1000000000+std::int64_t(ms)*1000000;
        return smoke.Published(input,ms,before,before+1000,delivered,busy);
    };
    using Receipt=TriggerInputSmoke::Receipt;
    TriggerInputSmoke smoke;
    assert(sample(smoke,0,1)==Receipt::Neutral);
    assert(smoke.Input(tracking,2,399).hands[1].trigger==0&&smoke.Input(tracking,2,400).hands[1].trigger==1&&
        smoke.Input(tracking,2,479).hands[1].trigger==1&&smoke.Input(tracking,2,480).hands[1].trigger==0);
    assert(sample(smoke,400,2)==Receipt::Press&&sample(smoke,450,3)==Receipt::None);
    assert(sample(smoke,480,4)==Receipt::Release&&sample(smoke,979,5)==Receipt::None);
    assert(sample(smoke,980,6)==Receipt::NeutralTail&&sample(smoke,4999,7)==Receipt::None);
    assert(TriggerInputSmoke::Continue(4999)&&!TriggerInputSmoke::Continue(5000));
    assert(!smoke.Complete(4999,true)&&!smoke.Complete(5000,false)&&smoke.Complete(5000,true));
    std::ostringstream smokeReport;smoke.Report(smokeReport,5000,true);
    assert(smokeReport.str().find("\"publication_complete\":true")!=std::string::npos&&
        smokeReport.str().find("\"native_input_observed\":false")!=std::string::npos);
    // Busy during the complete pulse cannot become a fabricated press; the
    // schedule never retries the attack after480ms to make the test pass.
    TriggerInputSmoke missed;
    sample(missed,0,1);assert(sample(missed,400,2,false,true)==Receipt::None);
    assert(sample(missed,479,3,false,true)==Receipt::None&&sample(missed,480,4)==Receipt::None);
    sample(missed,1000,5);assert(!missed.Complete(5000,true)&&!missed.Failed());
    TriggerInputSmoke late;assert(late.Input(tracking,1,420).hands[1].trigger==0);
    sample(late,490,1);sample(late,1000,2);assert(!late.Complete(5000,true));
    TriggerInputSmoke retried;sample(retried,0,1);sample(retried,400,2,false,true);
    assert(sample(retried,410,3)==Receipt::Press);sample(retried,480,4,false,true);
    assert(sample(retried,490,5)==Receipt::Release);sample(retried,990,6);assert(retried.Complete(5000,true));
    TriggerInputSmoke noTail;sample(noTail,0,1);sample(noTail,400,2);sample(noTail,480,3);sample(noTail,979,4);
    assert(!noTail.Complete(5000,true));
    TriggerInputSmoke unknown;sample(unknown,0,1,false,false);
    assert(unknown.Failed()&&unknown.Input(tracking,2,400).hands[1].trigger==0);
    TriggerInputSmoke duplicate;sample(duplicate,0,1);sample(duplicate,400,1);
    assert(duplicate.Failed()&&!duplicate.Complete(5000,true));
    TriggerInputSmoke changedSpace;sample(changedSpace,0,1);++tracking.spaceGeneration;sample(changedSpace,400,2);--tracking.spaceGeneration;
    assert(changedSpace.Failed());
    TriggerInputSmoke clock;sample(clock,0,1);sample(clock,400,2);sample(clock,399,3);assert(clock.Failed());
    TriggerInputSmoke lostTracking;sample(lostTracking,0,1);tracking.headValid=false;sample(lostTracking,400,2);tracking.headValid=true;
    assert(lostTracking.Failed());
    TriggerInputSmoke stalePulse;sample(stalePulse,0,1);auto invalid=stalePulse.Input(tracking,2,400);
    assert(stalePulse.Published(invalid,480,1480000000,1480001000,true,false)==Receipt::None&&stalePulse.Failed());
    for(unsigned hand=0;hand<2;++hand){TriggerInputSmoke extra;sample(extra,0,1);auto bad=extra.Input(tracking,2,400);
        bad.hands[hand].squeeze=1;extra.Published(bad,400,1400000000,1400001000,true,false);assert(extra.Failed());}
    assert(TriggerInputSmokeArguments(true,true,true,240,true,true,true));
    assert(!TriggerInputSmokeArguments(true,true,true,8,true,true,true));
    assert(!TriggerInputSmokeArguments(true,true,true,240,true,false,true));
    TriggerInputSmoke motion;assert(!motion.Motion({0,0,0})&&!motion.Motion({0,0,2}));
    assert(motion.Motion({0,0,-1})&&!motion.Motion({0,0,-1}));
    assert(motion.Duration()==8000&&motion.ContinueInput(7999)&&!motion.ContinueInput(8000));
    assert(motion.PairRequestDue(0,0,240)&&!motion.PairRequestDue(1,1,240)&&motion.PairRequestDue(32,1,240));
    assert(motion.PairRequestDue(7469,239,240)&&!motion.PairRequestDue(8000,240,240));
    const auto motionSample=[&](TriggerInputSmoke& fixture,unsigned ms,bool delivered=true,bool busy=false){
        const auto input=fixture.Input(tracking,ms+1,ms);
        assert(NeutralSceneActions(input)&&input.hands[0].grip.position.x==-.2f);
        assert(input.hands[1].aim.position.x==input.hands[1].grip.position.x&&
            input.hands[1].aim.position.y==input.hands[1].grip.position.y&&input.hands[1].aim.position.z==input.hands[1].grip.position.z);
        const auto before=1000000000+std::int64_t(ms)*1000000;
        return fixture.Published(input,ms,before,before+1000,delivered,busy);
    };
    unsigned motionRows=0;
    for(unsigned ms=0;ms<8000;ms+=10){
        const auto input=motion.Input(tracking,ms+1,ms);const auto z=input.hands[1].grip.position.z;
        if(ms==2000||ms==5200||ms==7500)assert(std::abs(z+.20f)<.00001f);
        if(ms==2120||ms==5320)assert(std::abs(z+.32f)<.00001f);
        if(motionSample(motion,ms)==Receipt::Motion)++motionRows;
    }
    assert(motionRows>=24&&motion.Complete(8000,true)&&!motion.Complete(7999,true));
    std::ostringstream motionReport;motion.Report(motionReport,8000,true);
    assert(motionReport.str().find("\"stroke_samples\":[12,12]")!=std::string::npos&&
        motionReport.str().find("\"miss_verified\":false")!=std::string::npos);
    TriggerInputSmoke lostJab;assert(lostJab.Motion({0,0,-1}));
    for(unsigned ms=0;ms<8000;ms+=10)motionSample(lostJab,ms,!(ms>=2000&&ms<2120),ms>=2000&&ms<2120);
    assert(!lostJab.Complete(8000,true)&&!lostJab.Failed());
    TriggerInputSmoke lateMotion;assert(lateMotion.Motion({0,0,-1}));
    motionSample(lateMotion,2050);assert(lateMotion.Failed());
    TriggerInputSmoke changedMotion;assert(changedMotion.Motion({0,0,-1}));motionSample(changedMotion,0);
    auto wrong=changedMotion.Input(tracking,2001,2000);wrong.hands[1].grip.position.x+=.01f;
    changedMotion.Published(wrong,2000,3000000000,3000001000,true,false);assert(changedMotion.Failed());
    std::cout<<"8 scene/body groups, isolated trigger and two-jab publication regressions PASS\n";
}
