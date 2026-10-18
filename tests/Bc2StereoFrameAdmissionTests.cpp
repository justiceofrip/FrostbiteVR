#include "Bc2StereoFrameAdmission.h"
#include "Bc2StereoProgressEvidence.h"
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <vector>
using namespace fvr::bc2;
namespace {
void Check(bool value){if(!value){std::cerr<<"stereo frame admission assertion failed\n";std::abort();}}
constexpr StereoFrameAdmissionBinding binding{0x10000,0x20000,0x21000,0x22000,0x23000};
constexpr StereoFrameOwner owner{0x30000,0x40000,0x50000,0x60000};
struct Memory {
    std::map<std::uint32_t,std::uint32_t> words;
    std::vector<std::uint32_t> reads;
    unsigned globalReads=0;bool replaceRegistryOnSecond=false,restoreOwnerOnSecond=false;
    Memory(){
        words={{binding.gameRendererGlobal,0x70000},{0x7108c,0x80000},
            {0x80000,binding.subsystemType},{0x8002c,owner.world},{0x80030,owner.request},
            {owner.world,binding.worldType},{owner.request,binding.requestType},
            {owner.request+8,owner.world},{owner.request+0xc4,3},
            {owner.request+0x24,0x90000},{owner.request+0x28,0x90008},{owner.request+0x2c,0x90010},
            {0x90000,owner.main},{0x90004,owner.eye},
            {owner.request+0x14,0xa0000},{owner.request+0x18,0xa000c},{owner.request+0x1c,0xa0010},
            {0xa0000,owner.main},{0xa0004,owner.eye},{0xa0008,0xb0000},
            {owner.main,binding.viewType},{owner.main+0x70,owner.request},
            {owner.eye,binding.viewType},{owner.eye+0x70,owner.request}};
    }
    bool Read(std::uint32_t at,void* dst,std::size_t bytes){
        reads.push_back(at);
        if(at==binding.gameRendererGlobal&&++globalReads==2&&replaceRegistryOnSecond)words[0xa0008]=0xc0000;
        if(at==binding.gameRendererGlobal&&globalReads==2&&restoreOwnerOnSecond){words[0x8002c]=owner.world;words[0x80030]=owner.request;}
        for(std::size_t n=0;n<bytes;n+=4){const auto found=words.find(at+static_cast<std::uint32_t>(n));
            if(found==words.end())return false;std::memcpy(static_cast<char*>(dst)+n,&found->second,4);}
        return true;
    }
    void Replacement(){words[0x8002c]=0xd0000;words[0x80030]=0xe0000;
        words[0xd0000]=binding.worldType;words[0xe0000]=binding.requestType;
        words[0xe0008]=0xd0000;words[0xe00c4]=3;}
    bool Touched(std::uint32_t value)const {for(auto at:reads)if(at==value)return true;return false;}
};
StereoFrameAdmission Inspect(Memory& m,StereoFrameAdmissionSnapshot& out){
    return InspectStereoFrameAdmission(binding,owner,[&](auto at,auto dst,auto size){return m.Read(at,dst,size);},out);
}
bool OldAdmission(Memory& m){
    StereoProgressSample s;s.world=s.expectedWorld=owner.world;s.request=s.expectedRequest=owner.request;
    s.eye=owner.eye;s.expectedEyeType=binding.viewType;
    return CheckStereoProgressOwner(s,[&](std::uintptr_t at){return m.words.at(static_cast<std::uint32_t>(at));})==StereoProgressReason::OwnerMatched;
}
void RemovedStockMainKeepsOldEyeAlive(){
    Memory m;m.words[owner.request+0x28]=0x90004;m.words[0x90000]=owner.eye;
    m.words.erase(owner.main);m.words.erase(owner.main+0x70);
    Check(OldAdmission(m));StereoFrameAdmissionSnapshot out{};out.renderer=99;const auto sentinel=out;
    Check(Inspect(m,out)==StereoFrameAdmission::RootsChanged&&out==sentinel);
    Check(!m.Touched(owner.main)&&!m.Touched(owner.eye));
}
void AuthoritativeReplacementWinsOverRetainedGraph(){
    Memory m;m.Replacement();
    Check(OldAdmission(m));StereoFrameAdmissionSnapshot out{};
    Check(Inspect(m,out)==StereoFrameAdmission::CurrentOwnerChanged);
    Check(out.currentWorld==0xd0000&&out.currentRequest==0xe0000&&m.globalReads==2);
    Check(!m.Touched(owner.world)&&!m.Touched(owner.request)&&!m.Touched(owner.main));
}
void OnlyStableVerifiedReplacementBlocks(){
    for(unsigned fault=0;fault<6;++fault){Memory m;m.Replacement();StereoFrameAdmissionSnapshot out{};
        if(fault==0)m.words[0x8002c]=0;
        if(fault==1)m.words[0xe0000]=binding.viewType;
        if(fault==2)m.words[0xe0008]=owner.world;
        if(fault==3)m.words[0xe00c4]=2;
        if(fault==4)m.words.erase(0xe0000);
        if(fault==5)m.restoreOwnerOnSecond=true;
        Check(!BlocksStereoFrameAdmission(Inspect(m,out)));
    }
    for(unsigned i=0;i<static_cast<unsigned>(StereoFrameAdmission::Count);++i){
        const auto result=static_cast<StereoFrameAdmission>(i);
        Check(BlocksStereoFrameAdmission(result)==(result==StereoFrameAdmission::CurrentOwnerChanged));
        Check(StereoFrameAdmissionNames[i]&&*StereoFrameAdmissionNames[i]);
    }
}
void TransientRejectionDoesNotLatch(){
    Memory m;StereoFrameAdmissionSnapshot out{};
    // There is deliberately no soldier/health/input field: an actorless scene
    // with native rendering ownership is admissible, including initial movies.
    Check(Inspect(m,out)==StereoFrameAdmission::Ready&&out.allCount==3);
    m.words[owner.request+0xc4]=2;Check(Inspect(m,out)==StereoFrameAdmission::RequestPhaseChanged);
    m.words[owner.request+0xc4]=3;Check(Inspect(m,out)==StereoFrameAdmission::Ready);
    m.words[0x90000]=0xd0000;Check(Inspect(m,out)==StereoFrameAdmission::RootsChanged);
    m.words[0x90000]=owner.main;Check(Inspect(m,out)==StereoFrameAdmission::Ready);
}
void RegistrationBeforeDereferenceAndChangingSnapshot(){
    Memory m;m.words[0xa0000]=0xd0000;StereoFrameAdmissionSnapshot out{};
    Check(Inspect(m,out)==StereoFrameAdmission::RegistryChanged&&!m.Touched(owner.main));
    Memory moving;moving.replaceRegistryOnSecond=true;out.renderer=99;const auto sentinel=out;
    Check(Inspect(moving,out)==StereoFrameAdmission::SnapshotChanged&&out==sentinel);
}
void InvalidNativeInputsFailClosed(){
    for(unsigned fault=0;fault<6;++fault){Memory m;StereoFrameAdmissionSnapshot out{};
        if(fault==0)m.words.erase(0x80030);
        if(fault==1)m.words[owner.request+0x18]=0xa0104;
        if(fault==2)m.words[owner.request+0x1c]=0x90000;
        if(fault==3)m.words[0xa0008]=owner.main;
        if(fault==4)m.words[owner.main+0x70]=0xd0000;
        if(fault==5)m.words[owner.world]=binding.viewType;
        Check(Inspect(m,out)!=StereoFrameAdmission::Ready);
    }
}
}
int main(){RemovedStockMainKeepsOldEyeAlive();AuthoritativeReplacementWinsOverRetainedGraph();
    OnlyStableVerifiedReplacementBlocks();TransientRejectionDoesNotLatch();RegistrationBeforeDereferenceAndChangingSnapshot();InvalidNativeInputsFailClosed();
    std::cout<<"6 stereo frame admission groups passed\n";
}
