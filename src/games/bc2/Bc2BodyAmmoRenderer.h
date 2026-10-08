#pragma once
#include "Bc2BeltPropProfiles.h"
#include "fvr/graphics/D3D11RigidPropRenderer.h"
#include "fvr/interaction/AmmoSupplyProp.h"
#include <atomic>
#include <memory>
#include <ostream>
#include <string>
#include <string_view>
namespace fvr::bc2 {
struct BodyAmmoTracking;
// Hashes select cached geometry; the exact names and rig below remain authority.
graphics::RigidPropGeometryKey MakeBodyAmmoGeometryKey(std::string_view asset,
    std::string_view mesh,std::string_view part,std::uint64_t rigFingerprint)noexcept;
struct BodyAmmoRenderSource {
    interaction::AmmoSupplyPropPose prop{};
    graphics::RigidPropGeometryKey geometry{};
    std::string asset,mesh,part;
    std::uint64_t rigFingerprint=0;
    std::shared_ptr<const BodyAmmoTracking> authority;
};
struct BodyAmmoGeometry {
    graphics::RigidPropGeometryKey key{};
    std::string asset,mesh,part;
    std::uint64_t rigFingerprint=0;
    std::vector<graphics::RigidPropSectionUpload> sections;
};
using BodyAmmoGeometryCatalog=std::vector<std::shared_ptr<const BodyAmmoGeometry>>;
struct BodyAmmoSectionBytes {
    const BeltPropSectionProfile* profile=nullptr;
    graphics::RigidPropDrawBytes bytes{}; // Borrowed only during CPU extraction.
    math::Matrix4 canonicalInverseBind{};
    graphics::RigidPropBasis basis=graphics::RigidPropBasis::NativeRightHanded;
    std::array<float,4> color{.3f,.3f,.3f,1};
};
// All geometry bytes come from the user's installed assets or exact copied draw.
// The immutable result owns independent vertices; no game assets belong in source.
std::shared_ptr<const BodyAmmoGeometry> BuildBodyAmmoGeometry(std::string_view asset,
    std::string_view mesh,std::string_view part,std::uint64_t rigFingerprint,
    std::span<const BodyAmmoSectionBytes>);
bool BodyAmmoGeometryMatches(const BodyAmmoGeometry&,const BodyAmmoRenderSource&)noexcept;
bool BodyAmmoSourceRetained(const BodyAmmoRenderSource&,const BodyAmmoRenderSource&,std::int64_t now)noexcept;
struct BodyAmmoEyeKey {
    std::uint64_t world=0,request=0,view=0,frame=0;
    unsigned eye=2;
    bool operator==(const BodyAmmoEyeKey&)const=default;
};
struct BodyAmmoTargetIdentity {
    std::uint64_t colorView=0,depthView=0,colorResource=0,depthResource=0;
    unsigned colorWidth=0,colorHeight=0,depthWidth=0,depthHeight=0;
    unsigned colorSamples=0,depthSamples=0,colorQuality=0,depthQuality=0;
    unsigned colorFormat=0,depthFormat=0;
    bool singleSliceMip=false,supportedViews=false,sameDevice=false;
    bool operator==(const BodyAmmoTargetIdentity&)const=default;
};
struct BodyAmmoBoundaryProof {
    BodyAmmoEyeKey eye{};BodyAmmoTargetIdentity target{};
    std::uint64_t immediateContext=0,queryObservationGeneration=0;
    std::int64_t observedNs=0,deadlineNs=0;
    math::Matrix4 eyeView{},projection{};
    // These are independent observations, never inferred from post-Draw timing.
    bool nativeDrawReturned=false,diagnosticQueryEnded=false;
    bool nativeQueriesObserved=false;unsigned activeScopedQueries=0;
    bool depthProjectionAssociated=false,reversedDepth=false;
};
enum BodyAmmoRenderMissing:unsigned {
    BodyAmmoNoSource=1u<<0,BodyAmmoChangedSource=1u<<1,BodyAmmoNoGeometry=1u<<2,
    BodyAmmoWrongEye=1u<<3,BodyAmmoNoColor=1u<<4,BodyAmmoNoDepth=1u<<5,
    BodyAmmoTargetLayout=1u<<6,BodyAmmoNoQueryProof=1u<<7,BodyAmmoQueryActive=1u<<8,
    BodyAmmoNoDepthProjectionProof=1u<<9,BodyAmmoStaleProof=1u<<10,
    BodyAmmoWrongThread=1u<<11,BodyAmmoBackendFailure=1u<<12,BodyAmmoStopped=1u<<13,
    BodyAmmoNotNativeAdmitted=1u<<14
};
unsigned BodyAmmoBoundaryMissing(const BodyAmmoBoundaryProof&,BodyAmmoEyeKey,
    const BodyAmmoTargetIdentity&,std::uint64_t context,std::int64_t now)noexcept;
// Only this adapter owns the renderer. CPU construction/reporting are harmless;
// actual GPU construction, upload, draw and destruction run on the first admitted
// immediate-context thread. Stop requests never release D3D objects on the caller.
class Bc2BodyAmmoRenderer {
public:
    Bc2BodyAmmoRenderer();~Bc2BodyAmmoRenderer();
    Bc2BodyAmmoRenderer(const Bc2BodyAmmoRenderer&)=delete;
    Bc2BodyAmmoRenderer& operator=(const Bc2BodyAmmoRenderer&)=delete;
    bool QueueGeometry(std::shared_ptr<const BodyAmmoGeometry>)noexcept;
    bool QueueGeometryCatalog(std::shared_ptr<const BodyAmmoGeometryCatalog>)noexcept;
    void EnableObservation(bool)noexcept;
    // Exact native call site: after original eye draw and its diagnostic query,
    // before CaptureTrackedEye. Empty proof records actual missing prerequisites.
    void EndEye(ID3D11DeviceContext*,BodyAmmoEyeKey,const BodyAmmoRenderSource* before,
        const BodyAmmoRenderSource* current,std::int64_t now,const BodyAmmoBoundaryProof&)noexcept;
    void RequestStop()noexcept;
    bool PumpStop()noexcept; // Called on graphics thread before hook retirement.
    bool Quiescent()const noexcept;
    void Report(std::ostream&)const;
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
} // namespace fvr::bc2
