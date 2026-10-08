#pragma once
#include "Bc2MagazineReloadCycle.h"
#include "Bc2ReloadFamily.h"
#include <atomic>
#include <array>
#include <optional>

namespace fvr::bc2 {
// One engine operation family per active cycle. Explicit selection requires
// the adapter's retired-cycle and shared native callback exclusion proof. Native callback
// ownership, original-once execution and retirement stay in the adapter. Typed
// entry/receipt methods prevent a shell request from becoming a magazine refill.
struct ReloadCancellationOrigin {
    ReloadHoldIdentity identity{};
    std::uint64_t cycle=0;
    ReloadNativeFamily family=ReloadNativeFamily::SpasTube;
    NativeMagazineProfileId profile=NativeMagazineProfileId::ScopedXm8;
    ReloadRequestCyclePhase prior=ReloadRequestCyclePhase::Idle;
};
class Bc2ReloadNativePolicy {
    // Used only under the adapter's existing policy exclusion. Records the first
    // transition, including expiry inside observation methods; repeated Cancel
    // must not relabel a formerly Held operation as an Arming cancellation.
    class CancellationOriginScope {
    public:
        explicit CancellationOriginScope(Bc2ReloadNativePolicy& p)noexcept:
            policy_(p),before_{p.Identity(),p.Cycle(),p.Family(),p.MagazineProfileId(),p.Phase()}{}
        ~CancellationOriginScope()noexcept {
            if(before_.prior!=ReloadRequestCyclePhase::Cancelled&&before_.cycle&&
               policy_.Phase()==ReloadRequestCyclePhase::Cancelled&&
               policy_.Cycle()==before_.cycle&&policy_.Identity()==before_.identity&&
               policy_.Family()==before_.family&&policy_.MagazineProfileId()==before_.profile)
                policy_.cancelOrigin_[before_.family==ReloadNativeFamily::Xm8Magazine?1u:0u]=before_;
        }
    private:
        Bc2ReloadNativePolicy& policy_;
        ReloadCancellationOrigin before_;
    };
public:
    std::optional<ReloadCancellationOrigin> CancellationOrigin()const noexcept {
        const auto& o=cancelOrigin_[IsMagazine()?1u:0u];
        if(!o||Phase()!=ReloadRequestCyclePhase::Cancelled||o->cycle!=Cycle()||
           o->identity!=Identity()||o->family!=Family()||o->profile!=MagazineProfileId())return {};
        return o;
    }
    bool CancelledFromArming()const noexcept {
        const auto o=CancellationOrigin();
        return o&&o->prior==ReloadRequestCyclePhase::Arming&&!PendingRequest()&&!UnresolvedRequest();
    }
    bool ConfigureMagazine()noexcept {
        if(spas_.Phase()!=ReloadRequestCyclePhase::Idle||magazine_.Phase()!=ReloadRequestCyclePhase::Idle)return false;
        magazineMode_=true;return true;
    }
    const auto& MagazineStartupPulse()const noexcept{return magazine_.StartupPulse();}
    auto MagazineFirstHoldingNs()const noexcept{return magazine_.FirstHoldingNs();}
    const auto& MagazineArmingContexts()const noexcept{return magazine_.ArmingContexts();}
    bool IsMagazine()const noexcept{return magazineMode_.load(std::memory_order_acquire);}
    ReloadNativeFamily Family()const noexcept{return IsMagazine()?ReloadNativeFamily::Xm8Magazine:ReloadNativeFamily::SpasTube;}
    NativeMagazineProfileId MagazineProfileId()const noexcept{return magazineProfileId_.load(std::memory_order_acquire);}
    const MagazineNativeProfile& MagazineProfile()const noexcept{return *ResolveMagazineNativeProfile(MagazineProfileId());}
    bool SelectMagazineProfile(NativeMagazineProfileId id,bool callbacksDrained,bool priorRetired)noexcept {
        return SelectProfile(ReloadNativeFamily::Xm8Magazine,id,callbacksDrained,priorRetired);
    }
    bool SelectFamily(ReloadNativeFamily family,bool callbacksDrained,bool priorRetired)noexcept {
        return SelectProfile(family,NativeMagazineProfileId::ScopedXm8,callbacksDrained,priorRetired);
    }
    // The native reserve reader uses this exact dispatch predicate before its
    // existing owner, three-copy count, capacity and expiry checks.
    bool MatchesSelectedConfig(const ReloadObservedConfig& c)const noexcept {
        return IsMagazine()?MagazineProfile().Matches(c):IsDiagnosticSpasConfig(c);
    }
    bool Start(const ReloadCycleControl& c,std::int64_t n)noexcept {CancellationOriginScope cancellation(*this);
        if(magazineMode_||!spas_.Start(c,n))return false;
        retired_[0].reset();cancelOrigin_[0].reset();return true;
    }
    bool StartMagazine(const ReloadCycleControl& c,const interaction::ManualReloadRequest& r,std::int64_t n,std::optional<ReloadMagazineStartupPulse> pulse=std::nullopt)noexcept {CancellationOriginScope cancellation(*this);
        if(!magazineMode_||!magazine_.Start(c,r,n,pulse))return false;
        retired_[1].reset();cancelOrigin_[1].reset();return true;
    }
    bool KeepAlive(const ReloadCycleControl& c,std::int64_t n)noexcept{CancellationOriginScope cancellation(*this);return magazineMode_?magazine_.KeepAlive(c,n):spas_.KeepAlive(c,n);}
    ReloadRequestDecision Evaluate(const ReloadHoldInput& i,bool timing,bool cohort,std::uint64_t u,std::int64_t n)noexcept {CancellationOriginScope cancellation(*this);
        return magazineMode_?magazine_.Evaluate(i,timing,cohort,u,n):spas_.Evaluate(i,timing,cohort,u,n);
    }
    bool Allows(const ReloadRequestDecision& d,std::int64_t n)noexcept{CancellationOriginScope cancellation(*this);return magazineMode_?magazine_.Allows(d,n):spas_.Allows(d,n);}
    bool Finish(const ReloadRequestDecision& d,const ReloadFiringObservation& s,std::int64_t n,bool retained,const ReloadDeltaOverride& o)noexcept {CancellationOriginScope cancellation(*this);
        return Finish(d,s,n,retained,o,n);
    }
    bool Finish(const ReloadRequestDecision& d,const ReloadFiringObservation& s,std::int64_t n,bool retained,const ReloadDeltaOverride& o,std::int64_t processing)noexcept {CancellationOriginScope cancellation(*this);
        return magazineMode_?magazine_.Finish(d,s,n,retained,o,processing):spas_.Finish(d,s,n,retained,o,processing);
    }
    bool ObserveMagazinePredictionRestore(const ReloadMagazinePredictionRestore& e,std::int64_t now)noexcept {
        CancellationOriginScope cancellation(*this);return magazineMode_&&magazine_.ObservePredictionRestore(e,now);
    }
    bool Transfer(const ReloadRoundTransfer& t,std::uint64_t u)noexcept {CancellationOriginScope cancellation(*this);
        if(!magazineMode_)return spas_.Transfer(t,u);
        return magazine_.Transfer({t.identity,t.cycle,t.invocation,t.beginNs,t.endNs,t.branch,
            t.loadedBefore,t.reserveBefore,t.loadedAfter,t.reserveAfter,t.ordinaryState12Verified,t.identityRetained},u);
    }
    std::optional<ReloadRoundLease> Lease(const ReloadHoldIdentity& i,std::uint64_t c,std::int64_t n)noexcept {CancellationOriginScope cancellation(*this);
        return magazineMode_?std::nullopt:spas_.Lease(i,c,n);
    }
    std::optional<ReloadMagazineLease> MagazineLease(const ReloadHoldIdentity& i,std::uint64_t c,std::int64_t n)noexcept {CancellationOriginScope cancellation(*this);
        return magazineMode_?magazine_.Lease(i,c,n):std::nullopt;
    }
    ReloadMagazineLeaseObservation ObserveMagazineLease(const ReloadHoldIdentity& i,std::uint64_t c,std::int64_t n)noexcept {
        CancellationOriginScope cancellation(*this);return magazineMode_?magazine_.ObserveLease(i,c,n):ReloadMagazineLeaseObservation{};
    }
    // Stop-only shared capability. Conversion carries the exact original native
    // lease; it cannot enter either insertion API or fabricate ammunition credit.
    std::optional<ReloadRoundLease> AbortLease(const ReloadHoldIdentity& i,std::uint64_t c,std::int64_t n)noexcept {CancellationOriginScope cancellation(*this);
        if(!magazineMode_)return spas_.Lease(i,c,n);
        const auto l=magazine_.Lease(i,c,n);if(!l)return {};
        return ReloadRoundLease{l->identity,l->cycle,l->sequence,l->observedNs,l->deadlineNs,
            l->loaded,l->reserve,l->capacity,l->nativeBindingVerified,l->allThreeHeld};
    }
    bool Submit(const Bc2ReloadNativeRequest& r,std::int64_t n)noexcept{CancellationOriginScope cancellation(*this);return !magazineMode_&&spas_.Submit(r,n);}
    bool SubmitMagazine(const ReloadMagazineNativeRequest& r,std::int64_t n)noexcept{CancellationOriginScope cancellation(*this);return magazineMode_&&magazine_.Submit(r,n);}
    std::optional<Bc2ReloadAckEvidence> TakeAcknowledgement(const ReloadHoldIdentity& i,std::uint64_t c,std::int64_t n)noexcept {CancellationOriginScope cancellation(*this);
        return magazineMode_?std::nullopt:spas_.TakeAcknowledgement(i,c,n);
    }
    std::optional<ReloadMagazineAckEvidence> TakeMagazineAcknowledgement(const ReloadHoldIdentity& i,std::uint64_t c,std::int64_t n)noexcept {CancellationOriginScope cancellation(*this);
        return magazineMode_?magazine_.TakeAcknowledgement(i,c,n):std::nullopt;
    }
    std::optional<ReloadMagazineGateAcknowledgement> TakeUnseatAcknowledgement(const ReloadHoldIdentity& i,std::uint64_t c,std::int64_t n)noexcept {CancellationOriginScope cancellation(*this);
        return magazineMode_?magazine_.TakeUnseatAcknowledgement(i,c,n):std::nullopt;
    }
    void Cancel(ReloadRequestCycleFailure f=ReloadRequestCycleFailure::Stopped)noexcept{CancellationOriginScope cancellation(*this);if(magazineMode_)magazine_.Cancel(f);else spas_.Cancel(f);}
    bool MissingEvidence(std::int64_t n)noexcept{CancellationOriginScope cancellation(*this);return magazineMode_?magazine_.MissingEvidence(n):spas_.MissingEvidence(n);}
    bool DrainCancelledInvocations(bool drained)noexcept{CancellationOriginScope cancellation(*this);return magazineMode_?magazine_.DrainCancelledInvocations(drained):spas_.DrainCancelledInvocations(drained);}
    ReloadRequestCyclePhase Phase()const noexcept{return magazineMode_?magazine_.Phase():spas_.Phase();}
    ReloadRequestCycleFailure Failure()const noexcept{return magazineMode_?magazine_.Failure():spas_.Failure();}
    const ReloadHoldIdentity& Identity()const noexcept{return magazineMode_?magazine_.Identity():spas_.Identity();}
    std::uint64_t Cycle()const noexcept{return magazineMode_?magazine_.Cycle():spas_.Cycle();}
    std::uint64_t PendingRequest()const noexcept{return magazineMode_?magazine_.PendingRequest():spas_.PendingRequest();}
    bool UnresolvedRequest()const noexcept{return magazineMode_?magazine_.UnresolvedRequest():spas_.UnresolvedRequest();}
    const std::optional<ReloadRequestClockFailure>& ClockFailure()const noexcept{return magazineMode_?magazine_.ClockFailure():spas_.ClockFailure();}
private:
    // Durable stop-only proof for each persistent policy object. Returning to a
    // previously used family does not create a new native cycle. A dispatcher
    // epoch change must not discard its exact old cycle's completed retirement.
    // No deadline is renewed and these facts are never exposed as ammo/hold/seat
    // evidence. Accepted Start invalidates the corresponding slot immediately.
    struct RetiredPolicyCycle {
        ReloadHoldIdentity identity{};
        std::uint64_t cycle=0;
        NativeMagazineProfileId profile=NativeMagazineProfileId::ScopedXm8;
    };
    std::array<std::optional<RetiredPolicyCycle>,2> retired_{};
    std::array<std::optional<ReloadCancellationOrigin>,2> cancelOrigin_{};
    template<class Policy>
    bool RetiredMatches(unsigned slot,const Policy& policy,NativeMagazineProfileId profile)const noexcept {
        const auto& r=retired_[slot];
        return r&&r->cycle&&r->identity==policy.Identity()&&r->cycle==policy.Cycle()&&
            r->profile==profile&&policy.Phase()==ReloadRequestCyclePhase::Cancelled;
    }
    bool SelectProfile(ReloadNativeFamily family,NativeMagazineProfileId id,bool callbacksDrained,bool priorRetired)noexcept {
        const auto* profile=ResolveMagazineNativeProfile(id);
        if(!callbacksDrained||(family!=ReloadNativeFamily::SpasTube&&family!=ReloadNativeFamily::Xm8Magazine)||
           (family==ReloadNativeFamily::Xm8Magazine&&(!profile||!profile->Reviewed())))return false;
        const bool sameFamily=family==Family();
        if(sameFamily&&(family==ReloadNativeFamily::SpasTube||id==MagazineProfileId()))return true;
        const auto oldFamily=Family();const auto oldProfile=MagazineProfileId();
        const unsigned oldSlot=oldFamily==ReloadNativeFamily::Xm8Magazine?1u:0u;
        const auto oldCycle=Cycle();const auto oldIdentity=Identity();
        const bool remembered=oldSlot?RetiredMatches(1,magazine_,oldProfile):
            RetiredMatches(0,spas_,NativeMagazineProfileId::ScopedXm8);
        if(oldCycle){
            if((!priorRetired&&!remembered)||Phase()!=ReloadRequestCyclePhase::Cancelled||
               !DrainCancelledInvocations(true))return false;
        }else if(Phase()!=ReloadRequestCyclePhase::Idle)return false;
        const auto next=family==ReloadNativeFamily::Xm8Magazine?magazine_.Phase():spas_.Phase();
        if(next!=ReloadRequestCyclePhase::Idle&&next!=ReloadRequestCyclePhase::Cancelled)return false;
        // An inactive target family could only have been left after its own
        // retirement. The active family's retirement is proved above.
        const bool rebind=family==ReloadNativeFamily::Xm8Magazine&&id!=oldProfile;
        if(rebind){
            // A different profile shares the same magazine policy object. An
            // inactive magazine cycle needs its own retirement, never the
            // currently selected tube policy's receipt.
            const bool magazineRetired=oldSlot==1?(priorRetired||remembered):
                RetiredMatches(1,magazine_,oldProfile);
            if(!magazine_.SelectProfile(*profile,true,magazineRetired))return false;
        }
        if(oldCycle)retired_[oldSlot]=RetiredPolicyCycle{oldIdentity,oldCycle,
            oldSlot?oldProfile:NativeMagazineProfileId::ScopedXm8};
        if(rebind){
            // Only a successful, excluded rebind changes the descriptor on the
            // already retired policy. Original identity/cycle remain immutable.
            if(retired_[1])retired_[1]->profile=id;
            magazineProfileId_.store(id,std::memory_order_release);
        }
        magazineMode_.store(family==ReloadNativeFamily::Xm8Magazine,std::memory_order_release);return true;
    }
    std::atomic<NativeMagazineProfileId> magazineProfileId_{NativeMagazineProfileId::ScopedXm8};
    std::atomic<bool> magazineMode_{false};
    Bc2ReloadRequestCycle spas_{true};
    Bc2MagazineReloadCycle magazine_{true};
};
}
