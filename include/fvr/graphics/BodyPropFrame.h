#pragma once
#include "fvr/graphics/RigidPropFrame.h"
#include "fvr/graphics/SharedTextureProtocol.h"
#include "fvr/graphics/AmmoCounter.h"
#include <cstddef>
#include <type_traits>
namespace fvr::graphics {
inline constexpr unsigned MaxBodyProps=8;
// Wire-only identity namespace, never a gameplay/native equipment generation.
// Prevent dynamic packet offsets from matching equal geometry produced by two
// different source types. Slot ordinal also distinguishes identical carried
// models within a single immutable inventory cohort. Overflow rejects display.
enum class BodyPropSourceKind:std::uint8_t {Ammo=1,SelectedHolster=2,Carried=3,VehicleReticle=4};
inline constexpr std::uint64_t BodyPropWireEpoch(BodyPropSourceKind kind,std::uint64_t epoch,unsigned ordinal=0)noexcept {
    const auto k=static_cast<unsigned>(kind);
    return k>=1&&k<=4&&epoch&&epoch<=(UINT64_MAX>>7)&&ordinal<MaxBodyProps?
        (std::uint64_t(k)<<60)|(epoch<<3)|ordinal:0;
}
// Render-only IPC. Source timestamps use system QPC converted to nanoseconds,
// never OpenXR predicted time. No native addresses, ammo grants or mesh bytes.
struct alignas(8) BodyPropInstance {
    RigidPropGeometryKey geometry{};
    math::Matrix4 world{};
    std::int64_t observedNs=0,deadlineNs=0;
    std::uint64_t actorGeneration=0,equipmentGeneration=0,spaceGeneration=0;
};
struct alignas(8) BodyPropEye {
    math::Matrix4 view{},projection{};
    std::uint32_t count=0,reserved=0;
    std::array<BodyPropInstance,MaxBodyProps> instances{};
    AmmoCounterSample ammo{}; // Optional; invalid/absent telemetry hides only HUD.
};
struct alignas(8) BodyPropFrame {
    std::uint32_t version=2,bytes=2480;
    std::uint64_t frameId=0,spaceGeneration=0,trackingGeneration=0;
    std::array<BodyPropEye,2> eyes{};
};
static_assert(sizeof(BodyPropInstance)==128&&sizeof(BodyPropEye)==1224&&sizeof(BodyPropFrame)==2480);
static_assert(offsetof(BodyPropEye,ammo)==1160);
static_assert(std::is_trivially_copyable_v<BodyPropFrame> && offsetof(BodyPropFrame,eyes)==32);
inline bool BodyPropFresh(const BodyPropInstance& p,std::int64_t now)noexcept {
    return ValidRigidPropKey(p.geometry)&&p.actorGeneration&&p.equipmentGeneration&&p.spaceGeneration&&RigidPropWorldValid(p.world)&&p.observedNs>0&&
        p.observedNs<=now&&p.deadlineNs>now&&p.deadlineNs-p.observedNs<=100000000;
}
inline bool SameBodyPropSource(const BodyPropInstance& a,const BodyPropInstance& b)noexcept {
    return ValidRigidPropKey(a.geometry)&&a.actorGeneration&&a.equipmentGeneration&&a.spaceGeneration&&
        a.geometry==b.geometry&&a.actorGeneration==b.actorGeneration&&a.equipmentGeneration==b.equipmentGeneration&&
        a.spaceGeneration==b.spaceGeneration;
}
// Shape/identity validation does not renew source deadlines or permit gameplay.
inline bool BodyPropFrameMatches(const BodyPropFrame& f,const PairTicket& t)noexcept {
    if(f.version!=2||f.bytes!=sizeof(f)||!f.frameId||f.frameId!=t.frameId||
        !f.spaceGeneration||f.spaceGeneration!=t.spaceGeneration||
        !f.trackingGeneration||f.trackingGeneration!=t.trackingGeneration)return false;
    for(const auto& e:f.eyes){
        if(e.count>MaxBodyProps||e.reserved)return false;
        math::Matrix4 identity{};for(unsigned n=0;n<4;++n)identity.values[n][n]=1;
        if(e.count&&!RigidPropClipTransform(identity,e.view,e.projection))return false;
        for(unsigned n=0;n<e.count;++n){const auto& p=e.instances[n];
            if(p.spaceGeneration!=f.spaceGeneration||!BodyPropFresh(p,p.observedNs))return false;
        }
    }
    return true;
}
} // namespace fvr::graphics
