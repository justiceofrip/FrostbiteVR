#include "Test.h"
#include "Bc2MenuLoading.h"
#include <cstring>
#include <fstream>
#include <iterator>
#include <unordered_map>
#include <vector>

using namespace fvr;
namespace {
struct Fixture {
    static constexpr std::uint32_t base=0x400000,manager=0x4000000,owner=0x5000000;
    bc2::MenuLoadingCandidates binding{0x15a4ca8,0x102b210,0x102b1fc};
    std::unordered_map<std::uint32_t,std::uint32_t> words{
        {base+0x15a4ca8,manager},{manager+0x82b24,0},{manager+0x82b20,0},
        {manager+0x82b28,owner},{owner,base+0x102b210},{owner+0x2c,base+0x102b1fc}};
    unsigned reads=0,roots=0;
    unsigned changeRootAt=0,changeOwnerAt=0,changeTableAt=0,changePauseAt=0;
    bc2::MenuMemory Memory(){return {this,[](void* context,std::uint32_t at,void* out,std::size_t size){
        auto& f=*static_cast<Fixture*>(context);++f.reads;
        if(size!=4)return false;
        if(at==base+f.binding.managerGlobal){
            ++f.roots;
            if(f.roots==f.changeRootAt)f.words[at]=manager+4;
            if(f.roots==f.changeOwnerAt)f.words[manager+0x82b28]=0;
            if(f.roots==f.changeTableAt)f.words[owner+0x2c]^=4;
            if(f.roots==f.changePauseAt)f.words[manager+0x82b20]=owner+0x1000;
        }
        const auto found=f.words.find(at);if(found==f.words.end())return false;
        std::memcpy(out,&found->second,4);return true;
    }};}
};
int Reader(){
    Fixture f;const auto original=f.words;
    const auto active=bc2::ReadMenuLoading(f.Memory(),f.binding,f.base);
    CHECK(active&&active->active&&active->manager==f.manager&&active->owner==f.owner);
    CHECK(f.words==original&&f.reads==20&&f.roots==4);
    // Baseline429 observed c3efc=1 throughout the opening; no state8 or ready
    // field is required. The fixture deliberately provides none of those reads.
    f.words[f.manager+0x82b28]=0;
    const auto retired=bc2::ReadMenuLoading(f.Memory(),f.binding,f.base);
    CHECK(retired&&!retired->active&&retired->owner==0&&retired->manager==f.manager);
    f.words[f.manager+0x82b20]=f.owner+0x1000;
    const auto gameplay=bc2::ReadMenuLoading(f.Memory(),f.binding,f.base);
    CHECK(gameplay&&!gameplay->active); // Inactive does not claim that pause is closed.
    for(unsigned bad=0;bad<12;++bad){
        Fixture g;
        if(bad==0)g.words[g.owner]^=4;
        if(bad==1)g.words[g.owner+0x2c]^=4;
        if(bad==2)g.words[g.manager+0x82b24]=g.owner+0x1000;
        if(bad==3)g.words[g.manager+0x82b20]=g.owner+0x1000;
        if(bad==4)g.words[g.manager+0x82b28]=0xfffffff0;
        if(bad==5)g.words[g.base+g.binding.managerGlobal]=0;
        if(bad==6)g.words[g.base+g.binding.managerGlobal]=0xfffffff0;
        if(bad==7)g.words.erase(g.owner+0x2c);
        if(bad==8)g.words.erase(g.manager+0x82b20);
        if(bad==9)++g.binding.managerGlobal;
        if(bad==10)++g.binding.ownerVtable;
        if(bad==11)++g.binding.eventVtable;
        CHECK(!bc2::ReadMenuLoading(g.Memory(),g.binding,g.base));
        CHECK(g.reads<=20);
    }
    CHECK(!bc2::ReadMenuLoading(f.Memory(),f.binding,UINT32_MAX));
    CHECK(!bc2::ReadMenuLoading(f.Memory(),f.binding,f.base+4));
    CHECK(!bc2::ReadMenuLoading({},f.binding,f.base));
    return 0;
}
int Transitions(){
    for(unsigned boundary=2;boundary<=4;++boundary){
        Fixture manager;manager.changeRootAt=boundary;
        CHECK(!bc2::ReadMenuLoading(manager.Memory(),manager.binding,manager.base));
        Fixture owner;owner.changeOwnerAt=boundary;
        CHECK(!bc2::ReadMenuLoading(owner.Memory(),owner.binding,owner.base));
    }
    Fixture table;table.changeTableAt=3;
    CHECK(!bc2::ReadMenuLoading(table.Memory(),table.binding,table.base));
    Fixture absent;absent.words[absent.manager+0x82b28]=0;absent.changePauseAt=3;
    CHECK(!bc2::ReadMenuLoading(absent.Memory(),absent.binding,absent.base));
    // Native can retire and later allocate at the same address. Observing the
    // intervening inactive state must retire the caller's passive epoch.
    Fixture f;
    const auto first=bc2::ReadMenuLoading(f.Memory(),f.binding,f.base);CHECK(first&&first->active);
    f.words[f.manager+0x82b28]=0;
    const auto empty=bc2::ReadMenuLoading(f.Memory(),f.binding,f.base);CHECK(empty&&!empty->active);
    f.words[f.manager+0x82b28]=f.owner;
    const auto reused=bc2::ReadMenuLoading(f.Memory(),f.binding,f.base);CHECK(reused&&reused->active);
    return 0;
}
int StaticEvidence(const char* path){
    std::ifstream file(path,std::ios::binary);CHECK(file);
    const std::vector<char> raw((std::istreambuf_iterator<char>(file)),{});
    std::vector<std::byte> bytes(raw.size());std::memcpy(bytes.data(),raw.data(),raw.size());
    const auto pe=engine::InspectPe(bytes);CHECK(pe.valid);
    const auto binding=bc2::DiscoverMenuLoading(bytes,pe.image);CHECK(binding);
    CHECK(((*binding)==bc2::MenuLoadingCandidates{0x15a4ca8,0x102b210,0x102b1fc}));
    // Reject broken identity, constructors, publication, resource path, native
    // retirement and transition table links independently of executable hashes.
    for(const std::uint32_t va:{0x737382,0x76461d,0x764617,0x764621,0x764635,
        0x9297c0,0x9297c9,0x9297e1,0x92983c,0x929841,0x92984f,0x929864,
        0x929874,0x92987f,0x75ad4b,0x75ad51,0x142b14d,0x8feca4,0x8fecb3,
        0x8fecb9,0x8feccb,0x956782,0x95678c,0x956839,0x956a68,0x956a6c,
        0x956873,0x9568bb,0x91214b,0x91214e,0x912113,0x142b210,0x142b220,
        0x142b1fc,0x142b20c}){
        bool touched=false;
        for(const auto& section:pe.image.sections)
            if(va>=Fixture::base+section.rva&&va-Fixture::base-section.rva<section.rawSize){
                const auto at=section.rawOffset+va-Fixture::base-section.rva;
                bytes[at]^=std::byte{1};
                CHECK(!bc2::DiscoverMenuLoading(bytes,pe.image));
                bytes[at]^=std::byte{1};touched=true;break;
            }
        CHECK(touched);
    }
    auto wrong=pe.image;wrong.machine=0x8664;
    CHECK(!bc2::DiscoverMenuLoading(bytes,wrong));
    CHECK(!bc2::DiscoverMenuLoading(std::span<const std::byte>(bytes).first(64),pe.image));
    return 0;
}
}
int main(int argc,char** argv){
    CHECK(Reader()==0);CHECK(Transitions()==0);
    if(argc>1)CHECK(StaticEvidence(argv[1])==0);
    return 0;
}
