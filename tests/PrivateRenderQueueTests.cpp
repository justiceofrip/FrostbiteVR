#include "fvr/graphics/D3D11RenderQueueLimit.h"
#include "Test.h"
#include <cstring>
#include <sstream>
using namespace fvr::graphics;
namespace {
struct Fake {
    using Device=unsigned;
    unsigned thread=7,value=3,opens=0,releases=0,reads=0,writes=0;
    HRESULT openResult=S_OK,readResult=S_OK,writeResult=S_OK;
    bool writeOnError=false;
    unsigned Thread()const noexcept{return thread;}
    HRESULT Open(Device)noexcept {++opens;return openResult;}
    HRESULT Read(unsigned& out)noexcept {++reads;if(SUCCEEDED(readResult))out=value;return readResult;}
    HRESULT Write(unsigned next)noexcept {++writes;if(SUCCEEDED(writeResult)||writeOnError)value=next;return writeResult;}
    void Release()noexcept {++releases;}
};
int Pure(){
    {Fake f;BasicRenderQueueLimit p(f);p.Configure(1,RenderQueueMode::Off);p.Close();
        CHECK(!f.opens&&!f.reads&&!f.writes&&!f.releases&&!p.Snapshot().attempted);}
    {Fake f;BasicRenderQueueLimit p(f);p.Configure(1,RenderQueueMode::Observe);p.Close();
        CHECK(f.value==3&&!f.writes&&p.Snapshot().closed&&p.Snapshot().restored&&!p.Snapshot().applied);}
    for(unsigned original:{1u,2u,3u,16u}){
        Fake f;f.value=original;BasicRenderQueueLimit p(f);p.Configure(1,RenderQueueMode::LimitOne);
        CHECK(f.value==1&&p.Snapshot().applied&&p.Snapshot().original==original);
        p.Configure(2,RenderQueueMode::Observe);CHECK(f.opens==1);
        p.Close();CHECK(f.value==original&&p.Snapshot().closed&&p.Snapshot().restored);
        CHECK(f.writes==(original==1?0u:2u));const auto reads=f.reads;p.Close();CHECK(f.reads==reads&&f.releases==1);
    }
    {Fake f;BasicRenderQueueLimit p(f);p.Configure(1,RenderQueueMode::LimitOne);f.thread=9;p.Close();
        CHECK(f.value==1&&f.writes==1&&!p.Snapshot().closed&&p.Snapshot().ownerMismatch==1);
        f.thread=7;p.Close();CHECK(f.value==3&&p.Snapshot().restored);}
    {Fake f;BasicRenderQueueLimit p(f);p.Configure(1,RenderQueueMode::LimitOne);f.value=2;p.Close();
        CHECK(f.value==2&&f.writes==1&&p.Snapshot().changedExternally&&!p.Snapshot().restored&&p.Snapshot().closed);}
    {Fake f;BasicRenderQueueLimit p(f);p.Configure(1,RenderQueueMode::LimitOne);f.value=3;p.Close();
        CHECK(f.writes==1&&p.Snapshot().restored&&!p.Snapshot().changedExternally);}
    {Fake f;f.openResult=E_NOINTERFACE;BasicRenderQueueLimit p(f);p.Configure(1,RenderQueueMode::LimitOne);p.Close();
        CHECK(!f.reads&&!f.writes&&!p.Snapshot().available&&p.Snapshot().closed);}
    for(unsigned invalid:{0u,17u}){Fake f;f.value=invalid;BasicRenderQueueLimit p(f);p.Configure(1,RenderQueueMode::LimitOne);
        CHECK(!p.Snapshot().available&&!f.writes);}
    {Fake f;f.readResult=E_FAIL;BasicRenderQueueLimit p(f);p.Configure(1,RenderQueueMode::LimitOne);p.Close();
        CHECK(!p.Snapshot().available&&!f.writes);}
    {Fake f;f.writeResult=E_FAIL;BasicRenderQueueLimit p(f);p.Configure(1,RenderQueueMode::LimitOne);p.Close();
        CHECK(f.value==3&&!p.Snapshot().applied&&p.Snapshot().restored&&f.writes==1);}
    {Fake f;f.writeResult=E_FAIL;f.writeOnError=true;BasicRenderQueueLimit p(f);p.Configure(1,RenderQueueMode::LimitOne);
        CHECK(f.value==1&&!p.Snapshot().applied);f.writeResult=S_OK;p.Close();CHECK(f.value==3&&p.Snapshot().restored);}
    {Fake f;BasicRenderQueueLimit p(f);p.Configure(1,RenderQueueMode::LimitOne);f.readResult=E_FAIL;p.Close();
        CHECK(f.value==1&&!p.Snapshot().closed&&!f.releases);f.readResult=S_OK;p.Close();CHECK(p.Snapshot().restored);}
    {Fake f;BasicRenderQueueLimit p(f);p.Configure(1,RenderQueueMode::LimitOne);f.writeResult=E_FAIL;p.Close();
        CHECK(f.value==1&&!p.Snapshot().closed);f.writeResult=S_OK;p.Close();CHECK(f.value==3&&p.Snapshot().restored);}
    {Fake f;BasicRenderQueueLimit p(f);p.Configure(1,RenderQueueMode::LimitOne);p.Close();std::ostringstream out;
        WriteRenderQueueSnapshot(out,p.Snapshot());CHECK(out.str().find("\"mode\":\"limit_one\"")!=std::string::npos);
        CHECK(out.str().find("\"restored\":true")!=std::string::npos);}
    return 0;
}
int Gpu(){
    Microsoft::WRL::ComPtr<ID3D11Device> device;Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
    CHECK(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context)));
    DxgiRenderQueueBackend b;BasicRenderQueueLimit p(b);p.Configure(device.Get(),RenderQueueMode::LimitOne);
    CHECK(p.Snapshot().available&&p.Snapshot().applied&&p.Snapshot().observed==1);
    const auto original=p.Snapshot().original;p.Close();CHECK(p.Snapshot().closed&&p.Snapshot().restored);
    Microsoft::WRL::ComPtr<IDXGIDevice1> dxgi;CHECK(SUCCEEDED(device.As(&dxgi)));
    UINT value=0;CHECK(SUCCEEDED(dxgi->GetMaximumFrameLatency(&value))&&value==original);
    std::printf("WARP queue limit restored: %u -> 1 -> %u\n",original,value);return 0;
}
}
int main(int argc,char** argv){if(argc==2&&std::strcmp(argv[1],"--gpu")==0)return Gpu();return Pure();}
