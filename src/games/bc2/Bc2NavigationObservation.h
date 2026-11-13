#pragma once
#include "fvr/interaction/ControllerInput.h"
#include <algorithm>
#include <atomic>
#include <locale>
#include <ostream>
#include <sstream>
#include <string>

namespace fvr::bc2 {
enum class NavigationObservationReason : unsigned {
    Available,Disabled,Stopped,GatherPending,Busy,Changed,OwnerUnavailable,RouteChanged,InputPause,
    StaleInput,InvalidInput,FocusLoss,HeadUntracked,HandsUntracked,NotOnFoot,NativeInactive,MenuUnavailable,
    SourceClockChanged,MenuClockChanged
};
inline const char* NavigationObservationReasonName(NavigationObservationReason reason)noexcept {
    constexpr const char* names[]={"available","disabled","stopped","gather_pending","observation_busy",
        "observation_changed","owner_unavailable","route_changed","input_pause","stale_input","invalid_input",
        "focus_loss","head_untracked","hands_untracked","not_on_foot","native_inactive","menu_unavailable",
        "source_clock_changed","menu_clock_changed"};
    return names[unsigned(reason)];
}
struct NavigationObservationOwner {
    std::uint32_t manager=0,player=0,soldier=0,weak=0,controlled=0,entry=0,router=0,cache=0;
    std::uint64_t actorGeneration=0,inputEpoch=0,movementEpoch=0;
    bool operator==(const NavigationObservationOwner&)const=default;
};
struct NavigationObservation {
    // Publication serial is private transport coherence, never actor authority or JSON.
    std::uint64_t serial=0,gatherSequence=0;
    std::int64_t sampledNs=0,inputObservedNs=0,inputDeadlineNs=0,validUntilNs=0;
    NavigationObservationOwner owner{};
    bool onFoot=false,nativePlaying=false,allowedRead=false;
    std::uint32_t allowedMask=0;
    interaction::InputFrame input{};
    NavigationObservationReason reason=NavigationObservationReason::Disabled;
    bool Available()const noexcept{return reason==NavigationObservationReason::Available;}
};
inline NavigationObservationReason NavigationObservationRejection(const NavigationObservation& value,std::int64_t now)noexcept {
    using R=NavigationObservationReason;
    const auto& o=value.owner;const auto& input=value.input;
    if(!o.manager||!o.player||!o.soldier||!o.weak||!o.controlled||!o.entry||!o.router||!o.cache||
       !o.actorGeneration||!o.inputEpoch||!o.movementEpoch||!value.gatherSequence)return R::OwnerUnavailable;
    if(!value.onFoot||o.controlled!=o.soldier)return R::NotOnFoot;
    if(!value.allowedRead||!value.nativePlaying||(value.allowedMask&0x103u)!=0x103u)return R::NativeInactive;
    if(now<=0||value.inputObservedNs<=0||value.inputObservedNs>value.sampledNs||value.sampledNs>now||
       value.inputDeadlineNs<=now||value.inputDeadlineNs<=value.inputObservedNs||
       value.inputDeadlineNs-value.inputObservedNs>100000000)return R::StaleInput;
    if(!input.focused)return R::FocusLoss;
    if(!input.headValid)return R::HeadUntracked;
    for(const auto& hand:input.hands)if(!hand.gripTracked||!hand.aimTracked)return R::HandsUntracked;
    if(!interaction::ValidInput(input))return R::InvalidInput;
    if(!interaction::GameplayOwnsInput(input.menu,now)||
       value.validUntilNs!=(std::min)(value.inputDeadlineNs,input.menu.deadlineNs))return R::MenuUnavailable;
    return R::Available;
}

// One value mailbox, enabled only by the existing diagnostic startup admission.
// Neither input nor report threads wait. Revocation advances outside the copy
// gate so a dropped store/read cannot make an older value current again.
class NavigationObservationPublication {
public:
    bool Enable()noexcept {
        bool expected=false;if(!enabled_.compare_exchange_strong(expected,true,std::memory_order_acq_rel))return false;
        Invalidate(NavigationObservationReason::GatherPending);return true;
    }
    bool Enabled()const noexcept{return enabled_.load(std::memory_order_acquire);}
    std::uint64_t Invalidate(NavigationObservationReason reason)noexcept {
        if(!Enabled())return 0;
        reason_.store(reason,std::memory_order_release);
        return serial_.fetch_add(1,std::memory_order_acq_rel)+1;
    }
    std::uint64_t BeginGather()noexcept{return Invalidate(NavigationObservationReason::GatherPending);}
    void InvalidatePlayer(std::uint32_t player,NavigationObservationReason reason)noexcept {
        if(Enabled()&&player&&player_.load(std::memory_order_acquire)==player)Invalidate(reason);
    }
    void Stop()noexcept {
        enabled_.store(false,std::memory_order_release);
        reason_.store(NavigationObservationReason::Stopped,std::memory_order_release);
        serial_.fetch_add(1,std::memory_order_acq_rel);
    }
    bool Publish(std::uint64_t serial,NavigationObservation value,const NavigationObservationOwner& current,
        bool cacheCurrent,std::int64_t now)noexcept {
        if(!serial||!Enabled())return false;
        value.serial=serial;value.sampledNs=now;
        value.validUntilNs=(std::min)(value.inputDeadlineNs,value.input.menu.deadlineNs);
        auto reason=value.owner==current&&cacheCurrent?NavigationObservationRejection(value,now):NavigationObservationReason::OwnerUnavailable;
        if(reason!=NavigationObservationReason::Available){Invalidate(reason);return false;}
        if(gate_.test_and_set(std::memory_order_acquire)){Invalidate(NavigationObservationReason::Busy);return false;}
        if(!Enabled()||serial_.load(std::memory_order_acquire)!=serial){gate_.clear(std::memory_order_release);return false;}
        // Repeated source/menu packets keep their original clocks, including
        // after a local refusal. A later observation time cannot extend either.
        if(latest_.input.generation==value.input.generation&&latest_.input.spaceGeneration==value.input.spaceGeneration&&
           (latest_.inputObservedNs!=value.inputObservedNs||latest_.inputDeadlineNs!=value.inputDeadlineNs))reason=NavigationObservationReason::SourceClockChanged;
        if(latest_.input.menu.sequence&&latest_.input.menu.sequence==value.input.menu.sequence&&
           latest_.input.menu.epoch==value.input.menu.epoch&&latest_.input.menu.deadlineNs!=value.input.menu.deadlineNs)reason=NavigationObservationReason::MenuClockChanged;
        if(reason==NavigationObservationReason::Available){value.reason=reason;latest_=value;player_.store(value.owner.player,std::memory_order_release);}
        gate_.clear(std::memory_order_release);
        if(reason!=NavigationObservationReason::Available){Invalidate(reason);return false;}
        return Enabled()&&serial_.load(std::memory_order_acquire)==serial;
    }
    NavigationObservation Read(std::int64_t now)const noexcept {
        NavigationObservation value;
        if(!Enabled()){value.reason=reason_.load(std::memory_order_acquire);return value;}
        const auto serial=serial_.load(std::memory_order_acquire);
        if(gate_.test_and_set(std::memory_order_acquire)){value.reason=NavigationObservationReason::Busy;return value;}
        value=latest_;gate_.clear(std::memory_order_release);
        if(value.serial!=serial||serial!=serial_.load(std::memory_order_acquire)||!Enabled())
            value.reason=reason_.load(std::memory_order_acquire);
        else value.reason=NavigationObservationRejection(value,now);
        return value;
    }
    NavigationObservationReason Current(const NavigationObservation& value,std::int64_t now)const noexcept {
        if(!value.Available())return value.reason;
        if(!Enabled())return reason_.load(std::memory_order_acquire);
        if(value.serial!=serial_.load(std::memory_order_acquire))return NavigationObservationReason::Changed;
        return NavigationObservationRejection(value,now);
    }
private:
    friend struct NavigationObservationTestAccess;
    std::atomic<bool> enabled_{false};
    std::atomic<std::uint64_t> serial_{0};
    std::atomic<NavigationObservationReason> reason_{NavigationObservationReason::Disabled};
    std::atomic<std::uint32_t> player_{0};
    mutable std::atomic_flag gate_=ATOMIC_FLAG_INIT;
    NavigationObservation latest_{};
};
inline void WriteNavigationObservation(std::ostream& out,const NavigationObservation& v){
    const auto flag=[&](bool b){out<<(b?"true":"false");};
    out<<"{\"observationOnly\":true,\"available\":";flag(v.Available());out<<",\"reason\":";
    if(v.Available())out<<"null";else out<<'"'<<NavigationObservationReasonName(v.reason)<<'"';
    out<<",\"gatherSequence\":"<<v.gatherSequence<<",\"sampledNs\":"<<v.sampledNs
        <<",\"inputObservedNs\":"<<v.inputObservedNs<<",\"inputDeadlineNs\":"<<v.inputDeadlineNs<<",\"validUntilNs\":"<<v.validUntilNs;
    const auto& o=v.owner;
    out<<",\"owner\":{\"manager\":"<<o.manager<<",\"player\":"<<o.player<<",\"soldier\":"<<o.soldier<<",\"weak\":"<<o.weak
        <<",\"controlled\":"<<o.controlled<<",\"entry\":"<<o.entry<<",\"router\":"<<o.router<<",\"cache\":"<<o.cache
        <<",\"actorGeneration\":"<<o.actorGeneration<<",\"inputEpoch\":"<<o.inputEpoch<<",\"movementEpoch\":"<<o.movementEpoch<<'}';
    out<<",\"onFoot\":";flag(v.onFoot);out<<",\"nativePlaying\":";flag(v.nativePlaying);
    out<<",\"allowedRead\":";flag(v.allowedRead);out<<",\"allowedMask\":"<<v.allowedMask<<",\"input\":";
    if(!v.Available()){out<<"null}";return;} // Invalid raw poses need not be finite JSON numbers.
    const auto pose=[&](const math::Pose& p){out<<"{\"position\":["<<p.position.x<<','<<p.position.y<<','<<p.position.z
        <<"],\"orientation\":["<<p.orientation.x<<','<<p.orientation.y<<','<<p.orientation.z<<','<<p.orientation.w<<"]}";};
    const auto& input=v.input;
    out<<"{\"generation\":"<<input.generation<<",\"spaceGeneration\":"<<input.spaceGeneration<<",\"predictedNs\":"<<input.predictedNs<<",\"focused\":";flag(input.focused);
    out<<",\"headValid\":";flag(input.headValid);out<<",\"floorRelative\":";flag(input.floorRelative);
    out<<",\"worldUnitsPerMeter\":"<<input.worldUnitsPerMeter<<",\"trackingBasisGeneration\":"<<input.trackingBasisGeneration<<",\"referenceHead\":";pose(input.referenceHead);
    out<<",\"head\":";pose(input.head);out<<",\"hands\":[";
    for(unsigned i=0;i<2;++i){if(i)out<<',';const auto& h=input.hands[i];out<<"{\"grip\":";pose(h.grip);out<<",\"aim\":";pose(h.aim);
        out<<",\"gripTracked\":";flag(h.gripTracked);out<<",\"aimTracked\":";flag(h.aimTracked);
        out<<",\"active\":"<<h.active<<",\"held\":"<<h.held<<",\"touchActive\":"<<h.touchActive<<",\"touched\":"<<h.touched
            <<",\"stickX\":"<<h.stickX<<",\"stickY\":"<<h.stickY<<",\"trigger\":"<<h.trigger<<",\"squeeze\":"<<h.squeeze<<'}';}
    out<<"],\"menu\":{\"owner\":"<<unsigned(input.menu.owner)<<",\"sequence\":"<<input.menu.sequence<<",\"epoch\":"<<input.menu.epoch<<",\"deadlineNs\":"<<input.menu.deadlineNs<<"}}}";
}
inline std::string NavigationObservationJson(const NavigationObservation& value){
    std::ostringstream out;out.imbue(std::locale::classic());out.precision(9);WriteNavigationObservation(out,value);return out.str();
}
// Final worker bookend uses the same copy, never a newer packet. Even the last
// payload fallback retains independent navigation and leaves firearm invalid.
template<class Current>
std::string CompleteNavigationSnapshot(std::string payload,const std::string& section,NavigationObservation value,
    Current current,std::uint64_t nonce,std::int64_t observed,const char* invalidReason,std::size_t limit){
    const auto reason=current(value);
    if(reason!=value.reason){value.reason=reason;const auto at=payload.find(section);
        if(at!=std::string::npos)payload.replace(at,section.size(),",\"navigationObservation\":"+NavigationObservationJson(value));}
    if(payload.size()<=limit)return payload;
    std::ostringstream fallback;fallback.imbue(std::locale::classic());
    fallback<<"{\"schema\":\"fvr.bc2.live-mechanism-snapshot.v1\",\"nonce\":\""<<nonce
        <<"\",\"valid\":false,\"invalid_reason\":\""<<invalidReason<<"\",\"observed_ns\":"<<observed
        <<",\"navigationObservation\":"<<NavigationObservationJson(value)<<",\"diagnosticsOmitted\":\"payload_limit\"}\n";
    return fallback.str();
}
}
