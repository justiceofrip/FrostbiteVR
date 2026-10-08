#include "Bc2ReloadDrawGeometry.h"
#include "Test.h"
#include <vector>
#include <iostream>
using namespace fvr::bc2;
int FingerprintAndRelocation(){
    std::vector<std::byte> vertices(4*48);for(unsigned i=0;i<vertices.size();++i)vertices[i]=std::byte(i);
    std::vector<std::byte> indices(90*2);for(unsigned i=0;i<90;++i){const std::uint16_t v=std::uint16_t(i%3);std::memcpy(indices.data()+i*2,&v,2);}
    auto a=FingerprintReloadDraw(vertices,indices,90,2,48,0,0);CHECK(a&&a->section==0);
    std::vector<std::byte> relocated(48);relocated.insert(relocated.end(),vertices.begin(),vertices.end());
    auto b=FingerprintReloadDraw(relocated,indices,90,2,48,48,0);CHECK(b&&b->vertexSkinHash==a->vertexSkinHash&&b->positionHash==a->positionHash);
    auto c=FingerprintReloadDraw(relocated,indices,90,2,48,0,1);CHECK(c&&c->vertexSkinHash==a->vertexSkinHash);
    std::vector<std::byte> wide(90*4);for(unsigned i=0;i<90;++i){const std::uint32_t v=i%3+2;std::memcpy(wide.data()+i*4,&v,4);}
    auto d=FingerprintReloadDraw(vertices,wide,90,4,48,0,-2);CHECK(d&&d->vertexSkinHash==a->vertexSkinHash);
    vertices[12]^=std::byte{1};auto changed=FingerprintReloadDraw(vertices,indices,90,2,48,0,0);CHECK(changed&&changed->vertexSkinHash!=a->vertexSkinHash&&changed->positionHash==a->positionHash);
    vertices[0]^=std::byte{1};changed=FingerprintReloadDraw(vertices,indices,90,2,48,0,0);CHECK(changed&&changed->positionHash!=a->positionHash);return 0;
}
int CountsAreNotIdentity(){
    CHECK(IdentifyReloadDrawGeometry(270,0xe05c8e35ec306bd2ull,0x5fd96edbcded25baull)==1);
    CHECK(IdentifyReloadDrawGeometry(90,0xce6e99d2c3cd267eull,0xa9888be789a5c902ull)==2);
    CHECK(IdentifyReloadDrawGeometry(90,0xe05c8e35ec306bd2ull,0x5fd96edbcded25baull)==0);
    CHECK(IdentifyReloadDrawGeometry(270,0xe05c8e35ec306bd3ull,0x5fd96edbcded25baull)==0);
    CHECK(IdentifyReloadDrawGeometry(270,0xe05c8e35ec306bd2ull,0x5fd96edbcded25bbull)==0);
    const char* known="hello";CHECK(ReloadDrawHash({reinterpret_cast<const std::byte*>(known),5})==0xa430d84680aabd0bull);return 0;
}
int OpticLayout(){
    CHECK(IdentifyReloadDrawGeometry(12,0x1b481b02516969b5ull,0x9ad7a905a35a527dull)==3);
    CHECK(IdentifyReloadDrawGeometry(90,0x1b481b02516969b5ull,0x9ad7a905a35a527dull)==0);
    std::vector<std::byte> vertices(8*68),indices(12*2);
    auto a=FingerprintReloadDraw(vertices,indices,12,2,68,0,0);CHECK(a&&a->section==0);
    vertices[16]=std::byte{255};auto b=FingerprintReloadDraw(vertices,indices,12,2,68,0,0);
    CHECK(b&&a->positionHash==b->positionHash&&a->vertexSkinHash!=b->vertexSkinHash);
    vertices[10]=std::byte{1};auto c=FingerprintReloadDraw(vertices,indices,12,2,68,0,0);CHECK(c&&b->positionHash!=c->positionHash);
    CHECK(!FingerprintReloadDraw(vertices,indices,12,2,48,0,0));return 0;
}
int BoundsFailClosed(){
    std::vector<std::byte> vertices(48),indices(180);
    CHECK(FingerprintReloadDraw(vertices,indices,90,2,48,0,0));
    CHECK(!FingerprintReloadDraw(vertices,indices,89,2,48,0,0));
    CHECK(!FingerprintReloadDraw(vertices,indices,90,4,48,0,0));
    CHECK(!FingerprintReloadDraw(vertices,indices,90,2,16,0,0));
    CHECK(!FingerprintReloadDraw(vertices,indices,90,2,48,0,-1));
    CHECK(!FingerprintReloadDraw(vertices,indices,90,2,48,UINT32_MAX,INT32_MAX));
    CHECK(!FingerprintReloadDraw(vertices,indices,90,2,48,33,0));
    indices[0]=std::byte{0xff};indices[1]=std::byte{0xff};CHECK(!FingerprintReloadDraw(vertices,indices,90,2,48,0,0));
    return 0;
}
int main(){if(FingerprintAndRelocation()||CountsAreNotIdentity()||BoundsFailClosed()||OpticLayout())return 1;std::cout<<"4 shell/reticle geometry cases passed\n";}
