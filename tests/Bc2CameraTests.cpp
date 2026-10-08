#include "Test.h"
#include "Bc2Camera.h"
#include "fvr/interaction/TrackingMath.h"
#include <cstring>
using namespace fvr;
namespace {
float F(const bc2::RenderViewCopy& c,unsigned offset){float f=0;std::memcpy(&f,c.bytes.data()+offset,4);return f;}
}
int main(){
    bc2::RenderViewCopy prototype{};prototype.bytes.fill(std::byte{0x5a});
    math::Matrix4 world{};world.values={{{1,0,0,0},{0,1,0,0},{0,0,1,0},{12,4,8,1}}};
    const math::FovTangents fov{-.7f,1.2f,.8f,-.6f};
    const auto copy=bc2::BuildRenderViewCopy(prototype,world,fov,.04f,2000);CHECK(copy);
    CHECK(copy->bytes[8]==std::byte{1});CHECK(Near(F(*copy,0x50+0x38),-8));CHECK(Near(F(*copy,0x50+0x3c),0));
    // Reconstruct the boundaries implied by the parameters, then project the
    // original frustum edges. Native-engine verification is a separate probe.
    const float halfH=std::tan(F(*copy,0x10)*.5f),halfW=halfH*F(*copy,0x24);
    const math::FovTangents recovered{
        halfW*(2*F(*copy,0x3c)-1),halfW*(2*F(*copy,0x3c)+1),
        halfH*(2*F(*copy,0x40)+1),halfH*(2*F(*copy,0x40)-1)};
    const auto p=math::MakeLhProjectionFromFovTangents(recovered,.04f,2000);CHECK(p);
    for(float z:{.04f,1.f,100.f,2000.f})for(float x:{fov.left,fov.right})for(float y:{fov.down,fov.up}){
        auto clip=math::TransformRowVector({x*z,y*z,z,1},*p);
        CHECK(Near(clip.x/clip.w,x==fov.left?-1.f:1.f));CHECK(Near(clip.y/clip.w,y==fov.down?-1.f:1.f));
    }
    // Untouched native fields, all cache bytes and all padding outside the
    // transform survive construction. Prototype itself is immutable.
    for(unsigned i=0;i<prototype.bytes.size();++i){
        CHECK(prototype.bytes[i]==std::byte{0x5a});
        const bool changed=i<9||(i>=0xc&&i<0x18)||(i>=0x1c&&i<0x28)||
            (i>=0x3c&&i<0x4c)||(i>=0x50&&i<0x90);
        if(!changed)CHECK(copy->bytes[i]==prototype.bytes[i]);
    }
    CHECK(!bc2::BuildRenderViewCopy(prototype,world,{0,0,1,-1},.1f,100));
    CHECK(!bc2::BuildRenderViewCopy(prototype,world,{-1,1,NAN,-1},.1f,100));
    CHECK(!bc2::BuildRenderViewCopy(prototype,world,fov,0,100));
    CHECK(!bc2::BuildRenderViewCopy(prototype,world,fov,100,1));
    const auto poseOnly=bc2::BuildTransformCopy(prototype,world);CHECK(poseOnly);
    for(unsigned i=4;i<prototype.bytes.size();++i)if(i<0x50||i>=0x90)CHECK(poseOnly->bytes[i]==prototype.bytes[i]);
    math::StereoCullEnvelope envelope{world,fov,.04f,2000.f};
    const auto cull=bc2::BuildCullingViewCopy(prototype,envelope);CHECK(cull);
    CHECK(F(*cull,0x3c)==0&&F(*cull,0x40)==0);
    const float cullHeight=std::tan(F(*cull,0x10)*.5f),cullWidth=cullHeight*F(*cull,0x24);
    CHECK(cullHeight+.000001f>=fov.up&&cullHeight+.000001f>=-fov.down&&cullWidth+.000001f>=fov.right&&cullWidth+.000001f>=-fov.left);
    envelope.fov={-1,1,1.5573f,-1.5573f};CHECK(bc2::BuildCullingViewCopy(prototype,envelope));
    envelope.fov={-1,1,1.5575f,-1.5575f};CHECK(!bc2::BuildCullingViewCopy(prototype,envelope));
    // A projection alone can support the wider angle; native culling cannot.
    CHECK(bc2::BuildRenderViewCopy(prototype,world,envelope.fov,.04f,2000));
    envelope.fov.up=NAN;CHECK(!bc2::BuildCullingViewCopy(prototype,envelope));
    world.values[1][1]=2;CHECK(!bc2::BuildTransformCopy(prototype,world));CHECK(!bc2::BuildRenderViewCopy(prototype,world,fov,.1f,100));
    return 0;
}
