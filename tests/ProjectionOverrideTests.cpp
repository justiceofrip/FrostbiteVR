#include "Test.h"
#include "fvr/math/ProjectionOverride.h"
#include <limits>
using namespace fvr::math;
int main(){
    std::array<float,16> right{1,0,0,0,0,1.111111f,0,0,.2f,0,-1.00005f,-1,0,0,-.1f,0};
    auto left=right;left[0]=.9f;left[5]=1.2f;left[8]=-.24f;left[9]=.17f;
    auto biased=right;biased[10]=-1.53968f;biased[14]=-11.562f;
    auto fixed=RetargetDepthProjection(biased,right,left);CHECK(fixed);
    // Verify clip-space X/Y/W at several depths, keeping the native biased Z.
    for(float z:{-8.f,-30.f,-400.f}){std::array<float,4> point{2.f,-1.f,z,1.f};
        for(unsigned col=0;col<4;++col){float actual=0,expected=0;
            for(unsigned row=0;row<4;++row){actual+=point[row]*(*fixed)[row*4+col];expected+=point[row]*(col==2?biased:left)[row*4+col];}
            CHECK(Near(actual,expected));}}
    CHECK((*fixed)[10]==biased[10]&&(*fixed)[14]==biased[14]);
    CHECK(RetargetDepthProjection(biased,right,right)==biased);
    auto unrelated=biased;unrelated[0]*=2;CHECK(!RetargetDepthProjection(unrelated,right,left));
    unrelated=biased;unrelated[11]=1;CHECK(!RetargetDepthProjection(unrelated,right,left));
    unrelated=biased;unrelated[14]=std::numeric_limits<float>::quiet_NaN();CHECK(!RetargetDepthProjection(unrelated,right,left));
    auto invalid=left;invalid[9]=std::numeric_limits<float>::infinity();CHECK(!RetargetDepthProjection(biased,right,invalid));
    auto weapon=right;weapon[0]*=3.3f;weapon[5]*=3.3f;weapon[10]=-1.5f;weapon[14]=-.2f;
    auto firstPerson=RetargetFirstPersonProjection(weapon,left);CHECK(firstPerson);
    CHECK((*firstPerson)[0]==left[0]&&(*firstPerson)[5]==left[5]&&(*firstPerson)[8]==left[8]&&(*firstPerson)[9]==left[9]);
    CHECK((*firstPerson)[10]==weapon[10]&&(*firstPerson)[14]==weapon[14]);
    auto bad=weapon;bad[15]=1;CHECK(!RetargetFirstPersonProjection(bad,left));
    bad=weapon;bad[11]=1;CHECK(!RetargetFirstPersonProjection(bad,left));
    bad=weapon;bad[3]=.1f;CHECK(!RetargetFirstPersonProjection(bad,left));
    bad=weapon;bad[0]=0;CHECK(!RetargetFirstPersonProjection(bad,left));
    bad=weapon;bad[10]=std::numeric_limits<float>::quiet_NaN();CHECK(!RetargetFirstPersonProjection(bad,left));
    return 0;
}
