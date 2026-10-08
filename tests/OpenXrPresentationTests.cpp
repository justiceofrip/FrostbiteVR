#include "Test.h"
#include "fvr/xr/OpenXrHost.h"
#include "fvr/graphics/D3D11SharedPair.h"
#include "fvr/platform/windows/ProcessLifetime.h"
#include <Windows.h>
#include <wrl/client.h>
#include <vector>
#include <iostream>
#include <limits>
using Microsoft::WRL::ComPtr;
using namespace fvr;
struct Delayed final:runtime::IFrameProvider {
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;std::array<ComPtr<ID3D11Texture2D>,2> images;
    graphics::D3D11PairProducer producer;runtime::TrackingFrame pending{};unsigned consumed=0,discarded=0;std::uint64_t epoch=0;
    bool faultInjected=false,invalidPoseInjected=false;std::string error;
    bool TryGetPair(const runtime::PresentationRequirements&,const runtime::TrackingFrame&,graphics::TextureDescriptor&,graphics::PairTicket&)noexcept override{return false;}
    void Suspend()noexcept override{pending={};}
    bool TryGetCompletedPair(const runtime::PresentationRequirements& req,const runtime::TrackingFrame& current,runtime::TrackingFrame& rendered,graphics::TextureDescriptor& d,graphics::PairTicket& ticket)noexcept override{
        try {
            const unsigned frame=unsigned((current.predictedNs-1000000000)/11000000);
            if(frame>=40&&frame<=67){pending={};return false;}
            if(!pending.generation||pending.spaceGeneration!=current.spaceGeneration){pending=current;return false;}
            if(frame%4!=0)return false;
            rendered=pending;pending={};
            if(!device){D3D_FEATURE_LEVEL level{};if(FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,&level,&context)))return false;
                D3D11_TEXTURE2D_DESC desc{};desc.Width=req.width;desc.Height=req.height;desc.MipLevels=desc.ArraySize=desc.SampleDesc.Count=1;desc.Format=DXGI_FORMAT(req.format);
                for(auto& image:images)if(FAILED(device->CreateTexture2D(&desc,nullptr,&image)))return false;
            }
            if(!graphics::Valid(producer.Descriptor()))if(producer.Create(device.Get(),req.width,req.height,DXGI_FORMAT(req.format),++epoch)!=graphics::TransferResult::Ok)return false;
            const unsigned source=unsigned((rendered.predictedNs-1000000000)/11000000);
            for(unsigned eye=0;eye<2;++eye){std::vector<std::uint32_t> pixels(std::size_t(req.width)*req.height,0xff5a0000u|((eye+1)<<8)|source);context->UpdateSubresource(images[eye].Get(),0,nullptr,pixels.data(),req.width*4,0);}
            const std::array<graphics::TextureSlice,2> slices{{{images[0].Get(),0},{images[1].Get(),0}}};
            if(producer.Publish(slices,{source,rendered.spaceGeneration,rendered.generation,rendered.predictedNs},ticket)!=graphics::TransferResult::Ok)return false;
            d=producer.Descriptor();if(frame==20){ticket.sequence+=100;faultInjected=true;}
            if(frame==24){rendered.eyes[0].position.x=std::numeric_limits<float>::quiet_NaN();invalidPoseInjected=true;}
            return true;
        }catch(...){error="fixture failed";return false;}
    }
    void PairConsumed(const graphics::PairTicket& ticket,bool okay)noexcept override{if(okay)++consumed;else {++discarded;producer.Reset();}producer.Acknowledge(ticket,okay);}
};
struct InputObserver final:runtime::IFrameProvider {
    unsigned samples=0,tracked=0,released=0,invalid=0;bool floor=false;
    bool sawUntracked=false,sawInactive=false,sawBadStick=false,sawValidPose=false;
    bool TryGetPair(const runtime::PresentationRequirements&,const runtime::TrackingFrame&,graphics::TextureDescriptor&,graphics::PairTicket&)noexcept override{return false;}
    void PairConsumed(const graphics::PairTicket&,bool)noexcept override{}
    void UpdateInput(const interaction::InputFrame& f)noexcept override {
        ++samples;if(!interaction::ValidInput(f))++invalid;if(f.floorRelative)floor=true;
        const unsigned n=unsigned((f.predictedNs-1000000000)/11000000);
        if(!f.focused){++released;for(const auto& h:f.hands)if(h.active||h.held||h.gripTracked||h.aimTracked)++invalid;return;}
        if(f.worldUnitsPerMeter!=2.5f||f.referenceHead.orientation.x!=0||f.referenceHead.orientation.z!=0)++invalid;
        if(n==8||n==9){sawUntracked=true;if(f.hands[0].gripTracked||f.hands[1].aimTracked)++invalid;}
        if(n==12){sawInactive=true;if(f.hands[1].trigger!=0||(f.hands[1].active&interaction::Trigger))++invalid;}
        if(n==13){sawBadStick=true;if(f.hands[0].stickX!=0||(f.hands[0].active&interaction::Stick))++invalid;}
        if(f.hands[0].gripTracked&&f.hands[1].aimTracked){++tracked;
            if(f.hands[0].grip.position.x!=-.25f||f.hands[1].aim.position.x!=.25f||f.hands[0].grip.position.z!=-.4f||f.hands[1].aim.position.z!=-.6f)++invalid;
            else sawValidPose=true;
        }
    }
};
int wmain(int argc,wchar_t** argv){if(argc==2&&std::wstring(argv[1])==L"--lifetime-child"){Sleep(1000);return 0;}CHECK(argc==2);const auto path=std::filesystem::absolute(argv[1]);HMODULE runtime=LoadLibraryExW(path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);CHECK(runtime);
    Delayed provider;xr::HostOptions options;options.loaderPath=path;options.probeOnly=false;options.seconds=3;options.provider=&provider;
    const auto report=xr::RunOpenXrHost(options);if(!report.okay)std::cerr<<report.error<<'\n';CHECK(report.okay);
    const auto errors=reinterpret_cast<unsigned(*)()>(GetProcAddress(runtime,"FakeXrErrors"));
    const auto reuses=reinterpret_cast<unsigned(*)()>(GetProcAddress(runtime,"FakeXrReuses"));
    const auto layers=reinterpret_cast<unsigned(*)()>(GetProcAddress(runtime,"FakeXrLayers"));CHECK(errors&&reuses&&layers);
    std::cout<<"frames="<<report.endedFrames<<" fresh="<<report.submittedPairs<<" reused="<<report.reusedFrames<<" blank="<<report.blankFrames<<" rejected="<<report.rejectedPairs<<" errors="<<errors()<<'\n';
    CHECK(errors()==0);CHECK(report.endedFrames==100);CHECK(report.reusedFrames>35);CHECK(reuses()>35);CHECK(report.presentedFrames==layers());
    CHECK(report.presentedFrames==report.submittedPairs+report.reusedFrames);CHECK(report.endedFrames==report.presentedFrames+report.blankFrames);
    CHECK(report.submittedPairs==provider.consumed);CHECK(provider.faultInjected&&provider.invalidPoseInjected&&provider.discarded==2&&report.rejectedPairs==2);
    CHECK(report.maxRetainedAgeNs<=250000000&&report.maxRetainedAgeNs>100000000);CHECK(provider.error.empty());
    // Unlimited mode must have a stop source, and must exit when it signals.
    xr::HostOptions continuous;continuous.loaderPath=path;continuous.probeOnly=false;continuous.seconds=0;
    const auto invalid=xr::RunOpenXrHost(continuous);CHECK(!invalid.okay&&!invalid.instanceCreated);
    unsigned polls=0;continuous.stopRequested=[&]{return ++polls>=10;};
    const auto stopped=xr::RunOpenXrHost(continuous);CHECK(stopped.okay&&stopped.endedFrames>0&&stopped.endedFrames<20);CHECK(errors()==0);
    wchar_t executable[32768]{};CHECK(GetModuleFileNameW(nullptr,executable,32768));
    const auto filename=std::filesystem::path(executable).filename().wstring();
    platform::ProcessLifetime wrong;CHECK(!wrong.Open(GetCurrentProcessId(),L"not-the-process.exe"));
    platform::ProcessLifetime own;CHECK(own.Open(GetCurrentProcessId(),filename.c_str())&&!own.Ended());
    STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION child{};
    auto command=std::wstring(L"\"")+executable+L"\" --lifetime-child";
    CHECK(CreateProcessW(nullptr,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&child));
    platform::ProcessLifetime lifetime;CHECK(lifetime.Open(child.dwProcessId,filename.c_str())&&!lifetime.Ended());
    CHECK(WaitForSingleObject(child.hProcess,5000)==WAIT_OBJECT_0&&lifetime.Ended());CloseHandle(child.hThread);CloseHandle(child.hProcess);
    std::cout<<"Continuous stop callback and exact-process lifetime checks passed\n";
    const auto stage=reinterpret_cast<void(*)(bool)>(GetProcAddress(runtime,"FakeXrStageAvailable"));CHECK(stage);
    for(bool floor:{true,false}){
        stage(floor);InputObserver input;xr::HostOptions control;control.loaderPath=path;control.probeOnly=false;control.seconds=3;control.provider=&input;
        control.controllers=control.roomscale=true;control.worldUnitsPerMeter=2.5f;
        const auto observed=xr::RunOpenXrHost(control);if(!observed.okay)std::cerr<<observed.error<<'\n';
        CHECK(observed.okay&&observed.controllersEnabled&&observed.controllerProfiles==3&&observed.floorRelative==floor);
        CHECK(input.invalid==0&&input.samples==101&&input.released>=5&&input.tracked>80&&input.floor==floor);
        CHECK(input.sawUntracked&&input.sawInactive&&input.sawBadStick&&input.sawValidPose&&errors()==0);
    }
    std::cout<<"OpenXR controller lifecycle, predicted poses, loss handling, scale and STAGE/LOCAL fallback passed\n";
    FreeLibrary(runtime);return 0;
}
