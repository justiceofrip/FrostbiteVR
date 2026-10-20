// Explicit bounded native-menu acceptance client; never loads an XR runtime.
#include "fvr/ipc/RemoteFrameProvider.h"
#include "fvr/ipc/MenuChannel.h"
#include "fvr/graphics/D3D11SharedPair.h"
#include <Windows.h>
#include <wrl/client.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <cstring>
using Microsoft::WRL::ComPtr;
int wmain(int argc,wchar_t** argv){try{
    if(argc<3||std::wstring(argv[1])!=L"--output")throw std::runtime_error("Usage: BC2NativeMenuProbe --output NEW_FOLDER [--resume-existing] [--click-close] [--front-end-observe | --front-end-observe-long]");
    bool resumeExisting=false,clickClose=false,frontEndObserve=false,frontEndLong=false;for(int i=3;i<argc;++i){if(std::wstring(argv[i])==L"--resume-existing")resumeExisting=true;else if(std::wstring(argv[i])==L"--click-close")clickClose=true;else if(std::wstring(argv[i])==L"--front-end-observe")frontEndObserve=true;else if(std::wstring(argv[i])==L"--front-end-observe-long")frontEndObserve=frontEndLong=true;else throw std::runtime_error("Unknown menu fixture option");}
    if(frontEndObserve&&(resumeExisting||clickClose))throw std::runtime_error("Front-end observation does not click or toggle menus");
    const auto folder=std::filesystem::absolute(argv[2]);if(std::filesystem::exists(folder))throw std::runtime_error("New report folder required");std::filesystem::create_directories(folder);
    fvr::ipc::RemoteFrameProvider frames;fvr::ipc::MenuChannel menu;
    if(!frames.Create()||!menu.CreateHost(frames.Token()))throw std::runtime_error("Create menu channels");
    std::wofstream(folder/L"channel.txt")<<frames.Token();
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
    if(FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context)))throw std::runtime_error("Create menu capture GPU");
    ComPtr<IDXGIDevice> dxgi;ComPtr<IDXGIAdapter> adapter;DXGI_ADAPTER_DESC gpu{};
    if(FAILED(device.As(&dxgi))||FAILED(dxgi->GetAdapter(&adapter))||FAILED(adapter->GetDesc(&gpu)))throw std::runtime_error("GPU identity");
    fvr::runtime::PresentationRequirements requirements{1920,1080,29,gpu.AdapterLuid.LowPart,gpu.AdapterLuid.HighPart};
    fvr::graphics::D3D11PairConsumer worldConsumer;fvr::graphics::TextureDescriptor worldOpened{};
    D3D11_TEXTURE2D_DESC worldDesc{};worldDesc.Width=1920;worldDesc.Height=1080;worldDesc.MipLevels=1;worldDesc.ArraySize=2;worldDesc.Format=DXGI_FORMAT_R8G8B8A8_TYPELESS;worldDesc.SampleDesc.Count=1;
    ComPtr<ID3D11Texture2D> worldEyes;if(FAILED(device->CreateTexture2D(&worldDesc,nullptr,&worldEyes)))throw std::runtime_error("World eye target");
    unsigned worldPairs=0,resumedPairs=0;std::uint64_t worldGeneration=0,gameplayAt=0,resumeClickAt=0,closeClickAt=0;
    bool resumeDone=!resumeExisting,closeDownDone=false;
    frames.SetRequestLifetime(150);
    fvr::graphics::D3D11PairConsumer consumer;fvr::graphics::TextureDescriptor opened{};
    std::array<ComPtr<ID3D11Texture2D>,2> scratch;std::ofstream events(folder/L"events.jsonl");
    const auto start=GetTickCount64();std::uint64_t connectedAt=0,sequence=0,openAt=0,closeAt=0;unsigned images=0,errors=0;
    bool openedMenu=false,closedMenu=false,saved=false,invalidInitial=false;unsigned frontEndReadySamples=0;
    std::uint64_t savedAtMs=0;
    constexpr std::uint64_t FrontEndCaptureDelayMs=6000;
    // Hold the front end beyond the launcher's former 35-second false timeout.
    // Keep one capture; the off-centre point detects aspect-conversion mistakes
    // which a centre-only hover would hide. This mode sends no button input.
    const std::uint64_t frontEndObserveMs=frontEndLong?45000:FrontEndCaptureDelayMs;
    fvr::ipc::MenuMode previous=fvr::ipc::MenuMode::Unknown;
    fvr::ipc::MenuControl control;control.space=1;control.flags=fvr::ipc::MenuFocused;
    fvr::graphics::PairTicket pending{};bool feedback=false,consumed=false;
    while(GetTickCount64()-start<(frontEndLong?60000u:20000u)){
        const auto now=GetTickCount64();fvr::ipc::MenuState state;
        // Match a connected, neutral VR player even while the menu owns input.
        // The current body/vehicle camera requires fresh tracking input; this
        // fixture previously requested eyes without publishing any input.
        fvr::runtime::TrackingFrame tracking{};tracking.generation=++worldGeneration;tracking.spaceGeneration=1;
        tracking.predictedNs=10000000000LL+std::int64_t(worldGeneration)*11000000;
        tracking.focused=tracking.headValid=true;tracking.eyes={tracking.head,tracking.head};
        tracking.eyes[0].position.x=-.032f;tracking.eyes[1].position.x=.032f;
        tracking.fov={fvr::math::FovTangents{-1,1,.8f,-.8f},fvr::math::FovTangents{-1,1,.8f,-.8f}};
        fvr::interaction::InputFrame input{};input.generation=worldGeneration;input.spaceGeneration=1;
        input.predictedNs=tracking.predictedNs;input.focused=input.headValid=true;input.referenceHead=tracking.referenceHead;input.head=tracking.head;
        for(unsigned h=0;h<2;++h){auto& hand=input.hands[h];hand.gripTracked=hand.aimTracked=true;hand.active=fvr::interaction::Components;
            hand.grip.position={h?.2f:-.2f,-.25f,-.45f};hand.aim=hand.grip;}
        frames.UpdateInput(input);
        if(menu.ReadState(state)==fvr::ipc::ChannelResult::Ok){
            if(!connectedAt){connectedAt=now;invalidInitial=(resumeExisting||frontEndObserve)?state.mode!=fvr::ipc::MenuMode::Menu:state.mode!=fvr::ipc::MenuMode::Gameplay;}
            if(state.mode!=previous){events<<"{\"ms\":"<<now-connectedAt<<",\"mode\":"<<unsigned(state.mode)<<",\"epoch\":"<<state.epoch<<",\"input_ready\":"<<state.inputReady<<"}\n";events.flush();previous=state.mode;}
            if(invalidInitial)break;
            if(frontEndObserve){
                if(state.mode!=fvr::ipc::MenuMode::Menu){invalidInitial=true;break;}
                if(state.inputReady)++frontEndReadySamples;
                // Exercise only the native laser-hover route. No button/toggle,
                // level selection, keyboard or desktop cursor input is sent.
                control.u=frontEndLong?.25f:.5f;control.v=frontEndLong?.25f:.5f;control.flags=fvr::ipc::MenuFocused|fvr::ipc::MenuPoint;
                frames.Suspend();
            }
            if(!frontEndObserve&&!resumeDone){
                if(state.mode==fvr::ipc::MenuMode::Gameplay){resumeDone=true;gameplayAt=now;control.flags=fvr::ipc::MenuFocused;}
                else {control.u=.791f;control.v=.124f;control.flags=fvr::ipc::MenuFocused|fvr::ipc::MenuPoint;
                    if(!resumeClickAt&&now-connectedAt>=750)resumeClickAt=now;
                    if(resumeClickAt&&now-resumeClickAt<150)control.flags|=fvr::ipc::MenuDown;}
            }
            if(!frontEndObserve&&resumeDone&&state.mode==fvr::ipc::MenuMode::Gameplay){
                if(!gameplayAt)gameplayAt=now;
                if(!openAt||closeAt){
                    fvr::runtime::TrackingFrame rendered{};fvr::graphics::TextureDescriptor d{};fvr::graphics::PairTicket t{};
                    if(frames.TryGetCompletedPair(requirements,tracking,rendered,d,t)){
                        if(std::memcmp(&worldOpened,&d,sizeof(d))){worldOpened={};if(worldConsumer.Open(device.Get(),d)==fvr::graphics::TransferResult::Ok)worldOpened=d;}
                        const bool copied=fvr::graphics::Valid(worldOpened)&&worldConsumer.Copy(t,{{{worldEyes.Get(),0},{worldEyes.Get(),1}}})==fvr::graphics::TransferResult::Ok;
                        frames.PairConsumed(t,copied);if(copied){++worldPairs;if(closeAt)++resumedPairs;}else ++errors;
                    }
                }else frames.FlushFeedback();
                if(!openAt&&now-gameplayAt>=750&&worldPairs>=8){control.toggle=1;openAt=now;}
            }
            else frames.Suspend();
            if(openAt&&state.mode==fvr::ipc::MenuMode::Menu)openedMenu=true;
            if(openedMenu&&!closeAt&&now-openAt>=3000){
                if(clickClose){control.u=.791f;control.v=.124f;control.flags=fvr::ipc::MenuFocused|fvr::ipc::MenuPoint;
                    if(!closeClickAt)closeClickAt=now;
                    if(now-closeClickAt>=150&&!closeDownDone){control.flags|=fvr::ipc::MenuDown;if(now-closeClickAt>=300)closeDownDone=true;}
                    if(closeDownDone){control.flags=fvr::ipc::MenuFocused|fvr::ipc::MenuPoint;closeAt=now;}
                }else {control.toggle=2;closeAt=now;}
            }
            if(closeAt&&state.mode==fvr::ipc::MenuMode::Gameplay)closedMenu=true;
            control.menuEpoch=state.epoch;
        }
        control.sequence=++sequence;menu.PublishControl(control);
        if(feedback){const auto r=menu.SurfaceConsumed(pending,consumed);if(r!=fvr::ipc::ChannelResult::Busy)feedback=false;}
        fvr::ipc::MenuSurface surface;
        if(!feedback&&menu.TakeSurface(surface)==fvr::ipc::ChannelResult::Ok){
            pending=surface.ticket;feedback=true;consumed=false;const auto& d=surface.descriptor;
            if(std::memcmp(&opened,&d,sizeof(d))){opened={};consumer.Reset();
                if(consumer.Open(device.Get(),d)==fvr::graphics::TransferResult::Ok){D3D11_TEXTURE2D_DESC td{};td.Width=d.width;td.Height=d.height;td.Format=DXGI_FORMAT(d.format);td.MipLevels=td.ArraySize=td.SampleDesc.Count=1;bool okay=true;for(auto& t:scratch){t.Reset();okay&=SUCCEEDED(device->CreateTexture2D(&td,nullptr,&t));}if(okay)opened=d;}
            }
            if(fvr::graphics::Valid(opened))consumed=consumer.Copy(surface.ticket,{{{scratch[0].Get(),0},{scratch[1].Get(),0}}})==fvr::graphics::TransferResult::Ok;
            // Capture once after the startup timeline has had time to advance.
            // The first received surface can be the intro's black frame or title
            // artwork; successful transport is not proof of visible menu buttons.
            if(consumed){++images;if(!saved&&(!frontEndObserve||(connectedAt&&now-connectedAt>=FrontEndCaptureDelayMs))){D3D11_TEXTURE2D_DESC td{};scratch[0]->GetDesc(&td);td.Usage=D3D11_USAGE_STAGING;td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;ComPtr<ID3D11Texture2D> stage;
                if(SUCCEEDED(device->CreateTexture2D(&td,nullptr,&stage))){context->CopyResource(stage.Get(),scratch[0].Get());D3D11_MAPPED_SUBRESOURCE map{};
                    if(SUCCEEDED(context->Map(stage.Get(),0,D3D11_MAP_READ,0,&map))){std::ofstream raw(folder/L"menu.rgba",std::ios::binary);for(unsigned y=0;y<d.height;++y)raw.write(static_cast<const char*>(map.pData)+y*map.RowPitch,d.width*4);context->Unmap(stage.Get(),0);saved=bool(raw);if(saved)savedAtMs=now-connectedAt;std::ofstream(folder/L"image.json")<<"{\"width\":"<<d.width<<",\"height\":"<<d.height<<",\"format\":"<<d.format<<",\"elapsed_ms\":"<<savedAtMs<<",\"surface_index\":"<<images<<",\"visual_acceptance\":false}";}
                }
            }}else ++errors;
            const auto r=menu.SurfaceConsumed(pending,consumed);if(r!=fvr::ipc::ChannelResult::Busy)feedback=false;
        }
        if(frontEndObserve&&images>=60&&frontEndReadySamples>=60&&saved&&connectedAt&&now-connectedAt>=frontEndObserveMs)break;
        if(closedMenu&&resumedPairs>=8&&now-closeAt>=500)break;
        // Bound open failure and close the menu through the same original path.
        if(!frontEndObserve&&connectedAt&&now-connectedAt>13000){if(openedMenu&&!closeAt){control.toggle=2;closeAt=now;}if(!openedMenu||now-connectedAt>15000)break;}
        Sleep(10);
    }
    control.sequence=++sequence;control.flags=0;menu.PublishControl(control);
    const auto observedMs=connectedAt?GetTickCount64()-connectedAt:0;
    const bool sequenceOkay=frontEndObserve?(images>=60&&frontEndReadySamples>=60&&observedMs>=frontEndObserveMs):(resumeDone&&worldPairs>=8&&resumedPairs>=8&&openedMenu&&closedMenu);
    const bool okay=sequenceOkay&&images&&saved&&!errors&&!invalidInitial;
    std::ofstream(folder/L"result.json")<<"{\"opened\":"<<(openedMenu?"true":"false")<<",\"closed\":"<<(closedMenu?"true":"false")<<",\"images\":"<<images<<",\"errors\":"<<errors<<",\"saved\":"<<(saved?"true":"false")<<",\"invalid_initial\":"<<(invalidInitial?"true":"false")<<",\"passed\":"<<(okay?"true":"false")<<",\"world_pairs\":"<<worldPairs<<",\"resumed_pairs\":"<<resumedPairs<<",\"neutral_input_samples\":"<<worldGeneration<<",\"resume_existing\":"<<resumeExisting<<",\"click_close\":"<<clickClose<<",\"front_end_observe\":"<<frontEndObserve<<",\"front_end_ready_samples\":"<<frontEndReadySamples<<",\"observed_ms\":"<<observedMs<<",\"off_center_hover\":"<<frontEndLong<<",\"headset_tested\":false}";
    return okay?0:2;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
