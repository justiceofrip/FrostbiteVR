#pragma once
#include "Bc2VehicleReticleSource.h"
#include "fvr/graphics/VehicleReticle.h"
#include "fvr/interaction/VehicleTracking.h"
#include <algorithm>
namespace fvr::bc2 {
struct VehicleReticleProducerContext {
    interaction::VehicleCameraSample input{};
    std::uint64_t inputSequence=0,scene=0,weaponGeneration=0;
    bool focused=false,playing=false;
};
struct VehicleReticleObservation {
    graphics::VehicleReticleSample visual{}; // proof remains Unproven
    VehicleSeatIdentity seat{};
    unsigned component=0,nativeWeapon=0,effects=0,firingData=0,primaryFire=0;
    std::uint64_t routeFingerprint=0;
};
namespace vehicle_reticle_producer_detail {
inline bool Input(const VehicleReticleProducerContext& c,const BoatAimSnapshot& a,
    const VehicleRouteSnapshot& r,std::int64_t now)noexcept {
    return a.owner==r.identity&&a.player==r.player&&a.owner.actor&&a.owner.actorGeneration&&a.owner.seatGeneration&&
        a.owner.controlled&&a.owner.entry&&a.owner.router&&a.owner.cache&&r.fingerprint&&r.slot==0&&
        a.weaponComponent>=0x10000&&a.nativeWeapon>=0x10000&&c.inputSequence&&c.scene&&c.weaponGeneration&&c.focused&&c.playing&&
        c.input.owner.generation==a.owner.actorGeneration&&interaction::AdmitVehicleCamera(c.input,
            a.owner.actor,a.owner.controlled,a.owner.entry,c.input.space,now)==interaction::VehicleCameraAdmission::Ready;
}
inline bool Ticket(const graphics::PairTicket& t)noexcept {
    bool session=false;for(auto v:t.session)session|=v!=0;
    return t.magic==graphics::TicketMagic&&t.version==graphics::TextureProtocol&&t.bytes==sizeof(t)&&
        !t.reserved0&&!t.reserved[0]&&!t.reserved[1]&&t.resourceEpoch&&t.sequence&&t.sequence<=INT64_MAX&&
        t.frameId&&t.spaceGeneration&&t.trackingGeneration&&t.predictedNs>0&&session;
}
inline bool SameTicket(const graphics::PairTicket& a,const graphics::PairTicket& b)noexcept {
    return Ticket(a)&&Ticket(b)&&a.resourceEpoch==b.resourceEpoch&&a.sequence==b.sequence&&a.frameId==b.frameId&&
        a.spaceGeneration==b.spaceGeneration&&a.trackingGeneration==b.trackingGeneration&&a.predictedNs==b.predictedNs&&a.session==b.session;
}
inline std::optional<math::Matrix4> Sight(const VehicleGunRayObservation& ray)noexcept {
    const auto& d=ray.direction;
    if(!std::isfinite(ray.origin.x)||!std::isfinite(ray.origin.y)||!std::isfinite(ray.origin.z)||
       !std::isfinite(d.x)||!std::isfinite(d.y)||!std::isfinite(d.z))return {};
    const double norm=double(d.x)*d.x+double(d.y)*d.y+double(d.z)*d.z;if(norm<.999||norm>1.001)return {};
    // Choose glyph roll only; +Z follows the measured native velocity vector.
    // Neither desired head direction nor a guessed native bore sign appears.
    math::Vec3 up=std::abs(d.y)>.95f?math::Vec3{1,0,0}:math::Vec3{0,1,0};
    math::Vec3 right{up.y*d.z-up.z*d.y,up.z*d.x-up.x*d.z,up.x*d.y-up.y*d.x};
    const float len=std::sqrt(right.x*right.x+right.y*right.y+right.z*right.z);if(!std::isfinite(len)||len<.01f)return {};
    right={right.x/len,right.y/len,right.z/len};up={d.y*right.z-d.z*right.y,d.z*right.x-d.x*right.z,d.x*right.y-d.y*right.x};
    math::Matrix4 m{};m.values[0]={right.x,right.y,right.z,0};m.values[1]={up.x,up.y,up.z,0};m.values[2]={d.x,d.y,d.z,0};m.values[3]={ray.origin.x,ray.origin.y,ray.origin.z,1};
    return graphics::VehicleReticleBasis(m)?std::optional(m):std::nullopt;
}
}
// Diagnostic producer seam: actual read-only reader feeds one frozen world ray
// per stereo pair. Production Append remains disabled pending the missing
// mounted-instance/shot-source monitor evidence. No runtime flag can grant it.
class VehicleReticleNativeProducer {
public:
    std::optional<VehicleReticleObservation> Observe(const VehicleRouteMemory& memory,
        const VehicleReticleSourceBinding& binding,const BoatAimSnapshot& aim,
        const VehicleRouteSnapshot& route,const VehicleReticleProducerContext& context,std::int64_t now)noexcept {
        using namespace vehicle_reticle_producer_detail;
        if(!Input(context,aim,route,now)||context.input.observedNs>INT64_MAX-100000000||
           (latest_&&latest_->seat==aim.owner&&(context.inputSequence<latest_->visual.inputSequence||
             context.input.observedNs<latest_->visual.observedNs))){latest_.reset();return {};}
        const auto ray=ReadPblDriverGunRayObservation(memory,binding,aim);if(!ray){latest_.reset();return {};}
        const auto sight=Sight(*ray);if(!sight){latest_.reset();return {};}
        graphics::VehicleReticleOwner owner{aim.owner.actor,aim.owner.actorGeneration,aim.owner.controlled,aim.owner.entry,
            aim.owner.seatGeneration,aim.nativeWeapon,context.weaponGeneration,context.scene,context.input.space};
        if(!latest_||latest_->visual.owner!=owner||latest_->component!=aim.weaponComponent||latest_->effects!=ray->effects||
           latest_->firingData!=ray->firingData||latest_->primaryFire!=ray->primaryFire||latest_->routeFingerprint!=route.fingerprint){if(epoch_==UINT64_MAX){latest_.reset();return {};}++epoch_;}
        const auto end=std::min(context.input.deadlineNs,context.input.observedNs+100000000);
        if(now>=end){latest_.reset();return {};}
        VehicleReticleObservation out;out.visual={owner,epoch_,context.inputSequence,*sight,context.input.observedNs,end,
            graphics::VehicleReticleProof::Unproven,true,context.playing,context.focused};
        out.seat=aim.owner;out.component=aim.weaponComponent;out.nativeWeapon=aim.nativeWeapon;
        out.effects=ray->effects;out.firingData=ray->firingData;out.primaryFire=ray->primaryFire;
        out.routeFingerprint=route.fingerprint;
        latest_=out;return out;
    }
    bool Begin(const graphics::PairTicket& ticket,std::int64_t now)noexcept {
        frozen_.reset();eyes_=0;if(!latest_||!vehicle_reticle_producer_detail::Ticket(ticket)||
            latest_->visual.observedNs>now||now>=latest_->visual.deadlineNs||
            ticket.spaceGeneration!=latest_->visual.owner.space||ticket.trackingGeneration!=latest_->visual.inputSequence||
            (hasTicket_&&ticket.session==ticket_.session&&ticket.resourceEpoch==ticket_.resourceEpoch&&
              (ticket.sequence<=ticket_.sequence||ticket.frameId<=ticket_.frameId)))return false;
        frozen_=latest_;ticket_=ticket;hasTicket_=true;return true;
    }
    bool Current(const BoatAimSnapshot& aim,const VehicleRouteSnapshot& route,
        const VehicleReticleProducerContext& input,const graphics::PairTicket& ticket,std::int64_t now)const noexcept {
        if(!frozen_||!latest_||!vehicle_reticle_producer_detail::SameTicket(ticket_,ticket)||
           !vehicle_reticle_producer_detail::Input(input,aim,route,now))return false;
        const auto& s=*frozen_;return now>=s.visual.observedNs&&now<s.visual.deadlineNs&&
            s.seat==aim.owner&&s.component==aim.weaponComponent&&s.nativeWeapon==aim.nativeWeapon&&s.routeFingerprint==route.fingerprint&&
            s.visual.owner.scene==input.scene&&s.visual.owner.space==input.input.space&&s.visual.owner.weaponGeneration==input.weaponGeneration&&
            input.inputSequence>=s.visual.inputSequence&&input.input.observedNs>=s.visual.observedNs&&
            latest_->visual.presentationEpoch==s.visual.presentationEpoch&&latest_->visual.owner==s.visual.owner;
    }
    std::optional<VehicleReticleObservation> Eye(const VehicleRouteMemory& memory,const VehicleReticleSourceBinding& binding,
        unsigned eye,const BoatAimSnapshot& aim,const VehicleRouteSnapshot& route,
        const VehicleReticleProducerContext& context,const graphics::PairTicket& ticket,std::int64_t now)noexcept {
        if(eye>1||(eyes_&(1u<<eye))||!Observe(memory,binding,aim,route,context,now)||
           !Current(aim,route,context,ticket,now)){frozen_.reset();eyes_=0;return {};}
        eyes_|=1u<<eye;return frozen_; // newer observations never move the old pair
    }
    bool Complete(const VehicleRouteMemory& memory,const VehicleReticleSourceBinding& binding,
        const BoatAimSnapshot& aim,const VehicleRouteSnapshot& route,const VehicleReticleProducerContext& context,
        const graphics::PairTicket& ticket,std::int64_t now)noexcept {
        const bool okay=eyes_==3&&Observe(memory,binding,aim,route,context,now)&&Current(aim,route,context,ticket,now);
        frozen_.reset();eyes_=0;return okay;
    }
    bool Append(graphics::BodyPropFrame& frame,const graphics::PairTicket& ticket,std::int64_t now)const noexcept {
        if(!VehicleReticleNativeShotSourceAdmitted||!frozen_||!latest_||eyes_!=3||!vehicle_reticle_producer_detail::SameTicket(ticket_,ticket))return false;
        return graphics::AppendVehicleReticle(frame,ticket,frozen_->visual,latest_->visual,now);
    }
    void Reset()noexcept{latest_.reset();frozen_.reset();eyes_=0;} // epoch never rewinds
private:
    std::uint64_t epoch_=0;unsigned eyes_=0;bool hasTicket_=false;graphics::PairTicket ticket_{};
    std::optional<VehicleReticleObservation> latest_,frozen_;
};
}
