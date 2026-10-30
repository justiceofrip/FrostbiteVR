#pragma once
#include "fvr/interaction/PhysicalPistolSlide.h"
#include "fvr/interaction/FeedInteraction.h"
#include "Bc2ReloadConfigDescriptor.h"
#include <algorithm>
#include <span>
#include <string_view>

namespace fvr::bc2 {
// Exact configured asset evidence. These identifiers select geometry only;
// they never supply a native chamber, reload or firing capability.
struct Bc2PistolSlideSelection {
    std::string_view asset,configurationPath,configurationGuid,configurationSha256,meshPath;
    std::uint64_t rigFingerprint=0;
    bool operator==(const Bc2PistolSlideSelection&)const=default;
};
struct Bc2PistolSlideGeometry {
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
struct Bc2PistolSlideInteractionPolicy {
    std::string_view name,sourceReference;
    std::uint64_t id=0,revision=0;
    float sourceDisplacementMeters=0,strokeMeters=0;
    float frontToleranceMeters=0,rearToleranceMeters=0,hysteresisMeters=0;
    float contactRadiusMeters=0,lateralToleranceMeters=0,rotationToleranceRadians=0,maxStepMeters=0;
    std::int64_t endpointDwellNs=0,maximumStrokeNs=0;
};
using Bc2Mp443SlideGeometry=Bc2PistolSlideGeometry;
using Bc2Mp443ModSlideInteractionPolicy=Bc2PistolSlideInteractionPolicy;
// Native behavior is reviewed separately from authored geometry. This class
// selects existing sidecar/native checks; it never establishes a chamber state.
enum class Bc2PistolSlideNativeClass : std::uint8_t { Unsupported, SingleFireMagazineZeroBolt };
struct Bc2PistolSlideRegistration {
    std::uint64_t key=0;
    std::string_view profileDigest;
    const Bc2PistolSlideGeometry* geometry=nullptr;
    const Bc2PistolSlideInteractionPolicy* interaction=nullptr;
    ReloadConfigDescriptor configuration{};
    Bc2PistolSlideNativeClass nativeClass=Bc2PistolSlideNativeClass::Unsupported;
    bool enabled=false,reviewed=false;
};
inline constexpr std::size_t MaxGeneratedBc2PistolSlideProfiles=256;
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
inline constexpr std::array<ReloadTimingWord,6> Mp443Timing{{
    {0x10,std::bit_cast<std::uint32_t>(.5f)},{0x14,0},
    {0x18,std::bit_cast<std::uint32_t>(3.75f)},{0x20,1},{0x24,0},{0x2c,0}}};
inline constexpr Bc2PistolSlideRegistration Mp443Registration{
    0x8d75edb84430ee81ull,"8d75edb84430ee81b7bc609ece5a84971a4436ae555cca0691d040ec40e90e8e",
    &Mp443Geometry,&Mp443ModPolicy,
    {"MP443_sp","Objects/Weapons/Handheld/RU_hg_MP443Grach/SP_hg_MP443Grach",
     {0,1,8,29,17,3,0,3.75f,.5f,0,0,0,false,false},Mp443Timing,ReloadDescriptorAdmission::ReviewedNative},
    Bc2PistolSlideNativeClass::SingleFireMagazineZeroBolt,true,true};
}
namespace bc2_pistol_slide_profile_detail {
inline bool Text(std::string_view text)noexcept {
    return !text.empty()&&text.find('\0')==std::string_view::npos;
}
inline bool Digest(std::string_view text)noexcept {
    return text.size()==64&&std::all_of(text.begin(),text.end(),[](char c){
        return (c>='0'&&c<='9')||(c>='a'&&c<='f');});
}
inline bool Guid(std::string_view text)noexcept {
    if(text.size()!=36)return false;
    for(std::size_t i=0;i<text.size();++i){const char c=text[i];
        if(i==8||i==13||i==18||i==23){if(c!='-')return false;}
        else if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')))return false;
    }return true;
}
inline bool Geometry(const Bc2PistolSlideGeometry& g)noexcept {
    const auto& s=g.selection;
    if(!Text(s.asset)||!Text(s.configurationPath)||!Guid(s.configurationGuid)||!Digest(s.configurationSha256)||
       !Text(s.meshPath)||!s.rigFingerprint||!Text(g.evidencePath)||!Digest(g.geometrySha256)||
       !Text(g.roleReviewPath)||!Digest(g.linearHelperSha256)||!Text(g.root)||!Text(g.slide)||g.root==g.slide||
       g.rootIndex==g.slideIndex||g.rootIndex>=1024||g.slideIndex>=1024||
       !interaction::feed_mechanism_detail::Pose(g.weaponFromStaticSlide)||
       !interaction::feed_mechanism_detail::Pose(g.slideFromAuthoredWrist)||
       !std::isfinite(g.minimumDisplacementMeters)||!std::isfinite(g.maximumDisplacementMeters)||
       !std::isfinite(float(g.minimumDisplacementMeters))||!std::isfinite(float(g.maximumDisplacementMeters))||
       g.minimumDisplacementMeters>0||g.maximumDisplacementMeters<0||
       g.minimumDisplacementMeters>=g.maximumDisplacementMeters||
       !std::isfinite(g.authoredLandmarkSeconds)||g.authoredLandmarkSeconds<0||
       !std::isfinite(g.ring3VertexDistanceMeters)||g.ring3VertexDistanceMeters<0)return false;
    double axisLength=0,distance=0;
    for(unsigned i=0;i<3;++i){
        if(!std::isfinite(g.rearward[i])||!std::isfinite(g.partBoundsMin[i])||!std::isfinite(g.partBoundsMax[i])||
           !std::isfinite(g.ring3Part[i])||!std::isfinite(g.nearestVertexPart[i])||
           g.partBoundsMin[i]>=g.partBoundsMax[i]||g.nearestVertexPart[i]<g.partBoundsMin[i]||
           g.nearestVertexPart[i]>g.partBoundsMax[i])return false;
        axisLength+=double(g.rearward[i])*g.rearward[i];
        const double delta=double(g.ring3Part[i])-g.nearestVertexPart[i];distance+=delta*delta;
    }
    return std::abs(axisLength-1)<1e-5&&std::abs(std::sqrt(distance)-g.ring3VertexDistanceMeters)<1e-5;
}
inline std::optional<math::Matrix4> LocalFrame(const Bc2PistolSlideGeometry& g,float displacement)noexcept {
    // Compare float endpoints so measured representable endpoints remain usable.
    if(!std::isfinite(displacement)||displacement<float(g.minimumDisplacementMeters)||
       displacement>float(g.maximumDisplacementMeters))return {};
    auto frame=g.weaponFromStaticSlide;
    for(unsigned i=0;i<3;++i)frame.values[3][i]+=g.rearward[i]*displacement;
    return interaction::feed_mechanism_detail::Pose(frame)?std::optional{frame}:std::nullopt;
}
inline std::optional<interaction::PistolSlideProfile> Profile(
    const Bc2PistolSlideGeometry& g,const Bc2PistolSlideInteractionPolicy& policy)noexcept {
    using namespace interaction;
    if(!Geometry(g)||!Text(policy.name)||!Text(policy.sourceReference)||!std::isfinite(policy.sourceDisplacementMeters)||
       double(policy.sourceDisplacementMeters-policy.frontToleranceMeters-policy.hysteresisMeters)<g.minimumDisplacementMeters||
       double(policy.sourceDisplacementMeters+policy.strokeMeters+policy.rearToleranceMeters+policy.hysteresisMeters)>g.maximumDisplacementMeters)return {};
    const auto source=LocalFrame(g,policy.sourceDisplacementMeters);if(!source)return {};
    PistolSlideProfile p;p.id=policy.id;p.revision=policy.revision;
    // This recognizer reference is an authored contact, not native closed state.
    p.closedContact=feed_interaction_detail::Multiply(g.slideFromAuthoredWrist,*source);
    p.axis=g.rearward;p.stroke=policy.strokeMeters;p.frontTolerance=policy.frontToleranceMeters;
    p.rearTolerance=policy.rearToleranceMeters;p.hysteresis=policy.hysteresisMeters;
    p.contactRadius=policy.contactRadiusMeters;p.lateralTolerance=policy.lateralToleranceMeters;
    p.rotationTolerance=policy.rotationToleranceRadians;p.maxStepMeters=policy.maxStepMeters;
    p.endpointDwellNs=policy.endpointDwellNs;p.maximumStrokeNs=policy.maximumStrokeNs;
    return pistol_slide_detail::Profile(p)?std::optional{p}:std::nullopt;
}
inline bool Configuration(const ReloadConfigDescriptor& d)noexcept {
    const auto& v=d.values;
    if(d.admission!=ReloadDescriptorAdmission::ReviewedNative||!Text(d.assetName)||!Text(d.assetPath)||
       d.assetName.size()>=ReloadObservedConfig{}.assetName.size()||d.assetPath.size()>=ReloadObservedConfig{}.assetPath.size()||
       v.fireLogicType!=0||v.reloadType!=1||v.fireInputAction!=8||v.reloadInputAction!=29||
       v.baseCapacity<=0||v.numberOfMagazines<=0||v.boltDelay!=0||v.boltTime!=0||
       v.holdBoltUntilFireRelease||v.holdBoltUntilZoomRelease||
       !std::isfinite(v.reloadDelay)||v.reloadDelay<0||!std::isfinite(v.reloadTime)||v.reloadTime<=0||
       !std::isfinite(v.reloadThreshold)||v.reloadThreshold<0||v.reloadThreshold>1||
       !std::isfinite(v.postReloadTime)||v.postReloadTime<0||d.timing.size()!=6)return false;
    const std::array<ReloadTimingWord,6> expected{{
        {0x10,std::bit_cast<std::uint32_t>(v.reloadThreshold)},{0x14,std::bit_cast<std::uint32_t>(v.reloadDelay)},
        {0x18,std::bit_cast<std::uint32_t>(v.reloadTime)},{0x20,1},{0x24,0},{0x2c,0}}};
    for(std::size_t i=0;i<d.timing.size();++i){const auto& w=d.timing[i];
        if(w.offset!=expected[i].offset||w.expected!=expected[i].expected||w.comparison!=ReloadTimingComparison::Bits)return false;
    }return true;
}
inline bool SameConfiguration(const ReloadConfigDescriptor& a,const ReloadConfigDescriptor& b)noexcept {
    const auto& x=a.values;const auto& y=b.values;
    if(a.assetName!=b.assetName||a.assetPath!=b.assetPath||a.admission!=b.admission||a.timing.size()!=b.timing.size()||
       x.fireLogicType!=y.fireLogicType||x.reloadType!=y.reloadType||x.fireInputAction!=y.fireInputAction||
       x.reloadInputAction!=y.reloadInputAction||x.baseCapacity!=y.baseCapacity||x.numberOfMagazines!=y.numberOfMagazines||
       x.reloadDelay!=y.reloadDelay||x.reloadTime!=y.reloadTime||x.reloadThreshold!=y.reloadThreshold||
       x.postReloadTime!=y.postReloadTime||x.boltDelay!=y.boltDelay||x.boltTime!=y.boltTime||
       x.holdBoltUntilFireRelease!=y.holdBoltUntilFireRelease||x.holdBoltUntilZoomRelease!=y.holdBoltUntilZoomRelease)return false;
    for(std::size_t i=0;i<a.timing.size();++i)if(a.timing[i].offset!=b.timing[i].offset||
        a.timing[i].expected!=b.timing[i].expected||a.timing[i].comparison!=b.timing[i].comparison)return false;
    return true;
}
inline bool Conflict(const Bc2PistolSlideRegistration& a,const Bc2PistolSlideRegistration& b)noexcept {
    return a.key==b.key||a.profileDigest==b.profileDigest||
        (a.configuration.assetName==b.configuration.assetName&&a.configuration.assetPath==b.configuration.assetPath)||
        (a.geometry&&b.geometry&&a.geometry->selection==b.geometry->selection);
}
}
[[nodiscard]] inline std::optional<std::uint64_t> Bc2PistolSlideProfileKey(std::string_view digest)noexcept {
    if(!bc2_pistol_slide_profile_detail::Digest(digest))return {};
    std::uint64_t key=0;
    for(std::size_t i=0;i<16;++i){const auto c=digest[i];key=(key<<4)|std::uint64_t(c<='9'?c-'0':c-'a'+10);}
    return key?std::optional{key}:std::nullopt;
}
[[nodiscard]] inline bool Bc2PistolSlideRegistrationValid(const Bc2PistolSlideRegistration& row)noexcept {
    const auto key=Bc2PistolSlideProfileKey(row.profileDigest);
    return row.enabled&&row.reviewed&&key&&*key==row.key&&row.geometry&&row.interaction&&
        row.nativeClass==Bc2PistolSlideNativeClass::SingleFireMagazineZeroBolt&&
        bc2_pistol_slide_profile_detail::Configuration(row.configuration)&&
        row.configuration.assetName==row.geometry->selection.asset&&
        row.configuration.assetPath==row.geometry->selection.configurationPath&&
        bool(bc2_pistol_slide_profile_detail::Profile(*row.geometry,*row.interaction));
}
[[nodiscard]] inline const Bc2PistolSlideRegistration* ResolveBc2PistolSlideRegistration(
    std::span<const Bc2PistolSlideRegistration> rows,std::uint64_t key)noexcept {
    if(!key||rows.size()>MaxGeneratedBc2PistolSlideProfiles+1)return nullptr;
    const Bc2PistolSlideRegistration* found=nullptr;
    for(const auto& row:rows)if(row.key==key){if(found)return nullptr;found=&row;}
    if(!found||!Bc2PistolSlideRegistrationValid(*found))return nullptr;
    for(const auto& row:rows)if(&row!=found&&bc2_pistol_slide_profile_detail::Conflict(row,*found))return nullptr;
    return found;
}
[[nodiscard]] std::span<const Bc2PistolSlideRegistration> RegisteredBc2PistolSlideProfiles()noexcept;
[[nodiscard]] inline const Bc2PistolSlideRegistration* ResolveBc2PistolSlideRegistration(std::uint64_t key)noexcept {
    return ResolveBc2PistolSlideRegistration(RegisteredBc2PistolSlideProfiles(),key);
}
[[nodiscard]] inline const Bc2PistolSlideRegistration* FindBc2PistolSlideRegistration(
    std::span<const Bc2PistolSlideRegistration> rows,const Bc2PistolSlideSelection& selection)noexcept {
    if(rows.size()>MaxGeneratedBc2PistolSlideProfiles+1)return nullptr;
    const Bc2PistolSlideRegistration* found=nullptr;
    for(const auto& row:rows)if(row.geometry&&row.geometry->selection==selection){
        if(found)return nullptr;found=&row;
    }
    return found?ResolveBc2PistolSlideRegistration(rows,found->key):nullptr;
}
[[nodiscard]] inline const Bc2PistolSlideRegistration* FindBc2PistolSlideRegistration(
    std::span<const Bc2PistolSlideRegistration> rows,const ReloadObservedConfig& config)noexcept {
    if(rows.size()>MaxGeneratedBc2PistolSlideProfiles+1)return nullptr;
    const Bc2PistolSlideRegistration* found=nullptr;
    for(const auto& row:rows)if(MatchesReloadDescriptor(config,row.configuration)){
        if(found)return nullptr;found=&row;
    }
    return found?ResolveBc2PistolSlideRegistration(rows,found->key):nullptr;
}
[[nodiscard]] inline const Bc2PistolSlideRegistration* FindBc2PistolSlideRegistration(
    std::span<const Bc2PistolSlideRegistration> rows,const ReloadConfigDescriptor& config)noexcept {
    if(rows.size()>MaxGeneratedBc2PistolSlideProfiles+1)return nullptr;
    const Bc2PistolSlideRegistration* found=nullptr;
    for(const auto& row:rows)if(bc2_pistol_slide_profile_detail::SameConfiguration(config,row.configuration)){
        if(found)return nullptr;found=&row;
    }
    return found?ResolveBc2PistolSlideRegistration(rows,found->key):nullptr;
}
[[nodiscard]] inline const Bc2PistolSlideRegistration* FindBc2PistolSlideRegistration(const Bc2PistolSlideSelection& selection)noexcept {
    return FindBc2PistolSlideRegistration(RegisteredBc2PistolSlideProfiles(),selection);
}
[[nodiscard]] inline const Bc2PistolSlideRegistration* FindBc2PistolSlideRegistration(const ReloadObservedConfig& config)noexcept {
    return FindBc2PistolSlideRegistration(RegisteredBc2PistolSlideProfiles(),config);
}
[[nodiscard]] inline const Bc2PistolSlideRegistration* FindBc2PistolSlideRegistration(const ReloadConfigDescriptor& config)noexcept {
    return FindBc2PistolSlideRegistration(RegisteredBc2PistolSlideProfiles(),config);
}
[[nodiscard]] inline const Bc2PistolSlideGeometry* FindBc2PistolSlideGeometry(const Bc2PistolSlideSelection& selection)noexcept {
    const auto* row=FindBc2PistolSlideRegistration(selection);return row?row->geometry:nullptr;
}
[[nodiscard]] inline std::optional<math::Matrix4> Bc2PistolSlideLocalFrame(
    const Bc2PistolSlideRegistration& row,float signedDisplacementMeters)noexcept {
    return Bc2PistolSlideRegistrationValid(row)?
        bc2_pistol_slide_profile_detail::LocalFrame(*row.geometry,signedDisplacementMeters):std::nullopt;
}
[[nodiscard]] inline std::optional<math::Matrix4> Bc2PistolSlideLocalFrame(
    const Bc2PistolSlideSelection& selection,float signedDisplacementMeters)noexcept {
    const auto* row=FindBc2PistolSlideRegistration(selection);return row?Bc2PistolSlideLocalFrame(*row,signedDisplacementMeters):std::nullopt;
}
[[nodiscard]] inline std::optional<interaction::PistolSlideProfile> BuildBc2PistolSlideProfile(
    const Bc2PistolSlideRegistration& row)noexcept {
    return Bc2PistolSlideRegistrationValid(row)?bc2_pistol_slide_profile_detail::Profile(*row.geometry,*row.interaction):std::nullopt;
}
[[nodiscard]] inline std::optional<interaction::PistolSlideProfile> BuildBc2PistolSlideProfile(
    const Bc2PistolSlideSelection& selection)noexcept {
    const auto* row=FindBc2PistolSlideRegistration(selection);return row?BuildBc2PistolSlideProfile(*row):std::nullopt;
}
// Legacy names use the exact same registry; no name, family or rig fallback.
[[nodiscard]] inline const Bc2Mp443SlideGeometry* FindBc2Mp443SlideGeometry(
    const Bc2PistolSlideSelection& selection)noexcept {
    return FindBc2PistolSlideGeometry(selection);
}
[[nodiscard]] inline std::optional<math::Matrix4> Bc2Mp443SlideLocalFrame(
    const Bc2PistolSlideSelection& selection,float signedDisplacementMeters)noexcept {
    return Bc2PistolSlideLocalFrame(selection,signedDisplacementMeters);
}
[[nodiscard]] inline std::optional<interaction::PistolSlideProfile> BuildBc2Mp443ModSlideProfile(
    const Bc2PistolSlideSelection& selection)noexcept {
    return BuildBc2PistolSlideProfile(selection);
}
}
