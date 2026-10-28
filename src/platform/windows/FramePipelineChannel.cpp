#include "fvr/ipc/FramePipelineChannel.h"
#include "fvr/ipc/FeedbackChannel.h"
#include <Windows.h>
#include <objbase.h>
#include <cstring>
#include <span>
namespace fvr::ipc {
namespace {
struct PipelineHandle {HANDLE h=nullptr;~PipelineHandle(){if(h)CloseHandle(h);}};
constexpr std::uint32_t LookaheadFeature=1;
constexpr std::uint32_t ExportOverlapFeature=2,KnownFeatures=LookaheadFeature|ExportOverlapFeature;
template<std::size_t N> struct alignas(8) BasicPipelineShared {
    // Formerly reserved0: old v9 endpoints reject any nonzero value. New
    // producers accept only known features and pin the negotiated value.
    std::uint32_t magic=0x31434646,version=N==3?10:9,bytes=0,features=0;
    std::array<std::uint8_t,16> session{};
    std::uint32_t hostPid=0,producerPid=0;
    std::uint64_t hostStarted=0,producerStarted=0;
    BasicFramePipeline<N> pipeline{};
    std::array<graphics::BodyPropFrame,N> props{};
    std::array<graphics::SceneDepthFrame,N> depth{};
    InputPacket input{};
    FrameRequest queued{}; // One immutable unassigned request, never another source/lease.
    BasicPipelineCaptureReleases<N> captureReleased{};
    std::array<std::uint64_t,3> reserved{};
};
using PipelineShared=BasicPipelineShared<2>;
using ThreeSourceShared=BasicPipelineShared<3>;
static_assert(sizeof(ThreeSourceShared)==10000&&offsetof(ThreeSourceShared,pipeline)==56&&
    offsetof(ThreeSourceShared,props)==1392&&offsetof(ThreeSourceShared,depth)==8832&&
    offsetof(ThreeSourceShared,queued)==9704&&offsetof(ThreeSourceShared,captureReleased)==9952&&
    offsetof(ThreeSourceShared,reserved)==9976);
static_assert(sizeof(PipelineShared)==6888&&offsetof(PipelineShared,pipeline)==56&&
    offsetof(PipelineShared,props)==952&&offsetof(PipelineShared,depth)==5912&&offsetof(PipelineShared,queued)==6600&&
    offsetof(PipelineShared,captureReleased)==6848&&offsetof(PipelineShared,reserved)==6864);
static_assert(std::is_trivially_copyable_v<PipelineShared>);
static_assert(std::is_trivially_copyable_v<ThreeSourceShared>);
struct PipelineView {
    std::span<PipelineSlot> slots;
    std::uint64_t& lastRequest;std::uint32_t& enabled;std::uint32_t& closing;
    template<std::size_t N> explicit PipelineView(BasicFramePipeline<N>& p):
        slots(p.slots),lastRequest(p.lastRequest),enabled(p.enabled),closing(p.closing){}
};
struct PipelineSharedView {
    std::uint32_t& magic;std::uint32_t& version;std::uint32_t& bytes;std::uint32_t& features;
    std::array<std::uint8_t,16>& session;std::uint32_t& hostPid;std::uint32_t& producerPid;
    std::uint64_t& hostStarted;std::uint64_t& producerStarted;PipelineView pipeline;
    std::span<graphics::BodyPropFrame> props;std::span<graphics::SceneDepthFrame> depth;
    InputPacket& input;FrameRequest& queued;std::span<std::uint64_t> captureReleased;std::span<std::uint64_t> reserved;
    template<std::size_t N> explicit PipelineSharedView(BasicPipelineShared<N>& s):
        magic(s.magic),version(s.version),bytes(s.bytes),features(s.features),session(s.session),
        hostPid(s.hostPid),producerPid(s.producerPid),hostStarted(s.hostStarted),producerStarted(s.producerStarted),
        pipeline(s.pipeline),props(s.props),depth(s.depth),input(s.input),queued(s.queued),
        captureReleased(s.captureReleased),reserved(s.reserved){}
};
std::int64_t PipelineNow(){LARGE_INTEGER n{};return QueryPerformanceCounter(&n)?n.QuadPart:0;}
std::uint64_t PipelineStarted(HANDLE process){FILETIME a{},b{},c{},d{};if(!GetProcessTimes(process,&a,&b,&c,&d))return 0;return (std::uint64_t(a.dwHighDateTime)<<32)|a.dwLowDateTime;}
bool PipelineTokenBytes(const std::wstring& token,std::array<std::uint8_t,16>& bytes){
    if(token.size()!=32)return false;bool any=false;
    for(unsigned i=0;i<16;++i){unsigned value=0;for(unsigned j=0;j<2;++j){const auto c=token[i*2+j];
        if(c>=L'0'&&c<=L'9')value=value*16+unsigned(c-L'0');else if(c>=L'a'&&c<=L'f')value=value*16+unsigned(c-L'a'+10);else return false;}
        bytes[i]=std::uint8_t(value);any|=value!=0;}return any;
}
std::wstring PipelineNewToken(){GUID guid{};if(FAILED(CoCreateGuid(&guid)))return {};const auto* b=reinterpret_cast<const unsigned char*>(&guid);std::wstring token;
    for(unsigned i=0;i<16;++i){token+=L"0123456789abcdef"[b[i]>>4];token+=L"0123456789abcdef"[b[i]&15];}return token;
}
bool PipelineRequirements(const runtime::PresentationRequirements& r){
    // Unchanged per-source ceiling. Total allocation is the graphics owner's gate.
    return r.width&&r.height&&std::uint64_t(r.width)*r.height*24<=202051584;
}
}
struct FramePipelineChannel::State {
    FeedbackChannel feedback;bool host=false,fault=false,pendingAck=false,ackConsumed=false,cancelRequested=false,stopRequested=false;
    std::uint32_t features=0;bool featuresPinned=false;unsigned sourceCount=2;
    PipelineSlotKey active{},deliveredKey{};graphics::PairTicket delivered{};
    // Host-only ordered authority. Terminal IDs stay here until Poll reports
    // that exact head once; slot reuse or queued expiry cannot reassociate it.
    std::array<PipelineSlotKey,2> requests{};unsigned requestCount=0;
    graphics::BodyPropFrame deliveredProps{};graphics::SceneDepthFrame deliveredDepth{};graphics::TextureDescriptor deliveredDescriptor{};
    PipelineHandle mapping,mutex,response,peer;void* mapped=nullptr;std::unique_ptr<PipelineSharedView> shared;
    std::array<std::uint8_t,16> session{};std::uint64_t next=0;std::int64_t frequency=0;
    ~State(){shared.reset();if(mapped)UnmapViewOfFile(mapped);}
    unsigned Version()const{return sourceCount==3?10u:9u;}
    DWORD Bytes()const{return sourceCount==3?DWORD(sizeof(ThreeSourceShared)):DWORD(sizeof(PipelineShared));}
    std::wstring Prefix(const std::wstring& token)const{return L"Local\\FrostbiteVR.Control.v"+std::to_wstring(Version())+L"."+token;}
    bool Bind(){
        mapped=MapViewOfFile(mapping.h,FILE_MAP_READ|FILE_MAP_WRITE,0,0,Bytes());if(!mapped)return false;
        if(sourceCount==3)shared=std::make_unique<PipelineSharedView>(*static_cast<ThreeSourceShared*>(mapped));
        else shared=std::make_unique<PipelineSharedView>(*static_cast<PipelineShared*>(mapped));
        return true;
    }
    template<std::size_t N> bool Initialize(){
        BasicPipelineShared<N> initial{};initial.bytes=sizeof(initial);initial.features=features;initial.session=session;
        initial.hostPid=GetCurrentProcessId();initial.hostStarted=PipelineStarted(GetCurrentProcess());
        if(!initial.hostStarted||!PipelineEnable(initial.pipeline))return false;
        std::memcpy(mapped,&initial,sizeof(initial));return true;
    }
    void Fault(){fault=true;}
    bool Lookahead()const{return (features&LookaheadFeature)!=0;}
    bool ExportOverlap()const{return (features&ExportOverlapFeature)!=0;}
    void PopRequest(){requests[0]=requests[1];requests[1]={};--requestCount;}
    void ObserveTimeout(FrameTimeoutSnapshot* out,const PipelineSlotKey& key,
        FrameTimeoutReason reason,std::int64_t now)const noexcept {
        if(!out)return;
        // Only called with the existing PollRequest mutex held, before mutation.
        // Reuse its timer sample; this observation does not read another clock.
        out->available=true;out->reason=reason;out->requestedKey=key;out->wireVersion=Version();
        out->nowQpc=now;out->frequencyQpc=frequency;out->clockValid=now>0&&frequency>0;
        out->queuedRequestId=shared->queued.requestId;out->queuedDeadlineQpc=shared->queued.deadlineQpc;
        if(key.slot<sourceCount){
            const auto& slot=shared->pipeline.slots[key.slot];out->requestObserved=true;
            out->observedRequestId=slot.frame.request.requestId;out->observedDeadlineQpc=slot.frame.request.deadlineQpc;
            out->phaseValid=true;out->phase=slot.frame.phase;out->nativeHeldRequest=slot.nativeHeldRequest;
            out->hasSource=slot.hasSource!=0;out->retiring=slot.retiring!=0;
            out->captureReleasedRequest=shared->captureReleased[key.slot];
            out->terminalPhase=slot.frame.phase==Phase::Idle||slot.frame.phase==Phase::Complete||slot.frame.phase==Phase::Cancelled;
        }else if(reason==FrameTimeoutReason::QueuedDeadline){
            out->requestObserved=true;out->observedRequestId=shared->queued.requestId;
            out->observedDeadlineQpc=shared->queued.deadlineQpc;
        }
        out->deadlineExpired=out->clockValid&&out->requestObserved&&now>=out->observedDeadlineQpc;
    }
    bool Header()const {
        if(shared->magic!=0x31434646||shared->version!=Version()||shared->bytes!=Bytes()||shared->session!=session||
            (shared->features&~KnownFeatures)||((shared->features&ExportOverlapFeature)&&!(shared->features&LookaheadFeature))||
            (featuresPinned&&shared->features!=features)||(sourceCount==3&&shared->features!=KnownFeatures)||
            !shared->hostPid||!shared->hostStarted||shared->pipeline.enabled!=1||shared->pipeline.closing>1)return false;
        for(auto r:shared->reserved)if(r)return false;
        if(!PipelineCaptureReleasesValid(shared->pipeline,shared->captureReleased,
            (shared->features&ExportOverlapFeature)!=0))return false;
        unsigned deliveredCount=0;
        for(const auto& s:shared->pipeline.slots){
            if(std::uint32_t(s.frame.phase)>std::uint32_t(Phase::Closed)||s.frame.reserved||s.frame.consumed>1||s.hasSource>1||s.retiring>1)return false;
            if(s.nativeHeldRequest&&s.nativeHeldRequest!=s.frame.request.requestId)return false;
            if((s.hasSource||s.retiring)&&!s.nativeHeldRequest)return false;
            deliveredCount+=s.frame.phase==Phase::Delivered;
        }
        if(sourceCount==3){unsigned pending=shared->queued.requestId?1u:0u;
            for(const auto& slot:shared->pipeline.slots)pending+=slot.frame.phase==Phase::Requested||slot.frame.phase==Phase::Rendering||slot.frame.phase==Phase::Ready;
            if(pending>2)return false;
        }
        return deliveredCount<=1;
    }
    struct Lock {
        State& s;bool held=false;
        explicit Lock(State& state,DWORD timeout=0):s(state){if(s.fault||!s.mutex.h||!s.shared)return;
            const auto r=WaitForSingleObject(s.mutex.h,timeout);
            if(r==WAIT_OBJECT_0){held=true;if(!s.Header())s.Fault();
                else if(s.stopRequested){s.shared->queued={};PipelineClose(s.shared->pipeline);}}
            else if(r==WAIT_ABANDONED){held=true;s.Fault();}else if(r!=WAIT_TIMEOUT)s.Fault();}
        explicit operator bool()const{return held&&!s.fault;}
        ~Lock(){if(held)ReleaseMutex(s.mutex.h);}
    };
    bool Peer(){
        if(fault)return false;const auto pid=host?shared->producerPid:shared->hostPid;const auto birth=host?shared->producerStarted:shared->hostStarted;
        if(!pid)return false;
        if(!peer.h){peer.h=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
            if(!peer.h||!birth||PipelineStarted(peer.h)!=birth){Fault();return false;}}
        if(WaitForSingleObject(peer.h,0)!=WAIT_TIMEOUT){Fault();return false;}return true;
    }
    void ApplyCancellation(){if(cancelRequested){shared->queued={};PipelineCancelSuccessors(shared->pipeline);
        active={};requests={};requestCount=0;cancelRequested=false;}}
    bool CanDefer()const {
        if(sourceCount!=2||shared->queued.requestId||deliveredKey.slot>=sourceCount||!deliveredKey.request||PipelineDelivered(shared->pipeline)!=1)return false;
        const auto& held=shared->pipeline.slots[1-deliveredKey.slot];
        // Only the ACK-to-graphics-retirement bubble. No extra queue behind a
        // camera render or Ready source, and no new source allocation implied.
        return held.nativeHeldRequest&&held.hasSource&&
            (held.frame.phase==Phase::Complete||(held.frame.phase==Phase::Idle&&held.retiring));
    }
    void Promote(std::int64_t now){
        auto& queued=shared->queued;if(!queued.requestId||shared->pipeline.closing)return;
        if(now<=0||now>=queued.deadlineQpc){queued={};return;}
        PipelineSlotKey key{};
        const bool promoted=Lookahead()?PipelineLookaheadRequest(shared->pipeline,queued,now,key):
            PipelineRequest(shared->pipeline,queued,now,PipelineDelivered(shared->pipeline)==1,key);
        if(promoted)queued={};
    }
    void Flush(){ApplyCancellation();if(pendingAck){
        if(!PipelineFeedback(shared->pipeline,delivered,ackConsumed,false)){Fault();return;}
        pendingAck=false;deliveredKey={};}}
    bool Delivered(const graphics::PairTicket& ticket)const {
        // Host-thread local authority, captured atomically with Deliver. Neither
        // native publication of the other slot nor IPC mutex contention can
        // change this snapshot. Feedback revokes it before committing its ACK.
        return deliveredKey.slot<sourceCount&&deliveredKey.request&&!pendingAck&&
            !fault&&peer.h&&WaitForSingleObject(peer.h,0)==WAIT_TIMEOUT&&
            !std::memcmp(&ticket,&delivered,sizeof(ticket));
    }
};
FramePipelineChannel::FramePipelineChannel()=default;
FramePipelineChannel::~FramePipelineChannel(){Close();}
void FramePipelineChannel::Close()noexcept {
    // Closing forbids admission but does not erase a surviving peer's exact
    // held-source marker. Physical retirement clears it, or the mapping's final
    // handle closes with the session; neither grants reuse in this lifetime.
    if(state_){auto& s=*state_;{State::Lock lock(s,20);if(lock){if(s.host)s.Flush();s.shared->queued={};PipelineClose(s.shared->pipeline);SetEvent(s.response.h);}}state_.reset();}token_.clear();
}
bool FramePipelineChannel::CreateHost(bool lookahead,bool exportOverlap,bool thirdSource)noexcept {
    Close();if((exportOverlap&&!lookahead)||(thirdSource&&(!lookahead||!exportOverlap)))return false;
    try{auto s=std::make_unique<State>();s->host=true;s->sourceCount=thirdSource?3u:2u;s->features=(lookahead?LookaheadFeature:0)|(exportOverlap?ExportOverlapFeature:0);s->featuresPinned=true;
        const auto token=PipelineNewToken();if(!PipelineTokenBytes(token,s->session))return false;
        const auto prefix=s->Prefix(token);
        s->mutex.h=CreateMutexW(nullptr,FALSE,(prefix+L".mutex").c_str());if(!s->mutex.h||GetLastError()==ERROR_ALREADY_EXISTS)return false;
        s->response.h=CreateEventW(nullptr,FALSE,FALSE,(prefix+L".response").c_str());if(!s->response.h||GetLastError()==ERROR_ALREADY_EXISTS)return false;
        s->mapping.h=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,s->Bytes(),(prefix+L".map").c_str());if(!s->mapping.h||GetLastError()==ERROR_ALREADY_EXISTS)return false;
        if(!s->Bind())return false;
        LARGE_INTEGER hz{};if(!QueryPerformanceFrequency(&hz)||hz.QuadPart<=0)return false;s->frequency=hz.QuadPart;
        if(!(thirdSource?s->Initialize<3>():s->Initialize<2>()))return false;
        s->feedback.CreateHost(token);token_=token;state_=std::move(s);return true;
    }catch(...){Close();return false;}
}
bool FramePipelineChannel::ConnectProducer(const std::wstring& token,bool thirdSource)noexcept {
    Close();try{auto s=std::make_unique<State>();s->sourceCount=thirdSource?3u:2u;if(!PipelineTokenBytes(token,s->session))return false;const auto prefix=s->Prefix(token);
        s->mutex.h=OpenMutexW(SYNCHRONIZE|MUTEX_MODIFY_STATE,FALSE,(prefix+L".mutex").c_str());if(!s->mutex.h)return false;
        s->response.h=OpenEventW(EVENT_MODIFY_STATE|SYNCHRONIZE,FALSE,(prefix+L".response").c_str());if(!s->response.h)return false;
        s->mapping.h=OpenFileMappingW(FILE_MAP_READ|FILE_MAP_WRITE,FALSE,(prefix+L".map").c_str());if(!s->mapping.h)return false;
        if(!s->Bind())return false;
        const auto birth=PipelineStarted(GetCurrentProcess());if(!birth)return false;
        {State::Lock lock(*s,20);if(!lock||s->shared->producerPid||s->shared->pipeline.closing||!s->Peer())return false;
            s->features=s->shared->features;s->featuresPinned=true;
            s->shared->producerPid=GetCurrentProcessId();s->shared->producerStarted=birth;}
        s->feedback.ConnectProducer(token);token_=token;state_=std::move(s);return true;
    }catch(...){Close();return false;}
}
bool FramePipelineChannel::Connected()noexcept {if(!state_)return false;auto& s=*state_;State::Lock lock(s);return lock&&!s.shared->pipeline.closing&&s.Peer();}
bool FramePipelineChannel::ExportOverlapEnabled()const noexcept {return state_&&state_->ExportOverlap();}
unsigned FramePipelineChannel::SourceSlotCount()const noexcept{return state_?state_->sourceCount:0u;}
ChannelResult FramePipelineChannel::BeginRequest(const runtime::PresentationRequirements& r,const runtime::TrackingFrame& t,unsigned ms,bool queued)noexcept {
    if(!state_||!state_->host)return ChannelResult::Closed;if(!ms||ms>200||!PipelineRequirements(r))return ChannelResult::Invalid;
    auto& s=*state_;if(s.Lookahead())return ChannelResult::Invalid;
    State::Lock lock(s);if(!lock)return s.fault?ChannelResult::Closed:ChannelResult::Busy;
    if(!s.Peer()||s.shared->pipeline.closing)return ChannelResult::Closed;
    if(queued)s.ApplyCancellation();else s.Flush();if(s.fault)return ChannelResult::Closed;
    if(s.active.request||s.pendingAck||(queued?!s.deliveredKey.request:s.deliveredKey.request!=0))return ChannelResult::Busy;
    const auto now=PipelineNow(),ticks=(s.frequency/1000)*ms+(s.frequency%1000)*ms/1000;
    if(now<=0||ticks<=0||now>INT64_MAX-ticks||s.next==UINT64_MAX)return ChannelResult::Invalid;
    FrameRequest request{};if(!Encode(r,t,s.next+1,now+ticks,request))return ChannelResult::Invalid;
    PipelineSlotKey key{};
    if(!PipelineRequest(s.shared->pipeline,request,now,queued,key)){
        if(!queued||!s.CanDefer())return ChannelResult::Busy;
        s.shared->queued=request;key={request.requestId,UINT32_MAX,0};
    }
    s.active=key;++s.next;ResetEvent(s.response.h);return ChannelResult::Ok;
}
ChannelResult FramePipelineChannel::SubmitLookaheadRequest(const runtime::PresentationRequirements& r,const runtime::TrackingFrame& t,unsigned ms)noexcept {
    if(!state_||!state_->host)return ChannelResult::Closed;auto& s=*state_;
    if(!s.Lookahead()||!ms||ms>200||!PipelineRequirements(r))return ChannelResult::Invalid;
    State::Lock lock(s);if(!lock)return s.fault?ChannelResult::Closed:ChannelResult::Busy;
    if(!s.Peer()||s.shared->pipeline.closing)return ChannelResult::Closed;
    // Commit known invalidation before any deferred ACK can unlock a source.
    s.Flush();if(s.fault)return ChannelResult::Closed;
    if(s.requestCount==s.requests.size()||s.shared->queued.requestId)return ChannelResult::Busy;
    const auto now=PipelineNow(),ticks=(s.frequency/1000)*ms+(s.frequency%1000)*ms/1000;
    if(now<=0||ticks<=0||now>INT64_MAX-ticks||s.next==UINT64_MAX)return ChannelResult::Invalid;
    FrameRequest request{};if(!Encode(r,t,s.next+1,now+ticks,request))return ChannelResult::Invalid;
    PipelineSlotKey key{};
    if(!PipelineLookaheadRequest(s.shared->pipeline,request,now,key)){
        // One metadata-only request can wait for exact physical retirement.
        // The original sample/deadline survive promotion unchanged.
        s.shared->queued=request;key={request.requestId,UINT32_MAX,0};
    }
    s.requests[s.requestCount++]=key;++s.next;ResetEvent(s.response.h);return ChannelResult::Ok;
}
ChannelResult FramePipelineChannel::PollRequest(graphics::TextureDescriptor& d,graphics::PairTicket& t,FrameTimeoutSnapshot* timeout)noexcept {
    if(timeout)*timeout={};
    if(!state_||!state_->host)return ChannelResult::Closed;auto& s=*state_;State::Lock lock(s);
    if(!lock)return s.fault?ChannelResult::Closed:ChannelResult::Busy;if(!s.Peer()||s.shared->pipeline.closing)return ChannelResult::Closed;s.ApplyCancellation();
    if(s.Lookahead()){
        if(!s.requestCount)return s.deliveredKey.request?ChannelResult::Busy:ChannelResult::Invalid;
        auto& key=s.requests[0];const auto now=PipelineNow();
        if(key.slot==UINT32_MAX){
            if(s.shared->queued.requestId==key.request){
                if(now<=0||now>=s.shared->queued.deadlineQpc){s.ObserveTimeout(timeout,key,FrameTimeoutReason::QueuedDeadline,now);s.shared->queued={};s.PopRequest();return ChannelResult::Timeout;}
                return ChannelResult::Busy;
            }
            for(unsigned index=0;index<s.sourceCount;++index)if(s.shared->pipeline.slots[index].frame.request.requestId==key.request){key.slot=index;break;}
            if(key.slot==UINT32_MAX){s.ObserveTimeout(timeout,key,FrameTimeoutReason::RequestMissing,now);s.PopRequest();return ChannelResult::Timeout;}
        }
        auto& slot=s.shared->pipeline.slots[key.slot];
        if(slot.frame.request.requestId!=key.request){s.ObserveTimeout(timeout,key,FrameTimeoutReason::SlotReplaced,now);s.PopRequest();return ChannelResult::Timeout;}
        if(now<=0||now>=slot.frame.request.deadlineQpc||slot.frame.phase==Phase::Idle||slot.frame.phase==Phase::Complete||slot.frame.phase==Phase::Cancelled){
            s.ObserveTimeout(timeout,key,FrameTimeoutReason::SlotDeadlineOrTerminal,now);Cancel(slot.frame,key.request);s.PopRequest();return ChannelResult::Timeout;
        }
        if(PipelineLookaheadDeliver(s.shared->pipeline,key,now,d,t)){
            s.deliveredKey=key;s.PopRequest();s.delivered=t;s.deliveredDescriptor=d;
            s.deliveredProps=s.shared->props[s.deliveredKey.slot];s.deliveredDepth=s.shared->depth[s.deliveredKey.slot];return ChannelResult::Ok;
        }
        return ChannelResult::Busy;
    }
    if(!s.active.request)return s.deliveredKey.request?ChannelResult::Busy:ChannelResult::Invalid;
    const auto now=PipelineNow();
    if(s.active.slot==UINT32_MAX){
        if(s.shared->queued.requestId==s.active.request){
            if(now<=0||now>=s.shared->queued.deadlineQpc){s.ObserveTimeout(timeout,s.active,FrameTimeoutReason::QueuedDeadline,now);s.shared->queued={};s.active={};return ChannelResult::Timeout;}
            return ChannelResult::Busy;
        }
        for(unsigned index=0;index<s.sourceCount;++index)if(s.shared->pipeline.slots[index].frame.request.requestId==s.active.request){s.active.slot=index;break;}
        if(s.active.slot==UINT32_MAX){s.ObserveTimeout(timeout,s.active,FrameTimeoutReason::RequestMissing,now);s.active={};return ChannelResult::Timeout;}
    }
    auto& slot=s.shared->pipeline.slots[s.active.slot];
    if(slot.frame.request.requestId!=s.active.request){s.ObserveTimeout(timeout,s.active,FrameTimeoutReason::SlotReplaced,now);s.active={};return ChannelResult::Timeout;}
    if(now<=0||now>=slot.frame.request.deadlineQpc||slot.frame.phase==Phase::Idle||slot.frame.phase==Phase::Complete||slot.frame.phase==Phase::Cancelled){
        s.ObserveTimeout(timeout,s.active,FrameTimeoutReason::SlotDeadlineOrTerminal,now);Cancel(slot.frame,s.active.request);s.active={};return ChannelResult::Timeout;}
    if(PipelineDeliver(s.shared->pipeline,s.active,now,d,t)){
        s.deliveredKey=s.active;s.active={};s.delivered=t;s.deliveredDescriptor=d;s.deliveredProps=s.shared->props[s.deliveredKey.slot];s.deliveredDepth=s.shared->depth[s.deliveredKey.slot];return ChannelResult::Ok;}
    return ChannelResult::Busy;
}
ChannelResult FramePipelineChannel::CancelRequest()noexcept {
    if(!state_||!state_->host)return ChannelResult::Closed;auto& s=*state_;s.cancelRequested=true;return FlushFeedback();
}
ChannelResult FramePipelineChannel::Feedback(const graphics::PairTicket& ticket,bool consumed)noexcept {
    if(!state_||!state_->host)return ChannelResult::Closed;auto& s=*state_;
    if(!s.deliveredKey.request||std::memcmp(&s.delivered,&ticket,sizeof(ticket))||(s.pendingAck&&s.ackConsumed!=consumed))return ChannelResult::Invalid;
    s.pendingAck=true;s.ackConsumed=consumed;return FlushFeedback();
}
ChannelResult FramePipelineChannel::FlushFeedback()noexcept {
    if(!state_||!state_->host)return ChannelResult::Closed;auto& s=*state_;if(!s.pendingAck&&!s.cancelRequested)return ChannelResult::Ok;
    State::Lock lock(s);if(!lock)return s.fault?ChannelResult::Closed:ChannelResult::Busy;if(!s.Peer())return ChannelResult::Closed;
    s.Flush();return s.fault?ChannelResult::Closed:(s.deliveredKey.request?ChannelResult::Busy:ChannelResult::Ok);
}
ChannelResult FramePipelineChannel::TryTake(FrameRequest& request,PipelineSlotKey& key)noexcept {
    request={};key={};if(!state_||state_->host)return ChannelResult::Closed;auto& s=*state_;State::Lock lock(s);
    if(!lock)return s.fault?ChannelResult::Closed:ChannelResult::Busy;if(!s.Peer()||s.shared->pipeline.closing)return ChannelResult::Closed;
    const auto now=PipelineNow();s.Promote(now);
    const bool taken=s.Lookahead()?PipelineLookaheadTake(s.shared->pipeline,now,key,request,
        s.ExportOverlap()?&s.shared->captureReleased:nullptr):PipelineTake(s.shared->pipeline,now,key,request);
    return taken?ChannelResult::Ok:ChannelResult::Busy;
}
ChannelResult FramePipelineChannel::MarkCaptureReleased(const PipelineSlotKey& key)noexcept {
    if(!state_||state_->host||!state_->ExportOverlap())return ChannelResult::Invalid;
    auto& s=*state_;State::Lock lock(s);if(!lock)return s.fault?ChannelResult::Closed:ChannelResult::Busy;
    if(!s.Peer()||s.shared->pipeline.closing)return ChannelResult::Closed;
    return PipelineMarkCaptureReleased(s.shared->pipeline,s.shared->captureReleased,key)?ChannelResult::Ok:ChannelResult::Invalid;
}
ChannelResult FramePipelineChannel::Publish(const PipelineSlotKey& key,const graphics::TextureDescriptor& d,const graphics::PairTicket& t,
    const graphics::BodyPropFrame* props,const graphics::SceneDepthFrame* depth)noexcept {
    if(!state_||state_->host)return ChannelResult::Closed;
    if(depth&&(depth->samples!=1||depth->quality||!graphics::SceneDepthMatches(*depth,d,t)))return ChannelResult::Invalid;
    auto& s=*state_;State::Lock lock(s);if(!lock)return s.fault?ChannelResult::Closed:ChannelResult::Busy;if(!s.Peer())return ChannelResult::Closed;
    if(key.slot>=s.sourceCount)return ChannelResult::Invalid;
    // Defensive cross-slot resource proof. The bridge must enforce this BEFORE
    // enqueueing writes; publication validation cannot repair an earlier alias.
    const auto colors=graphics::LegacyResourceIds(d);
    std::array<std::uint64_t,6> resources{colors[0],colors[1],0,0,0,0};
    if(depth)for(unsigned eye=0;eye<2;++eye)for(unsigned stage=0;stage<2;++stage)
        resources[2+eye*2+stage]=depth->resources[eye][stage];
    for(unsigned other=0;other<s.sourceCount;++other)if(other!=key.slot&&s.shared->pipeline.slots[other].hasSource){
        const auto oldColors=graphics::LegacyResourceIds(s.shared->pipeline.slots[other].frame.descriptor);
        const auto& z=s.shared->depth[other];
        const std::array<std::uint64_t,6> held{oldColors[0],oldColors[1],z.resources[0][0],z.resources[0][1],z.resources[1][0],z.resources[1][1]};
        for(auto id:resources)if(id)for(auto old:held)if(old&&id==old)return ChannelResult::Invalid;
    }
    const auto result=PipelinePublish(s.shared->pipeline,key,PipelineNow(),d,t);
    if(result==PipelinePublishResult::Rejected)return ChannelResult::Invalid;
    s.shared->props[key.slot]=props&&graphics::BodyPropFrameMatches(*props,t)?*props:graphics::BodyPropFrame{};
    // Overwrite the attachment on EVERY publication, including slot reuse.
    s.shared->depth[key.slot]=depth?*depth:graphics::SceneDepthFrame{};SetEvent(s.response.h);
    return result==PipelinePublishResult::Published?ChannelResult::Ok:ChannelResult::Timeout;
}
ChannelResult FramePipelineChannel::Skip(const PipelineSlotKey& key)noexcept {
    if(!state_||state_->host)return ChannelResult::Closed;auto& s=*state_;State::Lock lock(s);
    if(!lock)return s.fault?ChannelResult::Closed:ChannelResult::Busy;
    if(!PipelineSkip(s.shared->pipeline,key))return ChannelResult::Invalid;SetEvent(s.response.h);return ChannelResult::Ok;
}
ChannelResult FramePipelineChannel::ClaimRetirement(const PipelineSlotKey& key,PipelineRetirement& out)noexcept {
    out={};if(!state_||state_->host)return ChannelResult::Closed;auto& s=*state_;State::Lock lock(s);
    if(!lock)return s.fault?ChannelResult::Closed:ChannelResult::Busy;
    // Peer exit never implies positive completion or safe reuse. Owning bridge
    // teardown must retire all sets negatively instead of continuing a session.
    if(!s.Peer())return ChannelResult::Closed;
    return PipelineClaimRetirement(s.shared->pipeline,key,PipelineNow(),out)?ChannelResult::Ok:ChannelResult::Busy;
}
ChannelResult FramePipelineChannel::FinishRetirement(const PipelineRetirement& value)noexcept {
    if(!state_||state_->host)return ChannelResult::Closed;auto& s=*state_;State::Lock lock(s);
    if(!lock)return s.fault?ChannelResult::Closed:ChannelResult::Busy;
    if(!PipelineFinishRetirement(s.shared->pipeline,value,&s.shared->captureReleased))return ChannelResult::Invalid;
    s.shared->props[value.key.slot]={};s.shared->depth[value.key.slot]={};return ChannelResult::Ok;
}
ChannelResult FramePipelineChannel::StopAdmission()noexcept {
    if(!state_||state_->host)return ChannelResult::Invalid;auto& s=*state_;
    // Intent survives contention. Every subsequent producer lock commits it
    // before touching slots; no admission-thread callback or blocking close.
    s.stopRequested=true;State::Lock lock(s);
    return lock?ChannelResult::Ok:(s.fault?ChannelResult::Closed:ChannelResult::Busy);
}
ChannelResult FramePipelineChannel::CheckSourcesDrained()noexcept {
    if(!state_||state_->host)return ChannelResult::Invalid;auto& s=*state_;State::Lock lock(s);
    if(!lock)return s.fault?ChannelResult::Closed:ChannelResult::Busy;
    if(!s.Peer()||s.shared->pipeline.closing)return ChannelResult::Closed;
    for(const auto& slot:s.shared->pipeline.slots){
        if(slot.nativeHeldRequest||slot.hasSource||slot.retiring||
           (slot.frame.phase!=Phase::Idle&&slot.frame.phase!=Phase::Requested))return ChannelResult::Busy;
    }
    // Requested/queued samples have no native ownership. Preserve their exact
    // immutable tracking and deadline; this observation never consumes them.
    return ChannelResult::Ok;
}
bool FramePipelineChannel::ReadBodyProps(const graphics::PairTicket& t,graphics::BodyPropFrame& out)noexcept {
    out={};if(!state_||!state_->host)return false;const auto& s=*state_;
    if(!s.Delivered(t)||!graphics::BodyPropFrameMatches(s.deliveredProps,t))return false;out=s.deliveredProps;return true;
}
bool FramePipelineChannel::ReadSceneDepth(const graphics::TextureDescriptor& d,const graphics::PairTicket& t,graphics::SceneDepthFrame& out)noexcept {
    out={};if(!state_||!state_->host)return false;const auto& s=*state_;
    if(!s.Delivered(t)||s.deliveredDepth.samples!=1||s.deliveredDepth.quality||!graphics::SceneDepthMatches(s.deliveredDepth,d,t))return false;out=s.deliveredDepth;return true;
}
bool FramePipelineChannel::ReadSourceSlot(const graphics::PairTicket& t,runtime::SourceSlotIdentity& out)noexcept {
    out={};if(!state_||!state_->host)return false;const auto& s=*state_;
    if(!s.Delivered(t))return false;
    const bool absent=graphics::SceneDepthAbsent(s.deliveredDepth);
    if(!absent&&(s.deliveredDepth.samples!=1||s.deliveredDepth.quality||
        !graphics::SceneDepthMatches(s.deliveredDepth,s.deliveredDescriptor,t)))return false;
    out={s.session,s.deliveredKey.request,s.deliveredKey.slot,s.sourceCount,!absent};return true;
}
ChannelResult FramePipelineChannel::PublishInput(const interaction::InputFrame& input)noexcept {
    if(!state_||!state_->host)return ChannelResult::Invalid;auto& s=*state_;State::Lock lock(s);
    if(!lock)return s.fault?ChannelResult::Closed:ChannelResult::Busy;if(s.shared->pipeline.closing)return ChannelResult::Closed;
    if(input.generation<=s.shared->input.generation)return ChannelResult::Invalid;const auto now=PipelineNow();InputPacket packet{};
    if(now<=0||!EncodeInput(input,now+s.frequency/10,packet))return ChannelResult::Invalid;s.shared->input=packet;return ChannelResult::Ok;
}
ChannelResult FramePipelineChannel::ReadInput(interaction::InputFrame& out,std::int64_t* deadline)noexcept {
    out={};if(deadline)*deadline=0;if(!state_||state_->host)return ChannelResult::Invalid;auto& s=*state_;State::Lock lock(s);
    if(!lock)return s.fault?ChannelResult::Closed:ChannelResult::Busy;if(!s.Peer()||s.shared->pipeline.closing)return ChannelResult::Closed;
    if(!s.shared->input.generation)return ChannelResult::Busy;const auto now=PipelineNow();if(now<=0||now>=s.shared->input.deadlineQpc)return ChannelResult::Timeout;
    if(!DecodeInput(s.shared->input,out))return ChannelResult::Invalid;if(deadline)*deadline=s.shared->input.deadlineQpc;return ChannelResult::Ok;
}
bool FramePipelineChannel::PublishFeedback(const interaction::FeedbackEvent& e)noexcept{return state_&&!state_->host&&state_->feedback.Publish(e);}
bool FramePipelineChannel::TakeFeedback(interaction::FeedbackEvent& e)noexcept{e={};return state_&&state_->host&&state_->feedback.Take(e);}
}
