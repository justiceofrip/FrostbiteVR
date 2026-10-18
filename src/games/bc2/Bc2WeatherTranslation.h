#pragma once
#include "Bc2WeatherObservation.h"
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <optional>
#include <mutex>

namespace fvr::bc2::weatherTranslation {
#if defined(_WIN32)&&defined(_MSC_VER)
using NativeFinallyCall=void*(*)(void*);
using NativeFinallyCleanup=void(*)(void*,bool)noexcept;
// This leaf is also exercised by the Windows SEH regression. Never return from
// inside __try: that is an abnormal exit even when the native call succeeded.
// An explicit completion flag distinguishes native return from exception unwind.
__declspec(noinline) inline void* InvokeWithFinally(NativeFinallyCall call,void* context,NativeFinallyCleanup cleanup){
    void* result=nullptr;bool returned=false;
    __try {result=call(context);returned=true;}
    __finally {cleanup(context,returned);}
    return result;
}
#endif
using Words=std::array<std::uint32_t,16>;
struct Packet {
    std::uint32_t address=0,owner=0,packetOwner=0,world=0,flags=0;
    std::uint32_t arena=0,base=0,capacity=0,used=0;
    Words matrix{};
};
inline bool Overlap(std::uint32_t a,unsigned an,std::uint32_t b,unsigned bn)noexcept {
    return std::uint64_t(a)<std::uint64_t(b)+bn&&std::uint64_t(b)<std::uint64_t(a)+an;
}
inline bool Contains(const Packet& p,std::uint32_t address,unsigned size)noexcept {
    return address>=p.base&&std::uint64_t(address)+size<=std::uint64_t(p.base)+p.used;
}
inline bool Bounds(const Packet& p)noexcept {
    return p.owner>=0x10000&&p.owner<=UINT32_MAX-0x40&&p.arena>=0x10000&&p.arena<=UINT32_MAX-12&&
        p.packetOwner==p.owner&&(p.flags&0x2000)&&p.base>=0x10000&&p.capacity&&p.capacity<=0x40000000&&
        p.used<=p.capacity&&std::uint64_t(p.base)+p.capacity<=UINT32_MAX&&
        !(p.address&15)&&!(p.world&15)&&Contains(p,p.address,128)&&Contains(p,p.world,64)&&
        !Overlap(p.address,128,p.world,64);
}
inline bool SameStorage(const Packet& a,const Packet& b)noexcept {
    return Bounds(a)&&Bounds(b)&&a.address==b.address&&a.owner==b.owner&&a.packetOwner==b.packetOwner&&
        a.world==b.world&&a.arena==b.arena&&a.base==b.base&&a.capacity==b.capacity&&b.used>=a.used;
}
struct Plan {Packet packet{};Words after{};std::array<float,3> delta{};};
struct FrameToken {
    std::uint64_t epoch=0;weatherObservation::Key key{};
    bool operator==(const FrameToken&)const=default;
    explicit operator bool()const noexcept{return epoch&&key.Valid();}
};
struct FramePackets {FrameToken token{};std::array<Packet,2> packets{};std::array<Words,2> cameras{};};
// CPU-only bounded cache. No native addresses are dereferenced here. A producer
// carries its entry token through the original call; a delayed publisher cannot
// fill slots in a new frame, even if native addresses/frame IDs are reused.
class FrameCache {
public:
    bool Begin(weatherObservation::Key key)noexcept {
        std::lock_guard lock(mutex_);if(active_||!key.Valid())return false;
        ++epoch_;if(!epoch_)++epoch_;current_={epoch_,key};data_={};data_.token=current_;
        mask_=0;phase_=1;producers_=0;producingMask_=0;invalid_=false;return true;
    }
    FrameToken Capture(weatherObservation::Key key)noexcept {
        std::lock_guard lock(mutex_);return current_.key==key?current_:FrameToken{};
    }
    bool Current(FrameToken token)noexcept {std::lock_guard lock(mutex_);return token&&token==current_;}
    FrameToken EnterProducer(weatherObservation::Key key,unsigned slot)noexcept {
        std::lock_guard lock(mutex_);if(!current_||current_.key!=key||slot>1)return {};
        if(active_||((mask_|producingMask_)&(1u<<slot)))invalid_=true;
        ++producers_;producingMask_|=1u<<slot;return current_;
    }
    bool CompleteProducer(FrameToken token,unsigned slot,const Packet* packet,const Words* camera)noexcept {
        std::lock_guard lock(mutex_);if(!token||token!=current_||slot>1||!producers_)return false;
        --producers_;producingMask_&=~(1u<<slot);
        if(!packet||!camera||invalid_||active_||(mask_&(1u<<slot))){invalid_=true;return false;}
        data_.packets[slot]=*packet;data_.cameras[slot]=*camera;mask_|=1u<<slot;return true;
    }
    enum class Skip:unsigned {None,Contended,Stale,Invalid,Busy,Missing,Phase,Count};
    std::optional<FramePackets> Claim(FrameToken token,unsigned phase,Skip* reason=nullptr)noexcept {
        std::unique_lock lock(mutex_,std::try_to_lock);
        const auto skip=!lock?Skip::Contended:(!token||token!=current_?Skip::Stale:(invalid_?Skip::Invalid:
            (active_||producers_?Skip::Busy:(mask_!=3?Skip::Missing:(phase!=phase_||phase>2?Skip::Phase:Skip::None)))));
        if(reason)*reason=skip;if(skip!=Skip::None)return {};
        active_=true;return data_;
    }
    bool ConsumerIntact(FrameToken token,unsigned phase)noexcept {
        std::lock_guard lock(mutex_);return token==current_&&active_&&phase==phase_&&!invalid_&&!producers_;
    }
    bool Release(FrameToken token,unsigned phase)noexcept {
        // No cache lock is held across native calls or memory reads. Release and
        // retirement finish the short CPU-only transition rather than leaving a
        // scope permanently claimed after a harmless try-lock collision.
        std::lock_guard lock(mutex_);
        if(token!=current_||!active_||phase!=phase_)return false;
        active_=false;++phase_;return true;
    }
    bool Retire(FrameToken token)noexcept {
        std::lock_guard lock(mutex_);if(token!=current_||active_)return false;
        current_={};data_={};mask_=0;producers_=0;producingMask_=0;invalid_=false;return true;
    }
private:
    std::mutex mutex_;std::uint64_t epoch_=0;FrameToken current_{};FramePackets data_{};
    unsigned mask_=0,phase_=1,producers_=0,producingMask_=0;bool active_=false,invalid_=false;
};
inline std::optional<Plan> MakePlan(const Packet& captured,const Packet& current,const Words& left,const Words& right)noexcept {
    if(!SameStorage(captured,current)||captured.matrix!=current.matrix)return {};
    float distance2=0;
    for(unsigned i=0;i<16;++i){
        if(!std::isfinite(std::bit_cast<float>(left[i]))||!std::isfinite(std::bit_cast<float>(right[i])))return {};
        if(i<12&&left[i]!=right[i])return {};
        if(i>=12&&i<=14){if(current.matrix[i]!=right[i])return {};}
        else if(current.matrix[i]!=(i%5==0?0x3f800000u:0u))return {};
    }
    Plan out{current,current.matrix,{}};
    for(unsigned i=0;i<3;++i){out.delta[i]=std::bit_cast<float>(left[i+12])-std::bit_cast<float>(right[i+12]);
        distance2+=out.delta[i]*out.delta[i];out.after[i+12]=left[i+12];}
    if(!(distance2>0&&distance2<=.15f*.15f))return {};
    return out;
}
enum class Replaced {Rejected,Applied,Indeterminate};
// Fixed-size experimental transaction. Its destructor deliberately NEVER reads
// game pointers. The adapter must explicitly restore through the same guarded
// owner/frame/arena callback; failed ownership leaves a terminal receipt.
class Transaction {
public:
    bool Stage(const std::array<Plan,2>& plans)noexcept {
        if(staged_||failed_)return false;
        for(const auto& p:plans)if(!Bounds(p.packet))return false;
        const auto& a=plans[0].packet;const auto& b=plans[1].packet;
        if(Overlap(a.world,64,b.world,64)||Overlap(a.address,128,b.address,128)||
           Overlap(a.address,128,b.world,64)||Overlap(a.world,64,b.address,128))return false;
        plans_=plans;staged_=true;return true;
    }
    template<class Replace> bool Apply(Replace&& replace)noexcept {
        if(!staged_||attempted_||failed_)return false;attempted_=true;
        for(unsigned i=0;i<2;++i){const auto result=replace(i,plans_[i].packet.matrix,plans_[i].after);
            if(result!=Replaced::Rejected){active_|=1u<<i;written_|=1u<<i;}
            if(result!=Replaced::Applied){failed_=true;return false;}}
        return true;
    }
    template<class Replace> bool Restore(Replace&& replace)noexcept {
        bool restored=true;
        for(unsigned i=0;i<2;++i)if(active_&(1u<<i)){
            if(replace(i,plans_[i].after,plans_[i].packet.matrix)==Replaced::Applied)active_&=~(1u<<i);
            else {restored=false;failed_=true;}}
        return restored;
    }
    unsigned UnresolvedMask()const noexcept{return active_;}
    unsigned WrittenMask()const noexcept{return written_;}
    bool Failed()const noexcept{return failed_;}
private:
    std::array<Plan,2> plans_{};unsigned active_=0,written_=0;
    bool staged_=false,attempted_=false,failed_=false;
};
// Additional exact static/live pins for the allocator and state-packet enqueue.
inline constexpr std::array<weatherObservation::CodePin,3> CodePins{{
    {0x58d680,0x5a,0xa11e9190bff90b4eULL},
    {0x5a1290,0x25,0xa0d51222010673aaULL},
    {0x7dcc10,0x6f,0xca44e37ce983cf36ULL},
}};
}
