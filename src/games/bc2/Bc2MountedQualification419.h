#pragma once
#include "Bc2MountedSight413.h"
#include "Bc2PrivateMountedUpdate419.h"
#include <mutex>
#include <ostream>

namespace fvr::bc2::mounted419 {
enum class Event:unsigned {Gather,Busy,Overlap,Token,Source,BeforeOwner,Reader,Candidate,
    LeaveChanged,Expired,Reread,AimChanged,Retained,Count};
inline constexpr const char* EventNames[]{"gathers","contention","overlap_or_obsolete_update","lost_token",
    "source_context_changed","before_owner_rejected","prefire_reader_rejected","prefire_candidates",
    "unwound_or_leave_owner_changed","original_deadline_expired","leave_reader_rejected","aim_changed_during_update","retained_prefire"};
static_assert(std::size(EventNames)==unsigned(Event::Count));
inline bool Live(const VehicleShotSource& source,const VehicleReticleProducerContext& c,std::int64_t now)noexcept {
    const auto& s=source.profile.owner;
    return source.observationEpoch&&source.space&&source.input&&source.input==c.inputSequence&&
        source.space==c.input.space&&source.observedNs==c.input.observedNs&&source.deadlineNs==c.input.deadlineNs&&
        c.scene&&c.weaponGeneration&&c.focused&&c.playing&&s.actorGeneration==c.input.owner.generation&&
        interaction::AdmitVehicleCamera(c.input,s.actor,s.controlled,s.entry,source.space,now)==interaction::VehicleCameraAdmission::Ready;
}
class Qualification {
    struct Candidate {
        std::uint64_t serial=0,update=0,gatherEpoch=0;
        VehicleReticleObservation sight{};
        std::int64_t inputObservedNs=0,inputDeadlineNs=0;
    };
public:
    void Configure(mounted413::Binding binding,VehicleMountedApi api)noexcept {binding_=binding;api_=api;}
    void Gather(std::uint64_t epoch,const VehicleReticleProducerContext& c)noexcept {
        std::unique_lock lock(mutex_,std::try_to_lock);
        if(!lock){Busy();return;}
        gatherEpoch_=epoch;context_=c;pending_.reset();live_.reset();Count(Event::Gather);
    }
    void Invalidate()noexcept {updateSequence.Revoke();}
    void Lost()noexcept {Count(Event::Token);Invalidate();}
    void Close()noexcept {closed_.store(true);Invalidate();}
    void Enter(std::uint64_t serial,const VehicleShotSource& source,const OriginalFiringUpdate& call,
               const VehicleMountedSample& before)noexcept {
        const auto ticket=updateSequence.Exclusive();
        if(!ticket){Count(Event::Overlap);return;}
        if(before.reason!=VehicleMountedReason::Okay){Count(Event::BeforeOwner);return;}
        std::unique_lock lock(mutex_,std::try_to_lock);
        if(!lock){Busy();return;}
        pending_.reset();live_.reset();
        if(closed_.load()||source.observationEpoch!=gatherEpoch_||!api_.current(api_.context,source)||
           !Live(source,context_,Now())){Count(Event::Source);return;}
        const auto candidate=mounted413::ReadPrefire(binding_,api_,source,call,before,context_,serial);
        if(!candidate){Count(Event::Reader);return;}
        if(updateSequence.Exclusive()!=ticket){Count(Event::Overlap);return;}
        pending_=Candidate{serial,ticket,source.observationEpoch,*candidate,source.observedNs,source.deadlineNs};Count(Event::Candidate);
    }
    void Leave(std::uint64_t serial,const VehicleShotSource& source,const OriginalFiringUpdate& call,
               const VehicleMountedSample& after,bool same)noexcept {
        std::unique_lock lock(mutex_,std::try_to_lock);
        if(!lock){Busy();return;}
        if(!pending_||pending_->serial!=serial)return;
        const auto e=*pending_;pending_.reset();
        if(!same||closed_.load()){Count(Event::LeaveChanged);return;}
        if(updateSequence.Exclusive()!=e.update){Count(Event::Overlap);return;}
        if(e.gatherEpoch!=gatherEpoch_||source.observationEpoch!=e.gatherEpoch||!api_.current(api_.context,source)){
            Count(Event::Source);return;
        }
        if(!Fresh(e,Now())){Count(Event::Expired);return;}
        const auto again=mounted413::ReadPrefire(binding_,api_,source,call,after,context_,serial);
        if(!again){Count(Event::Reread);return;}
        if(again->visual.sightWorld.values!=e.sight.visual.sightWorld.values){Count(Event::AimChanged);return;}
        if(!Fresh(e,Now())){Count(Event::Expired);return;}
        if(closed_.load()||!api_.current(api_.context,source)||updateSequence.Exclusive()!=e.update){Count(Event::Overlap);return;}
        live_=e;historical_=e;Count(Event::Retained);
    }
    // Diagnostic inspection only. The retained value never acquires visual
    // proof or a later source deadline. No production renderer reads it.
    std::optional<VehicleReticleObservation> Retained()noexcept {
        std::unique_lock lock(mutex_,std::try_to_lock);
        if(!lock||!live_||closed_.load()||!updateSequence.Current(live_->update))return {};
        VehicleShotSource current{};
        if(!api_.source(api_.context,current)||current.observationEpoch!=live_->gatherEpoch||
           !api_.current(api_.context,current)||!Live(current,context_,Now()))return {};
        VehicleRouteSnapshot seat{};
        if(!api_.seat(api_.context,current,seat)||seat.identity!=live_->sight.seat||
           !CampaignVehiclePlaying(current.profile.profile,seat.permissionMask)||
           !RevalidateCampaignVehicleProfile(api_.memory,current.profile,seat))return {};
        const auto& sight=live_->sight;
        const auto word=[&](unsigned base,unsigned offset,unsigned expected){
            unsigned value=0;
            return base>=0x10000&&std::uint64_t(base)+offset+4<=UINT32_MAX&&
                api_.memory.read(api_.memory.context,base+offset,&value,4)&&value==expected;
        };
        if(!word(sight.component,0xd0,sight.nativeWeapon)||!word(sight.component,0xcc,sight.effects)||
           !word(sight.nativeWeapon,0x134,sight.effects)||!word(sight.nativeWeapon,12,sight.firingData)||
           !word(sight.effects,16,sight.firingData)||!word(sight.firingData,64,sight.primaryFire))return {};
        if(!Fresh(*live_,Now())||!api_.current(api_.context,current)||!updateSequence.Current(live_->update))return {};
        return live_->sight;
    }
    void Report(std::ostream& out){
        const bool live=api_.clock&&Retained().has_value();
        std::unique_lock lock(mutex_,std::try_to_lock);
        out<<"{\"private_setup_only\":true,\"bullet_dispatch_hook\":false,\"render_proof_granted\":false,\"live_prefire_at_read\":"<<(live?"true":"false")
           <<",\"open_updates\":"<<updateSequence.Depth()<<",\"counts\":{";
        for(unsigned i=0;i<counts_.size();++i){if(i)out<<',';out<<'"'<<EventNames[i]<<"\":"<<counts_[i].load();}
        out<<"},\"snapshot_available\":"<<bool(lock)<<",\"last_historical_prefire\":";
        if(!lock||!historical_)out<<"null";
        else {
            const auto& e=*historical_;const auto& c=e.sight;const auto& v=c.visual;const auto& s=c.seat;
            out<<"{\"update\":"<<e.update<<",\"source_epoch\":"<<e.gatherEpoch<<",\"serial\":"<<e.serial
               <<",\"actor\":"<<s.actor<<",\"actor_generation\":"<<s.actorGeneration<<",\"seat_generation\":"<<s.seatGeneration
               <<",\"vehicle\":"<<s.controlled<<",\"entry\":"<<s.entry<<",\"router\":"<<s.router<<",\"cache\":"<<s.cache
               <<",\"scene\":"<<v.owner.scene<<",\"space\":"<<v.owner.space<<",\"weapon_generation\":"<<v.owner.weaponGeneration
               <<",\"input\":"<<v.inputSequence<<",\"mounted\":"<<c.nativeWeapon<<",\"component\":"<<c.component
               <<",\"effects\":"<<c.effects<<",\"firing_data\":"<<c.firingData<<",\"primary\":"<<c.primaryFire
               <<",\"input_observed_ns\":"<<e.inputObservedNs<<",\"input_deadline_ns\":"<<e.inputDeadlineNs
               <<",\"observed_ns\":"<<v.observedNs<<",\"deadline_ns\":"<<v.deadlineNs;
            const auto vec=[&](unsigned row){const auto& x=v.sightWorld.values[row];out<<'['<<x[0]<<','<<x[1]<<','<<x[2]<<']';};
            out<<",\"origin\":";vec(3);out<<",\"forward\":";vec(2);out<<'}';
        }
        out<<'}';
    }
private:
    static bool Fresh(const Candidate& e,std::int64_t now)noexcept {
        return now>=e.sight.visual.observedNs&&now<e.sight.visual.deadlineNs;
    }
    std::int64_t Now()const noexcept{return api_.clock(api_.context);}
    void Count(Event event)noexcept{counts_[unsigned(event)].fetch_add(1,std::memory_order_relaxed);}
    void Busy()noexcept{Count(Event::Busy);Invalidate();}
    mounted413::Binding binding_{};VehicleMountedApi api_{};
    std::mutex mutex_;std::atomic<bool> closed_{false};
    std::uint64_t gatherEpoch_=0;VehicleReticleProducerContext context_{};
    std::optional<Candidate> pending_,live_,historical_;
    std::array<std::atomic<std::uint64_t>,unsigned(Event::Count)> counts_{};
};
} // namespace fvr::bc2::mounted419
