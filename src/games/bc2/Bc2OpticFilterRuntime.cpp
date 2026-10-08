#include "Bc2OpticFilterRuntime.h"
#include "Bc2RigWorkerLifecycle.h"
#include "Bc2OpticFilterSession.h"
#include <Windows.h>
#include <intrin.h>
#include <MinHook.h>
#include <atomic>
#include <mutex>
namespace fvr::bc2::opticFilterRuntime {
#if defined(_M_IX86)
namespace {
using Native=std::uintptr_t(__thiscall*)(void*,void*);
Native original=nullptr;void* hook=nullptr;OpticFilterBinding binding{};SourceResolver resolveSource=nullptr;ViewResolver resolveView=nullptr;
// Retain even when shutdown cannot drain: a still-running callback must not
// reference FvrRunNativeProbe's temporary executable vector after it returns.
std::vector<std::byte> ownedImage;
RigWorkerLifecycle lifecycle;std::atomic<unsigned> inFlight=0;
OpticFilterSession session;std::atomic<std::uint64_t> calls=0,ids=0,busy=0,overflow=0,drawDrops=0;
std::atomic<std::int64_t> startedNs=0;
struct Record {std::uint64_t id=0,generation=0;std::uint32_t thread=0;OpticFilterCall call{};OpticFilterStatus status{};
    OpticFilterSample before{},after{};View view{};bool viewVerified=false,ownerRetained=false,cancelled=false;std::int64_t beginNs=0,endNs=0;};
struct Draw {std::uint64_t scope=0,generation=0;std::uint32_t ordinal=0,count=0,start=0;std::int32_t base=0;bool indexed=false;OpticDrawState state{};};
std::array<Record,128> records{};std::array<Draw,512> draws{};unsigned recordCount=0,drawCount=0;std::mutex mutex;
struct Scope {Record* record=nullptr;unsigned drawCount=0;};thread_local Scope* current=nullptr;
struct Guard {Scope* previous;Guard(Scope* s):previous(current){current=s;}~Guard(){current=previous;}};
struct Callback {Callback(){++inFlight;}~Callback(){--inFlight;}};
bool Read(void*,unsigned a,void* out,std::size_t n){SIZE_T got=0;return a>=0x10000&&n&&n<=4096&&std::uint64_t(a)+n<=UINT32_MAX&&
    ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(a),out,n,&got)&&got==n;}
std::int64_t Now(){LARGE_INTEGER q{},f{};if(!QueryPerformanceCounter(&q)||!QueryPerformanceFrequency(&f)||q.QuadPart<=0||f.QuadPart<=0)return 0;
    return(q.QuadPart/f.QuadPart)*1000000000+(q.QuadPart%f.QuadPart)*1000000000/f.QuadPart;}
void Save(const Record& r){std::unique_lock lock(mutex,std::try_to_lock);if(!lock.owns_lock()){++busy;return;}if(recordCount==records.size()){++overflow;return;}records[recordCount++]=r;}
std::uintptr_t __fastcall Hook(void* self,void*,void* filter){Callback callback;Guard mask(nullptr);
    if(!lifecycle.Admitted()||!session.Connected())return original(self,filter);
    const auto now=Now(),started=startedNs.load(std::memory_order_acquire);if(now<=0||now<started||now-started>20000000000ll||calls.fetch_add(1)>=4096){SetConnected(false);return original(self,filter);}
    Record record;record.id=ids.fetch_add(1)+1;record.generation=session.Epoch();record.thread=GetCurrentThreadId();record.beginNs=now;
    record.call={reinterpret_cast<unsigned>(self),reinterpret_cast<unsigned>(filter),reinterpret_cast<unsigned>(_ReturnAddress())};
    const auto source=resolveSource?resolveSource():std::nullopt;
    const auto ticket=source&&source->selected?session.Begin(source->selected->observedNs):std::nullopt;
    if(ticket&&source&&source->selected){const auto read=ReadOpticFilter({nullptr,Read,nullptr},binding,*source->selected,source->owner,record.call,now,true);
        record.status=read.status;if(read.sample)record.before=*read.sample;}else record.status=OpticFilterStatus::Owner;
    if(resolveView){const auto view=resolveView();if(view){record.view=*view;record.viewVerified=true;}}
    // Even unmatched configuration is useful evidence. It never grants a
    // selected-owner association, but the actual typed native call is recorded.
    Scope scope{&record};Guard active(record.call.caller==binding.image.preferredBase+binding.candidates.filterReturn?&scope:nullptr);
    const auto result=original(self,filter); // original exactly once, no locks held
    record.endNs=Now();record.cancelled=record.generation!=session.Epoch()||!session.Connected()||!lifecycle.Admitted();
    if(!record.cancelled&&record.status==OpticFilterStatus::Observed&&source&&source->selected){
        const auto afterSource=resolveSource?resolveSource():std::nullopt;
        if(afterSource&&afterSource->owner==source->owner&&afterSource->selected&&afterSource->selected->owner==source->owner){
            // Reuse the original immutable lease; a newer publication may not
            // renew this callback's proof or disguise an expired original.
            const auto after=ReadOpticFilter({nullptr,Read,nullptr},binding,*source->selected,source->owner,record.call,record.endNs,true);
            if(after.sample){record.after=*after.sample;record.ownerRetained=SameOpticFilterOwner(record.before,record.after);}}}
    Save(record);return result;
}
void Texture(std::ostream& o,const OpticTextureIdentity& v){o<<"{\"view\":"<<v.view<<",\"resource\":"<<v.resource<<",\"dimension\":"<<v.dimension<<",\"width\":"<<v.width<<",\"height\":"<<v.height<<",\"format\":"<<v.format<<",\"mips\":"<<v.mips<<",\"array\":"<<v.arraySize<<",\"samples\":"<<v.samples<<'}';}
}
bool Install(std::span<const std::byte> bytes,const engine::PeImage& pe,unsigned base,SourceResolver source,ViewResolver view,bool diagnostic){
    if(!diagnostic||hook||!source)return false;
    try {ownedImage.assign(bytes.begin(),bytes.end());}catch(...){return false;}
    const auto candidate=DiscoverOpticFilter(ownedImage,pe);
    if(!candidate||!ValidateOpticFilterLive({nullptr,Read,nullptr},*candidate,base))return false;
    binding=*candidate;resolveSource=source;resolveView=view;const auto address=reinterpret_cast<void*>(base+binding.candidates.filterRenderer);
    if(MH_CreateHook(address,reinterpret_cast<void*>(Hook),reinterpret_cast<void**>(&original))!=MH_OK)return false;hook=address;return lifecycle.Created();
}
bool BeginGlobalStart()noexcept{return hook&&lifecycle.BeginGlobalStart();}
bool CompleteGlobalStart(bool success)noexcept {if(!hook||!lifecycle.PendingGlobalStart())return false;const auto now=Now();
    if(!success||now<=0){lifecycle.CompleteGlobalStart(false,RigWorkerEnableResult::Failed);Disable();return false;}
    const auto status=MH_EnableHook(hook);startedNs.store(now);const auto result=status==MH_OK?RigWorkerEnableResult::Enabled:status==MH_ERROR_ENABLED?RigWorkerEnableResult::AlreadyEnabled:RigWorkerEnableResult::Failed;
    if(lifecycle.CompleteGlobalStart(true,result))return true;Disable();return false;
}
void SetConnected(bool value)noexcept {session.SetConnected(value&&lifecycle.Admitted(),Now());}
void Disable()noexcept {SetConnected(false);lifecycle.StopAdmission();if(hook){const auto r=MH_DisableHook(hook);lifecycle.ConfirmDisable(r==MH_OK||r==MH_ERROR_DISABLED);}}
bool Quiescent()noexcept{return lifecycle.Quiescent(inFlight.load());}
void CaptureDraw(ID3D11DeviceContext* c,bool indexed,unsigned count,unsigned start,int base)noexcept {
    if(!current||!current->record||!c||!lifecycle.Admitted()||!session.Connected()||current->record->generation!=session.Epoch())return;
    if(current->drawCount>=8){++drawDrops;return;}const auto ordinal=current->drawCount++;
    Draw draw;draw.scope=current->record->id;draw.generation=current->record->generation;draw.ordinal=ordinal;draw.indexed=indexed;draw.count=count;draw.start=start;draw.base=base;
    draw.state=CaptureOpticDrawState(c);std::unique_lock lock(mutex,std::try_to_lock);if(!lock.owns_lock()){++busy;return;}if(drawCount==draws.size()){++drawDrops;return;}draws[drawCount++]=draw;
}
void Report(std::ostream& o){std::lock_guard lock(mutex);
    o<<"{\"enabled\":"<<(lifecycle.Admitted()?"true":"false")<<",\"connected\":"<<(session.Connected()?"true":"false")<<",\"epoch\":"<<session.Epoch()<<",\"in_flight\":"<<inFlight.load()<<",\"calls\":"<<calls.load()<<",\"busy\":"<<busy.load()<<",\"overflow\":"<<overflow.load()<<",\"draw_drops\":"<<drawDrops.load()<<",\"records\":[";
    for(unsigned i=0;i<recordCount;++i){if(i)o<<',';const auto& r=records[i];const auto& s=r.before;
        o<<"{\"id\":"<<r.id<<",\"epoch\":"<<r.generation<<",\"thread\":"<<r.thread<<",\"status\":"<<unsigned(r.status)<<",\"renderer\":"<<r.call.renderer<<",\"filter\":"<<r.call.filter<<",\"caller\":"<<r.call.caller<<",\"begin_ns\":"<<r.beginNs<<",\"end_ns\":"<<r.endNs<<",\"owner_retained\":"<<(r.ownerRetained?"true":"false")<<",\"cancelled\":"<<(r.cancelled?"true":"false")<<",\"source_sequence\":"<<s.sequence<<",\"source_observed_ns\":"<<s.observedNs<<",\"source_deadline_ns\":"<<s.deadlineNs<<",\"owner\":["<<s.owner.player<<','<<s.owner.soldier<<','<<s.owner.weak<<','<<s.owner.weapon<<','<<s.owner.actorGeneration<<','<<s.owner.equipGeneration<<','<<s.owner.space<<"],\"view_verified\":"<<(r.viewVerified?"true":"false")<<",\"view\":["<<r.view.request<<','<<r.view.view<<','<<r.view.frame<<','<<r.view.eye<<"],\"zoomed_match_mask\":"<<unsigned(s.zoomedMatchMask)<<",\"nonzoomed_match_mask\":"<<unsigned(s.nonZoomedMatchMask)<<",\"native_wrappers\":["<<s.rendererWrappers[0]<<','<<s.rendererWrappers[1]<<','<<s.rendererWrappers[2]<<"],\"scissor\":["<<s.scissor[0]<<','<<s.scissor[1]<<','<<s.scissor[2]<<','<<s.scissor[3]<<"],\"blur_center\":["<<s.blurCenter[0]<<','<<s.blurCenter[1]<<"],\"blur_scale\":"<<s.blurScale;
        o<<",\"asset\":\"";for(char ch:s.assetName){if(!ch)break;if(ch=='"'||ch=='\\')o<<'\\';o<<ch;}o<<"\",\"configured_states\":[";
        for(unsigned n=0;n<s.stateCount;++n){if(n)o<<',';o<<"{\"address\":"<<s.stateAddresses[n]<<",\"zoomed_filter\":"<<s.zoomedFilters[n]<<",\"nonzoomed_filter\":"<<s.nonZoomedFilters[n]<<",\"zoom_mesh\":"<<s.zoomMeshes[n]<<'}';}
        o<<"],\"native_wrappers_after\":["<<r.after.rendererWrappers[0]<<','<<r.after.rendererWrappers[1]<<','<<r.after.rendererWrappers[2]<<"]}";}

    o<<"],\"draws\":[";for(unsigned i=0;i<drawCount;++i){if(i)o<<',';const auto& d=draws[i];const auto& s=d.state;
        o<<"{\"scope\":"<<d.scope<<",\"epoch\":"<<d.generation<<",\"ordinal\":"<<d.ordinal<<",\"indexed\":"<<(d.indexed?"true":"false")<<",\"count\":"<<d.count<<",\"start\":"<<d.start<<",\"base\":"<<d.base<<",\"context\":"<<s.context<<",\"vs\":"<<s.vertexShader<<",\"ps\":"<<s.pixelShader<<",\"complete\":"<<(s.complete?"true":"false")<<",\"viewport_count\":"<<s.viewportCount<<",\"scissor_count\":"<<s.scissorCount<<",\"targets\":[";
        for(unsigned n=0;n<s.targets.size();++n){if(n)o<<',';Texture(o,s.targets[n]);}o<<"],\"depth\":";Texture(o,s.depth);o<<",\"pixel_resources\":[";for(unsigned n=0;n<s.pixelResources.size();++n){if(n)o<<',';Texture(o,s.pixelResources[n]);}o<<"],\"viewports\":[";
        for(unsigned n=0;n<s.viewportCount&&n<4;++n){if(n)o<<',';o<<'[';for(unsigned v=0;v<6;++v){if(v)o<<',';o<<s.viewports[n][v];}o<<']';}o<<"],\"scissors\":[";
        for(unsigned n=0;n<s.scissorCount&&n<4;++n){if(n)o<<',';o<<'[';for(unsigned v=0;v<4;++v){if(v)o<<',';o<<s.scissors[n][v];}o<<']';}o<<"],\"blend_state\":"<<s.blendState<<",\"depth_state\":"<<s.depthState<<",\"stencil_reference\":"<<s.stencilReference<<",\"sample_mask\":"<<s.sampleMask<<",\"topology\":"<<s.topology<<'}';}
    o<<"],\"native_state_written\":false,\"ads_input_changed\":false,\"render_authority\":false}";
}
#else
bool Install(std::span<const std::byte>,const engine::PeImage&,unsigned,SourceResolver,ViewResolver,bool){return false;}
bool BeginGlobalStart()noexcept{return false;}bool CompleteGlobalStart(bool)noexcept{return false;}void SetConnected(bool)noexcept{}void Disable()noexcept{}bool Quiescent()noexcept{return true;}
void CaptureDraw(ID3D11DeviceContext*,bool,unsigned,unsigned,int)noexcept{}
void Report(std::ostream& o){o<<"{\"unsupported_architecture\":true,\"render_authority\":false}";}
#endif
}
