#pragma once
#include <cstdint>
#include <optional>
namespace fvr::bc2 {
// Native ClientEntryComponent camera state is separate from the +d8 entry
// variant selector. Verify the installed consumer bytes before calling this
// read-only join. Word must retain failed-read state for its caller to reject.
struct ActiveEntryCamera {
    unsigned entryTable=0,begin=0,end=0,state=0,node=0,selected=0;
    unsigned camera=0,wrapper=0,wrapperTable=0,component=0;
    bool operator==(const ActiveEntryCamera&)const=default;
};
template<class Word>
std::optional<ActiveEntryCamera> ReadActiveEntryCamera(unsigned entry,unsigned image,Word word)noexcept {
    bool bounded=true;
    const auto read=[&](std::uint64_t at){
        if(at<0x10000||at>UINT32_MAX-4){bounded=false;return 0u;}
        return word(static_cast<unsigned>(at));
    };
    if(!entry||!image||image>UINT32_MAX-0x1800000)return {};
    ActiveEntryCamera s;s.entryTable=read(entry);
    // Both player and soldier entries inherit these actual camera consumers.
    if(!s.entryTable||read(std::uint64_t(s.entryTable)+0x154)!=image+0x3d3e60||
       read(std::uint64_t(s.entryTable)+0x170)!=image+0x3ce480)return {};
    s.begin=read(std::uint64_t(entry)+0x19c);s.end=read(std::uint64_t(entry)+0x1a0);
    s.state=read(std::uint64_t(entry)+0x1ac);
    if(!s.begin||s.end<s.begin||(s.end-s.begin)%12||(s.end-s.begin)/12>32||!s.state)return {};
    s.node=read(std::uint64_t(s.state)+0x20);
    if(!s.node)return {};
    // 0x79d9a0: the embedded sentinel selects zero; otherwise node+0x10.
    s.selected=std::uint64_t(s.node)==std::uint64_t(s.state)+8?0:read(std::uint64_t(s.node)+0x10);
    if(s.selected>=(s.end-s.begin)/12)return {};
    const auto row=std::uint64_t(s.begin)+s.selected*12;
    s.camera=read(row);s.wrapper=read(row+4);
    // 0x7c6570 installs this exact provider on the selected camera. A stale
    // row or a native camera transition must not grant current ownership.
    if(!s.camera||!s.wrapper||read(std::uint64_t(s.camera)+0x80)!=s.wrapper)return {};
    s.wrapperTable=read(s.wrapper);s.component=read(std::uint64_t(s.wrapper)+4);
    if(!s.wrapperTable||!s.component||read(std::uint64_t(s.wrapperTable)+4)!=image+0x3df4e0||
       read(std::uint64_t(s.wrapperTable)+8)!=image+0x3c4cb0||!bounded)return {};
    return s;
}
}
