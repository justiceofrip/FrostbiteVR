#pragma once
#include "fvr/graphics/SceneDrawWitness.h"
#include <array>
#include <cstdint>

namespace fvr::debug {
// Private observation only. Offsets are relative to the current IA binding,
// buffer-SRV view, or indirect argument block, never an inferred instance index.
enum class PayloadSource : unsigned {Vertex,Index,ShaderResource,Indirect};
enum class PayloadStage : unsigned {Vertex,Pixel,Geometry,Hull,Domain};
struct PayloadRange {
    PayloadSource source=PayloadSource::Vertex;
    PayloadStage stage=PayloadStage::Vertex;
    unsigned slot=0,offset=0,bytes=0;
};
inline constexpr unsigned PayloadRangeLimit=8,PayloadRangeBytes=64*1024;
inline constexpr unsigned PayloadEyeBytes=PayloadRangeLimit*PayloadRangeBytes;
inline constexpr unsigned PayloadPairBytes=2*PayloadEyeBytes;
struct PayloadSelection {
    std::array<unsigned,2> ordinal{};
    std::array<fvr::graphics::SceneWitnessDrawArguments,2> expected{};
    std::array<PayloadRange,PayloadRangeLimit> ranges{};
    unsigned count=0;
};
inline constexpr bool PayloadDrawShape(const fvr::graphics::SceneWitnessDrawArguments& a,
    const fvr::graphics::SceneWitnessDrawArguments& b)noexcept {
    // The actual argument-buffer identity is recorded at capture time. It may
    // differ between eyes; pointer equality is not evidence of equal contents.
    return a.kind==b.kind&&a.count==b.count&&a.instances==b.instances&&a.start==b.start&&
        a.firstInstance==b.firstInstance&&a.base==b.base&&a.argumentOffset==b.argumentOffset;
}
inline constexpr unsigned PayloadIndirectBytes(fvr::graphics::SceneWitnessDrawKind kind)noexcept {
    using K=fvr::graphics::SceneWitnessDrawKind;
    return kind==K::IndexedIndirect?20u:kind==K::Indirect?16u:0u;
}
inline constexpr bool PayloadPlanValid(const PayloadSelection& p)noexcept {
    if(!p.count||p.count>PayloadRangeLimit)return false;
    for(unsigned e=0;e<2;++e)
        if(p.ordinal[e]>fvr::graphics::SceneWitnessMaxFirstOrdinal||
            unsigned(p.expected[e].kind)>unsigned(fvr::graphics::SceneWitnessDrawKind::Indirect))return false;
    for(unsigned n=0;n<p.count;++n){const auto& r=p.ranges[n];
        if(unsigned(r.source)>unsigned(PayloadSource::Indirect)||unsigned(r.stage)>unsigned(PayloadStage::Domain)||
            !r.bytes||r.bytes>PayloadRangeBytes||r.offset%4||r.bytes%4)return false;
        if(r.source==PayloadSource::Vertex&&r.slot>=32)return false;
        if(r.source==PayloadSource::ShaderResource&&r.slot>=128)return false;
        if(r.source!=PayloadSource::ShaderResource&&r.stage!=PayloadStage::Vertex)return false;
        if((r.source==PayloadSource::Index||r.source==PayloadSource::Indirect)&&r.slot)return false;
        if(r.source==PayloadSource::Indirect&&(r.offset||
            r.bytes!=PayloadIndirectBytes(p.expected[0].kind)||r.bytes!=PayloadIndirectBytes(p.expected[1].kind)))return false;
        for(unsigned j=0;j<n;++j){const auto& a=p.ranges[j];
            if(a.source==r.source&&a.stage==r.stage&&a.slot==r.slot)return false;}
    }return true;
}
inline constexpr bool PayloadRangeFits(std::uint64_t total,std::uint64_t base,
    std::uint64_t available,unsigned relative,unsigned bytes)noexcept {
    return bytes&&bytes<=PayloadRangeBytes&&base<=total&&available<=total-base&&
        relative<=available&&bytes<=available-relative&&(base+relative)%4==0&&bytes%4==0;
}
inline constexpr bool PayloadKeyValid(const fvr::graphics::SceneWitnessKey& k)noexcept {
    return fvr::graphics::SceneWitnessValid(k)&&k.nativeOwner&&k.nativeRequest&&k.predictedNs>0;
}
}
