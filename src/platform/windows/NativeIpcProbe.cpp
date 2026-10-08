#include "fvr/ipc/RemoteFrameProvider.h"
#include "fvr/graphics/D3D11SharedPair.h"
#include "Bc2ReloadRequestProbe.h"
#include "Bc2PumpDiagnostic.h"
#include "Bc2MagazineReloadProbe.h"
#include "Bc2PhysicalReloadPreparation.h"
#include "BoatFireProbe.h"
#include "SceneRecoveryProbe.h"
#include "BodyCrossDrawProbe.h"
#include "SightGestureFixtureGeometry.h"
#include <Windows.h>
#include <wrl/client.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
#include <cstring>
#include <cmath>
#include <stdexcept>
#include <algorithm>
namespace fs=std::filesystem;using namespace fvr;using Microsoft::WRL::ComPtr;
void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void Hr(HRESULT value,const char* message){Require(SUCCEEDED(value),message);}
std::vector<unsigned char> Pixels(ID3D11DeviceContext* context,ID3D11Texture2D* output,ID3D11Texture2D* staging,unsigned eye){
    context->CopySubresourceRegion(staging,0,0,0,0,output,eye,nullptr);D3D11_MAPPED_SUBRESOURCE map{};Hr(context->Map(staging,0,D3D11_MAP_READ,0,&map),"Read native image");
    std::vector<unsigned char> pixels(1920u*1080u*4u);for(unsigned row=0;row<1080;++row)std::memcpy(pixels.data()+std::size_t(row)*1920*4,static_cast<const char*>(map.pData)+std::size_t(row)*map.RowPitch,1920*4);context->Unmap(staging,0);return pixels;
}
int wmain(int argc,wchar_t** argv){try{
    Require(argc>=3&&std::wstring(argv[1])==L"--output","Usage: BC2NativeIpcProbe --output new-folder [--pairs N] [--asymmetric] [--static-pose] [--async]");
    unsigned targetPairs=8,requestLifetimeMs=150,shotPrimaryPulses=2;bool asymmetric=false,staticPose=false,async=false,fovSweep=false,controlsObserve=false,controlsPulse=false,controlsTurn=false,controlsFire=false,controlsButtons=false,controlsAim=false,controlsRoomscale=false,capturePoses=false,controlsHands=false,controlsIndependence=false,trackingRecovery=false,controlsEquip=false,controlsWeaponCycle=false,weaponModeCapture=false,sightGesture=false,sightStartSecondary=false,controlsUse=false,controlsVehicle=false,shotProbe=false,shotReload=false,deathProbe=false,supportProbe=false,supportReload=false,supportPrepareShots=false,reloadRequestProbe=false,physicalReloadProbe=false,weaponVisibilityProbe=false,bodyHolsterProbe=false,sceneRecoveryProbe=false,magazineReloadProbe=false,magazinePhysicalProbe=false;
    bool emptyFireNeutral=false,emptyFireOptionsOnly=true,pumpHoldProbe=false,pumpOptionsOnly=true;unsigned pumpFireSamples=0;
    bool boatHeadAimProbe=false,boatFireProbe=false,sceneControlsNeutral=false,neutralSceneOptionsOnly=true,bodyCrossDrawProbe=false,magazineOriginalReturnProbe=false,magazineFullReturnProbe=false,originalReturnOptionsOnly=true;
    bool inventoryReloadProbe=false,inventoryOptionsOnly=true;
    float roomscaleStepX=.4f,shotCycleDirection=-1,supportPrimaryDirection=0;
    for(int i=3;i<argc;++i){const std::wstring option=argv[i];
        inventoryOptionsOnly=inventoryOptionsOnly&&(option==L"--inventory-reload-probe"||option==L"--inventory-recovery-probe"||option==L"--pairs"||option==L"--static-pose"||option==L"--async"||option==L"--capture-poses");
        emptyFireOptionsOnly=emptyFireOptionsOnly&&bc2::EmptyFireReceiverOption(option);
        pumpOptionsOnly=pumpOptionsOnly&&(option==L"--pump-hold-probe"||option==L"--pairs"||option==L"--static-pose"||option==L"--async"||option==L"--capture-poses"||option==L"--request-lifetime");
        neutralSceneOptionsOnly=neutralSceneOptionsOnly&&probe::NeutralSceneOption(option);
        originalReturnOptionsOnly=originalReturnOptionsOnly&&(option==L"--magazine-original-return-probe"||option==L"--magazine-full-return-probe"||option==L"--pairs"||option==L"--static-pose"||option==L"--async"||option==L"--capture-poses"||option==L"--request-lifetime");
        if(option==L"--pairs"&&i+1<argc){targetPairs=std::stoul(argv[++i]);Require(targetPairs>=8&&targetPairs<=240,"Pair count must be 8..240");}
        else if(option==L"--request-lifetime"&&i+1<argc){requestLifetimeMs=std::stoul(argv[++i]);Require(requestLifetimeMs>=1&&requestLifetimeMs<=200,"Request lifetime must be 1..200 ms");}
        else if(option==L"--roomscale-step-x"&&i+1<argc){roomscaleStepX=std::stof(argv[++i]);Require(std::isfinite(roomscaleStepX)&&std::abs(roomscaleStepX)<=.5f,"Roomscale step must be at most half a metre");}
        else if(option==L"--shot-primary-pulses"&&i+1<argc){shotPrimaryPulses=std::stoul(argv[++i]);Require(shotPrimaryPulses<=2,"Shot primary pulses must be 0, 1 or 2");}
        else if(option==L"--shot-cycle-direction"&&i+1<argc){shotCycleDirection=std::stof(argv[++i]);Require(shotCycleDirection==1||shotCycleDirection==-1,"Shot cycle direction must be -1 or 1");}
        else if(option==L"--support-primary-direction"&&i+1<argc){supportPrimaryDirection=std::stof(argv[++i]);Require(supportPrimaryDirection==0||supportPrimaryDirection==1||supportPrimaryDirection==-1,"Invalid support selection");}
        else if(option==L"--xm8-magazine-probe"){magazineReloadProbe=controlsObserve=true;}
        else if(option==L"--inventory-reload-probe"||option==L"--inventory-recovery-probe"){Require(!inventoryReloadProbe,"Select one inventory diagnostic");inventoryReloadProbe=magazinePhysicalProbe=controlsObserve=true;}
        else if(option==L"--magazine-physical-probe"){magazinePhysicalProbe=controlsObserve=true;}
        else if(option==L"--magazine-original-return-probe"){magazineOriginalReturnProbe=magazinePhysicalProbe=controlsObserve=capturePoses=true;}
        else if(option==L"--magazine-full-return-probe"){magazineFullReturnProbe=magazinePhysicalProbe=controlsObserve=capturePoses=true;}
        else if(option==L"--scene-recovery-probe"){sceneRecoveryProbe=true;}
        else if(option==L"--empty-fire-neutral"){emptyFireNeutral=sceneControlsNeutral=controlsObserve=capturePoses=true;}
        else if(option==L"--scene-controls-neutral"){sceneControlsNeutral=sceneRecoveryProbe=controlsObserve=capturePoses=true;}
        else if(option==L"--body-cross-draw-probe"){bodyCrossDrawProbe=controlsObserve=true;}
        else if(option==L"--body-holster-probe"){bodyHolsterProbe=controlsObserve=true;}
        else if(option==L"--weapon-visibility-probe"){weaponVisibilityProbe=controlsObserve=true;}
        else if(option==L"--physical-reload-probe"){physicalReloadProbe=supportReload=supportProbe=controlsHands=controlsObserve=capturePoses=true;}
        else if(option==L"--reload-request-probe"){reloadRequestProbe=supportReload=supportProbe=controlsHands=controlsObserve=capturePoses=true;}
        else if(option==L"--pump-hold-probe"){pumpHoldProbe=controlsObserve=capturePoses=true;}
        else if(option==L"--support-prepare-shots")supportPrepareShots=true;
        else if(option==L"--controls-support-reload"){supportReload=supportProbe=controlsHands=controlsObserve=capturePoses=true;}
        else if(option==L"--controls-support-grip"){supportProbe=controlsHands=controlsObserve=capturePoses=true;}
        else if(option==L"--shot-reload")shotReload=true;
        else if(option==L"--controls-shot-probe"){shotProbe=controlsHands=controlsObserve=capturePoses=true;}
        else if(option==L"--controls-sight-flip"){sightGesture=weaponModeCapture=controlsHands=controlsObserve=capturePoses=true;}
        else if(option==L"--sight-start-secondary")sightStartSecondary=true;
        else if(option==L"--controls-weapon-mode"){weaponModeCapture=controlsHands=controlsObserve=capturePoses=true;}
        else if(option==L"--controls-weapon-cycle"){controlsWeaponCycle=controlsHands=controlsObserve=capturePoses=true;}
        else if(option==L"--controls-use"){controlsUse=controlsObserve=true;}
        else if(option==L"--controls-vehicle"){controlsVehicle=controlsObserve=true;}
        else if(option==L"--boat-head-aim-probe"){boatHeadAimProbe=controlsVehicle=controlsObserve=true;}
        else if(option==L"--boat-fire-probe"){boatFireProbe=boatHeadAimProbe=controlsVehicle=controlsObserve=true;}
        else if(option==L"--controls-equip"){controlsEquip=controlsHands=controlsObserve=capturePoses=true;}
        else if(option==L"--controls-tracking-recovery"){trackingRecovery=controlsHands=controlsObserve=capturePoses=true;}
        else if(option==L"--controls-death-probe"){deathProbe=controlsObserve=capturePoses=true;}
        else if(option==L"--controls-hand-independence"){controlsIndependence=controlsHands=controlsObserve=capturePoses=true;}
        else if(option==L"--controls-hands"){controlsHands=controlsObserve=capturePoses=true;}
        else if(option==L"--capture-poses"){capturePoses=true;}
        else if(option==L"--controls-roomscale"){controlsObserve=controlsRoomscale=true;}
        else if(option==L"--controls-aim"){controlsObserve=true;controlsAim=true;}else if(option==L"--controls-buttons"){controlsObserve=true;controlsButtons=true;}else if(option==L"--controls-turn"){controlsObserve=true;controlsTurn=true;}else if(option==L"--controls-fire"){controlsObserve=true;controlsFire=true;}else if(option==L"--controls-pulse"){controlsObserve=true;controlsPulse=true;}else if(option==L"--controls-observe")controlsObserve=true;else if(option==L"--asymmetric")asymmetric=true;else if(option==L"--static-pose")staticPose=true;else if(option==L"--async")async=true;else if(option==L"--fov-sweep")fovSweep=true;
        else throw std::runtime_error("Unknown native probe option");}
    Require(!emptyFireNeutral||(emptyFireOptionsOnly&&staticPose&&async&&targetPairs==240),"Empty-fire receiver requires isolated neutral static async240-pair stream");
    Require(!inventoryReloadProbe||inventoryOptionsOnly,"Combined inventory/reload receiver permits only neutral controller input");
    Require(!(magazineOriginalReturnProbe&&magazineFullReturnProbe)&&(!(magazineOriginalReturnProbe||magazineFullReturnProbe)||originalReturnOptionsOnly),"Select one magazine return fixture without other scripted actions");
    Require(!pumpHoldProbe||(pumpOptionsOnly&&staticPose&&async&&targetPairs==240&&requestLifetimeMs==150),"Pump hold receiver requires isolated15s static async240-pair stream");
    Require(!bodyCrossDrawProbe||(staticPose&&async&&targetPairs==240&&!magazinePhysicalProbe&&!magazineReloadProbe&&!sceneRecoveryProbe&&!weaponVisibilityProbe&&!bodyHolsterProbe&&!reloadRequestProbe&&!physicalReloadProbe&&!supportPrepareShots&&!supportProbe&&!shotProbe&&!shotReload&&!supportReload&&!controlsHands&&!controlsFire&&!deathProbe&&!controlsEquip&&!controlsWeaponCycle&&!controlsPulse&&!controlsUse&&!controlsButtons&&!controlsTurn&&!controlsRoomscale&&!trackingRecovery&&!controlsIndependence&&!controlsAim&&!weaponModeCapture&&!controlsVehicle&&!asymmetric&&!fovSweep&&!sightStartSecondary),"Body cross-draw requires isolated14s static async240-pair receiver gestures");
    Require(!magazinePhysicalProbe||(staticPose&&async&&targetPairs==240&&!magazineReloadProbe&&!sceneRecoveryProbe&&!weaponVisibilityProbe&&!bodyHolsterProbe&&!reloadRequestProbe&&!physicalReloadProbe&&!supportPrepareShots&&!supportProbe&&!shotProbe&&!controlsHands&&!controlsFire&&!deathProbe&&!controlsEquip&&!controlsWeaponCycle&&!controlsPulse&&!controlsUse&&!controlsButtons&&!controlsTurn&&!controlsRoomscale&&!trackingRecovery&&!controlsIndependence&&!controlsAim&&!weaponModeCapture&&!controlsVehicle&&!asymmetric&&!fovSweep),"Physical magazine fixture requires isolated30s neutral static async240-pair stream");
    Require(!magazineReloadProbe||(staticPose&&async&&targetPairs==240&&!sceneRecoveryProbe&&!weaponVisibilityProbe&&!bodyHolsterProbe&&!reloadRequestProbe&&!physicalReloadProbe&&!supportPrepareShots&&!supportProbe&&!shotProbe&&!controlsHands&&!controlsFire&&!deathProbe&&!controlsEquip&&!controlsWeaponCycle&&!controlsPulse&&!controlsUse&&!controlsButtons&&!controlsTurn&&!controlsRoomscale&&!trackingRecovery&&!controlsIndependence&&!controlsAim&&!weaponModeCapture&&!controlsVehicle&&!asymmetric&&!fovSweep),"Magazine fixture requires isolated30s static async240-pair stream");
    Require(!sceneRecoveryProbe||(sceneControlsNeutral?probe::NeutralSceneArguments(sceneRecoveryProbe,staticPose,async,targetPairs,neutralSceneOptionsOnly):(staticPose&&async&&targetPairs==240&&!controlsObserve&&!capturePoses&&!weaponVisibilityProbe&&!asymmetric&&!fovSweep)),"Scene recovery probe requires static async240-pair stream; neutral controllers reject every action script");
    Require(!(weaponVisibilityProbe&&bodyHolsterProbe),"Select one visibility/body fixture");
    Require(!(weaponVisibilityProbe||bodyHolsterProbe)||(staticPose&&async&&targetPairs==240&&!reloadRequestProbe&&!physicalReloadProbe&&!supportPrepareShots&&!supportProbe&&!shotProbe&&!controlsHands&&!controlsFire&&!deathProbe&&!controlsEquip&&!controlsWeaponCycle&&!controlsPulse&&!controlsUse&&!controlsButtons&&!controlsTurn&&!controlsRoomscale&&!trackingRecovery&&!controlsIndependence&&!controlsAim&&!weaponModeCapture&&!controlsVehicle&&!asymmetric&&!fovSweep),"Visibility probe requires neutral static async240-pair stream");
    Require(!supportPrepareShots||(supportReload&&supportPrimaryDirection==0&&!shotProbe&&!controlsFire&&!controlsPulse&&!deathProbe&&!weaponModeCapture),"Support preparation requires an already selected reload fixture without other actions");
    Require(!(reloadRequestProbe&&physicalReloadProbe),"Select one reload fixture");
    Require(!(reloadRequestProbe||physicalReloadProbe)||(staticPose&&async&&targetPairs==240&&supportPrimaryDirection==0&&!controlsFire&&!shotProbe&&!deathProbe&&!controlsEquip&&!controlsWeaponCycle&&!controlsPulse&&!controlsUse&&!controlsButtons&&!controlsTurn&&!controlsRoomscale&&!trackingRecovery&&!controlsIndependence&&!controlsAim&&!weaponModeCapture&&!controlsVehicle&&!asymmetric&&!fovSweep),"Request fixture requires isolated30s static async240-pair observation");
    Require(!sightStartSecondary||sightGesture,"Sight start-secondary requires controls-sight-flip");
    Require(!sightGesture||staticPose,"Measured sight fixture requires static-pose");
    Require(!weaponModeCapture||(staticPose&&async&&targetPairs==240),"Weapon mode capture requires static async 240-pair acquisition");
    Require(!weaponModeCapture||!(controlsFire||shotProbe||deathProbe||controlsEquip||controlsWeaponCycle||controlsPulse||controlsUse||controlsButtons||controlsTurn||controlsRoomscale||supportProbe||trackingRecovery||controlsIndependence||controlsAim),"Weapon mode capture cannot combine with other action scripts");
    Require(!controlsVehicle||(staticPose&&async&&targetPairs==240&&!controlsFire&&!shotProbe&&!deathProbe&&!controlsEquip&&!controlsWeaponCycle&&!controlsPulse&&!controlsUse&&!controlsButtons&&!controlsTurn&&!controlsRoomscale&&!supportProbe&&!trackingRecovery&&!controlsIndependence&&!controlsAim&&!controlsHands&&!weaponModeCapture&&!asymmetric&&!fovSweep),"Vehicle fixture requires isolated static async240-pair observation");
    const auto folder=fs::absolute(argv[2]);Require(!fs::exists(folder),"New output directory required");fs::create_directories(folder);
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;D3D_FEATURE_LEVEL level{};const D3D_FEATURE_LEVEL levels[]={D3D_FEATURE_LEVEL_11_0};
    Hr(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,levels,1,D3D11_SDK_VERSION,&device,&level,&context),"Create receiver device");
    ComPtr<IDXGIDevice> dxgi;Hr(device.As(&dxgi),"Receiver DXGI");ComPtr<IDXGIAdapter> adapter;Hr(dxgi->GetAdapter(&adapter),"Receiver adapter");DXGI_ADAPTER_DESC gpu{};Hr(adapter->GetDesc(&gpu),"GPU identity");
    runtime::PresentationRequirements requirements{1920,1080,29,gpu.AdapterLuid.LowPart,gpu.AdapterLuid.HighPart};
    D3D11_TEXTURE2D_DESC description{};description.Width=1920;description.Height=1080;description.MipLevels=1;description.ArraySize=2;description.Format=DXGI_FORMAT_R8G8B8A8_TYPELESS;description.SampleDesc.Count=1;description.Usage=D3D11_USAGE_DEFAULT;
    ComPtr<ID3D11Texture2D> output,staging;Hr(device->CreateTexture2D(&description,nullptr,&output),"Receiver eyes");description.ArraySize=1;description.Format=DXGI_FORMAT_R8G8B8A8_UNORM;description.Usage=D3D11_USAGE_STAGING;description.CPUAccessFlags=D3D11_CPU_ACCESS_READ;Hr(device->CreateTexture2D(&description,nullptr,&staging),"Receiver staging");
    ipc::RemoteFrameProvider host;Require(host.Create()&&host.SetBudget(50)&&host.SetRequestLifetime(requestLifetimeMs),"Create native receiver channel");{std::wofstream token(folder/L"channel.txt");token<<host.Token();Require(bool(token),"Write channel token");}
    const auto start=GetTickCount64();while(!host.Connected()&&GetTickCount64()-start<15000)Sleep(2);Require(host.Connected(),"Native producer connection timed out");
    const auto controlsStarted=GetTickCount64();
    probe::BoatFireProbe boatFireSchedule;unsigned boatFireInputSamples=0;std::uint64_t boatFireFirstInputMs=0,boatFireLastInputMs=0;
    unsigned consumed=0,timeouts=0,gpuBusy=0;std::uint64_t lastFrame=0;graphics::D3D11PairConsumer consumer;graphics::TextureDescriptor opened{};std::array<std::vector<unsigned char>,2> prior;
    std::ofstream records(folder/L"pairs.jsonl");
    std::ofstream neutralInputRecords;unsigned neutralInputSamples=0;
    if(sceneControlsNeutral||magazineOriginalReturnProbe||magazineFullReturnProbe){neutralInputRecords.open(folder/(magazineFullReturnProbe?L"full-return-input.jsonl":magazineOriginalReturnProbe?L"original-return-input.jsonl":L"scene-input.jsonl"));Require(bool(neutralInputRecords),"Open neutral scene input log");}
    for(std::uint64_t attempt=1;(consumed<targetPairs||(inventoryReloadProbe&&GetTickCount64()-controlsStarted<60000ull)||(emptyFireNeutral&&GetTickCount64()-controlsStarted<30000ull)||((magazineReloadProbe||magazinePhysicalProbe)&&GetTickCount64()-controlsStarted<30000ull)||((shotProbe||supportProbe)&&GetTickCount64()-controlsStarted<((reloadRequestProbe||physicalReloadProbe)?30000ull:11000ull))||(weaponModeCapture&&GetTickCount64()-controlsStarted<13000)||(controlsVehicle&&GetTickCount64()-controlsStarted<6500))&&GetTickCount64()-start<(inventoryReloadProbe?75000ull:emptyFireNeutral?45000ull:sceneRecoveryProbe?70000ull:(reloadRequestProbe||physicalReloadProbe||magazineReloadProbe||magazinePhysicalProbe)?45000ull:25000ull);++attempt){
        runtime::TrackingFrame tracking{};tracking.generation=0x100000000ULL+attempt;tracking.spaceGeneration=0x200000001ULL;tracking.predictedNs=10000000000LL+std::int64_t(attempt)*11000000;
        tracking.focused=tracking.headValid=true;const float angle=staticPose?0:float(consumed)*.025f;tracking.head.orientation={0,std::sin(angle*.5f),0,std::cos(angle*.5f)};tracking.head.position.x=staticPose?0:float(consumed)*.035f;
        if(controlsRoomscale){const auto ms=GetTickCount64()-controlsStarted;tracking.head={};tracking.head.position.x=(ms>=500&&ms<4000)?roomscaleStepX:0;}
        if(boatHeadAimProbe){const auto ms=GetTickCount64()-controlsStarted;
            // Aim-only sweeps; the separate fire fixture remains completely stationary.
            const float yaw=boatFireProbe?0:(ms>=1000&&ms<2000)?.1745329252f:(ms>=3000&&ms<4000)?-.1745329252f:0;
            const float pitch=boatFireProbe?0:(ms>=4000&&ms<5000)?.0872664626f:0;
            tracking.head={};tracking.head.orientation={std::cos(yaw*.5f)*std::sin(pitch*.5f),
                std::sin(yaw*.5f)*std::cos(pitch*.5f),-std::sin(yaw*.5f)*std::sin(pitch*.5f),std::cos(yaw*.5f)*std::cos(pitch*.5f)};
        }
        else if(controlsVehicle){const auto ms=GetTickCount64()-controlsStarted;
            // Enter with accumulated on-foot displacement/yaw, then lean10cm.
            tracking.head.position.x=(ms>=3500&&ms<5000)?.7f:.6f;
            tracking.head.orientation={0,std::sin(.1745329252f),0,std::cos(.1745329252f)};
        }
        tracking.eyes={tracking.head,tracking.head};tracking.eyes[0].position.x-=.032f;tracking.eyes[1].position.x+=.032f;tracking.fov={math::FovTangents{-1,1,.8f,-.8f},math::FovTangents{-1,1,.8f,-.8f}};
        if(asymmetric)tracking.fov={math::FovTangents{-1.2f,.8f,.9f,-.9f},math::FovTangents{-.8f,1.2f,.9f,-.9f}};
        if(fovSweep){
            const math::FovTangents symmetric{-1,1,.9f,-.9f},left{-1.2f,.8f,.9f,-.9f},right{-.8f,1.2f,.9f,-.9f};
            switch(consumed%4){case 0:tracking.fov={symmetric,symmetric};break;case 1:tracking.fov={left,right};break;
                case 2:tracking.fov={left,left};break;case 3:tracking.fov={right,right};break;}
        }
        if(controlsObserve){interaction::InputFrame controls{};controls.generation=attempt;controls.spaceGeneration=tracking.spaceGeneration;
            controls.predictedNs=tracking.predictedNs;controls.focused=controls.headValid=true;controls.referenceHead=tracking.referenceHead;controls.head=tracking.head;
            for(auto& hand:controls.hands){hand.gripTracked=hand.aimTracked=true;hand.active=interaction::Components;}
            const auto elapsed=GetTickCount64()-controlsStarted;
            if(sceneControlsNeutral)controls=probe::NeutralSceneInput(tracking,attempt);
            if(bodyCrossDrawProbe)probe::BodyCrossDrawInput(controls,elapsed);
            if(controlsHands&&!supportProbe&&!shotProbe&&!controlsIndependence&&!trackingRecovery&&!controlsEquip&&!controlsWeaponCycle&&!weaponModeCapture&&!deathProbe){
                controls.hands[0].grip.position={-.2f,-.25f,-.45f};controls.hands[1].grip.position={.2f,-.25f,-.45f};
                if(elapsed>=600&&elapsed<1400){controls.hands[0].grip.position.x-=.08f;controls.hands[1].grip.position.x+=.12f;}
                if(elapsed>=1400&&elapsed<2200){controls.hands[1].grip.orientation={0,0,.258819045f,.965925826f};}
                if(elapsed>=3000&&elapsed<3400)controls.hands[1].gripTracked=false;
                if(controlsRoomscale)for(auto& hand:controls.hands){hand.grip.position.x+=tracking.head.position.x;hand.grip.position.z+=tracking.head.position.z;}
            }

            if(pumpHoldProbe){
                controls.hands[0].grip.position={-.2f,-.25f,-.45f};controls.hands[1].grip.position={0,-.4f,0};
                for(auto& hand:controls.hands)hand.aim=hand.grip;
                controls.hands[1].trigger=bc2::PumpDiagnosticTrigger(elapsed);
                if(controls.hands[1].trigger>0)++pumpFireSamples;
            }
            if(magazinePhysicalProbe){
                // Actual Gameplay fixture owns every left-hand motion/grab.
                // No fire, selection, native Reload, or private gate markers.
                controls.hands[0].grip.position={-.2f,-.25f,-.45f};controls.hands[1].grip.position={.15f,-.10f,-.20f}; // Diagnostic framing: magazine visible below the receiver.
                for(auto& hand:controls.hands)hand.aim=hand.grip;
            }
            if(magazineReloadProbe){const auto schedule=bc2::MagazineProbeSchedule(elapsed);
                controls.hands[0].grip.position={-.2f,-.25f,-.45f};controls.hands[1].grip.position={0,-.4f,0};
                for(auto& hand:controls.hands)hand.aim=hand.grip;
                controls.hands[0].trigger=schedule.marker;controls.hands[1].held=schedule.reload?interaction::Secondary:0;
            }
            if(weaponVisibilityProbe||bodyHolsterProbe){
                controls.hands[0].grip.position={-.2f,-.25f,-.45f};controls.hands[1].grip.position={.2f,-.25f,-.45f};
                for(auto& hand:controls.hands)hand.aim=hand.grip;
            }
            if(supportProbe){
                controls.hands[0].grip.position={-.068f,-.381f,-.437f};controls.hands[1].grip.position={0,-.4f,0};
                if(elapsed>=2500&&elapsed<2700)controls.hands[1].stickY=supportPrimaryDirection;
                // Warm hooks/calibration first. Free-hand motion, grab+steer,
                // release, then tracking loss while squeezed and neutral rearm.
                if(elapsed>=2500&&elapsed<3000)controls.hands[0].grip.position.y+=.08f;
                if((elapsed>=3500&&elapsed<5500)||(elapsed>=6500&&elapsed<8000)||(elapsed>=8800&&elapsed<10000))controls.hands[0].squeeze=1;
                if(elapsed>=4000&&elapsed<6000)controls.hands[0].grip.position.y+=.08f;
                if(elapsed>=7000&&elapsed<7500)controls.hands[0].gripTracked=controls.hands[0].aimTracked=false;
                if(elapsed>=9200&&elapsed<9800)controls.hands[0].grip.position.x+=.05f;
            }
            if(supportReload){
                controls.hands[0].grip.position={-.068f,-.381f,-.437f};controls.hands[0].gripTracked=controls.hands[0].aimTracked=true;
                controls.hands[0].squeeze=elapsed>=3500&&elapsed<9500?1.f:0.f;
                controls.hands[1].held=elapsed>=6000&&elapsed<6200?interaction::Secondary:0;
                controls.hands[1].trigger=elapsed>=4200&&elapsed<5500?.65f:0.f; // Visual curl, never Fire.
                // Opt-in diagnostic preparation: two ordinary native shots,
                // then the existing reload pulse. Caller must verify a selected
                // firearm with ammunition before starting; never changes slots.
                if(supportPrepareShots)controls.hands[1].trigger=
                    ((elapsed>=3300&&elapsed<3400)||(elapsed>=4500&&elapsed<4600))?1.f:0.f;
                // Free-hand touch coverage after release. Unsupported input is
                // separately exercised by the ordinary support/sight fixtures.
                controls.hands[0].touchActive=interaction::ThumbTouch|interaction::IndexTouch;
                controls.hands[0].touched=elapsed>=9700&&elapsed<10300?interaction::ThumbTouch|interaction::IndexTouch:0;
            }
            if(reloadRequestProbe){const auto schedule=bc2::RequestProbeSchedule(elapsed,supportPrepareShots);
                controls.hands[0].trigger=schedule.marker;controls.hands[0].squeeze=0;
                controls.hands[1].trigger=schedule.trigger;controls.hands[1].held=schedule.reload?interaction::Secondary:0;
                controls.hands[1].stickY=0;
            }
            if(physicalReloadProbe){
                // Actual Gameplay fixture supplies the left trajectory from
                // genuine renderer contact. Here only static source poses and
                // the existing optional two ordinary preparation shots exist.
                controls.hands[0].grip.position={-.23f,-.55f,-.02f};
                controls.hands[1].grip.position={0,-.4f,0};
                controls.hands[0].squeeze=controls.hands[0].trigger=0;
                controls.hands[1].held=0;controls.hands[1].stickY=0;
                controls.hands[1].trigger=bc2::PhysicalReloadPreparationTrigger(elapsed,supportPrepareShots);
            }
            if(shotProbe){
                if(shotReload&&elapsed>=700&&elapsed<850)controls.hands[1].held=interaction::Secondary;
                controls.hands[0].grip.position={-.2f,-.25f,-.45f};controls.hands[1].grip.position={.2f,-.25f,-.45f};
                // Exercise native selection; its per-slot candidate tables do not
                // guarantee a particular gun. Adapter guards the firearm asset.
                if((shotPrimaryPulses>=1&&elapsed>=2500&&elapsed<2700)||(shotPrimaryPulses==2&&elapsed>=3300&&elapsed<3500))controls.hands[1].stickY=shotCycleDirection;
                if(elapsed>=6000&&elapsed<7600){controls.hands[1].grip.position.x+=.2f;controls.hands[1].grip.position.y-=.1f;}
                if((elapsed>=5000&&elapsed<5080)||(elapsed>=6600&&elapsed<6680)||(elapsed>=9200&&elapsed<9280))controls.hands[1].trigger=1;
                if(elapsed>=9000&&elapsed<9400)controls.hands[1].gripTracked=controls.hands[1].aimTracked=false;
            }
            if(controlsIndependence){
                controls.hands[0].grip.position={-.2f,-.25f,-.45f};controls.hands[1].grip.position={.2f,-.25f,-.45f};
                float yaw=0;
                if(elapsed>=1000&&elapsed<3000)yaw=float(elapsed-1000)/2000.f*2.1f;
                if(elapsed>=3000&&elapsed<5000)yaw=2.1f-float(elapsed-3000)/2000.f*4.2f;
                if(elapsed>=5000&&elapsed<6000)yaw=-2.1f+float(elapsed-5000)/1000.f*2.1f;
                controls.hands[1].aim.orientation=controls.hands[1].grip.orientation={0,std::sin(yaw*.5f),0,std::cos(yaw*.5f)};
            }
            if(weaponModeCapture){
                controls.hands[0].grip.position={-.2f,-.25f,-.45f};controls.hands[1].grip.position={.2f,-.25f,-.45f};
                if(sightGesture)controls.hands[1].grip.position={0,-.4f,0};
                // Test-only request through the verified native switch map.
                // No firearm trigger or global desktop input is generated.
                if(!sightGesture){
                    if((elapsed>=2500&&elapsed<2700)||(elapsed>=8000&&elapsed<8200))controls.hands[0].held=interaction::MenuClick;
                }else{
                    // Negative ADS control on the settled rifle, separate from
                    // sight gestures: full left trigger must only curl its finger.
                    if(elapsed>=5400&&elapsed<6400)controls.hands[0].trigger=1;
                    // Measured fixture geometry only; runtime learns authored
                    // grips and validates its own sight contact independently.
                    const auto atSight=[&](bool launcher,float angle){
                        controls.hands[0].grip.position=bc2::sight_fixture::SightGrip(launcher,angle);
                    };
                    // Reach and squeeze in the same XR packet. The preceding
                    // publication still has the distant hand: no artificial
                    // neutral dwell at the contact may conceal timing defects.
                    // Complete the physical quarter-turn beyond the toggle detent,
                    // exercising direct visual continuation after acknowledgement.
                    if(elapsed>=2300&&elapsed<3800){
                        const float fraction=std::clamp((float(elapsed)-2600.f)/550.f,0.f,1.f);
                        atSight(sightStartSecondary,sightStartSecondary?1.570796327f-fraction*1.570796327f:fraction*1.570796327f);
                        if(elapsed<3700)controls.hands[0].squeeze=1;
                    }
                    // Keep support coverage in launcher mode for either start:
                    // middle dwell for rifle-first, final dwell for launcher-first.
                    const std::uint64_t supportStart=sightStartSecondary?9900:4200;
                    if(elapsed>=supportStart&&elapsed<supportStart+2500){
                        // Start1cm off the authored contact to prove grab seats the hand.
                        controls.hands[0].grip.position=bc2::sight_fixture::LauncherSupportGrip();
                        if(elapsed>=supportStart+400&&elapsed<supportStart+2000)controls.hands[0].squeeze=1;
                    }
                    if(elapsed>=7800&&elapsed<9600){
                        const float fraction=std::clamp((float(elapsed)-8000.f)/600.f,0.f,1.f);
                        atSight(!sightStartSecondary,sightStartSecondary?fraction*1.570796327f:1.570796327f-fraction*1.570796327f);
                        if(elapsed<9400)controls.hands[0].squeeze=1;
                    }
                }
            }
            // Keep the complete synthetic firearm/left-hand trajectory inside
            // the avatar arm reach. Common rigid translation preserves every
            // weapon-local sight and support target; this is diagnostic only.
            if(sightGesture){
                controls.hands[0].grip.position.z+=.10f;
                controls.hands[1].grip.position.z+=.10f;
            }
            if(controlsWeaponCycle){
                controls.hands[0].grip.position={-.2f,-.25f,-.45f};controls.hands[1].grip.position={.2f,-.25f,-.45f};
                if(elapsed>=1000&&elapsed<1800)controls.hands[1].stickY=1;
                if(elapsed>=5500&&elapsed<6300)controls.hands[1].stickY=-1;
                if(controlsFire&&elapsed>=2200&&elapsed<2280)controls.hands[1].trigger=1;
                if(controlsFire&&elapsed>=3200&&elapsed<3320)controls.hands[1].held=interaction::Secondary;
            }
            if(controlsEquip){
                controls.hands[0].grip.position={-.2f,-.25f,-.45f};controls.hands[1].grip.position={.2f,-.25f,-.45f};
                if((elapsed>=1000&&elapsed<1100)||(elapsed>=5500&&elapsed<5600))controls.hands[1].held=interaction::MenuClick;
                if(controlsFire&&elapsed>=2200&&elapsed<2280)controls.hands[1].trigger=1;
                if(controlsFire&&elapsed>=3200&&elapsed<3320)controls.hands[1].held=interaction::Secondary;
            }
            if(trackingRecovery){
                controls.hands[0].grip.position={-.2f+.06f*std::sin(float(elapsed)*.003f),-.25f,-.45f};
                controls.hands[1].grip.position={.2f+.06f*std::cos(float(elapsed)*.003f),-.25f,-.45f};
                if((elapsed>=1000&&elapsed<2000)||(elapsed>=5000&&elapsed<5400))controls.hands[0].gripTracked=controls.hands[0].aimTracked=false;
                if((elapsed>=3000&&elapsed<4000)||(elapsed>=5000&&elapsed<5400))controls.hands[1].gripTracked=controls.hands[1].aimTracked=false;
                if(controlsFire&&((elapsed>=1400&&elapsed<1480)||(elapsed>=3200&&elapsed<4600)))controls.hands[1].trigger=1;
            }
            if(controlsVehicle&&!boatHeadAimProbe){
                if(elapsed>=1000&&elapsed<1300)controls.hands[0].stickY=.25f;
                if(elapsed>=2000&&elapsed<2300)controls.hands[0].stickX=.40f;
            }
            if(controlsUse&&elapsed>=400&&elapsed<500)controls.hands[0].held=interaction::Primary;
            if(controlsPulse&&elapsed>=400&&elapsed<900)controls.hands[0].stickY=.4f;
            if(controlsTurn&&elapsed>=400&&elapsed<900)controls.hands[1].stickX=.9f;
            if(controlsFire&&!trackingRecovery&&!controlsEquip&&!controlsWeaponCycle&&elapsed>=400&&elapsed<480)controls.hands[1].trigger=1;
            if(boatFireProbe&&probe::BoatFireProbe::Trigger(elapsed)){
                controls.hands[1].trigger=1;if(!boatFireInputSamples)boatFireFirstInputMs=elapsed;boatFireLastInputMs=elapsed;++boatFireInputSamples;
            }
            if(controlsAim&&elapsed>=400&&elapsed<1900){
                const float yaw=-.3490658504f,pitch=.1745329252f;
                controls.hands[1].aim.orientation={std::cos(yaw*.5f)*std::sin(pitch*.5f),std::sin(yaw*.5f)*std::cos(pitch*.5f),-std::sin(yaw*.5f)*std::sin(pitch*.5f),std::cos(yaw*.5f)*std::cos(pitch*.5f)};
            }
            if(controlsHands&&controlsAim&&elapsed>=400&&elapsed<1900){
                // Compose the scripted wrist roll with the same yaw/pitch as
                // the aim pose, as a physical controller would provide.
                const auto a=controls.hands[1].aim.orientation,b=controls.hands[1].grip.orientation;
                controls.hands[1].grip.orientation={a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w,a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z};
            }
            if(controlsButtons){
                if(elapsed>=400&&elapsed<1100)controls.hands[0].trigger=1;
                if(elapsed>=1600&&elapsed<1900)controls.hands[1].held=interaction::Secondary;
                if(elapsed>=3200&&elapsed<3500)controls.hands[1].held=interaction::Primary;
                if((elapsed>=4800&&elapsed<5100)||(elapsed>=6400&&elapsed<6700))controls.hands[0].held=interaction::Secondary;
            }
            if(deathProbe){
                // Diagnostic only: native grenade action with downward aim.
                // This never maps a squeeze to a grenade in a regular XR session.
                controls.hands[0].grip.position={-.2f,-.25f,-.45f};controls.hands[1].grip.position={.2f,-.25f,-.45f};
                const float pitch=(elapsed>=400&&elapsed<5000)?-1.4f:0.f;
                controls.hands[1].aim.orientation=controls.hands[1].grip.orientation={std::sin(pitch*.5f),0,0,std::cos(pitch*.5f)};
                controls.hands[1].squeeze=((elapsed>=1000&&elapsed<1250)||(elapsed>=3500&&elapsed<3750))?1.f:0.f;
            }
            if(sceneControlsNeutral||magazineOriginalReturnProbe||magazineFullReturnProbe){
                Require(probe::NeutralSceneActions(controls),"Nonneutral scene packet refused before publication");
                probe::WriteNeutralSceneInput(neutralInputRecords,controls,GetTickCount64());neutralInputRecords.flush();
                Require(bool(neutralInputRecords),"Save neutral scene input evidence");++neutralInputSamples;
            }
            host.UpdateInput(controls); // Each gameplay pulse has its own explicit opt-in diagnostic flag.

        }
        if((emptyFireNeutral||shotProbe||supportProbe||weaponModeCapture||controlsVehicle||magazineReloadProbe||magazinePhysicalProbe)&&consumed>=targetPairs){Sleep(5);continue;}
        // The producer's 30-second deadline starts before the receiver connects.
        // Finish the requested pairs with drain margin; keep publishing neutral
        // controls for the unchanged 30-second observation window above.
        // Scheduling pair239 at29.875s lost the last pair at producer shutdown.
        if((reloadRequestProbe||physicalReloadProbe||magazineReloadProbe||magazinePhysicalProbe)&&GetTickCount64()-controlsStarted<std::uint64_t(consumed)*28000/targetPairs){Sleep(5);continue;}
        if((pumpHoldProbe||weaponVisibilityProbe||bodyHolsterProbe||bodyCrossDrawProbe)&&GetTickCount64()-controlsStarted<std::uint64_t(consumed)*14000/targetPairs){Sleep(5);continue;}
        if(controlsVehicle&&GetTickCount64()-controlsStarted<std::uint64_t(consumed)*6500/targetPairs){Sleep(5);continue;}
        if(weaponModeCapture&&GetTickCount64()-controlsStarted<std::uint64_t(consumed)*13000/targetPairs){Sleep(5);continue;}
        // Deliberately spread neutral requests across checkpoint loads. This
        // original diagnostic publishes no controllers; explicit neutral mode
        // publishes stationary tracked poses with every action axis/button zero.
        if(sceneRecoveryProbe&&GetTickCount64()-controlsStarted<std::uint64_t(consumed)*55000/targetPairs){Sleep(5);continue;}
        graphics::TextureDescriptor descriptor;graphics::PairTicket ticket;runtime::TrackingFrame rendered=tracking;
        const bool ready=async?host.TryGetCompletedPair(requirements,tracking,rendered,descriptor,ticket):host.TryGetPair(requirements,tracking,descriptor,ticket);
        if(!ready){++timeouts;Sleep(async?11:2);continue;}tracking=rendered;
        // A replacement scene may restart its native frame serial. Request/tracking
        // identity is still checked for every pair; owner changes are audited in the native trace.
        Require(runtime::PairMatchesFrame(requirements,tracking,descriptor,ticket)&&(sceneRecoveryProbe||ticket.frameId>lastFrame),"Native frame identity mismatch");
        if(std::memcmp(&descriptor,&opened,sizeof(descriptor))){Require(consumer.Open(device.Get(),descriptor)==graphics::TransferResult::Ok,"Open native shared eyes");opened=descriptor;}
        const std::array<graphics::TextureSlice,2> targets={graphics::TextureSlice{output.Get(),0},graphics::TextureSlice{output.Get(),1}};
        const auto copyStarted=GetTickCount64();const auto copyResult=consumer.Copy(ticket,targets);const auto copyMs=GetTickCount64()-copyStarted;
        const bool copied=copyResult==graphics::TransferResult::Ok;host.PairConsumed(ticket,copied);
        if(!copied){records<<"{\"copy_failed\":true,\"native_frame\":"<<ticket.frameId<<",\"result\":"<<unsigned(copyResult)<<",\"hresult\":"<<consumer.LastError()<<",\"copy_ms\":"<<copyMs<<"}\n";records.flush();
            if(copyResult==graphics::TransferResult::Busy){++gpuBusy;continue;}throw std::runtime_error("Copy native GPU pair HRESULT="+std::to_string(consumer.LastError()));}

        // Half-second paired eye evidence throughout the expected physical
        // manipulation window; bounded full-resolution files, no phase guessing
        // used as authority. Correlate elapsed times with fixture/Pack logs.
        const bool inspectPixels=boatFireProbe?(boatFireSchedule.Capture(GetTickCount64()-controlsStarted)||consumed+1==targetPairs):magazinePhysicalProbe?(consumed<4||consumed+1==targetPairs||(consumed>=40&&consumed<=176&&consumed%4==0)):sceneRecoveryProbe?(consumed%24==0||consumed+1==targetPairs):(weaponVisibilityProbe||bodyHolsterProbe||bodyCrossDrawProbe)?(consumed%24==0||consumed+1==targetPairs):consumed<8||consumed+1==targetPairs||((controlsAim||controlsRoomscale||capturePoses)&&(consumed==30||consumed==60||(controlsHands&&(consumed==45||consumed==75||consumed==90||consumed==120||consumed==150))));
        if(inspectPixels){
        std::array<std::vector<unsigned char>,2> pixels{Pixels(context.Get(),output.Get(),staging.Get(),0),Pixels(context.Get(),output.Get(),staging.Get(),1)};
        const bool distinct=pixels[0]!=pixels[1],changed=!consumed||(pixels[0]!=prior[0]&&pixels[1]!=prior[1]);
        records<<"{\"elapsed_ms\":"<<(GetTickCount64()-controlsStarted)<<",\"pair\":"<<consumed<<",\"native_frame\":"<<ticket.frameId<<",\"copy_ms\":"<<copyMs<<",\"sharing_mode\":"<<unsigned(descriptor.sharing)<<",\"tracking\":"<<ticket.trackingGeneration<<",\"distinct\":"<<distinct<<",\"changed\":"<<changed<<"}\n";records.flush();
        for(unsigned eye=0;eye<2;++eye){std::ofstream image(folder/("pair-"+std::to_string(consumed)+"-eye-"+std::to_string(eye)+".rgba"),std::ios::binary);image.write(reinterpret_cast<const char*>(pixels[eye].data()),pixels[eye].size());}
        if(!sceneRecoveryProbe)Require(distinct&&(staticPose||changed),"Native images did not vary across eyes/tracking");prior=std::move(pixels);
        }else{records<<"{\"elapsed_ms\":"<<(GetTickCount64()-controlsStarted)<<",\"pair\":"<<consumed<<",\"native_frame\":"<<ticket.frameId<<",\"tracking\":"<<ticket.trackingGeneration<<",\"copy_ms\":"<<copyMs<<",\"pixels_checked\":false}\n";records.flush();}
        records<<"{\"tracking_sample\":"<<ticket.trackingGeneration<<",\"asymmetric\":"<<asymmetric<<",\"static_pose\":"<<staticPose<<",\"async\":"<<async<<",\"fov\":[";
        for(unsigned eye=0;eye<2;++eye){if(eye)records<<',';const auto& f=tracking.fov[eye];records<<'['<<f.left<<','<<f.right<<','<<f.up<<','<<f.down<<']';}records<<"]}\n";records.flush();
        lastFrame=ticket.frameId;++consumed;if(async)Sleep(11);
    }
    const auto feedbackUntil=GetTickCount64()+1000;while(host.FlushFeedback()==ipc::ChannelResult::Busy&&GetTickCount64()<feedbackUntil)Sleep(1);
    const auto& delivery=host.Statistics();
    std::ofstream(folder/L"result.json")<<"{\"empty_fire_neutral_receiver\":"<<(emptyFireNeutral?"true":"false")<<",\"consumed_pairs\":"<<consumed<<",\"scene_recovery_fixture\":"<<(sceneRecoveryProbe?"true":"false")<<",\"scene_controls_neutral\":"<<(sceneControlsNeutral?"true":"false")<<",\"scene_neutral_input_samples\":"<<(sceneControlsNeutral?neutralInputSamples:0)<<",\"original_return_input_samples\":"<<(magazineOriginalReturnProbe?neutralInputSamples:0)<<",\"full_return_input_samples\":"<<(magazineFullReturnProbe?neutralInputSamples:0)<<",\"magazine_full_return_fixture\":"<<(magazineFullReturnProbe?"true":"false")<<",\"magazine_original_return_fixture\":"<<(magazineOriginalReturnProbe?"true":"false")<<",\"magazine_physical_fixture\":"<<(magazinePhysicalProbe?"true":"false")<<",\"xm8_magazine_fixture\":"<<(magazineReloadProbe?"true":"false")<<",\"reload_request_fixture\":"<<(reloadRequestProbe?"true":"false")<<",\"weapon_visibility_fixture\":"<<(weaponVisibilityProbe?"true":"false")<<",\"body_cross_draw_fixture\":"<<(bodyCrossDrawProbe?"true":"false")<<",\"body_holster_fixture\":"<<(bodyHolsterProbe?"true":"false")<<",\"physical_reload_fixture\":"<<(physicalReloadProbe?"true":"false")<<",\"boat_head_aim_fixture\":"<<(boatHeadAimProbe?"true":"false")<<",\"boat_fire_fixture\":"<<(boatFireProbe?"true":"false")<<",\"boat_fire_input_samples\":"<<boatFireInputSamples<<",\"boat_fire_first_input_ms\":"<<boatFireFirstInputMs<<",\"boat_fire_last_input_ms\":"<<boatFireLastInputMs<<",\"boat_fire_window_ms\":[2000,2080],\"boat_fire_projectile_verified\":false"<<",\"vehicle_fixture\":"<<(controlsVehicle?"true":"false")<<",\"sight_gesture_fixture\":"<<(sightGesture?"true":"false")<<",\"sight_gesture_fixture_version\":"<<(sightStartSecondary?9:8)<<",\"sight_start_secondary\":"<<(sightStartSecondary?"true":"false")<<",\"left_trigger_pose_only_fixture\":"<<(sightGesture?"true":"false")<<",\"weapon_mode_capture\":"<<(weaponModeCapture?"true":"false")<<",\"controls_started_ms\":"<<controlsStarted<<",\"support_primary_direction\":"<<supportPrimaryDirection<<",\"support_grip_requested\":"<<(supportProbe?"true":"false")<<",\"support_reload_fixture\":"<<(supportReload?"true":"false")<<",\"pump_hold_fixture\":"<<(pumpHoldProbe?"true":"false")<<",\"pump_fire_input_samples\":"<<pumpFireSamples<<",\"pump_headset_verified\":false"<<",\"support_prepare_shots\":"<<(supportPrepareShots?"true":"false")<<",\"shot_probe_requested\":"<<(shotProbe?"true":"false")<<",\"shot_primary_pulses\":"<<shotPrimaryPulses<<",\"shot_cycle_direction\":"<<shotCycleDirection<<",\"shot_schedule_version\":2,\"request_lifetime_ms\":"<<requestLifetimeMs<<",\"async_timeouts\":"<<delivery.timeouts<<",\"async_mean_latency_us\":"<<(delivery.completed?delivery.totalLatencyUs/delivery.completed:0)<<",\"async_max_latency_us\":"<<delivery.maxLatencyUs<<",\"async_completed_after_50ms\":"<<delivery.completedAfter50Ms<<",\"gpu_busy\":"<<gpuBusy<<",\"unavailable_attempts\":"<<timeouts<<",\"native_tracking_transport_verified\":"<<(consumed==targetPairs)<<",\"headset_tested\":false}\n";
    Require(consumed==targetPairs,"Incomplete native stream");std::cout<<targetPairs<<" native BC2 pairs received with matching tracking identities\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
