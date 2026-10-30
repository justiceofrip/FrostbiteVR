// PRIVATE, bounded, root-operated IPC observer. Not a runtime/product target.
// Root must budget storage, verify the normal trace's menu_connection.ready,
// inspect this run's menu.rgba/image.json, and ONLY THEN atomically write
// request1 to resume-request.txt (SINGLEPLAYER tab, raster 420,166). After the
// fresh singleplayer.rgba/singleplayer-image.json is reviewed, atomically
// replace that file with request2 X Y for the visible campaign Resume item.
// If fresh Menu persists 1500 ms after Resume release, inspect the ONE saved
// post-resume.rgba/post-resume-image.json before optionally replacing the file
// with request3 X Y for a visible campaign confirmation. Root alone decides.
// X/Y are canonical unsigned decimal raster pixels, X<1024 and Y<768.
// Each request permits one trailing LF/CRLF only. No other navigation exists.
// MenuState has no frontEnd bit: that review is external.
// Normal NativeProbe stream startup independently verifies frontEnd && entered
// when no world exists. There is no invented allowFrontEnd switch or offset here.
#include "fvr/ipc/RemoteFrameProvider.h"
#include "fvr/ipc/MenuChannel.h"
#include "fvr/graphics/D3D11SharedPair.h"
#include <Windows.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

