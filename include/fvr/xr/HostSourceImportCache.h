#pragma once
#include "fvr/runtime/FrameProvider.h"
#include <cstring>

namespace fvr::xr {
// Host-thread metadata policy. A successful Bind pins exactly one source entry
// until its containing HostCopyLease releases local presentation ownership.
// No method implies GPU completion, sends feedback, or owns a native slot.
class HostSourceImportCache {
public:
    static constexpr unsigned Capacity=3;
    struct Selection {
        runtime::SourceSlotIdentity source{};
        graphics::TextureDescriptor descriptor{};
        graphics::PairTicket ticket{};
        std::uint64_t revision=0;
        unsigned count=0,index=Capacity;
        bool resetAll=false,resetPresentation=false,open=false;
    };
    struct Statistics {std::uint64_t opens=0,hits=0,resets=0,rejected=0,poisoned=0;};
    bool Select(unsigned count,const runtime::SourceSlotIdentity& source,
        const graphics::TextureDescriptor& descriptor,const graphics::PairTicket& ticket,Selection& out)noexcept {
        out={};
        if(!Plan(count,source,descriptor,ticket,out)){++stats_.rejected;return false;}
        return true;
    }
    bool Bind(const Selection& selected)noexcept {
        Selection current;
        if(selected.revision!=revision_||!Plan(selected.count,selected.source,selected.descriptor,selected.ticket,current)||
            current.index!=selected.index||current.open!=selected.open||current.resetAll!=selected.resetAll||
            current.resetPresentation!=selected.resetPresentation)return false;
        if(selected.resetAll){entries_={};lastRequest_=0;++stats_.resets;}
        count_=selected.count;session_=selected.source.channelSession;
        auto& entry=entries_[selected.index];entry.descriptor=selected.descriptor;entry.valid=true;entry.poisoned=false;
        active_=selected.index;ticket_=selected.ticket;
        if(count_>1)lastRequest_=selected.source.requestId;
        if(selected.open)++stats_.opens;else ++stats_.hits;
        ++revision_;return true;
    }
    // Failed Open destroys the selected D3D consumer state. Remember the failed
    // generation rather than accidentally reporting a later cache hit for it.
    void OpenFailed(const Selection& selected)noexcept {
        if(Bind(selected))Release(selected.ticket,true);
    }
    bool Release(const graphics::PairTicket& ticket,bool retired)noexcept {
        if(active_>=Capacity||!Same(ticket,ticket_))return false;
        if(retired){entries_[active_].poisoned=true;++stats_.poisoned;}
        active_=Capacity;ticket_={};++revision_;return true;
    }
    unsigned ActiveEntry(const graphics::PairTicket& ticket)const noexcept {
        return active_<Capacity&&Same(ticket,ticket_)?active_:Capacity;
    }
    const Statistics& Stats()const noexcept{return stats_;}
    unsigned SourceCount()const noexcept{return count_;}
private:
    struct Entry {graphics::TextureDescriptor descriptor{};bool valid=false,poisoned=false;};
    std::array<Entry,Capacity> entries_{};
    std::array<std::uint8_t,16> session_{};
    graphics::PairTicket ticket_{};
    unsigned count_=0,active_=Capacity;
    std::uint64_t lastRequest_=0,revision_=0;
    Statistics stats_{};
    template<class T>static bool Same(const T& a,const T& b)noexcept{return std::memcmp(&a,&b,sizeof(T))==0;}
    bool Plan(unsigned count,const runtime::SourceSlotIdentity& source,const graphics::TextureDescriptor& descriptor,
        const graphics::PairTicket& ticket,Selection& out)const noexcept {
        if(active_<Capacity||count<1||count>Capacity||!graphics::Valid(ticket,descriptor))return false;
        const bool pipeline=count>1;
        bool any=false;for(auto b:source.channelSession)any|=b!=0;
        if(pipeline&&(!any||source.slotCount!=count||source.slot>=count||!source.requestId||
            descriptor.sharing!=graphics::TextureSharing::LegacyFenced))return false;
        const unsigned index=pipeline?source.slot:0;
        const bool reset=count_!=count||(pipeline&&session_!=source.channelSession);
        if(pipeline&&!reset&&source.requestId<=lastRequest_)return false;
        const auto& old=entries_[index];
        const bool same=!reset&&old.valid&&Same(old.descriptor,descriptor);
        if(pipeline&&!reset&&old.valid){
            if(old.poisoned&&same)return false;
            if(!same&&descriptor.resourceEpoch<=old.descriptor.resourceEpoch)return false;
        }
        if(pipeline&&!reset){
            const auto ids=graphics::LegacyResourceIds(descriptor);
            for(unsigned n=0;n<count;++n){const auto& other=entries_[n];
                if(n==index||!other.valid)continue;
                const auto& d=other.descriptor;
                if(d.width!=descriptor.width||d.height!=descriptor.height||d.format!=descriptor.format||
                    d.adapterLow!=descriptor.adapterLow||d.adapterHigh!=descriptor.adapterHigh)return false;
                const auto otherIds=graphics::LegacyResourceIds(d);
                for(auto id:ids)for(auto otherId:otherIds)if(id==otherId)return false;
            }
        }
        out.source=source;out.descriptor=descriptor;out.ticket=ticket;out.revision=revision_;out.count=count;out.index=index;
        out.resetAll=reset;out.open=!same||old.poisoned;
        // v8 retains its original reset-on-descriptor-change behavior. Expected
        // v9/v10 slot switches/recreations keep valid XR/HUD and missing-depth state.
        out.resetPresentation=reset||(!pipeline&&out.open);return true;
    }
};
}
