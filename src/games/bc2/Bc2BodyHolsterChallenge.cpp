#include "Bc2BodyHolsterChallenge.h"
#include <bit>
#include <cmath>
#include <cstring>
namespace fvr::bc2 {
namespace {
constexpr std::array<std::size_t,4> Offsets{8+4*7,8+4*8,0x98,0x9c};
constexpr std::array<std::uint32_t,4> Masks{~0u,~0u,(1u<<12)|(1u<<14)|(1u<<29),(1u<<1)|(1u<<4)|(1u<<5)|(1u<<6)};
std::uint32_t Read(std::span<std::byte> b,std::size_t at)noexcept{std::uint32_t x;std::memcpy(&x,b.data()+at,4);return x;}
void Write(std::span<std::byte> b,std::size_t at,std::uint32_t x)noexcept{std::memcpy(b.data()+at,&x,4);}
}
bool BodyHolsterCacheChallenge::Stage(std::span<std::byte> cache,const HolsterSuppressionRequest& r,HolsterInputOwner current,
    const BodyFreeRightEvidence& free,const interaction::HandInteraction& hands,std::int64_t deadline)noexcept {
    if(active_||row_.staged||cache.size()!=InputBytes||!current.current||!current.current(current.context,r)||
       deadline<=r.input.nowNs||deadline-r.input.nowNs>15000000000ll||!BodyFreeRightEvidenceCurrent(free,r.input.nowNs)||
       !free.visibility.hide||free.visibility.request!=r.request||free.visibility.nativeOwner!=r.owner||
       free.input.owner!=r.input.owner||free.input.sequence>r.input.sequence||
       r.input.observedNs>r.input.nowNs||r.input.deadlineNs<=r.input.nowNs||!r.input.focused||!r.input.tracked[1]||
       hands.Current(interaction::InteractionHand::Right)||hands.Current(interaction::InteractionHand::Left))return false;
    row_.request=r;
    for(unsigned n=0;n<4;++n){row_.before[n]=Read(cache,Offsets[n]);
        if(n<2&&!std::isfinite(std::bit_cast<float>(row_.before[n])))return false;
        row_.challenged[n]=n<2?std::bit_cast<std::uint32_t>(1.f):(row_.before[n]|Masks[n]);
        row_.written[n]=row_.before[n]&~Masks[n];}
    cache_=cache;owner_=current;active_=row_.staged=true;
    for(unsigned n=0;n<4;++n)Write(cache_,Offsets[n],row_.challenged[n]);
    if(!owner_.current(owner_.context,r)){Restore();return false;}return true;
}
bool BodyHolsterCacheChallenge::Complete(const std::optional<HolsterSuppressionReceipt>& receipt)noexcept {
    if(!active_)return false;
    bool okay=receipt&&HolsterSuppressionCurrent(*receipt,row_.request)&&owner_.current(owner_.context,row_.request);
    row_.unrelatedPreserved=true;
    for(unsigned n=0;n<4;++n){row_.observed[n]=Read(cache_,Offsets[n]);
        okay=okay&&((row_.observed[n]&Masks[n])==0);
        row_.unrelatedPreserved=row_.unrelatedPreserved&&((row_.observed[n]&~Masks[n])==(row_.before[n]&~Masks[n]));}
    okay=okay&&row_.unrelatedPreserved;
    if(!okay){Restore();return false;}
    row_.committed=true;active_=false;cache_={};return true;
}
bool BodyHolsterCacheChallenge::Restore()noexcept {
    if(!active_)return true;bool exact=true;
    for(unsigned n=0;n<4;++n){const auto value=Read(cache_,Offsets[n]);
        if(n<2){if(value==row_.challenged[n])Write(cache_,Offsets[n],row_.before[n]);else if(value!=row_.written[n])exact=false;}
        else {const auto inserted=Masks[n]&~row_.before[n];
            // Only our synthetic one bits are removed. Current unrelated bits
            // and any pre-existing ordinary bits retain native ownership.
            Write(cache_,Offsets[n],value&~inserted);}
    }row_.restored=exact;active_=false;cache_={};return exact;
}
}
