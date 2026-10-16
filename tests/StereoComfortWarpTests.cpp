// Software-only pixel acceptance. No game, headset, runtime or native input.
#include "fvr/graphics/D3D11StereoComfort.h"
#include "Test.h"
#include <Windows.h>
#include <wrl/client.h>
#include <array>
#include <cstring>
#include <iostream>
#include <vector>
using namespace fvr::graphics;using Microsoft::WRL::ComPtr;
namespace {
constexpr unsigned Size=32;constexpr std::uint32_t Black=0xff000000,White=0xffffffff,Sentinel=0xff385b87;
struct Image {ComPtr<ID3D11Texture2D> texture;ComPtr<ID3D11RenderTargetView> target;};
bool Make(ID3D11Device* device,Image& image,DXGI_FORMAT format,unsigned size=Size,unsigned array=1,unsigned samples=1,unsigned bind=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE,DXGI_FORMAT targetFormat=DXGI_FORMAT_UNKNOWN){
    D3D11_TEXTURE2D_DESC d{};d.Width=d.Height=size;d.Format=format;d.MipLevels=1;d.ArraySize=array;d.SampleDesc.Count=samples;d.BindFlags=bind;
    if(FAILED(device->CreateTexture2D(&d,nullptr,&image.texture)))return false;
    D3D11_RENDER_TARGET_VIEW_DESC view{};view.Format=targetFormat;view.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2D;
    return !(bind&D3D11_BIND_RENDER_TARGET)||SUCCEEDED(device->CreateRenderTargetView(image.texture.Get(),targetFormat==DXGI_FORMAT_UNKNOWN?nullptr:&view,&image.target));
}
void Fill(ID3D11DeviceContext* context,const Image& image,const std::vector<std::uint32_t>& pixels){context->UpdateSubresource(image.texture.Get(),0,nullptr,pixels.data(),Size*4,0);}
void Fill(ID3D11DeviceContext* context,const Image& image,std::uint32_t pixel){Fill(context,image,std::vector<std::uint32_t>(Size*Size,pixel));}
std::vector<std::uint32_t> Read(ID3D11Device* device,ID3D11DeviceContext* context,const Image& image){
    D3D11_TEXTURE2D_DESC d{};image.texture->GetDesc(&d);d.Usage=D3D11_USAGE_STAGING;d.BindFlags=0;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;if(FAILED(device->CreateTexture2D(&d,nullptr,&staging)))return {};
    context->CopyResource(staging.Get(),image.texture.Get());D3D11_MAPPED_SUBRESOURCE map{};
    if(FAILED(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&map)))return {};
    std::vector<std::uint32_t> pixels(d.Width*d.Height);
    for(unsigned y=0;y<d.Height;++y)std::memcpy(pixels.data()+y*d.Width,static_cast<const char*>(map.pData)+y*map.RowPitch,d.Width*4);
    context->Unmap(staging.Get(),0);return pixels;
}
bool All(const std::vector<std::uint32_t>& pixels,std::uint32_t expected){if(pixels.size()!=Size*Size)return false;for(auto v:pixels)if(v!=expected)return false;return true;}
int Format(ID3D11Device* device,ID3D11DeviceContext* context,DXGI_FORMAT format){
    D3D11StereoComfort guard;CHECK(guard.Initialize(context));CHECK(!guard.Initialize(context));
    std::array<Image,2> source,output;for(unsigned eye=0;eye<2;++eye){CHECK(Make(device,source[eye],format));CHECK(Make(device,output[eye],format));}
    const std::array<TextureSlice,2> input{{{source[0].texture.Get(),0},{source[1].texture.Get(),0}}};
    const std::array<ID3D11RenderTargetView*,2> targets{output[0].target.Get(),output[1].target.Get()};
    auto* bound=output[1].target.Get();context->OMSetRenderTargets(1,&bound,nullptr);
    CHECK(!guard.Present(targets));
    // First mismatch, reversal, dark/bright agreement and immediate normal recovery.
    const std::array<std::array<std::uint32_t,4>,7> sequence{{
        {Black,White,Black,Black},{White,Black,Black,Black},
        {Black,Black,Black,Black},{White,White,White,White},
        {0xff28486a,0xff37597b,0xff28486a,0xff37597b},
        {Black,White,Black,Black},{0xff315171,0xff416181,0xff315171,0xff416181}}};
    for(const auto& row:sequence){for(unsigned eye=0;eye<2;++eye){Fill(context,source[eye],row[eye]);Fill(context,output[eye],Sentinel);}
        CHECK(guard.Prepare(input));CHECK(guard.Present(targets));
        for(unsigned eye=0;eye<2;++eye)CHECK(All(Read(device,context,output[eye]),row[eye+2]));
        ComPtr<ID3D11RenderTargetView> restored;context->OMGetRenderTargets(1,&restored,nullptr);CHECK(restored.Get()==bound);
        CHECK(!guard.Present(targets));
    }
    const bool srgb=format==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB||format==DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
    const auto gray=[](unsigned n){return 0xff000000u|n|(n<<8)|(n<<16);};
    const auto dark=gray(srgb?15:1),bright=gray(srgb?244:230);
    Fill(context,source[0],dark);Fill(context,source[1],bright);
    CHECK(guard.Prepare(input)&&guard.Present(targets));for(const auto& image:output)CHECK(All(Read(device,context,image),Black));
    // First code values outside each threshold do not trigger the safeguard.
    Fill(context,source[0],gray(srgb?16:2));
    CHECK(guard.Prepare(input)&&guard.Present(targets));CHECK(All(Read(device,context,output[1]),bright));
    Fill(context,source[0],dark);Fill(context,source[1],gray(srgb?243:229));
    CHECK(guard.Prepare(input)&&guard.Present(targets));CHECK(All(Read(device,context,output[0]),dark));
    // Normal images, including all 256 channel values, retain every pixel.
    std::array<std::vector<std::uint32_t>,2> patterns;
    for(unsigned eye=0;eye<2;++eye){patterns[eye].resize(Size*Size);for(unsigned n=0;n<Size*Size;++n)
        patterns[eye][n]=0xff000000u|((n+eye*23)&255)|(((n*3)&255)<<8)|(((n*7)&255)<<16);
        Fill(context,source[eye],patterns[eye]);}
    CHECK(guard.Prepare(input)&&guard.Present(targets));for(unsigned eye=0;eye<2;++eye)CHECK(Read(device,context,output[eye])==patterns[eye]);
    // A sampled non-extreme pixel prevents this deliberately narrow safeguard.
    auto mixed=std::vector<std::uint32_t>(Size*Size,Black);mixed[Size+1]=0xff808080;
    Fill(context,source[0],mixed);Fill(context,source[1],White);
    CHECK(guard.Prepare(input)&&guard.Present(targets));CHECK(Read(device,context,output[0])==mixed);CHECK(All(Read(device,context,output[1]),White));
    // Classification precedes host props and must govern both final images.
    Fill(context,source[0],Black);Fill(context,source[1],White);CHECK(guard.Prepare(input));
    Fill(context,source[0],Sentinel);Fill(context,source[1],Sentinel);
    CHECK(guard.Present(targets));for(const auto& image:output)CHECK(All(Read(device,context,image),Black));
    // Invalid second output cannot partially replace the first or retain a flag.
    for(auto& image:output)Fill(context,image,Sentinel);
    CHECK(guard.Prepare(input));CHECK(!guard.Present({targets[0],nullptr}));CHECK(!guard.Present(targets));
    for(const auto& image:output)CHECK(All(Read(device,context,image),Sentinel));
    CHECK(guard.Prepare(input));CHECK(!guard.Present({targets[0],targets[0]}));
    CHECK(guard.Prepare(input));CHECK(!guard.Present({targets[0],source[1].target.Get()}));
    for(unsigned fault=0;fault<8;++fault){CHECK(guard.Prepare(input));auto bad=input;Image other;
        if(fault==0)bad[1].texture=nullptr;
        if(fault==1)bad[1].subresource=1;
        if(fault==2)bad[1]=bad[0];
        if(fault>=3){const auto otherFormat=fault==4?(format==DXGI_FORMAT_R8G8B8A8_UNORM?DXGI_FORMAT_B8G8R8A8_UNORM:DXGI_FORMAT_R8G8B8A8_UNORM):format;
            CHECK(Make(device,other,otherFormat,fault==3?16:Size,fault==5?2:1,fault==6?2:1,
                fault==7?D3D11_BIND_RENDER_TARGET:D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE));bad[1].texture=other.texture.Get();
        }
        CHECK(!guard.Prepare(bad));CHECK(!guard.Present(targets));
    }
    Image undersized;CHECK(Make(device,undersized,format,16));CHECK(guard.Prepare(input));CHECK(!guard.Present({targets[0],undersized.target.Get()}));
    ComPtr<ID3D11Device> foreignDevice;ComPtr<ID3D11DeviceContext> foreignContext;D3D_FEATURE_LEVEL level{};
    CHECK(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&foreignDevice,&level,&foreignContext)));
    Image foreign;CHECK(Make(foreignDevice.Get(),foreign,format));
    CHECK(guard.Prepare(input));CHECK(!guard.Prepare({input[0],{foreign.texture.Get(),0}}));CHECK(!guard.Present(targets));
    CHECK(guard.Prepare(input));CHECK(!guard.Present({targets[0],foreign.target.Get()}));
    // XR may expose typeless backing storage for the requested typed format.
    Image typeless;CHECK(Make(device,typeless,CopyCompatibleFormats(format,28)?DXGI_FORMAT_R8G8B8A8_TYPELESS:DXGI_FORMAT_B8G8R8A8_TYPELESS,
        Size,1,1,D3D11_BIND_RENDER_TARGET,format));
    CHECK(guard.Prepare(input)&&guard.Present({targets[0],typeless.target.Get()}));CHECK(All(Read(device,context,typeless),Sentinel));
    // A fresh valid pair resumes immediately after all rejection paths.
    Fill(context,source[0],0xff224466);Fill(context,source[1],0xff335577);
    CHECK(guard.Prepare(input)&&guard.Present(targets));CHECK(All(Read(device,context,output[0]),0xff224466));CHECK(All(Read(device,context,output[1]),0xff335577));
    context->ClearState();return 0;
}
}
int main(){
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;D3D_FEATURE_LEVEL level{};
    CHECK(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,&level,&context)));
    for(auto f:{DXGI_FORMAT_R8G8B8A8_UNORM,DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,DXGI_FORMAT_B8G8R8A8_UNORM,DXGI_FORMAT_B8G8R8A8_UNORM_SRGB})CHECK(Format(device.Get(),context.Get(),f)==0);
    std::cout<<"Stereo comfort WARP: four formats, paired extremes, unchanged pixels, recovery, invalid resources and exact context restoration passed\n";return 0;
}
