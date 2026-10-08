#include "SceneRecoveryProbe.h"
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
    std::cout<<"6 scene neutral receiver groups PASS\n";
}
