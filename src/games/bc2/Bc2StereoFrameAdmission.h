#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace fvr::bc2 {
struct StereoFrameAdmissionBinding {
    std::uint32_t gameRendererGlobal=0,subsystemType=0,worldType=0,requestType=0,viewType=0;
};
struct StereoFrameOwner {
    std::uint32_t world=0,request=0,main=0,eye=0;
};
enum class StereoFrameAdmission {
    Ready,InvalidExpected,Unreadable,CurrentOwnerChanged,NativeTypeChanged,
    RequestPhaseChanged,RootsChanged,RegistryChanged,ViewOwnerChanged,SnapshotChanged,Count
};
inline constexpr std::array<const char*,static_cast<unsigned>(StereoFrameAdmission::Count)> StereoFrameAdmissionNames{
    "ready","invalid_expected","unreadable","current_owner_changed","native_type_changed",
    "request_phase_changed","roots_changed","registry_changed","view_owner_changed","snapshot_changed"
};
// Only an independently verified replacement currently blocks activation. A
// same-request main replacement has no verified native rebind yet: diagnose it
// without adding a permanent blackout to the existing runtime behavior.
inline bool BlocksStereoFrameAdmission(StereoFrameAdmission result)noexcept {
    return result==StereoFrameAdmission::CurrentOwnerChanged;
}
struct StereoFrameAdmissionSnapshot {
    std::uint32_t renderer=0,subsystem=0,currentWorld=0,currentRequest=0;
    std::array<std::uint32_t,3> rootHeader{},allHeader{};
    std::array<std::uint32_t,2> roots{};
    std::array<std::uint32_t,64> all{};
    std::uint32_t allCount=0;
    bool operator==(const StereoFrameAdmissionSnapshot&)const=default;
};

// Read-only admission at the adapter's already verified completed-work Update
// boundary. The retained private eye can keep an old request/world alive after
// the stock main is removed. Its type/backlink alone is not current ownership.
// No actor, health, input or time predicate participates: cutscenes remain valid.
// Ready and CurrentOwnerChanged publish a double-checked snapshot. The latter
// proves a different nonzero native owner, not merely a missing/loading pointer.
// Any other outcome leaves output unchanged. No result authorizes release,
// rebind, cancellation or a native write. The native boundary supplies exclusion;
// repeated memory reads alone cannot establish a universal lifetime guarantee.
template<class Read>
StereoFrameAdmission InspectStereoFrameAdmission(const StereoFrameAdmissionBinding& b,
    const StereoFrameOwner& owner,Read&& memory,StereoFrameAdmissionSnapshot& output) noexcept {
    using Result=StereoFrameAdmission;
    if(!b.gameRendererGlobal||!b.subsystemType||!b.worldType||!b.requestType||!b.viewType||
       !owner.world||!owner.request||!owner.main||!owner.eye||owner.main==owner.eye)
        return Result::InvalidExpected;
    const auto read=[&](std::uint64_t at,void* dst,std::size_t bytes)noexcept {
        return at>=0x10000&&bytes&&at<=UINT32_MAX&&bytes<=UINT32_MAX-at&&
            memory(static_cast<std::uint32_t>(at),dst,bytes);
    };
    const auto word=[&](std::uint64_t at,std::uint32_t& value)noexcept{return read(at,&value,sizeof(value));};
    const auto vectorCount=[](const std::array<std::uint32_t,3>& h,std::uint32_t limit,
                             std::uint32_t& count)noexcept {
        if(!h[0]||(h[0]&3u)||(h[1]&3u)||(h[2]&3u)||h[0]>h[1]||h[1]>h[2]||
           h[2]-h[0]>4096u*4u)return false;
        count=(h[1]-h[0])/4u;return count<=limit;
    };
    const auto capture=[&](StereoFrameAdmissionSnapshot& s)noexcept {
        std::uint32_t type=0,world=0,request=0,phase=0;
        if(!word(b.gameRendererGlobal,s.renderer)||
           !word(std::uint64_t(s.renderer)+0x108c,s.subsystem)||!word(s.subsystem,type))return Result::Unreadable;
        if(type!=b.subsystemType)return Result::NativeTypeChanged;
        if(!word(std::uint64_t(s.subsystem)+0x2c,s.currentWorld)||
           !word(std::uint64_t(s.subsystem)+0x30,s.currentRequest))return Result::Unreadable;
        if(!s.currentWorld||!s.currentRequest)return Result::Unreadable;
        // Verify the CURRENT graph without inspecting retained old objects.
        if(!word(s.currentWorld,type))return Result::Unreadable;
        if(type!=b.worldType)return Result::NativeTypeChanged;
        if(!word(s.currentRequest,type))return Result::Unreadable;
        if(type!=b.requestType)return Result::NativeTypeChanged;
        if(!word(std::uint64_t(s.currentRequest)+8,world)||
           !word(std::uint64_t(s.currentRequest)+0xc4,phase))return Result::Unreadable;
        if(world!=s.currentWorld)return Result::ViewOwnerChanged;
        if(phase!=3)return Result::RequestPhaseChanged;
        if(s.currentWorld!=owner.world||s.currentRequest!=owner.request)return Result::CurrentOwnerChanged;
        std::uint32_t rootCount=0;
        if(!read(std::uint64_t(owner.request)+0x24,s.rootHeader.data(),sizeof(s.rootHeader)))return Result::Unreadable;
        if(!vectorCount(s.rootHeader,2,rootCount)||rootCount!=2)return Result::RootsChanged;
        if(!read(s.rootHeader[0],s.roots.data(),sizeof(s.roots)))return Result::Unreadable;
        if(s.roots!=std::array<std::uint32_t,2>{owner.main,owner.eye})return Result::RootsChanged;
        if(!read(std::uint64_t(owner.request)+0x14,s.allHeader.data(),sizeof(s.allHeader)))return Result::Unreadable;
        if(!vectorCount(s.allHeader,64,s.allCount)||s.allCount<2)return Result::RegistryChanged;
        if(!read(s.allHeader[0],s.all.data(),s.allCount*sizeof(std::uint32_t)))return Result::Unreadable;
        unsigned mains=0,eyes=0;
        for(unsigned i=0;i<s.allCount;++i){
            if(!s.all[i])return Result::RegistryChanged;
            mains+=s.all[i]==owner.main;eyes+=s.all[i]==owner.eye;
            for(unsigned j=0;j<i;++j)if(s.all[i]==s.all[j])return Result::RegistryChanged;
        }
        if(mains!=1||eyes!=1)return Result::RegistryChanged;
        // Membership precedes either remembered root's dereference. ReadProcessMemory
        // and the second capture still fail closed if native ownership changes.
        for(const auto view:{owner.main,owner.eye}){
            if(!word(view,type))return Result::Unreadable;
            if(type!=b.viewType)return Result::NativeTypeChanged;
            if(!word(std::uint64_t(view)+0x70,request))return Result::Unreadable;
            if(request!=owner.request)return Result::ViewOwnerChanged;
        }
        return Result::Ready;
    };
    StereoFrameAdmissionSnapshot first{},second{};
    const auto firstResult=capture(first);
    if(firstResult!=Result::Ready&&firstResult!=Result::CurrentOwnerChanged)return firstResult;
    const auto secondResult=capture(second);
    if(secondResult!=Result::Ready&&secondResult!=Result::CurrentOwnerChanged)return secondResult;
    if(firstResult!=secondResult||first!=second)return Result::SnapshotChanged;
    output=second;return firstResult;
}
} // namespace fvr::bc2
