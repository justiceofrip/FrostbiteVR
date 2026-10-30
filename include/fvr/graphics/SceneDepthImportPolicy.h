#pragma once
#include "fvr/graphics/SceneDepthFrame.h"

namespace fvr::graphics {
// Source imports have up to three identities, but there is only one local depth target
// quartet. Caller must serialize this with the sole host color-copy lease.
class SceneDepthImportPolicy {
public:
    static constexpr unsigned Capacity=3;
    struct Selection {SceneDepthFrame frame{};TextureDescriptor descriptor{};unsigned slot=0,count=1;
        std::uint64_t revision=0;bool resetImports=false,openSources=false,createLocal=false;};
    bool Select(const SceneDepthFrame& frame,const TextureDescriptor& descriptor,const PairTicket& ticket,
        unsigned slot,unsigned count,Selection& out)const noexcept {
        out={};if(count<1||count>Capacity||slot>=count||!SceneDepthMatches(frame,descriptor,ticket)||
            (count>1&&(frame.samples!=1||frame.quality)))return false;
        const bool reset=count_!=count;const auto& old=opened_[slot];
        if(!reset&&valid_[slot]&&old.color.session==ticket.session&&old.color.resourceEpoch==ticket.resourceEpoch&&
            ticket.sequence<=old.color.sequence)return false;
        if(count>1&&!reset&&valid_[slot]&&old.color.session==ticket.session&&old.color.resourceEpoch==ticket.resourceEpoch&&
            (frame.resourceGeneration<old.resourceGeneration||
             (frame.resourceGeneration==old.resourceGeneration&&frame.resources!=old.resources)))return false;
        if(count>1&&!reset)for(unsigned n=0;n<count;++n){
            if(n==slot||!valid_[n])continue;
            const auto& other=opened_[n];if(!SameShape(frame,other))return false;
            const auto otherColors=LegacyIds(other.color.session);const auto colors=LegacyResourceIds(descriptor);
            for(const auto& eye:frame.resources)for(auto id:eye){
                for(const auto& otherEye:other.resources)for(auto oldId:otherEye)if(id==oldId)return false;
                for(auto color:otherColors)if(id==color)return false;
            }
            for(auto color:colors)for(const auto& eye:other.resources)for(auto id:eye)if(color==id)return false;
        }
        const bool open=reset||!valid_[slot]||!SameSources(frame,old);
        out.frame=frame;out.descriptor=descriptor;out.slot=slot;out.count=count;out.revision=revision_;
        out.resetImports=reset;out.openSources=open;
        // Shared source identities have a shorter lifetime than the private
        // copy targets. Reopen changed sources without reallocating an equal
        // shape, including the single-source MSAA transport.
        out.createLocal=!localValid_||!SameShape(frame,local_);
        return true;
    }
    bool Commit(const Selection& s)noexcept {
        Selection current;if(s.revision!=revision_||!Select(s.frame,s.descriptor,s.frame.color,s.slot,s.count,current)||
            s.resetImports!=current.resetImports||s.openSources!=current.openSources||s.createLocal!=current.createLocal)return false;
        if(s.resetImports){opened_={};valid_={};}
        count_=s.count;opened_[s.slot]=s.frame;valid_[s.slot]=true;local_=s.frame;localValid_=true;++revision_;return true;
    }
    void ResetSources()noexcept {opened_={};valid_={};count_=0;++revision_;}
    void Reset()noexcept {ResetSources();local_={};localValid_=false;}
private:
    std::array<SceneDepthFrame,Capacity> opened_{};std::array<bool,Capacity> valid_{};
    SceneDepthFrame local_{};bool localValid_=false;unsigned count_=0;std::uint64_t revision_=0;
    static auto LegacyIds(const std::array<std::uint8_t,16>& session)noexcept {
        std::array<std::uint64_t,2> ids{};std::memcpy(ids.data(),session.data(),16);return ids;
    }
    static bool SameShape(const SceneDepthFrame& a,const SceneDepthFrame& b)noexcept {
        return a.width==b.width&&a.height==b.height&&a.samples==b.samples&&a.quality==b.quality&&
            a.format==b.format&&a.adapterLow==b.adapterLow&&a.adapterHigh==b.adapterHigh;
    }
    static bool SameSources(const SceneDepthFrame& a,const SceneDepthFrame& b)noexcept {
        return SameShape(a,b)&&a.version==b.version&&a.bytes==b.bytes&&a.flags==b.flags&&a.reserved==b.reserved&&
            a.reserved1==b.reserved1&&a.resources==b.resources&&a.resourceGeneration==b.resourceGeneration&&
            a.color.session==b.color.session&&a.color.resourceEpoch==b.color.resourceEpoch;
    }
};
}
