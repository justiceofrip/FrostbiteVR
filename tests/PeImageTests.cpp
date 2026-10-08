#include "Test.h"
#include "fvr/engine/PeImage.h"
#include <vector>
using namespace fvr::engine;
void U16(std::vector<std::byte>& b,std::size_t p,unsigned v){b[p]=std::byte(v&255);b[p+1]=std::byte((v>>8)&255);}
void U32(std::vector<std::byte>& b,std::size_t p,unsigned v){U16(b,p,v&65535);U16(b,p+2,v>>16);}
std::vector<std::byte> Fixture(bool x64=false){std::vector<std::byte>b(0x600);U16(b,0,0x5a4d);U32(b,0x3c,0x80);U32(b,0x80,0x4550);U16(b,0x84,x64?0x8664:0x14c);U16(b,0x86,1);U16(b,0x94,x64?240:224);U16(b,0x96,0x22);U16(b,0x98,x64?0x20b:0x10b);U32(b,0x98+56,0x2000);const auto section=0x98+(x64?240:224);b[section]=std::byte{'.'};U32(b,section+8,0x400);U32(b,section+12,0x1000);U32(b,section+16,0x400);U32(b,section+20,0x200);U32(b,section+36,0x60000020);return b;}
int main(){
    for(bool x64:{false,true}){auto bytes=Fixture(x64);const auto result=InspectPe(bytes);CHECK(result.valid);CHECK(result.image.machine==(x64?0x8664:0x14c));CHECK(result.image.largeAddressAware);
        for(std::size_t n=0;n<bytes.size();++n)CHECK(!InspectPe(std::span(bytes).first(n)).valid);}
    auto bytes=Fixture();U32(bytes,0x3c,0xfffffff0);CHECK(!InspectPe(bytes).valid);bytes=Fixture();U16(bytes,0x86,97);CHECK(!InspectPe(bytes).valid);
    bytes=Fixture();U32(bytes,0x98+92,16);U32(bytes,0x98+104,0x1000);U32(bytes,0x98+108,40);U32(bytes,0x200+12,0x1080);const char name[]="d3d11.dll";for(unsigned i=0;i<sizeof(name);++i)bytes[0x280+i]=std::byte(name[i]);
    auto result=InspectPe(bytes);CHECK(result.valid&&result.image.imports.size()==1&&result.image.imports[0]=="d3d11.dll");
    U32(bytes,0x200+12,0xfffffff0);CHECK(!InspectPe(bytes).valid);bytes=Fixture(true);U16(bytes,0x84,0x14c);CHECK(!InspectPe(bytes).valid);
    return 0;
}