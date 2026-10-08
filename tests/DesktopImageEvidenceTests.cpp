#include "fvr/graphics/DesktopImageEvidence.h"
#include "Test.h"
#include <array>
#include <limits>
using namespace fvr::graphics;
int main(){
    const std::array<unsigned char,20> padded{255,255,255,0,250,250,250,255,91,92,93,94,0,1,5,255,255,0,0,255};
    auto a=InspectDesktopRgba(padded,2,2,12);CHECK(a);
    CHECK(a->pixels==4&&a->nearWhite==2&&a->nearBlack==1&&a->opaque==3);
    CHECK(a->minimum[0]==0&&a->maximum[0]==255&&a->channelSum[0]==760);
    CHECK(a->channelSum[1]==506&&a->channelSum[2]==510);
    const std::array<unsigned char,16> packed{255,255,255,0,250,250,250,255,0,1,5,255,255,0,0,255};
    auto b=InspectDesktopRgba(packed,2,2,8);CHECK(b&&b->rgbaHash==a->rgbaHash);
    auto changed=packed;changed[3]=255;auto c=InspectDesktopRgba(changed,2,2,8);
    CHECK(c&&c->nearWhite==b->nearWhite&&c->opaque==4&&c->rgbaHash!=b->rgbaHash);
    CHECK(!InspectDesktopRgba(packed,0,2,8));
    CHECK(!InspectDesktopRgba(packed,2,0,8));
    CHECK(!InspectDesktopRgba(packed,4097,1,4097*4));
    CHECK(!InspectDesktopRgba(packed,2,2,7));
    CHECK(!InspectDesktopRgba(std::span(packed).first(15),2,2,8));
    CHECK(!InspectDesktopRgba(packed,2,2,std::numeric_limits<std::size_t>::max()));
    CHECK(InspectDesktopRgba(std::span(packed).first(8),2,1,std::numeric_limits<std::size_t>::max()));
    const std::array<unsigned char,4> transparentWhite{255,255,255,0};
    auto white=InspectDesktopRgba(transparentWhite,1,1,4);
    CHECK(white&&white->nearWhite==1&&white->opaque==0);
    std::puts("Desktop image evidence tests passed.");return 0;
}