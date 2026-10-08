#include "Bc2RigWorkerRuntime.h"
#include "Bc2RigWorkerLifecycle.h"
#include <Windows.h>
#include <intrin.h>
#include <MinHook.h>
#include <atomic>
#include <mutex>
namespace fvr::bc2::rigWorkerRuntime {
#if defined(_M_IX86)
namespace {
// Native invoker does not consume EAX, but preserve its incidental value anyway.
using Prepare=std::uintptr_t(__thiscall*)(void*,unsigned,unsigned,unsigned,void*);
Prepare original=nullptr;void* hook=nullptr;RigWorkerBinding binding{};
OwnerResolver resolveOwner=nullptr;RigWorkerViewResolver resolveView=nullptr;
RigWorkerLifecycle lifecycle;std::atomic<unsigned> inFlight=0;
std::atomic<std::int64_t> startedNs=0;
std::array<std::atomic<std::uint64_t>,11> statuses{};
std::atomic<std::uint64_t> calls=0,admitted=0,completed=0,postChanged=0,busy=0,overflow=0;
struct Record {RigWorkerIdentity identity{};RigWorkerStatus status{};bool afterExact=false;std::int64_t beginNs=0,endNs=0;};
std::array<Record,128> records{};unsigned recordCount=0;std::mutex recordMutex;
struct Callback {Callback(){++inFlight;}~Callback(){--inFlight;}};
bool Read(void*,std::uint32_t at,void* dst,std::size_t n){SIZE_T got=0;
    return at>=0x10000&&n&&n<=4096&&std::uint64_t(at)+n<=UINT32_MAX&&ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(at),dst,n,&got)&&got==n;}
std::int64_t Now()noexcept {LARGE_INTEGER q{},f{};if(!QueryPerformanceCounter(&q)||!QueryPerformanceFrequency(&f)||q.QuadPart<=0||f.QuadPart<=0)return 0;
    return(q.QuadPart/f.QuadPart)*1000000000+(q.QuadPart%f.QuadPart)*1000000000/f.QuadPart;}
void Save(const Record& record)noexcept {std::unique_lock lock(recordMutex,std::try_to_lock);if(!lock.owns_lock()){++busy;return;}if(recordCount==records.size()){++overflow;return;}records[recordCount++]=record;}
std::uintptr_t __fastcall Hook(void* self,void*,unsigned count,unsigned rows,unsigned unused,void* workspace){
    Callback callback;
    if(!lifecycle.Admitted())return original(self,count,rows,unused,workspace);
    const auto entryNs=Now();const auto started=startedNs.load(std::memory_order_acquire);
    if(entryNs<=0||entryNs<started||entryNs-started>15000000000ll||calls.fetch_add(1)>=12288){lifecycle.StopAdmission();return original(self,count,rows,unused,workspace);}
    const RigWorkerCall call{reinterpret_cast<unsigned>(self),reinterpret_cast<unsigned>(_ReturnAddress()),count,rows,unused,reinterpret_cast<unsigned>(workspace)};
    const auto owner=resolveOwner?resolveOwner():std::nullopt;
    RigWorkerRead before{};if(owner)before=ReadRigWorker({nullptr,Read,nullptr},binding,call,*owner);
    RigWorkerAssociation association{};if(before.identity)association=AssociateRigWorker(*before.identity,resolveView);
    const auto status=before.identity?association.status:before.status;++statuses[unsigned(status)];
    const auto begin=Now();const bool admit=before.identity&&association.key&&begin>0;
    // Even a rejected nested callback masks the outer producer scope.
    Bc2ReloadProducerBinding::Scope scope(ReloadProducerBinding(),association.key.value_or(ReloadProducerView{}),admit,begin);
    if(admit)++admitted;
    const auto result=original(self,count,rows,unused,workspace); // exactly once
    const auto end=Now();bool exact=false;
    if(admit){const auto currentOwner=resolveOwner?resolveOwner():std::nullopt;
        if(currentOwner&&currentOwner==owner){const auto after=ReadRigWorker({nullptr,Read,nullptr},binding,call,*currentOwner);
            if(after.identity&&*after.identity==*before.identity){const auto again=AssociateRigWorker(*after.identity,resolveView);exact=again.key==association.key;}}
        scope.Complete(exact&&lifecycle.Admitted(),end);
        if(exact)++completed;else ++postChanged;
    }
    // Keep bounded partial paths to explain a failed native lookup without a
    // second capture. They never enter the producer binding.
    if(owner&&call.caller==binding.workerReturn&&before.status!=RigWorkerStatus::OwnerMissing)Save({before.identity.value_or(before.partial),status,exact,begin,end});
    return result;
}
}
bool Install(std::span<const std::byte> bytes,const engine::PeImage& pe,std::uint32_t base,RigWorkerSceneTypes scene,OwnerResolver owner,RigWorkerViewResolver view,bool diagnostic){
    if(!diagnostic||hook||!owner||!view)return false;const auto candidate=DiscoverRigWorker(bytes,pe,scene);
    if(!candidate||!ValidateRigWorkerLive({nullptr,Read,nullptr},*candidate,base))return false;
    binding=*candidate;resolveOwner=owner;resolveView=view;
    auto address=reinterpret_cast<void*>(binding.prepare);
    if(MH_CreateHook(address,reinterpret_cast<void*>(Hook),reinterpret_cast<void**>(&original))!=MH_OK)return false;
    hook=address;return lifecycle.Created();
}
bool BeginGlobalStart()noexcept {return hook&&lifecycle.BeginGlobalStart();}
bool CompleteGlobalStart(bool globalEnableSucceeded)noexcept {
    if(!hook||!lifecycle.PendingGlobalStart())return false;
    const auto now=Now();
    if(!globalEnableSucceeded||now<=0){lifecycle.CompleteGlobalStart(false,RigWorkerEnableResult::Failed);Disable();return false;}
    // MH_ERROR_ENABLED is accepted ONLY after the parent's explicitly bracketed
    // successful global start. Unknown/failed global startup cannot open admission.
    const auto result=MH_EnableHook(hook);
    const auto outcome=result==MH_OK?RigWorkerEnableResult::Enabled:result==MH_ERROR_ENABLED?RigWorkerEnableResult::AlreadyEnabled:RigWorkerEnableResult::Failed;
    startedNs.store(now,std::memory_order_release);
    if(lifecycle.CompleteGlobalStart(true,outcome))return true;
    Disable();return false;
}
void Disable()noexcept {lifecycle.StopAdmission();if(hook){const auto result=MH_DisableHook(hook);lifecycle.ConfirmDisable(result==MH_OK||result==MH_ERROR_DISABLED);}}
bool Quiescent()noexcept {return lifecycle.Quiescent(inFlight.load(std::memory_order_acquire));}
void Report(std::ostream& out){
    out<<"{\"enabled\":"<<(lifecycle.Admitted()?"true":"false")<<",\"native_hook_may_be_enabled\":"<<(lifecycle.EntryMayBeEnabled()?"true":"false")<<",\"in_flight\":"<<inFlight.load()<<",\"calls\":"<<calls.load()<<",\"admitted\":"<<admitted.load()<<",\"completed\":"<<completed.load()<<",\"post_changed\":"<<postChanged.load()<<",\"busy\":"<<busy.load()<<",\"overflow\":"<<overflow.load()<<",\"statuses\":[";
    for(unsigned i=0;i<statuses.size();++i){if(i)out<<',';out<<statuses[i].load();}
    out<<"],\"status_names\":[\"observed\",\"invalid_arguments\",\"wrong_caller\",\"read_failure\",\"owner_missing\",\"owner_ambiguous\",\"bounds\",\"wrong_type\",\"changed_during_read\",\"no_exact_view\",\"multiple_stereo_views\"],\"records\":[";
    std::lock_guard lock(recordMutex);
    for(unsigned i=0;i<recordCount;++i){if(i)out<<',';const auto& r=records[i];const auto& s=r.identity;
        out<<"{\"status\":"<<unsigned(r.status)<<",\"renderer\":"<<s.call.renderer<<",\"workspace\":"<<s.call.workspace<<",\"job\":"<<s.job<<",\"batch\":"<<s.batch<<",\"arena\":"<<s.arena<<",\"arena_base\":"<<s.arenaBase<<",\"view\":"<<s.view<<",\"request\":"<<s.request<<",\"world\":"<<s.world<<",\"frame\":"<<s.nativeFrame<<",\"request_state\":"<<s.requestState<<",\"actor\":"<<s.owner.actor<<",\"weapon\":"<<s.owner.weapon<<",\"row_index\":"<<s.selectedRowIndex<<",\"row_view_mask\":"<<s.selectedViewMask<<",\"after_exact\":"<<(r.afterExact?"true":"false")<<",\"begin_ns\":"<<r.beginNs<<",\"end_ns\":"<<r.endNs<<",\"views\":[";
        for(unsigned v=0;v<s.viewCount&&v<s.views.size();++v){if(v)out<<',';out<<s.views[v];}out<<"],\"observed_vtables\":[";
        for(unsigned v=0;v<s.observedVtables.size();++v){if(v)out<<',';out<<s.observedVtables[v];}
        out<<"],\"rejected_view_index\":"<<s.rejectedViewIndex<<",\"view_details\":[";
        for(unsigned v=0;v<s.viewCount&&v<s.views.size();++v){if(v)out<<',';out<<"{\"view\":"<<s.views[v]<<",\"vtable\":"<<s.viewTables[v]<<",\"request\":"<<s.viewRequests[v]<<",\"parent\":"<<s.viewParents[v]<<'}';}out<<"]}";}
    out<<"],\"native_state_written\":false}";
}
#else
bool Install(std::span<const std::byte>,const engine::PeImage&,std::uint32_t,RigWorkerSceneTypes,OwnerResolver,RigWorkerViewResolver,bool){return false;}
bool BeginGlobalStart()noexcept{return false;}bool CompleteGlobalStart(bool)noexcept{return false;}void Disable()noexcept{}bool Quiescent()noexcept{return true;}
void Report(std::ostream& out){out<<"{\"unsupported_architecture\":true}";}
#endif
}
