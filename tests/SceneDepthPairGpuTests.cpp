// Explicit hardware test: real shared resources and the existing color fences.
// Never registered in CTest; no game or XR session is started.
#include "fvr/graphics/D3D11SceneDepthPair.h"
#include "fvr/graphics/D3D11SharedPair.h"
#include "fvr/graphics/D3D11SceneDepthCompositor.h"
#include "fvr/graphics/D3D11FrameBridge.h"
#include "fvr/ipc/RemoteFrameProvider.h"
#include "Test.h"
#include <Windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <array>
#include <iostream>
using namespace fvr::graphics;using Microsoft::WRL::ComPtr;
namespace {
struct Color {ComPtr<ID3D11Texture2D> texture;ComPtr<ID3D11RenderTargetView> target;ComPtr<ID3D11ShaderResourceView> view;};
bool MakeColor(ID3D11Device* d,Color& out){D3D11_TEXTURE2D_DESC desc{};desc.Width=desc.Height=32;desc.ArraySize=desc.MipLevels=desc.SampleDesc.Count=1;desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
 return SUCCEEDED(d->CreateTexture2D(&desc,nullptr,&out.texture))&&SUCCEEDED(d->CreateRenderTargetView(out.texture.Get(),nullptr,&out.target))&&SUCCEEDED(d->CreateShaderResourceView(out.texture.Get(),nullptr,&out.view));}
std::array<unsigned char,4> Pixel(ID3D11Device* d,ID3D11DeviceContext* c,const Color& target){D3D11_TEXTURE2D_DESC desc{};target.texture->GetDesc(&desc);desc.BindFlags=0;desc.Usage=D3D11_USAGE_STAGING;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
 ComPtr<ID3D11Texture2D> staging;if(FAILED(d->CreateTexture2D(&desc,nullptr,&staging)))return {};c->CopyResource(staging.Get(),target.texture.Get());D3D11_MAPPED_SUBRESOURCE map{};
 if(FAILED(c->Map(staging.Get(),0,D3D11_MAP_READ,0,&map)))return {};const auto* p=static_cast<const unsigned char*>(map.pData)+16*map.RowPitch+16*4;std::array<unsigned char,4> result{p[0],p[1],p[2],p[3]};c->Unmap(staging.Get(),0);return result;}
bool Publish(D3D11PairProducer& producer,const std::array<TextureSlice,2>& sources,const FrameMetadata& frame,PairTicket& ticket){
 const auto end=GetTickCount64()+2000;do{auto result=producer.Publish(sources,frame,ticket);if(result==TransferResult::Ok)return true;if(result!=TransferResult::Busy)return false;Sleep(1);}while(GetTickCount64()<end);return false;}
int Run(unsigned samples){ComPtr<ID3D11Device> a,b;ComPtr<ID3D11DeviceContext> ac,bc;D3D_FEATURE_LEVEL fl;
 CHECK(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&a,&fl,&ac)));
 CHECK(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&b,&fl,&bc)));
 D3D11SceneDepthProducer depth;D3D11SceneDepthConsumer reader;CHECK(depth.Initialize(a.Get())&&reader.Initialize(b.Get()));
 D3D11PairProducer color;D3D11PairConsumer colorReader;CHECK(color.Create(a.Get(),32,32,DXGI_FORMAT_R8G8B8A8_UNORM,1,TextureSharing::LegacyFenced)==TransferResult::Ok);
 auto descriptor=color.Descriptor();CHECK(colorReader.Open(b.Get(),descriptor)==TransferResult::Ok);
 std::array<Color,2> original,copied,output;Color props;CHECK(MakeColor(b.Get(),props));const float blue[]{0,0,1,1},red[]{1,0,0,1};bc->ClearRenderTargetView(props.target.Get(),red);
 for(unsigned eye=0;eye<2;++eye){CHECK(MakeColor(a.Get(),original[eye])&&MakeColor(b.Get(),copied[eye])&&MakeColor(b.Get(),output[eye]));ac->ClearRenderTargetView(original[eye].target.Get(),blue);}
 const std::array<TextureSlice,2> source{{{original[0].texture.Get(),0},{original[1].texture.Get(),0}}},destination{{{copied[0].texture.Get(),0},{copied[1].texture.Get(),0}}};
 D3D11_TEXTURE2D_DESC dd{};dd.Width=dd.Height=32;dd.MipLevels=dd.ArraySize=1;dd.SampleDesc.Count=samples;dd.Format=DXGI_FORMAT_R24G8_TYPELESS;dd.BindFlags=D3D11_BIND_DEPTH_STENCIL;
 ComPtr<ID3D11Texture2D> nativeDepth;CHECK(SUCCEEDED(a->CreateTexture2D(&dd,nullptr,&nativeDepth)));D3D11_DEPTH_STENCIL_VIEW_DESC dv{};dv.Format=DXGI_FORMAT_D24_UNORM_S8_UINT;dv.ViewDimension=samples==1?D3D11_DSV_DIMENSION_TEXTURE2D:D3D11_DSV_DIMENSION_TEXTURE2DMS;
 ComPtr<ID3D11DepthStencilView> target;CHECK(SUCCEEDED(a->CreateDepthStencilView(nativeDepth.Get(),&dv,&target)));
 std::array<float,32*32> values;values.fill(.5f);dd.SampleDesc.Count=1;dd.Format=DXGI_FORMAT_R32_FLOAT;dd.BindFlags=D3D11_BIND_SHADER_RESOURCE;D3D11_SUBRESOURCE_DATA initial{};initial.pSysMem=values.data();initial.SysMemPitch=32*4;
 ComPtr<ID3D11Texture2D> propDepth;ComPtr<ID3D11ShaderResourceView> propDepthView;CHECK(SUCCEEDED(b->CreateTexture2D(&dd,&initial,&propDepth))&&SUCCEEDED(b->CreateShaderResourceView(propDepth.Get(),nullptr,&propDepthView)));
 D3D11SceneDepthCompositor compositor;CHECK(compositor.Initialize(bc.Get()));
 FrameMetadata frame{1,1,1,1000000000};CHECK(depth.Begin(frame));CHECK(!depth.Capture(1,0,nativeDepth.Get()));depth.Cancel();CHECK(!depth.Begin(frame));
 ++frame.frameId;++frame.trackingGeneration;
 const auto capture=[&](bool firstOccluded){if(!depth.Begin(frame))return false;for(unsigned i=0;i<4;++i){ac->ClearDepthStencilView(target.Get(),D3D11_CLEAR_DEPTH|D3D11_CLEAR_STENCIL,(firstOccluded&&i==1)?.25f:.75f,1);if(!depth.Capture(i/2,i%2,nativeDepth.Get()))return false;}return true;};
 CHECK(capture(true));PairTicket ticket;CHECK(Publish(color,source,frame,ticket));SceneDepthFrame attachment;CHECK(depth.Seal(descriptor,ticket,false,attachment));
 const auto firstGeneration=attachment.resourceGeneration;auto nextFrame=frame;++nextFrame.frameId;CHECK(!depth.Begin(nextFrame));depth.Cancel();CHECK(!depth.Begin(nextFrame));
 auto wrong=ticket;++wrong.sequence;depth.Acknowledge(wrong,true);CHECK(!depth.Begin(nextFrame));
 CHECK(reader.CopyBeforeColor(attachment,descriptor,ticket));CHECK(!reader.CopyBeforeColor(attachment,descriptor,ticket));CHECK(colorReader.Copy(ticket,destination)==TransferResult::Ok);
 SceneDepthViews views;CHECK(reader.Views(ticket,views));CHECK(!reader.Views(wrong,views));CHECK(reader.Views(ticket,views));
 depth.Acknowledge(ticket,true);color.Acknowledge(ticket,true);
 // Overwrite and fence producer resources for the NEXT frame before drawing
 // with the previous local depth. The old local snapshots must remain stable.
 ++frame.frameId;++frame.trackingGeneration;CHECK(capture(false));PairTicket second;CHECK(Publish(color,source,frame,second));SceneDepthFrame attachment2;CHECK(depth.Seal(descriptor,second,false,attachment2));
 CHECK(attachment2.resourceGeneration==firstGeneration&&attachment2.resources==attachment.resources);
 for(unsigned eye=0;eye<2;++eye){CHECK(compositor.Compose({copied[eye].view.Get(),props.view.Get(),propDepthView.Get(),views.eyes[eye][0],views.eyes[eye][1],output[eye].target.Get()}));
  const auto p=Pixel(b.Get(),bc.Get(),output[eye]);CHECK(p[0]==(eye?255:0)&&p[2]==(eye?0:255));}
 CHECK(reader.CopyBeforeColor(attachment2,descriptor,second));CHECK(colorReader.Copy(second,destination)==TransferResult::Ok);CHECK(!reader.Views(ticket,views));CHECK(reader.Views(second,views));
 for(unsigned eye=0;eye<2;++eye){CHECK(compositor.Compose({copied[eye].view.Get(),props.view.Get(),propDepthView.Get(),views.eyes[eye][0],views.eyes[eye][1],output[eye].target.Get()}));CHECK(Pixel(b.Get(),bc.Get(),output[eye])[0]==255);}
 // Failure feedback retires the source set. A new epoch must not reuse the
 // previously shared allocation while a consumer can still hold it open.
 depth.Acknowledge(second,false);color.Acknowledge(second,false);CHECK(color.Create(a.Get(),32,32,DXGI_FORMAT_R8G8B8A8_UNORM,2,TextureSharing::LegacyFenced)==TransferResult::Ok);
 descriptor=color.Descriptor();CHECK(colorReader.Open(b.Get(),descriptor)==TransferResult::Ok);++frame.frameId;++frame.trackingGeneration;CHECK(capture(true));PairTicket third;CHECK(Publish(color,source,frame,third));SceneDepthFrame attachment3;CHECK(depth.Seal(descriptor,third,false,attachment3));
 CHECK(attachment3.resourceGeneration>firstGeneration&&attachment3.resources!=attachment2.resources);CHECK(reader.CopyBeforeColor(attachment3,descriptor,third));CHECK(colorReader.Copy(third,destination)==TransferResult::Ok);
 CHECK(reader.Views(third,views));CHECK(compositor.Compose({copied[0].view.Get(),props.view.Get(),propDepthView.Get(),views.eyes[0][0],views.eyes[0][1],output[0].target.Get()}));CHECK(Pixel(b.Get(),bc.Get(),output[0])[2]==255);
 depth.Acknowledge(third,true);color.Acknowledge(third,true);
 // A scene load restarts its native frame number without reopening the channel.
 CHECK(color.Create(a.Get(),32,32,DXGI_FORMAT_R8G8B8A8_UNORM,3,TextureSharing::LegacyFenced)==TransferResult::Ok);
 descriptor=color.Descriptor();CHECK(colorReader.Open(b.Get(),descriptor)==TransferResult::Ok);
 frame.frameId=1;++frame.trackingGeneration;CHECK(capture(false));PairTicket restarted;
 CHECK(Publish(color,source,frame,restarted));SceneDepthFrame afterScene;CHECK(depth.Seal(descriptor,restarted,false,afterScene));
 CHECK(reader.CopyBeforeColor(afterScene,descriptor,restarted));CHECK(colorReader.Copy(restarted,destination)==TransferResult::Ok);
 CHECK(reader.Views(restarted,views));depth.Acknowledge(restarted,true);color.Acknowledge(restarted,true);
 reader.Reset();CHECK(!reader.Views(restarted,views));depth.Close();
 std::cout<<"SceneDepthPair GPU samples="<<samples<<": color fences, both eyes, delayed composition, wrong ACK, discard generation passed\n";return 0;
}
}
int RunBridge(){using namespace fvr;ComPtr<ID3D11Device> a,b;ComPtr<ID3D11DeviceContext> ac,bc;D3D_FEATURE_LEVEL fl;
 CHECK(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&a,&fl,&ac)));
 CHECK(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&b,&fl,&bc)));
 ComPtr<IDXGIDevice> dx;ComPtr<IDXGIAdapter> adapter;DXGI_ADAPTER_DESC gpu{};CHECK(SUCCEEDED(a.As(&dx))&&SUCCEEDED(dx->GetAdapter(&adapter))&&SUCCEEDED(adapter->GetDesc(&gpu)));
 ipc::RemoteFrameProvider host;CHECK(host.Create());D3D11FrameBridge bridge;CHECK(bridge.ConnectGraphics(a.Get(),host.Token(),FrameSharingPreference::LegacyIpc));
 runtime::PresentationRequirements req{32,32,28,gpu.AdapterLuid.LowPart,gpu.AdapterLuid.HighPart};runtime::TrackingFrame input{};
 input.spaceGeneration=1;input.focused=input.headValid=true;input.fov={math::FovTangents{-1,1,1,-1},math::FovTangents{-1,1,1,-1}};
 D3D11SceneDepthConsumer depth;CHECK(depth.Initialize(b.Get()));D3D11PairConsumer color;TextureDescriptor opened{};
 Color source;std::array<Color,2> target;CHECK(MakeColor(a.Get(),source));for(auto& t:target)CHECK(MakeColor(b.Get(),t));
 const float blue[]{0,0,1,1};ac->ClearRenderTargetView(source.target.Get(),blue);
 D3D11_TEXTURE2D_DESC dd{};dd.Width=dd.Height=32;dd.MipLevels=dd.ArraySize=1;dd.SampleDesc.Count=8;dd.Format=DXGI_FORMAT_R24G8_TYPELESS;dd.BindFlags=D3D11_BIND_DEPTH_STENCIL;
 ComPtr<ID3D11Texture2D> native;CHECK(SUCCEEDED(a->CreateTexture2D(&dd,nullptr,&native)));D3D11_DEPTH_STENCIL_VIEW_DESC ds{};ds.Format=DXGI_FORMAT_D24_UNORM_S8_UINT;ds.ViewDimension=D3D11_DSV_DIMENSION_TEXTURE2DMS;
 ComPtr<ID3D11DepthStencilView> dsv;CHECK(SUCCEEDED(a->CreateDepthStencilView(native.Get(),&ds,&dsv)));
 std::uint64_t epoch=0;
 for(unsigned step=0;step<5;++step){input.generation=step+1;input.predictedNs=1000000000+step*10000000;runtime::TrackingFrame rendered;TextureDescriptor descriptor;PairTicket ticket;
  CHECK(!host.TryGetCompletedPair(req,input,rendered,descriptor,ticket));ipc::FrameLease lease;
  CHECK(bridge.TryBegin({step<2?1u:2u,step<2?100u+step:step-1u,1},lease));
  const bool started=bridge.BeginDepthCapture(lease,false);CHECK(started==(step!=0));
  for(unsigned eye=0;eye<2;++eye){for(unsigned stage=0;stage<2;++stage){
    ac->ClearDepthStencilView(dsv.Get(),D3D11_CLEAR_DEPTH|D3D11_CLEAR_STENCIL,.75f,0);
    if(started&&step!=3)CHECK(bridge.CaptureDepth(eye,stage,native.Get(),lease));}
   CHECK(bridge.CaptureEye(eye,source.texture.Get(),lease));}
  CHECK(bridge.PublishRestored(lease));const auto deadline=GetTickCount64()+2000;bool delivered=false;
  do{bridge.PumpGraphics();delivered=host.TryGetCompletedPair(req,input,rendered,descriptor,ticket);if(!delivered)Sleep(1);}while(!delivered&&GetTickCount64()<deadline);
  CHECK(delivered);if(step==2)CHECK(descriptor.resourceEpoch>epoch);epoch=descriptor.resourceEpoch;
  if(std::memcmp(&opened,&descriptor,sizeof(opened))){CHECK(color.Open(b.Get(),descriptor)==TransferResult::Ok);opened=descriptor;depth.Reset();}
  SceneDepthFrame attachment;const bool hasDepth=host.ReadSceneDepth(descriptor,ticket,attachment);CHECK(hasDepth==(step!=0&&step!=3));
  if(hasDepth)CHECK(depth.CopyBeforeColor(attachment,descriptor,ticket));
  CHECK(color.Copy(ticket,{{{target[0].texture.Get(),0},{target[1].texture.Get(),0}}})==TransferResult::Ok);
  if(hasDepth){SceneDepthViews views;CHECK(depth.Views(ticket,views));}
  host.PairConsumed(ticket,step!=3);bridge.PumpGraphics();CHECK(!host.ReadSceneDepth(descriptor,ticket,attachment));
 }
 bridge.CloseGraphics();CHECK(bridge.Published()==5&&bridge.Consumed()==4&&bridge.Discarded()==1);
 std::cout<<"SceneDepth bridge GPU: exact IPC, first frame, owner/frame restart, incomplete depth and discarded-frame recovery passed\n";return 0;
}
int main(){for(unsigned samples:{1u,2u,4u,8u})if(Run(samples))return 1;return RunBridge();}