using Microsoft::WRL::ComPtr;
namespace fs=std::filesystem;
namespace {
constexpr ULONGLONG LifetimeMs=120000,WorkMs=119000;
constexpr unsigned MenuWidth=1024,MenuHeight=768,WorldWidth=1920,WorldHeight=1080;
constexpr unsigned MenuLogicalWidth=1280,MenuLogicalHeight=720;
constexpr unsigned SingleplayerX=420,SingleplayerY=166;
constexpr std::uint64_t MaximumRawBytes=4ull*(3ull*MenuWidth*MenuHeight+2ull*WorldWidth*WorldHeight);
std::int64_t Qpc(){LARGE_INTEGER q{};return QueryPerformanceCounter(&q)?q.QuadPart:0;}
std::int64_t QpcNs(){LARGE_INTEGER f{};const auto q=Qpc();if(q<=0||!QueryPerformanceFrequency(&f)||f.QuadPart<=0)return 0;
    return (q/f.QuadPart)*1000000000+(q%f.QuadPart)*1000000000/f.QuadPart;}
// PRIVATE diagnostic mirror of src/platform/windows/MenuChannel.cpp::Shared.
// Read only its connection header, never channel state/control/surface authority.
struct alignas(8) MenuConnectionHeader {
    std::uint32_t magic=0,version=0,bytes=0,closed=0;
    std::array<std::uint8_t,16> session{};
    std::uint32_t hostPid=0,producerPid=0;
    std::uint64_t hostStarted=0,producerStarted=0;
};
struct alignas(8) MenuConnectionLayout {
    MenuConnectionHeader header{};
    fvr::ipc::MenuState status{};fvr::ipc::MenuControl control{};fvr::ipc::MenuSurface surface{};
    std::uint32_t phase=0,consumed=0;
};
static_assert(sizeof(MenuConnectionHeader)==56&&alignof(MenuConnectionHeader)==8&&
    offsetof(MenuConnectionHeader,magic)==0&&offsetof(MenuConnectionHeader,version)==4&&
    offsetof(MenuConnectionHeader,bytes)==8&&offsetof(MenuConnectionHeader,closed)==12&&
    offsetof(MenuConnectionHeader,session)==16&&offsetof(MenuConnectionHeader,hostPid)==32&&
    offsetof(MenuConnectionHeader,producerPid)==36&&offsetof(MenuConnectionHeader,hostStarted)==40&&
    offsetof(MenuConnectionHeader,producerStarted)==48);
static_assert(sizeof(MenuConnectionLayout)==384&&alignof(MenuConnectionLayout)==8&&
    offsetof(MenuConnectionLayout,header)==0&&offsetof(MenuConnectionLayout,status)==56&&
    offsetof(MenuConnectionLayout,control)==104&&offsetof(MenuConnectionLayout,surface)==168&&
    offsetof(MenuConnectionLayout,phase)==376&&offsetof(MenuConnectionLayout,consumed)==380);
struct MenuConnectionWitness {
    HANDLE mapping=nullptr;const void* view=nullptr;DWORD mapError=0;
    std::array<std::uint8_t,16> expectedSession{};bool tokenValid=false;
    DWORD expectedHostPid=GetCurrentProcessId();std::uint64_t expectedHostStarted=0;
    explicit MenuConnectionWitness(const std::wstring& token)noexcept {try{
        if(token.size()!=32)return;bool any=false;
        for(unsigned i=0;i<16;++i){unsigned value=0;
            for(unsigned j=0;j<2;++j){const auto c=token[2*i+j];
                if(c>=L'0'&&c<=L'9')value=value*16+c-L'0';
                else if(c>=L'a'&&c<=L'f')value=value*16+c-L'a'+10;else return;}
            expectedSession[i]=std::uint8_t(value);any|=value!=0;}
        tokenValid=any;if(!tokenValid)return;
        FILETIME created{},exited{},kernel{},user{};
        if(GetProcessTimes(GetCurrentProcess(),&created,&exited,&kernel,&user))
            expectedHostStarted=(std::uint64_t(created.dwHighDateTime)<<32)|created.dwLowDateTime;
        const auto name=L"Local\\FrostbiteVR.Menu.v1."+token+L".map";
        mapping=OpenFileMappingW(FILE_MAP_READ,FALSE,name.c_str());
        if(!mapping){mapError=GetLastError();return;}
        view=MapViewOfFile(mapping,FILE_MAP_READ,0,0,sizeof(MenuConnectionLayout));
        if(!view)mapError=GetLastError();
    }catch(...){/* Diagnostic failure cannot authorize or refuse any input. */}}
    ~MenuConnectionWitness(){if(view)UnmapViewOfFile(view);if(mapping)CloseHandle(mapping);}
    void CopyHeader(MenuConnectionHeader& header)const noexcept {
        const auto* source=static_cast<const volatile std::uint8_t*>(view);
        auto* destination=reinterpret_cast<std::uint8_t*>(&header);
        for(std::size_t i=0;i<sizeof(header);++i)destination[i]=source[i];
    }
    struct Snapshot {MenuConnectionHeader header{};bool stable=false;};
    Snapshot Sample()const noexcept {
        Snapshot sample{};MenuConnectionHeader second{};
        // Exactly two bounded volatile reads; no mutex, retry, wait or writes.
        // Equality is observational stability, not a synchronized lease.
        if(view){CopyHeader(sample.header);CopyHeader(second);sample.stable=!std::memcmp(&sample.header,&second,sizeof(second));}
        return sample;
    }
    void Write(std::ostream& out,const Snapshot& sample)const {
        const auto& first=sample.header;const bool stable=sample.stable;
        const bool sessionMatches=stable&&tokenValid&&first.session==expectedSession;
        const bool hostMatches=stable&&expectedHostStarted&&first.hostPid==expectedHostPid&&first.hostStarted==expectedHostStarted;
        const bool producerCoherent=stable&&((first.producerPid==0)==(first.producerStarted==0));
        const bool valid=stable&&first.magic==0x314d5646&&first.version==1&&first.bytes==sizeof(MenuConnectionLayout)&&
            first.closed<=1&&sessionMatches&&hostMatches&&producerCoherent;
        out<<"{\"diagnostic_only\":true,\"read_only\":true,\"mapped\":"<<(view?"true":"false")
           <<",\"map_error\":"<<mapError<<",\"snapshot_stable\":"<<(stable?"true":"false")
           <<",\"header_valid\":"<<(valid?"true":"false")<<",\"expected_host_pid\":"<<expectedHostPid
           <<",\"expected_host_started\":"<<expectedHostStarted;
        if(stable){out<<",\"magic\":"<<first.magic<<",\"version\":"<<first.version<<",\"bytes\":"<<first.bytes
            <<",\"session\":[";
            for(unsigned i=0;i<first.session.size();++i){if(i)out<<',';out<<unsigned(first.session[i]);}
            out<<"],\"session_matches\":"<<(sessionMatches?"true":"false")
               <<",\"host_identity_matches\":"<<(hostMatches?"true":"false")
               <<",\"producer_identity_coherent\":"<<(producerCoherent?"true":"false")
               <<",\"host_pid\":"<<first.hostPid<<",\"host_started\":"<<first.hostStarted
               <<",\"producer_pid\":"<<first.producerPid<<",\"producer_started\":"<<first.producerStarted
               <<",\"closed\":"<<first.closed;}
        out<<'}';
    }
};
struct ChannelResults {
    std::array<std::uint64_t,5> counts{};const char* last="not_called";
    void Record(fvr::ipc::ChannelResult result)noexcept {
        switch(result){
        case fvr::ipc::ChannelResult::Ok:++counts[0];last="Ok";break;
        case fvr::ipc::ChannelResult::Busy:++counts[1];last="Busy";break;
        case fvr::ipc::ChannelResult::Invalid:++counts[2];last="Invalid";break;
        case fvr::ipc::ChannelResult::Timeout:++counts[3];last="Timeout";break;
        case fvr::ipc::ChannelResult::Closed:++counts[4];last="Closed";break;
        }
    }
    void Write(std::ostream& out)const {
        out<<"{\"last_result\":\""<<last<<"\",\"Ok\":"<<counts[0]<<",\"Busy\":"<<counts[1]
           <<",\"Invalid\":"<<counts[2]<<",\"Timeout\":"<<counts[3]<<",\"Closed\":"<<counts[4]<<'}';
    }
};
bool Same(const fvr::graphics::TextureDescriptor& a,const fvr::graphics::TextureDescriptor& b){return !std::memcmp(&a,&b,sizeof(a));}
bool Ready(const fvr::ipc::MenuState& s){return fvr::ipc::ValidMenuState(s)&&s.mode==fvr::ipc::MenuMode::Menu&&
    s.inputReady==1&&s.logicalWidth==MenuLogicalWidth&&s.logicalHeight==MenuLogicalHeight&&s.deadlineQpc>Qpc();}
void Identity(std::ostream& out,const fvr::graphics::TextureDescriptor& d,const fvr::graphics::PairTicket& t){
    out<<"{\"descriptor_magic\":"<<d.magic<<",\"descriptor_version\":"<<d.version<<",\"descriptor_bytes\":"<<d.bytes
       <<",\"descriptor_reserved\":"<<d.reserved<<",\"width\":"<<d.width<<",\"height\":"<<d.height<<",\"format\":"<<d.format
       <<",\"resource_epoch\":"<<d.resourceEpoch<<",\"sharing\":"<<unsigned(d.sharing)
       <<",\"adapter_low\":"<<d.adapterLow<<",\"adapter_high\":"<<d.adapterHigh<<",\"session\":[";
    for(unsigned i=0;i<d.session.size();++i){if(i)out<<',';out<<unsigned(d.session[i]);}
    out<<"],\"ticket_magic\":"<<t.magic<<",\"ticket_version\":"<<t.version<<",\"ticket_bytes\":"<<t.bytes
       <<",\"ticket_reserved0\":"<<t.reserved0<<",\"ticket_reserved\":["<<t.reserved[0]<<','<<t.reserved[1]
       <<"],\"ticket_resource_epoch\":"<<t.resourceEpoch<<",\"ticket_session\":[";
    for(unsigned i=0;i<t.session.size();++i){if(i)out<<',';out<<unsigned(t.session[i]);}
    out<<"],\"ticket_sequence\":"<<t.sequence<<",\"frame\":"<<t.frameId
       <<",\"tracking_generation\":"<<t.trackingGeneration<<",\"space\":"<<t.spaceGeneration
       <<",\"predicted_ns\":"<<t.predictedNs<<'}';
}
void StateIdentity(std::ostream& out,const fvr::ipc::MenuState& s){
    out<<"{\"sequence\":"<<s.sequence<<",\"epoch\":"<<s.epoch<<",\"deadline_qpc\":"<<s.deadlineQpc
       <<",\"mode\":"<<unsigned(s.mode)<<",\"logical_width\":"<<s.logicalWidth<<",\"logical_height\":"<<s.logicalHeight
       <<",\"input_ready\":"<<s.inputReady<<",\"reserved\":"<<s.reserved<<'}';
}
void MenuEvidence(std::ostream& out,const fvr::ipc::MenuState& state,const fvr::ipc::MenuSurface& surface){
    out<<"{\"state\":";StateIdentity(out,state);out<<",\"surface_state\":";StateIdentity(out,surface.state);
    out<<",\"identity\":";Identity(out,surface.descriptor,surface.ticket);out<<'}';
}
void Body(std::ostream& out,const fvr::graphics::BodyPropFrame& b,bool available,std::int64_t now){
    out<<"{\"available\":"<<(available?"true":"false")<<",\"sample_qpc_ns\":"<<now;
    if(available){out<<",\"frame\":"<<b.frameId<<",\"tracking_generation\":"<<b.trackingGeneration
       <<",\"space\":"<<b.spaceGeneration<<",\"eyes\":[";
        for(unsigned eye=0;eye<2;++eye){if(eye)out<<',';const auto& e=b.eyes[eye];const auto& a=e.ammo;
            out<<"{\"count\":"<<e.count<<",\"ammo\":{\"sequence\":"<<a.sequence
               <<",\"actor_generation\":"<<a.actorGeneration<<",\"equipment_generation\":"<<a.equipmentGeneration
               <<",\"space\":"<<a.spaceGeneration<<",\"observed_ns\":"<<a.observedNs<<",\"deadline_ns\":"<<a.deadlineNs
               <<",\"fresh_at_sample\":"<<(fvr::graphics::AmmoCounterFresh(a,now)?"true":"false")
               <<",\"loaded\":"<<a.loaded<<",\"reserve\":"<<a.reserve<<",\"capacity\":"<<a.capacity<<"},\"props\":[";
            for(unsigned p=0;p<e.count;++p){if(p)out<<',';const auto& v=e.instances[p];
                out<<"{\"asset\":"<<v.geometry.asset<<",\"part\":"<<v.geometry.part<<",\"revision\":"<<v.geometry.revision
                   <<",\"actor_generation\":"<<v.actorGeneration<<",\"equipment_generation\":"<<v.equipmentGeneration
                   <<",\"space\":"<<v.spaceGeneration<<",\"observed_ns\":"<<v.observedNs<<",\"deadline_ns\":"<<v.deadlineNs
                   <<",\"fresh_at_sample\":"<<(fvr::graphics::BodyPropFresh(v,now)?"true":"false")<<'}';}
            out<<"]}";
        }out<<']';
    }out<<'}';
}
// A failed capture is never retried. No background recording or extra frames.
template<class NeutralPulse>
bool Save(ID3D11Device* device,ID3D11DeviceContext* context,ID3D11Texture2D* texture,
          unsigned slice,const fs::path& path,ULONGLONG deadline,NeutralPulse&& pulse){
    D3D11_TEXTURE2D_DESC d{};texture->GetDesc(&d);d.ArraySize=1;d.MipLevels=1;
    d.Usage=D3D11_USAGE_STAGING;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;d.BindFlags=d.MiscFlags=0;
    ComPtr<ID3D11Texture2D> stage;if(FAILED(device->CreateTexture2D(&d,nullptr,&stage)))return false;
    context->CopySubresourceRegion(stage.Get(),0,0,0,0,texture,slice,nullptr);context->Flush();
    D3D11_MAPPED_SUBRESOURCE mapped{};HRESULT result=DXGI_ERROR_WAS_STILL_DRAWING;
    const auto localEnd=(std::min)(deadline,GetTickCount64()+200);
    while(GetTickCount64()<localEnd){if(!pulse())return false;
        result=context->Map(stage.Get(),0,D3D11_MAP_READ,D3D11_MAP_FLAG_DO_NOT_WAIT,&mapped);
        if(result!=DXGI_ERROR_WAS_STILL_DRAWING)break;Sleep(1);}
    if(FAILED(result))return false;
    std::ofstream raw(path,std::ios::binary);
    bool trackingCurrent=true;
    for(unsigned y=0;y<d.Height&&raw;++y){
        if((y%16)==0&&(!pulse()||GetTickCount64()>=deadline)){trackingCurrent=false;break;}
        raw.write(static_cast<const char*>(mapped.pData)+y*mapped.RowPitch,d.Width*4);
    }
    context->Unmap(stage.Get(),0);raw.close();return trackingCurrent&&bool(raw);
}
enum class Request {Absent,First,Second,Third,Invalid};
Request ReadRequest(const fs::path& file,unsigned& x,unsigned& y){
    std::error_code ec;if(!fs::exists(file,ec))return ec?Request::Invalid:Request::Absent;
    const auto size=fs::file_size(file,ec);if(ec||size<8||size>19)return Request::Invalid;
    std::ifstream input(file,std::ios::binary);std::array<char,20> buffer{};
    input.read(buffer.data(),buffer.size());const auto count=input.gcount();
    if(input.bad()||count<8||count>19||std::uintmax_t(count)!=size)return Request::Invalid;
    std::string text(buffer.data(),std::size_t(count));
    if(!text.empty()&&text.back()=='\n'){text.pop_back();if(!text.empty()&&text.back()=='\r')text.pop_back();}
    if(text=="request1")return Request::First;
    const auto request=!text.compare(0,9,"request2 ")?Request::Second:
        !text.compare(0,9,"request3 ")?Request::Third:Request::Invalid;
    if(request==Request::Invalid)return request;
    std::size_t at=9;
    const auto integer=[&](unsigned limit,unsigned& value){
        const auto begin=at;value=0;
        while(at<text.size()&&text[at]>='0'&&text[at]<='9'){
            value=value*10+unsigned(text[at++]-'0');if(value>=limit)return false;}
        return at>begin&&(at==begin+1||text[begin]!='0');
    };
    if(!integer(MenuWidth,x)||at>=text.size()||text[at++]!=' '||!integer(MenuHeight,y)||at!=text.size())return Request::Invalid;
    return request;
}
enum class Phase {MainCapture,MainReview,SingleplayerClick,SingleplayerSettle,SingleplayerReview,ResumeClick,AwaitWorld,
    PostResumeSettle,PostResumeReview,ConfirmationClick};
struct NavigationStep {
    fvr::ipc::MenuSurface pin{};fvr::ipc::MenuState state{};
    bool saved=false,accepted=false;unsigned x=0,y=0;
    ULONGLONG requestAt=0,pressAt=0,releaseAt=0;
    std::uint64_t releaseSurfaceSequence=0,releaseStateSequence=0;
    std::uint64_t neutralGeneration=0;
};
struct Retirement {
    fvr::ipc::RemoteFrameProvider& frames;fvr::ipc::MenuChannel& menu;
    fvr::ipc::MenuControl& control;std::uint64_t& sequence;ULONGLONG deadline;
    fvr::graphics::PairTicket menuTicket{};bool menuAck=false,menuConsumed=false,finished=false;
    unsigned neutralPublications=0;bool menuAckFinished=false,frameFeedbackFinished=false;
    void Run()noexcept {
        if(finished)return;finished=true;
        // Clear queued requests BEFORE old GPU ACKs. Exact menu feedback remains
        // pending across Busy; closing a peer finally retires any undelivered slot.
        const auto end=(std::min)(deadline,GetTickCount64()+500);
        do{
            frames.Suspend();frameFeedbackFinished=frames.FlushFeedback()!=fvr::ipc::ChannelResult::Busy;
            control.flags=0;control.sequence=++sequence;
            if(menu.PublishControl(control)==fvr::ipc::ChannelResult::Ok)++neutralPublications;
            if(menuAck&&menu.SurfaceConsumed(menuTicket,menuConsumed)!=fvr::ipc::ChannelResult::Busy)menuAck=false;
            menuAckFinished=!menuAck;Sleep(5);
        }while(GetTickCount64()<end);
        // Fresh invalid tracking explicitly withdraws transient input authority.
        fvr::interaction::InputFrame retired{};retired.generation=UINT64_MAX;retired.spaceGeneration=1;
        retired.predictedNs=INT64_MAX;frames.UpdateInput(retired);
        frames.Suspend();frames.FlushFeedback();menu.Close();frames.Close();
    }
    ~Retirement(){Run();}
};
}

