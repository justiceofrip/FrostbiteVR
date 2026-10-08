#pragma once
#include <cstdint>
#include <string_view>
namespace fvr::bc2 {
// Trial selection is independent of production acceptance bits. Exactly one
// profile can be admitted for the existing bounded diagnostic lifetime.
enum class BodyHolsterDiagnosticProfile:std::uint32_t {Disabled=0,Spas=1,ScopedXm8=2,ScopedXm8Fire=3,ExactConfiguredTable=4,ExactConfiguredTableFire=5};
constexpr bool BodyHolsterConfiguredDiagnostic(BodyHolsterDiagnosticProfile p)noexcept{
    return p==BodyHolsterDiagnosticProfile::ExactConfiguredTable||p==BodyHolsterDiagnosticProfile::ExactConfiguredTableFire;
}
constexpr bool ValidBodyHolsterDiagnosticProfile(BodyHolsterDiagnosticProfile profile)noexcept {
    return profile==BodyHolsterDiagnosticProfile::Spas||profile==BodyHolsterDiagnosticProfile::ScopedXm8||profile==BodyHolsterDiagnosticProfile::ScopedXm8Fire||BodyHolsterConfiguredDiagnostic(profile);
}
constexpr bool ValidBodyHolsterDiagnosticConfig(BodyHolsterDiagnosticProfile profile,std::uint32_t flags)noexcept {
    return flags&0x80000000u?ValidBodyHolsterDiagnosticProfile(profile):profile==BodyHolsterDiagnosticProfile::Disabled;
}
constexpr unsigned BodyHolsterDiagnosticMask(BodyHolsterDiagnosticProfile profile)noexcept {
    return profile==BodyHolsterDiagnosticProfile::Spas?1u:(profile==BodyHolsterDiagnosticProfile::ScopedXm8||profile==BodyHolsterDiagnosticProfile::ScopedXm8Fire)?2u:0u;
}
constexpr std::string_view BodyHolsterDiagnosticAsset(BodyHolsterDiagnosticProfile profile)noexcept {
    return profile==BodyHolsterDiagnosticProfile::Spas?"SPAS12_sp":(profile==BodyHolsterDiagnosticProfile::ScopedXm8||profile==BodyHolsterDiagnosticProfile::ScopedXm8Fire)?"XM8_sp_s":"";
}
}
