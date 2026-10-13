#pragma once
#include <array>
#include <string_view>
namespace fvr::bc2 {
enum class BodyCarryClass : unsigned { Unsupported, LongGun, Sidearm };
struct BodyCarryProfile { std::string_view asset; unsigned nativeCategory; BodyCarryClass carry; };
// Carry metadata is independent of native fire/reload classification. The
// installed handgun SoldierWeaponData rows use wcSmg (2), despite being pistols.
// This exact reviewed asset changes only body placement: no firing, reload,
// visibility, mesh, or chamber capability is admitted by this table.
// Evidence: support308/312 exact authored handgun configurations. Native295-305
// additionally exercise MP443_sp; other rows add carry metadata only.
inline constexpr std::array<BodyCarryProfile,6> BodyCarryProfiles{{
    {"M1911",2,BodyCarryClass::Sidearm},
    {"M9",2,BodyCarryClass::Sidearm},
    {"M9-3",2,BodyCarryClass::Sidearm},
    {"MP412",2,BodyCarryClass::Sidearm},
    {"MP443",2,BodyCarryClass::Sidearm},
    {"MP443_sp",2,BodyCarryClass::Sidearm},
}};
inline BodyCarryClass ResolveBodyCarry(unsigned category,std::string_view asset)noexcept {
    for(const auto& p:BodyCarryProfiles)if(p.nativeCategory==category&&p.asset==asset)return p.carry;
    return category<=4?BodyCarryClass::LongGun:BodyCarryClass::Unsupported;
}
}