int wmain(int argc,wchar_t** argv){try{
    if(argc!=3||std::wstring(argv[1])!=L"--output")throw std::runtime_error("Usage: NativeCampaignObserve --output NEW_FOLDER");
    const auto start=GetTickCount64(),deadline=start+LifetimeMs;
    const auto folder=fs::absolute(argv[2]);if(fs::exists(folder))throw std::runtime_error("New report folder required");
    fs::create_directories(folder);const auto requestFile=folder/L"resume-request.txt";
    fvr::ipc::RemoteFrameProvider frames;fvr::ipc::MenuChannel menu;
    if(!frames.Create()||!menu.CreateHost(frames.Token()))throw std::runtime_error("Create channels");
    std::uint64_t sequence=0;fvr::ipc::MenuControl control{};control.space=1;
    Retirement retirement{frames,menu,control,sequence,deadline};
    MenuConnectionWitness connectionWitness(frames.Token());
    ChannelResults readStateResults,currentReadStateResults,publishControlResults,takeSurfaceResults;
    std::wofstream channelFile(folder/L"channel.txt");channelFile<<frames.Token();channelFile.close();
    if(!channelFile)throw std::runtime_error("Write channel");
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
    if(FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context)))throw std::runtime_error("Create GPU");
    ComPtr<IDXGIDevice> dxgi;ComPtr<IDXGIAdapter> adapter;DXGI_ADAPTER_DESC gpu{};
    if(FAILED(device.As(&dxgi))||FAILED(dxgi->GetAdapter(&adapter))||FAILED(adapter->GetDesc(&gpu)))throw std::runtime_error("GPU identity");
    fvr::runtime::PresentationRequirements requirements{WorldWidth,WorldHeight,29,gpu.AdapterLuid.LowPart,gpu.AdapterLuid.HighPart};
    fvr::graphics::D3D11PairConsumer menuConsumer,worldConsumer;
    fvr::graphics::TextureDescriptor menuOpened{},worldOpened{},settledMenu{};
    std::array<ComPtr<ID3D11Texture2D>,2> menuScratch;
    D3D11_TEXTURE2D_DESC wd{};wd.Width=WorldWidth;wd.Height=WorldHeight;wd.MipLevels=1;wd.ArraySize=2;
    wd.Format=DXGI_FORMAT_R8G8B8A8_TYPELESS;wd.SampleDesc.Count=1;
    ComPtr<ID3D11Texture2D> worldEyes;if(FAILED(device->CreateTexture2D(&wd,nullptr,&worldEyes)))throw std::runtime_error("World eye target");
    frames.SetRequestLifetime(150);
    std::ofstream events(folder/L"events.jsonl"),telemetry(folder/L"state.jsonl");
    if(!events||!telemetry)throw std::runtime_error("Create telemetry");
    const auto event=[&](const char* name,const std::string& details="null"){
        events<<"{\"elapsed_ms\":"<<GetTickCount64()-start<<",\"event\":\""<<name<<"\",\"details\":"<<details<<"}\n";events.flush();};
    const char* failure=nullptr;std::string failureDetails="null";
    const auto fail=[&](const char* why,const std::string& details="null"){
        if(!failure){failure=why;failureDetails=details;event(why,details);}};
    unsigned menuPairs=0,worldPairs=0,stablePairs=0,telemetryRows=0,pressEpisodes=0,releaseEpisodes=0;
    unsigned menuCopyBusy=0,worldCopyBusy=0;
    std::uint64_t generation=0,lastMenuSequence=0,lastWorldFrame=0,settledEpoch=0;
    std::array<ULONGLONG,64> trackingTimes{};
    ULONGLONG connectedAt=0,lastInputAt=0,lastStateAt=0,lastSurfaceAt=0,gameplayAt=0,nextTelemetry=0,settledAt=0;
    bool worldCaptureAttempted=false,worldSaved=false,postResumeSkipped=false;
    std::array<NavigationStep,3> steps{};steps[0].x=SingleplayerX;steps[0].y=SingleplayerY;
    Phase phase=Phase::MainCapture;
    const auto stepIndex=[&](){
        if(phase==Phase::PostResumeSettle||phase==Phase::PostResumeReview||phase==Phase::ConfirmationClick)return 2u;
        if(phase==Phase::AwaitWorld)return steps[2].accepted?2u:1u;
        return phase==Phase::SingleplayerReview||phase==Phase::ResumeClick?1u:0u;};
    const auto pinned=[&](){return phase==Phase::MainReview||phase==Phase::SingleplayerClick||
        phase==Phase::SingleplayerReview||phase==Phase::ResumeClick||phase==Phase::PostResumeReview||phase==Phase::ConfirmationClick;};
    const auto skipPostResume=[&](const fvr::ipc::MenuState& observed){
        if(phase==Phase::PostResumeSettle&&fvr::ipc::ValidMenuState(observed)&&observed.deadlineQpc>Qpc()&&
            (observed.mode==fvr::ipc::MenuMode::Passive||observed.mode==fvr::ipc::MenuMode::Gameplay)){
            postResumeSkipped=true;phase=Phase::AwaitWorld;settledAt=0;
            std::ostringstream details;StateIdentity(details,observed);
            event("post_resume_capture_skipped_loading_or_gameplay",details.str());}
    };
    // Menu tickets carry native menu epoch/sequence, not emulated input IDs.
    const auto menuTrackingMatches=[](const fvr::ipc::MenuSurface& observed){
        return observed.ticket.spaceGeneration==observed.state.epoch&&
            observed.ticket.trackingGeneration==observed.state.sequence&&observed.ticket.predictedNs==1;
    };
    fvr::ipc::MenuSurface lastSurface{};fvr::ipc::MenuState lastState{};
    fvr::graphics::PairTicket lastWorldTicket{};fvr::graphics::BodyPropFrame lastBody{};bool bodyAvailable=false;
    std::int64_t bodySampleNs=0;
    const auto refuseMenu=[&](const char* why,const fvr::ipc::MenuState& actualState,const fvr::ipc::MenuSurface& actualSurface){
        const auto& expected=steps[stepIndex()];const auto at=GetTickCount64();std::ostringstream details;
        details<<"{\"navigation_step\":"<<stepIndex()+1<<",\"phase\":"<<unsigned(phase)
            <<",\"review_authorization_withdrawn\":true,\"sample_qpc\":"<<Qpc()<<",\"expected\":";
        MenuEvidence(details,expected.state,expected.pin);details<<",\"observed\":";MenuEvidence(details,actualState,actualSurface);
        details<<",\"expected_adapter_low\":"<<requirements.adapterLow<<",\"expected_adapter_high\":"<<requirements.adapterHigh
            <<",\"same_descriptor\":"<<(Same(expected.pin.descriptor,actualSurface.descriptor)?"true":"false")
            <<",\"state_ready\":"<<(Ready(actualState)?"true":"false")
            <<",\"surface_state_ready\":"<<(Ready(actualSurface.state)?"true":"false")
            <<",\"last_surface_age_ms\":"<<(lastSurfaceAt?at-lastSurfaceAt:UINT64_MAX)
            <<",\"last_input_age_ms\":"<<(lastInputAt?at-lastInputAt:UINT64_MAX)
            <<",\"capture_neutral_generation\":"<<expected.neutralGeneration<<",\"current_neutral_generation\":"<<generation
            <<",\"menu_ticket_tracking_matches_state\":"<<(menuTrackingMatches(actualSurface)?"true":"false")<<'}';
        fail(why,details.str());
    };
    event("created_neutral_waiting_for_front_menu");
    while(GetTickCount64()-start<WorkMs&&!failure&&!worldSaved){
        const auto now=GetTickCount64();
        // Same stationary emulated tracking sample as NativeMenuProbe. Every
        // trigger, squeeze, stick and held button remains value-initialized zero.
        fvr::runtime::TrackingFrame tracking{};tracking.generation=++generation;tracking.spaceGeneration=1;
        tracking.predictedNs=10000000000LL+std::int64_t(generation)*11000000;
        tracking.focused=tracking.headValid=true;tracking.eyes={tracking.head,tracking.head};
        tracking.eyes[0].position.x=-.032f;tracking.eyes[1].position.x=.032f;
        tracking.fov={fvr::math::FovTangents{-1,1,.8f,-.8f},fvr::math::FovTangents{-1,1,.8f,-.8f}};
        trackingTimes[generation%trackingTimes.size()]=now;
        fvr::interaction::InputFrame input{};input.generation=generation;input.spaceGeneration=1;
        input.predictedNs=tracking.predictedNs;input.focused=input.headValid=true;input.referenceHead=tracking.referenceHead;input.head=tracking.head;
        for(unsigned hand=0;hand<2;++hand){auto& h=input.hands[hand];h.gripTracked=h.aimTracked=true;
            h.active=fvr::interaction::Components;h.grip.position={hand?.2f:-.2f,-.25f,-.45f};h.aim=h.grip;}
        if(steps[0].accepted&&lastInputAt&&now-lastInputAt>=100){fail("tracking_publication_gap");break;}
        const auto published=frames.UpdateInputObserved(input);if(published.known&&published.succeeded)lastInputAt=now;
        // CPU readback/file output may span several input leases. Keep ONLY the
        // same neutral tracked sample current; never renew a menu-down here.
        const auto pulseNeutral=[&](){const auto at=GetTickCount64();
            if(lastInputAt&&at-lastInputAt>=100)return false;
            if(lastInputAt&&at-lastInputAt<20)return true;
            auto next=input;next.generation=++generation;next.predictedNs=10000000000LL+std::int64_t(generation)*11000000;
            trackingTimes[generation%trackingTimes.size()]=at;
            const auto result=frames.UpdateInputObserved(next);if(result.succeeded)lastInputAt=at;
            return !lastInputAt||at-lastInputAt<100;
        };
        if(connectedAt&&lastInputAt&&now-lastInputAt>=100){fail("tracking_publication_lost");break;}
        fvr::ipc::MenuState state{};const auto stateResult=menu.ReadState(state);readStateResults.Record(stateResult);
        if(stateResult==fvr::ipc::ChannelResult::Ok){
            if(!connectedAt)connectedAt=now;lastStateAt=now;lastState=state;
            skipPostResume(state);
            if(phase!=Phase::AwaitWorld&&state.mode==fvr::ipc::MenuMode::Gameplay){
                refuseMenu(stepIndex()==2?"reviewed_post_resume_menu_changed":"unexpected_gameplay_before_authorized_resume_release",state,lastSurface);break;}
            if(pinned()&&(!Ready(state)||state.epoch!=steps[stepIndex()].pin.state.epoch||
                (stepIndex()==2&&state.sequence<steps[2].state.sequence))){
                refuseMenu("reviewed_menu_identity_changed",state,lastSurface);break;}
        }else if(connectedAt&&(stateResult==fvr::ipc::ChannelResult::Closed||stateResult==fvr::ipc::ChannelResult::Invalid)){
            fail("menu_channel_closed_or_invalid");break;
        }
        if(connectedAt&&now-lastStateAt>=250){fail("menu_state_lease_lost");break;}
        if(phase==Phase::SingleplayerSettle&&lastSurfaceAt&&now-lastSurfaceAt>=250)settledAt=0;
        if(phase==Phase::PostResumeSettle&&(!Ready(state)||!lastSurfaceAt||now-lastSurfaceAt>=100))settledAt=0;
        if(retirement.menuAck){const auto ack=menu.SurfaceConsumed(retirement.menuTicket,retirement.menuConsumed);
            if(ack!=fvr::ipc::ChannelResult::Busy){retirement.menuAck=false;if(ack!=fvr::ipc::ChannelResult::Ok)fail("menu_ack_failed");}}
        fvr::ipc::MenuSurface surface{};
        const bool takeSurfaceCalled=!retirement.menuAck;
        const auto surfaceResult=takeSurfaceCalled?menu.TakeSurface(surface):fvr::ipc::ChannelResult::Busy;
        if(takeSurfaceCalled)takeSurfaceResults.Record(surfaceResult);
        if(takeSurfaceCalled&&surfaceResult==fvr::ipc::ChannelResult::Ok){
            retirement.menuTicket=surface.ticket;retirement.menuAck=true;retirement.menuConsumed=false;
            skipPostResume(surface.state);
            const auto& d=surface.descriptor;
            const bool identity=fvr::ipc::ValidMenuSurface(surface)&&d.width==MenuWidth&&d.height==MenuHeight&&
                d.adapterLow==requirements.adapterLow&&d.adapterHigh==requirements.adapterHigh&&
                (!pinned()||(Same(d,steps[stepIndex()].pin.descriptor)&&surface.state.epoch==steps[stepIndex()].pin.state.epoch))&&
                (!(pinned()&&stepIndex()==2)||
                    (Ready(surface.state)&&menuTrackingMatches(surface)&&surface.ticket.sequence>=steps[2].pin.ticket.sequence&&
                     surface.state.sequence>=steps[2].pin.state.sequence));
            if(!identity)refuseMenu("menu_surface_identity_mismatch",state,surface);
            else {
                auto copied=fvr::graphics::TransferResult::Faulted;
                if(!Same(menuOpened,d)){menuOpened={};menuConsumer.Reset();
                    copied=menuConsumer.Open(device.Get(),d);
                    if(copied==fvr::graphics::TransferResult::Ok){
                        D3D11_TEXTURE2D_DESC td{};td.Width=d.width;td.Height=d.height;td.Format=DXGI_FORMAT(d.format);
                        td.MipLevels=td.ArraySize=td.SampleDesc.Count=1;bool made=true;
                        for(auto& texture:menuScratch){texture.Reset();made&=SUCCEEDED(device->CreateTexture2D(&td,nullptr,&texture));}
                        if(made)menuOpened=d;else copied=fvr::graphics::TransferResult::Faulted;
                    }
                }
                if(fvr::graphics::Valid(menuOpened))copied=menuConsumer.Copy(surface.ticket,
                    {{{menuScratch[0].Get(),0},{menuScratch[1].Get(),0}}});
                retirement.menuConsumed=copied==fvr::graphics::TransferResult::Ok;
                if(copied==fvr::graphics::TransferResult::Busy)++menuCopyBusy;
                if(copied!=fvr::graphics::TransferResult::Ok&&copied!=fvr::graphics::TransferResult::Busy)fail("menu_gpu_copy_failed");
                else if(retirement.menuConsumed){++menuPairs;lastSurface=surface;lastSurfaceAt=GetTickCount64();lastMenuSequence=surface.ticket.sequence;
                    const bool coherent=Ready(surface.state)&&Ready(state)&&surface.state.epoch==state.epoch;
                    bool captureMain=phase==Phase::MainCapture&&connectedAt&&now-connectedAt>=6000&&coherent;
                    bool captureSingleplayer=false,capturePostResume=false;
                    if(phase==Phase::SingleplayerSettle){
                        const auto& tab=steps[0];
                        const bool afterRelease=tab.releaseAt&&surface.ticket.sequence>tab.releaseSurfaceSequence&&
                            surface.state.sequence>tab.releaseStateSequence&&state.sequence>tab.releaseStateSequence;
                        if(!coherent||!afterRelease)settledAt=0;
                        else{
                            if(!settledAt||settledEpoch!=state.epoch||!Same(settledMenu,d)){
                                settledAt=GetTickCount64();settledEpoch=state.epoch;settledMenu=d;}
                            captureSingleplayer=GetTickCount64()-settledAt>=750&&GetTickCount64()-tab.releaseAt>=750;
                        }
                    }
                    if(phase==Phase::PostResumeSettle){
                        const auto& resume=steps[1];
                        const bool afterRelease=resume.releaseAt&&surface.ticket.sequence>resume.releaseSurfaceSequence&&
                            surface.state.sequence>resume.releaseStateSequence&&state.sequence>resume.releaseStateSequence;
                        if(!coherent||!afterRelease||!menuTrackingMatches(surface)||!published.known||!published.succeeded||
                            !lastInputAt||GetTickCount64()-lastInputAt>=100)settledAt=0;
                        else{
                            if(!settledAt||settledEpoch!=state.epoch||!Same(settledMenu,d)){
                                settledAt=GetTickCount64();settledEpoch=state.epoch;settledMenu=d;}
                            capturePostResume=GetTickCount64()-settledAt>=1500&&GetTickCount64()-resume.releaseAt>=1500;
                        }
                    }
                    if(captureMain||captureSingleplayer||capturePostResume){
                        const unsigned index=captureMain?0u:captureSingleplayer?1u:2u;auto& step=steps[index];unsigned x=0,y=0;
                        const auto expectedRequest=captureMain?Request::Absent:captureSingleplayer?Request::First:Request::Second;
                        if(ReadRequest(requestFile,x,y)!=expectedRequest)fail("request_precedes_capture_review");
                        else if(!Save(device.Get(),context.Get(),menuScratch[0].Get(),0,
                            folder/(captureMain?L"menu.rgba":captureSingleplayer?L"singleplayer.rgba":L"post-resume.rgba"),deadline,pulseNeutral))fail("menu_capture_failed");
                        else{step.pin=surface;step.state=state;step.neutralGeneration=generation;
                            std::ofstream report(folder/(captureMain?L"image.json":captureSingleplayer?L"singleplayer-image.json":L"post-resume-image.json"));
                            report<<"{\"kind\":\""<<(captureMain?"main_menu_before_singleplayer":captureSingleplayer?"singleplayer_menu_before_campaign_resume":"post_resume_menu_before_optional_campaign_confirmation")
                                  <<"\",\"elapsed_ms\":"<<GetTickCount64()-start
                                  <<",\"width\":"<<d.width<<",\"height\":"<<d.height<<",\"format\":"<<d.format
                                  <<",\"menu_epoch\":"<<state.epoch<<",\"state_sequence\":"<<state.sequence
                                  <<",\"state_deadline_qpc\":"<<state.deadlineQpc<<",\"surface_deadline_qpc\":"<<surface.state.deadlineQpc
                                  <<",\"logical_width\":1280,\"logical_height\":720,\"navigation_step\":"<<index+1
                                  <<",\"request_protocol\":\""<<(captureMain?"request1":captureSingleplayer?"request2 X Y":"request3 X Y")<<'"';
                            if(captureMain)report<<",\"singleplayer_x\":"<<SingleplayerX<<",\"singleplayer_y\":"<<SingleplayerY;
                            else if(captureSingleplayer)report<<",\"singleplayer_tab_release_elapsed_ms\":"<<steps[0].releaseAt-start
                                <<",\"settled_ms\":"<<GetTickCount64()-settledAt<<",\"resume_coordinates_require_root_review\":true";
                            else report<<",\"campaign_resume_release_elapsed_ms\":"<<steps[1].releaseAt-start
                                <<",\"settled_ms\":"<<GetTickCount64()-settledAt<<",\"neutral_generation\":"<<step.neutralGeneration
                                <<",\"neutral_space\":1,\"optional_confirmation_requires_root_visual_review\":true";
                            report<<",\"native_front_end_bit_available\":false,\"review_required\":true,\"identity\":";
                            Identity(report,d,surface.ticket);report<<'}';report.close();
                            if(!report)fail("menu_report_failed");else{
                                step.saved=true;phase=captureMain?Phase::MainReview:captureSingleplayer?Phase::SingleplayerReview:Phase::PostResumeReview;
                                event(captureMain?"menu_saved_await_root_review_and_request1":captureSingleplayer?
                                    "singleplayer_saved_await_root_review_and_request2":"post_resume_saved_await_root_review_and_optional_request3");}
                        }
                    }
                }
            }
            const auto ack=menu.SurfaceConsumed(retirement.menuTicket,retirement.menuConsumed);
            if(ack!=fvr::ipc::ChannelResult::Busy){retirement.menuAck=false;if(ack!=fvr::ipc::ChannelResult::Ok)fail("menu_ack_failed");}
        }
        if(failure)break;
        // Re-read after capture/copy. A formerly fresh state is not a new lease.
        fvr::ipc::MenuState current{};const auto currentStateResult=menu.ReadState(current);
        currentReadStateResults.Record(currentStateResult);const bool currentRead=currentStateResult==fvr::ipc::ChannelResult::Ok;
        if(currentRead)skipPostResume(current);
        if(phase==Phase::PostResumeSettle&&currentRead&&(!Ready(current)||current.epoch!=settledEpoch))settledAt=0;
        if(currentRead&&phase!=Phase::AwaitWorld&&current.mode==fvr::ipc::MenuMode::Gameplay){
            refuseMenu(stepIndex()==2?"reviewed_post_resume_menu_changed":"unexpected_gameplay_before_authorized_resume_release",current,lastSurface);break;}
        if(pinned()&&currentRead&&(!Ready(current)||current.epoch!=steps[stepIndex()].pin.state.epoch||
            (stepIndex()==2&&current.sequence<steps[2].state.sequence))){
            refuseMenu("reviewed_menu_identity_changed_after_copy",current,lastSurface);break;}
        const auto& pin=steps[stepIndex()].pin;
        const bool menuGuard=pinned()&&currentRead&&Ready(current)&&current.epoch==pin.state.epoch&&
            Ready(lastSurface.state)&&lastSurface.state.epoch==pin.state.epoch&&Same(lastSurface.descriptor,pin.descriptor)&&
            lastSurfaceAt&&GetTickCount64()-lastSurfaceAt<100&&published.known&&published.succeeded&&lastInputAt&&GetTickCount64()-lastInputAt<100&&
            (stepIndex()!=2||(steps[2].saved&&menuTrackingMatches(lastSurface)&&
                lastSurface.ticket.sequence>=pin.ticket.sequence&&lastSurface.state.sequence>=pin.state.sequence&&
                current.sequence>=steps[2].state.sequence&&generation>=steps[2].neutralGeneration));
        unsigned requestedX=0,requestedY=0;
        const auto request=ReadRequest(requestFile,requestedX,requestedY);
        if(phase==Phase::MainCapture&&request!=Request::Absent)fail("request_precedes_capture_review");
        else if((phase==Phase::SingleplayerClick||phase==Phase::SingleplayerSettle)&&request!=Request::First)
            fail("request2_precedes_singleplayer_capture_review");
        else if((phase==Phase::ResumeClick||phase==Phase::PostResumeSettle)&&request!=Request::Second)
            fail("request3_precedes_post_resume_capture_review");
        else if(phase==Phase::ConfirmationClick&&request!=Request::Third)fail("request3_changed_during_confirmation_click");
        else if(phase==Phase::AwaitWorld&&request!=(steps[2].accepted?Request::Third:Request::Second))
            fail("request_changed_after_navigation_finished");
        else if(phase==Phase::MainReview||phase==Phase::SingleplayerReview||phase==Phase::PostResumeReview){
            const unsigned index=stepIndex();const auto waiting=index==2?Request::Second:index?Request::First:Request::Absent;
            const auto expected=index==2?Request::Third:index?Request::Second:Request::First;
            if(request!=waiting){
                if(request!=expected)fail("invalid_or_early_request");
                else if(!menuGuard||GetTickCount64()-start>WorkMs-2000)refuseMenu("request_identity_or_lease_rejected",current,lastSurface);
                else{auto& step=steps[index];step.accepted=true;step.requestAt=GetTickCount64();
                    if(index){step.x=requestedX;step.y=requestedY;}
                    phase=index==2?Phase::ConfirmationClick:index?Phase::ResumeClick:Phase::SingleplayerClick;
                    std::ostringstream details;details<<"{\"raster_x\":"<<step.x<<",\"raster_y\":"<<step.y
                        <<",\"u\":"<<float(step.x)/MenuWidth<<",\"v\":"<<float(step.y)/MenuHeight<<'}';
                    event(index==2?"request3_accepted_root_reviewed_campaign_confirmation":index?
                        "request2_accepted_root_reviewed_campaign_resume":"request1_accepted_root_reviewed_singleplayer_tab",details.str());}
            }
        }
        control.flags=0;control.menuEpoch=currentRead?current.epoch:0;
        if((phase==Phase::MainCapture||phase==Phase::MainReview)&&currentRead&&Ready(current)){
            control.flags=fvr::ipc::MenuFocused|fvr::ipc::MenuPoint;control.u=control.v=.5f;}
        // Keep the displayed widget hovered through settling and visual review.
        if((phase==Phase::SingleplayerSettle||phase==Phase::SingleplayerReview||
            phase==Phase::PostResumeSettle||phase==Phase::PostResumeReview)&&currentRead&&Ready(current)){
            const auto& hovered=steps[phase==Phase::SingleplayerSettle||phase==Phase::SingleplayerReview?0u:1u];
            control.flags=fvr::ipc::MenuFocused|fvr::ipc::MenuPoint;
            control.u=float(hovered.x)/MenuWidth;control.v=float(hovered.y)/MenuHeight;}
        // An initial neutral point arms the native pointer. A continuous 150 ms
        // down interval is ONE press, refreshed below the native 100 ms lease.
        // No controller MenuClick, native cancel/toggle, Fire or gameplay action.
        const bool clicking=phase==Phase::SingleplayerClick||phase==Phase::ResumeClick||phase==Phase::ConfirmationClick;
        if(clicking&&!failure){auto& step=steps[stepIndex()];
            if(!menuGuard)refuseMenu("menu_or_tracking_guard_lost_during_click",current,lastSurface);
            else{control.u=float(step.x)/MenuWidth;control.v=float(step.y)/MenuHeight;control.flags=fvr::ipc::MenuFocused|fvr::ipc::MenuPoint;
                if(GetTickCount64()-step.requestAt>=150&&(!step.pressAt||GetTickCount64()-step.pressAt<150))control.flags|=fvr::ipc::MenuDown;
            }
        }
        if(failure)break;
        control.sequence=++sequence;const auto sent=menu.PublishControl(control);publishControlResults.Record(sent);
        if(sent==fvr::ipc::ChannelResult::Ok&&clicking){const unsigned index=stepIndex();auto& step=steps[index];
            if((control.flags&fvr::ipc::MenuDown)&&!step.pressAt){step.pressAt=GetTickCount64();++pressEpisodes;
                event(index==2?"campaign_confirmation_press_published":index?"campaign_resume_press_published":"singleplayer_tab_press_published");}
            if(step.pressAt&&!(control.flags&fvr::ipc::MenuDown)&&!step.releaseAt){
                step.releaseAt=GetTickCount64();++releaseEpisodes;step.releaseSurfaceSequence=lastSurface.ticket.sequence;
                step.releaseStateSequence=(std::max)(current.sequence,lastSurface.state.sequence);
                phase=index==2?Phase::AwaitWorld:index?Phase::PostResumeSettle:Phase::SingleplayerSettle;settledAt=0;
                event(index==2?"campaign_confirmation_release_published":index?"campaign_resume_release_published":"singleplayer_tab_release_published");}
        }else if(sent!=fvr::ipc::ChannelResult::Ok&&sent!=fvr::ipc::ChannelResult::Busy&&connectedAt){fail("control_publication_failed");break;}
        // After Resume, only the separately reviewed optional confirmation can
        // add one click. Loading/gameplay before that capture bypasses the gate.
        if(phase==Phase::AwaitWorld&&GetTickCount64()-steps[stepIndex()].releaseAt>=150&&currentRead&&current.mode==fvr::ipc::MenuMode::Gameplay){
            if(!gameplayAt){gameplayAt=GetTickCount64();event("gameplay_observed");}
            fvr::runtime::TrackingFrame rendered{};fvr::graphics::TextureDescriptor d{};fvr::graphics::PairTicket ticket{};
            if(frames.TryGetCompletedPair(requirements,tracking,rendered,d,ticket)){
                bool copied=false;
                const auto stamp=trackingTimes[rendered.generation%trackingTimes.size()];
                const bool valid=fvr::runtime::PairMatchesFrame(requirements,rendered,d,ticket)&&rendered.generation<=generation&&
                    generation-rendered.generation<trackingTimes.size()&&stamp&&GetTickCount64()-stamp<=250&&
                    (!fvr::graphics::Valid(worldOpened)||Same(worldOpened,d))&&ticket.frameId>lastWorldFrame;
                if(!valid){std::ostringstream details;details<<"{\"expected_identity\":";Identity(details,worldOpened,lastWorldTicket);
                    details<<",\"observed_identity\":";Identity(details,d,ticket);
                    details<<",\"rendered_generation\":"<<rendered.generation<<",\"published_generation\":"<<generation
                        <<",\"tracking_stamp_ms\":"<<stamp<<",\"tracking_age_ms\":"<<(stamp?GetTickCount64()-stamp:UINT64_MAX)
                        <<",\"expected_width\":"<<WorldWidth<<",\"expected_height\":"<<WorldHeight
                        <<",\"expected_format\":"<<requirements.format<<",\"expected_adapter_low\":"<<requirements.adapterLow
                        <<",\"expected_adapter_high\":"<<requirements.adapterHigh<<'}';
                    fail("world_source_epoch_or_tracking_mismatch",details.str());}
                else{
                    auto transfer=fvr::graphics::TransferResult::Faulted;
                    if(!fvr::graphics::Valid(worldOpened)){transfer=worldConsumer.Open(device.Get(),d);
                        if(transfer==fvr::graphics::TransferResult::Ok)worldOpened=d;}
                    if(fvr::graphics::Valid(worldOpened))transfer=worldConsumer.Copy(ticket,{{{worldEyes.Get(),0},{worldEyes.Get(),1}}});
                    copied=transfer==fvr::graphics::TransferResult::Ok;
                    if(transfer==fvr::graphics::TransferResult::Busy){++worldCopyBusy;stablePairs=0;}
                    else if(!copied)fail("world_gpu_copy_failed");
                    else{++worldPairs;++stablePairs;lastWorldFrame=ticket.frameId;lastWorldTicket=ticket;
                        bodyAvailable=frames.ReadBodyProps(ticket,lastBody);bodySampleNs=QpcNs();
                    }
                }
                // Always return this exact delivered ticket, including failures.
                frames.PairConsumed(ticket,copied);
                if(copied&&!worldCaptureAttempted&&stablePairs>=8&&GetTickCount64()-gameplayAt>=750){
                    worldCaptureAttempted=true;
                    const bool left=Save(device.Get(),context.Get(),worldEyes.Get(),0,folder/L"world-left.rgba",deadline,pulseNeutral);
                    const bool right=Save(device.Get(),context.Get(),worldEyes.Get(),1,folder/L"world-right.rgba",deadline,pulseNeutral);
                    worldSaved=left&&right;
                    std::ofstream report(folder/L"world-image.json");report<<"{\"elapsed_ms\":"<<GetTickCount64()-start
                        <<",\"width\":"<<d.width<<",\"height\":"<<d.height<<",\"format\":"<<d.format
                        <<",\"saved_pair\":"<<(worldSaved?"true":"false")<<",\"stable_pairs\":"<<stablePairs
                        <<",\"menu_epoch\":"<<current.epoch<<",\"identity\":";Identity(report,d,ticket);
                    report<<",\"body\":";Body(report,lastBody,bodyAvailable,bodySampleNs);
                    report<<",\"headset_tested\":false,\"reload_success_claimed\":false}";report.close();
                    if(!worldSaved||!report)fail("world_capture_failed");else event("one_world_pair_saved");
                }
            }
        }else{frames.Suspend();frames.FlushFeedback();stablePairs=0;gameplayAt=0;}
        if(now>=nextTelemetry&&telemetryRows<239){++telemetryRows;nextTelemetry=now+500;
            telemetry<<"{\"elapsed_ms\":"<<now-start<<",\"qpc\":"<<Qpc()<<",\"mode\":"<<unsigned(lastState.mode)
                <<",\"navigation_phase\":"<<unsigned(phase)<<",\"navigation_step\":"<<stepIndex()+1
                <<",\"menu_epoch\":"<<lastState.epoch<<",\"state_sequence\":"<<lastState.sequence
                <<",\"state_deadline_qpc\":"<<lastState.deadlineQpc<<",\"input_ready\":"<<lastState.inputReady
                <<",\"neutral_generation\":"<<generation<<",\"input_published\":"<<(published.succeeded?"true":"false")
                <<",\"menu_pairs\":"<<menuPairs<<",\"last_menu_ticket_sequence\":"<<lastMenuSequence
                <<",\"world_pairs\":"<<worldPairs<<",\"last_world_identity\":";Identity(telemetry,worldOpened,lastWorldTicket);
            telemetry<<",\"body\":";Body(telemetry,lastBody,bodyAvailable,bodySampleNs);
            telemetry<<",\"menu_connection_witness\":";connectionWitness.Write(telemetry,connectionWitness.Sample());
            telemetry<<",\"menu_work_loop_results\":{\"read_state\":";readStateResults.Write(telemetry);
            telemetry<<",\"read_state_after_copy\":";currentReadStateResults.Write(telemetry);
            telemetry<<",\"publish_control\":";publishControlResults.Write(telemetry);
            telemetry<<",\"take_surface\":";takeSurfaceResults.Write(telemetry);telemetry<<'}';
            telemetry<<",\"body_is_last_delivered_snapshot\":true,\"current_sample_qpc_ns\":"<<QpcNs()
                     <<",\"fire\":false,\"grab\":false,\"reload\":false,\"movement\":false}\n";telemetry.flush();
        }
        Sleep(10);
    }
    if(!failure&&!worldSaved)fail("observation_deadline_reached");
    // Keep the last failed operation even if its guard broke before telemetry.
    // Snapshot before our own Close sets closed; defer file I/O until neutral
    // retirement has completed. One row is reserved within the 240-row cap.
    const auto finalConnection=connectionWitness.Sample();const auto finalTelemetryAt=GetTickCount64();const auto finalTelemetryQpc=Qpc();
    retirement.Run();event("channels_retired");
    if(telemetryRows<240){++telemetryRows;
        telemetry<<"{\"elapsed_ms\":"<<finalTelemetryAt-start<<",\"qpc\":"<<finalTelemetryQpc
                 <<",\"sampling_phase\":\"before_host_retirement\",\"menu_connection_witness\":";
        connectionWitness.Write(telemetry,finalConnection);
        telemetry<<",\"menu_work_loop_results\":{\"read_state\":";readStateResults.Write(telemetry);
        telemetry<<",\"read_state_after_copy\":";currentReadStateResults.Write(telemetry);
        telemetry<<",\"publish_control\":";publishControlResults.Write(telemetry);
        telemetry<<",\"take_surface\":";takeSurfaceResults.Write(telemetry);telemetry<<"}}\n";telemetry.flush();
    }
    const unsigned expectedEpisodes=steps[2].accepted?3u:2u;
    const bool okay=!failure&&steps[0].saved&&steps[1].saved&&steps[0].accepted&&steps[1].accepted&&
        (!steps[2].accepted||steps[2].saved)&&pressEpisodes==expectedEpisodes&&releaseEpisodes==expectedEpisodes&&worldSaved;
    std::ofstream result(folder/L"result.json");
    result<<"{\"passed_observation\":"<<(okay?"true":"false")<<",\"failure\":\""<<(failure?failure:"")
          <<"\",\"failure_details\":"<<failureDetails<<",\"lifetime_limit_ms\":"<<LifetimeMs<<",\"elapsed_ms\":"<<GetTickCount64()-start
          <<",\"menu_saved\":"<<(steps[0].saved?"true":"false")<<",\"singleplayer_saved\":"<<(steps[1].saved?"true":"false")
          <<",\"post_resume_saved\":"<<(steps[2].saved?"true":"false")<<",\"post_resume_skipped_loading_or_gameplay\":"<<(postResumeSkipped?"true":"false")
          <<",\"world_pair_saved\":"<<(worldSaved?"true":"false")
          <<",\"root_request1_accepted\":"<<(steps[0].accepted?"true":"false")<<",\"root_request2_accepted\":"<<(steps[1].accepted?"true":"false")
          <<",\"root_request3_accepted\":"<<(steps[2].accepted?"true":"false")
          <<",\"campaign_confirmation_x\":"<<steps[2].x<<",\"campaign_confirmation_y\":"<<steps[2].y
          <<",\"campaign_resume_x\":"<<steps[1].x<<",\"campaign_resume_y\":"<<steps[1].y<<",\"published_press_episodes\":"<<pressEpisodes
          <<",\"published_release_episodes\":"<<releaseEpisodes<<",\"native_down_up_counts_require_trace\":true"
          <<",\"menu_epoch\":"<<steps[0].pin.state.epoch<<",\"singleplayer_menu_epoch\":"<<steps[1].pin.state.epoch
          <<",\"post_resume_menu_epoch\":"<<steps[2].pin.state.epoch<<",\"post_resume_neutral_generation\":"<<steps[2].neutralGeneration
          <<",\"menu_pairs\":"<<menuPairs<<",\"world_pairs\":"<<worldPairs<<",\"menu_copy_busy\":"<<menuCopyBusy<<",\"world_copy_busy\":"<<worldCopyBusy
          <<",\"telemetry_rows\":"<<telemetryRows<<",\"neutral_input_samples\":"<<generation
          <<",\"retirement_neutral_publications\":"<<retirement.neutralPublications
          <<",\"menu_feedback_drained\":"<<(retirement.menuAckFinished?"true":"false")
          <<",\"world_feedback_drained\":"<<(retirement.frameFeedbackFinished?"true":"false")
          <<",\"raw_image_limit\":5,\"maximum_raw_bytes\":"<<MaximumRawBytes
          <<",\"root_review_attested_by_request1\":"<<(steps[0].accepted?"true":"false")
          <<",\"root_review_attested_by_request2\":"<<(steps[1].accepted?"true":"false")
          <<",\"root_review_attested_by_request3\":"<<(steps[2].accepted?"true":"false")
          <<",\"front_end_proof_contract\":\"root_reviewed_main_and_singleplayer_images_and_normal_trace\",\"headset_tested\":false"
          <<",\"native_reload_ready_claimed\":false,\"reload_success_claimed\":false}";
    result.close();return okay&&result?0:2;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
