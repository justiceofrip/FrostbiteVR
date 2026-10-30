#pragma once
#include "fvr/interaction/PhysicalPistolSlide.h"
#include "fvr/interaction/FeedInteraction.h"
#include <string_view>

namespace fvr::bc2 {
// Exact configured asset evidence. These identifiers select geometry only;
// they never supply a native chamber, reload or firing capability.
struct Bc2PistolSlideSelection {
    std::string_view asset,configurationPath,configurationGuid,configurationSha256,meshPath;
    std::uint64_t rigFingerprint=0;
    bool operator==(const Bc2PistolSlideSelection&)const=default;
};
struct Bc2Mp443SlideGeometry {
    Bc2PistolSlideSelection selection{};
    std::string_view evidencePath,geometrySha256,roleReviewPath,linearHelperSha256;
    std::string_view root,slide;
    std::uint32_t rootIndex=0,slideIndex=0;
    // Canonical reflected-Z row-vector METRES. This static comparison frame
    // is neither a native closed stop nor the minimum authored displacement.
    math::Matrix4 weaponFromStaticSlide{},slideFromAuthoredWrist{};
    std::array<float,3> rearward{},partBoundsMin{},partBoundsMax{};
    std::array<float,3> ring3Part{},nearestVertexPart{};
    double minimumDisplacementMeters=0,maximumDisplacementMeters=0;
    double authoredLandmarkSeconds=0,ring3VertexDistanceMeters=0;
    bool nativeClosedVerified=false,nativeRetractedVerified=false,stableControllerGraspVerified=false;
};
// Distinct mod interaction policy. The source is the authored 3.0s wrist
// transported with the slide to its static comparison, NOT a native chamber
// boundary. All thresholds below are mod choices, not authored tolerances.
struct Bc2Mp443ModSlideInteractionPolicy {
    std::string_view name,sourceReference;
    std::uint64_t id=0,revision=0;
    float sourceDisplacementMeters=0,strokeMeters=0;
    float frontToleranceMeters=0,rearToleranceMeters=0,hysteresisMeters=0;
    float contactRadiusMeters=0,lateralToleranceMeters=0,rotationToleranceRadians=0,maxStepMeters=0;
    std::int64_t endpointDwellNs=0,maximumStrokeNs=0;
};
namespace bc2_pistol_slide_profile_detail {
constexpr math::Matrix4 Frame(std::array<float,16> values)noexcept {
    math::Matrix4 out{};
    for(unsigned i=0;i<4;++i)for(unsigned j=0;j<4;++j)out.values[i][j]=values[i*4+j];
    return out;
}
inline constexpr Bc2Mp443SlideGeometry Mp443Geometry{
    {"MP443_sp","Objects/Weapons/Handheld/RU_hg_MP443Grach/SP_hg_MP443Grach",
     "58098858-821b-4406-a19c-ae62e5d38bdf",
     "e16f5713854ca38ab58d61bd3c568e2f87533c0e82032d71d61f27449d5484cc",
     "Objects/Weapons/Handheld/RU_hg_MP443Grach/RU_hg_MP443Grach_Mesh",0xa7f219a1426216abull},
    "handoff/tasks/CHAT16/20261030-implementation17/mp443-geometry.json",
    "09314649460c4b0d81bd0138a39d44f3680c0fed4e485b353ae66fe7c2b3638c",
    "handoff/tasks/CHAT16/20261030-implementation17/mp443-role-review.json",
    "c82762805550a36628f7e3fd91699129eb63d95e142f1976c6579c07da890a0f",
    "jntWpn_1","jntWpn_3",10,30,
    Frame({1,0,0,0, 0,1,0,0, 0,0,1,0,
        -2.4863219039161777e-8f,.0802396759390831f,-.08735319972038269f,1}),
    // mp443-role-review.json authored_finger_landmarks[1], time_s = 3.0.
    Frame({-.8161363623421347f,.03144293098996938f,.5770032756871573f,0,
        .03882313651358668f,.9992459917088099f,.0004605704348747408f,0,
        -.5765537287488458f,.022776965220109434f,-.8167417019603969f,0,
        .09829838552760058f,-.024858002259805518f,.12061080385312578f,1}),
    {0,0,1},
    {-.011442961171287054f,-.0373541116714477f,-.07570183277130287f},
    {.011202645488066418f,.006363868713378964f,.13051281869411338f},
    {-.009689049754664442f,-.00669222070481897f,.04665304955670779f},
    {-.009862856939451046f,-.007363677024841046f,.044900313019750816f},
    -.0019974632828737467,.07247133458104844,3.0,.0018849795476781619,
    false,false,false
};
inline constexpr Bc2Mp443ModSlideInteractionPolicy Mp443ModPolicy{
    "MP443 static-reference manual slide v1",
    "mp443-role-review.json: authored_finger_landmarks[1] wrist at 3.0s, transported to static slide reference",
    0x4d50343433534c31ull,1,
    0,.065f,.001f,.003f,.00075f,.035f,.025f,.35f,.025f,
    30000000,8000000000ll
};
}
// Deliberately no alias, pistol-family, mesh-only or rig-only fallback.
[[nodiscard]] inline const Bc2Mp443SlideGeometry* FindBc2Mp443SlideGeometry(
    const Bc2PistolSlideSelection& selection)noexcept {
    const auto& g=bc2_pistol_slide_profile_detail::Mp443Geometry;
    return selection==g.selection?&g:nullptr;
}
[[nodiscard]] inline std::optional<math::Matrix4> Bc2Mp443SlideLocalFrame(
    const Bc2PistolSlideSelection& selection,float signedDisplacementMeters)noexcept {
    const auto* g=FindBc2Mp443SlideGeometry(selection);
    // Compare to float endpoints so the representable measured endpoints are
    // usable; no clamping conceals an unsupported displacement.
    if(!g||!std::isfinite(signedDisplacementMeters)||
       signedDisplacementMeters<float(g->minimumDisplacementMeters)||
       signedDisplacementMeters>float(g->maximumDisplacementMeters))return {};
    auto frame=g->weaponFromStaticSlide;
    for(unsigned i=0;i<3;++i)frame.values[3][i]+=g->rearward[i]*signedDisplacementMeters;
    return frame;
}
[[nodiscard]] inline std::optional<interaction::PistolSlideProfile> BuildBc2Mp443ModSlideProfile(
    const Bc2PistolSlideSelection& selection)noexcept {
    using namespace interaction;
    const auto* g=FindBc2Mp443SlideGeometry(selection);if(!g)return {};
    const auto& policy=bc2_pistol_slide_profile_detail::Mp443ModPolicy;
    if(double(policy.sourceDisplacementMeters-policy.frontToleranceMeters-policy.hysteresisMeters)<g->minimumDisplacementMeters||
       double(policy.sourceDisplacementMeters+policy.strokeMeters+policy.rearToleranceMeters+policy.hysteresisMeters)>g->maximumDisplacementMeters)return {};
    const auto source=Bc2Mp443SlideLocalFrame(selection,policy.sourceDisplacementMeters);if(!source)return {};
    PistolSlideProfile p;p.id=policy.id;p.revision=policy.revision;
    // The shared field name describes the recognizer's forward reference.
    // It does not turn this static authored pose into a native closed state.
    p.closedContact=feed_interaction_detail::Multiply(g->slideFromAuthoredWrist,*source);
    p.axis=g->rearward;p.stroke=policy.strokeMeters;p.frontTolerance=policy.frontToleranceMeters;
    p.rearTolerance=policy.rearToleranceMeters;p.hysteresis=policy.hysteresisMeters;
    p.contactRadius=policy.contactRadiusMeters;p.lateralTolerance=policy.lateralToleranceMeters;
    p.rotationTolerance=policy.rotationToleranceRadians;p.maxStepMeters=policy.maxStepMeters;
    p.endpointDwellNs=policy.endpointDwellNs;p.maximumStrokeNs=policy.maximumStrokeNs;
    return pistol_slide_detail::Profile(p)?std::optional<PistolSlideProfile>{p}:std::nullopt;
}
}
