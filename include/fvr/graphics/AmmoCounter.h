#pragma once
#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <type_traits>
namespace fvr::graphics {
// Read-only presentation wire data. Identity consists of generations, never
// native addresses. equipmentGeneration is the HUD presentation lifetime, not
// a native ammunition/equipment generation: BC2 binds equipment AND committed
// scene lifetime into it. Times are original system-QPC nanoseconds, not XR time.
struct alignas(8) AmmoCounterSample {
    std::uint64_t sequence=0;
    std::int64_t observedNs=0,deadlineNs=0;
    std::uint64_t actorGeneration=0,equipmentGeneration=0,spaceGeneration=0;
    std::int32_t loaded=0,reserve=0,capacity=0;
    std::uint32_t reserved=0;
    bool operator==(const AmmoCounterSample&)const=default;
};
static_assert(sizeof(AmmoCounterSample)==64&&offsetof(AmmoCounterSample,loaded)==48);
static_assert(std::is_trivially_copyable_v<AmmoCounterSample>);
inline constexpr std::int64_t AmmoCounterMaxAgeNs=100000000;
// An owner-only packet carries no ammunition observation. Its original times
// describe current held ownership, allowing the HUD to keep its last digits.
// Older wire consumers reject the reserved tag; the 64-byte layout is unchanged.
inline constexpr std::uint32_t AmmoCounterOwnerOnly=1;
inline constexpr std::int64_t AmmoCounterOwnerMaxAgeNs=250000000;
inline bool AmmoCounterFresh(const AmmoCounterSample& s,std::int64_t now)noexcept {
    return s.sequence&&s.actorGeneration&&s.equipmentGeneration&&s.spaceGeneration&&!s.reserved&&
        s.observedNs>0&&s.observedNs<=now&&s.deadlineNs>now&&s.deadlineNs-s.observedNs<=AmmoCounterMaxAgeNs&&
        s.capacity>0&&s.capacity<=1000000&&s.loaded>=0&&s.loaded<=s.capacity&&s.reserve>=0&&s.reserve<=1000000;
}
inline bool AmmoCounterOwnerOnlyFresh(const AmmoCounterSample& s,std::int64_t now)noexcept {
    return !s.sequence&&s.reserved==AmmoCounterOwnerOnly&&s.actorGeneration&&s.equipmentGeneration&&s.spaceGeneration&&
        !s.loaded&&!s.reserve&&!s.capacity&&s.observedNs>0&&s.observedNs<=now&&s.deadlineNs>now&&
        s.deadlineNs-s.observedNs<=AmmoCounterOwnerMaxAgeNs;
}
inline bool SameAmmoCounterOwner(const AmmoCounterSample& a,const AmmoCounterSample& b)noexcept {
    return a.actorGeneration==b.actorGeneration&&a.equipmentGeneration==b.equipmentGeneration&&a.spaceGeneration==b.spaceGeneration;
}
inline bool SameAmmoCounterCounts(const AmmoCounterSample& a,const AmmoCounterSample& b)noexcept {
    return a.loaded==b.loaded&&a.reserve==b.reserve&&a.capacity==b.capacity;
}
// Final restored-frame publication rechecks current presentation ownership.
// Clearing these two samples leaves all body props and native frame data alone.
inline bool RetainAmmoCounterPairOwner(AmmoCounterSample& a,AmmoCounterSample& b,
    const AmmoCounterSample& current,std::int64_t now)noexcept {
    const auto valid=[&](const AmmoCounterSample& s){return AmmoCounterFresh(s,now)||AmmoCounterOwnerOnlyFresh(s,now);};
    if(!valid(a)||!valid(b)||!valid(current)||!SameAmmoCounterOwner(a,b)||!SameAmmoCounterOwner(a,current)){
        a={};b={};return false;
    }
    const auto deadline=(std::min)({a.deadlineNs,b.deadlineNs,current.deadlineNs});
    a.deadlineNs=b.deadlineNs=deadline;return true;
}
inline std::optional<AmmoCounterSample> AmmoCounterPair(const AmmoCounterSample& a,const AmmoCounterSample& b,
    std::uint64_t space,std::int64_t now)noexcept {
    if(!space||a.spaceGeneration!=space||!AmmoCounterFresh(a,now)||!AmmoCounterFresh(b,now)||
        !SameAmmoCounterOwner(a,b)||a.capacity!=b.capacity)return {};
    if((a.sequence==b.sequence&&(a.observedNs!=b.observedNs||!SameAmmoCounterCounts(a,b)))||
       (a.sequence<b.sequence&&a.observedNs>b.observedNs)||(b.sequence<a.sequence&&b.observedNs>a.observedNs))return {};
    // Read-only HUD observations can advance between the two native draws.
    // One shared quad uses the latest exact-owner observation for BOTH eyes;
    // a shot/reload between draws must not blank the counter. Equal sequence
    // with conflicting counts is still corruption, not a newer observation.
    auto out=a.sequence>=b.sequence?a:b;
    out.deadlineNs=(std::min)(a.deadlineNs,b.deadlineNs);
    return AmmoCounterFresh(out,now)?std::optional{out}:std::nullopt;
}
// Count admission stays strict, but admitted digits are presentation state.
// Only independent current-owner evidence can keep them visible through an
// ammo-publication gap. Never renew the saved ammunition observation itself.
class AmmoCounterPresentation {
public:
    void Reset()noexcept {last_={};owner_={};}
    // Passive diagnostics: original admitted evidence, never a renewed lease.
    const AmmoCounterSample& CountEvidence()const noexcept {return last_;}
    const AmmoCounterSample& OwnerEvidence()const noexcept {return owner_;}
    bool Receive(const AmmoCounterSample& a,const AmmoCounterSample& b,std::uint64_t space,std::int64_t now)noexcept {
        const bool countA=AmmoCounterFresh(a,now),countB=AmmoCounterFresh(b,now);
        if(!space||a.spaceGeneration!=space||!SameAmmoCounterOwner(a,b)||
           (!countA&&!AmmoCounterOwnerOnlyFresh(a,now))||(!countB&&!AmmoCounterOwnerOnlyFresh(b,now))){
            // A coherent count pair can expire in transit while an independent
            // owner-only lease already authorizes these exact displayed digits.
            // Reject the expired count without erasing that ORIGINAL authority.
            // Nil, owner-only expiry, malformed pairs and lifetime changes still
            // retire. Neither the old count nor the owner lease is rewritten.
            const auto pairTime=(std::max)(a.observedNs,b.observedNs);
            const auto expiredPair=pairTime<=now?AmmoCounterPair(a,b,space,pairTime):std::nullopt;
            if(AmmoCounterOwnerOnlyFresh(owner_,now)&&Select(space,now)&&expiredPair&&
               expiredPair->deadlineNs<=now&&SameAmmoCounterOwner(*expiredPair,owner_)&&
               expiredPair->sequence>=last_.sequence&&expiredPair->observedNs>=last_.observedNs&&
               (expiredPair->sequence!=last_.sequence||
                (expiredPair->observedNs==last_.observedNs&&SameAmmoCounterCounts(*expiredPair,last_))))return false;
            Reset();return false;
        }
        if(owner_.actorGeneration&&!SameAmmoCounterOwner(owner_,a))Reset();
        const auto renewOwner=[&](const AmmoCounterSample& left,const AmmoCounterSample& right)noexcept {
            auto owner=left.observedNs>=right.observedNs?left:right;
            owner.deadlineNs=(std::min)(left.deadlineNs,right.deadlineNs);
            if(owner.observedNs==owner_.observedNs)owner.deadlineNs=(std::min)(owner.deadlineNs,owner_.deadlineNs);
            if(owner.observedNs>=owner_.observedNs)owner_=owner;
        };
        if(!countA&&!countB){renewOwner(a,b);return Select(space,now)!=nullptr;}
        const auto rejectCount=[&]()noexcept {
            // Rejected count metadata cannot renew ownership. An independently
            // valid owner-only marker in the other eye still can.
            if(!countA)renewOwner(a,a);if(!countB)renewOwner(b,b);return false;
        };
        auto next=countA&&countB?AmmoCounterPair(a,b,space,now):std::optional{countA?a:b};
        if(!next)return rejectCount();
        if(last_.sequence){
            if(next->sequence<last_.sequence||next->observedNs<last_.observedNs)return rejectCount();
            if(next->sequence==last_.sequence){
                if(next->observedNs!=last_.observedNs||!SameAmmoCounterCounts(last_,*next))return rejectCount();
                next->deadlineNs=(std::min)(next->deadlineNs,last_.deadlineNs);
            }
        }
        if(!AmmoCounterFresh(*next,now))return rejectCount();
        renewOwner(*next,countA&&countB?*next:countA?b:a);last_=*next;return true;
    }
    const AmmoCounterSample* Select(std::uint64_t space,std::int64_t now)const noexcept {
        return last_.sequence&&last_.spaceGeneration==space&&SameAmmoCounterOwner(last_,owner_)&&
            last_.observedNs<=now&&owner_.observedNs<=now&&owner_.deadlineNs>now?&last_:nullptr;
    }
private:AmmoCounterSample last_{},owner_{};
};
// Host display policy only. This does not admit counts, prove current native
// ownership, or make the strict presentation/observation helpers fresh.
struct AmmoCounterDisplaySource {
    std::array<std::uint8_t,16> session{};
    std::uint64_t resourceEpoch=0;
    bool Valid()const noexcept {
        return resourceEpoch&&std::any_of(session.begin(),session.end(),[](auto b){return b!=0;});
    }
    bool operator==(const AmmoCounterDisplaySource&)const=default;
};
enum class AmmoCounterDisplayKind {None,CurrentOwner,StaleUploadedImage};
struct AmmoCounterDisplaySelection {
    const AmmoCounterSample* sample=nullptr;
    AmmoCounterDisplayKind kind=AmmoCounterDisplayKind::None;
};
inline constexpr std::int64_t AmmoCounterHostDisplayGraceNs=50000000;
class AmmoCounterHostDisplay {
public:
    void Reset()noexcept {presentation_.Reset();source_={};graceSource_={};lastClockNs_=0;}
    const AmmoCounterSample& CountEvidence()const noexcept {return presentation_.CountEvidence();}
    const AmmoCounterSample& OwnerEvidence()const noexcept {return presentation_.OwnerEvidence();}
    // An imported source can change before its body props reach Receive. Veto
    // grace immediately, without changing normal fresh-owner presentation.
    // Switching away and back cannot restore grace without an accepted count.
    void ObserveSource(const AmmoCounterDisplaySource& source)noexcept {
        if(source_!=source||!source.Valid())graceSource_={};
        source_=source;
    }
    bool Receive(const AmmoCounterSample& a,const AmmoCounterSample& b,std::uint64_t space,
        std::int64_t now,const AmmoCounterDisplaySource& source)noexcept {
        ObserveSource(source);
        if(!source.Valid()||!AdvanceClock(now)){Reset();return false;}
        // A retransmission can carry a later publication deadline for the same
        // count. Reject it without erasing an already earned display grace.
        // This grants no new count, owner evidence or time; Select still needs
        // the matching released image within the same original display bound.
        const auto& admitted=CountEvidence();auto repeated=a;repeated.deadlineNs=admitted.deadlineNs;
        if(source_==graceSource_&&repeated==admitted&&b==a&&OwnerEvidence()==admitted&&
           a.spaceGeneration==space&&AmmoCounterFresh(a,a.observedNs)&&
           a.deadlineNs>=admitted.deadlineNs&&a.deadlineNs<=now&&
           now-admitted.deadlineNs<AmmoCounterHostDisplayGraceNs)return false;
        const bool accepted=presentation_.Receive(a,b,space,now);
        // Keep existing expired-arrival/independent-owner handling, but no
        // rejected packet may grant the additional stale-display privilege.
        if(!accepted)graceSource_={};
        else if(AmmoCounterFresh(a,now)||AmmoCounterFresh(b,now))graceSource_=source_;
        return accepted;
    }
    AmmoCounterDisplaySelection SelectForDisplay(std::uint64_t space,std::int64_t now,bool active,
        const AmmoCounterSample& image,bool hasImage,bool pending)noexcept {
        const auto& count=CountEvidence();const auto& owner=OwnerEvidence();
        if(!active||!space||(count.sequence&&count.spaceGeneration!=space)||!AdvanceClock(now)){
            Reset();return {};
        }
        if(const auto* sample=presentation_.Select(space,now))return {sample,AmmoCounterDisplayKind::CurrentOwner};
        // Reuse only already released, unchanged pixels. The deadline remains
        // the ORIGINAL owner deadline, with an exclusive 50 ms upper bound.
        // No new count/upload, lease renewal, or unobserved-owner proof follows.
        if(!source_.Valid()||source_!=graceSource_||!hasImage||pending||
           !AmmoCounterFresh(count,count.observedNs)||
           (!AmmoCounterFresh(owner,owner.observedNs)&&!AmmoCounterOwnerOnlyFresh(owner,owner.observedNs))||
           !AmmoCounterFresh(image,image.observedNs)||!SameAmmoCounterOwner(count,owner)||
           !SameAmmoCounterOwner(count,image)||!SameAmmoCounterCounts(count,image)||
           count.observedNs>now||owner.observedNs>now||image.observedNs>now||
           now<owner.deadlineNs||now-owner.deadlineNs>=AmmoCounterHostDisplayGraceNs)return {};
        return {&count,AmmoCounterDisplayKind::StaleUploadedImage};
    }
private:
    bool AdvanceClock(std::int64_t now)noexcept {
        if(now<=0||now<lastClockNs_)return false;
        lastClockNs_=now;return true;
    }
    AmmoCounterPresentation presentation_;
    AmmoCounterDisplaySource source_{},graceSource_{};
    std::int64_t lastClockNs_=0;
};
struct AmmoCounterText {std::array<char,24> data{};std::size_t size=0;};
inline AmmoCounterText FormatAmmoCounter(std::int32_t loaded,std::int32_t reserve)noexcept {
    AmmoCounterText text;if(loaded<0||loaded>1000000||reserve<0||reserve>1000000)return text;
    auto* at=std::to_chars(text.data.data(),text.data.data()+text.data.size()-1,loaded).ptr;
    *at++=' ';*at++='/';*at++=' ';
    at=std::to_chars(at,text.data.data()+text.data.size()-1,reserve).ptr;text.size=std::size_t(at-text.data.data());return text;
}
inline constexpr std::array<unsigned char,7> AmmoCounterGlyph(char c)noexcept {
    switch(c){
    case '0':return {14,17,19,21,25,17,14};case '1':return {4,12,4,4,4,4,14};
    case '2':return {14,17,1,2,4,8,31};case '3':return {30,1,1,14,1,1,30};
    case '4':return {2,6,10,18,31,2,2};case '5':return {31,16,16,30,1,1,30};
    case '6':return {14,16,16,30,17,17,14};case '7':return {31,1,2,4,8,8,8};
    case '8':return {14,17,17,14,17,17,14};case '9':return {14,17,17,15,1,1,14};
    case '/':return {1,1,2,4,8,16,16};default:return {};
    }
}
struct AmmoCounterBitmap {
    static constexpr unsigned Width=256,Height=48;
    // RGBA8, opaque glyphs on a translucent black panel. No font/asset dependency.
    std::array<std::uint8_t,Width*Height*4> pixels{};
    bool Render(std::int32_t loaded,std::int32_t reserve)noexcept {
        pixels.fill(0);const auto text=FormatAmmoCounter(loaded,reserve);if(!text.size)return false;
        const unsigned scale=text.size<=13?3:2;
        const unsigned width=unsigned(text.size)*6*scale-scale,left=(Width-width)/2,top=(Height-7*scale)/2;
        for(unsigned y=top-4;y<top+7*scale+4;++y)for(unsigned x=left-4;x<left+width+4;++x)pixels[(y*Width+x)*4+3]=192;
        for(unsigned n=0;n<text.size;++n){const auto glyph=AmmoCounterGlyph(text.data[n]);
            for(unsigned row=0;row<7;++row)for(unsigned col=0;col<5;++col)if(glyph[row]&(1u<<(4-col)))
                for(unsigned dy=0;dy<scale;++dy)for(unsigned dx=0;dx<scale;++dx){
                    const auto i=((top+row*scale+dy)*Width+left+(n*6+col)*scale+dx)*4;
                    pixels[i]=pixels[i+1]=pixels[i+2]=pixels[i+3]=255;
                }
        }return true;
    }
};
} // namespace fvr::graphics
