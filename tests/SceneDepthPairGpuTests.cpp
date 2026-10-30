// Explicit hardware test: real shared resources and the existing color fences.
// Never registered in CTest; no game or XR session is started.
#include "fvr/graphics/D3D11SceneDepthPair.h"
#include "fvr/graphics/D3D11SharedPair.h"
#include "fvr/graphics/D3D11SceneDepthCompositor.h"
#include "fvr/graphics/D3D11FrameBridge.h"
#include "fvr/graphics/DeferredDepthAdmission.h"
#include "fvr/ipc/RemoteFrameProvider.h"
#include "fvr/xr/HostCopyLease.h"
#include "Test.h"
#include <Windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <array>
#include <iostream>
#include <memory>
#include <sstream>
#include <string_view>
#include <thread>
// Test-only access to force the real local staged gate busy, rather than
// hoping for a timing race or faking a GPU completion.
namespace fvr::ipc {struct StagedFrameProducerTestAccess {
 static bool Lock(StagedFrameProducer& p){return !p.gate_.test_and_set(std::memory_order_acquire);}
 static void Unlock(StagedFrameProducer& p){p.gate_.clear(std::memory_order_release);}
};}
namespace fvr::graphics {struct D3D11FrameBridgeTestAccess {
 static ipc::StagedFrameProducer& Channel(D3D11FrameBridge& b){return b.channel_;}
 static bool Prepared(const D3D11FrameBridge& b,const ipc::FrameLease& l){return b.PreparedGraphicsMatches(l);}
 static void ReleaseCapture(D3D11FrameBridge& b,unsigned slot){b.TryReleasePipelineCapture(slot);}
 static bool CaptureReleased(const D3D11FrameBridge& b,unsigned slot){return b.pipeline_[slot].captureReleased;}
};}
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
bool CompleteCopy(D3D11PairConsumer& consumer,const PairTicket& ticket){
 const auto end=GetTickCount64()+2000;
 do{const auto result=consumer.PollCopy(ticket);if(result==TransferResult::Ok)return !consumer.HasPendingCopy()&&consumer.LastError()==S_OK;
  if(result!=TransferResult::Busy||!consumer.HasPendingCopy()||consumer.LastError()!=S_FALSE)return false;Sleep(1);
 }while(GetTickCount64()<end);return false;
}
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
 const auto attachmentBefore=attachment;const auto ticketBefore=ticket;const auto descriptorBefore=descriptor;
 const auto inputsIntact=[&]{return std::memcmp(&attachment,&attachmentBefore,sizeof(attachment))==0&&
     std::memcmp(&ticket,&ticketBefore,sizeof(ticket))==0&&std::memcmp(&descriptor,&descriptorBefore,sizeof(descriptor))==0;};
 CHECK(reader.CopyBeforeColor(attachment,descriptor,ticket));CHECK(inputsIntact());
 CHECK(!reader.CopyBeforeColor(attachment,descriptor,ticket));CHECK(inputsIntact());
 CHECK(colorReader.SubmitCopy(ticket,destination)==TransferResult::Busy);CHECK(colorReader.HasPendingCopy());CHECK(inputsIntact());
 CHECK(CompleteCopy(colorReader,ticket));CHECK(inputsIntact());
 SceneDepthViews views;CHECK(reader.Views(ticket,views));
 CHECK(!reader.Views(wrong,views));CHECK(inputsIntact());
 CHECK(reader.Views(ticket,views));CHECK(inputsIntact());
 std::array<ComPtr<ID3D11ShaderResourceView>,4> retainedViews;
 for(unsigned i=0;i<4;++i)retainedViews[i]=views.eyes[i/2][i%2];
 CHECK(reader.ImportStatistics().sourceOpens==4&&reader.ImportStatistics().localCreates==4);
 depth.Acknowledge(ticket,true);color.Acknowledge(ticket,true);
 // Overwrite and fence producer resources for the NEXT frame before drawing
 // with the previous local depth. The old local snapshots must remain stable.
 ++frame.frameId;++frame.trackingGeneration;CHECK(capture(false));PairTicket second;CHECK(Publish(color,source,frame,second));SceneDepthFrame attachment2;CHECK(depth.Seal(descriptor,second,false,attachment2));
 CHECK(attachment2.resourceGeneration==firstGeneration&&attachment2.resources==attachment.resources);
 const auto attachment2Before=attachment2;const auto secondBefore=second;
 for(unsigned eye=0;eye<2;++eye){CHECK(compositor.Compose({copied[eye].view.Get(),props.view.Get(),propDepthView.Get(),views.eyes[eye][0],views.eyes[eye][1],output[eye].target.Get()}));
  const auto p=Pixel(b.Get(),bc.Get(),output[eye]);CHECK(p[0]==(eye?255:0)&&p[2]==(eye?0:255));}
 CHECK(reader.CopyBeforeColor(attachment2,descriptor,second));CHECK(colorReader.Copy(second,destination)==TransferResult::Ok);CHECK(!reader.Views(ticket,views));CHECK(reader.Views(second,views));
 CHECK(std::memcmp(&attachment2,&attachment2Before,sizeof(attachment2))==0&&std::memcmp(&second,&secondBefore,sizeof(second))==0&&inputsIntact());
 for(unsigned eye=0;eye<2;++eye){CHECK(compositor.Compose({copied[eye].view.Get(),props.view.Get(),propDepthView.Get(),views.eyes[eye][0],views.eyes[eye][1],output[eye].target.Get()}));CHECK(Pixel(b.Get(),bc.Get(),output[eye])[0]==255);}
 // Failure feedback retires the source set. A new epoch must not reuse the
 // previously shared allocation while a consumer can still hold it open.
 depth.Acknowledge(second,false);color.Acknowledge(second,false);CHECK(color.Create(a.Get(),32,32,DXGI_FORMAT_R8G8B8A8_UNORM,2,TextureSharing::LegacyFenced)==TransferResult::Ok);
 descriptor=color.Descriptor();CHECK(colorReader.Open(b.Get(),descriptor)==TransferResult::Ok);++frame.frameId;++frame.trackingGeneration;CHECK(capture(true));PairTicket third;CHECK(Publish(color,source,frame,third));SceneDepthFrame attachment3;CHECK(depth.Seal(descriptor,third,false,attachment3));
 reader.ResetSources();CHECK(!reader.Views(second,views));
 // A failed import after the epoch change must expose no previous ticket or
 // views, yet must keep the local allocation available for a verified retry.
 D3D11PairProducer wrongShape;CHECK(wrongShape.Create(a.Get(),32,32,DXGI_FORMAT_R8G8B8A8_UNORM,99,TextureSharing::LegacyFenced)==TransferResult::Ok);
 auto malformed=attachment3;malformed.resources[0][0]=LegacyResourceIds(wrongShape.Descriptor())[0];
 CHECK(!reader.CopyBeforeColor(malformed,descriptor,third));
 CHECK(!reader.Views(second,views)&&!reader.Views(third,views));
 CHECK(reader.ImportStatistics().localCreates==4);
 auto foreignDescriptor=descriptor;auto foreignDepth=attachment3;++foreignDescriptor.adapterLow;++foreignDepth.adapterLow;
 CHECK(!reader.CopyBeforeColor(foreignDepth,foreignDescriptor,third));CHECK(!reader.Views(third,views));
 CHECK(!reader.Initialize(a.Get())); // A consumer's device cannot be rebound.
 CHECK(attachment3.resourceGeneration>firstGeneration&&attachment3.resources!=attachment2.resources);CHECK(reader.CopyBeforeColor(attachment3,descriptor,third));CHECK(colorReader.Copy(third,destination)==TransferResult::Ok);
 CHECK(reader.ImportStatistics().sourceOpens==9&&reader.ImportStatistics().localCreates==4);
 CHECK(reader.Views(third,views));for(unsigned i=0;i<4;++i)CHECK(views.eyes[i/2][i%2]==retainedViews[i].Get());
 CHECK(reader.Views(third,views));CHECK(compositor.Compose({copied[0].view.Get(),props.view.Get(),propDepthView.Get(),views.eyes[0][0],views.eyes[0][1],output[0].target.Get()}));CHECK(Pixel(b.Get(),bc.Get(),output[0])[2]==255);
 depth.Acknowledge(third,true);color.Acknowledge(third,true);
 // A scene load restarts its native frame number without reopening the channel.
 CHECK(color.Create(a.Get(),32,32,DXGI_FORMAT_R8G8B8A8_UNORM,3,TextureSharing::LegacyFenced)==TransferResult::Ok);
 descriptor=color.Descriptor();CHECK(colorReader.Open(b.Get(),descriptor)==TransferResult::Ok);
 frame.frameId=1;++frame.trackingGeneration;CHECK(capture(false));PairTicket restarted;
 CHECK(Publish(color,source,frame,restarted));SceneDepthFrame afterScene;CHECK(depth.Seal(descriptor,restarted,false,afterScene));
 reader.ResetSources();CHECK(!reader.Views(third,views));
 CHECK(reader.CopyBeforeColor(afterScene,descriptor,restarted));CHECK(colorReader.Copy(restarted,destination)==TransferResult::Ok);
 CHECK(reader.Views(restarted,views));for(unsigned i=0;i<4;++i)CHECK(views.eyes[i/2][i%2]==retainedViews[i].Get());
 CHECK(reader.ImportStatistics().sourceOpens==13&&reader.ImportStatistics().localCreates==4);
 for(unsigned eye=0;eye<2;++eye){CHECK(compositor.Compose({copied[eye].view.Get(),props.view.Get(),propDepthView.Get(),views.eyes[eye][0],views.eyes[eye][1],output[eye].target.Get()}));CHECK(Pixel(b.Get(),bc.Get(),output[eye])[0]==255);}
 depth.Acknowledge(restarted,true);color.Acknowledge(restarted,true);
 reader.Reset();CHECK(!reader.Views(restarted,views));
 ++frame.frameId;++frame.trackingGeneration;CHECK(capture(true));PairTicket resetTicket;
 CHECK(Publish(color,source,frame,resetTicket));SceneDepthFrame resetDepth;CHECK(depth.Seal(descriptor,resetTicket,false,resetDepth));
 CHECK(reader.CopyBeforeColor(resetDepth,descriptor,resetTicket));CHECK(colorReader.Copy(resetTicket,destination)==TransferResult::Ok);
 CHECK(reader.Views(resetTicket,views));for(unsigned i=0;i<4;++i)CHECK(views.eyes[i/2][i%2]!=retainedViews[i].Get());
 CHECK(reader.ImportStatistics().sourceOpens==17&&reader.ImportStatistics().localCreates==8);
 depth.Acknowledge(resetTicket,true);color.Acknowledge(resetTicket,true);depth.Close();
 std::cout<<"SceneDepthPair GPU samples="<<samples<<": exact cached views and fresh pixels across source epochs, failed import, device gate, full reset passed (4 local creates across 3 epochs; 8 after full reset)\n";return 0;
}
}
int RunAsyncCopy(){
 ComPtr<ID3D11Device> a,b;ComPtr<ID3D11DeviceContext> ac,bc;D3D_FEATURE_LEVEL fl;
 CHECK(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&a,&fl,&ac)));
 CHECK(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&b,&fl,&bc)));
 D3D11PairProducer producer;D3D11PairConsumer consumer;
 CHECK(producer.Create(a.Get(),32,32,DXGI_FORMAT_R8G8B8A8_UNORM,1,TextureSharing::LegacyFenced)==TransferResult::Ok);
 auto descriptor=producer.Descriptor();CHECK(consumer.Open(b.Get(),descriptor)==TransferResult::Ok);
 std::array<Color,2> original,copied,other;
 for(unsigned eye=0;eye<2;++eye)CHECK(MakeColor(a.Get(),original[eye])&&MakeColor(b.Get(),copied[eye])&&MakeColor(b.Get(),other[eye]));
 const float red[]{1,0,0,1},blue[]{0,0,1,1},green[]{0,1,0,1};
 ac->ClearRenderTargetView(original[0].target.Get(),red);ac->ClearRenderTargetView(original[1].target.Get(),blue);
 const std::array<TextureSlice,2> source{{{original[0].texture.Get(),0},{original[1].texture.Get(),0}}};
 const std::array<TextureSlice,2> destination{{{copied[0].texture.Get(),0},{copied[1].texture.Get(),0}}};
 const std::array<TextureSlice,2> otherDestination{{{other[0].texture.Get(),0},{other[1].texture.Get(),0}}};
 FrameMetadata frame{1,1,1,1000000000};PairTicket ticket;
 CHECK(!producer.ExportReadsQueued(a.Get(),ac.Get(),frame));
 const auto firstExport=producer.Publish(source,frame,ticket);
 CHECK(firstExport==TransferResult::Busy||firstExport==TransferResult::Ok);
 CHECK(producer.ExportReadsQueued(a.Get(),ac.Get(),frame));
 CHECK(!producer.ExportReadsQueued(b.Get(),ac.Get(),frame)&&!producer.ExportReadsQueued(a.Get(),bc.Get(),frame));
 CHECK(!producer.ExportReadsQueued(nullptr,ac.Get(),frame)&&!producer.ExportReadsQueued(a.Get(),nullptr,frame));
 for(unsigned field=0;field<4;++field){auto wrong=frame;
  switch(field){case 0:++wrong.frameId;break;case 1:++wrong.spaceGeneration;break;case 2:++wrong.trackingGeneration;break;default:++wrong.predictedNs;break;}
  CHECK(!producer.ExportReadsQueued(a.Get(),ac.Get(),wrong));
 }
 // B overwrites the shared capture inputs immediately after A's real reads
 // were queued. No CPU fence/poll occurs between those ordered GPU commands.
 // The first query may already be complete; that does not change the proof.
 for(auto& eye:original)ac->ClearRenderTargetView(eye.target.Get(),green);
 if(firstExport==TransferResult::Busy)CHECK(Publish(producer,source,frame,ticket));
 CHECK(producer.ExportReadsQueued(a.Get(),ac.Get(),frame));const auto originalTicket=ticket;
 CHECK(consumer.SubmitCopy(ticket,destination)==TransferResult::Busy&&consumer.HasPendingCopy()&&consumer.LastError()==S_FALSE);
 // Delayed host polling does not retire a valid pair after the old 20ms wait.
 // This tests lifetime across frames; it does not pretend to time GPU execution.
 Sleep(35);CHECK(consumer.HasPendingCopy());
 CHECK(consumer.SubmitCopy(ticket,destination)==TransferResult::Busy&&consumer.HasPendingCopy());
 CHECK(consumer.SubmitCopy(ticket,otherDestination)==TransferResult::Invalid&&consumer.HasPendingCopy());
 CHECK(consumer.Copy(ticket,destination)==TransferResult::Busy&&consumer.HasPendingCopy());
 CHECK(consumer.Open(b.Get(),descriptor)==TransferResult::Busy&&consumer.HasPendingCopy());
 // Every identity component is immutable while the copy owns the ticket.
 for(unsigned field=0;field<12;++field){auto wrong=ticket;
  switch(field){case 0:++wrong.magic;break;case 1:++wrong.version;break;case 2:++wrong.bytes;break;case 3:++wrong.reserved0;break;
   case 4:++wrong.resourceEpoch;break;case 5:++wrong.sequence;break;case 6:++wrong.frameId;break;case 7:++wrong.spaceGeneration;break;
   case 8:++wrong.trackingGeneration;break;case 9:++wrong.predictedNs;break;case 10:++wrong.session[0];break;default:++wrong.reserved[1];break;}
  CHECK(consumer.PollCopy(wrong)==TransferResult::Invalid&&consumer.HasPendingCopy());
  CHECK(consumer.CancelCopy(wrong)==TransferResult::Invalid&&consumer.HasPendingCopy());
 }
 auto next=frame;++next.frameId;++next.trackingGeneration;PairTicket blocked;
 CHECK(!producer.ExportReadsQueued(a.Get(),ac.Get(),next));
 CHECK(producer.Publish(source,next,blocked)==TransferResult::Busy);
 CHECK(CompleteCopy(consumer,ticket));CHECK(consumer.PollCopy(ticket)==TransferResult::Invalid);
 CHECK(consumer.SubmitCopy(ticket,destination)==TransferResult::Invalid);
 CHECK(std::memcmp(&ticket,&originalTicket,sizeof(ticket))==0);
 CHECK(Pixel(b.Get(),bc.Get(),copied[0])[0]==255&&Pixel(b.Get(),bc.Get(),copied[1])[2]==255);
 // GPU completion is distinct from the caller's exact feedback.
 CHECK(producer.Publish(source,next,blocked)==TransferResult::Busy);
 auto wrongAck=ticket;++wrongAck.sequence;producer.Acknowledge(wrongAck,true);
 CHECK(producer.ExportReadsQueued(a.Get(),ac.Get(),frame));
 producer.Acknowledge(ticket,true);CHECK(!producer.ExportReadsQueued(a.Get(),ac.Get(),frame));
 ac->ClearRenderTargetView(original[0].target.Get(),red);ac->ClearRenderTargetView(original[1].target.Get(),blue);
 frame=next;CHECK(Publish(producer,source,frame,ticket));
 CHECK(consumer.SubmitCopy(ticket,destination)==TransferResult::Busy&&consumer.HasPendingCopy());
 // Put a later marker after the queued copy. An accidental repeated Submit
 // would overwrite this marker; a true idempotent submission leaves it intact.
 for(auto& eye:copied)bc->ClearRenderTargetView(eye.target.Get(),green);
 CHECK(consumer.SubmitCopy(ticket,destination)==TransferResult::Busy&&consumer.HasPendingCopy());
 CHECK(CompleteCopy(consumer,ticket));
 for(auto& eye:copied)CHECK(Pixel(b.Get(),bc.Get(),eye)[1]==255);
 producer.Acknowledge(ticket,true);
 ++frame.frameId;++frame.trackingGeneration;CHECK(Publish(producer,source,frame,ticket));
 CHECK(consumer.SubmitCopy(ticket,destination)==TransferResult::Busy&&consumer.HasPendingCopy());
 CHECK(consumer.CancelCopy(ticket)==TransferResult::Ok&&!consumer.HasPendingCopy()&&consumer.LastError()==E_ABORT);
 CHECK(consumer.SubmitCopy(ticket,destination)==TransferResult::Faulted);CHECK(consumer.CancelCopy(ticket)==TransferResult::Invalid);
 producer.Acknowledge(ticket,false);
 CHECK(!producer.ExportReadsQueued(a.Get(),ac.Get(),frame));
 CHECK(producer.Create(a.Get(),32,32,DXGI_FORMAT_R8G8B8A8_UNORM,2,TextureSharing::LegacyFenced)==TransferResult::Ok);
 descriptor=producer.Descriptor();CHECK(consumer.Open(b.Get(),descriptor)==TransferResult::Ok);
 ++frame.frameId;++frame.trackingGeneration;CHECK(Publish(producer,source,frame,ticket));
 CHECK(consumer.SubmitCopy(ticket,destination)==TransferResult::Busy&&consumer.HasPendingCopy());CHECK(CompleteCopy(consumer,ticket));
 CHECK(Pixel(b.Get(),bc.Get(),copied[0])[0]==255&&Pixel(b.Get(),bc.Get(),copied[1])[2]==255);producer.Acknowledge(ticket,true);
 producer.Reset();CHECK(!producer.ExportReadsQueued(a.Get(),ac.Get(),frame));
 std::cout<<"Async shared color GPU: exact queued-read identity, ordered capture overwrite, delayed poll, duplicate submission, ACK backpressure and cancellation recovery passed\n";
 return 0;
}
int RunBridge(){using namespace fvr;ComPtr<ID3D11Device> a,b;ComPtr<ID3D11DeviceContext> ac,bc;D3D_FEATURE_LEVEL fl;
 CHECK(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&a,&fl,&ac)));
 CHECK(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&b,&fl,&bc)));
 ComPtr<IDXGIDevice> dx;ComPtr<IDXGIAdapter> adapter;DXGI_ADAPTER_DESC gpu{};CHECK(SUCCEEDED(a.As(&dx))&&SUCCEEDED(dx->GetAdapter(&adapter))&&SUCCEEDED(adapter->GetDesc(&gpu)));
 ipc::RemoteFrameProvider host;CHECK(host.Create());auto bridgeStorage=std::make_unique<D3D11FrameBridge>();auto& bridge=*bridgeStorage;CHECK(bridge.ConnectGraphics(a.Get(),host.Token(),FrameSharingPreference::LegacyIpc));
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
 // Native resize can happen before either eye. Explicit pair-discard policy
 // must leave allocations/acks intact and allow the ORIGINAL size to resume.
 D3D11_TEXTURE2D_DESC changed{};source.texture->GetDesc(&changed);changed.Width=16;
 ComPtr<ID3D11Texture2D> resized;CHECK(SUCCEEDED(a->CreateTexture2D(&changed,nullptr,&resized)));
 const float red[]{1,0,0,1};ac->ClearRenderTargetView(source.target.Get(),red);
 std::uint64_t nextFrame=5,nextTracking=6;
 for(unsigned rejectedEye:{0u,1u}){
  input.generation=nextTracking++;input.predictedNs+=10000000;
  runtime::TrackingFrame rendered;TextureDescriptor descriptor;PairTicket ticket;ipc::FrameLease rejected;
  CHECK(!host.TryGetCompletedPair(req,input,rendered,descriptor,ticket));
  CHECK(bridge.TryBegin({2,nextFrame++,1},rejected));
  if(rejectedEye)CHECK(bridge.CaptureEye(0,source.texture.Get(),rejected,SourceDimensionPolicy::DiscardPair));
  const auto captured=bridge.CapturedEyes(),published=bridge.Published(),consumed=bridge.Consumed();
  CHECK(!bridge.CaptureEye(rejectedEye,resized.Get(),rejected,SourceDimensionPolicy::DiscardPair));
  CHECK(bridge.CapturedEyes()==captured&&bridge.Published()==published&&bridge.Consumed()==consumed);
  CHECK(bridge.FailureStage()==0&&!bridge.Fatal());
  bridge.Cancel(rejected);bridge.PumpGraphics();
  CHECK(!host.TryGetCompletedPair(req,input,rendered,descriptor,ticket)); // skipped, never half-published
  input.generation=nextTracking++;input.predictedNs+=10000000;
  ipc::FrameLease resumed;CHECK(bridge.TryBegin({2,nextFrame++,1},resumed));
  CHECK(resumed.requirements.width==req.width&&resumed.requirements.height==req.height);
  for(unsigned eye=0;eye<2;++eye)CHECK(bridge.CaptureEye(eye,source.texture.Get(),resumed,SourceDimensionPolicy::DiscardPair));
  CHECK(bridge.PublishRestored(resumed));const auto deadline=GetTickCount64()+2000;bool delivered=false;
  do{bridge.PumpGraphics();delivered=host.TryGetCompletedPair(req,input,rendered,descriptor,ticket);if(!delivered)Sleep(1);}while(!delivered&&GetTickCount64()<deadline);
  CHECK(delivered&&std::memcmp(&opened,&descriptor,sizeof(opened))==0); // same resource identity and epoch
  ipc::FrameLease blocked;CHECK(!bridge.TryBegin({2,nextFrame,1},blocked)); // host still owns the old slot
  bridge.PumpGraphics();CHECK(bridge.Consumed()==consumed&&!bridge.TryBegin({2,nextFrame,1},blocked));
  CHECK(color.Copy(ticket,{{{target[0].texture.Get(),0},{target[1].texture.Get(),0}}})==TransferResult::Ok);
  for(const auto& eye:target){const auto p=Pixel(b.Get(),bc.Get(),eye);CHECK(p[0]==255&&p[2]==0);}
  host.PairConsumed(ticket,true);bridge.PumpGraphics();CHECK(bridge.Consumed()==consumed+1);
 }
 // The default remains fail-closed for other callers, without an unrelated
 // handled texture-sharing fallback operation contaminating the diagnosis.
 CHECK(bridge.FailureStage()==0&&bridge.FailureOperation()==0);
 input.generation=nextTracking++;input.predictedNs+=10000000;
 runtime::TrackingFrame finalRendered;TextureDescriptor finalDescriptor;PairTicket finalTicket;
 CHECK(!host.TryGetCompletedPair(req,input,finalRendered,finalDescriptor,finalTicket));ipc::FrameLease resizedLease;
 CHECK(bridge.TryBegin({2,nextFrame++,1},resizedLease));
 const auto captured=bridge.CapturedEyes();CHECK(!bridge.CaptureEye(0,resized.Get(),resizedLease));
 CHECK(bridge.CapturedEyes()==captured&&bridge.FailureStage()==2&&bridge.FailureCode()==E_INVALIDARG&&bridge.FailureOperation()==0&&bridge.Fatal());
 bridge.Cancel(resizedLease);bridge.PumpGraphics();
 CHECK(!bridge.CaptureEye(2,source.texture.Get(),resizedLease));
 CHECK(bridge.FailureStage()==2&&bridge.FailureCode()==E_INVALIDARG&&bridge.FailureOperation()==0);
 bridge.CloseGraphics();CHECK(bridge.Published()==7&&bridge.Consumed()==6&&bridge.Discarded()==1);
 // Opting into transient dimensions never permits a wrong device, format or
 // layout. Combine each violation with wrong width to prove strict precedence.
 for(unsigned violation=0;violation<5;++violation){
  ipc::RemoteFrameProvider invalidHost;CHECK(invalidHost.Create());auto invalidStorage=std::make_unique<D3D11FrameBridge>();auto& invalidBridge=*invalidStorage;
  CHECK(invalidBridge.ConnectGraphics(a.Get(),invalidHost.Token(),FrameSharingPreference::LegacyIpc));
  input.generation=nextTracking++;input.predictedNs+=10000000;
  CHECK(!invalidHost.TryGetCompletedPair(req,input,finalRendered,finalDescriptor,finalTicket));ipc::FrameLease invalidLease;
  CHECK(invalidBridge.TryBegin({2,1,1},invalidLease));
  auto desc=changed;ID3D11Device* owner=a.Get();
  if(violation==0)owner=b.Get();
  if(violation==1)desc.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;
  if(violation==2)desc.MipLevels=2;
  if(violation==3)desc.ArraySize=2;
  if(violation==4)desc.SampleDesc.Count=2;
  ComPtr<ID3D11Texture2D> incompatible;CHECK(SUCCEEDED(owner->CreateTexture2D(&desc,nullptr,&incompatible)));
  CHECK(!invalidBridge.CaptureEye(0,incompatible.Get(),invalidLease,SourceDimensionPolicy::DiscardPair));
  CHECK(invalidBridge.Fatal()&&invalidBridge.FailureStage()==2&&invalidBridge.FailureCode()==E_INVALIDARG&&invalidBridge.CapturedEyes()==0);
  invalidBridge.Cancel(invalidLease);invalidBridge.CloseGraphics();
 }
 std::cout<<"SceneDepth bridge GPU: exact IPC, owner/frame restart, discarded-frame and transient-size recovery, strict incompatible-source rejection passed\n";return 0;
}
int RunBridgeMirror(FrameSharingPreference preference,unsigned expectedMode){using namespace fvr;
 // Exercise the public bridge presentation path without exposing mutable mirror
 // state. This window is never shown, activated or presented to the desktop.
 struct HiddenWindow {HWND value=nullptr;~HiddenWindow(){if(value)DestroyWindow(value);}} window;
 window.value=CreateWindowExW(WS_EX_NOACTIVATE,L"STATIC",L"Fvr bridge mirror GPU test",WS_POPUP,0,0,32,32,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
 CHECK(window.value&&!IsWindowVisible(window.value));
 ComPtr<ID3D11Device> a,b;ComPtr<ID3D11DeviceContext> ac,bc;D3D_FEATURE_LEVEL fl;
 CHECK(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&a,&fl,&ac)));
 CHECK(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&b,&fl,&bc)));
 ComPtr<IDXGIDevice> dx;ComPtr<IDXGIAdapter> adapter;ComPtr<IDXGIFactory> factory;DXGI_ADAPTER_DESC gpu{};
 CHECK(SUCCEEDED(a.As(&dx))&&SUCCEEDED(dx->GetAdapter(&adapter))&&SUCCEEDED(adapter->GetDesc(&gpu))&&SUCCEEDED(adapter->GetParent(IID_PPV_ARGS(&factory))));
 DXGI_SWAP_CHAIN_DESC swap{};swap.BufferDesc.Width=swap.BufferDesc.Height=32;swap.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
 swap.SampleDesc.Count=1;swap.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;swap.BufferCount=1;swap.OutputWindow=window.value;swap.Windowed=TRUE;swap.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;
 ComPtr<IDXGISwapChain> chain;CHECK(SUCCEEDED(factory->CreateSwapChain(a.Get(),&swap,&chain))&&!IsWindowVisible(window.value));
 Color desktop;CHECK(SUCCEEDED(chain->GetBuffer(0,IID_PPV_ARGS(&desktop.texture))));
 std::array<Color,2> native,copied;for(unsigned eye=0;eye<2;++eye)CHECK(MakeColor(a.Get(),native[eye])&&MakeColor(b.Get(),copied[eye]));
 ipc::RemoteFrameProvider host;CHECK(host.Create());auto bridgeStorage=std::make_unique<D3D11FrameBridge>();auto& bridge=*bridgeStorage;CHECK(bridge.ConnectGraphics(a.Get(),host.Token(),preference));
 runtime::PresentationRequirements req{32,32,28,gpu.AdapterLuid.LowPart,gpu.AdapterLuid.HighPart};runtime::TrackingFrame input{};
 input.spaceGeneration=1;input.focused=input.headValid=true;input.fov={math::FovTangents{-1,1,1,-1},math::FovTangents{-1,1,1,-1}};
 const std::array<std::array<float,4>,3> colors{{{{1,0,0,1}},{{0,1,0,1}},{{0,0,1,1}}}};
 const std::array<std::array<unsigned char,4>,3> pixels{{{{255,0,0,255}},{{0,255,0,255}},{{0,0,255,255}}}};
 const float overwritten[]{1,0,1,1};D3D11PairConsumer consumer;TextureDescriptor opened{};std::uint64_t discardedEpoch=0;
 const auto mirrorMatches=[&](unsigned color){return bridge.PresentDesktop(chain.Get(),true,true,false,false)&&Pixel(a.Get(),ac.Get(),desktop)==pixels[color];};
 for(unsigned step=0;step<3;++step){
  input.generation=step+1;input.predictedNs=1000000000+step*10000000;
  runtime::TrackingFrame rendered;TextureDescriptor descriptor;PairTicket ticket;
  const auto requestTick=GetTickCount64();
  CHECK(!host.TryGetCompletedPair(req,input,rendered,descriptor,ticket));ipc::FrameLease lease;bool began=false;
  const auto beginDeadline=GetTickCount64()+2000;
  do{bridge.PumpGraphics();began=bridge.TryBegin({1,step+1u,1},lease);if(!began)Sleep(1);}while(!began&&GetTickCount64()<beginDeadline);
  const auto beginTick=GetTickCount64();
  CHECK(began);const unsigned right=(step+2)%3;
  ac->ClearRenderTargetView(native[0].target.Get(),colors[step].data());ac->ClearRenderTargetView(native[1].target.Get(),colors[right].data());
  CHECK(bridge.CaptureEye(0,native[0].texture.Get(),lease));CHECK(bridge.SharingMode()==expectedMode);
  const auto leftTick=GetTickCount64();
  // The next capture overwrites the bridge's local left source, but an
  // unfinished pair must not replace the previous completed desktop image.
  if(step)CHECK(mirrorMatches(step-1));
  const auto mirrorTick=GetTickCount64();
  CHECK(bridge.CaptureEye(1,native[1].texture.Get(),lease));CHECK(bridge.PublishRestored(lease));
  const auto publishTick=GetTickCount64();
  CHECK(bridge.DesktopMirror().Committed()==step+1);
  // Queue native reuse immediately after publication, before any GPU readback.
  for(auto& eye:native)ac->ClearRenderTargetView(eye.target.Get(),overwritten);
  const auto deliveryDeadline=GetTickCount64()+2000;bool delivered=false;
  do{bridge.PumpGraphics();delivered=host.TryGetCompletedPair(req,input,rendered,descriptor,ticket);if(!delivered)Sleep(1);}while(!delivered&&GetTickCount64()<deliveryDeadline);
  if(!delivered||ticket.frameId!=step+1){
   // Failure-only observations: preserve the original request lifetime, all
   // scheduling/copy steps and assertion. Tick spans are CPU observations.
   const auto& stats=host.Statistics();
   std::cerr<<"Bridge mirror delivery failure {\"expected_mode\":"<<expectedMode<<",\"step\":"<<step
    <<",\"delivered\":"<<(delivered?"true":"false")<<",\"request_lifetime_ms\":"<<host.RequestLifetimeMs()
    <<",\"ticks_ms\":{\"request\":"<<requestTick<<",\"begin\":"<<beginTick<<",\"left_queued\":"<<leftTick
    <<",\"previous_mirror_read\":"<<mirrorTick<<",\"published_call\":"<<publishTick<<",\"poll_end\":"<<GetTickCount64()<<'}'
    <<",\"lease\":{\"request\":"<<lease.requestId<<",\"frame\":"<<lease.native.frameId
    <<",\"tracking\":"<<lease.tracking.generation<<",\"space\":"<<lease.tracking.spaceGeneration<<",\"predicted_ns\":"<<lease.tracking.predictedNs<<'}'
    <<",\"input\":{\"tracking\":"<<input.generation<<",\"space\":"<<input.spaceGeneration<<",\"predicted_ns\":"<<input.predictedNs<<'}'
    <<",\"ticket\":{\"frame\":"<<ticket.frameId<<",\"tracking\":"<<ticket.trackingGeneration
    <<",\"space\":"<<ticket.spaceGeneration<<",\"predicted_ns\":"<<ticket.predictedNs<<",\"sequence\":"<<ticket.sequence<<'}'
    <<",\"bridge\":{\"mode\":"<<bridge.SharingMode()<<",\"sources\":"<<bridge.SourceSlotCount()
    <<",\"failure_stage\":"<<bridge.FailureStage()<<",\"failure_code\":"<<bridge.FailureCode()
    <<",\"published\":"<<bridge.Published()<<",\"consumed\":"<<bridge.Consumed()<<",\"discarded\":"<<bridge.Discarded()
    <<",\"device_removed_reason\":"<<a->GetDeviceRemovedReason()<<'}'
    <<",\"host\":{\"requested\":"<<stats.requested<<",\"completed\":"<<stats.completed<<",\"timeouts\":"<<stats.timeouts
    <<",\"first_timeout\":";stats.firstTimeout.Write(std::cerr);std::cerr<<"}}\n";
  }
  CHECK(delivered&&ticket.frameId==step+1);if(step==2)CHECK(descriptor.resourceEpoch>discardedEpoch);
  if(std::memcmp(&opened,&descriptor,sizeof(opened))){CHECK(consumer.Open(b.Get(),descriptor)==TransferResult::Ok);opened=descriptor;}
  const std::array<TextureSlice,2> destinations{{{copied[0].texture.Get(),0},{copied[1].texture.Get(),0}}};
  const auto copyDeadline=GetTickCount64()+2000;TransferResult result;
  do{result=consumer.HasPendingCopy()?consumer.PollCopy(ticket):consumer.SubmitCopy(ticket,destinations);if(result==TransferResult::Busy)Sleep(1);}while(result==TransferResult::Busy&&GetTickCount64()<copyDeadline);
  CHECK(result==TransferResult::Ok&&!consumer.HasPendingCopy());
  CHECK(Pixel(b.Get(),bc.Get(),copied[0])==pixels[step]&&Pixel(b.Get(),bc.Get(),copied[1])==pixels[right]);
  CHECK(mirrorMatches(step));ipc::FrameLease blocked;CHECK(!bridge.TryBegin({1,step+2u,1},blocked));
  const bool accept=step!=1;if(!accept)discardedEpoch=descriptor.resourceEpoch;
  host.PairConsumed(ticket,accept);const auto feedbackDeadline=GetTickCount64()+2000;
  do{host.FlushFeedback();bridge.PumpGraphics();if(bridge.Consumed()+bridge.Discarded()!=step+1)Sleep(1);}while(bridge.Consumed()+bridge.Discarded()!=step+1&&GetTickCount64()<feedbackDeadline);
  CHECK(bridge.Consumed()+bridge.Discarded()==step+1);
  // Discard retires the shared export, not the completed local mirror. Both
  // positive and negative feedback must leave the same left image available.
  CHECK(mirrorMatches(step)&&bridge.DesktopMirror().Failed()==0&&bridge.FailureStage()==0);
 }
 CHECK(bridge.Published()==3&&bridge.Consumed()==2&&bridge.Discarded()==1&&!IsWindowVisible(window.value));
 host.Suspend();bridge.CloseGraphics();host.Close();
 std::cout<<"Bridge desktop mirror GPU mode="<<expectedMode<<": distinct stereo pixels, native overwrite, ACK/discard retention and next-pair recovery passed\n";
 return 0;
}
int RunEarlyAdmission(){using namespace fvr;
 // The environment opt-in is process-local, used only at Connect, and restored
 // before exercising either thread. No game, runtime or global settings change.
 const wchar_t* name=L"FVR_PRIVATE_EARLY_FRAME_ADMISSION";wchar_t saved[64]{};
 const auto length=GetEnvironmentVariableW(name,saved,64);CHECK(length<64);
 CHECK(SetEnvironmentVariableW(name,L"1"));
 struct RestoreEnv {const wchar_t* name;const wchar_t* saved;~RestoreEnv(){SetEnvironmentVariableW(name,saved);}} restore{name,length?saved:nullptr};
 ComPtr<ID3D11Device> a,b;ComPtr<ID3D11DeviceContext> ac,bc;D3D_FEATURE_LEVEL fl;
 CHECK(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&a,&fl,&ac)));
 CHECK(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&b,&fl,&bc)));
 ComPtr<IDXGIDevice> dx;ComPtr<IDXGIAdapter> adapter;DXGI_ADAPTER_DESC gpu{};
 CHECK(SUCCEEDED(a.As(&dx))&&SUCCEEDED(dx->GetAdapter(&adapter))&&SUCCEEDED(adapter->GetDesc(&gpu)));
 ipc::RemoteFrameProvider host;CHECK(host.Create());auto bridgeStorage=std::make_unique<D3D11FrameBridge>();auto& bridge=*bridgeStorage;
 CHECK(bridge.ConnectGraphics(a.Get(),host.Token(),FrameSharingPreference::LegacyIpc));
 CHECK(bridge.EarlyFrameAdmissionEnabled());CHECK(SetEnvironmentVariableW(name,length?saved:nullptr));
 runtime::PresentationRequirements req{32,32,28,gpu.AdapterLuid.LowPart,gpu.AdapterLuid.HighPart};
 runtime::TrackingFrame input{};input.generation=input.spaceGeneration=1;input.predictedNs=1000000000;
 input.focused=input.headValid=true;input.fov={math::FovTangents{-1,1,1,-1},math::FovTangents{-1,1,1,-1}};
 runtime::TrackingFrame rendered;TextureDescriptor descriptor,opened{};PairTicket ticket;
 CHECK(!host.TryGetCompletedPair(req,input,rendered,descriptor,ticket));
 ipc::FrameLease lease;CHECK(bridge.TryBegin({11,1,1},lease));
 Color source;std::array<Color,2> copied;CHECK(MakeColor(a.Get(),source));for(auto& c:copied)CHECK(MakeColor(b.Get(),c));
 D3D11PairConsumer consumer;
 D3D11_TEXTURE2D_DESC dd{};dd.Width=dd.Height=32;dd.MipLevels=dd.ArraySize=dd.SampleDesc.Count=1;
 dd.Format=DXGI_FORMAT_R24G8_TYPELESS;dd.BindFlags=D3D11_BIND_DEPTH_STENCIL;
 ComPtr<ID3D11Texture2D> nativeDepth;CHECK(SUCCEEDED(a->CreateTexture2D(&dd,nullptr,&nativeDepth)));
 for(unsigned step=0;step<3;++step){
  // No explicit graphics pump between early camera admission and this first
  // mutation. BeginDepth must drain the old exact receipt before clearing it.
  const bool depth=bridge.BeginDepthCapture(lease,false);CHECK(depth==(step!=0));
  CHECK(bridge.Consumed()==(step?1u:0u)&&bridge.Discarded()==(step==2?1u:0u));
  const float color[]{step==1?0.f:1.f,step==1?1.f:0.f,0,1};ac->ClearRenderTargetView(source.target.Get(),color);
  {
  auto& staged=D3D11FrameBridgeTestAccess::Channel(bridge);
  CHECK(ipc::StagedFrameProducerTestAccess::Lock(staged));
  struct Unlock {ipc::StagedFrameProducer& staged;~Unlock(){ipc::StagedFrameProducerTestAccess::Unlock(staged);}} unlock{staged};
  // Every depth/color step and restored publication must survive contention
  // after the first exact preparation; no new source ownership is inferred.
  for(unsigned eye=0;eye<2;++eye){
   if(depth)for(unsigned stage=0;stage<2;++stage)CHECK(bridge.CaptureDepth(eye,stage,nativeDepth.Get(),lease));
   CHECK(bridge.CaptureEye(eye,source.texture.Get(),lease));
  }
  CHECK(D3D11FrameBridgeTestAccess::Prepared(bridge,lease));
  // The grant matches the whole original pose and native lease, not just a
  // request number. None of these checks changes the retained valid capture.
  auto changed=lease;++changed.tracking.generation;CHECK(!D3D11FrameBridgeTestAccess::Prepared(bridge,changed));
  changed=lease;changed.tracking.eyes[0].position.x+=.01f;CHECK(!D3D11FrameBridgeTestAccess::Prepared(bridge,changed));
  changed=lease;changed.tracking.fov[1].right+=.01f;CHECK(!D3D11FrameBridgeTestAccess::Prepared(bridge,changed));
  changed=lease;++changed.native.deviceEpoch;CHECK(!D3D11FrameBridgeTestAccess::Prepared(bridge,changed));
  changed=lease;++changed.channelGeneration;CHECK(!D3D11FrameBridgeTestAccess::Prepared(bridge,changed));
  changed=lease;++changed.requirements.width;CHECK(!D3D11FrameBridgeTestAccess::Prepared(bridge,changed));
   RestoredPublicationResult observation{};
   CHECK(!bridge.PublishRestored(changed,&observation)&&observation==RestoredPublicationResult::PrepareBusy);
   bool wrongThreadAccepted=true;RestoredPublicationResult wrongThreadObservation{};
   std::thread wrongThread([&]{wrongThreadAccepted=bridge.PublishRestored(lease,&wrongThreadObservation);});wrongThread.join();
   CHECK(!wrongThreadAccepted&&wrongThreadObservation==RestoredPublicationResult::WrongGraphicsOwner);
   CHECK(bridge.PublishRestored(lease,&observation)&&observation==RestoredPublicationResult::Accepted);
   CHECK(bridge.Published()==step&&bridge.PreparedPublications()==step+1);
   CHECK(bridge.PreparedGraphicsReuses()==(step==0?2u:step==1?8u:14u));
   CHECK(!D3D11FrameBridgeTestAccess::Prepared(bridge,lease));
  }
  ++input.generation;input.predictedNs+=11000000;
  bool delivered=false;const auto limit=GetTickCount64()+2000;
  do{bridge.PumpGraphics();delivered=host.TryGetCompletedPair(req,input,rendered,descriptor,ticket);if(!delivered)Sleep(1);}while(!delivered&&GetTickCount64()<limit);
  CHECK(delivered&&ticket.frameId==step+1&&ticket.trackingGeneration==lease.tracking.generation);
  if(std::memcmp(&opened,&descriptor,sizeof(opened))){CHECK(consumer.Open(b.Get(),descriptor)==TransferResult::Ok);opened=descriptor;}
  CHECK(consumer.SubmitCopy(ticket,{{{copied[0].texture.Get(),0},{copied[1].texture.Get(),0}}})==TransferResult::Busy);
  ipc::FrameLease next;CHECK(!bridge.TryBegin({11,step+2u,1},next)); // No early source release.
  CHECK(CompleteCopy(consumer,ticket));
  for(const auto& image:copied){const auto pixel=Pixel(b.Get(),bc.Get(),image);CHECK(pixel[0]==(step==1?0:255)&&pixel[1]==(step==1?255:0));}
  const auto consumed=bridge.Consumed(),discarded=bridge.Discarded();host.PairConsumed(ticket,step!=1);
  bool admitted=false;std::thread visibility([&]{admitted=bridge.TryBegin({11,step+2u,1},next);});visibility.join();
  CHECK(admitted&&next.tracking.generation==input.generation);
  CHECK(bridge.Consumed()==consumed&&bridge.Discarded()==discarded); // No GPU callback on visibility thread.
  FrameBridgeTimingSnapshot timing;CHECK(bridge.TimingSnapshot(timing));
  CHECK(timing.hasActive&&timing.hasRetiring&&timing.active.key.frame==step+2&&timing.retiring.key.frame==step+1);
  CHECK(timing.retiring.ackObservedQpc>0&&timing.retiring.qpc[unsigned(FrameBridgeTimingStage::Released)]==0);
  lease=next;
 }
 // Cancel the admitted successor before any new depth/color operation. The
 // old consumed receipt must still drain once, without becoming a discard.
 bridge.Cancel(lease);bridge.PumpGraphics();
 CHECK(bridge.Consumed()==2&&bridge.Discarded()==1&&bridge.EarlyAcknowledgements()==3&&!bridge.Fatal());
 FrameBridgeTimingSnapshot done;CHECK(bridge.TimingSnapshot(done)&&!done.hasRetiring&&!done.hasActive);
 CHECK(done.unmatched==0&&done.invalidClock==0&&done.completed==4);
 host.Suspend();bridge.CloseGraphics();
 CHECK(bridge.Consumed()==2&&bridge.Discarded()==1);
 std::cout<<"Early camera admission GPU: exact host ACK, visibility-thread admission, deferred cleanup, discard recovery, pre-capture cancel and all prepared depth/color/publication steps under forced gate contention passed\n";
 return 0;
}
// Real shared resources and IPC. XR-shaped textures stand in for acquired
// runtime images; this is not an xrEndFrame, compositor, or headset claim.
int RunOrderedComposition(unsigned samples,bool reversed){using namespace fvr;
 ComPtr<ID3D11Device> nativeDevice,hostDevice;ComPtr<ID3D11DeviceContext> nativeContext,hostContext;D3D_FEATURE_LEVEL level;
 CHECK(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&nativeDevice,&level,&nativeContext)));
 CHECK(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&hostDevice,&level,&hostContext)));
 D3D11PairProducer producer;D3D11PairConsumer consumer;D3D11SceneDepthProducer nativeDepth;D3D11SceneDepthConsumer localDepth;
 CHECK(producer.Create(nativeDevice.Get(),32,32,DXGI_FORMAT_R8G8B8A8_UNORM,1,TextureSharing::LegacyFenced)==TransferResult::Ok);
 const auto descriptor=producer.Descriptor();CHECK(consumer.Open(hostDevice.Get(),descriptor)==TransferResult::Ok);
 CHECK(nativeDepth.Initialize(nativeDevice.Get())&&localDepth.Initialize(hostDevice.Get()));
 std::array<Color,2> original,scratch,composed;std::array<std::array<Color,2>,3> xrTarget;
 std::array<std::array<ComPtr<ID3D11Texture2D>,2>,3> readback;Color props;
 CHECK(MakeColor(hostDevice.Get(),props));const float red[]{1,0,0,1},green[]{0,1,0,1},blue[]{0,0,1,1};
 hostContext->ClearRenderTargetView(props.target.Get(),green);
 for(unsigned eye=0;eye<2;++eye){
  CHECK(MakeColor(nativeDevice.Get(),original[eye])&&MakeColor(hostDevice.Get(),scratch[eye])&&MakeColor(hostDevice.Get(),composed[eye]));
  for(unsigned step=0;step<3;++step){
   CHECK(MakeColor(hostDevice.Get(),xrTarget[step][eye]));D3D11_TEXTURE2D_DESC desc{};xrTarget[step][eye].texture->GetDesc(&desc);
   desc.BindFlags=0;desc.Usage=D3D11_USAGE_STAGING;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
   CHECK(SUCCEEDED(hostDevice->CreateTexture2D(&desc,nullptr,&readback[step][eye])));
  }
 }
 D3D11_TEXTURE2D_DESC depthDesc{};depthDesc.Width=depthDesc.Height=32;depthDesc.MipLevels=depthDesc.ArraySize=1;
 depthDesc.SampleDesc.Count=samples;depthDesc.Format=DXGI_FORMAT_R24G8_TYPELESS;depthDesc.BindFlags=D3D11_BIND_DEPTH_STENCIL;
 ComPtr<ID3D11Texture2D> depthTexture;ComPtr<ID3D11DepthStencilView> depthTarget;
 CHECK(SUCCEEDED(nativeDevice->CreateTexture2D(&depthDesc,nullptr,&depthTexture)));
 D3D11_DEPTH_STENCIL_VIEW_DESC dsv{};dsv.Format=DXGI_FORMAT_D24_UNORM_S8_UINT;
 dsv.ViewDimension=samples==1?D3D11_DSV_DIMENSION_TEXTURE2D:D3D11_DSV_DIMENSION_TEXTURE2DMS;
 CHECK(SUCCEEDED(nativeDevice->CreateDepthStencilView(depthTexture.Get(),&dsv,&depthTarget)));
 std::array<float,32*32> depths;depths.fill(.5f);depthDesc.SampleDesc.Count=1;depthDesc.Format=DXGI_FORMAT_R32_FLOAT;depthDesc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
 D3D11_SUBRESOURCE_DATA initial{};initial.pSysMem=depths.data();initial.SysMemPitch=32*4;
 ComPtr<ID3D11Texture2D> propDepth;ComPtr<ID3D11ShaderResourceView> propDepthView;
 CHECK(SUCCEEDED(hostDevice->CreateTexture2D(&depthDesc,&initial,&propDepth))&&SUCCEEDED(hostDevice->CreateShaderResourceView(propDepth.Get(),nullptr,&propDepthView)));
 D3D11SceneDepthCompositor compositor;CHECK(compositor.Initialize(hostContext.Get()));
 ipc::FrameChannel hostChannel,nativeChannel;CHECK(hostChannel.CreateHost()&&nativeChannel.ConnectProducer(hostChannel.Token()));
 // This provider counts calls while forwarding the real feedback through IPC.
 struct Feedback final:runtime::IFrameProvider {
  ipc::FrameChannel& channel;unsigned positive=0,negative=0;ipc::ChannelResult last=ipc::ChannelResult::Invalid;
  explicit Feedback(ipc::FrameChannel& c):channel(c){}
  bool TryGetPair(const runtime::PresentationRequirements&,const runtime::TrackingFrame&,TextureDescriptor&,PairTicket&)noexcept override{return false;}
  void PairConsumed(const PairTicket& ticket,bool consumed)noexcept override{if(consumed)++positive;else ++negative;last=channel.Feedback(ticket,consumed);}
  void Suspend()noexcept override{channel.CancelRequest();}
 } feedback(hostChannel);
 // This event is ONLY the test/teardown boundary after target/readback writes.
 // The consumer's earlier source event cannot stand in for this drain.
 struct Drain {
  ID3D11DeviceContext* context=nullptr;ComPtr<ID3D11Query> query;bool armed=false,done=false;
  void Arm(){context->End(query.Get());context->Flush();armed=true;done=false;}
  bool Wait(){if(!armed)return false;const auto deadline=GetTickCount64()+2000;
   do{BOOL ready=FALSE;const auto hr=context->GetData(query.Get(),&ready,sizeof(ready),D3D11_ASYNC_GETDATA_DONOTFLUSH);
    if(hr==S_OK&&ready){done=true;return true;}if(FAILED(hr))return false;Sleep(1);
   }while(GetTickCount64()<deadline);return false;}
  ~Drain(){if(context&&query&&!done){Arm();Wait();}}
 } drain;
 drain.context=hostContext.Get();D3D11_QUERY_DESC queryDesc{D3D11_QUERY_EVENT,0};CHECK(SUCCEEDED(hostDevice->CreateQuery(&queryDesc,&drain.query)));
 runtime::PresentationRequirements requirements{32,32,descriptor.format,descriptor.adapterLow,descriptor.adapterHigh};runtime::TrackingFrame tracking{};
 tracking.generation=tracking.spaceGeneration=1;tracking.predictedNs=1000000000;tracking.focused=tracking.headValid=true;
 tracking.fov={math::FovTangents{-1,1,1,-1},math::FovTangents{-1,1,1,-1}};
 CHECK(hostChannel.BeginRequest(requirements,tracking,200)==ipc::ChannelResult::Ok);
 const std::array<TextureSlice,2> sources{{{original[0].texture.Get(),0},{original[1].texture.Get(),0}}};
 const std::array<TextureSlice,2> destinations{{{scratch[0].texture.Get(),0},{scratch[1].texture.Get(),0}}};
 for(unsigned step=0;step<3;++step){
  ipc::FrameRequest request;CHECK(nativeChannel.TryTake(request)==ipc::ChannelResult::Ok);
  FrameMetadata frame{};frame.frameId=step+1u;frame.spaceGeneration=tracking.spaceGeneration;
  frame.trackingGeneration=tracking.generation;frame.predictedNs=tracking.predictedNs;
  CHECK(frame.spaceGeneration==request.spaceGeneration&&frame.trackingGeneration==request.trackingGeneration&&frame.predictedNs==request.predictedNs);
  CHECK(nativeDepth.Begin(frame));
  nativeContext->ClearRenderTargetView(original[0].target.Get(),step?blue:red);
  nativeContext->ClearRenderTargetView(original[1].target.Get(),step?red:blue);
  for(unsigned eye=0;eye<2;++eye)for(unsigned stage=0;stage<2;++stage){
   const bool occluded=(step==0&&eye==0&&stage==1)||(step==1&&eye==1&&stage==0)||(step==2&&eye==0&&stage==0);
   const float z=(occluded!=reversed)?.25f:.75f;
   nativeContext->ClearDepthStencilView(depthTarget.Get(),D3D11_CLEAR_DEPTH|D3D11_CLEAR_STENCIL,z,0);
   CHECK(nativeDepth.Capture(eye,stage,depthTexture.Get()));
  }
  PairTicket ticket;CHECK(Publish(producer,sources,frame,ticket));SceneDepthFrame attachment;
  CHECK(nativeDepth.Seal(descriptor,ticket,reversed,attachment));CHECK(nativeChannel.Publish(descriptor,ticket,nullptr,&attachment)==ipc::ChannelResult::Ok);
  TextureDescriptor delivered;PairTicket received;CHECK(hostChannel.PollRequest(delivered,received)==ipc::ChannelResult::Ok);
  CHECK(std::memcmp(&ticket,&received,sizeof(ticket))==0);SceneDepthFrame sharedDepth;
  CHECK(hostChannel.ReadSceneDepth(delivered,received,sharedDepth));CHECK(localDepth.CopyBeforeColor(sharedDepth,delivered,received));
  CHECK(consumer.SubmitCopy(received,destinations)==TransferResult::Busy&&consumer.HasPendingCopy());
  auto lease=std::make_unique<xr::HostCopyLease>(feedback,received);
  SceneDepthViews views;CHECK(localDepth.Views(received,views)&&views.reversed==reversed);
  // No PollCopy/GetData/Map between SubmitCopy and these same-context commands.
  for(unsigned eye=0;eye<2;++eye){
   CHECK(compositor.Compose({scratch[eye].view.Get(),props.view.Get(),propDepthView.Get(),views.eyes[eye][0],views.eyes[eye][1],composed[eye].target.Get(),reversed}));
   hostContext->CopyResource(xrTarget[step][eye].texture.Get(),composed[eye].texture.Get());
   hostContext->CopyResource(readback[step][eye].Get(),xrTarget[step][eye].texture.Get());
  }
  CHECK(lease->MarkComposed()&&lease->Composed()&&lease->Copying()&&!lease->Ready());CHECK(!lease->MarkComposed());
  CHECK(feedback.positive==step&&feedback.negative==0&&nativeChannel.PollOutcome()==ipc::Outcome::Pending);
  auto wrong=received;++wrong.trackingGeneration;
  CHECK(consumer.PollCopy(wrong)==TransferResult::Invalid&&consumer.HasPendingCopy());
  CHECK(hostChannel.Feedback(wrong,true)==ipc::ChannelResult::Invalid);
  CHECK(consumer.Open(hostDevice.Get(),delivered)==TransferResult::Busy);
  CHECK(consumer.SubmitCopy(received,destinations)==TransferResult::Busy); // No second GPU copy.
  auto successor=tracking;++successor.generation;successor.predictedNs+=11000000;
  CHECK(hostChannel.QueueRequest(requirements,successor,200)==ipc::ChannelResult::Ok);
  ipc::FrameRequest blockedRequest;CHECK(nativeChannel.TryTake(blockedRequest)==ipc::ChannelResult::Busy);
  auto next=frame;++next.frameId;++next.trackingGeneration;PairTicket blocked;
  CHECK(producer.Publish(sources,next,blocked)==TransferResult::Busy&&!nativeDepth.Begin(next));
  if(step<2){
   // This source event precedes composition. Never label it target completion.
   CHECK(CompleteCopy(consumer,received));CHECK(lease->Complete()&&!lease->Complete());
   CHECK(lease->Ready()&&lease->Composed()&&feedback.last==ipc::ChannelResult::Ok&&feedback.positive==step+1&&feedback.negative==0);
   CHECK(hostChannel.Feedback(received,true)==ipc::ChannelResult::Invalid);
   CHECK(nativeChannel.PollOutcome()==ipc::Outcome::Consumed);
   producer.Acknowledge(received,true);nativeDepth.Acknowledge(received,true);lease.reset();
   CHECK(feedback.positive==step+1&&feedback.negative==0);
   tracking=successor; // Next scratch writes follow old composition reads.
  }else{
   // Cancel AFTER composition was queued, without claiming a completed source.
   // Negative feedback retires that source set; local targets stay until drain.
   lease->CancelPresentation();CHECK(!lease->MarkComposed());
   CHECK(consumer.CancelCopy(received)==TransferResult::Ok);lease.reset();
   CHECK(feedback.positive==2&&feedback.negative==1&&feedback.last==ipc::ChannelResult::Ok);
   CHECK(nativeChannel.PollOutcome()==ipc::Outcome::Discarded);
   producer.Acknowledge(received,false);nativeDepth.Acknowledge(received,false);
   CHECK(nativeChannel.TryTake(blockedRequest)==ipc::ChannelResult::Busy);
   CHECK(consumer.SubmitCopy(received,destinations)==TransferResult::Faulted);
   CHECK(hostChannel.Feedback(received,false)==ipc::ChannelResult::Invalid);
  }
 }
 drain.Arm();CHECK(drain.Wait()); // Covers both eyes, all target writes and staging copies.
 const std::array<unsigned char,4> redPixel{255,0,0,255},greenPixel{0,255,0,255},bluePixel{0,0,255,255};
 // The event remains the GPU command-completion proof. Independently allow
 // transient CPU mapping availability, with one 2 s budget for all six maps.
 const auto mapDeadline=GetTickCount64()+2000;
 for(unsigned step=0;step<3;++step)for(unsigned eye=0;eye<2;++eye){
  D3D11_MAPPED_SUBRESOURCE map{};HRESULT mapResult=E_UNEXPECTED;unsigned mapRetries=0;
  for(;;){
   map={};mapResult=hostContext->Map(readback[step][eye].Get(),0,D3D11_MAP_READ,D3D11_MAP_FLAG_DO_NOT_WAIT,&map);
   if(mapResult!=DXGI_ERROR_WAS_STILL_DRAWING||GetTickCount64()>=mapDeadline)break;
   ++mapRetries;Sleep(1);
  }
  if(FAILED(mapResult))std::fprintf(stderr,"Ordered readback Map failed: hr=0x%08lx device_removed=0x%08lx step=%u eye=%u samples=%u reversed=%u retries=%u\n",
   static_cast<unsigned long>(mapResult),static_cast<unsigned long>(hostDevice->GetDeviceRemovedReason()),step,eye,samples,unsigned(reversed),mapRetries);
  else if(mapRetries)std::fprintf(stdout,"Ordered readback transient Map retries=%u step=%u eye=%u samples=%u reversed=%u\n",mapRetries,step,eye,samples,unsigned(reversed));
  CHECK(SUCCEEDED(mapResult));
  const auto* bytes=static_cast<const unsigned char*>(map.pData)+16*map.RowPitch+16*4;
  const std::array<unsigned char,4> actual{bytes[0],bytes[1],bytes[2],bytes[3]};hostContext->Unmap(readback[step][eye].Get(),0);
  const auto& expected=step==0?(eye?greenPixel:redPixel):(step==1?(eye?redPixel:greenPixel):(eye?greenPixel:bluePixel));
  CHECK(actual==expected);
 }
 localDepth.Reset();consumer.Reset();nativeDepth.Close();producer.Reset();hostChannel.Close();nativeChannel.Close();
 CHECK(feedback.positive==2&&feedback.negative==1);
 std::cout<<"GPU-ordered composition samples="<<samples<<" reversed="<<reversed<<": distinct eyes, both depth stages, ordered scratch reuse, exact IPC ACK and cancelled-target drain passed\n";
 return 0;
}

