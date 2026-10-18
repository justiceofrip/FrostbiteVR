#pragma once
#include "Bc2MenuState.h"
#include <string_view>

namespace fvr::bc2 {
enum class MenuStartupMovie : unsigned {None=0, EA=1, Dolby=2};
struct MenuStartupBinding {
    std::uint32_t managerGlobal=0, updateSlot=0, update=0, cleanup=0;
    bool operator==(const MenuStartupBinding&)const=default;
};
// The inspected retail x86 build only. Live spans must also match the file.
std::optional<MenuStartupBinding> DiscoverMenuStartup(std::span<const std::byte>,const engine::PeImage&);
std::array<MenuStateCodeSpan,5> MenuStartupCodeSpans()noexcept;
struct MenuStartupSnapshot {
    std::uint32_t manager=0,owner=0,index=0,count=0;
    bool enabled=false;
    std::array<char,257> current{},previous{};
    bool operator==(const MenuStartupSnapshot&)const=default;
};
// Bounded reads; validates front-end ownership, all three vtables and the exact
// selected sequence entry. Does not interpret the transient profile-save pulse
// as menu visibility or persistently completed startup.
std::optional<MenuStartupSnapshot> ReadMenuStartup(const MenuMemory&,const MenuStartupBinding&,
    std::uint32_t imageBase,std::uint32_t expectedManager);
constexpr MenuStartupMovie ChooseMissingStartupMovie(bool exactOwner,bool enabled,
    unsigned index,unsigned count,std::string_view current,std::string_view previous,
    unsigned missingMask,unsigned attemptedMask)noexcept {
    if(!exactOwner||!enabled||!count||count>16||!index||index>count||current!=previous)return MenuStartupMovie::None;
    const auto movie=current=="Movies/UIMALogos"?MenuStartupMovie::EA:
        current=="Movies/UIMADolby"?MenuStartupMovie::Dolby:MenuStartupMovie::None;
    const auto bit=unsigned(movie);
    return bit&&(missingMask&bit)&&!(attemptedMask&bit)?movie:MenuStartupMovie::None;
}
}
