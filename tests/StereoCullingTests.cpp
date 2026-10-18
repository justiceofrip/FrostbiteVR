#include "Test.h"
#include "fvr/math/StereoCulling.h"
#include "fvr/interaction/TrackingMath.h"
#include <limits>
using namespace fvr;
math::Matrix4 Identity(){math::Matrix4 m{};for(unsigned i=0;i<4;++i)m.values[i][i]=1;return m;}
bool Inside(const math::StereoCullEnvelope& volume,const math::Vec4& world){
    const auto inverse=interaction::InverseAnimatedTransform(volume.world);if(!inverse)return false;
    const auto p=math::TransformRowVector(world,*inverse);
    return p.z>=volume.nearPlane&&p.z<=volume.farPlane&&p.x>=volume.fov.left*p.z&&p.x<=volume.fov.right*p.z&&p.y>=volume.fov.down*p.z&&p.y<=volume.fov.up*p.z;
}
int FloatPlaneEvidence(){
    // Retained416 loading-camera position. The old fixed-only comparison failed
    // two cases with a 2-ULP residual; do not replace it with a larger fixed mm
    // tolerance that would hide the same residual near the coordinate origin.
    constexpr float origin=9999.f;
    const float ulp=std::nextafter(origin,std::numeric_limits<float>::infinity())-origin;
    CHECK(ulp==.0009765625f);
    auto translated=math::MeasureFloatPlaneContainment({-1,0,0,origin},{origin-2*ulp,origin,origin,1});
    CHECK(translated.valid&&translated.inside&&translated.distance==.001953125&&translated.excess==0);
    constexpr double u=std::numeric_limits<float>::epsilon()*.5;
    const double analytic=(6*u)/(1-6*u)*(double(origin-2*ulp)+origin);
    CHECK(translated.roundoffAllowance>=analytic&&translated.roundoffAllowance<analytic+1e-8);
    const auto small=math::MeasureFloatPlaneContainment({-1,0,0,0},{-2*ulp,0,0,1});
    CHECK(small.valid&&!small.inside&&small.distance==translated.distance&&small.excess>.0009);
    CHECK(small.roundoffAllowance<1e-8);
    auto outside=math::MeasureFloatPlaneContainment({-1,0,0,origin},{origin-.05f,origin,origin,1});
    CHECK(outside.valid&&!outside.inside&&outside.excess>.04);
    // Rotated plane at a large coordinate: cancellation is bounded, while
    // geometry materially outside remains rejected.
    const float nx=std::cos(.7f),nz=std::sin(.7f),w=-(nx*origin+nz*origin);
    const math::Vec4 plane{nx,0,nz,w};
    CHECK(math::MeasureFloatPlaneContainment(plane,{origin,origin,origin,1}).inside);
    CHECK(math::MeasureFloatPlaneContainment(plane,{origin-nx,origin,origin-nz,1}).inside);
    outside=math::MeasureFloatPlaneContainment(plane,{origin+.1f*nx,origin,origin+.1f*nz,1});
    CHECK(outside.valid&&!outside.inside&&outside.excess>.08);
    // A handedness or orientation error is not a rounding discrepancy.
    CHECK(math::MeasureFloatPlaneContainment({0,0,-1,origin},{0,0,origin+1,1}).inside);
    CHECK(!math::MeasureFloatPlaneContainment({0,0,1,origin},{0,0,origin+1,1}).inside);
    CHECK(math::MeasureFloatPlaneContainment({0,0,1,-origin},{origin+1,0,origin-1,1}).inside);
    CHECK(!math::MeasureFloatPlaneContainment({1,0,0,-origin},{origin+1,0,origin-1,1}).inside);
    CHECK(!math::MeasureFloatPlaneContainment({NAN,0,0,0},{0,0,0,1}).valid);
    CHECK(!math::MeasureFloatPlaneContainment({1,0,0,0},{INFINITY,0,0,1}).valid);
    CHECK(!math::MeasureFloatPlaneContainment({1,0,0,0},{0,0,0,0}).valid);
    CHECK(!math::MeasureFloatPlaneContainment({1,0,0,0},{0,0,0,1},-1).valid);
    CHECK(!math::MeasureFloatPlaneContainment({1,0,0,0},{0,0,0,1},INFINITY).valid);
    CHECK(!math::MeasureFloatPlaneContainment({std::numeric_limits<float>::max(),0,0,0},{2,0,0,1}).valid);
    return 0;
}
int main(){
    CHECK(FloatPlaneEvidence()==0);
    auto center=Identity();std::array<math::Matrix4,2> eyes{center,center};eyes[0].values[3][0]=-.032f;eyes[1].values[3][0]=.032f;
    std::array<math::FovTangents,2> fov{math::FovTangents{-1,1,1,-1},math::FovTangents{-1,1,1,-1}};
    auto envelope=math::EncloseStereoFrusta(center,eyes,fov,.04f,300);CHECK(envelope);
    // Analytic near-edge requirement: tan=1 + half-IPD/near = 1.8. A mono cone
    // would drop close geometry visible only to the outer edge of either eye.
    CHECK(envelope->fov.right>=1.8f&&envelope->fov.right<1.801f);CHECK(envelope->nearPlane<.04f&&envelope->farPlane>300);
    CHECK(Inside(*envelope,{.072f,.04f,.04f,1}));CHECK(Inside(*envelope,{-.072f,-.04f,.04f,1}));
    // Canted eye bases and off-center FOVs under a translated/turned game camera.
    center.values[0][0]=0;center.values[0][2]=-1;center.values[2][0]=1;center.values[2][2]=0;center.values[3]={140,3,-80,1};
    for(unsigned eye=0;eye<2;++eye){auto local=Identity();const float angle=eye?.08f:-.08f;local.values[0][0]=local.values[2][2]=std::cos(angle);local.values[0][2]=-std::sin(angle);local.values[2][0]=std::sin(angle);local.values[3][0]=eye?.032f:-.032f;local.values[3][2]=.008f;eyes[eye]=interaction::Multiply(local,center);}
    fov={math::FovTangents{-1.1f,.8f,.9f,-1.2f},math::FovTangents{-.8f,1.1f,1.2f,-.9f}};envelope=math::EncloseStereoFrusta(center,eyes,fov,.08f,100);CHECK(envelope);
    for(unsigned eye=0;eye<2;++eye)for(float depth:{.08f,.2f,1.f,17.f,100.f})for(float u:{0.f,.25f,.5f,.75f,1.f})for(float v:{0.f,.25f,.5f,.75f,1.f}){
        const auto x=fov[eye].left+(fov[eye].right-fov[eye].left)*u,y=fov[eye].down+(fov[eye].up-fov[eye].down)*v;
        const auto point=math::TransformRowVector({depth*x,depth*y,depth,1},eyes[eye]);CHECK(Inside(*envelope,point));
    }
    auto badEyes=eyes;badEyes[0].values[0][0]=NAN;CHECK(!math::EncloseStereoFrusta(center,badEyes,fov,.08f,100));
    auto badFov=fov;badFov[1].up=badFov[1].down;CHECK(!math::EncloseStereoFrusta(center,eyes,badFov,.08f,100));
    CHECK(!math::EncloseStereoFrusta(center,eyes,fov,100,.08f));
    center=Identity();eyes={center,center};eyes[0].values[3][2]=-1;CHECK(!math::EncloseStereoFrusta(center,eyes,fov,.08f,100));
    return 0;
}