// Real two-device v9 resources. No game, simulation, XR session or desktop Present.
int RunTwoSlotBridge(bool exportOverlap=false,bool thirdSource=false){using namespace fvr;
 CHECK(!thirdSource||exportOverlap);const unsigned sourceSlots=thirdSource?3u:2u;
 const wchar_t* name=L"FVR_PRIVATE_TWO_SOURCE_SLOTS";wchar_t saved[32768]{};
 const auto length=GetEnvironmentVariableW(name,saved,unsigned(std::size(saved)));CHECK(length<std::size(saved));
 CHECK(SetEnvironmentVariableW(name,L"1"));
 struct RestoreEnv {const wchar_t* name;const wchar_t* value;~RestoreEnv(){SetEnvironmentVariableW(name,value);}} restore{name,length?saved:nullptr};
 const wchar_t* thirdName=L"FVR_PRIVATE_THREE_SOURCE_SLOTS";wchar_t thirdSaved[32768]{};
 const auto thirdLength=GetEnvironmentVariableW(thirdName,thirdSaved,unsigned(std::size(thirdSaved)));CHECK(thirdLength<std::size(thirdSaved));
 RestoreEnv restoreThird{thirdName,thirdLength?thirdSaved:nullptr};CHECK(SetEnvironmentVariableW(thirdName,thirdSource?L"1":nullptr));
 struct HiddenWindow {HWND value=nullptr;~HiddenWindow(){if(value)DestroyWindow(value);}} window;
 window.value=CreateWindowExW(WS_EX_NOACTIVATE,L"STATIC",L"Fvr two-source GPU test",WS_POPUP,0,0,32,32,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
 CHECK(window.value&&!IsWindowVisible(window.value));
 ComPtr<ID3D11Device> nativeDevice,hostDevice;ComPtr<ID3D11DeviceContext> nativeContext,hostContext;D3D_FEATURE_LEVEL level;
 CHECK(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&nativeDevice,&level,&nativeContext)));
 CHECK(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&hostDevice,&level,&hostContext)));
 ComPtr<IDXGIDevice> dx;ComPtr<IDXGIAdapter> adapter;ComPtr<IDXGIFactory> factory;DXGI_ADAPTER_DESC gpu{};
 CHECK(SUCCEEDED(nativeDevice.As(&dx))&&SUCCEEDED(dx->GetAdapter(&adapter))&&SUCCEEDED(adapter->GetDesc(&gpu))&&SUCCEEDED(adapter->GetParent(IID_PPV_ARGS(&factory))));
 DXGI_SWAP_CHAIN_DESC swap{};swap.BufferDesc.Width=swap.BufferDesc.Height=32;swap.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
 swap.SampleDesc.Count=1;swap.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;swap.BufferCount=1;swap.OutputWindow=window.value;swap.Windowed=TRUE;
 ComPtr<IDXGISwapChain> chain;CHECK(SUCCEEDED(factory->CreateSwapChain(nativeDevice.Get(),&swap,&chain)));
 Color desktop;CHECK(SUCCEEDED(chain->GetBuffer(0,IID_PPV_ARGS(&desktop.texture))));
 std::array<Color,2> native,copied;for(unsigned eye=0;eye<2;++eye)CHECK(MakeColor(nativeDevice.Get(),native[eye])&&MakeColor(hostDevice.Get(),copied[eye]));
 D3D11_TEXTURE2D_DESC depthDesc{};depthDesc.Width=depthDesc.Height=32;depthDesc.MipLevels=depthDesc.ArraySize=depthDesc.SampleDesc.Count=1;
 depthDesc.Format=DXGI_FORMAT_R24G8_TYPELESS;depthDesc.BindFlags=D3D11_BIND_DEPTH_STENCIL;
 ComPtr<ID3D11Texture2D> depthTexture;ComPtr<ID3D11DepthStencilView> depthTarget;
 CHECK(SUCCEEDED(nativeDevice->CreateTexture2D(&depthDesc,nullptr,&depthTexture)));
 D3D11_DEPTH_STENCIL_VIEW_DESC dsv{};dsv.Format=DXGI_FORMAT_D24_UNORM_S8_UINT;dsv.ViewDimension=D3D11_DSV_DIMENSION_TEXTURE2D;
 CHECK(SUCCEEDED(nativeDevice->CreateDepthStencilView(depthTexture.Get(),&dsv,&depthTarget)));
 std::array<D3D11PairConsumer,3> readers;D3D11SceneDepthConsumer depths;CHECK(depths.Initialize(hostDevice.Get()));
 ipc::RemoteFrameProvider host;CHECK(host.Create(true,exportOverlap,exportOverlap,thirdSource)&&host.SourceSlotCount()==sourceSlots&&host.SetRequestLifetime(200));
 CHECK(host.LookaheadEnabled()==exportOverlap&&host.ExportOverlapEnabled()==exportOverlap);
 auto bridgeStorage=std::make_unique<D3D11FrameBridge>();auto& bridge=*bridgeStorage;
 CHECK(bridge.ConnectGraphics(nativeDevice.Get(),host.Token())); // NativeProbe's exact call shape.
 CHECK(bridge.TwoSourceSlotsEnabled()&&bridge.SourceSlotCount()==sourceSlots&&!bridge.EarlyFrameAdmissionEnabled());
 CHECK(SetEnvironmentVariableW(name,length?saved:nullptr));
 runtime::PresentationRequirements req{32,32,28,gpu.AdapterLuid.LowPart,gpu.AdapterLuid.HighPart};
 runtime::TrackingFrame input{};input.generation=input.spaceGeneration=1;input.predictedNs=1000000000;input.focused=input.headValid=true;
 input.fov={math::FovTangents{-1,1,1,-1},math::FovTangents{-1,1,1,-1}};
 runtime::TrackingFrame rendered;TextureDescriptor descriptor;PairTicket ticket;
 const std::array<std::array<float,4>,3> colors{{{{1,0,0,1}},{{0,1,0,1}},{{0,0,1,1}}}};
 const std::array<std::array<unsigned char,4>,3> pixels{{{{255,0,0,255}},{{0,255,0,255}},{{0,0,255,255}}}};
 const auto mirrorMatches=[&](unsigned step){return bridge.PresentDesktop(chain.Get(),true,true,false,false)&&Pixel(nativeDevice.Get(),nativeContext.Get(),desktop)==pixels[step%3];};
 const auto capture=[&](const ipc::FrameLease& lease,unsigned step,bool publish=true){
  // A full -> B absent (persistent Busy) -> A partial/omitted -> B full.
  // Partial capture reuses A's allocated quartet, but must not export old depth.
  DeferredDepthAdmission admission;unsigned admissionAttempts=0,depthBegins=0;
  const DepthAdmissionKey key{lease.channelGeneration,lease.requestId,lease.native.owner,lease.native.frameId,
   lease.native.deviceEpoch,lease.tracking.generation,lease.tracking.spaceGeneration,lease.tracking.predictedNs,lease.sourceSlot,lease.sourceSlotCount};
  const auto beginDepth=[&]()noexcept{
   ++admissionAttempts;auto preparation=RestoredPublicationResult::NotAttempted;
   if(!bridge.BeginDepthCapture(lease,false,&preparation))return preparation==RestoredPublicationResult::PrepareBusy?
    DepthAdmissionResult::Busy:DepthAdmissionResult::Rejected;
   ++depthBegins;return DepthAdmissionResult::Ready;
  };
  auto& staged=D3D11FrameBridgeTestAccess::Channel(bridge);
  struct UnlockAdmission {ipc::StagedFrameProducer& p;~UnlockAdmission(){ipc::StagedFrameProducerTestAccess::Unlock(p);}};
  if(step==0){
   // Both 2/3-source paths force actual staged contention at scope entry.
   // No timing race, fake GPU completion, deadline extension or added wait.
   if(!ipc::StagedFrameProducerTestAccess::Lock(staged))return false;
   UnlockAdmission unlock{staged};
   if(!admission.Start(key,beginDepth)||admissionAttempts!=1||depthBegins||
      D3D11FrameBridgeTestAccess::Prepared(bridge,lease))return false;
  }else if(!exportOverlap&&step==1){
   // Retain the existing absent-depth pair, now produced by persistent real
   // contention at the destructive world boundary rather than skipping Begin.
   {
    if(!ipc::StagedFrameProducerTestAccess::Lock(staged))return false;
    UnlockAdmission unlock{staged};
    if(!admission.Start(key,beginDepth)||admissionAttempts!=1||depthBegins)return false;
    nativeContext->ClearDepthStencilView(depthTarget.Get(),D3D11_CLEAR_DEPTH,.75f,0);
    if(admission.BeforeWorldCopy(key,beginDepth)||admissionAttempts!=2||depthBegins)return false;
    nativeContext->ClearDepthStencilView(depthTarget.Get(),D3D11_CLEAR_DEPTH,.25f,0);
   }
   // Unlocking after that clear cannot relabel first-person depth as world.
   if(admission.BeforeWorldCopy(key,beginDepth)||admissionAttempts!=2||depthBegins)return false;
  }else if(!bridge.BeginDepthCapture(lease,false))return false;
  for(unsigned eye=0;eye<2;++eye){
   for(unsigned stage=0;stage<2;++stage){
    if(!exportOverlap&&(step==1||(step==2&&(eye||stage))))continue;
    const float value=.10f+.15f*float(step)+.01f*float(eye*2+stage);
    nativeContext->ClearDepthStencilView(depthTarget.Get(),D3D11_CLEAR_DEPTH,value,0);
    if(step==0&&!eye&&!stage){
     if(!admission.BeforeWorldCopy(key,beginDepth)||admissionAttempts!=2||depthBegins!=1||
        !D3D11FrameBridgeTestAccess::Prepared(bridge,lease))return false;
     if(admission.BeforeWorldCopy(key,beginDepth)||admissionAttempts!=2||depthBegins!=1)return false;
    }
    if(!bridge.CaptureDepth(eye,stage,depthTexture.Get(),lease))return false;}
   nativeContext->ClearRenderTargetView(native[eye].target.Get(),colors[(step+eye)%3].data());
   if(!bridge.CaptureEye(eye,native[eye].texture.Get(),lease))return false;
  }
  return !publish||bridge.PublishRestored(lease);
 };
 const auto deliver=[&](){const auto deadline=GetTickCount64()+2000;
  do{bridge.PumpGraphics();if(host.TryGetCompletedPair(req,input,rendered,descriptor,ticket))return true;Sleep(1);}while(GetTickCount64()<deadline);
  return false;
 };
 const auto retire=[&](const PairTicket& t,bool consumed,std::uint64_t expected){host.PairConsumed(t,consumed);const auto deadline=GetTickCount64()+2000;
  do{host.FlushFeedback();bridge.PumpGraphics();if(bridge.Consumed()+bridge.Discarded()==expected)return true;Sleep(1);}while(GetTickCount64()<deadline);
  return false;
 };
 const auto read=[&](unsigned step,const TextureDescriptor& d,const PairTicket& t){
  runtime::SourceSlotIdentity identity;SceneDepthFrame frame;SceneDepthViews views;
  const bool expectedDepth=exportOverlap||step==0||step==3;
  if(!host.ReadSourceSlot(t,identity)||identity.slotCount!=sourceSlots||identity.slot>=sourceSlots||identity.depthPresent!=expectedDepth)return false;
  const bool hasDepth=host.ReadSceneDepth(d,t,frame);
  if(expectedDepth){
   if(!hasDepth||frame.samples!=1||frame.quality||!depths.CopyBeforeColor(frame,d,t,identity.slot,sourceSlots)||!depths.Views(t,views))return false;
  }else if(hasDepth||!SceneDepthAbsent(frame)||depths.Views(t,views))return false;
  auto& reader=readers[identity.slot];
  // Open is intentionally explicit here; the separate host cache test owns hit accounting.
  if(reader.Open(hostDevice.Get(),d)!=TransferResult::Ok||
     reader.SubmitCopy(t,{{{copied[0].texture.Get(),0},{copied[1].texture.Get(),0}}})!=TransferResult::Busy||!CompleteCopy(reader,t))return false;
  for(unsigned eye=0;eye<2;++eye){if(Pixel(hostDevice.Get(),hostContext.Get(),copied[eye])!=pixels[(step+eye)%3])return false;
   for(unsigned stage=0;expectedDepth&&stage<2;++stage){ComPtr<ID3D11Resource> resource;views.eyes[eye][stage]->GetResource(&resource);ComPtr<ID3D11Texture2D> local;
    if(FAILED(resource.As(&local)))return false;D3D11_TEXTURE2D_DESC shape{};local->GetDesc(&shape);
    shape.BindFlags=shape.MiscFlags=0;shape.Usage=D3D11_USAGE_STAGING;shape.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;if(FAILED(hostDevice->CreateTexture2D(&shape,nullptr,&staging)))return false;
    hostContext->CopyResource(staging.Get(),local.Get());D3D11_MAPPED_SUBRESOURCE map{};
    if(FAILED(hostContext->Map(staging.Get(),0,D3D11_MAP_READ,0,&map)))return false;
    const auto value=(*static_cast<const std::uint32_t*>(map.pData))&0x00ffffffu;hostContext->Unmap(staging.Get(),0);
    const auto expected=std::uint32_t((.10f+.15f*float(step)+.01f*float(eye*2+stage))*16777215.f);
    if(value+2<expected||expected+2<value)return false;
   }
  }
  return true;
 };
 if(thirdSource){
  // Three physical source sets, one camera/capture pair and one delivered host
  // lease. Hold A while B and C overwrite the native inputs and become Ready.
  const auto advance=[&](){++input.generation;input.predictedNs+=11000000;};
  const auto published=[&](std::uint64_t expected){const auto deadline=GetTickCount64()+2000;
   do{bridge.PumpGraphics();if(bridge.Published()==expected)return true;Sleep(1);}while(GetTickCount64()<deadline);return false;};
  const auto poseMatches=[&](const runtime::TrackingFrame& expected){return rendered.generation==expected.generation&&rendered.predictedNs==expected.predictedNs;};
  ipc::FrameLease a,b,c,d,e,f,blocked;
  const auto aTracking=input;host.ServiceRequests(req,input);CHECK(bridge.TryBegin({29,1,1},a)&&a.sourceSlotCount==3);
  advance();const auto bTracking=input;host.ServiceRequests(req,input);
  CHECK(!bridge.TryBegin({29,2,1},blocked)&&capture(a,0)&&deliver()&&poseMatches(aTracking));
  const auto aDescriptor=descriptor;const auto aTicket=ticket;
  advance();const auto cTracking=input;host.ServiceRequests(req,input);
  CHECK(bridge.TryBegin({29,2,1},b)&&b.sourceSlotCount==3&&b.sourceSlot!=a.sourceSlot);
  CHECK(b.tracking.generation==bTracking.generation&&b.tracking.predictedNs==bTracking.predictedNs);
  CHECK(capture(b,1)&&published(2));
  CHECK(bridge.TryBegin({29,3,1},c)&&c.sourceSlotCount==3&&c.sourceSlot!=a.sourceSlot&&c.sourceSlot!=b.sourceSlot);
  CHECK(c.tracking.generation==cTracking.generation&&c.tracking.predictedNs==cTracking.predictedNs);
  auto wrongCount=c;wrongCount.sourceSlotCount=2;CHECK(!bridge.BeginDepthCapture(wrongCount,false));
  auto wrongSlot=c;wrongSlot.sourceSlot=3;CHECK(!bridge.BeginDepthCapture(wrongSlot,false));
  CHECK(capture(c,2)&&published(3)&&mirrorMatches(2));
  CHECK(bridge.Consumed()==0&&bridge.Discarded()==0&&!bridge.TryBegin({29,4,1},blocked));
  CHECK(!host.TryGetCompletedPair(req,input,rendered,descriptor,ticket)); // A still owns the one host delivery.
  CHECK(read(0,aDescriptor,aTicket)&&retire(aTicket,true,1));
  CHECK(deliver()&&poseMatches(bTracking));const auto bDescriptor=descriptor;const auto bTicket=ticket;
  advance();const auto dTracking=input;host.ServiceRequests(req,input);
  CHECK(bridge.TryBegin({29,4,1},d)&&d.sourceSlot==a.sourceSlot&&capture(d,3,false));
  CHECK(read(1,bDescriptor,bTicket)&&retire(bTicket,true,2));
  CHECK(D3D11FrameBridgeTestAccess::Prepared(bridge,d)&&bridge.PublishRestored(d));
  CHECK(deliver()&&poseMatches(cTracking));const auto cDescriptor=descriptor;const auto cTicket=ticket;
  advance();const auto eTracking=input;host.ServiceRequests(req,input);
  CHECK(bridge.TryBegin({29,5,1},e)&&e.sourceSlot==b.sourceSlot&&capture(e,4));
  CHECK(read(2,cDescriptor,cTicket)&&retire(cTicket,false,3)&&mirrorMatches(4));
  CHECK(deliver()&&poseMatches(dTracking));const auto dDescriptor=descriptor;const auto dTicket=ticket;
  CHECK(dDescriptor.resourceEpoch==aDescriptor.resourceEpoch&&LegacyResourceIds(dDescriptor)==LegacyResourceIds(aDescriptor));
  advance();const auto fTracking=input;host.ServiceRequests(req,input);
  CHECK(bridge.TryBegin({29,6,1},f)&&f.sourceSlot==c.sourceSlot&&capture(f,5));
  CHECK(read(3,dDescriptor,dTicket)&&retire(dTicket,true,4));
  CHECK(deliver()&&poseMatches(eTracking));
  CHECK(descriptor.resourceEpoch==bDescriptor.resourceEpoch&&LegacyResourceIds(descriptor)==LegacyResourceIds(bDescriptor));
  CHECK(read(4,descriptor,ticket)&&retire(ticket,true,5));
  CHECK(deliver()&&poseMatches(fTracking));
  CHECK(descriptor.resourceEpoch>cDescriptor.resourceEpoch&&LegacyResourceIds(descriptor)!=LegacyResourceIds(cDescriptor));
  CHECK(read(5,descriptor,ticket)&&retire(ticket,true,6)&&mirrorMatches(5));
  CHECK(bridge.Published()==6&&bridge.Consumed()==5&&bridge.Discarded()==1&&!bridge.Fatal());
  FrameBridgeTimingSnapshot timing;CHECK(!bridge.TimingSnapshot(timing));std::uint64_t completed=0;
  for(unsigned slot=0;slot<3;++slot){CHECK(bridge.SlotTimingSnapshot(slot,timing)&&timing.unmatched==0);completed+=timing.completed;}
  CHECK(completed==6&&!bridge.SlotTimingSnapshot(3,timing));
  host.Suspend();bridge.CloseGraphics();host.Close();const auto evidence=bridge.PipelineSnapshot(true);
  CHECK(evidence.configured&&evidence.available&&evidence.exportOverlap&&evidence.allocationAdmitted&&evidence.sourceSlots==3);
  CHECK(evidence.LogicalTextureCount()==21&&evidence.LogicalByteLimit()==768ull*1024*1024&&evidence.logicalReservationBytes==32u*32u*4u*21u);
  CHECK(evidence.rejectedFeedback==0&&evidence.budgetChecks==(evidence.budgetAvailable?6u:0u));
  CHECK(evidence.slots[a.sourceSlot].generations==1&&evidence.slots[b.sourceSlot].generations==1&&evidence.slots[c.sourceSlot].generations==2);
  std::array<std::uint64_t,18> resources{};unsigned resourceCount=0;
  const std::array<ipc::FrameLease,3> lastLeases{d,e,f};
  for(const auto& lease:lastLeases){const auto& slot=evidence.slots[lease.sourceSlot];const auto& proof=slot.lastPublication;
   CHECK(slot.eyeCopies==4&&slot.depthCopies==8&&proof.present&&proof.key.request==lease.requestId&&proof.ticket.frameId==lease.native.frameId);
   CHECK(SceneDepthMatches(proof.depth,proof.color,proof.ticket)&&proof.depth.samples==1&&proof.depth.quality==0&&proof.observedQpc>0);
   for(const auto id:LegacyResourceIds(proof.color))resources[resourceCount++]=id;
   for(const auto& eye:proof.depth.resources)for(const auto id:eye)resources[resourceCount++]=id;
  }
  CHECK(resourceCount==resources.size());for(unsigned i=0;i<resourceCount;++i){CHECK(resources[i]);for(unsigned j=0;j<i;++j)CHECK(resources[i]!=resources[j]);}
  CHECK(!bridge.PipelineSnapshot(false).slots[2].lastPublication.present);
  std::ostringstream json;evidence.Write(json);CHECK(json.str().find("fvr.native-three-source-slots.v1")!=std::string::npos&&json.str().find("\"source_slots\":3")!=std::string::npos);
  std::cout<<"Three native source slots GPU: held A/B/C, exact original poses/color/depth, ABC-to-A reuse, third-generation retirement and mirror passed\n";
  return 0;
 }
 if(exportOverlap){
  // The same real bridge/resources, with two immutable request poses queued
  // before A exports. No second native camera may enter before restoration.
  host.ServiceRequests(req,input);ipc::FrameLease a,b,blocked;
  CHECK(bridge.TryBegin({23,1,1},a));const auto aTracking=input;
  ++input.generation;input.predictedNs+=11000000;host.ServiceRequests(req,input);const auto bTracking=input;
  CHECK(!bridge.TryBegin({23,2,1},blocked));CHECK(capture(a,0,false));
  D3D11FrameBridgeTestAccess::ReleaseCapture(bridge,a.sourceSlot);
  CHECK(!D3D11FrameBridgeTestAccess::CaptureReleased(bridge,a.sourceSlot));
  {
   auto& staged=D3D11FrameBridgeTestAccess::Channel(bridge);CHECK(ipc::StagedFrameProducerTestAccess::Lock(staged));
   struct Unlock {ipc::StagedFrameProducer& p;~Unlock(){ipc::StagedFrameProducerTestAccess::Unlock(p);}} unlock{staged};
   CHECK(bridge.PublishRestored(a)&&bridge.Published()==0);
  }
  // Exercise the actual release service without polling/forcing a GPU query.
  // A fast initial query can already be Submitting and legitimately skip the
  // optimization; that branch must publish normally before B is admitted.
  D3D11FrameBridgeTestAccess::ReleaseCapture(bridge,a.sourceSlot);
  const bool released=D3D11FrameBridgeTestAccess::CaptureReleased(bridge,a.sourceSlot);
  if(released){CHECK(bridge.Published()==0&&bridge.TryBegin({23,2,1},b));}
  else{
   CHECK(!bridge.TryBegin({23,2,1},blocked));const auto deadline=GetTickCount64()+2000;
   do{bridge.PumpGraphics();if(bridge.Published()==1)break;Sleep(1);}while(GetTickCount64()<deadline);
   CHECK(bridge.Published()==1&&bridge.TryBegin({23,2,1},b));
  }
  CHECK(a.sourceSlot!=b.sourceSlot&&b.tracking.generation==bTracking.generation&&b.tracking.predictedNs==bTracking.predictedNs);
  CHECK(capture(b,1,false)); // Overwrites both shared eyes and the native depth input before any A host copy/ACK.
  CHECK(bridge.Consumed()==0&&bridge.Discarded()==0&&!bridge.TryBegin({23,3,1},blocked));
  CHECK(deliver());const auto aDescriptor=descriptor;const auto aTicket=ticket;
  CHECK(rendered.generation==aTracking.generation&&rendered.predictedNs==aTracking.predictedNs);
  CHECK(read(0,aDescriptor,aTicket));CHECK(bridge.Consumed()==0&&bridge.Discarded()==0);
  CHECK(retire(aTicket,true,1)&&D3D11FrameBridgeTestAccess::Prepared(bridge,b));
  CHECK(bridge.PublishRestored(b)&&deliver());
  CHECK(rendered.generation==bTracking.generation&&rendered.predictedNs==bTracking.predictedNs);
  CHECK(LegacyResourceIds(aDescriptor)!=LegacyResourceIds(descriptor));
  CHECK(read(1,descriptor,ticket)&&retire(ticket,true,2));
  CHECK(bridge.Published()==2&&bridge.Consumed()==2&&bridge.Discarded()==0&&!bridge.Fatal());
  host.Suspend();bridge.CloseGraphics();host.Close();const auto evidence=bridge.PipelineSnapshot(true);
   CHECK(evidence.exportOverlap&&evidence.rejectedFeedback==0&&evidence.captureReleaseDeclined==0);
  CHECK(!released||evidence.captureReleases>0);
  CHECK(evidence.slots[0].eyeCopies+evidence.slots[1].eyeCopies==4&&evidence.slots[0].depthCopies+evidence.slots[1].depthCopies==8);
  std::cout<<"Export-overlap GPU: original A color/depth after B capture overwrite, exact poses/ACK, pre-publication capture release="<<released<<", real-query fast path allowed\n";
  return 0;
 }
 CHECK(!host.TryGetCompletedPair(req,input,rendered,descriptor,ticket));
 ipc::FrameLease a,b,c,d;CHECK(bridge.TryBegin({17,1,1},a)&&a.sourceSlotCount==2);
 CHECK(capture(a,0));++input.generation;input.predictedNs+=11000000;CHECK(deliver());
 const auto aDescriptor=descriptor;const auto aTicket=ticket;
 runtime::SourceSlotIdentity aIdentity;CHECK(host.ReadSourceSlot(aTicket,aIdentity)&&aIdentity.slot==a.sourceSlot&&aIdentity.requestId==a.requestId);
 CHECK(bridge.TryBegin({17,2,1},b)&&b.sourceSlotCount==2&&b.sourceSlot!=a.sourceSlot);
 CHECK(capture(b,1,false)&&mirrorMatches(0));
 // A remains immutable after B overwrites both capture eyes. Completing A
 // while B is Rendering must preserve B's exact prepared graphics grant.
 CHECK(read(0,aDescriptor,aTicket));CHECK(retire(aTicket,true,1));
 CHECK(D3D11FrameBridgeTestAccess::Prepared(bridge,b));
 {
  auto& staged=D3D11FrameBridgeTestAccess::Channel(bridge);CHECK(ipc::StagedFrameProducerTestAccess::Lock(staged));
  struct Unlock {ipc::StagedFrameProducer& p;~Unlock(){ipc::StagedFrameProducerTestAccess::Unlock(p);}} unlock{staged};
  CHECK(bridge.PublishRestored(b)&&bridge.Published()==1);
 }
 CHECK(mirrorMatches(1));
 const auto publishDeadline=GetTickCount64()+2000;
 do{bridge.PumpGraphics();if(bridge.Published()==2)break;Sleep(1);}while(GetTickCount64()<publishDeadline);
 CHECK(bridge.Published()==2&&bridge.Consumed()==1&&bridge.Discarded()==0);
 ipc::FrameLease blocked;CHECK(!bridge.TryBegin({17,3,1},blocked));
 ++input.generation;input.predictedNs+=11000000;CHECK(deliver());const auto bDescriptor=descriptor;const auto bTicket=ticket;
 CHECK(LegacyResourceIds(aDescriptor)!=LegacyResourceIds(bDescriptor));
 CHECK(bridge.TryBegin({17,3,1},c)&&c.sourceSlot==a.sourceSlot);CHECK(capture(c,2)&&mirrorMatches(2));
 const auto overlapDeadline=GetTickCount64()+2000;
 do{bridge.PumpGraphics();if(bridge.Published()==3)break;Sleep(1);}while(GetTickCount64()<overlapDeadline);
 CHECK(bridge.Published()==3&&bridge.Consumed()==1&&bridge.Discarded()==0);
 CHECK(read(1,bDescriptor,bTicket));CHECK(retire(bTicket,false,2)); // Only B's source generation retires.
 CHECK(mirrorMatches(2));++input.generation;input.predictedNs+=11000000;CHECK(deliver());
 const auto cDescriptor=descriptor;const auto cTicket=ticket;
 CHECK(cDescriptor.resourceEpoch==aDescriptor.resourceEpoch&&LegacyResourceIds(cDescriptor)==LegacyResourceIds(aDescriptor));
 CHECK(bridge.TryBegin({17,4,1},d)&&d.sourceSlot==b.sourceSlot);CHECK(capture(d,3));
 CHECK(read(2,cDescriptor,cTicket)&&retire(cTicket,true,3));
 ++input.generation;input.predictedNs+=11000000;CHECK(deliver());
 CHECK(descriptor.resourceEpoch>bDescriptor.resourceEpoch&&LegacyResourceIds(descriptor)!=LegacyResourceIds(bDescriptor));
 CHECK(read(3,descriptor,ticket)&&retire(ticket,true,4));
 CHECK(mirrorMatches(3));
 ipc::FrameLease cancelled;CHECK(bridge.TryBegin({17,5,1},cancelled));bridge.Cancel(cancelled);bridge.PumpGraphics();
 CHECK(bridge.Published()==4&&bridge.Consumed()==3&&bridge.Discarded()==1&&!bridge.Fatal());
 FrameBridgeTimingSnapshot timing;CHECK(!bridge.TimingSnapshot(timing));
 CHECK(bridge.SlotTimingSnapshot(0,timing)&&timing.unmatched==0);const auto completed0=timing.completed;
 CHECK(bridge.SlotTimingSnapshot(1,timing)&&timing.unmatched==0&&completed0+timing.completed==5);
 host.Suspend();bridge.CloseGraphics();host.Close();
 const auto evidence=bridge.PipelineSnapshot(true);
 CHECK(evidence.configured&&evidence.available&&evidence.allocationAdmitted&&evidence.logicalReservationBytes==32u*32u*4u*15u);
 CHECK(evidence.rejectedFeedback==0&&evidence.slots[a.sourceSlot].generations==1&&evidence.slots[b.sourceSlot].generations==2);
 const auto& aProof=evidence.slots[a.sourceSlot].lastPublication;const auto& bProof=evidence.slots[b.sourceSlot].lastPublication;
 CHECK(aProof.present&&bProof.present&&aProof.key.request==c.requestId&&bProof.key.request==d.requestId);
 CHECK(aProof.ticket.frameId==c.native.frameId&&bProof.ticket.frameId==d.native.frameId);
 CHECK(SceneDepthAbsent(aProof.depth)&&SceneDepthMatches(bProof.depth,bProof.color,bProof.ticket));
 CHECK(bProof.depth.samples==1&&LegacyResourceIds(aProof.color)!=LegacyResourceIds(bProof.color));
 CHECK(aProof.observedQpc>0&&bProof.observedQpc>0&&!bridge.PipelineSnapshot(false).slots[0].lastPublication.present);
 CHECK(evidence.budgetChecks==(evidence.budgetAvailable?4u:0u)); // Initial + first A depth + B color recreation + first B depth.
 CHECK(evidence.slots[0].eyeCopies+evidence.slots[1].eyeCopies==8&&evidence.slots[0].depthCopies+evidence.slots[1].depthCopies==9);
 // Each rejection is a separate channel lifetime. Never reuse failed sources.
 CHECK(SetEnvironmentVariableW(name,L"1"));
 for(unsigned fault=0;fault<3;++fault){
  ipc::RemoteFrameProvider rejectedHost;CHECK(rejectedHost.Create(true));auto rejectedStorage=std::make_unique<D3D11FrameBridge>();auto& rejected=*rejectedStorage;
  CHECK(rejected.ConnectGraphics(nativeDevice.Get(),rejectedHost.Token(),FrameSharingPreference::LegacyIpc));
  auto requested=req;if(fault==0)requested.width=requested.height=4096; // Channel rejects above its source-set cap before bridge allocation.
  CHECK(!rejectedHost.TryGetCompletedPair(requested,input,rendered,descriptor,ticket));ipc::FrameLease lease;
  if(fault==0){CHECK(!rejected.TryBegin({19,1,1},lease)&&!rejected.Fatal());}
  else {
   CHECK(rejected.TryBegin({19,1,1},lease));
   CHECK(rejected.BeginDepthCapture(lease,false));
   if(fault==1){auto multisample=depthDesc;multisample.SampleDesc.Count=2;ComPtr<ID3D11Texture2D> source;
    CHECK(SUCCEEDED(nativeDevice->CreateTexture2D(&multisample,nullptr,&source)));
    CHECK(!rejected.CaptureDepth(0,0,source.Get(),lease)&&rejected.FailureStage()==22);
   }else{
    bool foreignAccepted=true;std::thread foreign([&]{foreignAccepted=rejected.CaptureEye(0,native[0].texture.Get(),lease);});foreign.join();
    CHECK(!foreignAccepted&&rejected.CapturedEyes()==0&&!rejected.Fatal());
    CHECK(!rejected.CaptureEye(0,copied[0].texture.Get(),lease)&&rejected.FailureStage()==22); // Wrong device.
   }
  }
  if(fault!=0)rejected.Cancel(lease);rejected.PumpGraphics();rejectedHost.Suspend();rejected.CloseGraphics();rejectedHost.Close();
  const auto rejectedProof=rejected.PipelineSnapshot(true);
  CHECK(rejected.Published()==0&&rejected.CapturedEyes()==0&&rejectedProof.slots[0].depthCopies+rejectedProof.slots[1].depthCopies==0);
  if(fault==0)CHECK(!rejectedProof.allocationAdmitted&&rejectedProof.slots[0].generations==0&&rejectedProof.slots[1].generations==0);
 }
 CHECK(SetEnvironmentVariableW(name,length?saved:nullptr));
 CHECK(!IsWindowVisible(window.value));
 std::cout<<"Two native source slots GPU: full/absent/partial-omitted/full depth, held A/B color, exact ACK/retirement, generation replacement and mirror passed\n";
 return 0;
}

