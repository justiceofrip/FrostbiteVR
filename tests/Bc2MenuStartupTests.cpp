#include "Test.h"
#include "Bc2MenuStartup.h"
#include <cstring>
#include <fstream>
#include <iterator>
#include <unordered_map>
using namespace fvr;
namespace {
constexpr bc2::MenuStartupBinding Binding{0x15a4ca8,0x1040860,0x559900,0x3438b0};
using Movie=bc2::MenuStartupMovie;
int Policy(){
    const auto choose=bc2::ChooseMissingStartupMovie;
    CHECK(choose(true,true,2,3,"Movies/UIMALogos","Movies/UIMALogos",3,0)==Movie::EA);
    CHECK(choose(true,true,3,3,"Movies/UIMADolby","Movies/UIMADolby",3,1)==Movie::Dolby);
    CHECK(choose(true,true,1,3,"Movies/UIMAESRB_French_English","Movies/UIMAESRB_French_English",3,0)==Movie::None);
    CHECK(choose(true,true,2,3,"Movies/UIMALogos","Movies/UIMALogos",0,0)==Movie::None);
    CHECK(choose(true,true,2,3,"Movies/UIMALogos","Movies/UIMALogos",3,1)==Movie::None);
    CHECK(choose(false,true,2,3,"Movies/UIMALogos","Movies/UIMALogos",3,0)==Movie::None);
    CHECK(choose(true,false,2,3,"Movies/UIMALogos","Movies/UIMALogos",3,0)==Movie::None);
    CHECK(choose(true,true,2,3,"Movies/UIMALogos","Movies/UIMADolby",3,0)==Movie::None);
    CHECK(choose(true,true,0,3,"Movies/UIMALogos","Movies/UIMALogos",3,0)==Movie::None);
    CHECK(choose(true,true,4,3,"Movies/UIMALogos","Movies/UIMALogos",3,0)==Movie::None);
    CHECK(choose(true,true,2,17,"Movies/UIMALogos","Movies/UIMALogos",3,0)==Movie::None);
    CHECK(choose(true,true,2,3,"","",3,0)==Movie::None);
    return 0;
}
struct Memory {
    static constexpr unsigned Manager=0x2000000,Owner=0x2100000,Sequence=0x2200000,Text=0x2300000;
    std::unordered_map<unsigned,unsigned char> bytes;
    unsigned maxRead=0,ownerReads=0;bool switchOwner=false;
    void Put(unsigned at,unsigned value,unsigned size=4){for(unsigned i=0;i<size;++i)bytes[at+i]=static_cast<unsigned char>(value>>(i*8));}
    void String(unsigned at,unsigned data,const char* value){
        const auto n=unsigned(std::strlen(value));Put(at,data);Put(at+4,data+n);
        for(unsigned i=0;i<n;++i)bytes[data+i]=static_cast<unsigned char>(value[i]);
    }
    Memory(){
        Put(0x19a4ca8,Manager);Put(Manager+0x82b20,0);Put(Manager+0x82b24,Owner);
        Put(Owner,0x142b4e8);Put(Owner+0x2c,0x142b4d4);Put(Owner+0x90,0x142b4c8);
        Put(Manager+0x82ccc,1,1);Put(Manager+0x82cc8,2);Put(Owner+0x114,Sequence);Put(Owner+0x118,Sequence+36);
        String(Owner+0x124,Text,"Movies/UIMALogos");String(Sequence+12,Text+256,"Movies/UIMALogos");
    }
    bc2::MenuMemory View(){return {this,[](void* ctx,unsigned at,void* dst,std::size_t n){
        auto& m=*static_cast<Memory*>(ctx);if(n>m.maxRead)m.maxRead=unsigned(n);
        if(at==Manager+0x82b24&&++m.ownerReads==2&&m.switchOwner)m.Put(at,Owner+0x1000);
        for(unsigned i=0;i<n;++i){const auto f=m.bytes.find(at+i);if(f==m.bytes.end())return false;static_cast<unsigned char*>(dst)[i]=f->second;}return true;
    }};}
    auto Read(){return bc2::ReadMenuStartup(View(),Binding,0x400000,Manager);}
};
int ReadBoundaries(){
    Memory good;const auto s=good.Read();CHECK(s&&s->owner==Memory::Owner&&s->enabled&&s->count==3&&s->index==2);
    CHECK(std::string_view(s->current.data())=="Movies/UIMALogos");CHECK(good.maxRead<=256);
    for(unsigned bad=0;bad<13;++bad){Memory m;
        switch(bad){
        case 0:m.Put(0x19a4ca8,Memory::Manager+4);break;
        case 1:m.Put(Memory::Manager+0x82b20,Memory::Owner);break;
        case 2:m.Put(Memory::Owner,0x142b7a0);break;
        case 3:m.Put(Memory::Owner+0x2c,0);break;
        case 4:m.Put(Memory::Owner+0x90,0);break;
        case 5:m.Put(Memory::Owner+0x118,Memory::Sequence-1);break;
        case 6:m.Put(Memory::Owner+0x118,Memory::Sequence+37);break;
        case 7:m.Put(Memory::Owner+0x118,Memory::Sequence+17*12);break;
        case 8:m.Put(Memory::Owner+0x128,Memory::Text+257);break;
        case 9:m.bytes.erase(Memory::Text+2);break;
        case 10:m.bytes[Memory::Text+2]=0;break;
        case 11:m.switchOwner=true;break;
        case 12:m.Put(Memory::Manager+0x82ccc,2,1);break;
        }
        CHECK(!m.Read());CHECK(m.maxRead<=256);
    }
    Memory wrong;CHECK(!bc2::ReadMenuStartup(wrong.View(),Binding,0x500000,Memory::Manager));
    auto bad=Binding;bad.cleanup+=1;CHECK(!bc2::ReadMenuStartup(wrong.View(),bad,0x400000,Memory::Manager));
    // A transient save flag is intentionally absent from the fixture. Reaching
    // an empty exhausted sequence is readable without asserting menu visibility.
    Memory exhausted;exhausted.Put(Memory::Manager+0x82cc8,4);exhausted.String(Memory::Owner+0x124,Memory::Text,"");
    CHECK(exhausted.Read());return 0;
}
int ActualFile(const char* path){
    std::ifstream in(path,std::ios::binary);CHECK(in);std::vector<char> raw((std::istreambuf_iterator<char>(in)),{});
    std::vector<std::byte> bytes(raw.size());std::memcpy(bytes.data(),raw.data(),raw.size());
    const auto pe=engine::InspectPe(bytes);CHECK(pe.valid);CHECK(bc2::DiscoverMenuStartup(bytes,pe.image)==Binding);
    // Verify rejection of modified code, call targets, slot, PE architecture.
    for(const auto va:{0x7438b0u,0x959900u,0x760270u,0x77c01cu,0x760391u,0x7438dbu,0x1440860u}){
        std::size_t at=bytes.size();const auto rva=va-0x400000;
        for(const auto& s:pe.image.sections)if(rva>=s.rva&&rva-s.rva<s.rawSize)at=s.rawOffset+rva-s.rva;
        CHECK(at<bytes.size());bytes[at]^=std::byte{1};CHECK(!bc2::DiscoverMenuStartup(bytes,pe.image));bytes[at]^=std::byte{1};
    }
    auto changed=pe.image;changed.timestamp++;CHECK(!bc2::DiscoverMenuStartup(bytes,changed));
    changed=pe.image;changed.machine=0x8664;CHECK(!bc2::DiscoverMenuStartup(bytes,changed));return 0;
}
}
int main(int argc,char** argv){const auto result=Policy()+ReadBoundaries()+(argc==2?ActualFile(argv[1]):0);
    if(!result)std::puts("BC2 missing-logo policy and bounded-read checks passed");return result;}
