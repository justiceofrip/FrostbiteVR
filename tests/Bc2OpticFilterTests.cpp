#include "Test.h"
#include "Bc2OpticFilter.h"
#include "Bc2OpticFilterSession.h"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <limits>
using namespace fvr;using namespace fvr::bc2;
namespace {
struct Fixture {
    std::vector<std::byte> file;engine::PeImage pe;OpticFilterBinding binding;
    std::map<unsigned,std::byte> memory;SelectedMeshesSnapshot selected;OpticFilterCall call{0x25000,0x26000,0};
    unsigned reads=0,mutateAt=0,mutateTarget=0,mutateValue=0;
    template<class T>void Put(unsigned at,const T& v){const auto* b=reinterpret_cast<const std::byte*>(&v);for(unsigned n=0;n<sizeof(T);++n)memory[at+n]=b[n];}
    std::optional<std::size_t> At(unsigned address,std::size_t count)const {
        if(address<binding.image.preferredBase)return {};const auto rva=address-binding.image.preferredBase;
        for(const auto& s:pe.sections)if(rva>=s.rva&&rva-s.rva+count<=s.rawSize)return std::size_t(s.rawOffset)+rva-s.rva;return {};
    }
    Fixture(const std::vector<std::byte>& source):file(source){pe=engine::InspectPe(file).image;binding=*DiscoverOpticFilter(file,pe);
        selected.owner={0x10000,0x12000,0x14000,0x16000,1,2,3};selected.sequence=10;selected.observedNs=100;selected.deadlineNs=10000100;
        selected.inventory=0x18000;selected.weaponData=0x20000;selected.stateTypeInfo=0x1bfca0c;selected.stateCount=1;selected.states[0].state=0x23000;
        std::memcpy(selected.weaponName.data(),"fixture",8);call.caller=binding.image.preferredBase+binding.candidates.filterReturn;
        const auto& o=selected.owner;Put<unsigned char>(o.player+0xccd,8);Put(o.player+0xc54,o.weak);Put(o.weak,o.soldier+4);Put(o.player+0xc68,o.soldier);Put(o.soldier+0x220,o.player);
        Put<unsigned char>(o.soldier+0x114,1);Put(o.soldier+0x24c,selected.inventory);Put<unsigned>(selected.inventory+0x14c,0);
        Put<unsigned>(o.soldier+0x260,0x1a000);Put<unsigned>(o.soldier+0x264,0x1a004);Put(0x1a000,o.weapon);Put(o.weapon+4,selected.weaponData);
        const std::array<unsigned,4> stateHeader{0,selected.stateTypeInfo,0x23000,0x230c8};Put(selected.weaponData+0x88,stateHeader);
        Put<unsigned>(selected.stateTypeInfo+4,0x1bfbed4);
        const std::array<std::byte,6> pattern{std::byte{0xb8},std::byte{0x48},std::byte{0x6b},std::byte{0xbf},std::byte{0x01},std::byte{0xc3}};
        const auto it=std::search(file.begin(),file.end(),pattern.begin(),pattern.end());if(it==file.end())throw 1;
        const auto offset=std::size_t(it-file.begin());unsigned getter=0;for(const auto& s:pe.sections)if(offset>=s.rawOffset&&offset<s.rawOffset+s.rawSize)getter=binding.image.preferredBase+s.rva+unsigned(offset-s.rawOffset);
        Put<unsigned>(call.filter,0x28000);Put(0x28008,getter);Put(call.filter+0x20,std::array<float,4>{0,0,1,1});Put(call.filter+0x60,std::array<float,2>{.5f,.5f});Put(call.filter+0xc0,1.f);
        Put<unsigned>(call.renderer+0xc8,0x30000);Put<unsigned>(call.renderer+0xd8,0x31000);Put<unsigned>(call.renderer+0xdc,0x32000);
        Put(selected.states[0].state+0xac,call.filter);Put<unsigned>(selected.states[0].state+0xbc,0);Put<unsigned>(selected.states[0].state+0x94,0);
    }
    static bool Read(void* context,unsigned at,void* out,std::size_t n){auto& f=*static_cast<Fixture*>(context);++f.reads;
        auto* dest=static_cast<std::byte*>(out);for(unsigned i=0;i<n;++i){const auto it=f.memory.find(at+i);if(it!=f.memory.end())dest[i]=it->second;
            else {const auto off=f.At(at+i,1);if(!off)return false;dest[i]=f.file[*off];}}
        if(f.mutateAt==at){f.mutateAt=0;f.Put(f.mutateTarget,f.mutateValue);}return true;
    }
    ReloadStateMemory Memory(){return {this,Read,nullptr};}
    OpticFilterRead Run(bool enabled=true,std::int64_t now=200){return ReadOpticFilter(Memory(),binding,selected,selected.owner,call,now,enabled);}
};
int ExactConfiguredFilter(const std::vector<std::byte>& file){Fixture f(file);const auto before=f.memory;const auto p=f.Run();CHECK(p.sample&&p.status==OpticFilterStatus::Observed);
    CHECK(p.sample->zoomedMatchMask==1&&!p.sample->nonZoomedMatchMask&&p.sample->rendererWrappers[0]==0x30000&&f.memory==before);
    CHECK(!p.sample->activeStateVerified&&!p.sample->nativeAdsAcknowledged&&!p.sample->magnifiedSceneVerified&&!p.sample->renderAuthority);
    CHECK(p.reads<1024&&p.bytes<65536);return 0;}
int DefaultsAndFreshness(const std::vector<std::byte>& file){Fixture f(file);CHECK(f.Run(false).status==OpticFilterStatus::Disabled&&f.reads==0);
    CHECK(f.Run(true,99).status==OpticFilterStatus::Expired);CHECK(f.Run(true,f.selected.deadlineNs).status==OpticFilterStatus::Expired);
    f.call.caller++;CHECK(f.Run().status==OpticFilterStatus::Caller);return 0;}
int IdentityAndMetadataReject(const std::vector<std::byte>& file){for(unsigned n=0;n<7;++n){Fixture f(file);
    if(n==0)f.selected.owner.equipGeneration=0;if(n==1)f.Put<unsigned>(f.selected.owner.player+0xc68,0x12340);
    if(n==2)f.selected.stateTypeInfo++;if(n==3)f.selected.states[0].state+=0xc8;
    if(n==4)f.Put<unsigned>(0x23000+0xac,0x26100);if(n==5)f.Put<float>(f.call.filter+0xc0,std::numeric_limits<float>::quiet_NaN());
    if(n==6)f.Put<unsigned>(f.call.filter,0x28100);CHECK(!f.Run().sample);}return 0;}
int SharedConfiguredFilterIsNotActiveState(const std::vector<std::byte>& file){Fixture f(file);f.selected.stateCount=2;f.selected.states[1].state=0x230c8;
    f.Put<unsigned>(f.selected.weaponData+0x94,0x23190);for(unsigned state:{0x23000u,0x230c8u}){f.Put(state+0xac,f.call.filter);f.Put(state+0xbc,f.call.filter);f.Put<unsigned>(state+0x94,0);}
    const auto r=f.Run();CHECK(r.sample&&r.sample->zoomedMatchMask==3&&r.sample->nonZoomedMatchMask==3&&!r.sample->activeStateVerified);return 0;}
int BracketMutationRejects(const std::vector<std::byte>& file){Fixture f(file);f.mutateAt=f.call.filter+0xc0;f.mutateTarget=f.selected.states[0].state+0xac;f.mutateValue=0x26100;
    CHECK(!f.Run().sample);return 0;}
int OriginalLeaseAndResources(const std::vector<std::byte>& file){Fixture f(file);auto a=f.Run();CHECK(a.sample);auto b=*a.sample;b.rendererWrappers[0]++;CHECK(SameOpticFilterOwner(*a.sample,b));
    b.sequence++;CHECK(!SameOpticFilterOwner(*a.sample,b));b=*a.sample;b.deadlineNs++;CHECK(!SameOpticFilterOwner(*a.sample,b));b=*a.sample;b.owner.space++;CHECK(!SameOpticFilterOwner(*a.sample,b));return 0;}
int LiveProofMutationRejects(const std::vector<std::byte>& file){Fixture f(file);CHECK(ValidateOpticFilterLive(f.Memory(),f.binding,f.binding.image.preferredBase));
    f.Put<unsigned char>(f.binding.image.preferredBase+f.binding.candidates.filterRenderer,0x90);CHECK(!ValidateOpticFilterLive(f.Memory(),f.binding,f.binding.image.preferredBase));return 0;}
int DisconnectNeverReusesOldSource(){OpticFilterSession s;CHECK(!s.Begin(100));s.SetConnected(true,100);CHECK(!s.Begin(99));const auto old=s.Begin(100);CHECK(old&&s.Current(*old));
    s.SetConnected(true,110);CHECK(s.Current(*old));s.SetConnected(false,120);CHECK(!s.Current(*old)&&!s.Begin(120));s.SetConnected(true,130);
    CHECK(!s.Current(*old)&&!s.Begin(129)&&s.Begin(130));s.SetConnected(true,140);CHECK(s.Begin(130));s.SetConnected(false,0);CHECK(!s.Begin(150));return 0;}
int IsolatedDiagnosticFlags(){constexpr auto flags=0x8000000u|0x17800u|9;CHECK(ValidOpticFilterProbeConfig(0,0)&&ValidOpticFilterProbeConfig(flags,15000));
    CHECK(!ValidOpticFilterProbeConfig(flags,15001)&&!ValidOpticFilterProbeConfig(flags,99)&&!ValidOpticFilterProbeConfig(flags^1,1000));
    for(unsigned bit:{0x400u,0x8000u,0x20000u,0x40000u,0x400000u,0x800000u,0x1000000u,0x2000000u,0x4000000u})CHECK(!ValidOpticFilterProbeConfig(flags|bit,1000));
    for(unsigned bit:{0x800u,0x1000u,0x2000u,0x4000u,0x10000u})CHECK(!ValidOpticFilterProbeConfig(flags^bit,1000));return 0;}
}
int main(int argc,char** argv){CHECK(!DiscoverOpticFilter({},{}));if(DisconnectNeverReusesOldSource()||IsolatedDiagnosticFlags())return 1;
    if(argc<2){std::cout<<"Bc2OpticFilter: 2 portable groups passed; installed-image fixtures require EXE path\n";return 0;}
    CHECK(argc==2);std::ifstream input(argv[1],std::ios::binary);CHECK(input);const std::vector<char> raw((std::istreambuf_iterator<char>(input)),{});
    std::vector<std::byte> file(raw.size());std::memcpy(file.data(),raw.data(),raw.size());CHECK(DiscoverOpticFilter(file,engine::InspectPe(file).image));
    if(ExactConfiguredFilter(file)||DefaultsAndFreshness(file)||IdentityAndMetadataReject(file)||SharedConfiguredFilterIsNotActiveState(file)||BracketMutationRejects(file)||OriginalLeaseAndResources(file)||LiveProofMutationRejects(file))return 1;
    std::cout<<"Bc2OpticFilter: 9 cases passed\n";}
