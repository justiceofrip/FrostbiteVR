#include "Test.h"
#include "fvr/xr/OpenXrHost.h"
#include "fvr/ipc/RemoteFrameProvider.h"
#include "fvr/ipc/MenuChannel.h"
#include "fvr/graphics/D3D11SharedPair.h"
#include <Windows.h>
#include <wrl/client.h>
#include <vector>
#include <iostream>
using namespace fvr;
using Microsoft::WRL::ComPtr;
struct WorldAndMenu final:runtime::IFrameProvider {
    ipc::RemoteFrameProvider remote;ipc::FrameChannel producer;ipc::MenuChannel menuHost,menuProducer;
    graphics::D3D11PairProducer textures;ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
    std::array<ComPtr<ID3D11Texture2D>,2> images;graphics::PairTicket waitingTicket{};
    bool waiting=false,publishedMenu=false,expectedMenu=false;unsigned frame=0,errors=0,consumed=0,discarded=0,menuSuspends=0,menuFrames=0,oldPairDelivered=0;
    bool cancelledOpeningPair=false,resumedFresh=false;std::uint64_t epoch=0,stateSequence=0;
    unsigned (*layers)()=nullptr;unsigned previousLayers=0;
    bool Initialize(){return remote.Create()&&producer.ConnectProducer(remote.Token())&&menuHost.CreateHost(remote.Token())&&menuProducer.ConnectProducer(remote.Token());}
    bool TryGetPair(const runtime::PresentationRequirements&,const runtime::TrackingFrame&,graphics::TextureDescriptor&,graphics::PairTicket&)noexcept override{return false;}
    bool TryGetCompletedPair(const runtime::PresentationRequirements& r,const runtime::TrackingFrame& t,runtime::TrackingFrame& rendered,graphics::TextureDescriptor& d,graphics::PairTicket& p)noexcept override {
        if(expectedMenu)++errors;
        const bool okay=remote.TryGetCompletedPair(r,t,rendered,d,p);
        if(okay){const auto source=unsigned((rendered.predictedNs-1000000000)/11000000);if(source==9&&frame>=19)++oldPairDelivered;if(frame>=19&&source>=19)resumedFresh=true;}
        return okay;
    }
    void PairConsumed(const graphics::PairTicket& t,bool c)noexcept override{remote.PairConsumed(t,c);}
    void Suspend()noexcept override{if(expectedMenu)++menuSuspends;remote.Suspend();}
    void Pump(){
        if(waiting){const auto result=producer.PollOutcome();if(result==ipc::Outcome::Pending)return;
            textures.Acknowledge(waitingTicket,result==ipc::Outcome::Consumed);
            if(result==ipc::Outcome::Consumed)++consumed;else{++discarded;if(waitingTicket.frameId==9&&expectedMenu)cancelledOpeningPair=true;textures.Reset();}
            waiting=false;
        }
        ipc::FrameRequest request;if(producer.TryTake(request)!=ipc::ChannelResult::Ok)return;
        runtime::PresentationRequirements req;runtime::TrackingFrame tracking;
        if(!ipc::Decode(request,req,tracking)){++errors;producer.Skip();return;}
        if(!device){D3D_FEATURE_LEVEL level{};if(FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,&level,&context))){++errors;producer.Skip();return;}
            D3D11_TEXTURE2D_DESC desc{};desc.Width=req.width;desc.Height=req.height;desc.Format=DXGI_FORMAT(req.format);desc.MipLevels=desc.ArraySize=desc.SampleDesc.Count=1;
            for(auto& image:images)if(FAILED(device->CreateTexture2D(&desc,nullptr,&image))){++errors;producer.Skip();return;}}
        if(!graphics::Valid(textures.Descriptor())&&textures.Create(device.Get(),req.width,req.height,DXGI_FORMAT(req.format),++epoch)!=graphics::TransferResult::Ok){++errors;producer.Skip();return;}
        const auto source=unsigned((tracking.predictedNs-1000000000)/11000000);
        for(unsigned eye=0;eye<2;++eye){std::vector<std::uint32_t> pixels(std::size_t(req.width)*req.height,0xff5a0000u|((eye+1)<<8)|source);context->UpdateSubresource(images[eye].Get(),0,nullptr,pixels.data(),req.width*4,0);}
        const std::array<graphics::TextureSlice,2> slices{{{images[0].Get(),0},{images[1].Get(),0}}};
        if(textures.Publish(slices,{source,tracking.spaceGeneration,tracking.generation,tracking.predictedNs},waitingTicket)!=graphics::TransferResult::Ok||
           producer.Publish(textures.Descriptor(),waitingTicket)!=ipc::ChannelResult::Ok){++errors;textures.Reset();producer.Skip();return;}
        waiting=true;
    }
    void UpdateInput(const interaction::InputFrame& input)noexcept override {
        try{
            frame=unsigned((input.predictedNs-1000000000)/11000000);expectedMenu=publishedMenu;
            // Counter observes EndFrame of the preceding frame. No world image
            // may survive menu entry, even while the menu raster is unavailable.
            const auto nowLayers=layers();if(frame>=11&&frame<=19&&nowLayers!=previousLayers)++errors;previousLayers=nowLayers;
            if(expectedMenu){++menuFrames;if(input.focused||input.hands[0].active||input.hands[1].active)++errors;}
            publishedMenu=frame>=9&&frame<=17;
            const ipc::MenuState state{++stateSequence,publishedMenu?2u:3u,0,publishedMenu?ipc::MenuMode::Menu:ipc::MenuMode::Gameplay,1280,720,1};
            if(menuProducer.PublishState(state)!=ipc::ChannelResult::Ok)++errors;
            remote.UpdateInput(input);Pump();
        }catch(...){++errors;}
    }
};
int wmain(int argc,wchar_t** argv){
    CHECK(argc==2);const auto path=std::filesystem::absolute(argv[1]);
    auto dll=LoadLibraryExW(path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);CHECK(dll);
    const auto errors=reinterpret_cast<unsigned(*)()>(GetProcAddress(dll,"FakeXrErrors"));
    const auto layers=reinterpret_cast<unsigned(*)()>(GetProcAddress(dll,"FakeXrLayers"));CHECK(errors&&layers);
    WorldAndMenu provider;provider.layers=layers;CHECK(provider.Initialize());
    xr::HostOptions options;options.loaderPath=path;options.probeOnly=false;options.seconds=3;options.provider=&provider;options.menu=&provider.menuHost;
    const auto report=xr::RunOpenXrHost(options);provider.Pump();if(!report.okay)std::cerr<<report.error<<'\n';
    std::cout<<"menu_frames="<<provider.menuFrames<<" menu_suspends="<<provider.menuSuspends<<" opening_pair_cancelled="<<provider.cancelledOpeningPair
        <<" old_pair_delivered="<<provider.oldPairDelivered<<" fresh_resume="<<provider.resumedFresh<<" consumed="<<provider.consumed<<" discarded="<<provider.discarded<<" errors="<<provider.errors+errors()<<'\n';
    CHECK(report.okay&&report.endedFrames==100&&errors()==0&&provider.errors==0);
    CHECK(provider.menuFrames==9&&provider.menuSuspends>=provider.menuFrames&&provider.cancelledOpeningPair&&!provider.oldPairDelivered&&provider.resumedFresh);
    CHECK(report.submittedPairs>20&&report.submittedPairs==provider.consumed&&!provider.waiting);
    CHECK(report.menuFrames==0&&report.menuErrors==0);FreeLibrary(dll);return 0;
}
