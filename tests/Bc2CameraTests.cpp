#include "Test.h"
#include "Bc2Camera.h"
#include "Bc2PresentationCameraPolicy.h"
#include "Bc2TrackedViews.h"
#include "fvr/interaction/TrackingMath.h"
#include <cstring>
using namespace fvr;
namespace {
float F(const bc2::RenderViewCopy& c,unsigned offset){float f=0;std::memcpy(&f,c.bytes.data()+offset,4);return f;}
unsigned Dirty(const void* p){unsigned value=0;std::memcpy(&value,p,4);return value;}
void Clear(void* p,unsigned mask){const auto value=Dirty(p)&~mask;std::memcpy(p,&value,4);}
unsigned scaleCalls=0,projectionCalls=0;
void View(void* p){Clear(p,1);}
void Projection(void* p){++projectionCalls;Clear(p,2);}
void Frustum(void* p){Clear(p,8);}
void Scale(void* p){
    ++scaleCalls;
    // Observed native secondary-camera output from idle02. The native getter
    // model deliberately writes every scalar and clears only dirty bit4.
    const std::array<float,4> values{.9578142166f,2.107218981f,2.200028896f,4.840126991f};
    std::memcpy(static_cast<std::byte*>(p)+0x210,values.data(),sizeof(values));Clear(p,4);
}
void LazyScaleGetter(void* p){if(Dirty(p)&4)Scale(p);}
}
int main(){
    bc2::RenderViewCopy prototype{};prototype.bytes.fill(std::byte{0x5a});
    math::Matrix4 world{};world.values={{{1,0,0,0},{0,1,0,0},{0,0,1,0},{12,4,8,1}}};
    const math::FovTangents fov{-.7f,1.2f,.8f,-.6f};
    const auto copy=bc2::BuildRenderViewCopy(prototype,world,fov,.04f,2000);CHECK(copy);
    {
        // A live soldier anchor can outlast a presentation transition. The
        // suppression seam must never enter its gameplay adjustment in LetterBox.
        bc2::MenuPresentationLifetime lifetime;
        bc2::MenuPresentationObservation observed;
        observed.owner={0x10000,0x20000,0x2002c,0x20030,false,false,false};
        observed.state=bc2::MenuPresentationState::Playing;
        const auto playing=lifetime.Observe(observed,1'000'000'000);
        observed.state=bc2::MenuPresentationState::LetterBox;
        const auto cinematic=lifetime.Observe(observed,1'000'000'001);
        CHECK(!bc2::SameMenuPresentationLifetime(playing,cinematic));
        auto authoredWorld=world;
        authoredWorld.values={{{0,0,-1,0},{-.6f,.8f,0,0},{.8f,.6f,0,0},{12,4,8,1}}};
        const auto authored=bc2::BuildRenderViewCopy(prototype,authoredWorld,fov,.04f,2000);
        CHECK(authored);
        std::array<bc2::RenderViewCopy,2> source{*authored,*authored};
        source[1].bytes[0x200]=std::byte{0x27};
        const auto untouched=source;
        unsigned gameplayCalls=0;
        const auto gameplayAdjustment=[&]()noexcept {
            ++gameplayCalls;source[0].bytes[0x200]=std::byte{0x11};
            source[1].bytes[0x200]=std::byte{0x12};return false;
        };
        CHECK(bc2::ApplyOnFootViewBasePolicy(&cinematic,cinematic.observedNs,gameplayAdjustment));
        CHECK(gameplayCalls==0);
        for(unsigned camera=0;camera<2;++camera)CHECK(source[camera].bytes==untouched[camera].bytes);
        // Keeping the authored basis does not freeze tracking or discard eye
        // separation: the unchanged production stereo builder still composes it.
        runtime::TrackingFrame tracking;
        tracking.focused=tracking.headValid=true;
        tracking.head.position={0,.08f,0};
        tracking.eyes[0].position={-.032f,.08f,0};tracking.eyes[1].position={.032f,.08f,0};
        tracking.fov={fov,fov};
        const auto plan=bc2::BuildTrackedViews(source,tracking,1);CHECK(plan);
        std::array<engine::FrostbiteCameraInput,2> eyes{};
        for(unsigned side=0;side<2;++side){
            std::memcpy(&eyes[side].transform,plan->camera[side][0].bytes.data()+0x50,64);
            eyes[side].nearPlane=.04f;eyes[side].farPlane=2000;eyes[side].worldUnitsPerMeter=1;
            const auto actual=engine::CanonicalCamera(eyes[side]);CHECK(actual);
            for(unsigned row=0;row<3;++row)for(unsigned axis=0;axis<3;++axis)
                CHECK(Near(actual->camera.values[row][axis],authoredWorld.values[row][axis]));
            for(unsigned axis=0;axis<3;++axis)CHECK(Near(actual->camera.values[3][axis],
                authoredWorld.values[3][axis]+.08f*authoredWorld.values[1][axis]+
                (side?.032f:-.032f)*authoredWorld.values[0][axis]));
            CHECK(plan->camera[side][1].bytes==plan->camera[0][1].bytes);
            CHECK(source[side].bytes==untouched[side].bytes);
        }
        CHECK(!bc2::ApplyOnFootViewBasePolicy(&playing,playing.observedNs,gameplayAdjustment));
        CHECK(gameplayCalls==1&&source[0].bytes[0x200]==std::byte{0x11}&&source[1].bytes[0x200]==std::byte{0x12});
        // Unknown, future, original-deadline expiry and missing lifetime proof
        // must not remember the earlier LetterBox decision or hide failures.
        const auto unknown=lifetime.Observe({},1'000'000'002);
        CHECK(!bc2::ApplyOnFootViewBasePolicy(&unknown,1'000'000'002,gameplayAdjustment));
        CHECK(!bc2::ApplyOnFootViewBasePolicy(nullptr,1'000'000'002,gameplayAdjustment));
        CHECK(!bc2::ApplyOnFootViewBasePolicy(&cinematic,cinematic.observedNs-1,gameplayAdjustment));
        CHECK(!bc2::ApplyOnFootViewBasePolicy(&cinematic,cinematic.deadlineNs,gameplayAdjustment));
        auto unowned=cinematic;unowned.sceneEpoch=0;
        CHECK(!bc2::ApplyOnFootViewBasePolicy(&unowned,cinematic.observedNs,gameplayAdjustment));
        observed.state=bc2::MenuPresentationState::Playing;
        const auto resumed=lifetime.Observe(observed,1'000'000'003);
        CHECK(!bc2::SameMenuPresentationLifetime(cinematic,resumed));
        CHECK(!bc2::ApplyOnFootViewBasePolicy(&resumed,resumed.observedNs,gameplayAdjustment));
        CHECK(gameplayCalls==7);
        CHECK(bc2::ApplyOnFootViewBasePolicy(&resumed,resumed.observedNs,[]()noexcept{return true;}));
    }
    {
        auto prior=*copy;View(prior.bytes.data());Projection(prior.bytes.data());Frustum(prior.bytes.data());
        const auto written=prior;LazyScaleGetter(prior.bytes.data());
        CHECK(prior.bytes!=written.bytes); // Reproduces the omitted fourth cache.
        for(unsigned camera=0;camera<4;++camera){
            auto prepared=*copy;prepared.bytes[0x200]=std::byte(camera);
            bc2::PrecomputeCameraCaches(prepared,View,Projection,Frustum,Scale);
            CHECK(!(Dirty(prepared.bytes.data())&15));
            const auto exact=prepared;const auto calls=scaleCalls;
            LazyScaleGetter(prepared.bytes.data());CHECK(scaleCalls==calls);
            CHECK(prepared.bytes==exact.bytes); // Exact full-value restore remains valid.
            CHECK(F(prepared,0x218)==2.200028896f&&F(prepared,0x21c)==4.840126991f);
            // No masked comparison: an unrelated camera write still rejects.
            prepared.bytes[0x200]^=std::byte{1};CHECK(prepared.bytes!=exact.bytes);
        }
        auto pose=*copy;bc2::PrecomputeCameraCaches(pose,View,Projection,Frustum,Scale);
        const auto exact=pose;const auto calls=scaleCalls,projections=projectionCalls;
        bc2::PrecomputeCameraCaches(pose,View,Projection,Frustum,Scale,false);
        CHECK(pose.bytes==exact.bytes&&scaleCalls==calls&&projectionCalls==projections);
        auto inherited=*copy;Clear(inherited.bytes.data(),2);
        bc2::PrecomputeCameraCaches(inherited,View,Projection,Frustum,Scale,false);
        CHECK(!(Dirty(inherited.bytes.data())&15)&&scaleCalls==calls+1&&projectionCalls==projections);
        CHECK(copy->bytes==bc2::BuildRenderViewCopy(prototype,world,fov,.04f,2000)->bytes);
    }
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
