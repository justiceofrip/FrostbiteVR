#include "Test.h"
#include "Bc2ReloadFlow.h"
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <vector>
using namespace fvr::bc2;
namespace {
template<class T,std::size_t N>void Put(std::array<std::byte,N>& bytes,std::size_t at,T value){std::memcpy(bytes.data()+at,&value,sizeof(value));}
std::array<std::byte,0x30> Context(){std::array<std::byte,0x30> bytes{};Put(bytes,0x18,.016f);Put(bytes,0x1c,123u);Put(bytes,0x20,1.f);return bytes;}
ReloadFlowBinding Binding(){
    ReloadFlowBinding b;b.state.preferredBase=0x400000;b.state.imageSize=0x1000000;
    b.state.code[1].rva=0x2e62d0;b.state.code[2].rva=0x2e69e0;b.code[2].rva=0x2e1bb0;b.code[3].rva=0x2e6d20;
    b.transfers={ReloadTransferSite{0x2e6831,ReloadTransferPath::OrdinaryState12},{0x2e6b99,ReloadTransferPath::TimedInterruption},
        {0x2e1cb9,ReloadTransferPath::SpecialLogic4},{0x2e1db0,ReloadTransferPath::SpecialLogic4},{0x2e6d6c,ReloadTransferPath::ZeroDurationPreparation}};
    constexpr std::array<unsigned,16> offsets{0x3e,0x52,0x13d,0x6ba,0x6ba,0x2bc,0x2e5,0x45d,0x4b5,0x384,0x4ed,0x52a,0x54e,0x5cb,0x601,0x62a};
    for(unsigned n=0;n<offsets.size();++n)b.stepDispatchRvas[n]=b.state.code[1].rva+offsets[n];return b;
}
ReloadStateSnapshot Snapshot(){
    ReloadStateSnapshot s;s.owner={0x10000,0x11000,0x12000,0x13000,1,2,3};s.sequence=20;s.observedNs=1000;
    s.branches[0].address=0x14000;s.branches[0].wrapperOffset=0x3c;s.branches[1].address=0x15000;s.branches[1].wrapperOffset=0x40;return s;
}
int ContextFlagsAndNoMutation(){
    for(unsigned flags=0;flags<8;++flags){auto bytes=Context();Put(bytes,0x2c,flags);bytes[0x24]=std::byte{1};bytes[0x28]=std::byte{1};
        const auto before=bytes;const auto c=DecodeReloadUpdateContext(bytes);CHECK(c&&bytes==before);
        CHECK(Near(c->deltaSeconds,.016f)&&Near(c->reloadTimeMultiplier,1)&&c->rawWord1c==123&&c->inputFlags==flags);
        CHECK(c->fireRequested==bool(flags&1)&&c->orderRequested==bool(flags&2)&&c->reloadRequested==bool(flags&4));
        CHECK(c->flags24Through28[0]&&!c->flags24Through28[1]&&c->flags24Through28[4]);}
    auto bytes=Context();Put(bytes,0x18,0.f);Put(bytes,0x20,0.f);CHECK(DecodeReloadUpdateContext(bytes));
    Put(bytes,0x18,1.f);Put(bytes,0x20,1024.f);CHECK(DecodeReloadUpdateContext(bytes));return 0;
}
int MalformedContext(){
    auto bytes=Context();CHECK(!DecodeReloadUpdateContext(std::span(bytes).first(0x2f)));CHECK(!DecodeReloadUpdateContext({}));
    for(float bad:{-1.f,1.001f,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()}){
        bytes=Context();Put(bytes,0x18,bad);CHECK(!DecodeReloadUpdateContext(bytes));}
    for(float bad:{-1.f,1024.01f,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()}){
        bytes=Context();Put(bytes,0x20,bad);CHECK(!DecodeReloadUpdateContext(bytes));}
    for(unsigned n=0;n<5;++n){bytes=Context();bytes[0x24+n]=std::byte{2};CHECK(!DecodeReloadUpdateContext(bytes));}
    for(unsigned flags:{8u,0xffffffffu,0x80000000u}){bytes=Context();Put(bytes,0x2c,flags);CHECK(!DecodeReloadUpdateContext(bytes));}
    return 0;
}
int SnapshotProjection(){
    std::array<std::byte,0x40> bytes{};Put(bytes,0,11u);Put(bytes,4,12u);Put(bytes,8,.72f);Put(bytes,0x18,5);Put(bytes,0x1c,7);
    const auto before=bytes;const auto result=DecodeReloadFiringSnapshot(bytes);CHECK(result&&bytes==before);
    CHECK(result->current==11&&result->next==12&&Near(result->phaseTimer,.72f)&&result->loaded==5&&result->reserve==7);
    CHECK(!DecodeReloadFiringSnapshot(std::span(bytes).first(0x3f)));
    for(unsigned offset:{0u,4u}){auto bad=bytes;Put(bad,offset,16u);CHECK(!DecodeReloadFiringSnapshot(bad));}
    for(unsigned offset:{0x18u,0x1cu}){auto bad=bytes;Put(bad,offset,-2);CHECK(!DecodeReloadFiringSnapshot(bad));}
    for(float value:{std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()}){
        auto bad=bytes;Put(bad,8,value);CHECK(!DecodeReloadFiringSnapshot(bad));}
    // Unmapped snapshot bits and padding are retained, not assigned bool meaning.
    bytes[0x3f]=std::byte{255};CHECK(DecodeReloadFiringSnapshot(bytes));return 0;
}
int ExactTransferProvenance(){
    const auto b=Binding();auto s=Snapshot();s.config.fireLogicType=4;
    for(const auto& site:b.transfers)for(unsigned n=0;n<2;++n){
        const auto r=ClassifyReloadTransfer(b,b.state.preferredBase,b.state.preferredBase+site.returnRva,s.branches[n].address,s);
        CHECK(r&&r->path==site.path&&r->branch==n&&r->wrapperOffset==s.branches[n].wrapperOffset&&r->firing==s.branches[n].address);
        CHECK(!r->authoritative&&!r->acknowledgement&&!b.manualGateEnabled&&!b.nativeCallEnabled&&!b.authorityProven);}
    s.config.fireLogicType=1;CHECK(!ClassifyReloadTransfer(b,0x400000,0x6e1cb9,0x14000,s));
    CHECK(ClassifyReloadTransfer(b,0x400000,0x6e6831,0x14000,s));
    // Both branches may agree, differ, or be busy: a call-site classification
    // never converts that observation into a transfer result or readiness.
    s.branches[0].loaded=0;s.branches[1].loaded=8;s.branches[0].currentState=11;
    CHECK(ClassifyReloadTransfer(b,0x400000,0x6e6831,0x14000,s));return 0;
}
int OwnershipAndMalformedProofRejection(){
    const auto good=Binding();const auto valid=Snapshot();
    const auto accepts=[](const ReloadFlowBinding& b,const ReloadStateSnapshot& s){return ClassifyReloadTransfer(b,0x400000,0x6e6831,0x14000,s).has_value();};
    CHECK(accepts(good,valid));
    for(unsigned n=0;n<12;++n){auto s=valid;switch(n){
        case 0:s.owner.player=0;break;case 1:s.owner.soldier=0;break;case 2:s.owner.weak=0;break;case 3:s.owner.weapon=0;break;
        case 4:s.owner.actorGeneration=0;break;case 5:s.owner.equipGeneration=0;break;case 6:s.owner.space=0;break;
        case 7:s.sequence=0;break;case 8:s.observedNs=0;break;case 9:s.branches[0].wrapperOffset=0x40;break;
        case 10:s.branches[1].address=s.branches[0].address;break;case 11:s.branches[1].address=0;break;}
        CHECK(!accepts(good,s));}
    CHECK(!ClassifyReloadTransfer(good,0x410000,0x6e6831,0x14000,valid));
    CHECK(!ClassifyReloadTransfer(good,0x400000,0x3fffff,0x14000,valid));
    CHECK(!ClassifyReloadTransfer(good,0x400000,0x6e6830,0x14000,valid));
    CHECK(!ClassifyReloadTransfer(good,0x400000,0x6e6831,0x16000,valid));
    for(unsigned n=0;n<5;++n){auto b=good;switch(n){case 0:b.transfers[0].returnRva++;break;
        case 1:b.transfers[0].path=ReloadTransferPath::TimedInterruption;break;case 2:b.stepDispatchRvas[12]++;break;
        case 3:b.state.code[1].rva=0xffffff00u;break;case 4:b.state.imageSize=0;break;}CHECK(!accepts(b,valid));}
    return 0;
}
// Optional installed-image check maps file bytes through the PE sections only;
// it never opens a process, invokes native code or writes the executable.
int InstalledImageProof(const char* path){
    std::ifstream stream(path,std::ios::binary);CHECK(stream.good());
    const std::vector<char> file((std::istreambuf_iterator<char>(stream)),std::istreambuf_iterator<char>());
    std::vector<std::byte> bytes(file.size());std::memcpy(bytes.data(),file.data(),file.size());
    const auto inspected=fvr::engine::InspectPe(bytes);CHECK(inspected.valid);auto pe=inspected.image;
    const auto binding=DiscoverReloadFlow(bytes,pe);CHECK(binding);CHECK(binding->transfers[0].returnRva==0x2e6831);
    CHECK(binding->branch3cGetterRva==0x3efce0&&binding->branch40GetterRva==0x3efcf0);
    struct FileMemory{std::vector<std::byte>* bytes;fvr::engine::PeImage* pe;std::uint32_t base;};
    FileMemory fm{&bytes,&pe,binding->state.preferredBase};
    ReloadStateMemory memory{&fm,[](void* context,unsigned at,void* out,std::size_t size){
        const auto& f=*static_cast<FileMemory*>(context);if(at<f.base)return false;const auto rva=at-f.base;
        for(const auto& s:f.pe->sections)if(rva>=s.rva){const auto delta=std::uint64_t(rva)-s.rva,pos=std::uint64_t(s.rawOffset)+delta;
            if(delta<=s.rawSize&&size<=s.rawSize-delta&&pos<=f.bytes->size()&&size<=f.bytes->size()-pos){std::memcpy(out,f.bytes->data()+pos,size);return true;}}
        return false;
    },nullptr};
    const auto offset=[&](unsigned rva){for(const auto& s:pe.sections)if(rva>=s.rva&&rva-s.rva<s.rawSize)return std::size_t(s.rawOffset)+rva-s.rva;return bytes.size();};
    CHECK(ValidateReloadFlowLive(memory,*binding,fm.base));
    const auto hold=DiscoverReloadHoldCode(bytes,pe,*binding);CHECK(hold&&ValidateReloadHoldCodeLive(memory,*hold,fm.base,pe.imageSize));
    for(const auto& proof:*hold){const auto at=offset(proof.rva+proof.size-1);CHECK(at<bytes.size());bytes[at]^=std::byte{1};
        CHECK(!DiscoverReloadHoldCode(bytes,pe,*binding));CHECK(!ValidateReloadHoldCodeLive(memory,*hold,fm.base,pe.imageSize));bytes[at]^=std::byte{1};}
    auto invalidHold=*hold;invalidHold[0].rva=0xfffffff0;CHECK(!ValidateReloadHoldCodeLive(memory,invalidHold,fm.base,pe.imageSize));

    unsigned mutations=0;
    for(const auto& proof:binding->code){
        const auto at=offset(proof.rva+proof.size-1);CHECK(at<bytes.size());bytes[at]^=std::byte{1};
        CHECK(!ValidateReloadFlowLive(memory,*binding,fm.base));CHECK(!DiscoverReloadFlow(bytes,pe));bytes[at]^=std::byte{1};++mutations;
    }
    // In addition to full function fingerprints, check the decoded state table,
    // both forwarding getters and all original state-proof regions.
    for(unsigned rva:{binding->state.code[1].rva+0x6c4,binding->branch3cGetterRva,binding->branch40GetterRva}){
        const auto at=offset(rva);CHECK(at<bytes.size());bytes[at]^=std::byte{1};
        CHECK(!ValidateReloadFlowLive(memory,*binding,fm.base));CHECK(!DiscoverReloadFlow(bytes,pe));bytes[at]^=std::byte{1};++mutations;
    }
    for(const auto& proof:binding->state.code){const auto at=offset(proof.rva+proof.size-1);CHECK(at<bytes.size());bytes[at]^=std::byte{1};
        CHECK(!ValidateReloadFlowLive(memory,*binding,fm.base));bytes[at]^=std::byte{1};++mutations;}
    CHECK(!ValidateReloadFlowLive(memory,*binding,fm.base+0x10000));
    auto bad=*binding;bad.transfers[0].path=ReloadTransferPath::TimedInterruption;CHECK(!ValidateReloadFlowLive(memory,bad,fm.base));
    bad=*binding;bad.stepDispatchRvas[1]++;CHECK(!ValidateReloadFlowLive(memory,bad,fm.base));
    bad=*binding;bad.code[0].rva=0xfffffff0;CHECK(!ValidateReloadFlowLive(memory,bad,fm.base));
    auto missing=memory;missing.read=nullptr;CHECK(!ValidateReloadFlowLive(missing,*binding,fm.base));
    const auto start=offset(binding->code[0].rva);CHECK(start<bytes.size());
    const std::vector<std::byte> duplicate(bytes.begin()+start,bytes.begin()+start+binding->code[0].size);
    const auto raw=std::uint32_t(bytes.size());bytes.insert(bytes.end(),duplicate.begin(),duplicate.end());
    pe.sections.push_back({"synthetic_duplicate",pe.imageSize,std::uint32_t(duplicate.size()),raw,std::uint32_t(duplicate.size()),0x20000000});pe.imageSize+=0x1000;
    CHECK(!DiscoverReloadFlow(bytes,pe));
    std::printf("Installed image: 14 function proofs, 5 transfer sites, 16 states, %u byte mutations and ambiguous signature rejected.\n",mutations);return 0;
}
}
int main(int argc,char** argv){
    if(ContextFlagsAndNoMutation()||MalformedContext()||SnapshotProjection()||ExactTransferProvenance()||OwnershipAndMalformedProofRejection())return 1;
    std::printf("Five deterministic reload-flow groups passed. Native capabilities remain disabled.\n");
    return argc==2?InstalledImageProof(argv[1]):0;
}