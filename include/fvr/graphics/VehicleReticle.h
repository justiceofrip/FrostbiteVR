#pragma once
#include "fvr/graphics/BodyPropFrame.h"
#include <algorithm>
namespace fvr::graphics {
// Public synthetic glyph identity; never an installed mesh or native pointer.
inline constexpr RigidPropGeometryKey VehicleReticleGeometry{0x4656525652455431ull,0x41494d43524f5353ull,1};
struct VehicleReticleOwner {
    std::uint64_t actor=0,actorGeneration=0,vehicle=0,seat=0,seatGeneration=0,
        weapon=0,weaponGeneration=0,scene=0,space=0;
    bool operator==(const VehicleReticleOwner&)const=default;
};
enum class VehicleReticleProof : unsigned {Unproven,NativeSightLine};
struct VehicleReticleSample {
    VehicleReticleOwner owner{};
    // Render-only epoch advances on any complete owner identity change. The
    // native adapter owns this epoch; a pointer hash is not an epoch.
    std::uint64_t presentationEpoch=0,inputSequence=0;
    math::Matrix4 sightWorld{}; // adapter-proven +Z native aim, not desired HMD aim
    std::int64_t observedNs=0,deadlineNs=0;
    VehicleReticleProof proof=VehicleReticleProof::Unproven;
    bool armedSeat=false,playing=false,focused=false;
};
struct VehicleReticleStyle {
    // A display plane on the proven aiming ray, NOT a projectile impact point.
    float displayDistanceMeters=50,angularRadiusRadians=.0035f,worldUnitsPerMeter=1;
};
inline bool VehicleReticleBasis(const math::Matrix4& m)noexcept {
    if(!interaction::InverseRigid(m))return false;
    const auto& a=m.values;
    const float determinant=a[0][0]*(a[1][1]*a[2][2]-a[1][2]*a[2][1])-
        a[0][1]*(a[1][0]*a[2][2]-a[1][2]*a[2][0])+a[0][2]*(a[1][0]*a[2][1]-a[1][1]*a[2][0]);
    return std::isfinite(determinant)&&determinant>.98f&&determinant<1.02f;
}
inline bool VehicleReticleFresh(const VehicleReticleSample& s,std::int64_t now)noexcept {
    const auto& o=s.owner;
    return s.proof==VehicleReticleProof::NativeSightLine&&s.armedSeat&&s.playing&&s.focused&&
        o.actor&&o.actorGeneration&&o.vehicle&&o.seat&&o.seatGeneration&&o.weapon&&o.weaponGeneration&&o.scene&&o.space&&
        s.presentationEpoch&&s.inputSequence&&s.observedNs>0&&s.observedNs<=now&&s.deadlineNs>now&&
        s.deadlineNs-s.observedNs<=100000000&&VehicleReticleBasis(s.sightWorld);
}
inline bool VehicleReticleRetained(const VehicleReticleSample& original,
    const VehicleReticleSample& current,std::int64_t now)noexcept {
    return VehicleReticleFresh(original,now)&&VehicleReticleFresh(current,now)&&original.owner==current.owner&&
        original.presentationEpoch==current.presentationEpoch&&current.inputSequence>=original.inputSequence&&
        current.observedNs>=original.observedNs;
}
inline std::optional<math::Matrix4> VehicleReticleWorld(const VehicleReticleSample& s,
    const VehicleReticleStyle& style)noexcept {
    if(!std::isfinite(style.displayDistanceMeters)||style.displayDistanceMeters<1||style.displayDistanceMeters>1000||
       !std::isfinite(style.angularRadiusRadians)||style.angularRadiusRadians<.001f||style.angularRadiusRadians>.03f||
       !std::isfinite(style.worldUnitsPerMeter)||style.worldUnitsPerMeter<.01f||style.worldUnitsPerMeter>1000||
       !VehicleReticleBasis(s.sightWorld))return {};
    const float distance=style.displayDistanceMeters*style.worldUnitsPerMeter;
    const float scale=distance*std::tan(style.angularRadiusRadians);
    auto world=s.sightWorld;
    for(unsigned c=0;c<3;++c){world.values[3][c]+=distance*s.sightWorld.values[2][c];
        for(unsigned r=0;r<3;++r)world.values[r][c]*=scale;}
    return RigidPropWorldValid(world)?std::optional(world):std::nullopt;
}
// Projects the one shared native-ray display point independently for each eye.
// No edge clamping: a target behind/outside either eye is absent in both.
inline bool VehicleReticleInsideEye(const math::Matrix4& world,const BodyPropEye& eye)noexcept {
    const auto clip=RigidPropClipTransform(world,eye.view,eye.projection);if(!clip)return false;
    const auto& p=clip->values[3];if(!std::isfinite(p[3])||p[3]<=.0001f)return false;
    return p[0]>=-p[3]&&p[0]<=p[3]&&p[1]>=-p[3]&&p[1]<=p[3]&&p[2]>=0&&p[2]<=p[3];
}
// Transactional append to an already owned pair. Call after both native eyes
// have their exact matrices; rejection leaves all preexisting props untouched.
// Current BC2 capture does not yet grant NativeSightLine, so no native caller
// can enable this by treating camera/component correlation as a proof.
inline bool AppendVehicleReticle(BodyPropFrame& frame,const PairTicket& ticket,
    const VehicleReticleSample& original,const VehicleReticleSample& current,
    std::int64_t now,const VehicleReticleStyle& style={})noexcept {
    if(!BodyPropFrameMatches(frame,ticket)||!VehicleReticleRetained(original,current,now)||
       original.owner.space!=ticket.spaceGeneration||original.inputSequence!=ticket.trackingGeneration||
       frame.eyes[0].count!=frame.eyes[1].count||frame.eyes[0].count>=MaxBodyProps)return false;
    const auto world=VehicleReticleWorld(original,style);if(!world)return false;
    for(const auto& eye:frame.eyes){
        if(!VehicleReticleInsideEye(*world,eye))return false;
        for(unsigned n=0;n<eye.count;++n)if(eye.instances[n].geometry==VehicleReticleGeometry)return false;
    }
    const BodyPropInstance instance{VehicleReticleGeometry,*world,std::max(original.observedNs,current.observedNs),
        std::min(original.deadlineNs,current.deadlineNs),original.owner.actorGeneration,
        BodyPropWireEpoch(BodyPropSourceKind::VehicleReticle,original.presentationEpoch),original.owner.space};
    if(!BodyPropFresh(instance,now))return false;
    for(auto& eye:frame.eyes)eye.instances[eye.count++]=instance;
    return true;
}
} // namespace fvr::graphics
