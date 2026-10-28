#pragma once
#include <Windows.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <ostream>
#include <type_traits>

// Opt-in diagnostic only. No game writes, new hooks, callback allocation,
// waits or file IO. Configure before admission; report after existing drains.
namespace fvr::diagnostics::privateCpu {
enum class Stage:unsigned {MainOrdinary,MainTracked,ExtraEye,RigTotal,RigRead,ProfileCapture,GraphicsPump,ProducerPublish,Count};
constexpr unsigned Count=unsigned(Stage::Count),MaxSamples=8192,RigRecords=8;
constexpr std::array<unsigned,7> HistogramUpperMicroseconds{10,100,1000,5000,10000,20000,50000};
struct Identity {
    unsigned thread=0;std::uint64_t creation=0;
    explicit operator bool()const noexcept{return thread&&creation;}
    bool operator==(const Identity&)const=default;
};
struct Stamp {std::uint64_t qpc=0,cycles=0;};
inline bool Now(Stamp& out)noexcept {
    const auto error=GetLastError();LARGE_INTEGER q{};ULONG64 cycles=0;
    const bool valid=QueryPerformanceCounter(&q)&&q.QuadPart>0&&QueryThreadCycleTime(GetCurrentThread(),&cycles);
    if(valid)out={std::uint64_t(q.QuadPart),cycles};SetLastError(error);return valid;
}
struct NativeClock {
    static Identity Current()noexcept {
        const auto error=GetLastError();
        // A new physical thread gets a new TLS value even if Windows reuses its
        // numeric TID. Never accept a different lifetime through a cached TID.
        thread_local const Identity identity=[]()noexcept {
            FILETIME created{},exit{},kernel{},user{};Identity result{};
            if(GetThreadTimes(GetCurrentThread(),&created,&exit,&kernel,&user))
                result={GetCurrentThreadId(),(std::uint64_t(created.dwHighDateTime)<<32)|created.dwLowDateTime};
            return result;
        }();
        SetLastError(error);return identity;
    }
    static bool Sample(Stamp& out)noexcept{return Now(out);}
    static bool Configure(unsigned thread,const wchar_t* environment,Identity& owner,std::uint64_t& frequency)noexcept {
        const auto error=GetLastError();wchar_t value[4]{};LARGE_INTEGER hz{};bool okay=false;
        const auto n=GetEnvironmentVariableW(environment,value,4);
        if(thread&&n==2&&value[0]==L'1'&&value[1]==L'2'&&QueryPerformanceFrequency(&hz)&&hz.QuadPart>0&&
           std::uint64_t(hz.QuadPart)<=UINT64_MAX/12){
            // Setup worker only. Bind the currently live graphics owner, then
            // compare its creation identity again on the actual callback thread.
            const auto handle=OpenThread(THREAD_QUERY_LIMITED_INFORMATION,FALSE,thread);
            if(handle){FILETIME created{},exit{},kernel{},user{};
                if(GetProcessIdOfThread(handle)==GetCurrentProcessId()&&GetThreadTimes(handle,&created,&exit,&kernel,&user)){
                    owner={thread,(std::uint64_t(created.dwHighDateTime)<<32)|created.dwLowDateTime};
                    frequency=std::uint64_t(hz.QuadPart);okay=bool(owner);
                }
                CloseHandle(handle);
            }
        }
        SetLastError(error);return okay;
    }
};
struct Aggregate {
    std::uint64_t calls=0,samples=0,qpcSum=0,qpcMax=0,cyclesSum=0,cyclesMax=0;
    unsigned reentrant=0,crossedDeadline=0,clockErrors=0,abnormal=0,capped=0;
    std::array<unsigned,8> elapsedHistogram{};
    bool active=false,saturated=false;
};
struct Token {Stamp begin{};Stage stage{};unsigned record=0;Identity owner{};bool active=false;};
static_assert(std::is_trivially_destructible_v<Token>); // Native __finally leaf.
template<class Clock> class BasicRecorder {
    enum Phase:unsigned {Off,Armed,Starting,Collecting,Stopped};
    enum RecordPhase:unsigned {Empty,Claiming,Bound,Measuring,Ended};
    struct Record {
        std::atomic<unsigned> phase{Empty};Identity owner{};
        Stamp first{},lastObserved{},end{};std::array<Aggregate,Count> rows{};
    };
    std::atomic<unsigned> phase_{Off};bool configured_=false;
    std::uint64_t frequency_=0,activation_=0,deadline_=0;Identity activator_{};
    std::array<Record,1+RigRecords> records_{};
    std::array<std::atomic<unsigned>,Count> foreign_{},unregistered_{};
    std::atomic<unsigned> startErrors_{0},overflow_{0},lateAdmission_{0};
    static bool Rig(Stage stage)noexcept{return stage==Stage::RigTotal||stage==Stage::RigRead||stage==Stage::ProfileCapture;}
    static void Add(std::uint64_t& sum,std::uint64_t value,bool& saturated)noexcept {
        if(value>UINT64_MAX-sum){sum=UINT64_MAX;saturated=true;}else sum+=value;
    }
    static bool Ready(unsigned phase)noexcept{return phase==Bound||phase==Measuring||phase==Ended;}
    int FindRig(Identity who)const noexcept {
        for(unsigned i=1;i<records_.size();++i)
            if(Ready(records_[i].phase.load(std::memory_order_acquire))&&records_[i].owner==who)return int(i);
        return -1;
    }
    int Owner(Stage stage,Identity who)noexcept {
        if(!who){foreign_[unsigned(stage)].fetch_add(1,std::memory_order_relaxed);return -1;}
        if(Rig(stage)){
            const auto index=FindRig(who);
            if(index<0)unregistered_[unsigned(stage)].fetch_add(1,std::memory_order_relaxed);
            return index;
        }
        if(records_[0].owner==who)return 0;
        foreign_[unsigned(stage)].fetch_add(1,std::memory_order_relaxed);return -1;
    }
    void Stop(Record& record,const Stamp& now)noexcept {
        if(record.phase.load(std::memory_order_relaxed)==Measuring){
            record.end=now;record.phase.store(Ended,std::memory_order_release);
        }
        phase_.store(Stopped,std::memory_order_release);
    }
public:
    void ConfigureFromEnvironment(unsigned provenGraphicsThread,const wchar_t* environment)noexcept {
        // Setup worker, before any callbacks. Reconfiguration never resets a
        // registered owner/window or converts an old token into a new sample.
        if(configured_)return;configured_=true;Identity owner{};std::uint64_t frequency=0;
        if(!Clock::Configure(provenGraphicsThread,environment,owner,frequency)||!owner||!frequency||frequency>UINT64_MAX/12)return;
        records_[0].owner=owner;records_[0].phase.store(Bound,std::memory_order_release);
        frequency_=frequency;phase_.store(Armed,std::memory_order_release);
    }
    bool Enabled()const noexcept {const auto phase=phase_.load(std::memory_order_acquire);return phase==Armed||phase==Starting||phase==Collecting;}
    bool AllowOwner(Stage fallback)noexcept {
        const auto phase=phase_.load(std::memory_order_acquire);
        if(phase!=Collecting&&phase!=Stopped)return false;
        if(phase==Stopped&&records_[0].phase.load(std::memory_order_acquire)!=Measuring)return false;
        // Native draw classification must still reject foreign graphics
        // callbacks BEFORE looking at the owner's mutable view state.
        return Owner(fallback,Clock::Current())>=0;
    }
    void StartFromVerifiedNativeOwner()noexcept {
        // Caller has freshly proved the local infantry actor/weak/equipment.
        // Animation workers may migrate: each exact lifetime owns its own rows.
        auto phase=phase_.load(std::memory_order_acquire);
        if(phase!=Armed&&phase!=Collecting)return;
        const auto who=Clock::Current();
        if(phase==Collecting&&who&&FindRig(who)>=0)return;
        Stamp now{};
        if(!who||!Clock::Sample(now)||!now.qpc||now.qpc>UINT64_MAX-frequency_*12){
            startErrors_.fetch_add(1,std::memory_order_relaxed);return;
        }
        if(phase==Armed){
            if(!phase_.compare_exchange_strong(phase,Starting,std::memory_order_acq_rel))return;
            activation_=now.qpc;deadline_=now.qpc+frequency_*12;activator_=who;
            phase_.store(Collecting,std::memory_order_release);
        }
        if(now.qpc<activation_){startErrors_.fetch_add(1,std::memory_order_relaxed);return;}
        if(now.qpc>=deadline_){lateAdmission_.fetch_add(1,std::memory_order_relaxed);phase_.store(Stopped,std::memory_order_release);return;}
        if(FindRig(who)>=0)return;
        for(unsigned i=1;i<records_.size();++i){auto expected=unsigned(Empty);
            if(!records_[i].phase.compare_exchange_strong(expected,Claiming,std::memory_order_acq_rel))continue;
            auto& record=records_[i];record.owner=who;record.first=record.lastObserved=now;
            record.phase.store(Measuring,std::memory_order_release);return;
        }
        overflow_.fetch_add(1,std::memory_order_relaxed);
    }
    Token Begin(Stage stage,bool eligible=true)noexcept {
        if(!eligible||unsigned(stage)>=Count)return {};
        const auto phase=phase_.load(std::memory_order_acquire);
        if(phase!=Collecting&&phase!=Stopped)return {};
        const auto who=Clock::Current();const auto index=Owner(stage,who);if(index<0)return {};
        auto& record=records_[unsigned(index)];const auto recordPhase=record.phase.load(std::memory_order_acquire);
        if(recordPhase==Ended)return {};
        auto& row=record.rows[unsigned(stage)];++row.calls;
        if(row.active){++row.reentrant;return {};}
        Stamp now{};
        if(!Clock::Sample(now)||now.qpc<activation_||
           (recordPhase==Measuring&&(now.qpc<record.first.qpc||now.cycles<record.first.cycles))){++row.clockErrors;return {};}
        if(recordPhase==Bound){
            // Graphics can start later than rig activation. Never fabricate
            // graphics cycles at the rig's QPC origin, or extend the deadline.
            if(now.qpc>=deadline_){phase_.store(Stopped,std::memory_order_release);return {};}
            record.first=record.lastObserved=now;record.phase.store(Measuring,std::memory_order_release);
        }
        if(now.qpc>=deadline_){Stop(record,now);return {};}
        // Another owner may have observed the deadline first; do not accept a
        // late/racing sample even if its clock stamp was just before the gate.
        if(phase_.load(std::memory_order_acquire)==Stopped)return {};
        record.lastObserved=now;
        if(row.samples>=MaxSamples){++row.capped;return {};}
        row.active=true;return {now,stage,unsigned(index),who,true};
    }
    void End(Token token,bool normal=true)noexcept {
        if(!token.active||unsigned(token.stage)>=Count||token.record>=records_.size())return;
        auto& record=records_[token.record];
        if(Clock::Current()!=token.owner||record.owner!=token.owner){
            foreign_[unsigned(token.stage)].fetch_add(1,std::memory_order_relaxed);return;
        }
        auto& row=record.rows[unsigned(token.stage)];row.active=false;Stamp now{};
        if(!Clock::Sample(now)||now.qpc<token.begin.qpc||now.cycles<token.begin.cycles){++row.clockErrors;return;}
        if(now.qpc>=deadline_){++row.crossedDeadline;Stop(record,now);return;}
        record.lastObserved=now;
        if(!normal){++row.abnormal;return;}
        const auto elapsed=now.qpc-token.begin.qpc,cycles=now.cycles-token.begin.cycles;
        ++row.samples;Add(row.qpcSum,elapsed,row.saturated);Add(row.cyclesSum,cycles,row.saturated);
        if(elapsed>row.qpcMax)row.qpcMax=elapsed;if(cycles>row.cyclesMax)row.cyclesMax=cycles;
        unsigned bucket=0;
        for(const auto us:HistogramUpperMicroseconds){
            const auto limit=(frequency_/1000000)*us+((frequency_%1000000)*us+999999)/1000000;
            if(elapsed<limit)break;++bucket;
        }
        ++row.elapsedHistogram[bucket];
    }
    void Report(std::ostream& out,bool callbacksDrained)const {
        const auto phase=phase_.load(std::memory_order_acquire);
        out<<"{\"schema\":\"fvr.bc2.private-cpu-attribution.v2\",\"enabled\":"<<(phase?"true":"false")
           <<",\"available\":"<<(callbacksDrained?"true":"false")<<",\"inclusive_nested_stages\":true";
        if(!callbacksDrained){out<<'}';return;}
        out<<",\"qpc_frequency\":"<<frequency_<<",\"activation_qpc\":"<<activation_<<",\"deadline_qpc\":"<<deadline_
           <<",\"activation_thread\":"<<activator_.thread<<",\"activation_creation_filetime\":"<<activator_.creation
           <<",\"window_closed\":"<<(phase==Stopped?"true":"false")<<",\"start_errors\":"<<startErrors_.load()
           <<",\"rig_record_capacity\":"<<RigRecords<<",\"rig_admission_overflow_calls\":"<<overflow_.load()
           <<",\"late_admission_calls\":"<<lateAdmission_.load()<<",\"per_stage_sample_cap\":"<<MaxSamples;
        const char* names[]={"native_main_ordinary","native_main_tracked","native_extra_eye","rig_total","rig_fresh_read","profile_capture","graphics_pump","producer_publish"};
        out<<",\"routing\":[";
        for(unsigned i=0;i<Count;++i){if(i)out<<',';out<<"{\"name\":\""<<names[i]<<"\",\"foreign_thread\":"<<foreign_[i].load()
            <<",\"unregistered_thread\":"<<unregistered_[i].load()<<'}';}
        out<<"],\"records\":[";bool first=true;
        for(unsigned index=0;index<records_.size();++index){const auto& record=records_[index];
            if(!Ready(record.phase.load(std::memory_order_acquire)))continue;
            if(!first)out<<',';first=false;
            out<<"{\"slot\":"<<index<<",\"role\":\""<<(index?"rig":"graphics")<<"\",\"owner_thread\":"<<record.owner.thread
               <<",\"owner_creation_filetime\":"<<record.owner.creation<<",\"begin_qpc\":"<<record.first.qpc
               <<",\"deadline_qpc\":"<<deadline_<<",\"end_qpc\":"<<record.end.qpc
               <<",\"begin_thread_cycles\":"<<record.first.cycles<<",\"end_thread_cycles\":"<<record.end.cycles
               <<",\"last_observed_qpc\":"<<record.lastObserved.qpc<<",\"last_observed_thread_cycles\":"<<record.lastObserved.cycles
               <<",\"window_elapsed\":"<<((deadline_&&record.end.qpc>=deadline_)?"true":"false")<<",\"stages\":[";
            bool firstStage=true;
            for(unsigned i=0;i<Count;++i){if(Rig(Stage(i))!=(index!=0))continue;
                if(!firstStage)out<<',';firstStage=false;const auto& r=record.rows[i];
                out<<"{\"name\":\""<<names[i]<<"\",\"calls\":"<<r.calls<<",\"samples\":"<<r.samples
                   <<",\"qpc_sum\":"<<r.qpcSum<<",\"qpc_max\":"<<r.qpcMax<<",\"thread_cycles_sum\":"<<r.cyclesSum
                   <<",\"thread_cycles_max\":"<<r.cyclesMax<<",\"reentrant\":"<<r.reentrant<<",\"crossed_deadline\":"<<r.crossedDeadline
                   <<",\"clock_errors\":"<<r.clockErrors<<",\"abnormal\":"<<r.abnormal<<",\"capped\":"<<r.capped
                   <<",\"active\":"<<(r.active?"true":"false")<<",\"saturated\":"<<(r.saturated?"true":"false")<<",\"elapsed_histogram\":[";
                for(unsigned b=0;b<r.elapsedHistogram.size();++b){if(b)out<<',';out<<r.elapsedHistogram[b];}out<<"]}";
            }out<<"]}";
        }
        out<<"],\"histogram_upper_microseconds\":[";
        for(unsigned b=0;b<HistogramUpperMicroseconds.size();++b){if(b)out<<',';out<<HistogramUpperMicroseconds[b];}
        out<<"],\"histogram_final_bucket_unbounded\":true}";
    }
};
using Recorder=BasicRecorder<NativeClock>;
inline Recorder recorder;
class Scope {
    Token token_;
public:
    explicit Scope(Stage stage,bool eligible=true)noexcept:token_(recorder.Begin(stage,eligible)){}
    ~Scope(){recorder.End(token_);}
    Scope(const Scope&)=delete;Scope& operator=(const Scope&)=delete;
};
}
