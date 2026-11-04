#pragma once
// Private read-only candidate for the mounted firing-update callback.
#include "Bc2ActiveEntryCamera.h"
#include "Bc2VehicleMountedObserver.h"
#include "Bc2VehicleReticleProducer.h"
#include <bit>
#include <cstring>

namespace fvr::bc2::mounted413 {
struct Binding { VehicleMountedBinding mounted{}; };

// Verify once before observer registration. Existing mounted proof includes
// the full client Shoot producer and its caller-owned firing context.
inline Binding Verify(const VehicleRouteMemory& m,unsigned image) noexcept {
    const auto mounted=VerifyVehicleMountedCode(m,image);
    if(!mounted.verified)return {};
    struct Span {unsigned rva,size;std::uint64_t hash;};
    constexpr Span proofs[]{
        {0x402290,203,0xc2a15310993e444aull}, {0x4eaaa0,941,0x9a305a0d366434dbull},
        {0x4082b0,70,0xcc3bbcef15622944ull}, {0x3f7700,43,0xae476662f7b5fc92ull},
        {0x480390,13,0x6359436605727c49ull}, {0x3be2b0,7,0x4da9bb0e987569f5ull},
        {0x3ccef0,10,0x3c82537eef83c85bull}, {0x3c61d0,35,0x93dafb6af04fe456ull},
        {0x405b30,4,0x1d6faef6ff900168ull}, {0x413da0,3,0x7b290c1b0b7f4d96ull},
        {0x3ed840,7,0xdf10c147af4a395aull}, {0x3ed9a0,7,0x49345bf8055cecfaull},
        {0x3810,91,0xafea8e480677f991ull}, {0x182e0,637,0x2fe633cf4b4606b6ull},
        {0x13db40,176,0x616ae5164b92640full}, {0x3870,241,0x98f6e38780fc828full},
        {0x3970,204,0xd5372c0078db7313ull}, {0x10d94f0,4,0x4b72477f9c5c2f98ull},
        {0x100a198,4,0xd41a841d370c349aull}, {0x100a19c,4,0x570323f6451d88cull},
        {0x100a0a0,4,0x4d25477f9dcdc418ull}
    };
    for(const auto& p:proofs){
        std::array<unsigned char,1024> data{};
        if(!m.read(m.context,image+p.rva,data.data(),p.size))return {};
        std::uint64_t hash=14695981039346656037ull;
        for(unsigned i=0;i<p.size;++i)hash=(hash^data[i])*1099511628211ull;
        if(hash!=p.hash)return {};
    }
    return {mounted};
}

namespace detail {
struct Reader {
    const VehicleRouteMemory& memory;
    bool valid=true;
    template<class T> T Get(std::uint64_t at) noexcept {
        T value{};
        if(!valid||at<0x10000||at>UINT32_MAX-sizeof(T)||
           !memory.read(memory.context,unsigned(at),&value,sizeof(T)))valid=false;
        return value;
    }
    unsigned Word(std::uint64_t at) noexcept {return Get<unsigned>(at);}
    unsigned Byte(std::uint64_t at) noexcept {return Get<unsigned char>(at);}
};
using MatrixWords=std::array<unsigned,16>;
enum class Basis {Mount,Camera,MountWithCameraPosition};
struct Native {
    Basis basis=Basis::Mount;
    unsigned descriptor=0,weak=0,wrapper=0,camera=0,component=0,selector=0;
    unsigned componentBegin=0,componentEnd=0,cameraData=0,flags=0;
    MatrixWords matrix{};
    bool operator==(const Native&)const=default;
};

// Narrow authored shape used by the AH60 guns and any equally qualified gun.
// The checked raw offsets are the actual client Shoot fields. Config+48 is
// ActiveForceSpawnToCamera, +49 ForceSpawnToCamera, +4A RelativeTargetAiming.
inline std::optional<Native> Read(const VehicleRouteMemory& memory,unsigned image,
    const VehicleShotSource& source,const VehicleRouteSnapshot& seat,const VehicleMountedSample& m) noexcept {
    Reader r{memory}; Native n;
    const std::uint64_t cfg=std::uint64_t(m.primary)+0xa0,e=m.effects,player=seat.player,entry=m.owner.entry;
    if(!memory.type||m.context[4]||m.context[5]||r.Word(e)!=image+0x1032300||
       r.Word(std::uint64_t(m.mounted)+0xb0)==std::uint64_t(m.mounted)+0xf4)return {};
    const auto firing=r.Get<std::array<unsigned,20>>(cfg);
    n.descriptor=firing[17];
    n.flags=firing[18]&0xffffff;
    if(!r.valid||!memory.type(memory.context,n.descriptor,"BulletEntityData")||
       firing[13]!=1||firing[16]||firing[15]||firing[0]||firing[1]||
       !std::isfinite(std::bit_cast<float>(firing[2]))||std::bit_cast<float>(firing[2])<=0||
       firing[4]||firing[5]||firing[6]!=0x3f800000||firing[8]||firing[9]||firing[10]||
       (n.flags!=0&&n.flags!=0x101)||
       (r.Byte(std::uint64_t(image)+0x1171369)&&r.Byte(std::uint64_t(m.data)+0x46)))return {};
    n.matrix=r.Get<MatrixWords>(e+0x30);
    if(n.flags){
        // Qualify only the native current local shooter, with both virtuals
        // resolved by exact targets. Unknown/absent camera owners fail closed.
        n.weak=r.Word(e+0x630);
        n.wrapper=r.Word(player+0xc88);
        const auto playerTable=r.Word(player),wrapperTable=r.Word(n.wrapper);
        if(!n.wrapper||r.Word(n.weak)!=std::uint64_t(n.wrapper)+4||
           wrapperTable!=image+0x1031934||r.Word(wrapperTable)!=image+0x405b30||
           r.Word(std::uint64_t(n.wrapper)+8)!=player||playerTable!=image+0x1031d3c||
           r.Word(std::uint64_t(playerTable)+4)!=image+0x413da0||
           r.Word(std::uint64_t(playerTable)+0x30)!=image+0x3ed840||
           r.Word(std::uint64_t(playerTable)+0x38)!=image+0x3ed9a0||
           r.Word(player+0xc68)!=m.owner.controlled||r.Word(player+0xc6c)!=seat.slot)return {};
        const auto entries=r.Word(std::uint64_t(m.owner.controlled)+0x7c);
        if(r.Word(std::uint64_t(entries)+std::uint64_t(seat.slot)*4)!=entry)return {};
        const auto active=ReadActiveEntryCamera(unsigned(entry),image,[&](unsigned at){return r.Word(at);});
        if(!active||active->camera!=source.profile.camera)return {};
        n.camera=active->camera;n.component=active->component;
        n.componentBegin=r.Word(entry+0x1b4);n.componentEnd=r.Word(entry+0x1b8);
        n.selector=r.Word(entry+0xd8);
        if(!n.componentBegin||n.componentEnd<=n.componentBegin||(n.componentEnd-n.componentBegin)%4||
           (n.componentEnd-n.componentBegin)/4>32)return {};
        const auto count=(n.componentEnd-n.componentBegin)/4,index=r.Word(n.selector);
        if(index>=count||r.Word(std::uint64_t(n.componentBegin)+4*index)!=n.component)return {};
        const bool positionOnly=(r.Byte(player+0xccd)&2)!=0;
        if(positionOnly){
            // This branch stops at the FIRST eligible camera irrespective of
            // selector. Refuse if another eligible camera precedes this one.
            for(unsigned i=0;i<index;++i){
                const auto candidate=r.Word(std::uint64_t(n.componentBegin)+4*i);
                if(candidate&&r.Byte(std::uint64_t(r.Word(std::uint64_t(candidate)+12))+0xcf))return {};
            }
        }
        n.cameraData=r.Word(std::uint64_t(n.component)+12);
        const auto collection=r.Word(std::uint64_t(n.component)+0x24);
        const auto slot=r.Byte(std::uint64_t(n.component)+0x28);
        if(!memory.type(memory.context,n.component,"ClientCameraComponent")||!r.Byte(std::uint64_t(n.cameraData)+0xcf)||
           collection!=source.profile.collection||slot>=source.profile.count||
           source.profile.rows[slot].component!=n.component||r.Byte(std::uint64_t(collection)+14)!=source.profile.layout)return {};
        const auto at=std::uint64_t(collection)+(source.profile.layout+1ull)*0x50+std::uint64_t(slot)*0x40;
        const auto camera=r.Get<MatrixWords>(at);
        if(positionOnly){std::copy_n(camera.begin()+12,3,n.matrix.begin()+12);n.basis=Basis::MountWithCameraPosition;}
        else {n.matrix=camera;n.basis=Basis::Camera;}
    }
    // Frostbite only copies xyz from each padded row.
    for(unsigned i=3;i<16;i+=4)n.matrix[i]=i==15?0x3f800000:0;
    return r.valid?std::optional{n}:std::nullopt;
}
} // namespace detail

// Called synchronously at the existing native firing-update boundary with
// the source/owner captured at entry, including neutral and prefire updates.
// Reacquire the route, exact mounted receiver, native matrix and source epoch
// at both brackets. A later gather or queued shot never refreshes this value.
inline std::optional<VehicleReticleObservation> ReadPrefire(const Binding& binding,
    const VehicleMountedApi& api,const VehicleShotSource& source,const OriginalFiringUpdate& call,
    const VehicleMountedSample& born,const VehicleReticleProducerContext& c,std::uint64_t presentationEpoch) noexcept {
    if(!binding.mounted.verified||!api.memory.read||!api.current||!api.seat||!api.clock||!api.stack||
       born.reason!=VehicleMountedReason::Okay||!presentationEpoch||!c.scene||!c.weaponGeneration||
       !c.playing||!c.focused||c.inputSequence!=source.input||c.input.space!=source.space||
       c.input.owner.generation!=born.owner.actorGeneration)return {};
    std::optional<detail::Native> previous;
    auto observed=std::int64_t{};
    for(unsigned i=0;i<2;++i){
        const auto now=api.clock(api.context);VehicleRouteSnapshot seat{};
        if(now<born.nowNs||now<observed||!api.current(api.context,source)||!api.seat(api.context,source,seat)||
           !CampaignVehiclePlaying(source.profile.profile,seat.permissionMask)||
           interaction::AdmitVehicleCamera(c.input,born.owner.actor,born.owner.controlled,born.owner.entry,source.space,now)!=
               interaction::VehicleCameraAdmission::Ready)return {};
        const auto m=ReadVehicleMounted(api.memory,binding.mounted,source,seat,call,now,api.stack(api.context,call.context,0x30));
        if(!VehicleMountedSame(born,m)||m.context!=born.context)return {};
        auto sample=detail::Read(api.memory,binding.mounted.image,source,seat,m);
        if(!sample||(previous&&*previous!=*sample)||!RevalidateCampaignVehicleProfile(api.memory,source.profile,seat))return {};
        if(!previous)observed=now;
        previous=sample;
    }
    const auto end=std::min({source.deadlineNs,c.input.deadlineNs,observed>INT64_MAX-100000000?INT64_MAX:observed+100000000});
    VehicleRouteSnapshot finalSeat{};
    if(!api.seat(api.context,source,finalSeat)||finalSeat.identity!=born.owner||
       !RevalidateCampaignVehicleProfile(api.memory,source.profile,finalSeat))return {};
    const auto finished=api.clock(api.context);
    if(!api.current(api.context,source)||finished<observed||finished>=end)return {};
    math::Matrix4 matrix{};std::memcpy(&matrix,previous->matrix.data(),sizeof(previous->matrix));
    if(!graphics::VehicleReticleBasis(matrix))return {};
    const auto& v=matrix.values;
    VehicleGunRayObservation ray{{v[3][0],v[3][1],-v[3][2]},{v[2][0],v[2][1],-v[2][2]},born.effects,born.data,born.primary};
    const auto sight=vehicle_reticle_producer_detail::Sight(ray);if(!sight)return {};
    const auto& s=born.owner;VehicleReticleObservation result{};
    result.visual={{s.actor,s.actorGeneration,s.controlled,s.entry,s.seatGeneration,born.mounted,c.weaponGeneration,c.scene,source.space},
        presentationEpoch,source.input,*sight,observed,end,graphics::VehicleReticleProof::Unproven,true,c.playing,c.focused};
    result.seat=s;result.component=born.component;result.nativeWeapon=born.mounted;result.effects=born.effects;
    result.firingData=born.data;result.primaryFire=born.primary;result.routeFingerprint=source.profile.profile.routeFingerprint;
    return result;
}
} // namespace fvr::bc2::mounted413
