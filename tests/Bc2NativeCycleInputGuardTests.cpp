#include "Bc2NativeCycleInputGuard.h"
#include "Test.h"
using namespace fvr::bc2;
namespace {
struct Memory {
    std::array<std::uint32_t,2> words{0xaabbcc00,5};unsigned calls=0,writes=0,compares=0;
    unsigned failAt=0;bool scribble=false;bool seen=false;
    NativeCycleInputAccess Access(){return {this,[](void* p,unsigned index,std::uint32_t expected,std::uint32_t next,std::uint32_t& observed)noexcept{
        auto& m=*static_cast<Memory*>(p);if(++m.compares==m.failAt)return false;if(index>=2)return false;
        observed=m.words[index];if(observed==expected){m.words[index]=next;++m.writes;}return true;}};}
    static void Original(void* p){auto& m=*static_cast<Memory*>(p);++m.calls;m.seen=m.words==std::array<std::uint32_t,2>{0xaabbcc01,0};
        if(m.scribble)m.words[1]=9;}
};
int ExactScopedInputAndPadding(){
    for(unsigned input=0;input<8;++input)for(unsigned inhibited=0;inhibited<2;++inhibited){
        Memory m;m.words={0xaabbcc00|inhibited,input};const auto saved=m.words;NativeCycleInputGuard receipt;
        RunNativeCycleInputGuard(m.Access(),saved,true,Memory::Original,&m,receipt);
        CHECK(m.calls==1&&m.seen&&m.words==saved&&receipt.Exact()&&receipt.original==saved&&receipt.effective[0]==0xaabbcc01);
    }return 0;
}
int PartialPatchRollbackAndOriginalStillOnce(){
    for(unsigned fail=1;fail<=2;++fail){Memory m;const auto original=m.words;m.failAt=fail;NativeCycleInputGuard receipt;
        RunNativeCycleInputGuard(m.Access(),original,true,Memory::Original,&m,receipt);
        CHECK(m.calls==1&&!receipt.applied&&!receipt.Exact()&&m.words==original&&receipt.rollback==(fail==2));}
    return 0;
}
int RestoreContradictionCannotClaimContinuity(){
    Memory m;m.scribble=true;NativeCycleInputGuard receipt;RunNativeCycleInputGuard(m.Access(),m.words,true,Memory::Original,&m,receipt);
    CHECK(m.calls==1&&m.seen&&!receipt.Exact()&&receipt.unexpectedWrite&&!receipt.restored&&m.words[1]==9&&m.words[0]==0xaabbcc00);return 0;
}
int DisabledOrMalformedInputDoesNotMutate(){
    for(unsigned mode=0;mode<3;++mode){Memory m;if(mode==1)m.words[0]|=2;if(mode==2)m.words[1]=8;
        const auto original=m.words;NativeCycleInputGuard receipt;RunNativeCycleInputGuard(m.Access(),original,mode!=0,Memory::Original,&m,receipt);
        CHECK(m.calls==1&&!m.writes&&!receipt.applied&&!receipt.Exact()&&m.words==original);}
    return 0;
}
}
int main(){if(ExactScopedInputAndPadding()||PartialPatchRollbackAndOriginalStillOnce()||RestoreContradictionCannotClaimContinuity()||
    DisabledOrMalformedInputDoesNotMutate())return 1;
    std::puts("4 native input guard groups passed; scoped caller words only, exact restoration/rollback, original once.");}
