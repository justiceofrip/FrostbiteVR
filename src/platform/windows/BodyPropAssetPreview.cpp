// Explicit offline software preview. Reads a private converted geometry/material
// cache and writes images. No game, OpenXR, input, network or injection calls.
#include "Bc2BodyAmmoGeometryCache.h"
#include "fvr/graphics/BodyPropMaterials.h"
#include <Windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace fvr;using namespace fvr::graphics;using Microsoft::WRL::ComPtr;
namespace {
void Require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
math::Matrix4 Identity(){math::Matrix4 m{};for(unsigned i=0;i<4;++i)m.values[i][i]=1;return m;}
}
int wmain(int argc,wchar_t** argv){try {
    std::filesystem::path geometry,materials,output;std::string assetName;
    for(int i=1;i<argc;++i){const std::wstring option=argv[i];Require(i+1<argc,"Missing argument");
        if(option==L"--geometry")geometry=argv[++i];else if(option==L"--materials")materials=argv[++i];
        else if(option==L"--output")output=argv[++i];else if(option==L"--asset"){
            const std::wstring value=argv[++i];for(auto c:value){Require(c>=32&&c<127,"ASCII asset name required");assetName+=char(c);}}
        else throw std::runtime_error("Unknown preview option");}
    Require(!geometry.empty()&&!output.empty()&&!assetName.empty()&&!std::filesystem::exists(output),"Require geometry, asset and new output directory");
    const auto loaded=bc2::LoadBodyAmmoGeometryCache(geometry);Require(bool(loaded.catalog),bc2::BodyAmmoCacheStatusName(loaded.status));
    auto source=std::make_shared<BodyPropCatalog>();for(const auto& a:*loaded.catalog)source->push_back({a->key,a->sections});
    std::shared_ptr<const BodyPropCatalog> catalog=source;BodyPropMaterialResult converted;
    if(!materials.empty()){converted=LoadBodyPropMaterials(materials,*source);Require(bool(converted.catalog),converted.error);catalog=converted.catalog;}
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;D3D_FEATURE_LEVEL got{},want=D3D_FEATURE_LEVEL_11_0;
    Require(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,&want,1,D3D11_SDK_VERSION,&device,&got,&context)),"WARP device");
    constexpr unsigned Size=512;D3D11_TEXTURE2D_DESC d{};d.Width=d.Height=Size;d.MipLevels=d.ArraySize=d.SampleDesc.Count=1;
    d.Format=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;d.Usage=D3D11_USAGE_DEFAULT;d.BindFlags=D3D11_BIND_RENDER_TARGET;
    ComPtr<ID3D11Texture2D> color,depth,readback;ComPtr<ID3D11RenderTargetView> rtv;ComPtr<ID3D11DepthStencilView> dsv;
    Require(SUCCEEDED(device->CreateTexture2D(&d,nullptr,&color))&&SUCCEEDED(device->CreateRenderTargetView(color.Get(),nullptr,&rtv)),"Color target");
    d.Format=DXGI_FORMAT_D32_FLOAT;d.BindFlags=D3D11_BIND_DEPTH_STENCIL;
    Require(SUCCEEDED(device->CreateTexture2D(&d,nullptr,&depth))&&SUCCEEDED(device->CreateDepthStencilView(depth.Get(),nullptr,&dsv)),"Depth target");
    d.Format=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;d.BindFlags=0;d.Usage=D3D11_USAGE_STAGING;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    Require(SUCCEEDED(device->CreateTexture2D(&d,nullptr,&readback)),"Readback target");
    D3D11RigidPropRenderer renderer(true);Require(renderer.Initialize(context.Get()),"Prop renderer");
    std::filesystem::create_directories(output);std::ofstream report(output/L"preview.json");
    report<<"{\"software_preview\":true,\"game_or_headset_verified\":false,\"material_sections\":"<<converted.sections<<",\"images\":[";
    unsigned images=0;const auto projection=*math::MakeLhProjectionFromFovTangents({-1,1,1,-1},.05f,100);
    for(unsigned a=0;a<loaded.catalog->size();++a){if((*loaded.catalog)[a]->asset!=assetName)continue;const auto& asset=(*catalog)[a];
        Require(renderer.Upload(asset.key,asset.sections),"Asset upload");
        std::array<float,3> lo{10,10,10},hi{-10,-10,-10};unsigned textured=0;
        for(const auto& s:asset.sections){textured+=s.albedo?1:0;for(const auto& v:s.mesh.vertices){const std::array<float,3> xyz{v.x,v.y,v.z};
            for(unsigned k=0;k<3;++k){lo[k]=std::min(lo[k],xyz[k]);hi[k]=std::max(hi[k],xyz[k]);}}}
        const auto extent=std::max({hi[0]-lo[0],hi[1]-lo[1],hi[2]-lo[2]});Require(std::isfinite(extent)&&extent>.001f,"Asset bounds");
        for(unsigned view=0;view<3;++view){auto center=Identity();for(unsigned k=0;k<3;++k)center.values[3][k]=-(hi[k]+lo[k])*.5f;
            const float angle=.45f+float(view)*1.1f;auto rotate=Identity();rotate.values[0][0]=rotate.values[2][2]=std::cos(angle);
            rotate.values[0][2]=-std::sin(angle);rotate.values[2][0]=std::sin(angle);
            auto scale=Identity();for(unsigned k=0;k<3;++k)scale.values[k][k]=2/extent;scale.values[3][2]=2;
            const auto world=interaction::Multiply(interaction::Multiply(center,rotate),scale);
            const float background[]{.015f,.02f,.025f,1};context->ClearRenderTargetView(rtv.Get(),background);context->ClearDepthStencilView(dsv.Get(),D3D11_CLEAR_DEPTH,1,0);
            RigidPropEye eye{1,2,3,std::uint64_t(images)+1,5,6,7,8,9,10,0,1000000000,1100000000};
            Require(renderer.Draw(asset.key,eye,eye,eye.observedNs,world,Identity(),projection,{rtv.Get(),dsv.Get(),Size,Size,false,true}),"Draw preview");
            context->CopyResource(readback.Get(),color.Get());D3D11_MAPPED_SUBRESOURCE mapped{};Require(SUCCEEDED(context->Map(readback.Get(),0,D3D11_MAP_READ,0,&mapped)),"Read preview");
            const auto name="asset-"+std::to_string(a)+"-view-"+std::to_string(view)+".ppm";std::ofstream image(output/name,std::ios::binary);image<<"P6\n"<<Size<<' '<<Size<<"\n255\n";
            for(unsigned y=0;y<Size;++y){const auto* row=static_cast<const char*>(mapped.pData)+y*mapped.RowPitch;for(unsigned x=0;x<Size;++x)image.write(row+x*4,3);}
            context->Unmap(readback.Get(),0);Require(bool(image),"Write preview");
            if(images++)report<<',';report<<"{\"file\":\""<<name<<"\",\"asset\":"<<a<<",\"textured_sections\":"<<textured<<'}';
        }
    }
    Require(images>0,"Requested asset missing");report<<"]}\n";Require(bool(report),"Write preview metadata");context->ClearState();
    std::cout<<"Offline WARP preview: "<<images<<" images, "<<converted.sections<<" converted material sections\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
