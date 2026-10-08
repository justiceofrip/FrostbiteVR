#include "Bc2RigWorkerBinding.h"
#include "Bc2RigWorkerLifecycle.h"
#include "Bc2Profile.h"
#include "Test.h"
#include <cstring>
#include <functional>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>
using namespace fvr::bc2;
namespace {
constexpr unsigned Heap=0x110000,Main=0x11c000,Shadow=0x11d000,Right=0x11e000,Request=0x120000,World=0x122000;
std::optional<ReloadProducerView> Resolve(unsigned w,unsigned q,unsigned v,unsigned f)noexcept {
    if(w!=World||q!=Request||f!=77||(v!=Main&&v!=Right))return {};
    return ReloadProducerView{w,q,v,f,v==Right?1u:0u};
}
struct Fixture {
    std::vector<std::byte> bytes=std::vector<std::byte>(0x30000);
    RigWorkerBinding binding{};RigWorkerCall call{Heap,0xbb4895,2,0x124000,9,0x112000};
    ReloadProducerOwner owner{0x126000,0x127000,0x128000,11,12};
    unsigned reads=0,workspaceReads=0;std::function<void(Fixture&)> mutation;
    template<class T>void Put(unsigned at,T value){std::memcpy(bytes.data()+at-Heap,&value,sizeof(value));}
    Fixture(){binding.base=0x400000;binding.imageSize=0x1000000;binding.rendererTable=0x403000;binding.workerReturn=0xbb4895;
        binding.scene={0x402000,0x402100,0x402200};binding.derivedViewTable=0x402300;binding.commonViewTable=0x402400;
        Put(Heap,binding.rendererTable);Put(Heap+0x6b0,call.workspace);
        Put(call.workspace,0x114000u);Put(call.workspace+4,0x118000u);Put(call.workspace+8,0x11a000u);
        Put(0x114000,1u);Put(0x114004,0x116000u);Put(0x114008,0x118000u);
        Put(0x116000,9u);Put(0x116004,2u);Put(0x116008,call.rows);
        Put(0x118000,Main);Put(0x11800c,0x11a000u);Put(0x118024,std::uint8_t{1});Put(0x118028,Main);
        Put(0x11a000,0x200000u);Put(0x11a008,std::uint16_t{16});
        for(auto v:{Main,Shadow,Right}){Put(v,binding.scene.view);Put(v+0x70,Request);}
        Put(Request,binding.scene.request);Put(Request+8,World);Put(Request+0xc4,3u);
        Put(World,binding.scene.world);Put(World+0x88,77u);
        Put(call.rows,0x129000u);Put(call.rows+32,unsigned(owner.actor));Put(call.rows+36,std::uint16_t{1});
    }
    static bool Read(void* opaque,unsigned at,void* dst,std::size_t n){auto& f=*static_cast<Fixture*>(opaque);++f.reads;
        if(at==f.call.workspace&&++f.workspaceReads==2&&f.mutation)f.mutation(f);
        if(at<Heap||std::uint64_t(at)+n>Heap+f.bytes.size())return false;std::memcpy(dst,f.bytes.data()+at-Heap,n);return true;}
    RigWorkerRead Run(){return ReadRigWorker({this,Read,nullptr},binding,call,owner);}
};
int ValidWorker(){Fixture f;auto r=f.Run();CHECK(r.identity&&r.status==RigWorkerStatus::Observed);const auto& s=*r.identity;
    CHECK(s.job==0x114000&&s.batch==0x118000&&s.arena==0x11a000&&s.view==Main&&s.nativeFrame==77);
    CHECK(s.selectedRowIndex==1&&s.selectedViewMask==1&&s.owner==f.owner);auto a=AssociateRigWorker(s,Resolve);CHECK(a.key&&a.key->eye==0);CHECK(f.reads<150);return 0;}
int RightAndNoEyeGuess(){Fixture f;f.Put(0x118000,Right);f.Put(0x118028,Right);auto r=f.Run();CHECK(r.identity);CHECK(AssociateRigWorker(*r.identity,Resolve).key->eye==1);
    Fixture shadow;shadow.Put(0x118000,Shadow);auto s=shadow.Run();CHECK(s.identity);CHECK(!AssociateRigWorker(*s.identity,Resolve).key);return 0;}
int SharedStereoBatch(){Fixture f;f.Put(0x118024,std::uint8_t{2});f.Put(0x11802c,Right);auto r=f.Run();CHECK(r.identity);
    CHECK(AssociateRigWorker(*r.identity,Resolve).status==RigWorkerStatus::MultipleStereoViews);
    Fixture s;s.Put(0x118024,std::uint8_t{2});s.Put(0x11802c,Shadow);r=s.Run();CHECK(r.identity&&AssociateRigWorker(*r.identity,Resolve).key);return 0;}
int CallerAndTypes(){for(unsigned n=0;n<6;++n){Fixture f;
    if(n==0)++f.call.caller;if(n==1)f.Put(Heap,0u);if(n==2)f.Put(Heap+0x6b0,0x113000u);
    if(n==3)f.Put(Main,0u);if(n==4)f.Put(Request,0u);if(n==5)f.Put(World,0u);
    CHECK(!f.Run().identity);}return 0;}
int ArrayBounds(){for(unsigned n=0;n<8;++n){Fixture f;
    if(n==0)f.call.rowCount=1025;if(n==1)f.call.rows=0xfffffff0;if(n==2)f.Put(0x114000,4097u);
    if(n==3)f.Put(0x118024,std::uint8_t{17});if(n==4)f.Put(0x118024,std::uint8_t{0});if(n==5)f.Put(0x11a008,std::uint16_t{3});
    if(n==6)f.Put(0x116004,3u);if(n==7){f.Put(0x118024,std::uint8_t{2});f.Put(0x11802c,Main);}
    CHECK(!f.Run().identity);}return 0;}
int Owners(){Fixture missing;missing.Put(missing.call.rows+32,0u);CHECK(missing.Run().status==RigWorkerStatus::OwnerMissing);
    Fixture duplicate;duplicate.Put(duplicate.call.rows,unsigned(duplicate.owner.actor));CHECK(duplicate.Run().status==RigWorkerStatus::OwnerAmbiguous);
    Fixture noToken;noToken.owner.ownerGeneration=0;CHECK(!noToken.Run().identity);return 0;}
int RepeatedEvidence(){for(unsigned n=0;n<6;++n){Fixture f;f.mutation=[n](Fixture& x){
    if(n==0)x.Put(World+0x88,78u);if(n==1)x.Put(0x118028,Shadow);if(n==2)x.Put(x.call.rows+36,std::uint16_t{2});
    if(n==3)x.Put(0x114000,2u);if(n==4)x.Put(Request+0xc4,1u);if(n==5)x.Put(0x118000,Shadow);};
    auto r=f.Run();CHECK(r.status==RigWorkerStatus::ChangedDuringRead&&!r.identity);}return 0;}
int NativeArenaAdvances(){Fixture f;f.mutation=[](Fixture& x){x.Put(0x11a00c,10000u);};auto r=f.Run();CHECK(r.identity);
    Fixture bad;bad.Put(0x11800c,0x11b000u);CHECK(!bad.Run().identity);return 0;}
int ForeignViewAndFailure(){Fixture f;f.Put(0x118024,std::uint8_t{2});f.Put(0x11802c,Shadow);f.Put(Shadow+0x70,Request+4);CHECK(f.Run().status==RigWorkerStatus::WrongType);
    Fixture invalid;invalid.Put(Main+0x70,0x5000000u);CHECK(invalid.Run().status==RigWorkerStatus::ReadFailure);return 0;}
int MaximumRows(){Fixture f;f.call.rowCount=1024;f.Put(0x116004,1024u);auto r=f.Run();CHECK(r.identity);CHECK(f.reads<2200);return 0;}
void Child(Fixture& f){f.Put(0x118024,std::uint8_t{2});f.Put(0x11802c,Shadow);f.Put(Shadow,f.binding.derivedViewTable);f.Put(Shadow+0x1670,Main);}
int DerivedChild(){Fixture f;Child(f);const auto r=f.Run();CHECK(r.identity);CHECK(r.identity->viewTables[1]==f.binding.derivedViewTable);
    CHECK(r.identity->viewParents[1]==Main&&r.identity->viewRequests[1]==Request);CHECK(r.identity->rejectedViewIndex==UINT32_MAX);
    CHECK(AssociateRigWorker(*r.identity,Resolve).key->view==Main);return 0;}
int WrongChildRelationship(){for(unsigned n=0;n<3;++n){Fixture f;Child(f);if(n==0)f.Put(Shadow+0x1670,Right);if(n==1)f.Put(Shadow+0x70,Request+4);if(n==2)f.Put(Shadow+0x1670,0u);
    const auto r=f.Run();CHECK(!r.identity&&r.status==RigWorkerStatus::WrongType);CHECK(r.partial.rejectedViewIndex==1);}return 0;}
int ChildClassIsExact(){for(unsigned table:{0x402400u,0x402500u,0u}){Fixture f;Child(f);f.Put(Shadow,table);auto r=f.Run();CHECK(!r.identity&&r.status==RigWorkerStatus::WrongType);CHECK(r.partial.viewTables[1]==table);}
    Fixture primary;Child(primary);primary.Put(Main,primary.binding.derivedViewTable);CHECK(primary.Run().status==RigWorkerStatus::WrongType);return 0;}
int ChildChangedDuringRead(){for(unsigned n=0;n<3;++n){Fixture f;Child(f);f.mutation=[n](Fixture& x){if(n==0)x.Put(Shadow+0x1670,Right);if(n==1)x.Put(Shadow,x.binding.scene.view);if(n==2)x.Put(Shadow+0x70,Request+4);};
    CHECK(f.Run().status==RigWorkerStatus::ChangedDuringRead);}return 0;}
int ImmutableViewLease(){RigWorkerViewLease lease{{*Resolve(World,Request,Main,77),*Resolve(World,Request,Right,77)},100,1000};
    CHECK(LookupRigWorkerViewLease(lease,World,Request,Main,77,101)->eye==0);CHECK(LookupRigWorkerViewLease(lease,World,Request,Right,77,101)->eye==1);
    CHECK(!LookupRigWorkerViewLease(lease,World,Request,Shadow,77,101));CHECK(!LookupRigWorkerViewLease(lease,World,Request,Main,78,101));
    CHECK(!LookupRigWorkerViewLease(lease,World,Request,Main,77,99));CHECK(!LookupRigWorkerViewLease(lease,World,Request,Main,77,1000));return 0;}
int MalformedViewLease(){for(unsigned n=0;n<5;++n){RigWorkerViewLease lease{{*Resolve(World,Request,Main,77),*Resolve(World,Request,Right,77)},100,1000};
    if(n==0)lease.eyes[1].view=Main;if(n==1)lease.eyes[1].nativeFrame=78;if(n==2)lease.eyes[1].request=0;if(n==3)lease.eyes[0].eye=1;if(n==4)lease.deadlineNs=300000101;
    CHECK(!LookupRigWorkerViewLease(lease,World,Request,Main,77,101));}return 0;}
RigWorkerViewLease Lease(unsigned frame,std::int64_t observed,std::int64_t deadline){return {{ReloadProducerView{World,Request,Main,frame,0},ReloadProducerView{World,Request,Right,frame,1}},observed,deadline};}
int OriginalDeadlineRetained(){const auto first=Lease(77,100,1000);const auto again=AdvanceRigWorkerViewLease(&first,Lease(77,900,1800));
    CHECK(again.observedNs==100&&again.deadlineNs==1000);CHECK(LookupRigWorkerViewLease(again,World,Request,Main,77,999));
    const auto expired=AdvanceRigWorkerViewLease(&again,Lease(77,2000,2900));
    CHECK(expired.observedNs==100&&expired.deadlineNs==1000);CHECK(!LookupRigWorkerViewLease(expired,World,Request,Main,77,2000));return 0;}
int ConflictingFrameStaysInvalid(){const auto first=Lease(77,100,1000);auto changed=Lease(77,200,1100);changed.eyes[1].view=Shadow;
    auto poisoned=AdvanceRigWorkerViewLease(&first,changed);CHECK(poisoned.deadlineNs==poisoned.observedNs);
    poisoned=AdvanceRigWorkerViewLease(&poisoned,Lease(77,300,1200));CHECK(!LookupRigWorkerViewLease(poisoned,World,Request,Main,77,300));
    const auto fresh=AdvanceRigWorkerViewLease(&poisoned,Lease(78,400,1300));CHECK(LookupRigWorkerViewLease(fresh,World,Request,Main,78,401));return 0;}
int FrameOrdering(){const auto first=Lease(77,100,1000);auto old=AdvanceRigWorkerViewLease(&first,Lease(76,200,1100));CHECK(old.eyes[0].nativeFrame==77&&old.observedNs==100);
    old=AdvanceRigWorkerViewLease(&first,Lease(78,99,999));CHECK(old.eyes[0].nativeFrame==77);
    const auto fresh=AdvanceRigWorkerViewLease(&first,Lease(78,200,1100));CHECK(fresh.eyes[0].nativeFrame==78&&fresh.observedNs==200);
    const auto wrapped=Lease(0xffffffffu,100,1000);const auto next=AdvanceRigWorkerViewLease(&wrapped,Lease(0,200,1100));CHECK(next.eyes[0].nativeFrame==0);
    CHECK(AdvanceRigWorkerViewLease(nullptr,first).deadlineNs==1000);return 0;}
int ExplicitGlobalStartup(){for(auto outcome:{RigWorkerEnableResult::Enabled,RigWorkerEnableResult::AlreadyEnabled}){RigWorkerLifecycle state;
    CHECK(state.Quiescent(0));CHECK(!state.BeginGlobalStart());CHECK(state.Created());CHECK(!state.Created());
    CHECK(!state.CompleteGlobalStart(true,outcome));CHECK(!state.Admitted());CHECK(state.Quiescent(0));
    CHECK(state.BeginGlobalStart());CHECK(!state.BeginGlobalStart());CHECK(!state.Admitted());CHECK(state.EntryMayBeEnabled());CHECK(!state.Quiescent(0));
    CHECK(state.CompleteGlobalStart(true,outcome));CHECK(state.Admitted());CHECK(!state.CompleteGlobalStart(true,outcome));
    state.StopAdmission();CHECK(!state.Admitted());CHECK(state.EntryMayBeEnabled());CHECK(!state.Quiescent(0));
    state.ConfirmDisable(true);CHECK(!state.EntryMayBeEnabled());CHECK(!state.Quiescent(1));CHECK(state.Quiescent(0));}return 0;}
int FailedGlobalStartupRequiresDisable(){for(auto outcome:{RigWorkerEnableResult::Enabled,RigWorkerEnableResult::AlreadyEnabled,RigWorkerEnableResult::Failed}){
    RigWorkerLifecycle state;CHECK(state.Created());CHECK(state.BeginGlobalStart());CHECK(!state.CompleteGlobalStart(false,outcome));
    CHECK(!state.Admitted());CHECK(state.EntryMayBeEnabled());state.ConfirmDisable(false);CHECK(!state.Quiescent(0));
    state.ConfirmDisable(true);CHECK(state.Quiescent(0));}
    RigWorkerLifecycle localFailure;CHECK(localFailure.Created());CHECK(localFailure.BeginGlobalStart());
    CHECK(!localFailure.CompleteGlobalStart(true,RigWorkerEnableResult::Failed));CHECK(!localFailure.Quiescent(0));
    localFailure.ConfirmDisable(true);CHECK(localFailure.Quiescent(0));return 0;}
int InterruptedGlobalStartup(){RigWorkerLifecycle state;CHECK(state.Created());CHECK(state.BeginGlobalStart());state.StopAdmission();
    CHECK(!state.CompleteGlobalStart(true,RigWorkerEnableResult::AlreadyEnabled));CHECK(!state.Admitted());CHECK(!state.Quiescent(0));
    state.ConfirmDisable(false);CHECK(!state.Quiescent(0));state.ConfirmDisable(true);CHECK(state.Quiescent(0));return 0;}
}
int main(int argc,char** argv){
    if(argc==2){std::ifstream in(argv[1],std::ios::binary);std::vector<char> raw((std::istreambuf_iterator<char>(in)),{});auto bytes=std::as_bytes(std::span(raw));
        const auto pe=fvr::engine::InspectPe(bytes);CHECK(pe.valid);const auto render=DiscoverRenderPath(bytes,pe.image),dummy=render;const auto layout=DiscoverViewLayout(bytes,pe.image);const auto lifecycle=DiscoverViewLifecycle(bytes,pe.image);
        CHECK(render&&layout&&lifecycle);const RigWorkerSceneTypes scene{0x400000+layout->vtable,0x400000+lifecycle->requestVtable,0x400000+render->worldRendererVtable};
        const auto b=DiscoverRigWorker(bytes,pe.image,scene);CHECK(b);CHECK(b->prepare==0x5f2860&&b->workerReturn==0xbb4895);
        struct FileMemory {std::span<const std::byte> bytes;const fvr::engine::PeImage* pe;};FileMemory file{bytes,&pe.image};
        const ReloadStateMemory memory{&file,[](void* context,unsigned at,void* destination,std::size_t size){const auto& f=*static_cast<FileMemory*>(context);if(at<0x400000)return false;const auto rva=at-0x400000;
            for(const auto& s:f.pe->sections)if(rva>=s.rva&&std::uint64_t(rva-s.rva)+size<=s.rawSize&&std::uint64_t(s.rawOffset)+rva-s.rva+size<=f.bytes.size()){
                std::memcpy(destination,f.bytes.data()+s.rawOffset+rva-s.rva,size);return true;}return false;},nullptr};
        CHECK(ValidateRigWorkerLive(memory,*b,0x400000));
        for(const auto& proof:b->code){auto changed=raw;for(const auto& section:pe.image.sections)if(proof.rva>=section.rva&&proof.rva-section.rva<section.rawSize){changed[section.rawOffset+proof.rva-section.rva+proof.size-1]^=1;break;}
            CHECK(!DiscoverRigWorker(std::as_bytes(std::span(changed)),pe.image,scene));}
        CHECK(b->derivedViewTable==0x1477588&&b->commonViewTable==0x1477498);
        for(const auto& proof:b->viewCode){auto changed=raw;for(const auto& section:pe.image.sections)if(proof.rva>=section.rva&&proof.rva-section.rva<section.rawSize){changed[section.rawOffset+proof.rva-section.rva+proof.size-1]^=1;break;}
            CHECK(!DiscoverRigWorker(std::as_bytes(std::span(changed)),pe.image,scene));}
        for(auto table:{b->commonViewTable,b->derivedViewTable,b->scene.view}){auto changed=raw;const auto rva=table-b->base;
            for(const auto& section:pe.image.sections)if(rva>=section.rva&&rva-section.rva<section.rawSize){changed[section.rawOffset+rva-section.rva+239]^=1;break;}
            CHECK(!DiscoverRigWorker(std::as_bytes(std::span(changed)),pe.image,scene));}
        std::cout<<"offline worker discovery, nine full-function and three vtable mutation rejections passed\n";return 0;
    }
    for(auto test:{ValidWorker,RightAndNoEyeGuess,SharedStereoBatch,CallerAndTypes,ArrayBounds,Owners,RepeatedEvidence,NativeArenaAdvances,ForeignViewAndFailure,MaximumRows,DerivedChild,WrongChildRelationship,ChildClassIsExact,ChildChangedDuringRead,ImmutableViewLease,MalformedViewLease,OriginalDeadlineRetained,ConflictingFrameStaysInvalid,FrameOrdering,ExplicitGlobalStartup,FailedGlobalStartupRequiresDisable,InterruptedGlobalStartup})if(test())return 1;
    std::cout<<"22 worker binding tests passed\n";return 0;
}
