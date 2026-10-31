#pragma once
#include "Bc2AmmoReserve.h"
#include "Bc2EquipmentIdentity.h"
#include <array>
#include <atomic>
#include <cmath>
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
    FireRouteAmmoChanged=1u<<5, FireRouteConfigurationChanged=1u<<6,
    FireRouteInteractionChanged=1u<<7
};
// Callback-local values only. Unobserved consumers serialize as null; neither
// missing evaluation nor an enum's zero value is evidence of acceptance.
struct SupportRouteObservation {
    unsigned reason=0,cancelFlags=0,inputFlags=0,distanceBand=0;
    std::uint64_t token=0,previousToken=0;
    std::array<char,24> reset{};
    bool observed=false,evaluated=false,holding=false,engaged=false,released=false,contact=false;
    bool ownershipAttempted=false,ownershipAccepted=false,handBusy=false,cancel=false;
    bool operator==(const SupportRouteObservation&)const=default;
};
struct SightRouteObservation {
    unsigned phase=0,policyPhase=0,reason=0,packetReason=0,admissionFlags=0;
    std::uint64_t request=0;
    bool observed=false,evaluated=false,packetObserved=false,familyValid=false,adapterEnabled=false,contact=false,preferred=false;
    bool paired=false,advanced=false,grabbed=false,committed=false,cancelled=false,released=false;
    bool ownershipAttempted=false,ownershipAccepted=false,dispatchRejected=false,reset=false;
    bool operator==(const SightRouteObservation&)const=default;
};
struct ShellRouteObservation {
    unsigned cancelReason=~0u,supplyReason=~0u,flags=0,sourceFlags=0,bridgePhase=0;
    std::uint64_t cycle=0;
    bool observed=false,externalCancel=false,active=false,reloadHeld=false,ammoOwnsHand=false;
    bool operator==(const ShellRouteObservation&)const=default;
};
struct FeedRouteObservation {
    unsigned exit=0,flags=0,status=0,rawChecked=0,rawPassed=0,admissionChecked=0,admissionPassed=0;
    unsigned reloadPhase=~0u,operation=~0u;
    unsigned selectionFailure=~0u;
    unsigned idleChecked=0,idlePassed=0;
    bool observed=false,cancel=false,suspended=false,policyEvaluated=false;
    bool operator==(const FeedRouteObservation&)const=default;
};
struct InteractionRouteObservation {
    SupportRouteObservation support{};
    SightRouteObservation sight{};
    ShellRouteObservation shell{};
    FeedRouteObservation feed{};
    bool operator==(const InteractionRouteObservation&)const=default;
};
inline bool SameInteractionOutcome(InteractionRouteObservation a,InteractionRouteObservation b)noexcept {
    // Packet duplication is provenance, not a new accepted/rejected gesture.
    a.sight.advanced=b.sight.advanced=false;
    if(a.sight.packetReason==1)a.sight.packetReason=0;
    if(b.sight.packetReason==1)b.sight.packetReason=0;
    // Source advancement is provenance; repeated neutral packets are not new
    // startup outcomes. The actual bit remains available in records/latest.
    a.feed.idlePassed&=~(1u<<2);b.feed.idlePassed&=~(1u<<2);
    return a==b;
}
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
    InteractionRouteObservation interactions{};
    // Measurements/provenance update latest but do not create a transition on
    // every pose/controller packet. Optional measurements stay null if absent.
    std::optional<float> supportDistance,leftSqueeze,sightDistance,sightAngle;
    std::optional<float> feedCoverDistance,feedCoverAngle;
    std::int64_t feedNeutralAgeNs=-1;
    std::uint64_t supportSource=0,sightSource=0,shellSource=0,feedSource=0;
    std::int64_t supportSourceDeadlineNs=0,sightSourceDeadlineNs=0,shellSourceDeadlineNs=0,feedSourceDeadlineNs=0;
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
        auto outcome=r.interactions;
        const auto feedStatus=(outcome.feed.status>>8)&255u,previousFeedStatus=(lastOutcome_.feed.status>>8)&255u;
        // A duplicate is not a new policy result. Keep the previous result for
        // comparison, while latest_ still exposes the actual duplicate packet.
        if(sameOwner&&outcome.feed.observed&&lastOutcome_.feed.observed&&feedStatus==7&&previousFeedStatus<7)
            outcome.feed.status=(outcome.feed.status&~0xff00u)|(previousFeedStatus<<8);
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
        if(haveLatest_&&!SameInteractionOutcome(outcome,lastOutcome_))r.changes|=FireRouteInteractionChanged;
        if(r.changes){
            r.serial=++events_;records_[next_]=r;next_=(next_+1)%records_.size();
            if(count_<records_.size())++count_;else ++overwritten_;
        }else r.serial=latest_.serial;
        latest_=r;lastOutcome_=outcome;haveLatest_=true;gate_.clear(std::memory_order_release);return true;
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
    InteractionRouteObservation lastOutcome_{};
    std::size_t next_=0,count_=0;
    std::uint64_t samples_=0,events_=0,overwritten_=0;
    bool haveLatest_=false;
};

