#include "fvr/ipc/MenuChannel.h"
#include <Windows.h>
#include <cstring>
namespace fvr::ipc {
namespace {
struct Handle {HANDLE h=nullptr;~Handle(){if(h)CloseHandle(h);}};
struct alignas(8) Shared {
    std::uint32_t magic=0x314d5646,version=1,bytes=0,closed=0;
    std::array<std::uint8_t,16> session{};
    std::uint32_t hostPid=0,producerPid=0;
    std::uint64_t hostStarted=0,producerStarted=0;
    MenuState status{};MenuControl control{};MenuSurface surface{};
    std::uint32_t phase=0,consumed=0; // idle, ready, delivered, complete
};
static_assert(sizeof(Shared)==384);
std::int64_t Now(){LARGE_INTEGER n{};return QueryPerformanceCounter(&n)?n.QuadPart:0;}
std::uint64_t Started(HANDLE p){FILETIME a{},b{},c{},d{};return GetProcessTimes(p,&a,&b,&c,&d)?(std::uint64_t(a.dwHighDateTime)<<32)|a.dwLowDateTime:0;}
bool Token(const std::wstring& t,std::array<std::uint8_t,16>& b){
    if(t.size()!=32)return false;bool any=false;
    for(unsigned i=0;i<16;++i){unsigned v=0;for(unsigned j=0;j<2;++j){auto c=t[2*i+j];if(c>=L'0'&&c<=L'9')v=v*16+c-L'0';else if(c>=L'a'&&c<=L'f')v=v*16+c-L'a'+10;else return false;}b[i]=std::uint8_t(v);any|=v!=0;}return any;
}
}
struct MenuChannel::State {
    bool host=false,fault=false;Handle mapping,mutex,peer;Shared* data=nullptr;
    std::array<std::uint8_t,16> session{};std::int64_t frequency=0;
    ~State(){if(data)UnmapViewOfFile(data);}
    bool Header()const {return data->magic==0x314d5646&&data->version==1&&data->bytes==sizeof(Shared)&&data->session==session&&data->closed<=1&&data->phase<=3&&data->consumed<=1&&data->hostPid&&data->hostStarted;}
    struct Lock {
        State& s;bool held=false;
        explicit Lock(State& state):s(state){if(s.fault||!s.mutex.h||!s.data)return;auto r=WaitForSingleObject(s.mutex.h,0);held=r==WAIT_OBJECT_0||r==WAIT_ABANDONED;if(held&&(r==WAIT_ABANDONED||!s.Header()))s.fault=true;}
        ~Lock(){if(held)ReleaseMutex(s.mutex.h);}explicit operator bool()const{return held&&!s.fault;}
    };
    bool Peer(){
        if(fault||data->closed)return false;auto pid=host?data->producerPid:data->hostPid;auto started=host?data->producerStarted:data->hostStarted;if(!pid)return false;
        if(!peer.h){peer.h=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);if(!peer.h||!started||Started(peer.h)!=started){fault=true;return false;}}
        if(WaitForSingleObject(peer.h,0)!=WAIT_TIMEOUT){fault=true;return false;}return true;
    }
    std::int64_t Deadline(){const auto now=Now();const auto ticks=frequency/10;return now>0&&ticks>0&&now<INT64_MAX-ticks?now+ticks:0;}
    ChannelResult Unlocked()const{return fault?ChannelResult::Closed:ChannelResult::Busy;}
};
MenuChannel::MenuChannel()=default;MenuChannel::~MenuChannel(){Close();}
void MenuChannel::Close()noexcept {if(state_){{State::Lock lock(*state_);if(lock)state_->data->closed=1;}state_.reset();}}
bool MenuChannel::CreateHost(const std::wstring& t)noexcept{return Open(t,true);}
bool MenuChannel::ConnectProducer(const std::wstring& t)noexcept{return Open(t,false);}
bool MenuChannel::Open(const std::wstring& token,bool host)noexcept {
    Close();try{
        auto s=std::make_unique<State>();s->host=host;if(!Token(token,s->session))return false;
        const auto prefix=L"Local\\FrostbiteVR.Menu.v1."+token;
        LARGE_INTEGER f{};if(!QueryPerformanceFrequency(&f)||f.QuadPart<=0)return false;s->frequency=f.QuadPart;
        if(host){
            s->mutex.h=CreateMutexW(nullptr,FALSE,(prefix+L".mutex").c_str());if(!s->mutex.h||GetLastError()==ERROR_ALREADY_EXISTS)return false;
            s->mapping.h=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(Shared),(prefix+L".map").c_str());if(!s->mapping.h||GetLastError()==ERROR_ALREADY_EXISTS)return false;
        }else{
            s->mutex.h=OpenMutexW(SYNCHRONIZE|MUTEX_MODIFY_STATE,FALSE,(prefix+L".mutex").c_str());if(!s->mutex.h)return false;
            s->mapping.h=OpenFileMappingW(FILE_MAP_READ|FILE_MAP_WRITE,FALSE,(prefix+L".map").c_str());if(!s->mapping.h)return false;
        }
        s->data=static_cast<Shared*>(MapViewOfFile(s->mapping.h,FILE_MAP_READ|FILE_MAP_WRITE,0,0,sizeof(Shared)));if(!s->data)return false;
        const auto started=Started(GetCurrentProcess());if(!started)return false;
        if(host){Shared initial{};initial.bytes=sizeof(initial);initial.session=s->session;initial.hostPid=GetCurrentProcessId();initial.hostStarted=started;std::memcpy(s->data,&initial,sizeof(initial));}
        else{State::Lock lock(*s);if(!lock||s->data->producerPid||!s->Peer())return false;s->data->producerStarted=started;s->data->producerPid=GetCurrentProcessId();}
        state_=std::move(s);return true;
    }catch(...){Close();return false;}
}
bool MenuChannel::Connected()noexcept {if(!state_)return false;State::Lock lock(*state_);return lock&&state_->Peer();}
ChannelResult MenuChannel::PublishState(MenuState v)noexcept {
    if(!state_||state_->host)return ChannelResult::Closed;auto& s=*state_;State::Lock lock(s);if(!lock)return s.Unlocked();if(!s.Peer())return ChannelResult::Closed;
    v.deadlineQpc=s.Deadline();if(!ValidMenuState(v)||v.sequence<=s.data->status.sequence)return ChannelResult::Invalid;s.data->status=v;return ChannelResult::Ok;
}
ChannelResult MenuChannel::ReadState(MenuState& v)noexcept {
    v={};if(!state_)return ChannelResult::Closed;auto& s=*state_;State::Lock lock(s);if(!lock)return s.Unlocked();if(!s.Peer())return ChannelResult::Closed;
    if(!ValidMenuState(s.data->status)||s.data->status.deadlineQpc<=Now())return ChannelResult::Timeout;v=s.data->status;return ChannelResult::Ok;
}
ChannelResult MenuChannel::PublishControl(MenuControl v)noexcept {
    if(!state_||!state_->host)return ChannelResult::Closed;auto& s=*state_;State::Lock lock(s);if(!lock)return s.Unlocked();if(!s.Peer())return ChannelResult::Closed;
    v.deadlineQpc=s.Deadline();if(!ValidMenuControl(v)||v.sequence<=s.data->control.sequence||v.toggle<s.data->control.toggle||v.cancel<s.data->control.cancel)return ChannelResult::Invalid;s.data->control=v;return ChannelResult::Ok;
}
ChannelResult MenuChannel::ReadControl(MenuControl& v)noexcept {
    v={};if(!state_||state_->host)return ChannelResult::Closed;auto& s=*state_;State::Lock lock(s);if(!lock)return s.Unlocked();if(!s.Peer())return ChannelResult::Closed;
    if(!ValidMenuControl(s.data->control)||s.data->control.deadlineQpc<=Now())return ChannelResult::Timeout;v=s.data->control;return ChannelResult::Ok;
}
ChannelResult MenuChannel::PublishSurface(const MenuSurface& v)noexcept {
    if(!state_||state_->host)return ChannelResult::Closed;auto& s=*state_;State::Lock lock(s);if(!lock)return s.Unlocked();if(!s.Peer())return ChannelResult::Closed;
    if(s.data->phase)return ChannelResult::Busy;
    if(!ValidMenuSurface(v))return ChannelResult::Invalid;
    if(v.state.deadlineQpc<=Now()||s.data->status.mode!=MenuMode::Menu||v.state.epoch!=s.data->status.epoch)return ChannelResult::Timeout;
    s.data->surface=v;s.data->phase=1;s.data->consumed=0;return ChannelResult::Ok;
}
ChannelResult MenuChannel::TakeSurface(MenuSurface& v)noexcept {
    v={};if(!state_||!state_->host)return ChannelResult::Closed;auto& s=*state_;State::Lock lock(s);if(!lock)return s.Unlocked();if(!s.Peer())return ChannelResult::Closed;
    if(s.data->phase!=1)return ChannelResult::Busy;
    if(!ValidMenuSurface(s.data->surface)||s.data->surface.state.deadlineQpc<=Now()||s.data->status.mode!=MenuMode::Menu||s.data->status.deadlineQpc<=Now()||s.data->surface.state.epoch!=s.data->status.epoch){s.data->phase=3;s.data->consumed=0;return ChannelResult::Timeout;}
    v=s.data->surface;s.data->phase=2;return ChannelResult::Ok;
}
ChannelResult MenuChannel::SurfaceConsumed(const graphics::PairTicket& t,bool consumed)noexcept {
    if(!state_||!state_->host)return ChannelResult::Closed;auto& s=*state_;State::Lock lock(s);if(!lock)return s.Unlocked();if(!s.Peer())return ChannelResult::Closed;
    if(s.data->phase!=2||std::memcmp(&t,&s.data->surface.ticket,sizeof(t)))return ChannelResult::Invalid;
    s.data->phase=3;s.data->consumed=consumed?1:0;return ChannelResult::Ok;
}
Outcome MenuChannel::PollOutcome()noexcept {
    if(!state_||state_->host)return Outcome::Discarded;auto& s=*state_;State::Lock lock(s);if(!lock)return s.fault?Outcome::Discarded:Outcome::Pending;if(!s.Peer())return Outcome::Discarded;
    if(s.data->phase==1&&(s.data->surface.state.deadlineQpc<=Now()||s.data->status.mode!=MenuMode::Menu||s.data->status.epoch!=s.data->surface.state.epoch)){s.data->phase=3;s.data->consumed=0;}
    if(s.data->phase!=3)return Outcome::Pending;const auto out=s.data->consumed?Outcome::Consumed:Outcome::Discarded;s.data->phase=0;s.data->consumed=0;return out;
}
}
