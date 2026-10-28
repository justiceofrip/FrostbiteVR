#pragma once
#include <array>
#include <cstdint>

namespace fvr::bc2::selectedKnife {
enum class KnifeTimingCall:unsigned { Update,Step,Export,Restore };
struct KnifeTimingState {
    unsigned current=0,next=0,timerBits=0,windupBits=0;
    std::array<unsigned,12> context{};
    std::array<unsigned,16> snapshot{};
    bool contextKnown=false,snapshotKnown=false;
};
struct KnifeTimingRow {
    std::uint64_t invocation=0,parentUpdate=0,serial=0,input=0,actorGeneration=0,equipGeneration=0;
    std::int64_t beginNs=0,endNs=0,publishedNs=0,sourceObservedNs=0,sourceDeadlineNs=0;
    KnifeTimingCall kind=KnifeTimingCall::Update;
    unsigned firing=0,pointer=0,caller=0,argument=0,deltaBits=0,thread=0,branch=3;
    unsigned soldier=0,weak=0,weapon=0,effects=0;
    std::array<unsigned,3> cohort{};
    KnifeTimingState before{},after{};
    unsigned returnPointer=0;bool returnPointerKnown=false;
    bool returned=false,ownerRetained=false;
};
bool TimingObservationRequested()noexcept;
bool TimingObservationEnabled()noexcept;
std::uint64_t BeginTimingObservation(KnifeTimingCall,unsigned firing,unsigned pointer,unsigned caller,
    unsigned argument,float delta,std::uint64_t parentUpdate)noexcept;
void EndTimingObservation(std::uint64_t token,unsigned returnPointer,bool returnPointerKnown)noexcept;
// One scope around an existing original native call. No observer slot is claimed
// and no input, timer, payload or native return value is modified.
inline thread_local std::uint64_t knifeTimingUpdate=0;
class KnifeTimingObservationScope {
public:
    KnifeTimingObservationScope(KnifeTimingCall kind,unsigned firing,unsigned pointer,unsigned caller,
        unsigned argument=0,float delta=0)noexcept:kind_(kind){
        if(!TimingObservationEnabled())return;
        token_=BeginTimingObservation(kind,firing,pointer,caller,argument,delta,knifeTimingUpdate);
        // An unobserved nested Update is a gap, not permission for its Step to
        // borrow the outer Update's invocation. Restore TLS even with token0.
        if(kind==KnifeTimingCall::Update){previous_=knifeTimingUpdate;knifeTimingUpdate=token_;updateScope_=true;}
    }
    void ReturnedPointer(unsigned value)noexcept{returnPointer_=value;returnPointerKnown_=true;}
    ~KnifeTimingObservationScope(){if(token_)EndTimingObservation(token_,returnPointer_,returnPointerKnown_);
        if(updateScope_)knifeTimingUpdate=previous_;}
    KnifeTimingObservationScope(const KnifeTimingObservationScope&)=delete;
    KnifeTimingObservationScope& operator=(const KnifeTimingObservationScope&)=delete;
private:
    KnifeTimingCall kind_;std::uint64_t token_=0,previous_=0;
    unsigned returnPointer_=0;bool returnPointerKnown_=false;
    bool updateScope_=false;
};
}
