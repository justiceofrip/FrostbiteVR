#include "Test.h"
#include "Bc2PistolAfterFeedInput.h"
using namespace fvr::bc2;
namespace {
struct Memory {
    std::array<std::byte,0x30> bytes{};std::uint32_t originalFlags=0;
    unsigned calls=0,compares=0,failAt=0;bool neutralSeen=false,scribble=false;
    void Flags(std::uint32_t v){std::memcpy(bytes.data()+0x2c,&v,4);}
    std::uint32_t Flags()const{std::uint32_t v=0;std::memcpy(&v,bytes.data()+0x2c,4);return v;}
    PistolAfterFeedInputAccess Access(){return {this,[](void* p,std::uint32_t expected,std::uint32_t next,std::uint32_t& observed)noexcept {
        auto& m=*static_cast<Memory*>(p);if(++m.compares==m.failAt)return false;
        observed=m.Flags();if(observed==expected)m.Flags(next);return true;}};}
    static void Original(void* p){auto& m=*static_cast<Memory*>(p);++m.calls;
        m.neutralSeen=m.Flags()==(m.originalFlags&~1u);if(m.scribble)m.Flags(m.Flags()|8u);}
};
int OnlyFireBitAndOneOriginal(){
    for(auto flags:{0u,1u,3u,5u,7u,0x80000101u}){
        Memory m;for(unsigned n=0;n<m.bytes.size();++n)m.bytes[n]=std::byte(n);
        m.originalFlags=flags;m.Flags(flags);const auto before=m.bytes;PistolAfterFeedInputReceipt receipt;
        RunPistolAfterFeedInput(m.Access(),flags,true,Memory::Original,&m,receipt);
        CHECK(m.calls==1&&m.neutralSeen&&m.bytes==before&&receipt.Exact());
        std::array<std::byte,0x30> effective{};CHECK(PistolAfterFeedEffectiveContext(before,m.bytes,receipt,effective));
        CHECK(effective[0x28]==before[0x28]); // stock Abort never becomes true
        for(unsigned n=0;n<0x2c;++n)CHECK(effective[n]==before[n]); // dt and all other inputs exact
        std::uint32_t seen=0;std::memcpy(&seen,effective.data()+0x2c,4);CHECK(seen==(flags&~1u));
    }return 0;
}
int NoClaimOnPatchOrRestoreFailure(){
    for(unsigned mode=0;mode<3;++mode){Memory m;m.originalFlags=5;m.Flags(5);const auto before=m.bytes;
        m.failAt=mode==0?1:mode==1?2:0;m.scribble=mode==2;PistolAfterFeedInputReceipt receipt;
        RunPistolAfterFeedInput(m.Access(),5,true,Memory::Original,&m,receipt);
        CHECK(m.calls==1&&!receipt.Exact());std::array<std::byte,0x30> effective{};
        CHECK(!PistolAfterFeedEffectiveContext(before,m.bytes,receipt,effective));
        if(mode==0)CHECK(m.bytes==before&&!receipt.applied);
        if(mode==2)CHECK(m.Flags()==12&&receipt.unexpectedWrite); // never overwrite a native contradiction
    }return 0;
}
int DisabledScopeAndUnrelatedMutation(){
    Memory m;m.originalFlags=7;m.Flags(7);const auto before=m.bytes;PistolAfterFeedInputReceipt receipt;
    RunPistolAfterFeedInput(m.Access(),7,false,Memory::Original,&m,receipt);
    CHECK(m.calls==1&&!m.compares&&m.bytes==before&&!receipt.requested);
    std::array<std::byte,0x30> effective{};CHECK(PistolAfterFeedEffectiveContext(before,m.bytes,receipt,effective)&&effective==before);
    m.bytes[0x18]=std::byte{1};CHECK(!PistolAfterFeedEffectiveContext(before,m.bytes,receipt,effective));return 0;
}
}
int main(){if(OnlyFireBitAndOneOriginal()||NoClaimOnPatchOrRestoreFailure()||DisabledScopeAndUnrelatedMutation())return 1;
    std::puts("3 pistol input groups passed; fire-only scope, one original, exact restore/effective evidence.");}
