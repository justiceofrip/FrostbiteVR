// Explicit Windows/WARP target only; no game, XR, file output or native hooks.
// Link the isolated tools/debug/D3D11SceneDrawPayloadWitness.cpp with d3d11.lib.
#include "../tools/debug/D3D11SceneDrawPayloadWitness.h"
#include "Test.h"
#include <Windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <array>
#include <cstring>
#include <initializer_list>
#include <memory>
#include <thread>
using Microsoft::WRL::ComPtr;
using namespace fvr::debug;
using namespace fvr::graphics;
namespace {
std::uint64_t Id(const void* p){return reinterpret_cast<std::uintptr_t>(p);}
ComPtr<ID3D11Buffer> Buffer(ID3D11Device* d,unsigned bind,unsigned misc=0,unsigned stride=0){
    D3D11_BUFFER_DESC desc{};desc.ByteWidth=256;desc.Usage=D3D11_USAGE_DEFAULT;
    desc.BindFlags=bind;desc.MiscFlags=misc;desc.StructureByteStride=stride;
    ComPtr<ID3D11Buffer> out;if(FAILED(d->CreateBuffer(&desc,nullptr,&out)))return {};return out;
}
bool Wait(D3D11SceneDrawPayloadWitness& witness,ID3D11DeviceContext* context,PayloadResult& out){
    // Test harness owns its WARP context. Production helper never Flushes.
    context->Flush();const auto deadline=GetTickCount64()+3000;
    do{witness.Poll(context);if(witness.Snapshot(context,out))return true;Sleep(1);}while(GetTickCount64()<deadline);
    return false;
}
}
int main(){
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;D3D_FEATURE_LEVEL level{};
    CHECK(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,&level,&context)));
    auto vertex=Buffer(device.Get(),D3D11_BIND_VERTEX_BUFFER),index=Buffer(device.Get(),D3D11_BIND_INDEX_BUFFER);
    auto structured=Buffer(device.Get(),D3D11_BIND_SHADER_RESOURCE,D3D11_RESOURCE_MISC_BUFFER_STRUCTURED,16);
    auto indirect=Buffer(device.Get(),0,D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS);
    CHECK(vertex&&index&&structured&&indirect);
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};srvDesc.Format=DXGI_FORMAT_UNKNOWN;srvDesc.ViewDimension=D3D11_SRV_DIMENSION_BUFFER;
    srvDesc.Buffer.FirstElement=2;srvDesc.Buffer.NumElements=6;ComPtr<ID3D11ShaderResourceView> srv;
    CHECK(SUCCEEDED(device->CreateShaderResourceView(structured.Get(),&srvDesc,&srv)));
    unsigned stride=16,offset=16;ID3D11Buffer* boundVertex=vertex.Get();ID3D11ShaderResourceView* boundSrv=srv.Get();
    context->IASetVertexBuffers(3,1,&boundVertex,&stride,&offset);
    context->IASetIndexBuffer(index.Get(),DXGI_FORMAT_R16_UINT,8);context->VSSetShaderResources(7,1,&boundSrv);
    PayloadSelection plan;plan.ordinal={17,19};plan.count=4;
    for(auto& a:plan.expected){a.kind=SceneWitnessDrawKind::IndexedIndirect;a.argumentOffset=16;}
    plan.ranges[0]={PayloadSource::Vertex,PayloadStage::Vertex,3,16,32};
    plan.ranges[1]={PayloadSource::Index,PayloadStage::Vertex,0,0,16};
    plan.ranges[2]={PayloadSource::ShaderResource,PayloadStage::Vertex,7,16,32};
    plan.ranges[3]={PayloadSource::Indirect,PayloadStage::Vertex,0,0,20};
    SceneWitnessKey key{1,2,3,4,5,6,7,8,9,10,0,1920,1080,11,12};
    D3D11SceneDrawPayloadWitness witness(plan);CHECK(witness.Valid());
    std::array<unsigned char,256> a{},b{};
    for(unsigned n=0;n<a.size();++n){a[n]=static_cast<unsigned char>(n);b[n]=static_cast<unsigned char>(255-n);}
    auto args=plan.expected[0];args.argumentBuffer=Id(indirect.Get());
    for(unsigned eye=0;eye<2;++eye){const auto& data=eye?b:a;
        for(auto* buffer:{vertex.Get(),index.Get(),structured.Get(),indirect.Get()})context->UpdateSubresource(buffer,0,nullptr,data.data(),0,0);
        CHECK(!witness.Observe(context.Get(),key,eye,eye?key.rightView:key.mainView,plan.ordinal[eye]-1,args,indirect.Get()));
        CHECK(witness.Observe(context.Get(),key,eye,eye?key.rightView:key.mainView,plan.ordinal[eye],args,indirect.Get()));
        CHECK(witness.EndEye(context.Get(),key,eye,eye?key.rightView:key.mainView));
    }
    // A subsequent update must not replace bytes already queued for either eye.
    std::array<unsigned char,256> overwritten{};
    for(auto* buffer:{vertex.Get(),index.Get(),structured.Get(),indirect.Get()})context->UpdateSubresource(buffer,0,nullptr,overwritten.data(),0,0);
    auto result=std::make_unique<PayloadResult>();CHECK(Wait(witness,context.Get(),*result));
    CHECK(!result->identityFailure&&!result->shapeFailure&&!result->missingDraw);
    const std::array<unsigned,4> expectedOffsets{32,8,48,16};
    for(unsigned eye=0;eye<2;++eye)for(unsigned n=0;n<plan.count;++n){const auto& r=result->eyes[eye].records[n];
        CHECK(r.status==PayloadStatus::Ready);CHECK(r.copiedOffset==expectedOffsets[n]);
        CHECK(std::memcmp(result->bytes.data()+r.payloadOffset,(eye?b:a).data()+r.copiedOffset,r.copiedBytes)==0);
    }
    CHECK(result->eyes[0].records[2].firstElement==2&&result->eyes[0].records[2].elements==6);
    CHECK(result->eyes[0].records[2].srvDescriptorBytes==sizeof(D3D11_SHADER_RESOURCE_VIEW_DESC));
    ComPtr<ID3D11Buffer> stillVertex;unsigned stillStride=0,stillOffset=0;
    context->IAGetVertexBuffers(3,1,&stillVertex,&stillStride,&stillOffset);
    ComPtr<ID3D11ShaderResourceView> stillSrv;context->VSGetShaderResources(7,1,&stillSrv);
    CHECK(stillVertex.Get()==vertex.Get()&&stillStride==stride&&stillOffset==offset&&stillSrv.Get()==srv.Get());
    CHECK(witness.CloseOnGraphicsThread(context.Get()));
    // A different source epoch cannot supply the missing right eye.
    D3D11SceneDrawPayloadWitness wrongPair(plan);
    CHECK(wrongPair.Observe(context.Get(),key,0,key.mainView,17,args,indirect.Get()));
    CHECK(wrongPair.EndEye(context.Get(),key,0,key.mainView));
    auto changed=key;++changed.nativeRequest;
    CHECK(!wrongPair.Observe(context.Get(),changed,1,changed.rightView,19,args,indirect.Get()));
    CHECK(wrongPair.CloseOnGraphicsThread(context.Get()));CHECK(wrongPair.Snapshot(context.Get(),*result));
    CHECK(result->identityFailure&&!result->eyes[1].captured);
    // Wrong-thread calls must not touch the D3D context or consume a draw.
    D3D11SceneDrawPayloadWitness owner(plan);CHECK(owner.Observe(context.Get(),key,0,key.mainView,17,args,indirect.Get()));
    bool foreign=true;std::thread other([&]{foreign=owner.EndEye(context.Get(),key,0,key.mainView);});other.join();CHECK(!foreign);
    CHECK(owner.EndEye(context.Get(),key,0,key.mainView));CHECK(owner.CloseOnGraphicsThread(context.Get()));
    CHECK(owner.Snapshot(context.Get(),*result));CHECK(result->wrongThread==1&&!result->identityFailure);
    // Missing ranges are explicit outcomes; bytes cannot be called complete.
    auto limited=plan;limited.ranges[2].offset=80;limited.ranges[2].bytes=32; // View is only96 bytes.
    D3D11SceneDrawPayloadWitness bounds(limited);CHECK(bounds.Observe(context.Get(),key,0,key.mainView,17,args,indirect.Get()));
    CHECK(bounds.CloseOnGraphicsThread(context.Get()));CHECK(bounds.Snapshot(context.Get(),*result));
    CHECK(result->eyes[0].records[2].status==PayloadStatus::InvalidRange);
    D3D11SceneDrawPayloadWitness missing(plan);CHECK(!missing.EndEye(context.Get(),key,0,key.mainView));
    CHECK(missing.CloseOnGraphicsThread(context.Get()));CHECK(missing.Snapshot(context.Get(),*result));CHECK(result->missingDraw);
    auto wrongArgs=args;++wrongArgs.argumentOffset;
    D3D11SceneDrawPayloadWitness shape(plan);CHECK(!shape.Observe(context.Get(),key,0,key.mainView,17,wrongArgs,indirect.Get()));
    CHECK(shape.CloseOnGraphicsThread(context.Get()));CHECK(shape.Snapshot(context.Get(),*result));CHECK(result->shapeFailure);
    auto optional=plan;optional.count=1;optional.ranges[0]={PayloadSource::ShaderResource,PayloadStage::Pixel,127,0,4};
    D3D11SceneDrawPayloadWitness absent(optional);CHECK(absent.Observe(context.Get(),key,0,key.mainView,17,args));
    CHECK(absent.CloseOnGraphicsThread(context.Get()));CHECK(absent.Snapshot(context.Get(),*result));
    CHECK(result->eyes[0].records[0].status==PayloadStatus::Absent);
    D3D11_TEXTURE2D_DESC textureDesc{};textureDesc.Width=textureDesc.Height=textureDesc.MipLevels=textureDesc.ArraySize=1;
    textureDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;textureDesc.SampleDesc.Count=1;
    textureDesc.Usage=D3D11_USAGE_DEFAULT;textureDesc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    ComPtr<ID3D11Texture2D> texture;ComPtr<ID3D11ShaderResourceView> textureSrv;
    CHECK(SUCCEEDED(device->CreateTexture2D(&textureDesc,nullptr,&texture)));
    CHECK(SUCCEEDED(device->CreateShaderResourceView(texture.Get(),nullptr,&textureSrv)));
    ID3D11ShaderResourceView* textureBinding=textureSrv.Get();context->PSSetShaderResources(127,1,&textureBinding);
    D3D11SceneDrawPayloadWitness unsupported(optional);CHECK(unsupported.Observe(context.Get(),key,0,key.mainView,17,args));
    CHECK(unsupported.CloseOnGraphicsThread(context.Get()));CHECK(unsupported.Snapshot(context.Get(),*result));
    CHECK(result->eyes[0].records[0].status==PayloadStatus::UnsupportedView);
    ComPtr<ID3D11Resource> textureResource;textureSrv->GetResource(&textureResource);
    CHECK(result->eyes[0].records[0].resource==Id(textureResource.Get())&&result->eyes[0].records[0].copiedBytes==0);
    CHECK(result->eyes[0].records[0].srvDescriptorBytes==sizeof(D3D11_SHADER_RESOURCE_VIEW_DESC));
    // Even a successful Map would not prove CopySubresourceRegion executed
    // while native predication was bound. Refuse both predicate polarities.
    context->SetPredication(nullptr,FALSE);
    D3D11_QUERY_DESC predicateDesc{D3D11_QUERY_OCCLUSION_PREDICATE,0};ComPtr<ID3D11Predicate> predicate;
    CHECK(SUCCEEDED(device->CreatePredicate(&predicateDesc,&predicate)));
    context->Begin(predicate.Get());context->End(predicate.Get());context->Flush();
    BOOL visible=TRUE;HRESULT predicateResult=S_FALSE;const auto predicateDeadline=GetTickCount64()+3000;
    do{predicateResult=context->GetData(predicate.Get(),&visible,sizeof(visible),D3D11_ASYNC_GETDATA_DONOTFLUSH);
       if(predicateResult!=S_FALSE)break;Sleep(1);}while(GetTickCount64()<predicateDeadline);
    CHECK(predicateResult==S_OK&&visible==FALSE);
    for(const BOOL polarity:{FALSE,TRUE}){
        context->SetPredication(predicate.Get(),polarity);
        D3D11SceneDrawPayloadWitness predicated(plan);
        CHECK(predicated.Observe(context.Get(),key,0,key.mainView,17,args,indirect.Get()));
        predicated.Poll(context.Get());CHECK(predicated.EndEye(context.Get(),key,0,key.mainView));
        CHECK(predicated.CloseOnGraphicsThread(context.Get()));CHECK(predicated.Snapshot(context.Get(),*result));
        CHECK(result->eyes[0].predicate==Id(predicate.Get())&&result->eyes[0].predicateValue==(polarity!=FALSE));
        for(unsigned n=0;n<plan.count;++n){const auto& r=result->eyes[0].records[n];
            CHECK(r.status==PayloadStatus::PredicatedNotCopied&&r.copiedBytes==0);}
        ComPtr<ID3D11Predicate> retained;BOOL retainedPolarity=FALSE;context->GetPredication(&retained,&retainedPolarity);
        CHECK(retained.Get()==predicate.Get()&&retainedPolarity==polarity);
    }
    context->SetPredication(nullptr,FALSE);
    context->ClearState();return 0;
}
