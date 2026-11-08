#pragma once
#include "Bc2DiagnosticObservationWindow.h"
#include "Bc2ReloadCohortRetry.h"
#include "Bc2ReloadHold.h"
#include "Bc2AmmoResourceBinding.h"
#include <array>

namespace fvr::bc2 {
// Fixed-container producer evidence only. None of these values grants a lease,
// changes an admission result, or participates in native command selection.
struct AmmoPublisherObservation {
    const char* failure=nullptr;
    std::uint64_t windowEpoch=0,invocation=0,parent=0,update=0,frameId=0;
    std::int64_t windowBeginNs=0,windowEndNs=0,beginNs=0,endNs=0,failureNs=0;
    unsigned firing=0,caller=0,contextAddress=0,depth=0,failedBranch=3;
    bool leasePresent=false,serverPresent=false,prepared=false;
    ReloadStateSnapshot leaseSnapshot{};
    ReloadHoldIdentity leaseIdentity{};
    std::int64_t leaseDeadlineNs=0;
    std::uint64_t leaseRevision=0,cleanupEpoch=0,completionCycle=0;
    std::uint64_t stackLow=0,stackHigh=0;
    ReloadCohortRejection preparation{};
    std::optional<ReloadFlowBoundary> preparationBoundary;
    std::optional<int> preparationCapacity;
    ReloadHoldInput input{};
    unsigned deltaBits=0;
    std::array<std::byte,0x30> rawContext{};
    bool rawContextCopied=false;
    std::int64_t checkNowNs=0,bindingReadNs=0;
    std::array<std::int64_t,3> neutral{};
    ReloadHoldIdentity neutralIdentity{};
    bool neutralOwnerChanged=false;
    AmmoResourceBindingReadObservation bindingRead{};
    int fixed=-1,bolt=-1,profilePresent=-1,requestPhase=-1;
    bool requestLockEntered=false;
    std::int64_t selectBeginNs=0,selectEndNs=0;
    bool selected=false;
    std::optional<interaction::AmmunitionSnapshot> selectedSnapshot;
    struct Publication {
        bool attempted=false,passed=false,viewPresent=false;
        std::int64_t beginNs=0,endNs=0;
        std::optional<AmmoResourceBinding> binding;
        std::optional<interaction::AmmunitionSnapshot> snapshot;
    } selectPublication{},updatePublication{};
    int updateBoundary=-1,updateProbeLock=-1;
    bool freshPublication=false;
};
// Wrap the ORIGINAL failing operand, at its original short-circuit position.
// Inactive capture adds no reads and returns exactly the supplied result.
inline bool AmmoPublisherFails(AmmoPublisherObservation* row,const char* name,bool failed)noexcept {
    if(row&&failed&&!row->failure)row->failure=name;
    return failed;
}
inline bool AmmoPublisherFreshPublication(const AmmoPublisherObservation& row,
    const AmmoPublisherObservation::Publication& p)noexcept {
    return row.selected&&row.selectedSnapshot&&p.passed&&p.binding&&p.snapshot&&
        p.binding->owner==row.input.identity.owner&&p.snapshot->context==row.selectedSnapshot->context&&
        p.snapshot->sequence==row.selectedSnapshot->sequence&&
        p.endNs>=p.binding->observedNs&&p.endNs<p.binding->deadlineNs&&
        p.endNs>=p.snapshot->observedNs&&p.endNs<p.snapshot->deadlineNs;
}
// Used under DiagnosticObservationWindow's ONE dedicated try-lock. Eight gap
// episodes retain their preceding fresh sample, exact first/latest refusal, and
// first new Select/publication. Intentional client "await server" is progress.
struct AmmoPublisherRetention {
    static constexpr unsigned Capacity=8;
    struct Gap {
        std::optional<AmmoPublisherObservation> precedingAccepted,firstFailure,latestFailure,firstSelect,firstPublication;
    };
    std::array<Gap,Capacity> gaps{};
    std::optional<AmmoPublisherObservation> lastFresh;
    std::uint64_t episodes=0,overwritten=0,accepted=0,selections=0,republished=0,outOfOrder=0,scopeChanges=0;
    std::int64_t lastEventNs=0;
    unsigned next=0,count=0;
    bool open=false;
    static bool SameScope(const AmmoPublisherObservation& a,const AmmoPublisherObservation& b)noexcept {
        return a.leaseIdentity==b.leaseIdentity&&a.leaseSnapshot.config==b.leaseSnapshot.config;
    }
    template<class Entry> void Observe(const Entry& entry,bool rejected)noexcept {
        const auto& row=entry.value;
        const auto eventNs=row.failureNs?row.failureNs:row.selectBeginNs?row.selectBeginNs:row.endNs;
        if(eventNs<lastEventNs)++outOfOrder;
        if(eventNs>lastEventNs)lastEventNs=eventNs;
        if(row.selected)++selections;
        if(row.updatePublication.passed&&!row.freshPublication)++republished;
        if(rejected&&open&&!SameScope(row,*gaps[(next+Capacity-1)%Capacity].firstFailure)){
            ++scopeChanges;open=false; // Retain the old unresolved episode.
        }
        if(rejected&&!open){
            auto& gap=gaps[next];gap={};
            if(lastFresh&&SameScope(row,*lastFresh))gap.precedingAccepted=lastFresh;
            gap.firstFailure=row;
            next=(next+1)%Capacity;if(count<Capacity)++count;else ++overwritten;
            ++episodes;open=true;
        }
        if(open&&SameScope(row,*gaps[(next+Capacity-1)%Capacity].firstFailure)){
            auto& gap=gaps[(next+Capacity-1)%Capacity];
            if(rejected)gap.latestFailure=row;
            if(row.selected&&!gap.firstSelect)gap.firstSelect=row;
            if(row.freshPublication&&!gap.firstPublication){gap.firstPublication=row;open=false;}
        }
        if(row.freshPublication){++accepted;lastFresh=row;}
    }
};
using AmmoPublisherObservationWindow=DiagnosticObservationWindow<AmmoPublisherObservation,AmmoPublisherRetention>;
}
