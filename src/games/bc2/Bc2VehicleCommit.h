#pragma once
#include "Bc2VehicleInput.h"
#include <bit>
#include <cmath>
#include <cstring>
namespace fvr::bc2 {
// Scope has a caller-owned, already-verified native gather buffer. A successful
// commit deliberately survives this scope because native consumers run later.
// The next original gather rebuilds its fields, including after input loss.
class VehicleInputOverride {
public:
    VehicleInputOverride()=default;VehicleInputOverride(const VehicleInputOverride&)=delete;
    VehicleInputOverride& operator=(const VehicleInputOverride&)=delete;
    ~VehicleInputOverride(){Restore();}
    bool Apply(std::span<std::byte> cache,const VehicleInputPlan& plan)noexcept {
        if(active_||cache.size()<InputBytes||plan.status!=VehicleInputPlanStatus::Ready||plan.count>plan.edits.size())return false;
        std::uint32_t seen=0;
        for(unsigned n=0;n<plan.count;++n){const auto& e=plan.edits[n];unsigned key=0;
            if(e.offset==0x98){key=11;if((e.before^e.after)&~(1u<<16))return false;}
            else {if(e.offset<8||e.offset>48||(e.offset-8)%4)return false;key=unsigned(e.offset-8)/4;
                if(!(0x773u&(1u<<key))||!std::isfinite(std::bit_cast<float>(e.before))||!std::isfinite(std::bit_cast<float>(e.after))||
                    std::abs(std::bit_cast<float>(e.after))>1||(key==8&&std::bit_cast<float>(e.after)<0))return false;
                if(!(Word(cache,0x98)&(1u<<key)))return false;}
            if((seen&(1u<<key))||Word(cache,e.offset)!=e.before)return false;seen|=1u<<key;
        }
        cache_=cache;plan_=plan;for(unsigned n=0;n<plan.count;++n)Put(cache,plan.edits[n].offset,plan.edits[n].after);active_=true;return true;
    }
    void Commit()noexcept{active_=false;cache_={};}
    bool Restore()noexcept {
        if(!active_)return true;bool exact=true;
        for(unsigned n=0;n<plan_.count;++n){const auto& e=plan_.edits[n];const auto current=Word(cache_,e.offset);
            if(e.offset==0x98){const auto unchanged=~(current^e.after)&(1u<<16);if(!unchanged)exact=false;Put(cache_,e.offset,(current&~unchanged)|(e.before&unchanged));}
            else if(current==e.after)Put(cache_,e.offset,e.before);else exact=false;}
        active_=false;cache_={};return exact;
    }
private:
    static std::uint32_t Word(std::span<std::byte> c,std::size_t at)noexcept{std::uint32_t v;std::memcpy(&v,c.data()+at,4);return v;}
    static void Put(std::span<std::byte> c,std::size_t at,std::uint32_t v)noexcept{std::memcpy(c.data()+at,&v,4);}
    std::span<std::byte> cache_{};VehicleInputPlan plan_{};bool active_=false;
};
}
