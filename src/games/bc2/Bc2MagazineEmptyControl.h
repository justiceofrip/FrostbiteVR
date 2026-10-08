#pragma once
#include "Bc2MagazineNativeProfile.h"
#include "Bc2ManualEmptyFamily.h"
#include "Bc2ReloadRequestCycle.h"
#include <algorithm>
#include <array>
#include <optional>
#include <cstdint>

namespace fvr::bc2 {
// Only the verified Step state-2 reload-entry branch consumes this override.
// Never wrap Update: its shot subscribers also receive the complete context.
inline std::int64_t MagazineEmptyEvidenceDeadline(std::int64_t observed,std::int64_t ownerDeadline)noexcept {
    // Every branch also consumes the original server-owner read (200 ms).
    if(observed<=0||observed>INT64_MAX-200000000||ownerDeadline<=observed)return 0;
    return std::min(ownerDeadline,observed+200000000);
}
struct MagazineEmptyStep {
    ReloadHoldIdentity identity{};
    NativeMagazineProfileId profile=NativeMagazineProfileId::ScopedXm8;
    std::uint64_t ownerRevision=0,sourceSequence=0;
    std::int64_t observedNs=0,deadlineNs=0;
    unsigned branch=3;
    ReloadFiringObservation before{};
};
inline bool ManualEmptyStepBoundaryEligible(const MagazineEmptyStep& s,
    ReloadRequestCyclePhase phase,const ReloadUpdateContext& context,std::int64_t now)noexcept {
    const auto& b=s.before;const auto& o=s.identity.owner;
    const bool inactive=phase==ReloadRequestCyclePhase::Idle||phase==ReloadRequestCyclePhase::Finished||
        phase==ReloadRequestCyclePhase::Cancelled;
    // Arming authorizes the explicit native Reload pulse, not an unrelated
    // empty-magazine entry. Preserve that pulse while suppressing automatic
    // entry during the pending physical interaction's neutral/fire callbacks.
    const bool unsolicitedArming=phase==ReloadRequestCyclePhase::Arming&&
        !context.reloadRequested&&!(context.inputFlags&4u);
    return (inactive||unsolicitedArming)&&
        o.player>=0x10000&&o.soldier>=0x10000&&o.weak>=0x10000&&o.weapon>=0x10000&&
        o.actorGeneration&&o.equipGeneration&&o.space&&s.identity.serverPlayer>=0x10000&&
        s.identity.serverSoldier>=0x10000&&s.identity.serverItem>=0x10000&&
        s.identity.firing[0]>=0x10000&&s.identity.firing[1]>=0x10000&&s.identity.firing[2]>=0x10000&&
        s.identity.firing[0]!=s.identity.firing[1]&&s.identity.firing[0]!=s.identity.firing[2]&&s.identity.firing[1]!=s.identity.firing[2]&&
        s.ownerRevision&&s.sourceSequence&&s.observedNs>0&&s.observedNs<=now&&now<s.deadlineNs&&
        s.deadlineNs==MagazineEmptyEvidenceDeadline(s.observedNs,s.deadlineNs)&&s.branch<3&&b.address==s.identity.firing[s.branch]&&
        b.address>=0x10000&&b.wrapperOffset==(s.branch==0?0x3cu:s.branch==1?0x40u:0x10u)&&
        b.currentState==2&&b.nextState==2&&std::isfinite(b.phaseTimer)&&b.phaseTimer>=0&&
        b.loaded>=0&&b.loaded<=1000000&&b.reserve>=0&&b.reserve<=1000000&&!(b.flagsA8&(8|16))&&
        ValidManualReloadDelta(context.deltaSeconds)&&
        context.reloadTimeMultiplier==1&&!context.orderRequested&&!(context.inputFlags&~5u)&&
        context.fireRequested==bool(context.inputFlags&1)&&context.reloadRequested==bool(context.inputFlags&4)&&
        context.flags24Through28[0]&&!context.flags24Through28[2]&&!context.flags24Through28[4];
}
inline bool MagazineEmptyStepEligible(const MagazineEmptyStep& s,const ReloadObservedConfig& config,
    ReloadRequestCyclePhase phase,const ReloadUpdateContext& context,std::int64_t now)noexcept {
    const auto* profile=ResolveMagazineNativeProfile(s.profile);
    return profile&&profile->Matches(config)&&profile->ReviewedDispatch()&&
        ManualEmptyStepBoundaryEligible(s,phase,context,now);
}
inline bool ManualEmptyFamilyStepEligible(const MagazineEmptyStep& s,ReloadNativeFamily family,
    const ReloadObservedConfig& config,ReloadRequestCyclePhase phase,
    const ReloadUpdateContext& context,std::int64_t now)noexcept {
    if(family==ReloadNativeFamily::Xm8Magazine)return MagazineEmptyStepEligible(s,config,phase,context,now);
    return ManualEmptyFamilyConfig(family,s.profile,config)&&ManualEmptyStepBoundaryEligible(s,phase,context,now);
}
struct MagazineEmptyByteAccess {
    void* context=nullptr;
    bool (*compareExchange)(void*,std::uint8_t expected,std::uint8_t replacement,std::uint8_t& observed)=nullptr;
};
struct MagazineEmptyByteOverride {
    std::uint8_t original=0,beforeRestore=0;
    bool applied=false,restored=false,unexpectedNativeWrite=false;
    bool Apply(const MagazineEmptyByteAccess& a,std::uint8_t expected)noexcept {
        // A native true value is already inhibited and is never normalized.
        if(applied||expected!=0||!a.compareExchange)return false;
        std::uint8_t actual=0;
        if(!a.compareExchange(a.context,expected,1,actual)||actual!=expected)return false;
        original=expected;applied=true;return true;
    }
    bool Restore(const MagazineEmptyByteAccess& a)noexcept {
        if(!applied||restored||!a.compareExchange)return false;
        if(!a.compareExchange(a.context,1,original,beforeRestore))return false;
        unexpectedNativeWrite=beforeRestore!=1;
        restored=!unexpectedNativeWrite;return restored;
    }
};
inline void RunMagazineEmptyByteOverride(const MagazineEmptyByteAccess& access,std::uint8_t expected,bool request,
    void (*original)(void*),void* context,MagazineEmptyByteOverride& receipt) {
    if(request)receipt.Apply(access,expected);
#if defined(_MSC_VER)
    __try {original(context);}
    __finally {if(receipt.applied)receipt.Restore(access);}
#else
    struct Cleanup {const MagazineEmptyByteAccess& a;MagazineEmptyByteOverride& r;~Cleanup(){if(r.applied)r.Restore(a);}} cleanup{access,receipt};
    original(context);
#endif
}
// Applied/restored native Step receipts only. This never grants a reload hold,
// ammunition transfer, replacement object, or successful cancellation.
class MagazineEmptyControlReceipts {
public:
    void Clear()noexcept{rows_={};}
    bool Observe(const MagazineEmptyStep& s,const ReloadFiringObservation& after,
        const MagazineEmptyByteOverride& patch,bool ownerRetained,std::int64_t now)noexcept {
        if(s.branch>=3)return false;
        rows_[s.branch].reset();
        if(!ownerRetained||!patch.applied||!patch.restored||patch.unexpectedNativeWrite||
            now<s.observedNs||now>=s.deadlineNs||s.deadlineNs!=MagazineEmptyEvidenceDeadline(s.observedNs,s.deadlineNs)||after.address!=s.before.address||
            after.wrapperOffset!=s.before.wrapperOffset||after.currentState!=2||after.nextState!=2||
            after.loaded!=s.before.loaded||after.reserve!=s.before.reserve||after.flagsA8!=s.before.flagsA8)return false;
        rows_[s.branch]=s;return true;
    }
    std::optional<std::int64_t> EmptyDeadline(const ReloadHoldIdentity& id,NativeMagazineProfileId profile,
        std::uint64_t revision,int reserve,std::int64_t now)const noexcept {
        std::int64_t deadline=INT64_MAX;
        for(unsigned n=0;n<3;++n){const auto& s=rows_[n];
            if(!s||s->identity!=id||s->profile!=profile||s->ownerRevision!=revision||
                s->branch!=n||s->before.loaded!=0||s->before.reserve!=reserve||
                now<s->observedNs||now>=s->deadlineNs)return {};
            deadline=std::min(deadline,s->deadlineNs);
        }
        return deadline;
    }
private:
    std::array<std::optional<MagazineEmptyStep>,3> rows_{};
};
} // namespace fvr::bc2