// Explicit scene recovery keeps the live host, but retires only fully drained
// source sets. A frame-1 replacement must get fresh shared resource identities.
int RunBridgeWorldTransition(bool thirdSource){using namespace fvr;
 struct Environment {const wchar_t* name;std::wstring saved;bool present=false;
  explicit Environment(const wchar_t* n):name(n){wchar_t value[32768]{};const auto size=GetEnvironmentVariableW(n,value,unsigned(std::size(value)));present=size>0&&size<std::size(value);if(present)saved.assign(value,size);}
  ~Environment(){SetEnvironmentVariableW(name,present?saved.c_str():nullptr);}
 } oldTwo(L"FVR_PRIVATE_TWO_SOURCE_SLOTS"),oldThree(L"FVR_PRIVATE_THREE_SOURCE_SLOTS");
 CHECK(SetEnvironmentVariableW(oldTwo.name,L"1")&&SetEnvironmentVariableW(oldThree.name,thirdSource?L"1":nullptr));
 ComPtr<ID3D11Device> nativeDevice,hostDevice;ComPtr<ID3D11DeviceContext> nativeContext,hostContext;D3D_FEATURE_LEVEL level;
 CHECK(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&nativeDevice,&level,&nativeContext)));
 CHECK(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&hostDevice,&level,&hostContext)));
 ComPtr<IDXGIDevice> dx;ComPtr<IDXGIAdapter> adapter;DXGI_ADAPTER_DESC gpu{};
 CHECK(SUCCEEDED(nativeDevice.As(&dx))&&SUCCEEDED(dx->GetAdapter(&adapter))&&SUCCEEDED(adapter->GetDesc(&gpu)));
 std::array<Color,2> native,copied;for(unsigned eye=0;eye<2;++eye)CHECK(MakeColor(nativeDevice.Get(),native[eye])&&MakeColor(hostDevice.Get(),copied[eye]));
 D3D11_TEXTURE2D_DESC shape{};shape.Width=shape.Height=32;shape.MipLevels=shape.ArraySize=shape.SampleDesc.Count=1;
 shape.Format=DXGI_FORMAT_R24G8_TYPELESS;shape.BindFlags=D3D11_BIND_DEPTH_STENCIL;
 ComPtr<ID3D11Texture2D> z;ComPtr<ID3D11DepthStencilView> zTarget;CHECK(SUCCEEDED(nativeDevice->CreateTexture2D(&shape,nullptr,&z)));
 D3D11_DEPTH_STENCIL_VIEW_DESC zView{};zView.Format=DXGI_FORMAT_D24_UNORM_S8_UINT;zView.ViewDimension=D3D11_DSV_DIMENSION_TEXTURE2D;
 CHECK(SUCCEEDED(nativeDevice->CreateDepthStencilView(z.Get(),&zView,&zTarget)));
 ipc::FrameChannel host;CHECK(host.CreateHost(true,true,true,thirdSource));
 auto storage=std::make_unique<D3D11FrameBridge>();auto& bridge=*storage;CHECK(bridge.ConnectGraphics(nativeDevice.Get(),host.Token()));
 runtime::PresentationRequirements req{32,32,28,gpu.AdapterLuid.LowPart,gpu.AdapterLuid.HighPart};
 runtime::TrackingFrame tracking{};tracking.generation=tracking.spaceGeneration=1;tracking.predictedNs=1000000000;tracking.focused=tracking.headValid=true;
 tracking.fov={math::FovTangents{-1,1,1,-1},math::FovTangents{-1,1,1,-1}};
 const ipc::NativeFrameKey oldOwner{17,50,1},replacement{19,1,1};
 const auto capture=[&](const ipc::FrameLease& lease,bool reversedColors,bool depthStarted=false){
  if(!depthStarted&&!bridge.BeginDepthCapture(lease,false))return false;
  for(unsigned eye=0;eye<2;++eye){for(unsigned stage=0;stage<2;++stage){
   nativeContext->ClearDepthStencilView(zTarget.Get(),D3D11_CLEAR_DEPTH,.2f+.1f*float(eye*2+stage),0);
   if(!bridge.CaptureDepth(eye,stage,z.Get(),lease))return false;
  }
  const float red[]{1,0,0,1},blue[]{0,0,1,1};nativeContext->ClearRenderTargetView(native[eye].target.Get(),(eye!=unsigned(reversedColors))?blue:red);
  if(!bridge.CaptureEye(eye,native[eye].texture.Get(),lease))return false;}
  return bridge.PublishRestored(lease);
 };
 const auto deliver=[&](TextureDescriptor& descriptor,PairTicket& ticket){const auto end=GetTickCount64()+2000;
  do{bridge.PumpGraphics();const auto r=host.PollRequest(descriptor,ticket);if(r==ipc::ChannelResult::Ok)return true;if(r!=ipc::ChannelResult::Busy)return false;Sleep(1);}while(GetTickCount64()<end);return false;
 };
 D3D11PairConsumer reader;D3D11SceneDepthConsumer depthReader;CHECK(depthReader.Initialize(hostDevice.Get()));
 const auto copy=[&](const TextureDescriptor& descriptor,const PairTicket& ticket,SceneDepthFrame& depth){
  runtime::SourceSlotIdentity identity;if(!host.ReadSourceSlot(ticket,identity)||!host.ReadSceneDepth(descriptor,ticket,depth)||
    !depthReader.CopyBeforeColor(depth,descriptor,ticket,identity.slot,identity.slotCount))return false;
  return reader.Open(hostDevice.Get(),descriptor)==TransferResult::Ok&&
   reader.SubmitCopy(ticket,{{{copied[0].texture.Get(),0},{copied[1].texture.Get(),0}}})==TransferResult::Busy&&CompleteCopy(reader,ticket);
 };
 CHECK(bridge.TransitionWorldOwnerGraphics({9,1,1},oldOwner)==ipc::ChannelResult::Ok); // Truly cold, no allocation or lease.
 CHECK(host.SubmitLookaheadRequest(req,tracking,200)==ipc::ChannelResult::Ok);ipc::FrameLease first;
 CHECK(bridge.TryBegin(oldOwner,first));
 CHECK(bridge.TransitionWorldOwnerGraphics(oldOwner,replacement)==ipc::ChannelResult::Busy); // Rendering before graphics allocation.
 CHECK(bridge.BeginDepthCapture(first,false));
 CHECK(bridge.TransitionWorldOwnerGraphics(oldOwner,replacement)==ipc::ChannelResult::Busy);
 CHECK(capture(first,false,true));TextureDescriptor beforeDescriptor;PairTicket beforeTicket;
 CHECK(deliver(beforeDescriptor,beforeTicket));SceneDepthFrame beforeDepth;CHECK(copy(beforeDescriptor,beforeTicket,beforeDepth));
 CHECK(bridge.TransitionWorldOwnerGraphics(oldOwner,replacement)==ipc::ChannelResult::Busy);
 auto wrongOld=oldOwner;++wrongOld.owner;CHECK(bridge.TransitionWorldOwnerGraphics(wrongOld,replacement)==ipc::ChannelResult::Invalid);
 auto wrongDevice=replacement;++wrongDevice.deviceEpoch;CHECK(bridge.TransitionWorldOwnerGraphics(oldOwner,wrongDevice)==ipc::ChannelResult::Invalid);
 ipc::ChannelResult foreign=ipc::ChannelResult::Ok;std::thread other([&]{foreign=bridge.TransitionWorldOwnerGraphics(oldOwner,replacement);});other.join();CHECK(foreign==ipc::ChannelResult::Invalid);
 CHECK(!bridge.Fatal()&&host.Connected());
 ++tracking.generation;tracking.predictedNs+=11000000;CHECK(host.SubmitLookaheadRequest(req,tracking,200)==ipc::ChannelResult::Ok);
 CHECK(host.Feedback(beforeTicket,true)==ipc::ChannelResult::Ok);bridge.PumpGraphics();CHECK(bridge.Consumed()==1);
 auto& staged=D3D11FrameBridgeTestAccess::Channel(bridge);CHECK(ipc::StagedFrameProducerTestAccess::Lock(staged));
 const auto busy=bridge.TransitionWorldOwnerGraphics(oldOwner,replacement);ipc::StagedFrameProducerTestAccess::Unlock(staged);CHECK(busy==ipc::ChannelResult::Busy);
 auto oldAtRecovery=oldOwner;oldAtRecovery.frameId=99;CHECK(bridge.TransitionWorldOwnerGraphics(oldAtRecovery,replacement)==ipc::ChannelResult::Ok);
 CHECK(host.Connected()&&!bridge.Fatal());
 interaction::InputFrame controls{};controls.generation=controls.spaceGeneration=1;controls.predictedNs=1000000000;controls.focused=controls.headValid=true;
 CHECK(host.PublishInput(controls)==ipc::ChannelResult::Ok);interaction::InputFrame observed;
 CHECK(bridge.ReadInput(observed)==ipc::ChannelResult::Ok&&observed.generation==controls.generation);
 ipc::FrameLease next;CHECK(bridge.TryBegin(replacement,next)&&next.channelGeneration==first.channelGeneration&&next.tracking.generation==tracking.generation);
 CHECK(capture(next,true));TextureDescriptor afterDescriptor;PairTicket afterTicket;CHECK(deliver(afterDescriptor,afterTicket));
 CHECK(afterTicket.frameId==1&&afterDescriptor.resourceEpoch>beforeDescriptor.resourceEpoch&&LegacyResourceIds(afterDescriptor)!=LegacyResourceIds(beforeDescriptor));
 CHECK(host.Feedback(beforeTicket,true)==ipc::ChannelResult::Invalid); // Old ticket cannot acknowledge the new owner.
 SceneDepthFrame afterDepth;CHECK(copy(afterDescriptor,afterTicket,afterDepth));CHECK(afterDepth.resources!=beforeDepth.resources);
 CHECK(Pixel(hostDevice.Get(),hostContext.Get(),copied[0])[2]==255&&Pixel(hostDevice.Get(),hostContext.Get(),copied[1])[0]==255);
 CHECK(host.Feedback(afterTicket,true)==ipc::ChannelResult::Ok);bridge.PumpGraphics();CHECK(bridge.Consumed()==2&&!bridge.Fatal());
 CHECK(bridge.TransitionWorldOwnerGraphics(oldOwner,replacement)==ipc::ChannelResult::Invalid);
 bridge.CloseGraphics();host.Close();std::cout<<"World-owner bridge GPU sources="<<(thirdSource?3:2)<<": held/draining rejection, live input, frame-1 fresh resources and exact new pixels passed\n";return 0;
}

int main(int argc,char** argv){if(argc==2&&std::string_view(argv[1])=="--legacy-depth-cache"){for(unsigned samples:{1u,2u,4u,8u})if(Run(samples))return 1;return 0;}
 if(RunTwoSlotBridge()||RunTwoSlotBridge(true)||RunTwoSlotBridge(true,true))return 1;if(RunBridgeWorldTransition(false)||RunBridgeWorldTransition(true))return 1;if(RunAsyncCopy())return 1;for(unsigned samples:{1u,8u})for(bool reversed:{false,true})if(RunOrderedComposition(samples,reversed))return 1;for(unsigned samples:{1u,2u,4u,8u})if(Run(samples))return 1;if(RunBridge()||RunEarlyAdmission())return 1;
 if(RunBridgeMirror(FrameSharingPreference::Automatic,1))return 1;
 if(RunBridgeMirror(FrameSharingPreference::LegacyRelay,2))return 1;
 return RunBridgeMirror(FrameSharingPreference::LegacyIpc,3);}
