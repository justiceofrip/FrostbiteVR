#include "Bc2ContextualToolInput.h"
#include <bit>
#include <cmath>
#include <cstring>
namespace fvr::bc2 {namespace {
constexpr std::array<std::size_t,4> Offsets{8+4*7,8+4*8,0x98,0x9c};
// Retain locomotion/menu/context actions. Own all weapon selection/attack bits,
// including StaticGadget34 and DynamicGadget1=35, rather than leak native input.
constexpr std::array<std::uint32_t,4> Masks{~0u,~0u,(1u<<12)|(1u<<14)|(1u<<29),0x7eu};
unsigned Word(std::span<std::byte> b,std::size_t at)noexcept{unsigned x;std::memcpy(&x,b.data()+at,4);return x;}
void Put(std::span<std::byte> b,std::size_t at,unsigned x)noexcept{std::memcpy(b.data()+at,&x,4);}
bool Valid(const ToolInputRequest& r)noexcept {
    using namespace interaction;const auto& s=r.scope;const auto& i=s.input;const auto& o=s.owner;
    if(!r.operation||!r.serial||!s.request||!s.nativeTick||s.cache<0x10000||o.player<0x10000||o.soldier<0x10000||o.weak<0x10000||
       !o.actorGeneration||!o.equipGeneration||!o.space||r.from.weapon!=o.weapon||!r.from.data||!r.target.weapon||!r.target.data||
       i.owner.actor!=((std::uint64_t(o.weak)<<32)|o.soldier)||i.owner.actorGeneration!=o.actorGeneration||i.owner.space!=o.space||
       !i.owner.equipGeneration||!i.sequence||i.observedNs<=0||i.observedNs>i.nowNs||i.deadlineNs<=i.nowNs||
       i.deadlineNs-i.observedNs>100000000||!i.focused||!i.tracked[0]||!i.tracked[1])return false;
    if(r.kind==ToolInputKind::Equip)return !r.zoom&&!r.fire&&r.from.slot<2&&BodyLongGunCategory(r.from.category)&&
        r.target.slot==4&&r.target.category==12&&r.custody.id&&r.custody.kind==HandClaimKind::BodyInventory&&
        r.custody.hand==InteractionHand::Right&&r.custody.owner==i.owner&&r.custody.item.id==r.target.weapon&&r.custody.item.generation;
    if(r.kind==ToolInputKind::Return)return !r.zoom&&!r.fire&&r.from.slot==4&&r.from.category==12&&
        r.target.slot<2&&BodyLongGunCategory(r.target.category)&&!r.custody.id;
    return r.kind==ToolInputKind::Controls&&r.from==r.target&&r.from.slot==4&&r.from.category==12&&
        r.custody.id&&r.custody.kind==HandClaimKind::GunHold&&r.custody.hand==InteractionHand::Right&&
        r.custody.owner==i.owner&&r.custody.item.id==r.target.weapon&&r.custody.item.generation;
}
}
bool ContextualToolInputOverride::Apply(std::span<std::byte> cache,const ToolInputRequest& r,ToolInputValidation v)noexcept {
    if(active_||cache.size()!=InputBytes||!Valid(r)||!v.current||!v.current(v.context,r))return false;
    for(unsigned n=0;n<4;++n){before_[n]=Word(cache,Offsets[n]);if(n<2&&!std::isfinite(std::bit_cast<float>(before_[n])))return false;
        written_[n]=before_[n]&~Masks[n];}
    if(r.kind==ToolInputKind::Equip)written_[3]|=1u<<(34-32);
    else if(r.kind==ToolInputKind::Return)written_[0]=std::bit_cast<unsigned>(1.f);
    else {if(r.zoom)written_[2]|=1u<<14;written_[1]=std::bit_cast<unsigned>(r.fire?1.f:0.f);}
    cache_=cache;request_=r;validation_=v;active_=true;
    for(unsigned n=0;n<4;++n)Put(cache_,Offsets[n],written_[n]);
    if(!validation_.current(validation_.context,request_)){Restore();return false;}return true;
}
std::optional<ToolInputReceipt> ContextualToolInputOverride::Commit()noexcept {
    if(!active_||!validation_.current(validation_.context,request_)){Restore();return {};}
    for(unsigned n=0;n<4;++n)if((Word(cache_,Offsets[n])&Masks[n])!=(written_[n]&Masks[n])){Restore();return {};}
    ToolInputReceipt out{request_};active_=false;cache_={};return out;
}
bool ContextualToolInputOverride::Restore()noexcept {
    if(!active_)return true;bool exact=true;
    for(unsigned n=0;n<4;++n){const auto current=Word(cache_,Offsets[n]);
        if(n<2){if(current==written_[n])Put(cache_,Offsets[n],before_[n]);else exact=false;}
        else {const auto unchanged=~(current^written_[n])&Masks[n];if(unchanged!=Masks[n])exact=false;
            Put(cache_,Offsets[n],(current&~unchanged)|(before_[n]&unchanged));}}
    active_=false;cache_={};return exact;
}
}
