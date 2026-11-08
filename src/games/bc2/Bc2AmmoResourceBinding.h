#pragma once
#include "Bc2ReloadState.h"
#include "fvr/interaction/AmmunitionLedger.h"
#include <atomic>

namespace fvr::bc2 {
// Identity only, from the body inventory's coherent native read and existing
// item lifetime. No hand pose, render frame, count or mutation authority.
struct AmmoResourceBinding {
    ReloadStateOwner owner{};
    interaction::AmmoResourceContext context{};
    std::uint32_t inventory=0,switching=0,data=0,persistence=0;
    std::int64_t observedNs=0,deadlineNs=0;
    bool operator==(const AmmoResourceBinding&)const=default;
};
inline bool AmmoResourceBindingFresh(const AmmoResourceBinding& b,const ReloadStateOwner& owner,std::int64_t now)noexcept {
    const auto& c=b.context;const auto& r=c.resource;
    return b.owner==owner&&owner.player>=0x10000&&owner.soldier>=0x10000&&owner.weak>=0x10000&&owner.weapon>=0x10000&&
        owner.actorGeneration&&owner.equipGeneration&&owner.space&&r.actor==owner.soldier&&r.actorGeneration==owner.actorGeneration&&
        r.weapon==owner.weapon&&r.weaponGeneration&&c.equipGeneration==owner.equipGeneration&&c.space==owner.space&&
        b.inventory>=0x10000&&b.switching>=0x10000&&b.data>=0x10000&&
        b.observedNs>0&&now>=b.observedNs&&now<b.deadlineNs&&b.deadlineNs-b.observedNs<=100000000;
}
// Latest gather publication. A busy writer invalidates the previous sample,
// rather than leaving old item identity available during replacement.
// Optional diagnostic copy from this same read attempt; no new authority.
enum class AmmoResourceBindingReadReason:unsigned {
    NotAttempted,Available,Busy,Unpublished,Revision,NoBinding,Owner,Future,Expired,Invalid
};
struct AmmoResourceBindingReadObservation {
    AmmoResourceBindingReadReason reason=AmmoResourceBindingReadReason::NotAttempted;
    bool gateHeld=false,revisionRead=false;
    std::uint64_t storedRevision=0,currentRevision=0;
    std::optional<AmmoResourceBinding> value;
};
// These copies explain publication attempts, including attempts that invalidated
// the revision but never committed. They never supply binding authority.
enum class AmmoResourceBindingPublishReason:unsigned {
    NotAttempted,Published,Busy,Superseded,RevisionOverflow
};
struct AmmoResourceBindingPublishObservation {
    AmmoResourceBindingPublishReason reason=AmmoResourceBindingPublishReason::NotAttempted;
    std::uint64_t attemptedRevision=0,currentRevision=0,previousRevision=0;
    bool gateHeld=false,revisionRead=false,previousCopied=false;
    std::optional<AmmoResourceBinding> attempted,previous;
};
class AmmoResourceBindingChannel {
public:
    bool Publish(const std::optional<AmmoResourceBinding>& binding,
        AmmoResourceBindingPublishObservation* observation=nullptr)noexcept {
        if(observation){*observation={};observation->attempted=binding;}
        const auto revision=revision_.fetch_add(1,std::memory_order_acq_rel)+1;
        if(observation)observation->attemptedRevision=revision;
        if(!revision){if(observation)observation->reason=AmmoResourceBindingPublishReason::RevisionOverflow;return false;}
        Gate gate(gate_);
        if(!gate.held){if(observation)observation->reason=AmmoResourceBindingPublishReason::Busy;return false;}
        const auto current=revision_.load(std::memory_order_acquire);
        if(observation){
            observation->gateHeld=observation->revisionRead=observation->previousCopied=true;
            observation->currentRevision=current;observation->previousRevision=storedRevision_;
            observation->previous=value_; // Existing channel gate only; no second read or lock.
        }
        if(revision!=current){if(observation)observation->reason=AmmoResourceBindingPublishReason::Superseded;return false;}
        value_=binding;storedRevision_=revision;
        if(observation)observation->reason=AmmoResourceBindingPublishReason::Published;return true;
    }
    std::optional<AmmoResourceBinding> Read(const ReloadStateOwner& owner,std::int64_t now,
        AmmoResourceBindingReadObservation* observation=nullptr)noexcept {
        if(observation)*observation={};
        const auto reject=[&](AmmoResourceBindingReadReason reason)->std::optional<AmmoResourceBinding>{
            if(observation)observation->reason=reason;return {};
        };
        Gate gate(gate_);
        if(!gate.held)return reject(AmmoResourceBindingReadReason::Busy);
        if(observation){observation->gateHeld=true;observation->storedRevision=storedRevision_;}
        if(!storedRevision_)return reject(AmmoResourceBindingReadReason::Unpublished);
        const auto revision=revision_.load(std::memory_order_acquire);
        if(observation){
            observation->revisionRead=true;observation->currentRevision=revision;
            observation->value=value_; // Protected stored value is DIAGNOSTIC ONLY on Revision.
        }
        if(storedRevision_!=revision)return reject(AmmoResourceBindingReadReason::Revision);
        if(!value_)return reject(AmmoResourceBindingReadReason::NoBinding);
        if(!AmmoResourceBindingFresh(*value_,owner,now)){
            // Classification uses the already locked value. It never retries.
            const auto reason=value_->owner!=owner?AmmoResourceBindingReadReason::Owner:
                now<value_->observedNs?AmmoResourceBindingReadReason::Future:
                now>=value_->deadlineNs?AmmoResourceBindingReadReason::Expired:AmmoResourceBindingReadReason::Invalid;
            return reject(reason);
        }
        if(observation)observation->reason=AmmoResourceBindingReadReason::Available;
        return value_;
    }
private:
    friend struct AmmoResourceBindingChannelTestAccess;
    struct Gate {std::atomic_flag& flag;bool held;
        explicit Gate(std::atomic_flag& f):flag(f),held(!f.test_and_set(std::memory_order_acquire)){}
        ~Gate(){if(held)flag.clear(std::memory_order_release);}};
    std::atomic_flag gate_=ATOMIC_FLAG_INIT;std::atomic<std::uint64_t> revision_=0;
    std::uint64_t storedRevision_=0;std::optional<AmmoResourceBinding> value_;
};
}
