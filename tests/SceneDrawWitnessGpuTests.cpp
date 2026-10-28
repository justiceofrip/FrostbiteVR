#include "fvr/graphics/D3D11SceneDrawWitness.h"
#include "Test.h"
#include <Windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <array>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <thread>
#include <vector>
using namespace fvr::graphics;using Microsoft::WRL::ComPtr;
int main(int argc,char** argv){
    // Explicit output path: root's storage-guarded recipe supplies a new G:
    // directory. Tests never create opaque temp trees or delete old evidence.
    CHECK(argc==2);const std::filesystem::path output=argv[1];CHECK(output.is_absolute()&&!std::filesystem::exists(output));
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;D3D_FEATURE_LEVEL level{};
    CHECK(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,&level,&context)));
    D3D11_BUFFER_DESC bd{};bd.ByteWidth=64;bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    ComPtr<ID3D11Buffer> cb;CHECK(SUCCEEDED(device->CreateBuffer(&bd,nullptr,&cb)));
    std::array<unsigned char,64> left{},right{};for(unsigned i=0;i<64;++i){left[i]=static_cast<unsigned char>(i+3);right[i]=static_cast<unsigned char>(i+101);}
    ID3D11Buffer* raw=cb.Get();context->VSSetConstantBuffers(0,1,&raw);context->PSSetConstantBuffers(3,1,&raw);
    D3D11_TEXTURE2D_DESC td{};td.Width=32;td.Height=24;td.MipLevels=td.ArraySize=td.SampleDesc.Count=1;td.Format=DXGI_FORMAT_R8G8B8A8_UNORM;td.BindFlags=D3D11_BIND_RENDER_TARGET;
    ComPtr<ID3D11Texture2D> target;ComPtr<ID3D11RenderTargetView> rtv;
    CHECK(SUCCEEDED(device->CreateTexture2D(&td,nullptr,&target)));CHECK(SUCCEEDED(device->CreateRenderTargetView(target.Get(),nullptr,&rtv)));
    ID3D11RenderTargetView* rt=rtv.Get();context->OMSetRenderTargets(1,&rt,nullptr);D3D11_VIEWPORT vp{0,0,32,24,0,1};context->RSSetViewports(1,&vp);
    const SceneWitnessKey key{1,2,3,4,5,6,7,8,9,10,0,32,24};
    D3D11SceneDrawWitness w;CHECK(!w.Enable(false));CHECK(w.Enable(true));CHECK(!w.Enable(true));CHECK(!w.Request(0));CHECK(!w.Request(2));CHECK(w.Request(1));
    std::array<std::byte,0x460> expected{};expected[17]=std::byte{0xa3};
    for(unsigned eye=0;eye<2;++eye){
        const auto& bytes=eye?right:left;context->UpdateSubresource(cb.Get(),0,nullptr,bytes.data(),0,0);
        float color[4]={eye?0.0f:1.0f,eye?1.0f:0.0f,0,1};context->ClearRenderTargetView(rtv.Get(),color);
        w.BeginEye(context.Get(),key,eye,eye?key.rightView:key.mainView,expected);
        // Identical shader/target identities never collapse separate instances.
        w.Observe(context.Get(),{SceneWitnessDrawKind::IndexedInstanced,90,2,0,1,-2});
        w.Observe(context.Get(),{SceneWitnessDrawKind::IndexedInstanced,90,3,0,7,-2});
        for(unsigned kind=0;kind<=unsigned(SceneWitnessDrawKind::Indirect);++kind)
            w.Observe(context.Get(),{SceneWitnessDrawKind(kind),30,1,0,0,0,kind>=5?123ull:0ull,kind>=5?16u:0u});
        w.EndEye(context.Get(),key,eye);w.CaptureColor(context.Get(),target.Get(),key,eye);
    }
    // Later updates must not rewrite already queued per-draw snapshots.
    std::array<unsigned char,64> later{};context->UpdateSubresource(cb.Get(),0,nullptr,later.data(),0,0);
    auto laterKey=key;++laterKey.request;++laterKey.nativeFrame;w.BeginEye(context.Get(),laterKey,0,laterKey.mainView);
    CHECK(!w.Failed());
    ComPtr<ID3D11Buffer> bound;context->VSGetConstantBuffers(0,1,&bound);CHECK(bound.Get()==cb.Get());
    ComPtr<ID3D11RenderTargetView> boundRt;context->OMGetRenderTargets(1,&boundRt,nullptr);CHECK(boundRt.Get()==rtv.Get());
    // Test owns flush/wait. Production observer only makes DO_NOT_WAIT polls.
    context->Flush();for(unsigned i=0;i<256&&!w.Completed();++i){w.Poll(context.Get());Sleep(1);}CHECK(w.Completed()==1&&w.Failed()==0);
    std::thread other([&]{w.BeginEye(context.Get(),key,0,key.mainView);w.Observe(context.Get(),{});w.Poll(context.Get());});other.join();
    CHECK(!w.Request(2,1));CHECK(w.Request(2));auto next=key;next.nativeFrame+=10;next.request+=10;
    w.BeginEye(context.Get(),next,0,next.mainView);w.EndEye(context.Get(),next,0);
    auto mismatched=next;++mismatched.space;w.BeginEye(context.Get(),mismatched,1,mismatched.rightView);CHECK(w.Failed()==2);
    CHECK(w.Request(3));next.nativeFrame+=10;next.request+=10;
    // Explicit overflow is retained even though the GPU work is bounded.
    ID3D11Buffer* nullBuffer=nullptr;context->VSSetConstantBuffers(0,1,&nullBuffer);context->PSSetConstantBuffers(3,1,&nullBuffer);
    w.BeginEye(context.Get(),next,0,next.mainView);
    for(unsigned i=0;i<SceneWitnessDrawsPerEye+2;++i)w.Observe(context.Get(),{SceneWitnessDrawKind::Vertices,3,1});
    w.EndEye(context.Get(),next,0);w.CaptureColor(context.Get(),target.Get(),next,0);
    w.BeginEye(context.Get(),next,1,next.rightView);w.EndEye(context.Get(),next,1);w.CaptureColor(context.Get(),target.Get(),next,1);
    context->Flush();for(unsigned i=0;i<256&&w.Completed()!=3;++i){w.Poll(context.Get());Sleep(1);}CHECK(w.Completed()==3);
    CHECK(!w.Request(4));CHECK(w.Stop());CHECK(w.Write(output));CHECK(!w.Write(output));
    std::ifstream metadata(output/"witness.json");const std::string json{std::istreambuf_iterator<char>(metadata),{}};
    CHECK(json.find("\"overflow\":2")!=std::string::npos);CHECK(json.find("\"instances\":2")!=std::string::npos&&json.find("\"instances\":3")!=std::string::npos);
    CHECK(json.find("\"expected_camera_valid\":1")!=std::string::npos);CHECK(json.find("\"failed\":2")!=std::string::npos);
    for(unsigned kind=0;kind<=6;++kind)CHECK(json.find("\"kind\":"+std::to_string(kind))!=std::string::npos);
    std::ifstream binary(output/"buffers.bin",std::ios::binary);const std::vector<unsigned char> captured{std::istreambuf_iterator<char>(binary),{}};
    CHECK(captured.size()==2*9*2*64);
    for(unsigned eye=0;eye<2;++eye)for(unsigned draw=0;draw<9;++draw)for(unsigned slot=0;slot<2;++slot){
        const auto offset=((eye*9+draw)*2+slot)*64;CHECK(std::memcmp(captured.data()+offset,(eye?right:left).data(),64)==0);
    }
    for(unsigned eye=0;eye<2;++eye){std::ifstream file(output/("pair-1-eye-"+std::to_string(eye)+".raw"),std::ios::binary);std::vector<unsigned char> color{std::istreambuf_iterator<char>(file),{}};
        CHECK(color.size()==32*24*4);CHECK(color[0]==(eye?0:255)&&color[1]==(eye?255:0));}
    // Exhaust the byte budget with real constant buffers. Stop must not wait
    // for pending GPU copies, and its report must identify omitted payloads.
    D3D11SceneDrawWitness budget;CHECK(budget.Enable(true)&&budget.Request(1,2));
    bd.ByteWidth=65536;ComPtr<ID3D11Buffer> large;CHECK(SUCCEEDED(device->CreateBuffer(&bd,nullptr,&large)));
    raw=large.Get();context->VSSetConstantBuffers(0,1,&raw);context->PSSetConstantBuffers(3,1,&raw);
    budget.BeginEye(context.Get(),key,0,key.mainView);
    for(unsigned i=0;i<258;++i)budget.Observe(context.Get(),{SceneWitnessDrawKind::Indexed,90,1});
    budget.EndEye(context.Get(),key,0);CHECK(budget.Stop());CHECK(budget.Write(output/"budget"));
    CHECK(std::filesystem::file_size(output/"budget"/"buffers.bin")<=SceneWitnessEyeBufferBudget);
    std::ifstream budgetFile(output/"budget"/"witness.json");const std::string budgetJson{std::istreambuf_iterator<char>(budgetFile),{}};
    CHECK(budgetJson.find("\"omitted\":1")!=std::string::npos);CHECK(budgetJson.find("\"completed\":0")!=std::string::npos);
    CHECK(budgetJson.find("\"first_draw_ordinal\":2")!=std::string::npos&&budgetJson.find("\"skipped_prefix\":2")!=std::string::npos);
    std::cout<<"scene draw witness: exact snapshots, pair mismatch, overflow and unchanged bindings passed\n";
    return 0;
}