inline void WriteFireRouteTelemetry(std::ostream& o,const FireRouteTelemetryBatch& b) {
    const auto text=[&](const auto& value){o<<'"';for(unsigned char c:value){if(!c)break;
        if(c=='"'||c=='\\')o<<'\\'<<char(c);else if(c>=32&&c<=126)o<<char(c);else o<<'?';}o<<'"';};
    const auto number=[&](unsigned value){if(value==~0u)o<<"null";else o<<value;};
    const auto measure=[&](std::optional<float> value){if(value&&std::isfinite(*value))o<<*value;else o<<"null";};
    const auto interactions=[&](const FireRouteRecord& r){
        const auto& s=r.interactions.support;const auto& v=r.interactions.sight;
        const auto& h=r.interactions.shell;const auto& f=r.interactions.feed;
        o<<"{\"support\":";if(!s.observed)o<<"null";else{
            o<<"{\"evaluated\":"<<s.evaluated<<",\"reason\":";number(s.evaluated?s.reason:~0u);
            o<<",\"cancel_flags\":"<<s.cancelFlags<<",\"input_flags\":"<<s.inputFlags
             <<",\"distance_band\":"<<s.distanceBand<<",\"holding\":"<<s.holding<<",\"engaged\":"<<s.engaged<<",\"released\":"<<s.released
             <<",\"contact\":"<<s.contact<<",\"cancel\":"<<s.cancel<<",\"hand_busy\":"<<s.handBusy
             <<",\"ownership_attempted\":"<<s.ownershipAttempted<<",\"ownership_accepted\":"<<s.ownershipAccepted
             <<",\"token\":"<<s.token<<",\"previous_token\":"<<s.previousToken<<",\"reset\":";text(s.reset);
            o<<",\"distance_m\":";measure(r.supportDistance);o<<",\"squeeze\":";measure(r.leftSqueeze);
            o<<",\"source_sequence\":"<<r.supportSource<<",\"source_deadline_ns\":"<<r.supportSourceDeadlineNs<<'}';}
        o<<",\"sight\":";if(!v.observed)o<<"null";else{
            o<<"{\"evaluated\":"<<v.evaluated<<",\"phase\":"<<v.phase<<",\"policy_phase\":";number(v.evaluated||v.reset?v.policyPhase:~0u);
            o<<",\"reason\":";number(v.evaluated||v.reset?v.reason:~0u);o<<",\"packet_observed\":"<<v.packetObserved<<",\"packet_reason\":";number(v.packetObserved?v.packetReason:~0u);
            o<<",\"admission_flags\":"<<v.admissionFlags
             <<",\"family_valid\":"<<v.familyValid<<",\"adapter_enabled\":"<<v.adapterEnabled<<",\"contact\":"<<v.contact
             <<",\"preferred\":"<<v.preferred<<",\"paired\":"<<v.paired<<",\"advanced\":"<<v.advanced<<",\"grabbed\":"<<v.grabbed
             <<",\"committed\":"<<v.committed<<",\"cancelled\":"<<v.cancelled<<",\"released\":"<<v.released<<",\"request\":"<<v.request
             <<",\"ownership_attempted\":"<<v.ownershipAttempted<<",\"ownership_accepted\":"<<v.ownershipAccepted
             <<",\"dispatch_rejected\":"<<v.dispatchRejected<<",\"reset\":"<<v.reset<<",\"distance_m\":";measure(r.sightDistance);
            o<<",\"angle_radians\":";measure(r.sightAngle);o<<",\"source_sequence\":"<<r.sightSource
             <<",\"source_deadline_ns\":"<<r.sightSourceDeadlineNs<<'}';}
        o<<",\"shell\":";if(!h.observed)o<<"null";else{
            o<<"{\"external_cancel\":"<<h.externalCancel<<",\"cancel_reason\":";number(h.cancelReason);
            o<<",\"supply_reason\":";number(h.supplyReason);o<<",\"source_flags\":"<<h.sourceFlags;
            if(!h.externalCancel){o<<",\"flags\":"<<h.flags<<",\"bridge_phase\":";number(h.bridgePhase);
                o<<",\"cycle\":"<<h.cycle<<",\"active\":"<<h.active<<",\"reload_held\":"<<h.reloadHeld<<",\"ammo_owns_hand\":"<<h.ammoOwnsHand;}
            o<<",\"source_sequence\":"<<r.shellSource<<",\"source_deadline_ns\":"<<r.shellSourceDeadlineNs<<'}';}
        o<<",\"feed\":";if(!f.observed)o<<"null";else{
            o<<"{\"suspended\":"<<f.suspended;if(!f.suspended){o<<",\"exit\":"<<f.exit<<",\"flags\":"<<f.flags<<",\"cancel\":"<<f.cancel
             <<",\"equipment_status\":"<<(f.status&255u)<<",\"policy_evaluated\":"<<f.policyEvaluated<<",\"interaction_status\":";
                number(f.policyEvaluated?(f.status>>8)&255u:~0u);o<<",\"reload_status\":";number(f.policyEvaluated?(f.status>>16)&255u:~0u);
                o<<",\"hand_reason\":";number(f.policyEvaluated?f.status>>24:~0u);o<<",\"selection_failure\":";number(f.selectionFailure);
                o<<",\"admission_checked\":"<<f.admissionChecked<<",\"admission_passed\":"<<f.admissionPassed
             <<",\"raw_checked\":"<<f.rawChecked<<",\"raw_passed\":"<<f.rawPassed<<",\"reload_phase\":";number(f.reloadPhase);
                o<<",\"operation\":";number(f.operation);
                o<<",\"idle_checked\":"<<f.idleChecked<<",\"idle_passed\":"<<f.idlePassed;
                o<<",\"cover_distance_m\":";measure(r.feedCoverDistance);o<<",\"cover_angle_radians\":";measure(r.feedCoverAngle);
                o<<",\"neutral_age_ns\":";if(r.feedNeutralAgeNs<0)o<<"null";else o<<r.feedNeutralAgeNs;}
            o<<",\"source_sequence\":"<<r.feedSource<<",\"source_deadline_ns\":"<<r.feedSourceDeadlineNs<<'}';}
        o<<'}';
    };
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
        o<<",\"capacity\":";if(r.ammo==FireRouteAmmo::Valid)o<<r.capacity;else o<<"null";
        o<<",\"interactions\":";interactions(r);o<<'}';
    };
    o<<"{\"schema\":1,\"sample_scope\":\"matched_gameplay_gathers\",\"ammo_scope\":\"existing_coherent_stock_count_read\""
     <<",\"shot_acknowledgement\":false,\"render_cadence\":false,\"snapshot_available\":true"
     <<",\"write_states\":[\"not_attempted\",\"refused\",\"applied\"]"
     <<",\"ammo_states\":[\"not_sampled\",\"unavailable\",\"valid\",\"wrong_owner\",\"invalid_lease\",\"invalid_counts\"]"
     <<",\"interaction_schema\":1,\"interaction_change_bit\":128,\"interaction_scope\":\"callback_local_evaluations_null_if_unobserved\""
     <<",\"interaction_tables\":{"
       "\"support_reason\":[\"none\",\"tracking\",\"contact\",\"action\",\"identity\",\"button\",\"distance\",\"separation\",\"crossed\"],"
       "\"support_cancel_bits\":[\"sight\",\"aim_invalid\",\"inactive\",\"use\",\"next_weapon\",\"previous_weapon\",\"shell_hand\",\"magazine_busy\",\"body_draw\",\"fixed_ammo\",\"fixed_mechanism\",\"feed_hand\",\"pistol_slide_hand\",\"bolt_manipulating\",\"pump_hand\"],"
       "\"support_input_bits\":[\"focused\",\"head_tracked\",\"left_grip_tracked\",\"right_grip_tracked\",\"right_aim_tracked\",\"squeeze_at_least_0_7\"],"
       "\"support_distance_band\":[\"unavailable\",\"within_acquire\",\"within_retention\",\"outside_retention\"],"
       "\"sight_phase\":[\"idle\",\"manipulating\",\"awaiting_acknowledgement\",\"latched\"],"
       "\"sight_reason\":[\"none\",\"invalid_sample\",\"tracking_lost\",\"identity_changed\",\"clock_discontinuity\",\"stale_tracking\",\"contact_lost\",\"released\",\"native_mode_changed\",\"gesture_timeout\",\"acknowledgement_timeout\",\"invalid_geometry\"],"
       "\"sight_packet_reason\":[\"paired\",\"duplicate\",\"released\",\"invalid_input\",\"waiting_publication\",\"missing_history\",\"expired_publication\",\"native_acknowledgement\",\"latched_tracking\"],"
       "\"sight_admission_bits\":[\"fixed_ammo\",\"fixed_mechanism\",\"bolt_custody\",\"pump_hand\",\"shell_hand\",\"magazine_hand\",\"magazine_busy\",\"body_draw\"],"
       "\"shell_cancel_reason\":[\"external\",\"api_or_clock\",\"unsafe_input\",\"requested_input\",\"wrong_asset\",\"owner_changed\",\"missing_source\",\"full_magazine\",\"empty_reserve\",\"retiring\",\"intent_exhausted\",\"keepalive_rejected\",\"bridge_cancelled\",\"start_identity_rejected\",\"start_rejected\",\"hold_not_established\",\"manual_policy_cancelled\",\"native_owners_rejected\",\"native_lease_rejected\",\"submission_rejected\"],"
       "\"shell_supply_reason\":[\"none\",\"invalid_config\",\"invalid_input\",\"stale_input\",\"invalid_source\",\"source_changed\",\"tracking_lost\",\"released\",\"need_neutral\",\"outside_pouch\",\"reserve_exhausted\",\"hand_unavailable\",\"claim_lost\",\"pending_other_owner\",\"invalid_reservation\",\"invalid_receipt\",\"counter_exhausted\",\"explicit\"],"
       "\"shell_source_bits\":[\"body_draw\",\"fixture\",\"aim_invalid\",\"inactive\",\"fire\",\"use\",\"next_weapon\",\"previous_weapon\",\"no_pouch\",\"magazine_busy\",\"pump_debt\"],"
       "\"shell_bridge_phase\":[\"disabled\",\"idle\",\"pending\",\"complete\",\"cancelled\"],"
       "\"shell_flags\":[\"focused\",\"left_tracked\",\"right_tracked\",\"grip_pressed\",\"left_released\",\"requested_cancel\",\"safe_current_gun\",\"reserve_current\",\"selected_shotgun_mesh\",\"raw_valid\",\"raw_native_owner\",\"raw_physical_lease\",\"shotgun_rig\",\"body_pose_valid\",\"inside_primary\",\"inside_alternate\",\"gun_claim_present\",\"gun_claim_current\",\"input_fresh\",\"all_three_held\",\"retiring\",\"native_drained\",\"shell_held\",\"shell_pending\",\"blocks_current\",\"all_three_idle\",\"reload_input_ready\",\"supply_source\",\"belt_enabled\",\"alternate_contact\",\"unattributed_cancel\"],"
       "\"feed_exit\":[\"none\",\"updated\",\"api_unavailable\",\"view_unavailable\",\"selection_or_input_unverified\",\"equipment_storage_unavailable\",\"migration_unverified\",\"gun_or_raw_unverified\",\"lifetime_exhausted\",\"session_unavailable\",\"mechanism_blocked\",\"policy_or_hand_status\"],"
       "\"feed_flags\":[\"selected\",\"blocks_fire\",\"pending\",\"owns_hand\",\"submit_refused\",\"gate_failed\"],"
       "\"feed_admission_bits\":[\"view\",\"registration\",\"same_registration\",\"selected_proof\",\"not_cancelled\",\"input_window\"],"
       "\"feed_selection_failure\":[\"none\",\"registration_identity\",\"equipment_missing\",\"current_equipment\",\"binding_stale\",\"rig_unverified\",\"meshes_missing\",\"native_hand\",\"raw_invalid\",\"raw_owner\",\"raw_rig_soldier\",\"raw_rig_weak\",\"raw_rig_fingerprint\",\"raw_sequence\",\"raw_left_pose\",\"raw_weapon_pose\",\"native_view_stale\",\"native_owner\",\"mesh_owner\",\"mesh_sequence\",\"mesh_configuration_unverified\",\"mesh_configuration_pointer\",\"mesh_configuration_name\",\"mesh_weapon_name\",\"mesh_weapon_data\",\"mesh_inventory\",\"mesh_time_invalid\",\"mesh_time_future\",\"mesh_time_expired\",\"mesh_state_count\",\"mesh_configured_array_missing\",\"mesh_array_mismatch\",\"mesh_count_zero\",\"mesh_count_overflow\",\"mesh_match_count\"],"
       "\"feed_raw_bits\":[\"gun_claim\",\"source_packet\",\"source_native\",\"source_window\",\"source_after_native\",\"native_fresh\",\"same_item\",\"same_counts\",\"recorded_left_evidence\"],"
       "\"feed_idle_bits\":[\"hand_available\",\"released_neutral\",\"new_source_packet\",\"source_grip\",\"cover_distance\",\"cover_angle\",\"neutral_present\",\"source_after_neutral\",\"neutral_fresh\",\"loaded_below_capacity\",\"reserve_available\"],"
       "\"feed_equipment_status\":[\"unbound\",\"ready\",\"suspended\",\"preflight_rejected\",\"dispatched\",\"awaiting_receipt\",\"awaiting_corroboration\",\"receipt_rejected\",\"acknowledgement_rejected\",\"reconciled\",\"complete\",\"retire_needs_reconciliation\",\"retired\",\"invariant_mismatch\"],"
       "\"feed_interaction_status\":[\"need_neutral\",\"armed\",\"acquired\",\"moving\",\"endpoint\",\"returning\",\"completed\",\"duplicate\",\"invalid_profile\",\"invalid_input\",\"candidate_changed\",\"contact_miss\",\"interrupted\",\"expired\",\"off_path\",\"motion_jump\",\"counter_exhausted\"],"
       "\"feed_reload_status\":[\"armed\",\"awaiting_physical_completion\",\"dispatched\",\"awaiting_acknowledgement\",\"awaiting_snapshot\",\"reconciled\",\"complete\",\"no_physical_step\",\"suspended\",\"neutral_required\",\"invalid_lifetime\",\"invalid_sample\",\"lifetime_mismatch\",\"unverified_binding\",\"duplicate_packet\",\"duplicate_completion\",\"invalid_completion\",\"acknowledgement_mismatch\",\"invalid_acknowledgement\",\"snapshot_mismatch\",\"sequence_rollback\",\"clock_discontinuity\",\"mechanism_unavailable\",\"retire_needs_reconciliation\",\"retired\",\"token_exhausted\"],"
       "\"feed_hand_reason\":[\"none\",\"invalid_config\",\"invalid_sample\",\"clock_reversed\",\"sequence_rollback\",\"stale_input\",\"identity_changed\",\"focus_lost\",\"tracking_lost\",\"lease_expired\",\"invalid_request\",\"stale_intent\",\"wrong_owner\",\"contact_unavailable\",\"contact_mismatch\",\"missing_evidence\",\"hand_occupied\",\"item_occupied\",\"missing_prerequisite\",\"stale_token\",\"token_exhausted\",\"released\",\"transferred\",\"dependency_lost\",\"reset\"],"
       "\"feed_reload_phase\":[\"awaiting_neutral\",\"ready\",\"awaiting_acknowledgement\",\"awaiting_snapshot\",\"suspended\",\"complete\",\"retired\"],"
       "\"feed_operation\":[\"none\",\"release_latch\",\"open_cover\",\"withdraw_feed\",\"detach_container\",\"attach_container\",\"seat_feed\",\"close_cover\",\"engage_latch\",\"cycle_action\"]}"
     <<",\"reason_bits\":[\"incomplete_gather\",\"not_on_foot\",\"stale_input\",\"native_inactive\",\"invalid_input\",\"focus_lost\",\"head_missing\",\"semantic_gate\",\"owner_unavailable\",\"context_recheck\",\"body_draw\",\"magazine\",\"pump\",\"bolt\",\"sight\",\"fixture\",\"pistol_slide\",\"feed\",\"fixed_container\",\"other_filter\",\"apply_refused\",\"holster_suppression\",\"detached_suppression\",\"selected_route\",\"right_aim_missing\"]"
     <<",\"capacity\":"<<FireRouteTelemetryCapacity<<",\"samples\":"<<b.samples<<",\"events\":"<<b.events
     <<",\"overwritten\":"<<b.overwritten<<",\"sample_lock_drops\":"<<b.sampleLockDrops<<",\"drain_lock_misses\":"<<b.drainLockMisses
     <<",\"latest\":";if(b.hasLatest)record(b.latest);else o<<"null";
    o<<",\"records\":[";for(std::size_t i=0;i<b.count;++i){if(i)o<<',';record(b.records[i]);}o<<"]}";
}
} // namespace fvr::bc2
