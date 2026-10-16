#pragma once
#include "Bc2OrdinaryGrenadeProbe.h"
#include "Bc2TwoGrenadeDeathWitness.h"

namespace fvr::bc2::player_grenade {
using private_grenade::Native;
using private_grenade::Facts;
using private_grenade::Request;
enum class Phase:unsigned {Gesture,AwaitReturn,AwaitNeutral,Quarantined};
enum class Fault:unsigned {None,NativeIdentity,ForeignSelection,OriginalAmmo,TargetAmmo,BodyIdentity,Custody,CommitUnknown,CommitValidation};
// Small per-player policy. No input writes, native calls, timers driving gestures,
// runtime journals, or global fire/reload/holster locks.
class Controller {
public:
    void Lifetime(const ReloadStateOwner&)noexcept;
    std::optional<Request> Prepare(const std::optional<Native>&,const Facts&)noexcept;
    bool CanStage(const Request&,const std::optional<Native>&,const Facts&)noexcept;
    void Commit(const Request&,const Facts&,std::optional<bool> observedBit)noexcept;
    void Observe(const std::optional<Native>&,const Facts&)noexcept;
    void Missing(std::int64_t)noexcept;
    bool Started()const noexcept{return phase_==Phase::AwaitReturn;}
    bool NeedsNative()const noexcept{return phase_!=Phase::Quarantined;}
    unsigned Original()const noexcept{return baseline_?baseline_->original.client.target:0;}
    unsigned Target()const noexcept{return baseline_?baseline_->target.client.target:0;}
    Phase Current()const noexcept{return phase_;}
    Fault Failure()const noexcept{return fault_;}
    unsigned Completed()const noexcept{return completed_;}
    unsigned NativeEdges()const noexcept{return edges_;}
    unsigned ObservationGaps()const noexcept{return gaps_;}
    bool LastReturnHadNoReadGaps()const noexcept{return completed_&&lastReturnNoGaps_;}
    std::optional<TwoGrenadeNativeWitness> SecondDeathReadiness(const Native&,const Facts&)const noexcept;
    void Report(std::ostream&)const;
private:
    private_grenade::Sample Sample(const Native&,const Facts&)const noexcept;
    bool Plan(const Native&)const noexcept;
    void CancelGesture(std::uint64_t)noexcept;
    void Quarantine(Fault)noexcept;
    private_grenade::OneGrenade gesture_;
    private_grenade::Sample staged_{};
    std::optional<Native> baseline_;
    std::optional<BodyInventoryDisplay> body_;
    interaction::HandClaimToken gun_{};
    ReloadStateOwner lifetime_{},lastReturnedOwner_{};
    Phase phase_=Phase::Gesture;Fault fault_=Fault::None;
    std::uint64_t neutralAfter_=0,lastSequence_=0;
    std::int64_t neutralObserved_=0,lastObserved_=0;
    // Original accepted source facts, retained across expiry; no derived time.
    std::uint64_t lastReturnInput_=0,lastNeutralInput_=0;
    std::int64_t lastReturnObservedNs_=0,lastNeutralObservedNs_=0;
    unsigned attempts_=0,edges_=0,completed_=0,gaps_=0;
    bool gap_=false;
    bool currentNoGaps_=true,lastReturnNoGaps_=false;
};
}
