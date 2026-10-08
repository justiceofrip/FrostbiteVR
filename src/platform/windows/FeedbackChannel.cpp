#include "fvr/ipc/FeedbackChannel.h"
#include <Windows.h>
#include <array>
#include <cstring>
#include <limits>
namespace fvr::ipc {
namespace {
struct Handle {HANDLE h=nullptr;~Handle(){if(h)CloseHandle(h);}};
struct alignas(8) Shared {
    std::uint32_t magic=0x31484346,version=1,bytes=584,closed=0;
    std::array<std::uint8_t,16> session{};
    std::uint32_t hostPid=0,producerPid=0;
    std::uint64_t hostStarted=0,producerStarted=0;
    std::uint32_t head=0,count=0;
    std::uint64_t lastEvent=0;
    std::array<FeedbackPacket,8> events{};
};
static_assert(sizeof(Shared)==584);
std::uint64_t Started(HANDLE p){FILETIME a{},b{},c{},d{};return GetProcessTimes(p,&a,&b,&c,&d)?(std::uint64_t(a.dwHighDateTime)<<32)|a.dwLowDateTime:0;}
bool Token(const std::wstring& t,std::array<std::uint8_t,16>& b){
    if(t.size()!=32)return false;bool any=false;
    for(unsigned i=0;i<16;++i){unsigned v=0;for(unsigned j=0;j<2;++j){const auto c=t[2*i+j];
        if(c>=L'0'&&c<=L'9')v=v*16+unsigned(c-L'0');else if(c>=L'a'&&c<=L'f')v=v*16+unsigned(c-L'a'+10);else return false;}
        b[i]=std::uint8_t(v);any|=v!=0;}return any;
}
}
struct FeedbackChannel::State {
    bool host=false,fault=false;Handle mapping,mutex,peer;Shared* data=nullptr;
    std::array<std::uint8_t,16> session{};std::int64_t frequency=0;
    ~State(){if(data)UnmapViewOfFile(data);}
    bool Header()const {return data->magic==0x31484346&&data->version==1&&data->bytes==sizeof(Shared)&&
        data->session==session&&data->closed<=1&&data->head<8&&data->count<=8&&data->hostPid&&data->hostStarted;}
    struct Lock {
        State& s;bool held=false;
        explicit Lock(State& state):s(state){if(s.fault||!s.mutex.h||!s.data)return;
            const auto r=WaitForSingleObject(s.mutex.h,0);held=r==WAIT_OBJECT_0||r==WAIT_ABANDONED;
            if(held&&(r==WAIT_ABANDONED||!s.Header()))s.fault=true;}
        ~Lock(){if(held)ReleaseMutex(s.mutex.h);}explicit operator bool()const{return held&&!s.fault;}
    };
    bool Peer(){
        if(fault||data->closed)return false;const auto pid=host?data->producerPid:data->hostPid;
        const auto started=host?data->producerStarted:data->hostStarted;if(!pid)return false;
        if(!peer.h){peer.h=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
            if(!peer.h||!started||Started(peer.h)!=started){fault=true;return false;}}
        if(WaitForSingleObject(peer.h,0)!=WAIT_TIMEOUT){fault=true;return false;}return true;
    }
    std::int64_t Now()const {LARGE_INTEGER q{};if(!QueryPerformanceCounter(&q)||q.QuadPart<=0||frequency<=0)return 0;
        if(q.QuadPart/frequency>std::numeric_limits<std::int64_t>::max()/1000000000)return 0;
        return q.QuadPart/frequency*1000000000+(q.QuadPart%frequency)*1000000000/frequency;}
    void Pop(){data->events[data->head]={};data->head=(data->head+1)%8;--data->count;}
};
FeedbackChannel::FeedbackChannel()=default;FeedbackChannel::~FeedbackChannel(){Close();}
void FeedbackChannel::Close()noexcept {if(state_){{State::Lock lock(*state_);if(lock)state_->data->closed=1;}state_.reset();}}
bool FeedbackChannel::CreateHost(const std::wstring& t)noexcept{return Open(t,true);}
bool FeedbackChannel::ConnectProducer(const std::wstring& t)noexcept{return Open(t,false);}
bool FeedbackChannel::Open(const std::wstring& token,bool host)noexcept {
    Close();try{
        auto s=std::make_unique<State>();s->host=host;if(!Token(token,s->session))return false;
        const auto prefix=L"Local\\FrostbiteVR.Feedback.v1."+token;
        LARGE_INTEGER f{};if(!QueryPerformanceFrequency(&f)||f.QuadPart<=0||f.QuadPart>1000000000)return false;s->frequency=f.QuadPart;
        if(host){
            s->mutex.h=CreateMutexW(nullptr,FALSE,(prefix+L".mutex").c_str());if(!s->mutex.h||GetLastError()==ERROR_ALREADY_EXISTS)return false;
            s->mapping.h=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(Shared),(prefix+L".map").c_str());if(!s->mapping.h||GetLastError()==ERROR_ALREADY_EXISTS)return false;
        }else{
            s->mutex.h=OpenMutexW(SYNCHRONIZE|MUTEX_MODIFY_STATE,FALSE,(prefix+L".mutex").c_str());if(!s->mutex.h)return false;
            s->mapping.h=OpenFileMappingW(FILE_MAP_READ|FILE_MAP_WRITE,FALSE,(prefix+L".map").c_str());if(!s->mapping.h)return false;
        }
        s->data=static_cast<Shared*>(MapViewOfFile(s->mapping.h,FILE_MAP_READ|FILE_MAP_WRITE,0,0,sizeof(Shared)));if(!s->data)return false;
        const auto started=Started(GetCurrentProcess());if(!started)return false;
        if(host){Shared initial{};initial.session=s->session;initial.hostPid=GetCurrentProcessId();initial.hostStarted=started;std::memcpy(s->data,&initial,sizeof(initial));}
        else {State::Lock lock(*s);if(!lock||s->data->producerPid||!s->Peer())return false;s->data->producerStarted=started;s->data->producerPid=GetCurrentProcessId();}
        state_=std::move(s);return true;
    }catch(...){Close();return false;}
}
bool FeedbackChannel::Publish(const interaction::FeedbackEvent& event)noexcept {
    if(!state_||state_->host)return false;auto& s=*state_;State::Lock lock(s);if(!lock||!s.Peer())return false;
    const auto now=s.Now();FeedbackPacket p;if(!EncodeFeedback(event,now,p)||event.id<=s.data->lastEvent)return false;
    while(s.data->count&&s.data->events[s.data->head].deadlineNs<=now)s.Pop();
    if(s.data->count==8)return false;
    s.data->events[(s.data->head+s.data->count)%8]=p;++s.data->count;s.data->lastEvent=event.id;return true;
}
bool FeedbackChannel::Take(interaction::FeedbackEvent& event)noexcept {
    event={};if(!state_||!state_->host)return false;auto& s=*state_;State::Lock lock(s);if(!lock||!s.Peer())return false;
    const auto now=s.Now();while(s.data->count){const auto p=s.data->events[s.data->head];s.Pop();if(DecodeFeedback(p,now,event))return true;}return false;
}
}
