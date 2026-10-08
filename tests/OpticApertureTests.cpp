#include "Test.h"
#include "fvr/math/OpticAperture.h"
#include <algorithm>
#include <limits>
using namespace fvr::math;
static OpticAperture Square(float z,float half=.01f){
 OpticAperture a;a.z=z;a.count=4;a.outline={AperturePoint{-half,-half},{half,-half},{half,half},{-half,half}};return a;
}
int main(){
 std::array a{Square(0),Square(.1f)};ReticleBounds r{{-.001f,-.001f,.2f},{.001f,.001f,.2f}};
 auto eval=[&](Vec3 eye){return EvaluateOpticAperture(a,r,eye);};
 CHECK(eval({0,0,-.1f}).visibility==ApertureVisibility::Visible);
 CHECK(eval({.2f,0,-.1f}).CanSkipDraw());CHECK(eval({0,.2f,-.1f}).CanSkipDraw());
 CHECK(eval({0,0,.01f}).CanSkipDraw());CHECK(eval({0,0,0}).CanSkipDraw());
 CHECK(eval({.015f,0,-.1f}).visibility==ApertureVisibility::Partial);
 // No box corner is inside this aperture, but the reticle overlaps its centre.
 auto tiny=Square(0,.0001f);CHECK(EvaluateOpticAperture(std::span(&tiny,1),r,{0,0,-.1f}).visibility==ApertureVisibility::Partial);
 // The second rim can reject a ray admitted by a wider rear rim.
 a[0]=Square(0,.1f);CHECK(eval({.035f,0,-.1f}).CanSkipDraw());
 auto one=std::span(a).first(1);CHECK(EvaluateOpticAperture(one,r,{.035f,0,-.1f}).visibility==ApertureVisibility::Visible);
 // Opposite winding, optic-local translation, and uniform scale retain decisions.
 a={Square(0),Square(.1f)};std::reverse(a[0].outline.begin(),a[0].outline.begin()+4);
 CHECK(eval({0,0,-.1f}).visibility==ApertureVisibility::Visible);
 for(auto& ap:a){ap.z=ap.z*3+5;for(unsigned i=0;i<ap.count;++i){ap.outline[i].x=ap.outline[i].x*3+2;ap.outline[i].y=ap.outline[i].y*3-1;}}
 r={{1.997f,-1.003f,5.6f},{2.003f,-.997f,5.6f}};CHECK(eval({2,-1,4.7f}).visibility==ApertureVisibility::Visible);
 // Invalid data never grants permission to skip a native draw.
 CHECK(!eval({std::numeric_limits<float>::quiet_NaN(),0,0}).CanSkipDraw());
 a[0].count=17;CHECK(eval({2,-1,4.7f}).visibility==ApertureVisibility::Invalid);
 a={Square(0),Square(.1f)};r={{-.001f,-.001f,.2f},{.001f,.001f,.2f}};
 a[0].outline[1]=a[0].outline[0];CHECK(eval({0,0,-.1f}).visibility==ApertureVisibility::Invalid);
 a={Square(0),Square(.1f)};std::swap(a[0].outline[1],a[0].outline[2]);CHECK(eval({0,0,-.1f}).visibility==ApertureVisibility::Invalid);
 a={Square(0),Square(.1f)};a[1].z=0;CHECK(eval({0,0,-.1f}).visibility==ApertureVisibility::Invalid);
 CHECK(EvaluateOpticAperture({},r,{0,0,-1}).visibility==ApertureVisibility::Invalid);
 a={Square(0),Square(.1f)};r.maximum.x=r.minimum.x;CHECK(eval({0,0,-.1f}).visibility==ApertureVisibility::Invalid);
 return 0;
}
