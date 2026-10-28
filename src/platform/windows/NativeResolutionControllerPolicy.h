#pragma once
#include "Bc2ResolutionRequest.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace fvr::bc2::resolutionController {
inline bool MatchingReply(const ResolutionRequest& input,const ResolutionRequest& output,bool compareDimensions=true)noexcept {
    return input.magic==output.magic&&input.version==output.version&&input.lease==output.lease&&
        (!compareDimensions||(input.width==output.width&&input.height==output.height));
}
inline bool SetupComplete(const ResolutionRequest& r)noexcept {
    return r.passed==1&&r.error==0&&r.applied==1&&r.hookRestored==1&&r.callbacksDrained==1&&
        r.guardInstalled==1&&r.guardActive==1&&r.guardDisabled==0&&r.guardDrained==0&&
        r.guardReleased==0&&r.guardNeverInstalled==0&&r.guardOwnerLost==0&&r.guardCleanupPending==0;
}
inline bool ReleaseComplete(const ResolutionRequest& r)noexcept {
    return r.passed==1&&r.error==0&&r.guardActive==0&&r.guardCleanupPending==0&&
        r.guardDisabled==1&&r.guardDrained==1&&
        ((r.guardInstalled==1&&r.guardReleased==1&&r.guardNeverInstalled==0&&r.hookRestored==1&&r.callbacksDrained==1)||
         (r.guardInstalled==0&&r.guardReleased==0&&r.guardNeverInstalled==1));
}
inline bool Unsigned(std::wstring_view text,std::uint64_t& out)noexcept {
    if(text.empty()||text.size()>20)return false;std::uint64_t value=0;
    for(auto c:text){if(c<L'0'||c>L'9')return false;const auto digit=std::uint64_t(c-L'0');
        if(value>(UINT64_MAX-digit)/10)return false;value=value*10+digit;}
    out=value;return true;
}
inline bool Hex(std::wstring_view text,std::size_t size)noexcept {
    if(text.size()!=size)return false;for(auto c:text)if(!((c>=L'0'&&c<=L'9')||(c>=L'a'&&c<=L'f')))return false;return true;
}
inline bool Token(std::wstring_view text,std::array<std::uint32_t,4>& out)noexcept {
    if(!Hex(text,32))return false;std::array<std::uint32_t,4> value{};
    for(unsigned i=0;i<32;++i){const auto c=text[i];value[i/8]=(value[i/8]<<4)|std::uint32_t(c<=L'9'?c-L'0':c-L'a'+10);}
    if(!(value[0]|value[1]|value[2]|value[3]))return false;out=value;return true;
}
struct ExecutableSpan {std::uint32_t rva=0;std::vector<std::byte> bytes;};
struct ImagePlan {
    std::uint32_t imageSize=0,timestamp=0,preferredBase=0;
    std::vector<std::byte> headers;
    std::vector<ExecutableSpan> executable;
    bool ContainsCode(std::uint32_t rva)const noexcept {
        for(const auto& s:executable)if(rva>=s.rva&&rva-s.rva<s.bytes.size())return true;return false;
    }
};
// Match the pinned file's PE headers and all executable bytes in the loaded
// module. Windows updates the mapped PE ImageBase as well as x86 HIGHLOW
// relocations: normalize both to the exact module base. Mutable data/IAT are
// deliberately excluded. Never infer loaded identity from its path.
inline std::optional<ImagePlan> Plan(std::span<const std::byte> file,std::uint32_t remoteBase){
    if(!remoteBase||file.size()<256||file.size()>64u*1024*1024)return {};
    auto fits=[&](std::uint64_t at,std::uint64_t size){return at<=file.size()&&size<=file.size()-at;};
    auto u16=[&](std::size_t at){std::uint16_t v=0;if(fits(at,2))std::memcpy(&v,file.data()+at,2);return v;};
    auto u32=[&](std::size_t at){std::uint32_t v=0;if(fits(at,4))std::memcpy(&v,file.data()+at,4);return v;};
    if(u16(0)!=0x5a4d)return {};const auto nt=u32(0x3c);
    if(!fits(nt,24)||u32(nt)!=0x4550||u16(nt+4)!=0x14c)return {};
    const auto sections=u16(nt+6),optionalSize=u16(nt+20);const auto optional=std::uint64_t(nt)+24;
    if(!sections||sections>96||optionalSize<224||!fits(optional,optionalSize)||u16(std::size_t(optional))!=0x10b)return {};
    ImagePlan plan;plan.timestamp=u32(nt+8);plan.preferredBase=u32(std::size_t(optional)+28);plan.imageSize=u32(std::size_t(optional)+56);
    const auto headerSize=u32(std::size_t(optional)+60);
    if(!plan.preferredBase||plan.imageSize<4096||plan.imageSize>64u*1024*1024||
       std::uint64_t(remoteBase)+plan.imageSize>0x100000000ull||!headerSize||headerSize>1024*1024||!fits(0,headerSize))return {};
    const auto table=optional+optionalSize;if(!fits(table,std::uint64_t(sections)*40)||table+std::uint64_t(sections)*40>headerSize)return {};
    struct Section {std::uint32_t rva=0,raw=0,size=0,mapped=0;};std::vector<Section> list;
    std::size_t codeBytes=0;
    for(unsigned i=0;i<sections;++i){const auto at=std::size_t(table+i*40);
        Section s{u32(at+12),u32(at+20),u32(at+16),u32(at+8)};s.mapped=std::max(s.mapped,s.size);
        if(!fits(s.raw,s.size)||std::uint64_t(s.rva)+s.mapped>plan.imageSize)return {};
        for(const auto& prior:list)if(s.mapped&&prior.mapped&&s.rva<std::uint64_t(prior.rva)+prior.mapped&&prior.rva<std::uint64_t(s.rva)+s.mapped)return {};
        list.push_back(s);
        if(u32(at+36)&0x20000000u){
            if(!s.size||(u32(at+36)&0x80000000u))return {};codeBytes+=s.size;if(codeBytes>16u*1024*1024)return {};
            plan.executable.push_back({s.rva,{file.begin()+s.raw,file.begin()+s.raw+s.size}});
        }
    }
    if(plan.executable.empty())return {};
    plan.headers.assign(file.begin(),file.begin()+headerSize);
    // The actual game11 mapping proved this loader-owned header update. Do not
    // ignore ImageBase: a different mapped base must still fail comparison.
    std::memcpy(plan.headers.data()+std::size_t(optional)+28,&remoteBase,sizeof(remoteBase));
    const auto directories=u32(std::size_t(optional)+92);
    if(directories<6)return {};
    const auto relocRva=u32(std::size_t(optional)+96+5*8),relocSize=u32(std::size_t(optional)+100+5*8);
    const auto delta=remoteBase-plan.preferredBase;
    if(!relocRva&&!relocSize){if(delta)return {};return plan;}
    if(!relocRva||relocSize<8||relocSize>4u*1024*1024)return {};
    std::optional<std::size_t> relocAt;
    for(const auto& s:list)if(relocRva>=s.rva&&std::uint64_t(relocRva-s.rva)+relocSize<=s.size)relocAt=std::size_t(s.raw)+relocRva-s.rva;
    if(!relocAt)return {};
    std::uint32_t used=0;
    while(used<relocSize){
        if(relocSize-used<8)return {};const auto at=*relocAt+used;const auto page=u32(at),block=u32(at+4);
        if(block<8||(block&1)||block>relocSize-used||page>=plan.imageSize)return {};
        for(unsigned item=8;item<block;item+=2){const auto entry=u16(at+item);const auto type=entry>>12;
            if(!type)continue;if(type!=3)return {};const auto offset=std::uint32_t(entry&4095);
            if(std::uint64_t(page)+offset+4>plan.imageSize)return {};const auto location=page+offset;
            for(auto& code:plan.executable)if(location>=code.rva&&location-code.rva<code.bytes.size()){
                const auto index=location-code.rva;if(code.bytes.size()-index<4)return {};std::uint32_t value=0;
                std::memcpy(&value,code.bytes.data()+index,4);value+=delta;std::memcpy(code.bytes.data()+index,&value,4);
            }
        }used+=block;
    }
    return plan;
}
}
