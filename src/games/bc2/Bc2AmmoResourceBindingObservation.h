#pragma once
#include "Bc2AmmoResourceBinding.h"
#include <array>

namespace fvr::bc2 {
struct AmmoResourceBindingPublicationObservation {
    std::int64_t beginNs=0,endNs=0;
    AmmoResourceBindingPublishObservation publication{};
};
// Value-only retention, used under the existing fixed observation window flag.
// Preserve the exact first fixed refusal. A delayed completed Publish may join
// its revision, but its completion time is never proof of an earlier commit.
struct AmmoResourceBindingPublicationRetention {
    static constexpr unsigned Capacity=16;
    std::array<AmmoResourceBindingPublicationObservation,Capacity> recent{};
    std::optional<AmmoResourceBindingReadObservation> target;
    std::optional<AmmoResourceBindingPublicationObservation> matched;
    std::int64_t readBeginNs=0,readEndNs=0;
    std::uint64_t attempts=0,overwritten=0;
    unsigned next=0,count=0;
    bool firstFailureSeen=false;
    bool Matches(const AmmoResourceBindingPublicationObservation& row)const noexcept {
        return target&&target->reason==AmmoResourceBindingReadReason::Revision&&
            readBeginNs>0&&readEndNs>=readBeginNs&&target->gateHeld&&target->revisionRead&&target->storedRevision&&
            target->currentRevision>target->storedRevision&&
            row.publication.attemptedRevision==target->currentRevision&&
            row.publication.reason!=AmmoResourceBindingPublishReason::NotAttempted&&
            row.beginNs>0&&row.endNs>=row.beginNs&&row.beginNs<=readEndNs;
    }
    void ObserveAuxiliary(const AmmoResourceBindingPublicationObservation& row)noexcept {
        ++attempts;
        if(!matched&&Matches(row))matched=row;
        recent[next]=row;next=(next+1)%Capacity;
        if(count<Capacity)++count;else ++overwritten;
    }
    template<class Entry> void Observe(const Entry& entry,bool rejected)noexcept {
        if(!rejected||firstFailureSeen)return;
        firstFailureSeen=true;
        const auto& row=entry.value;
        if(row.bindingRead.reason!=AmmoResourceBindingReadReason::Revision)return;
        target=row.bindingRead;readBeginNs=row.bindingReadNs;readEndNs=row.bindingReadEndNs;
        for(unsigned n=0;n<count;++n)if(Matches(recent[n])){matched=recent[n];break;}
    }
    // Identity comparison deliberately excludes observation/deadline renewal.
    // Same identity is diagnostic evidence only, never a continuation decision.
    std::optional<bool> AttemptMatchesStoredIdentity()const noexcept {
        if(!target||!matched||!target->value||!matched->publication.attempted)return {};
        const auto& a=*target->value;const auto& b=*matched->publication.attempted;
        return a.owner==b.owner&&a.context==b.context&&a.inventory==b.inventory&&
            a.switching==b.switching&&a.data==b.data&&a.persistence==b.persistence;
    }
};
}
