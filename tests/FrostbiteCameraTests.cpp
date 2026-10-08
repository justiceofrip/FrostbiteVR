#include "Test.h"
#include "fvr/engine/FrostbiteCamera.h"
#include "fvr/interaction/TrackingMath.h"
using namespace fvr;
int main(){
    engine::FrostbiteCameraInput source{};source.nearPlane=.1f;source.farPlane=1000;source.worldUnitsPerMeter=1;
    const float c=std::cos(.5f),s=std::sin(.5f);source.transform.values={{{c,0,-s,0},{0,1,0,0},{s,0,c,0},{12,4,-8,0}}};
    const auto canonical=engine::CanonicalCamera(source);CHECK(canonical);CHECK(Near(canonical->camera.values[3][2],8));CHECK(Near(canonical->camera.values[3][3],1));
    const math::FovTangents fov{-.7f,1.2f,.8f,-.6f};const auto projection=math::MakeLhProjectionFromFovTangents(fov,.1f,1000);CHECK(projection);
    const auto restored=engine::NativeEye({canonical->camera,*projection});CHECK(restored);
    for(unsigned row=0;row<4;++row)for(unsigned col=0;col<4;++col)CHECK(Near(restored->transform.values[row][col],source.transform.values[row][col]));
    CHECK(Near(restored->projection.values[2][3],-1));
    const auto near=math::TransformRowVector({0,0,-.1f,1},restored->projection),far=math::TransformRowVector({0,0,-1000,1},restored->projection);
    CHECK(Near(near.z/near.w,0));CHECK(Near(far.z/far.w,1));
    const auto leftEdge=math::TransformRowVector({-1.4f,0,-2,1},restored->projection),rightEdge=math::TransformRowVector({2.4f,0,-2,1},restored->projection);
    CHECK(Near(leftEdge.x/leftEdge.w,-1));CHECK(Near(rightEdge.x/rightEdge.w,1));
    math::Pose head{};head.position.y=1.7f;auto eyes=math::ComputeEyePoses(head,.064f);CHECK(eyes);
    const auto leftWorld=math::ComposeRuntimeHeadWithLhCamera(canonical->camera,head,eyes->left,1),rightWorld=math::ComposeRuntimeHeadWithLhCamera(canonical->camera,head,eyes->right,1);CHECK(leftWorld&&rightWorld);
    const auto left=engine::NativeEye({*leftWorld,*projection}),right=engine::NativeEye({*rightWorld,*projection});CHECK(left&&right);
    const float dx=right->transform.values[3][0]-left->transform.values[3][0],dz=right->transform.values[3][2]-left->transform.values[3][2];
    CHECK(Near(dx*c-dz*s,.064f));CHECK(Near(std::hypot(dx,dz),.064f));
    auto forward=head;forward.position.z=-.2f;const auto moved=math::ComposeRuntimeHeadWithLhCamera(canonical->camera,head,forward,1);CHECK(moved);const auto nativeMoved=engine::NativeEye({*moved,*projection});CHECK(nativeMoved);CHECK(Near(nativeMoved->transform.values[3][2],-8-.2f*c));
    auto bad=source;bad.transform.values[3][0]=NAN;CHECK(!engine::CanonicalCamera(bad));bad=source;bad.worldUnitsPerMeter=0;CHECK(!engine::CanonicalCamera(bad));bad=source;bad.farPlane=.01f;CHECK(!engine::CanonicalCamera(bad));
    auto invalidProjection=*projection;invalidProjection.values[0][0]=NAN;CHECK(!engine::NativeEye({canonical->camera,invalidProjection}));return 0;
}