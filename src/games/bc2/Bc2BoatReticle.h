#pragma once
#include "Bc2VehicleReticleProducer.h"
#include <mutex>
#include <atomic>
namespace fvr::bc2 {
struct BoatReticleShotWitness {
private:
    friend class BoatReticleRuntime;
    friend struct BoatReticleRuntimeTestAccess;
    VehicleReticleObservation source{};
    unsigned caller=0,config=0;
    BoatReticleShotWitness(VehicleReticleObservation s,unsigned c,unsigned f):source(s),caller(c),config(f){}
};
inline bool SameBoatReticleSource(const VehicleReticleObservation& a,const VehicleReticleObservation& b)noexcept {
    return a.visual.owner==b.visual.owner&&a.visual.presentationEpoch==b.visual.presentationEpoch&&a.seat==b.seat&&
        a.component==b.component&&a.nativeWeapon==b.nativeWeapon&&a.effects==b.effects&&a.firingData==b.firingData&&
        a.primaryFire==b.primaryFire&&a.routeFingerprint==b.routeFingerprint;
}
// A native Shoot callback supplies the missing runtime witness. This grants a
// sight-line display only, never firing/input/ammunition authority or an impact
// point. No new hook: the caller brackets its existing original-once callback.
class BoatReticleRuntime {
public:
    void Invalidate()noexcept {revocation_.fetch_add(1,std::memory_order_release);std::unique_lock lock(gate_,std::try_to_lock);if(!lock)return;
        producer_.Reset();current_.reset();witness_.reset();}
    void Publish(const VehicleRouteMemory& memory,const VehicleReticleSourceBinding& binding,
        const BoatAimSnapshot& aim,const VehicleRouteSnapshot& route,const VehicleReticleProducerContext& input,std::int64_t now)noexcept {
        std::unique_lock lock(gate_,std::try_to_lock);if(!lock)return;
        const auto revision=revocation_.load(std::memory_order_acquire);
        if(current_&&current_->revision!=revision){producer_.Reset();current_.reset();witness_.reset();}
        const auto next=producer_.Observe(memory,binding,aim,route,input,now,&diagnostic_);
        if(!next){current_.reset();witness_.reset();++rejected_;return;}
        if(witness_&&!SameBoatReticleSource(*witness_,*next))witness_.reset();
        current_=Current{*next,aim,route,revision};++observations_;
        if(witness_)current_->sample.visual.proof=graphics::VehicleReticleProof::NativeSightLine;
    }
    std::optional<graphics::VehicleReticleSample> Read(std::int64_t now)noexcept {
        std::unique_lock lock(gate_,std::try_to_lock);if(!lock||!current_||current_->revision!=revocation_.load(std::memory_order_acquire)||!graphics::VehicleReticleFresh(current_->sample.visual,now))return {};
        return current_->sample.visual;
    }
    std::optional<BoatReticleShotWitness> BeginShot(const VehicleRouteMemory& memory,const VehicleReticleSourceBinding& binding,
        const BoatAimBinding& aimBinding,std::uint64_t actor,unsigned controlled,unsigned entry,unsigned effects,unsigned config,
        unsigned caller,std::int64_t now)noexcept {
        std::unique_lock lock(gate_,std::try_to_lock);if(!lock||!current_||current_->revision!=revocation_.load(std::memory_order_acquire))return {};
        const auto& c=*current_;const auto& s=c.sample;
        if(!binding.verified||caller!=binding.image+0x408258||effects!=s.effects||std::uint64_t(s.primaryFire)+0xa0!=config||
           s.visual.owner.actor!=actor||s.visual.owner.vehicle!=controlled||s.visual.owner.seat!=entry||
           now<s.visual.observedNs||now>=s.visual.deadlineNs)return {};
        // Re-read native local-seat/camera/gun ownership and actual optional
        // shot branches at the callback, not merely the earlier gather sample.
        const auto aim=ReadPblDriverAim(memory,aimBinding,c.route);
        if(!aim||aim->owner!=c.aim.owner||aim->weaponComponent!=s.component||aim->nativeWeapon!=s.nativeWeapon)return {};
        const auto ray=ReadPblDriverGunRayObservation(memory,binding,*aim);
        if(!ray||ray->effects!=effects||ray->firingData!=s.firingData||ray->primaryFire!=s.primaryFire)return {};
        return BoatReticleShotWitness{s,caller,config};
    }
    bool EndShot(const BoatReticleShotWitness& before,const BoatReticleShotWitness& after)noexcept {
        std::unique_lock lock(gate_,std::try_to_lock);if(!lock||!current_||current_->revision!=revocation_.load(std::memory_order_acquire)||before.caller!=after.caller||before.config!=after.config||
           !SameBoatReticleSource(before.source,after.source)||!SameBoatReticleSource(current_->sample,after.source))return false;
        witness_=after.source;current_->sample.visual.proof=graphics::VehicleReticleProof::NativeSightLine;++witnesses_;return true;
    }
    struct Stats {std::uint64_t observations=0,witnesses=0,rejected=0;bool ready=false;VehicleReticleSourceDiagnostic source{};};
    Stats Statistics()noexcept {std::unique_lock lock(gate_,std::try_to_lock);return lock?Stats{observations_,witnesses_,rejected_,bool(witness_),diagnostic_}:Stats{};}
private:
    struct Current {VehicleReticleObservation sample;BoatAimSnapshot aim;VehicleRouteSnapshot route;std::uint64_t revision=0;};
    std::atomic<std::uint64_t> revocation_{1};
    std::mutex gate_;VehicleReticleNativeProducer producer_;
    std::optional<Current> current_;std::optional<VehicleReticleObservation> witness_;
    std::uint64_t observations_=0,witnesses_=0,rejected_=0;
    VehicleReticleSourceDiagnostic diagnostic_{};
};
}
