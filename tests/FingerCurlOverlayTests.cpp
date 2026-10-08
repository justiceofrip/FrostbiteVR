#include "Test.h"
#include "fvr/interaction/FingerCurlOverlay.h"
#include <limits>
using namespace fvr;using namespace interaction;
namespace {
math::Matrix4 At(float x=0,float y=0,float z=0){
    math::Matrix4 m{};for(unsigned n=0;n<4;++n)m.values[n][n]=1;m.values[3]={x,y,z,1};return m;
}
bool Close(const math::Matrix4& a,const math::Matrix4& b){
    for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)if(!Near(a.values[r][c],b.values[r][c],.00001f))return false;return true;
}
struct Fixture{
    std::array<std::int32_t,8> parents{-1,0,1,2,3,4,1,0};
    std::array<math::Matrix4,8> native{At(),At(1,2,3),At(1,2,3.06f),At(1,2,3.09f),At(1,2,3.11f),At(1,2,3.12f),At(1.1f,2,3.04f),{}};
    HandFingerBinding finger{};
    Fixture(){finger.count=3;for(unsigned n=0;n<3;++n)finger.joints[n]={n+2,{1,0,0},0,0,0};}
    std::optional<HandPose> Pose(std::array<float,3> radians){return GenerateFingerCurlOverlay(parents,native,1,finger,radians);}
};
int PreservationAndCurl(){
    Fixture f;const auto original=f.native;
    const auto zero=f.Pose({0,0,0});CHECK(zero&&zero->writes.empty());
    const auto posed=f.Pose({.2f,.3f,.1f});CHECK(posed&&posed->writes.size()==4);
    auto output=f.native;
    for(const auto& write:posed->writes){CHECK(write.index>=2&&write.index<=5);output[write.index]=write.transform;}
    CHECK(output[1].values==original[1].values&&output[6].values==original[6].values&&output[7].values==original[7].values);
    CHECK(output[3].values[3][1]<original[3].values[3][1]);
    CHECK(!Close(output[4],original[4])&&!Close(output[5],original[5]));
    for(unsigned n=2;n<=5;++n){
        const auto distance=[](const math::Matrix4& a,const math::Matrix4& b){return std::hypot(a.values[3][0]-b.values[3][0],a.values[3][1]-b.values[3][1],a.values[3][2]-b.values[3][2]);};
        CHECK(Near(distance(output[n],output[f.parents[n]]),distance(original[n],original[f.parents[n]])));
    }
    for(unsigned n=0;n<f.native.size();++n)CHECK(f.native[n].values==original[n].values);
    for(unsigned repeat=0;repeat<500;++repeat){
        const auto again=f.Pose({.2f,.3f,.1f});CHECK(again&&again->writes.size()==posed->writes.size());
        for(unsigned n=0;n<again->writes.size();++n)CHECK(again->writes[n].transform.values==posed->writes[n].transform.values);
    }
    CHECK(f.Pose({0,0,0})->writes.empty()); // Native recovery does not need inverse overlay.
    return 0;
}
int NativeAnimationAndWorldPlacement(){
    Fixture f;const auto baseline=f.Pose({.1f,.2f,.1f});CHECK(baseline);
    // Different original animation remains present beneath the same additive
    // amount, rather than being replaced by a reference hand or previous output.
    const auto nativeCurl=f.Pose({.15f,.1f,.05f});CHECK(nativeCurl);
    for(const auto& write:nativeCurl->writes)f.native[write.index]=write.transform;
    const auto animated=f.Pose({.1f,.2f,.1f});CHECK(animated);
    CHECK(!Close(animated->writes[1].transform,baseline->writes[1].transform));
    const auto released=f.Pose({0,0,0});CHECK(released&&released->writes.empty());
    auto turn=At(3,-2,1);turn.values[0]={0,1,0,0};turn.values[1]={-1,0,0,0};
    for(unsigned n=0;n<7;++n)f.native[n]=Multiply(f.native[n],turn);
    const auto moved=f.Pose({.1f,.2f,.1f});CHECK(moved);
    for(unsigned n=0;n<moved->writes.size();++n)CHECK(Close(moved->writes[n].transform,Multiply(animated->writes[n].transform,turn)));
    return 0;
}
int Invalid(){
    for(unsigned kind=0;kind<9;++kind){
        Fixture f;
        if(kind==0)f.parents[7]=7;
        if(kind==1)f.parents[3]=1;
        if(kind==2)f.finger.joints[1].index=2;
        if(kind==3)f.finger.joints[0].curlAxisLocal={};
        if(kind==4)f.finger.count=5;
        if(kind==5)f.native[1].values[0][0]=-1;
        if(kind==6)f.native[5].values[0][0]=0;
        if(kind==7)f.native[3].values[3][0]=std::numeric_limits<float>::quiet_NaN();
        if(kind==8)f.finger.joints[0].index=1;
        CHECK(!f.Pose({.1f,.2f,.1f}));
    }
    Fixture f;CHECK(!f.Pose({std::numeric_limits<float>::quiet_NaN(),0,0}));
    CHECK(!f.Pose({4,0,0}));const std::array<float,2> shortAngles{0,0};
    CHECK(!GenerateFingerCurlOverlay(f.parents,f.native,1,f.finger,shortAngles));
    return 0;
}
}
int main(){if(PreservationAndCurl()||NativeAnimationAndWorldPlacement()||Invalid())return 1;return 0;}
