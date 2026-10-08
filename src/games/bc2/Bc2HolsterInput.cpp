#include "Bc2HolsterInput.h"
#include <bit>
#include <cmath>
#include <cstring>
namespace fvr::bc2 {
namespace {
constexpr std::array<std::size_t,4> Offsets{8+4*7,8+4*8,0x98,0x9c};
// EntryInputActionEnum: alternate fire12, ADS14, reload29; launcher33,
// dynamic gadget36, melee37 and grenade38. Ordinary Fire8/cycle7 are floats.
constexpr std::array<std::uint32_t,4> Masks{~0u,~0u,(1u<<12)|(1u<<14)|(1u<<29),(1u<<1)|(1u<<4)|(1u<<5)|(1u<<6)};
std::uint32_t Word(std::span<std::byte> b,std::size_t n)noexcept{std::uint32_t x;std::memcpy(&x,b.data()+n,4);return x;}
void Put(std::span<std::byte> b,std::size_t n,std::uint32_t x)noexcept{std::memcpy(b.data()+n,&x,4);}
bool Valid(const HolsterSuppressionRequest& r,bool requireFresh=true)noexcept {
    const auto& i=r.input;const auto& o=r.owner;
    return r.request&&r.nativeTick&&r.cache>=0x10000&&o.player>=0x10000&&o.soldier>=0x10000&&o.weak>=0x10000&&o.weapon>=0x10000&&
        o.actorGeneration&&o.equipGeneration&&o.space&&i.owner.actor==((std::uint64_t(o.weak)<<32)|o.soldier)&&
        i.owner.actorGeneration==o.actorGeneration&&i.owner.equipGeneration&&i.owner.space==o.space&&
        i.sequence&&i.observedNs>0&&i.observedNs<=i.nowNs&&i.deadlineNs>i.observedNs&&i.deadlineNs-i.observedNs<=150000000&&
        (!requireFresh||(i.deadlineNs>i.nowNs&&i.focused&&i.tracked[1]));
}
}
bool HolsterSuppressionCurrent(const HolsterSuppressionReceipt& a,const HolsterSuppressionRequest& b)noexcept {
    return !a.equipmentDispatched&&Valid(a)&&Valid(b)&&a.request==b.request&&a.owner==b.owner&&a.nativeTick==b.nativeTick&&a.cache==b.cache&&
        a.input.owner==b.input.owner&&a.input.sequence==b.input.sequence&&a.input.observedNs==b.input.observedNs&&
        a.input.deadlineNs==b.input.deadlineNs&&a.input.focused==b.input.focused&&a.input.tracked==b.input.tracked;
}
bool HolsterInputOverride::Apply(std::span<std::byte> cache,const HolsterSuppressionRequest& r,HolsterInputOwner owner,std::optional<EntryAction> equipment)noexcept {
    // Cancellation recovery may keep suppressing the genuine current native
    // gather even after XR expires. Its expired original packet cannot grant
    // EmptyHands/free pose: HolsterSuppressionCurrent still requires freshness.
    if(active_||cache.size()!=InputBytes||!Valid(r,false)||!owner.current||!owner.current(owner.context,r)||
        (equipment&&*equipment!=EntryAction::SwitchPrimaryWeapon&&*equipment!=EntryAction::GrenadeLauncher&&*equipment!=EntryAction::DynamicGadget2))return false;
    for(unsigned n=0;n<4;++n){before_[n]=Word(cache,Offsets[n]);if(n<2&&!std::isfinite(std::bit_cast<float>(before_[n])))return false;written_[n]=before_[n]&~Masks[n];}
    equipment_=equipment.has_value();if(equipment){if(*equipment==EntryAction::SwitchPrimaryWeapon)written_[0]=std::bit_cast<std::uint32_t>(1.f);
        else written_[3]|=1u<<(unsigned(*equipment)-32);}
    cache_=cache;request_=r;owner_=owner;active_=true;
    for(unsigned n=0;n<4;++n)Put(cache_,Offsets[n],written_[n]);
    if(!owner_.current(owner_.context,request_)){Restore();return false;}return true;
}
std::optional<HolsterSuppressionReceipt> HolsterInputOverride::Commit()noexcept {
    if(!active_||!owner_.current(owner_.context,request_)){Restore();return {};}
    for(unsigned n=0;n<4;++n)if((Word(cache_,Offsets[n])&Masks[n])!=(written_[n]&Masks[n])){Restore();return {};}
    HolsterSuppressionReceipt out;static_cast<HolsterSuppressionRequest&>(out)=request_;out.equipmentDispatched=equipment_;
    active_=false;cache_={};return out;
}
bool HolsterInputOverride::Restore()noexcept {
    if(!active_)return true;bool exact=true;
    for(unsigned n=0;n<4;++n){const auto current=Word(cache_,Offsets[n]);
        if(n<2){if(current==written_[n])Put(cache_,Offsets[n],before_[n]);else exact=false;}
        else{const auto unchanged=~(current^written_[n])&Masks[n];if(unchanged!=Masks[n])exact=false;
            Put(cache_,Offsets[n],(current&~unchanged)|(before_[n]&unchanged));}}
    active_=false;cache_={};return exact;
}
}
