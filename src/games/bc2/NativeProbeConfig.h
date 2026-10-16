#pragma once
#include <cstdint>
#include "Bc2ChestKnifeProbeConfig.h"
#include "Bc2BodyHolsterDiagnostic.h"
#include "Bc2PumpDiagnostic.h"
#include "Bc2M95StockShotDiagnostic.h"
#include "Bc2M95PhysicalBoltDiagnostic.h"
#include "Bc2M95OrdinaryBoltDiagnostic.h"
#include "Bc2OrdinaryResourceInputDiagnostic.h"
namespace fvr::bc2 {
enum class BoatHeadAimMode:std::uint32_t {Disabled=0,AimOnly=1,AimAndFire=2};
constexpr bool ValidBoatHeadAimConfig(BoatHeadAimMode mode,std::uint32_t flags,std::uint32_t magazineSession)noexcept {
    if(mode==BoatHeadAimMode::Disabled)return true;
    if(mode!=BoatHeadAimMode::AimOnly&&mode!=BoatHeadAimMode::AimAndFire)return false;
    // An independent capability word: the legacy flags have no free bits.
    // Permit normal tracked features, but never combine with synthetic probes.
    constexpr std::uint32_t required=9u|0x3800u; // stream, controllers, motion aim, body follow
    constexpr std::uint32_t allowed=required|0x200u|0x400u|0x4000u|0x10000u|0x80000u|0x100000u|0x200000u|0x2000000u|0x10000000u;
    return (flags&0xffu)==9&&(flags&required)==required&&!(flags&~allowed)&&
        (magazineSession==0||magazineSession==3);
}
constexpr bool ValidPhysicalReloadProbeConfig(std::uint32_t flags,std::uint32_t duration)noexcept {
    return !(flags&0x4000000u)||((flags&0x2000000u)&&!(flags&0x200400u)&&duration==30000);
}
constexpr bool ValidPhysicalReloadConfig(std::uint32_t flags)noexcept {
    // Explicit opt-in, stream + controller/aim/body/observer/hands/fire/support.
    // Native diagnostics must remain isolated from physical hand commands.
    return !(flags&0x2000000u)||((flags&0xffu)==9&&(flags&0x197800u)==0x197800u&&!(flags&0x1c68000u));
}
constexpr bool ValidReloadRequestProbeConfig(std::uint32_t flags,std::uint32_t durationMs)noexcept {
    return !(flags&0x1000000u)||(!(flags&0xe68400u)&&(flags&0xffu)==9&&durationMs==30000&&(flags&0x110000u)==0x110000u);
}
constexpr bool ValidPhysicalReloadRepeatProbeConfig(std::uint32_t flags,std::uint32_t duration)noexcept {
    return !(flags&0x20000000u)||((flags&0x6000000u)==0x6000000u&&!(flags&0xd8000000u)&&
        ValidPhysicalReloadConfig(flags)&&ValidPhysicalReloadProbeConfig(flags,duration));
}
// Bit 0x20000000 selects two physical insertions in the same bounded consumer fixture.
// flags 0 = trace, 1 = one original draw camera pulse, 2 = one visibility pulse,
// 3 = disabled unsafe lifecycle, 4 = corrected pre-update ownership transaction,
// 5 = one native update containing two distinct views with captured output,
// 6 = initialized inactive lifecycle, 7 = one-frame work-pool expansion,
// 8 = 120 consecutive native stereo frames, 9 = bounded tracked IPC stream.
// Bit 0x100 opts stream mode into the expensive one-frame GPU pass inventory.
// Bit 0x200 bypasses the desktop Present sync interval during the stream.
// Bit 0x1000 adds experimental controller angular aim and independent view heading.
// Bit 0x10000 opts into experimental tracked hand/weapon pose publication.
// Bit 0x8000 runs a bounded arm-palette publication pulse (requires pose observer).
// Bit 0x100000 enables explicit two-hand support (requires muzzle firing/hands).
// Bit 0x200000 opts into candidate tracked sight flipping (requires two-hand support and native weapon-mode proof).
// Bit 0x400000 opts into one bounded three-branch native reload hold probe (hands/stream only; no continuous session).
// Bit 0x800000 opts into one diagnostic shell release/re-hold cycle; exclusive with 0x400000.
// Bit 0x1000000 is the isolated30s external request fixture; mutually exclusive with hold/round, never continuous.
// Bit 0x4000000 is the bounded synthetic controller trajectory through the actual physical consumer.
// Bit 0x2000000 opts into the physical SPAS consumer; default off pending native/headset acceptance.
// Bit 0x80000 enables opt-in post-configuration muzzle placement for verified campaign firearms.
// Bit 0x800 opts into verified local on-foot controller input.
// Bit 0x400 runs until hostPid exits (stream only); the duration word holds its PID.
// Finite input-only trials: stock1/2, or explicitly compiled player-policy3.
// Default false preserves every pre-player caller and does not enable normal XR.
constexpr bool ValidOrdinaryGrenadeInputSession(unsigned mode,unsigned flags,unsigned duration,
    unsigned magazineSession,bool otherDiagnostic,bool combinedCompiled,bool playerCompiled=false,bool twoDeathCompiled=false)noexcept {
    constexpr unsigned required=9u|0x800u|0x1000u|0x2000u|0x4000u|0x10000u|0x80000u|0x100000u|0x2000000u|0x10000000u;
    return !mode||((mode==1||(mode==2&&combinedCompiled)||(mode==3&&playerCompiled)||(mode==4&&playerCompiled&&twoDeathCompiled))&&duration==(mode==4?30000u:20000u)&&magazineSession==3&&
        (flags&~0x300u)==required&&!otherDiagnostic);
}
// Normal player opt-in is separate from every finite diagnostic. The existing
// controller still requires fresh native menu ownership for each gesture stage.
constexpr bool ValidNormalPlayerGrenades(unsigned enabled,unsigned flags,unsigned hostPid,
    unsigned magazineSession,bool otherDiagnostic,bool compiled)noexcept {
    if(!enabled)return true;
    constexpr unsigned required=9u|0x197800u|0x2000000u|0x10000000u|0x400u;
    constexpr unsigned allowed=required|0x200u|0x200000u; // mirror pacing, sight
    return enabled==1&&compiled&&hostPid&&magazineSession==3&&!otherDiagnostic&&
        (flags&0xffu)==9&&(flags&required)==required&&!(flags&~allowed);
}
// Independent normal capabilities; compile support alone enables no item.
constexpr bool ValidNormalPlayerTools(unsigned knife,unsigned contextual,unsigned flags,unsigned hostPid,
    unsigned magazineSession,bool otherDiagnostic,bool knifeCompiled,bool contextualCompiled)noexcept {
    return ValidNormalPlayerGrenades(knife,flags,hostPid,magazineSession,otherDiagnostic,knifeCompiled)&&
        ValidNormalPlayerGrenades(contextual,flags,hostPid,magazineSession,otherDiagnostic,contextualCompiled);
}
struct NativeProbeConfig {
    std::uint32_t magic=0x32504246,bytes=1220;
    union {std::uint32_t durationMs=3000;std::uint32_t hostPid;};
    std::uint32_t flags=0;
    wchar_t reportPath[512]{};
    wchar_t frameChannel[64]{};
    std::uint32_t magazineReloadSession=0; // MagazineReloadSession; explicit diagnostic only.
    BoatHeadAimMode boatHeadAim=BoatHeadAimMode::Disabled; // Explicit capability; fire is a separate opt-in.
    BodyHolsterDiagnosticProfile bodyHolsterDiagnostic=BodyHolsterDiagnosticProfile::Disabled; // Bounded trial only, never production acceptance.
    std::uint32_t m95PhysicalBoltCycles=0; // Explicit finite private physical bolt trial only.
    std::uint32_t m95StockShot=0; // 1: one loaded shot, 2: five shots through empty. No manual hold.
    std::uint32_t m95OrdinaryBoltInputCycles=0; // Bounded real-control trajectory; ordinary adapters only.
    std::uint32_t ordinaryGrenadeInput=0; // 1: stock forward;2: stock/fade;3: real-menu player;4: bounded two-grenade/fade. Never normal XR.
    std::uint32_t ordinaryResourceInput=0; // 1: combined; 2: eight pumps/empty tube, ordinary input only.
    PumpHoldDiagnostic pumpHoldDiagnostic=PumpHoldDiagnostic::Disabled; // Isolated one-shot350ms trial, never ordinary activation.
    std::uint32_t chestKnifeProbe=0; // Private finite1 draw/hold/return,2 one physical stroke. Default OFF.
    std::uint32_t normalPlayerGrenades=0; // Explicit continuous real-host option; never a finite recipe.
    std::uint32_t normalPlayerKnife=0; // Normal chest draw, motion-triggered stock melee, explicit release-return.
    std::uint32_t normalContextualTools=0; // Normal authored slot4 tool custody/controls; real menu required.
};
static_assert(sizeof(NativeProbeConfig)==1220);
}
