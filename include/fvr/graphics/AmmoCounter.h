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
// native addresses. Times are original system-QPC nanoseconds, not XR time.
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
inline bool AmmoCounterFresh(const AmmoCounterSample& s,std::int64_t now)noexcept {
    return s.sequence&&s.actorGeneration&&s.equipmentGeneration&&s.spaceGeneration&&!s.reserved&&
        s.observedNs>0&&s.observedNs<=now&&s.deadlineNs>now&&s.deadlineNs-s.observedNs<=AmmoCounterMaxAgeNs&&
        s.capacity>0&&s.capacity<=1000000&&s.loaded>=0&&s.loaded<=s.capacity&&s.reserve>=0&&s.reserve<=1000000;
}
inline bool SameAmmoCounterOwner(const AmmoCounterSample& a,const AmmoCounterSample& b)noexcept {
    return a.actorGeneration==b.actorGeneration&&a.equipmentGeneration==b.equipmentGeneration&&a.spaceGeneration==b.spaceGeneration;
}
inline bool SameAmmoCounterCounts(const AmmoCounterSample& a,const AmmoCounterSample& b)noexcept {
    return a.loaded==b.loaded&&a.reserve==b.reserve&&a.capacity==b.capacity;
}
inline std::optional<AmmoCounterSample> AmmoCounterPair(const AmmoCounterSample& a,const AmmoCounterSample& b,
    std::uint64_t space,std::int64_t now)noexcept {
    if(!space||a.spaceGeneration!=space||!AmmoCounterFresh(a,now)||!AmmoCounterFresh(b,now)||
        !SameAmmoCounterOwner(a,b)||!SameAmmoCounterCounts(a,b))return {};
    if((a.sequence==b.sequence&&a.observedNs!=b.observedNs)||
       (a.sequence<b.sequence&&a.observedNs>b.observedNs)||(b.sequence<a.sequence&&b.observedNs>a.observedNs))return {};
    auto out=a.sequence<=b.sequence?a:b;
    out.deadlineNs=(std::min)(a.deadlineNs,b.deadlineNs);
    return AmmoCounterFresh(out,now)?std::optional{out}:std::nullopt;
}
// Counts have their own expiry, even while the host retains a world image.
class AmmoCounterPresentation {
public:
    void Reset()noexcept {visible_=false;last_={};}
    bool Receive(const AmmoCounterSample& a,const AmmoCounterSample& b,std::uint64_t space,std::int64_t now)noexcept {
        visible_=false;auto next=AmmoCounterPair(a,b,space,now);if(!next)return false;
        if(last_.sequence&&SameAmmoCounterOwner(last_,*next)){
            if(next->sequence<last_.sequence||next->observedNs<last_.observedNs)return false;
            if(next->sequence==last_.sequence){
                if(next->observedNs!=last_.observedNs||!SameAmmoCounterCounts(last_,*next))return false;
                next->deadlineNs=(std::min)(next->deadlineNs,last_.deadlineNs);
            }
        }
        if(!AmmoCounterFresh(*next,now))return false;last_=*next;visible_=true;return true;
    }
    const AmmoCounterSample* Select(std::uint64_t space,std::int64_t now)const noexcept {
        return visible_&&last_.spaceGeneration==space&&AmmoCounterFresh(last_,now)?&last_:nullptr;
    }
private:AmmoCounterSample last_{};bool visible_=false;
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
