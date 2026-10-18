#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace fvr::interaction {
// Presentation only. A native, applied one-shell receipt supplies request;
// contact, insertion motion, submission and animation never create this event.
struct ShellReloadAudioIdentity {
    std::uint64_t actor=0,actorGeneration=0,item=0,itemGeneration=0,cycle=0;
    bool operator==(const ShellReloadAudioIdentity&)const=default;
};
inline bool ShellReloadAudioIdentityValid(const ShellReloadAudioIdentity& i)noexcept {
    return i.actor&&i.actorGeneration&&i.item&&i.itemGeneration&&i.cycle;
}
struct ShellReloadAudioEvent {
    ShellReloadAudioIdentity identity{};
    std::uint64_t request=0,sequence=0,nativeEvent=0;
    std::int64_t observedNs=0,deadlineNs=0;
    unsigned units=0;
    bool applied=false,focused=false,tracked=false;
};
enum class ShellReloadAudioAdmission { Accepted,Invalid,Stale,Duplicate,Capacity };
template<std::size_t Capacity=32> class ShellReloadAudioLedger {
    struct Row {ShellReloadAudioIdentity identity{};std::uint64_t request=0,sequence=0,nativeEvent=0;std::int64_t observedNs=0;};
    std::array<Row,Capacity> rows_{};
public:
    ShellReloadAudioAdmission Accept(const ShellReloadAudioEvent& e,std::int64_t now)noexcept {
        const auto& i=e.identity;
        if(!ShellReloadAudioIdentityValid(i)||!e.request||!e.sequence||!e.nativeEvent||e.units!=1||
           !e.applied||!e.focused||!e.tracked)return ShellReloadAudioAdmission::Invalid;
        if(e.observedNs<=0||e.observedNs>now||e.deadlineNs<=now||e.deadlineNs-e.observedNs>200000000)
            return ShellReloadAudioAdmission::Stale;
        Row* row=nullptr;Row* empty=nullptr;
        for(auto& r:rows_){if(!r.identity.actor&&!empty)empty=&r;
            if(r.identity.actor==i.actor&&r.identity.actorGeneration==i.actorGeneration&&r.identity.item==i.item){row=&r;break;}}
        if(!row){if(!empty)return ShellReloadAudioAdmission::Capacity;row=empty;}
        const auto& old=row->identity;
        if(old.actor){
            const bool newer=i.itemGeneration>old.itemGeneration;
            if(i.itemGeneration<old.itemGeneration||(!newer&&(i.cycle<old.cycle||
               (i.cycle==old.cycle&&e.request<row->request)))||e.sequence<row->sequence||e.observedNs<row->observedNs)
                return ShellReloadAudioAdmission::Stale;
            if(i==old&&e.request==row->request)return ShellReloadAudioAdmission::Duplicate;
            if(!newer&&e.nativeEvent<=row->nativeEvent)return ShellReloadAudioAdmission::Stale;
        }
        *row={i,e.request,e.sequence,e.nativeEvent,e.observedNs};return ShellReloadAudioAdmission::Accepted;
    }
    void RetireActor(std::uint64_t actor,std::uint64_t generation)noexcept {
        for(auto& r:rows_)if(r.identity.actor==actor&&r.identity.actorGeneration==generation)r={};
    }
};
// An insertion cue may replace stock only when this exact reload cycle's
// start was suppressed. Passing any start through vetoes replacement for that
// cycle. This state is independent from pump/bolt playback and their latch.
template<std::size_t Capacity=32> class ShellReloadAudioSuppression {
    struct Row {ShellReloadAudioIdentity identity{};bool passed=false,suppressed=false;};
    std::array<Row,Capacity> rows_{};
public:
    bool Start(const ShellReloadAudioIdentity& i,bool replacementReady)noexcept {
        if(!ShellReloadAudioIdentityValid(i))return false;
        Row* row=nullptr;Row* empty=nullptr;
        for(auto& r:rows_){if(!r.identity.actor&&!empty)empty=&r;
            if(r.identity.actor==i.actor&&r.identity.actorGeneration==i.actorGeneration&&r.identity.item==i.item){row=&r;break;}}
        if(!row){if(!empty)return false;row=empty;}
        if(row->identity.actor&&i!=row->identity){
            if(i.itemGeneration<row->identity.itemGeneration||
               (i.itemGeneration==row->identity.itemGeneration&&i.cycle<row->identity.cycle))return false;
            *row={};
        }
        row->identity=i;
        if(!replacementReady)row->passed=true;
        const bool suppress=replacementReady&&!row->passed;
        row->suppressed|=suppress;return suppress;
    }
    bool Allows(const ShellReloadAudioIdentity& i)const noexcept {
        for(const auto& r:rows_)if(r.identity==i)return r.suppressed&&!r.passed;
        return false;
    }
    void RetireActor(std::uint64_t actor,std::uint64_t generation)noexcept {
        for(auto& r:rows_)if(r.identity.actor==actor&&r.identity.actorGeneration==generation)r={};
    }
};
}
