#pragma once
#include "Bc2AmmoReserve.h"
#include "Bc2EquipmentIdentity.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <ostream>

namespace fvr::bc2 {
// Observations only. A committed input-cache write and a coherent stock count
// are neither a shot acknowledgement nor a measurement of rendered cadence.
enum class FireRouteWrite : std::uint8_t { NotAttempted, Refused, Applied };
enum class FireRouteAmmo : std::uint8_t { NotSampled, Unavailable, Valid, WrongOwner, InvalidLease, InvalidCounts };
enum FireRouteReason : std::uint32_t {
    FireRouteIncomplete=1u<<0, FireRouteNotOnFoot=1u<<1, FireRouteStaleInput=1u<<2,
    FireRouteNativeInactive=1u<<3, FireRouteInvalidInput=1u<<4, FireRouteFocusLost=1u<<5,
    FireRouteHeadMissing=1u<<6, FireRouteSemanticGate=1u<<7, FireRouteOwnerUnavailable=1u<<8,
    FireRouteContextRecheck=1u<<9, FireRouteBodyDraw=1u<<10, FireRouteMagazine=1u<<11,
    FireRoutePump=1u<<12, FireRouteBolt=1u<<13, FireRouteSight=1u<<14,
    FireRouteFixture=1u<<15, FireRoutePistolSlide=1u<<16, FireRouteFeed=1u<<17,
    FireRouteFixedContainer=1u<<18, FireRouteOtherFilter=1u<<19,
    FireRouteApplyRefused=1u<<20, FireRouteHolsterSuppression=1u<<21,
    FireRouteDetachedSuppression=1u<<22, FireRouteSelectedRoute=1u<<23,
    FireRouteRightAimMissing=1u<<24
};
enum FireRouteChange : std::uint32_t {
    FireRouteFirst=1u<<0, FireRouteOwnerChanged=1u<<1, FireRouteInputChanged=1u<<2,
    FireRouteMappingChanged=1u<<3, FireRouteWriteChanged=1u<<4,
    FireRouteAmmoChanged=1u<<5, FireRouteConfigurationChanged=1u<<6
};
struct FireRouteConfiguration {
    std::array<char,192> path{};
    std::uint32_t firingData=0,primaryFire=0;
    std::int32_t fireLogic=0,fireAction=0;
    bool observed=false;
    bool operator==(const FireRouteConfiguration&)const=default;
};
struct FireRouteRecord {
    ReloadStateOwner owner{};
    WeaponEquipmentIdentity equipment{};
    FireRouteConfiguration configuration{};
    std::uint64_t serial=0,input=0,inputEpoch=0,ammoSequence=0;
    std::int64_t gatherNs=0,sampledNs=0,inputObservedNs=0,inputDeadlineNs=0,inputAgeNs=-1;
    std::int64_t writeNs=0,ammoObservedNs=0,ammoDeadlineNs=0;
    std::uint32_t cache=0,nativeAction=8,reasons=FireRouteIncomplete,changes=0;
    std::int32_t loaded=-1,reserve=-1,capacity=-1;
    FireRouteWrite write=FireRouteWrite::NotAttempted;
    FireRouteAmmo ammo=FireRouteAmmo::NotSampled;
    bool onFoot=false,inputPresent=false,inputFresh=false,rightAimTracked=false,actionActive=false;
    bool requested=false,mapped=false,effective=false;
};

inline void ObserveFireRouteConfiguration(FireRouteRecord& r,const ReloadStateSnapshot& s)noexcept {
    if(s.owner!=r.owner||s.config.weaponData!=r.equipment.data)return;
    r.configuration={s.config.assetPath,s.config.firingData,s.config.primaryFire,
        s.config.fireLogicType,s.config.fireInputAction,true};
}
inline void ObserveFireRouteAmmo(FireRouteRecord& r,const std::optional<Bc2AmmoReserveLease>& a,
    std::int64_t now)noexcept {
    r.ammo=a?FireRouteAmmo::InvalidLease:FireRouteAmmo::Unavailable;
    r.loaded=r.reserve=r.capacity=-1;r.ammoSequence=0;r.ammoObservedNs=r.ammoDeadlineNs=0;
    if(!a)return;
    if(a->identity.owner!=r.owner){r.ammo=FireRouteAmmo::WrongOwner;return;}
    const auto& o=r.owner;
    if(!a->verified||!a->sequence||a->observedNs<=0||a->observedNs>now||a->deadlineNs<=now||
       a->deadlineNs-a->observedNs>250000000||o.player<0x10000||o.soldier<0x10000||
       o.weak<0x10000||o.weapon<0x10000||!o.actorGeneration||!o.equipGeneration||!o.space||
       a->identity.serverPlayer<0x10000||a->identity.serverSoldier<0x10000||a->identity.serverItem<0x10000||
       a->identity.firing[0]<0x10000||a->identity.firing[1]<0x10000||a->identity.firing[2]<0x10000)return;
    if(a->capacity<=0||a->capacity>1000000||a->loaded<0||a->loaded>a->capacity||a->reserve<0||a->reserve>1000000){
        r.ammo=FireRouteAmmo::InvalidCounts;return;
    }
    r.ammo=FireRouteAmmo::Valid;r.loaded=a->loaded;r.reserve=a->reserve;r.capacity=a->capacity;
    r.ammoSequence=a->sequence;r.ammoObservedNs=a->observedNs;r.ammoDeadlineNs=a->deadlineNs;
}

inline constexpr std::size_t FireRouteTelemetryCapacity=128;
struct FireRouteTelemetryBatch {
    std::array<FireRouteRecord,FireRouteTelemetryCapacity> records{};
    FireRouteRecord latest{};
    std::size_t count=0;
    std::uint64_t samples=0,events=0,overwritten=0,sampleLockDrops=0,drainLockMisses=0;
    bool hasLatest=false;
};
// Sampling never waits, allocates, serializes or performs a native read. Only
// transitions are retained; each drain also exposes the actual last sample's
// timestamp, never renewing an old input/ammo observation to the writer clock.
class FireRouteTelemetry {
public:
    bool Observe(FireRouteRecord r)noexcept {
        if(gate_.test_and_set(std::memory_order_acquire)){sampleLockDrops_.fetch_add(1,std::memory_order_relaxed);return false;}
        ++samples_;
        const bool sameOwner=haveLatest_&&r.owner==latest_.owner&&r.equipment==latest_.equipment;
        if(sameOwner&&!r.configuration.observed)r.configuration=latest_.configuration;
        r.inputAgeNs=r.inputPresent&&r.inputObservedNs>0&&r.sampledNs>=r.inputObservedNs?
            r.sampledNs-r.inputObservedNs:-1;
        r.changes=haveLatest_?0u:FireRouteFirst;
        if(haveLatest_&&!sameOwner)r.changes|=FireRouteOwnerChanged;
        if(haveLatest_&&(r.inputEpoch!=latest_.inputEpoch||r.onFoot!=latest_.onFoot||r.inputPresent!=latest_.inputPresent||
           r.inputFresh!=latest_.inputFresh||r.requested!=latest_.requested))r.changes|=FireRouteInputChanged;
        if(haveLatest_&&(r.mapped!=latest_.mapped||r.effective!=latest_.effective||r.reasons!=latest_.reasons||
           r.rightAimTracked!=latest_.rightAimTracked||r.actionActive!=latest_.actionActive||
           r.nativeAction!=latest_.nativeAction))r.changes|=FireRouteMappingChanged;
        if(haveLatest_&&r.write!=latest_.write)r.changes|=FireRouteWriteChanged;
        if(haveLatest_&&(r.ammo!=latest_.ammo||r.loaded!=latest_.loaded||r.reserve!=latest_.reserve||r.capacity!=latest_.capacity))
            r.changes|=FireRouteAmmoChanged;
        if(haveLatest_&&r.configuration!=latest_.configuration)r.changes|=FireRouteConfigurationChanged;
        if(r.changes){
            r.serial=++events_;records_[next_]=r;next_=(next_+1)%records_.size();
            if(count_<records_.size())++count_;else ++overwritten_;
        }else r.serial=latest_.serial;
        latest_=r;haveLatest_=true;gate_.clear(std::memory_order_release);return true;
    }
    bool TryDrain(FireRouteTelemetryBatch& out)noexcept {
        if(gate_.test_and_set(std::memory_order_acquire)){drainLockMisses_.fetch_add(1,std::memory_order_relaxed);return false;}
        out.count=count_;out.latest=latest_;out.hasLatest=haveLatest_;
        out.samples=samples_;out.events=events_;out.overwritten=overwritten_;
        out.sampleLockDrops=sampleLockDrops_.load(std::memory_order_relaxed);
        out.drainLockMisses=drainLockMisses_.load(std::memory_order_relaxed);
        for(std::size_t i=0;i<count_;++i)out.records[i]=records_[(next_+records_.size()-count_+i)%records_.size()];
        count_=0;gate_.clear(std::memory_order_release);return true;
    }
private:
    std::atomic_flag gate_=ATOMIC_FLAG_INIT;
    std::atomic<std::uint64_t> sampleLockDrops_{0},drainLockMisses_{0};
    std::array<FireRouteRecord,FireRouteTelemetryCapacity> records_{};
    FireRouteRecord latest_{};
    std::size_t next_=0,count_=0;
    std::uint64_t samples_=0,events_=0,overwritten_=0;
    bool haveLatest_=false;
};

inline void WriteFireRouteTelemetry(std::ostream& o,const FireRouteTelemetryBatch& b) {
    const auto text=[&](const auto& value){o<<'"';for(unsigned char c:value){if(!c)break;
        if(c=='"'||c=='\\')o<<'\\'<<char(c);else if(c>=32&&c<=126)o<<char(c);else o<<'?';}o<<'"';};
    const auto record=[&](const FireRouteRecord& r){
        o<<"{\"serial\":"<<r.serial<<",\"changes\":"<<r.changes<<",\"gather_ns\":"<<r.gatherNs<<",\"sampled_ns\":"<<r.sampledNs
         <<",\"input\":"<<r.input<<",\"input_epoch\":"<<r.inputEpoch<<",\"input_observed_ns\":"<<r.inputObservedNs
         <<",\"input_deadline_ns\":"<<r.inputDeadlineNs<<",\"input_age_ns\":"<<r.inputAgeNs
         <<",\"player\":"<<r.owner.player<<",\"soldier\":"<<r.owner.soldier<<",\"weak\":"<<r.owner.weak
         <<",\"weapon\":"<<r.owner.weapon<<",\"actor_generation\":"<<r.owner.actorGeneration
         <<",\"equip_generation\":"<<r.owner.equipGeneration<<",\"space\":"<<r.owner.space
         <<",\"weapon_data\":"<<r.equipment.data<<",\"persistence\":"<<r.equipment.persistence<<",\"asset\":";text(r.equipment.asset);
        o<<",\"configuration_observed\":"<<r.configuration.observed<<",\"configuration\":";text(r.configuration.path);
        o<<",\"firing_data\":"<<r.configuration.firingData<<",\"primary_fire\":"<<r.configuration.primaryFire
         <<",\"fire_logic\":"<<r.configuration.fireLogic<<",\"configured_fire_action\":"<<r.configuration.fireAction
         <<",\"cache\":"<<r.cache<<",\"native_action\":"<<r.nativeAction<<",\"on_foot\":"<<r.onFoot
         <<",\"input_present\":"<<r.inputPresent<<",\"input_fresh_at_gather\":"<<r.inputFresh
         <<",\"right_aim_tracked\":"<<r.rightAimTracked<<",\"action_active\":"<<r.actionActive
         <<",\"trigger_requested\":"<<r.requested<<",\"mapped_fire\":"<<r.mapped<<",\"effective_fire\":"<<r.effective
         <<",\"write\":"<<unsigned(r.write)<<",\"write_ns\":"<<r.writeNs<<",\"reason_mask\":"<<r.reasons
         <<",\"ammo_status\":"<<unsigned(r.ammo)<<",\"ammo_sequence\":"<<r.ammoSequence
         <<",\"ammo_observed_ns\":"<<r.ammoObservedNs<<",\"ammo_deadline_ns\":"<<r.ammoDeadlineNs<<",\"loaded\":";
        if(r.ammo==FireRouteAmmo::Valid)o<<r.loaded;else o<<"null";
        o<<",\"reserve\":";if(r.ammo==FireRouteAmmo::Valid)o<<r.reserve;else o<<"null";
        o<<",\"capacity\":";if(r.ammo==FireRouteAmmo::Valid)o<<r.capacity;else o<<"null";o<<'}';
    };
    o<<"{\"schema\":1,\"sample_scope\":\"matched_gameplay_gathers\",\"ammo_scope\":\"existing_coherent_stock_count_read\""
     <<",\"shot_acknowledgement\":false,\"render_cadence\":false,\"snapshot_available\":true"
     <<",\"write_states\":[\"not_attempted\",\"refused\",\"applied\"]"
     <<",\"ammo_states\":[\"not_sampled\",\"unavailable\",\"valid\",\"wrong_owner\",\"invalid_lease\",\"invalid_counts\"]"
     <<",\"reason_bits\":[\"incomplete_gather\",\"not_on_foot\",\"stale_input\",\"native_inactive\",\"invalid_input\",\"focus_lost\",\"head_missing\",\"semantic_gate\",\"owner_unavailable\",\"context_recheck\",\"body_draw\",\"magazine\",\"pump\",\"bolt\",\"sight\",\"fixture\",\"pistol_slide\",\"feed\",\"fixed_container\",\"other_filter\",\"apply_refused\",\"holster_suppression\",\"detached_suppression\",\"selected_route\",\"right_aim_missing\"]"
     <<",\"capacity\":"<<FireRouteTelemetryCapacity<<",\"samples\":"<<b.samples<<",\"events\":"<<b.events
     <<",\"overwritten\":"<<b.overwritten<<",\"sample_lock_drops\":"<<b.sampleLockDrops<<",\"drain_lock_misses\":"<<b.drainLockMisses
     <<",\"latest\":";if(b.hasLatest)record(b.latest);else o<<"null";
    o<<",\"records\":[";for(std::size_t i=0;i<b.count;++i){if(i)o<<',';record(b.records[i]);}o<<"]}";
}
} // namespace fvr::bc2
