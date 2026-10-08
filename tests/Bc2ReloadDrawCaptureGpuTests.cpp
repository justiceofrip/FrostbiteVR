#include "Bc2ReloadDrawCapture.h"
#include "Bc2ReloadDrawGeometry.h"
#include "Test.h"
#include <Windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include <iostream>
using namespace fvr::bc2;using Microsoft::WRL::ComPtr;
int main(){
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;D3D_FEATURE_LEVEL level{};
    CHECK(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,&level,&context)));
    std::array<std::byte,144> vertexBytes{};for(unsigned i=0;i<vertexBytes.size();++i)vertexBytes[i]=std::byte(i);
    std::array<std::uint16_t,90> indexValues{};for(unsigned i=0;i<90;++i)indexValues[i]=std::uint16_t(i%3);
    std::array<std::byte,128> constantBytes{};for(unsigned i=0;i<constantBytes.size();++i)constantBytes[i]=std::byte(i+7);
    const auto buffer=[&](const void* bytes,unsigned size,unsigned flags){ComPtr<ID3D11Buffer> b;D3D11_BUFFER_DESC desc{};desc.ByteWidth=size;desc.Usage=D3D11_USAGE_IMMUTABLE;desc.BindFlags=flags;D3D11_SUBRESOURCE_DATA data{bytes,0,0};if(FAILED(device->CreateBuffer(&desc,&data,&b)))return ComPtr<ID3D11Buffer>{};return b;};
    auto vertices=buffer(vertexBytes.data(),unsigned(vertexBytes.size()),D3D11_BIND_VERTEX_BUFFER);
    auto indices=buffer(indexValues.data(),unsigned(sizeof(indexValues)),D3D11_BIND_INDEX_BUFFER);
    auto constants=buffer(constantBytes.data(),unsigned(constantBytes.size()),D3D11_BIND_CONSTANT_BUFFER);CHECK(vertices&&indices&&constants);
    std::array<std::byte,320> slotBytes{};for(unsigned i=0;i<slotBytes.size();++i)slotBytes[i]=std::byte((i+13)&255);
    auto slotBuffer=buffer(slotBytes.data(),unsigned(slotBytes.size()),D3D11_BIND_CONSTANT_BUFFER);CHECK(slotBuffer);
    ID3D11Buffer* slotPointer=slotBuffer.Get();context->VSSetConstantBuffers(0,1,&slotPointer);
    unsigned stride=48,offset=0;ID3D11Buffer* vertex=vertices.Get();context->IASetVertexBuffers(0,1,&vertex,&stride,&offset);context->IASetIndexBuffer(indices.Get(),DXGI_FORMAT_R16_UINT,0);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);ID3D11Buffer* cb=constants.Get();context->VSSetConstantBuffers(3,1,&cb);
    Bc2ReloadDrawCapture capture;CHECK(!capture.Enable(false,1000000000));CHECK(capture.Enable(true,1000000000));CHECK(!capture.Enable(true,1000000000));
    ReloadDrawFrameEvidence f{};f.world=1;f.request=2;f.view=3;f.nativeFrame=4;f.nowNs=1000000000;f.tickMs=1000;
    f.producer.packedShellValid=true;std::memcpy(f.producer.packedShell.data(),constantBytes.data()+16,48);
    capture.BeginEye(context.Get(),f);
    // More unrelated world draws than the entire record budget must not crowd
    // out the first valid shell layout or consume the per-frame/pending quota.
    stride=32;context->IASetVertexBuffers(0,1,&vertex,&stride,&offset);
    for(unsigned i=0;i<2048;++i)capture.ObserveIndexed(context.Get(),90,0,0);
    stride=48;context->IASetVertexBuffers(0,1,&vertex,&stride,&offset);
    for(unsigned i=0;i<9;++i)capture.ObserveIndexed(context.Get(),90,0,0);capture.EndEye();
    // Test owns submission; observer never flushes or waits. Poll finite work.
    context->Flush();for(unsigned i=0;i<100;++i){capture.Poll(context.Get());Sleep(1);}
    ComPtr<ID3D11Buffer> afterV,afterI,afterC;UINT afterStride=0,afterOffset=0;context->IAGetVertexBuffers(0,1,&afterV,&afterStride,&afterOffset);
    DXGI_FORMAT afterFormat{};UINT afterIndexOffset=0;context->IAGetIndexBuffer(&afterI,&afterFormat,&afterIndexOffset);context->VSGetConstantBuffers(3,1,&afterC);
    D3D11_PRIMITIVE_TOPOLOGY afterTopology{};context->IAGetPrimitiveTopology(&afterTopology);
    CHECK(afterV.Get()==vertices.Get()&&afterI.Get()==indices.Get()&&afterC.Get()==constants.Get()&&afterStride==48&&afterOffset==0&&afterIndexOffset==0&&afterFormat==DXGI_FORMAT_R16_UINT&&afterTopology==D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    // A different OS thread cannot touch the immediate context through helper.
    std::thread other([&]{capture.Poll(context.Get());});other.join();
    f.nativeFrame=5;f.eye=1;f.producer.packedShellValid=false;f.nowNs+=Bc2ReloadDrawCapture::SamplePeriodNs;capture.BeginEye(context.Get(),f);capture.ObserveIndexed(context.Get(),90,0,0);capture.EndEye();context->Flush();
    for(unsigned i=0;i<100;++i){capture.Poll(context.Get());Sleep(1);}
    // Different layout: ACOG candidate uses float3+indices+weights (20 bytes).
    std::array<std::byte,8*68> opticVertexBytes{};for(unsigned i=0;i<opticVertexBytes.size();++i)opticVertexBytes[i]=std::byte(i+31);
    std::array<std::uint16_t,12> opticIndices{};for(unsigned i=0;i<12;++i)opticIndices[i]=std::uint16_t(i%8);
    auto opticV=buffer(opticVertexBytes.data(),unsigned(opticVertexBytes.size()),D3D11_BIND_VERTEX_BUFFER);
    auto opticI=buffer(opticIndices.data(),unsigned(sizeof(opticIndices)),D3D11_BIND_INDEX_BUFFER);CHECK(opticV&&opticI);
    vertex=opticV.Get();stride=68;context->IASetVertexBuffers(0,1,&vertex,&stride,&offset);context->IASetIndexBuffer(opticI.Get(),DXGI_FORMAT_R16_UINT,0);
    f.nativeFrame=6;f.nowNs+=Bc2ReloadDrawCapture::SamplePeriodNs;f.producer.packedOpticValid=true;
    context->VSSetConstantBuffers(0,1,&cb); // Too short: observe no candidate slot.
    std::memcpy(f.producer.packedOptic.data(),constantBytes.data()+32,48);
    capture.BeginEye(context.Get(),f);capture.ObserveIndexed(context.Get(),12,0,0);capture.EndEye();context->Flush();
    for(unsigned i=0;i<100;++i){capture.Poll(context.Get());Sleep(1);}
    f.nativeFrame=7;f.nowNs=1000000000+Bc2ReloadDrawCapture::WindowNs;capture.BeginEye(context.Get(),f);capture.ObserveIndexed(context.Get(),90,0,0);capture.EndEye();
    CHECK(capture.Stop());std::ostringstream out;capture.Report(out);const auto json=out.str();
    CHECK(json.find("\"sampled_frames\":3")!=std::string::npos&&json.find("\"candidates\":2059")!=std::string::npos);
    CHECK(json.find("\"layout_rejected\":2048")!=std::string::npos&&json.find("\"record_overflow\":0")!=std::string::npos&&json.find("\"frame_overflow\":0")!=std::string::npos);
    CHECK(json.find("\"pending_overflow\":1")!=std::string::npos&&json.find("\"wrong_thread\":1")!=std::string::npos&&json.find("\"errors\":0")!=std::string::npos);
    CHECK(json.find("\"geometry_valid\":true")!=std::string::npos&&json.find("\"shell_section\":0")!=std::string::npos);
    CHECK(json.find("\"packed_shell_offsets\":[16]")!=std::string::npos&&json.find("\"producer_association_verified\":false")!=std::string::npos);
    unsigned completed=0;std::size_t at=0;while((at=json.find("\"geometry_valid\":true",at))!=std::string::npos){++completed;++at;}CHECK(completed==10);
    CHECK(json.find("\"packed_optic_offsets\":[32]")!=std::string::npos);
    std::string expected;constexpr char hex[]="0123456789abcdef";
    for(unsigned i=272;i<320;++i){const auto v=std::to_integer<unsigned>(slotBytes[i]);expected+=hex[v>>4];expected+=hex[v&15];}
    const auto captured="\"shell_slot_observation\":{\"captured\":true,\"constant_buffer_slot\":0,\"byte_offset\":272,\"byte_count\":48,\"shader_consumption_verified\":false,\"bytes_hex\":\""+expected+"\"}";
    unsigned slotCount=0;at=0;while((at=json.find(captured,at))!=std::string::npos){++slotCount;++at;}CHECK(slotCount==9);
    CHECK(json.find("\"shell_slot_observation\":{\"captured\":false")!=std::string::npos);
    // Competing callbacks cannot touch render-thread state, and closing
    // admission linearizes with every in-flight count/recheck (seq_cst).
    Bc2ReloadDrawCapture concurrent;CHECK(concurrent.Enable(true,1000000000));
    f.nativeFrame=1;f.nowNs=1000000000;concurrent.BeginEye(context.Get(),f);concurrent.EndEye();
    std::ostringstream live;concurrent.Report(live);CHECK(live.str().find("\"drained\":false")!=std::string::npos);
    std::atomic<bool> go=false;std::atomic<unsigned> ready=0;std::vector<std::thread> racers;
    for(unsigned n=0;n<4;++n)racers.emplace_back([&]{++ready;while(!go.load())std::this_thread::yield();
        for(unsigned i=0;i<10000;++i){concurrent.BeginEye(context.Get(),f);concurrent.ObserveIndexed(context.Get(),90,0,0);concurrent.Poll(context.Get());concurrent.EndEye();}});
    while(ready.load()!=4)std::this_thread::yield();go=true;
    for(unsigned i=0;i<100;++i){concurrent.BeginEye(context.Get(),f);concurrent.EndEye();}
    bool drained=false;for(unsigned i=0;i<2000&&!drained;++i){drained=concurrent.Stop();if(!drained)std::this_thread::yield();}
    for(auto& racer:racers)racer.join();CHECK(concurrent.Stop());
    std::ostringstream before;concurrent.Report(before);
    concurrent.BeginEye(context.Get(),f);concurrent.ObserveIndexed(context.Get(),90,0,0);concurrent.Poll(context.Get());concurrent.EndEye();
    std::ostringstream after;concurrent.Report(after);CHECK(before.str()==after.str());
    CHECK(before.str().find("\"stopped\":true")!=std::string::npos&&before.str().find("\"candidates\":0")!=std::string::npos);
    std::cout<<"WARP readback passed: 10 retained draws across shell/optic layouts, identity rejection, exact CB matches, unchanged IA/CB state, queue/window bounds, concurrent admission/drain.\n";
}
