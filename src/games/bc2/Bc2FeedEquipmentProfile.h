#pragma once
#include "fvr/interaction/FeedInteraction.h"
#include <string_view>

namespace fvr::bc2 {
// Exact offline measurement, not a native binding or mechanism stage policy.
// Provenance: handoff/tasks/CHAT14/20261030-profile18/proof01.json.
// Canonical reflected-Z, row-vector metres relative to jntWpn_1.
enum class Bc2M60FeedPoseId : unsigned {
    Resting,PouchDetachSource,PouchWithdrawn,PouchAttachSource,
    PouchAttachedFeedUnseated,FeedSeatSource,FeedSeated,Count
};
struct Bc2M60FeedPose {
    double authoredSeconds=0;
    math::Matrix4 weaponFromPouch{},weaponFromFeedRoot{};
    // 13 in 12; wpnJnt_14 in 13; 15 in wpnJnt_14. These are NOT
    // three fixed offsets of a rigid belt or children of the pouch.
    std::array<math::Matrix4,3> parentFromChainChild{};
    // Same-frame authored LeftHand wrist, local to pouch6 except at
    // FeedSeatSource (local to terminal15). PouchWithdrawn also records its
    // same-frame grip for same-container reattachment. No native scripted
    // grip constraint is asserted.
    math::Matrix4 sourceFromWrist{};
};
struct Bc2M60FeedChainBone {
    std::string_view bone,parent;
    unsigned index=0,parentIndex=0;
};
struct Bc2M60FeedGeometry {
    std::string_view asset,configurationPath,meshPath;
    std::uint64_t rigFingerprint=0;
    std::string_view meshSha256,lodSha256,skeletonSha256,clipSha256;
    std::array<Bc2M60FeedPose,7> poses{};
    std::array<Bc2M60FeedChainBone,4> chain{};
    math::Matrix4 weaponFromHinge{},hingeFromOpeningContact{},hingeFromClosingContact{};
    float coverTravelRadians=0;
};
// Every value here is explicit interaction policy. Zero defaults fail closed.
// None is an authored tolerance, native stage threshold, or firing permission.
struct Bc2FeedInteractionPolicy {
    float completeFraction=0,releaseFraction=0;
    float pathToleranceMeters=0,orientationToleranceRadians=0;
    float maxStepMeters=0,maxStepRadians=0;
    std::int64_t endpointDwellNs=0,maximumGestureNs=0,maxInputGapNs=0;
};
enum class Bc2FeedGeometryAvailability : std::uint8_t {
    MeasuredAuthoredGeometry,SeparateWithdrawalUnmeasured,LatchUnmeasured,
    ChargeSemanticsUnassigned,UnsupportedOperation
};
[[nodiscard]] inline Bc2FeedGeometryAvailability Bc2M60FeedOperationAvailability(
    interaction::FeedPhysicalOperation op)noexcept {
    using Op=interaction::FeedPhysicalOperation;
    switch(op){
    case Op::OpenCover:case Op::CloseCover:case Op::DetachContainer:
    case Op::AttachContainer:case Op::SeatFeed:return Bc2FeedGeometryAvailability::MeasuredAuthoredGeometry;
    case Op::WithdrawFeed:return Bc2FeedGeometryAvailability::SeparateWithdrawalUnmeasured;
    case Op::ReleaseLatch:case Op::EngageLatch:return Bc2FeedGeometryAvailability::LatchUnmeasured;
    case Op::CycleAction:return Bc2FeedGeometryAvailability::ChargeSemanticsUnassigned;
    default:return Bc2FeedGeometryAvailability::UnsupportedOperation;
    }
}
namespace bc2_feed_profile_detail {
inline math::Matrix4 Terminal(const Bc2M60FeedPose& pose)noexcept {
    auto world=pose.weaponFromFeedRoot;
    for(const auto& local:pose.parentFromChainChild)world=interaction::feed_interaction_detail::Multiply(local,world);
    return world;
}
constexpr math::Matrix4 MeasuredFrame(std::array<float,16> values)noexcept {
    math::Matrix4 out{};
    for(unsigned i=0;i<4;++i)for(unsigned j=0;j<4;++j)out.values[i][j]=values[i*4+j];
    return out;
}
// BEGIN MEASURED FRAMES
inline const std::array<Bc2M60FeedPose,7> M60MeasuredPoses{{
    { // Resting; authored 0 seconds
      0, MeasuredFrame({0.9999999981f,4.312061772e-05f,4.311875842e-05f,0.f,-4.311875842e-05f,0.9999999981f,-4.312061772e-05f,0.f,-4.312061772e-05f,4.311875842e-05f,0.9999999981f,-0.f,0.1052855052f,-0.02162126536f,-0.3782466588f,1.f}),
      MeasuredFrame({0.9999999981f,4.312061772e-05f,4.311875842e-05f,0.f,-4.311875842e-05f,0.9999999981f,-4.312061772e-05f,0.f,-4.312061772e-05f,4.311875842e-05f,0.9999999981f,-0.f,0.08373835302f,0.0277554969f,-0.4063177281f,1.f}),
      {{MeasuredFrame({0.9999999991f,4.311970263e-05f,5.392844817e-06f,0.f,-4.311967355e-05f,0.9999999991f,-5.393077359e-06f,0.f,-5.393077358e-06f,5.392844816e-06f,1.f,0.f,-0.02514609694f,0.0179381948f,4.995115397e-09f,1.f}),
        MeasuredFrame({0.9999999991f,4.311970263e-05f,5.392844817e-06f,0.f,-4.311967355e-05f,0.9999999991f,-5.393077359e-06f,0.f,-5.393077358e-06f,5.392844816e-06f,1.f,0.f,-0.008239435221f,0.01318593779f,-2.600322627e-09f,1.f}),
        MeasuredFrame({0.9999999991f,4.311970263e-05f,5.392844817e-06f,0.f,-4.311967355e-05f,0.9999999991f,-5.393077359e-06f,0.f,-5.393077358e-06f,5.392844816e-06f,1.f,0.f,-0.02968590707f,0.007675176486f,1.875116596e-08f,1.f})}},
      MeasuredFrame({0.1283077608f,0.8463520432f,-0.5169384271f,0.f,0.2273299475f,-0.5324676705f,-0.8153522398f,0.f,-0.9653280341f,-0.01289956532f,-0.2607209001f,0.f,-0.03879443385f,-0.0237318495f,-0.1397647168f,1.f})},
    { // PouchDetachSource; authored 1.8 seconds
      1.8, MeasuredFrame({0.9999999998f,1.108360866e-05f,-1.768666381e-05f,0.f,-1.108329584e-05f,0.9999999998f,1.768685984e-05f,0.f,1.768685984e-05f,-1.768666381e-05f,0.9999999997f,-0.f,0.1052956145f,-0.02161867069f,-0.3782348248f,1.f}),
      MeasuredFrame({0.9999999998f,1.108360866e-05f,-1.768666381e-05f,0.f,-1.108329584e-05f,0.9999999998f,1.768685984e-05f,0.f,1.768685984e-05f,-1.768666381e-05f,0.9999999997f,-0.f,0.08375109573f,0.02775996708f,-0.4063094797f,1.f}),
      {{MeasuredFrame({0.9999999978f,-6.54465781e-05f,5.393137569e-06f,0.f,6.544660718e-05f,0.9999999978f,-5.392784619e-06f,0.f,-5.392784618e-06f,5.393137569e-06f,1.f,0.f,-0.02514609694f,0.0179381948f,4.995115513e-09f,1.f}),
        MeasuredFrame({1.f,9.101720965e-07f,5.392958638e-06f,0.f,-9.101430116e-07f,1.f,-5.392963547e-06f,0.f,-5.392963547e-06f,5.392958638e-06f,1.f,0.f,-0.01112430182f,0.01083194978f,-2.580845254e-08f,1.f}),
        MeasuredFrame({1.f,-5.960131284e-06f,5.392977191e-06f,0.f,5.960160369e-06f,1.f,-5.392945049e-06f,0.f,-5.392945049e-06f,5.392977191e-06f,1.f,0.f,-0.02968590707f,0.007675176486f,1.875116608e-08f,1.f})}},
      MeasuredFrame({-0.3997560016f,0.8497118225f,0.343780392f,0.f,0.4837263329f,0.5141367865f,-0.7082882179f,0.f,-0.7785910185f,-0.1168468376f,-0.6165572499f,0.f,0.1146011794f,-0.01182452718f,0.02065891551f,1.f})},
    { // PouchWithdrawn; authored 2.1 seconds
      2.1, MeasuredFrame({0.7744013616f,-0.3388196856f,0.5343255111f,0.f,0.05219479742f,0.8758564757f,0.4797406977f,0.f,-0.6305380514f,-0.3436228377f,0.6959490722f,-0.f,0.4018318477f,-0.2164332439f,-0.3639424479f,1.f}),
      MeasuredFrame({0.7744013616f,-0.3388196856f,0.5343255111f,0.f,0.05219479742f,0.8758564757f,0.4797406977f,0.f,-0.6305380514f,-0.3436228377f,0.6959490722f,-0.f,0.4053082638f,-0.1562445693f,-0.3713379189f,1.f}),
      {{MeasuredFrame({0.9746780429f,-0.2236128633f,5.967997676e-06f,0.f,0.2236128633f,0.9746780429f,-4.753853501e-06f,0.f,-4.753853501e-06f,5.967997675e-06f,1.f,0.f,-0.02514609694f,0.0179381948f,4.995115557e-09f,1.f}),
        MeasuredFrame({0.7941516852f,-0.6077195907f,6.858308644e-06f,0.f,0.6077195907f,0.7941516852f,-3.387740871e-06f,0.f,-3.387740871e-06f,6.858308644e-06f,1.f,0.f,-0.01137995883f,0.01061636482f,-2.280992985e-08f,1.f}),
        MeasuredFrame({0.748500858f,-0.6631338218f,6.984825233e-06f,0.f,0.6631338218f,0.748500858f,-3.143552016e-06f,0.f,-3.143552016e-06f,6.984825232e-06f,1.f,0.f,-0.02968590707f,0.007675176486f,1.875116582e-08f,1.f})}},
      MeasuredFrame({-0.4599995503f,0.7878912136f,0.4094238015f,0.f,0.2550996095f,0.5589347909f,-0.7889968877f,0.f,-0.8504849222f,-0.2584943617f,-0.4581004934f,0.f,0.1152145972f,-0.0412593621f,0.01254435013f,1.f})},
    { // PouchAttachSource; authored 3 seconds
      3, MeasuredFrame({0.8765087611f,-0.3641179181f,0.3148817769f,0.f,0.3822417499f,0.9240512721f,0.004526723428f,0.f,-0.2926151676f,0.1163932487f,0.9491201059f,-0.f,0.2713777015f,-0.04472070226f,-0.4290398393f,1.f}),
      MeasuredFrame({0.1588085584f,-0.972875106f,0.1682078175f,0.f,0.9429985985f,0.1999306487f,0.2660477006f,0.f,-0.292461083f,0.1163690844f,0.9491705596f,-0.f,0.2340561952f,-0.05784242608f,-0.4683723821f,1.f}),
      {{MeasuredFrame({0.9495886878f,0.3134985231f,4.468358656e-06f,0.f,-0.3134985231f,0.9495886878f,-6.18076286e-06f,0.f,-6.18076286e-06f,4.468358656e-06f,1.f,0.f,-0.02514609694f,0.0179381948f,4.99511534e-09f,1.f}),
        MeasuredFrame({0.7527542905f,-0.6583015859f,6.944788832e-06f,0.f,0.6583015859f,0.7527542905f,-3.152454073e-06f,0.f,-3.152454074e-06f,6.944788833e-06f,1.f,0.f,-0.008239435221f,0.01317254151f,-2.175960801e-08f,1.f}),
        MeasuredFrame({0.6985347547f,-0.715576129f,7.063700617e-06f,0.f,0.715576129f,0.6985347547f,-2.876141894e-06f,0.f,-2.876141895e-06f,7.063700618e-06f,1.f,0.f,-0.02968590707f,0.007675176486f,1.875116583e-08f,1.f})}},
      MeasuredFrame({-0.6466116502f,0.7384470279f,0.191283457f,0.f,0.1475284892f,0.3670827708f,-0.9184147126f,0.f,-0.7484174762f,-0.5656378934f,-0.3463019705f,0.f,0.1410970168f,0.01253833593f,0.02760756317f,1.f})},
    { // PouchAttachedFeedUnseated; authored 3.6 seconds
      3.6, MeasuredFrame({0.9999999973f,7.15199059e-05f,-1.800497088e-05f,0.f,-7.15195817e-05f,0.9999999973f,1.800625864e-05f,0.f,1.800625864e-05f,-1.800497088e-05f,0.9999999997f,-0.f,0.1051119553f,-0.02137011121f,-0.3783381491f,1.f}),
      MeasuredFrame({0.546266863f,-0.8376111952f,-7.293915994e-06f,0.f,0.8376111949f,0.546266863f,-2.433780222e-05f,0.f,2.437004021e-05f,7.185469179e-06f,0.9999999997f,-0.f,0.06478523004f,-0.0478658098f,-0.4063219945f,1.f}),
      {{MeasuredFrame({0.9495886878f,0.3134985231f,4.468358655e-06f,0.f,-0.3134985231f,0.9495886878f,-6.18076286e-06f,0.f,-6.180762861e-06f,4.468358656e-06f,1.f,0.f,-0.02514609694f,0.0179381948f,4.995115414e-09f,1.f}),
        MeasuredFrame({0.7527542905f,-0.6583015859f,6.944788832e-06f,0.f,0.6583015859f,0.7527542905f,-3.152454074e-06f,0.f,-3.152454074e-06f,6.944788832e-06f,1.f,0.f,-0.008239435221f,0.01317867687f,-2.531290627e-08f,1.f}),
        MeasuredFrame({0.6985347547f,-0.715576129f,7.063700617e-06f,0.f,0.715576129f,0.6985347547f,-2.876141895e-06f,0.f,-2.876141895e-06f,7.063700617e-06f,1.f,0.f,-0.02968590707f,0.007675176486f,1.87511659e-08f,1.f})}},
      MeasuredFrame({-0.6290062913f,0.6666564919f,0.3999002467f,0.f,0.1165322759f,0.589451305f,-0.7993543568f,0.f,-0.7686164936f,-0.4561976335f,-0.4484555775f,0.f,0.1528897269f,0.02267951025f,0.02805255002f,1.f})},
    { // FeedSeatSource; authored 3.933333333 seconds
      3.93333333333, MeasuredFrame({0.9999999996f,-1.917512204e-05f,1.909557832e-05f,0.f,1.917557736e-05f,0.9999999995f,-2.384429191e-05f,0.f,-1.909512109e-05f,2.384465807e-05f,0.9999999995f,-0.f,0.105113852f,-0.02135548123f,-0.3783329272f,1.f}),
      MeasuredFrame({0.5464713931f,-0.8374777697f,4.125399102e-05f,0.f,0.8374777708f,0.5464713925f,-2.669579339e-05f,0.f,-1.869924091e-07f,4.913778783e-05f,0.9999999988f,-0.f,0.06480360377f,-0.04795375991f,-0.4063177281f,1.f}),
      {{MeasuredFrame({0.9498625853f,0.3126676655f,4.471063118e-06f,0.f,-0.3126676655f,0.9498625853f,-6.17880952e-06f,0.f,-6.178809521e-06f,4.471063118e-06f,1.f,0.f,-0.02514609694f,0.0179381948f,4.995115407e-09f,1.f}),
        MeasuredFrame({0.7530716988f,-0.6579384595f,6.944034951e-06f,0.f,0.6579384595f,0.7530716988f,-3.154131667e-06f,0.f,-3.154131667e-06f,6.944034951e-06f,1.f,0.f,-0.008239435221f,0.01318155444f,-2.660386836e-08f,1.f}),
        MeasuredFrame({0.6980014966f,-0.7160962999f,7.06478758e-06f,0.f,0.7160962999f,0.6980014966f,-2.87351724e-06f,0.f,-2.87351724e-06f,7.064787581e-06f,1.f,0.f,-0.02968590707f,0.007675176486f,1.87511659e-08f,1.f})}},
      MeasuredFrame({0.5403604804f,-0.1967096594f,0.8181172661f,0.f,-0.4982741432f,-0.8582879753f,0.1227380527f,0.f,0.6780364514f,-0.4739694729f,-0.5618002397f,0.f,-0.1402505319f,0.06040388801f,0.04489482814f,1.f})},
    { // FeedSeated; authored 4.5 seconds
      4.5, MeasuredFrame({0.9999999981f,4.312061772e-05f,4.311875842e-05f,0.f,-4.311875842e-05f,0.9999999981f,-4.312061772e-05f,0.f,-4.312061772e-05f,4.311875842e-05f,0.9999999981f,-0.f,0.105113852f,-0.02135863461f,-0.3783329272f,1.f}),
      MeasuredFrame({0.9999999989f,-1.734192717e-05f,4.3120062e-05f,0.f,1.734378648e-05f,0.9999999989f,-4.311931418e-05f,0.f,-4.311931418e-05f,4.3120062e-05f,0.9999999981f,-0.f,0.08375071836f,0.02775129337f,-0.4063177281f,1.f}),
      {{MeasuredFrame({0.9999999963f,-8.639436581e-05f,5.393194598e-06f,0.f,8.63943949e-05f,0.9999999963f,-5.392728676e-06f,0.f,-5.392728676e-06f,5.393194598e-06f,1.f,0.f,-0.02514609694f,0.0179381948f,4.995115467e-09f,1.f}),
        MeasuredFrame({0.9999999998f,-2.098506637e-05f,5.393017766e-06f,0.f,2.098509545e-05f,0.9999999998f,-5.392904595e-06f,0.f,-5.392904595e-06f,5.393017767e-06f,1.f,0.f,-0.008239435221f,0.0131855759f,-2.767872034e-08f,1.f}),
        MeasuredFrame({0.9999999919f,-0.0001269176437f,5.39330332e-06f,0.f,0.0001269176728f,0.9999999919f,-5.392618858e-06f,0.f,-5.392618858e-06f,5.393303321e-06f,1.f,0.f,-0.02968590707f,0.007675176486f,1.875116593e-08f,1.f})}},
      MeasuredFrame({0.1752851703f,-0.4658624921f,0.8673218823f,0.f,-0.3759479846f,0.7825294908f,0.4962969966f,0.f,-0.9099111066f,-0.4130614172f,-0.03797424996f,0.f,-0.04719782258f,0.2213964597f,0.004165070726f,1.f})}
}};
inline const math::Matrix4 M60HingeFrame = MeasuredFrame({0.f,0.f,1.f,0.f,0.f,1.f,0.f,0.f,-1.f,0.f,0.f,0.f,2.629630187e-08f,0.07453461736f,-0.4183450341f,1.f});
inline const math::Matrix4 M60OpeningContact = MeasuredFrame({0.7971139874f,-0.5155624278f,-0.3143321081f,0.f,0.5996201023f,0.737204983f,0.3114234192f,0.f,0.07116898229f,-0.4367198142f,0.8967779992f,0.f,0.1601332109f,0.04591688966f,-0.0664629656f,1.f});
inline const math::Matrix4 M60ClosingContact = MeasuredFrame({0.5155986092f,0.7970923832f,-0.314327547f,0.f,-0.7371807195f,0.5996513009f,0.3114207831f,0.f,0.4367180567f,0.07114808462f,0.8967805133f,0.f,-0.04590960312f,0.1601353457f,-0.06646285548f,1.f});
inline constexpr float M60CoverTravel = 1.570753062f;
// END MEASURED FRAMES
inline const Bc2M60FeedGeometry M60Geometry{
    "M60_sp","Objects/Weapons/Handheld/BU_lmg_M60/SP_lmg_M60",
    "Objects/Weapons/Handheld/BU_lmg_M60/BU_lmg_M60_Mesh",0xa7f219a1426216abull,
    "76111c128ce0365d6df6263bfa87cf6329979283d62dc95c95806d5b8eec4446",
    "bff83bba14b87b2833321648d5d3ef738496896e173b20a73bf638aecb4440d9",
    "d1d713cc7301bc5ae2b011a0991ca03c2fcbb26c1187900f6fd129533d173c09",
    "a6b0e5c68340c3ec2211e25a7f682bbce93befc1535e13fc31bd46bf727614f3",
    M60MeasuredPoses,{{{"jntWpn_12","jntWpn_1",38,10},
        {"jntWpn_13","jntWpn_12",100,38},
        {"jntWpnwpnJnt_14","jntWpn_13",114,100},
        {"jntWpn_15","jntWpnwpnJnt_14",116,114}}},
    M60HingeFrame,M60OpeningContact,M60ClosingContact,M60CoverTravel
};
}
// No category, alias, case-folding, mesh-only or rig-only fallback.
[[nodiscard]] inline const Bc2M60FeedGeometry* FindBc2M60FeedGeometry(
    std::string_view asset,std::string_view configurationPath,std::string_view meshPath,
    std::uint64_t rigFingerprint)noexcept {
    const auto& g=bc2_feed_profile_detail::M60Geometry;
    return asset==g.asset&&configurationPath==g.configurationPath&&meshPath==g.meshPath&&
        rigFingerprint==g.rigFingerprint?&g:nullptr;
}
// Geometry-only, opt-in construction for the caller's explicit same-container
// replenishment policy: coupled pouch/feed detach, retained-pouch reattach,
// terminal seating. A mechanism profile must separately select that policy;
// this never fills Unknown with Absent or invents native cover/chamber state.
[[nodiscard]] inline std::optional<interaction::FeedMotionProfile> BuildBc2M60FeedMotion(
    const Bc2M60FeedGeometry& geometry,interaction::FeedPhysicalOperation operation,
    const Bc2FeedInteractionPolicy& policy)noexcept {
    using namespace interaction;
    if(&geometry!=&bc2_feed_profile_detail::M60Geometry||
       Bc2M60FeedOperationAvailability(operation)!=Bc2FeedGeometryAvailability::MeasuredAuthoredGeometry)return {};
    FeedMotionProfile out;out.operation=operation;out.measured=true;
    out.pathToleranceMeters=policy.pathToleranceMeters;
    out.orientationToleranceRadians=policy.orientationToleranceRadians;
    out.maxStepMeters=policy.maxStepMeters;out.maxStepRadians=policy.maxStepRadians;
    out.endpointDwellNs=policy.endpointDwellNs;out.maximumGestureNs=policy.maximumGestureNs;
    out.maxInputGapNs=policy.maxInputGapNs;
    if(operation==FeedPhysicalOperation::OpenCover||operation==FeedPhysicalOperation::CloseCover){
        const bool open=operation==FeedPhysicalOperation::OpenCover;
        out.kind=FeedMotionKind::Hinge;out.weaponFromMotion=geometry.weaponFromHinge;
        out.motionFromContact=open?geometry.hingeFromOpeningContact:geometry.hingeFromClosingContact;
        out.travel=open?geometry.coverTravelRadians:-geometry.coverTravelRadians;
        out.completeFraction=policy.completeFraction;out.releaseFraction=policy.releaseFraction;
    }else {
        Bc2M60FeedPoseId source,destination;
        if(operation==FeedPhysicalOperation::DetachContainer){
            source=Bc2M60FeedPoseId::PouchDetachSource;destination=Bc2M60FeedPoseId::PouchWithdrawn;
        }else if(operation==FeedPhysicalOperation::AttachContainer){
            source=Bc2M60FeedPoseId::PouchWithdrawn;destination=Bc2M60FeedPoseId::PouchAttachedFeedUnseated;
        }else {source=Bc2M60FeedPoseId::PouchAttachedFeedUnseated;destination=Bc2M60FeedPoseId::FeedSeated;}
        const auto& a=geometry.poses[static_cast<unsigned>(source)];
        const auto& b=geometry.poses[static_cast<unsigned>(destination)];
        const bool feed=operation==FeedPhysicalOperation::SeatFeed;
        out.kind=FeedMotionKind::FreePlacement;
        // Transport the authored terminal-local wrist reference at t3.9333
        // onto the retained t3.6 terminal. The object stays at the prior
        // stage's destination; the recognizer retains the actual hand offset.
        const auto& grip=feed?geometry.poses[static_cast<unsigned>(Bc2M60FeedPoseId::FeedSeatSource)].sourceFromWrist:a.sourceFromWrist;
        // The grasp is on terminal15. Root12 has a different motion because
        // the three intervening parent-local transforms change during seating.
        out.placement={feed?bc2_feed_profile_detail::Terminal(a):a.weaponFromPouch,
            feed?bc2_feed_profile_detail::Terminal(b):b.weaponFromPouch,grip};
    }
    return feed_interaction_detail::Motion(out)?std::optional<FeedMotionProfile>{out}:std::nullopt;
}
// Freeze this result for the next gesture. A preceding accepted placement may
// end anywhere inside the policy tolerance, so use its actual retained object
// pose as the next source. Only this mod-owned source changes; the receiver
// destination and authored bone-local wrist reference remain measured data.
[[nodiscard]] inline std::optional<interaction::FeedMotionProfile> BuildBc2M60FeedMotionFromRetainedPose(
    const Bc2M60FeedGeometry& geometry,interaction::FeedPhysicalOperation operation,
    const Bc2FeedInteractionPolicy& policy,const Bc2M60FeedPose& retained)noexcept {
    using namespace interaction;
    auto motion=BuildBc2M60FeedMotion(geometry,operation,policy);
    if(!motion||motion->kind!=FeedMotionKind::FreePlacement||
       !feed_mechanism_detail::Pose(retained.weaponFromPouch)||!feed_mechanism_detail::Pose(retained.weaponFromFeedRoot))return {};
    for(const auto& local:retained.parentFromChainChild)if(!feed_mechanism_detail::Pose(local))return {};
    motion->placement.weaponFromSource=operation==FeedPhysicalOperation::SeatFeed?
        bc2_feed_profile_detail::Terminal(retained):retained.weaponFromPouch;
    return feed_interaction_detail::Motion(*motion)?motion:std::nullopt;
}
[[nodiscard]] inline interaction::FeedInteractionProfile BuildBc2M60FeedInteractionProfile(
    const Bc2M60FeedGeometry& geometry,std::uint64_t profileId,std::uint64_t profileRevision,
    const Bc2FeedInteractionPolicy& policy)noexcept {
    interaction::FeedInteractionProfile out;
    if(!profileId||!profileRevision)return out;
    out.profileId=profileId;out.profileRevision=profileRevision;
    for(unsigned i=1;i<out.operations.size();++i){
        const auto motion=BuildBc2M60FeedMotion(geometry,static_cast<interaction::FeedPhysicalOperation>(i),policy);
        if(motion)out.operations[i]=*motion;
    }
    return out;
}
struct Bc2M60FeedPresentation {
    interaction::FeedSupplyKey supply{};
    math::Matrix4 weaponFromPouch{};
    std::array<math::Matrix4,4> weaponFromChain{};
};
// Consumer retains one articulated pose per exact supply lifetime, including
// stow/tracking loss. Its free-placement target updates the selected object;
// child articulation must be supplied explicitly, never replaced by a single
// belt mesh or silently flattened to fixed root offsets. This composes poses,
// not a constraint solver, animation interpolator, or custody state machine.
[[nodiscard]] inline std::optional<Bc2M60FeedPresentation> ComposeBc2M60FeedPresentation(
    interaction::FeedSupplyKey supply,const Bc2M60FeedPose& pose)noexcept {
    using namespace interaction;
    if(!feed_mechanism_detail::Supply(supply,true)||!feed_mechanism_detail::Pose(pose.weaponFromPouch)||
       !feed_mechanism_detail::Pose(pose.weaponFromFeedRoot))return {};
    Bc2M60FeedPresentation out;out.supply=supply;out.weaponFromPouch=pose.weaponFromPouch;
    out.weaponFromChain[0]=pose.weaponFromFeedRoot;
    for(unsigned i=0;i<pose.parentFromChainChild.size();++i){
        if(!feed_mechanism_detail::Pose(pose.parentFromChainChild[i]))return {};
        out.weaponFromChain[i+1]=feed_interaction_detail::Multiply(pose.parentFromChainChild[i],out.weaponFromChain[i]);
        if(!feed_mechanism_detail::Pose(out.weaponFromChain[i+1]))return {};
    }
    return out;
}
// The recognizer carries terminal15 at the user's actual grip offset. Solve
// backwards through all three explicit current joints so it stays attached to
// that hand while articulation changes. Pouch6 is independently retained.
// This does not choose articulation or infer a native feed-seat completion.
[[nodiscard]] inline std::optional<Bc2M60FeedPresentation> PlaceBc2M60FeedTerminal(
    interaction::FeedSupplyKey supply,const Bc2M60FeedPose& articulation,
    const math::Matrix4& weaponFromTerminal)noexcept {
    using namespace interaction;
    if(!feed_mechanism_detail::Pose(weaponFromTerminal))return {};
    auto pose=articulation;auto parent=weaponFromTerminal;
    for(unsigned i=static_cast<unsigned>(pose.parentFromChainChild.size());i>0;--i){
        const auto& local=pose.parentFromChainChild[i-1];
        if(!feed_mechanism_detail::Pose(local))return {};
        parent=feed_interaction_detail::Multiply(feed_interaction_detail::Inverse(local),parent);
    }
    pose.weaponFromFeedRoot=parent;
    return ComposeBc2M60FeedPresentation(supply,pose);
}
// Coupled carry for the caller's explicit same-container policy. Preserve the
// current authored root12-to-pouch6 relation and all three distinct joints.
// The caller supplies current articulation; this does not select/interpolate
// a clip or reinterpret a pouch target as an anatomical hand target.
[[nodiscard]] inline std::optional<Bc2M60FeedPresentation> PlaceBc2M60FeedPouch(
    interaction::FeedSupplyKey supply,const Bc2M60FeedPose& articulation,
    const math::Matrix4& weaponFromPouch)noexcept {
    using namespace interaction;
    if(!feed_mechanism_detail::Pose(weaponFromPouch)||!feed_mechanism_detail::Pose(articulation.weaponFromPouch)||
       !feed_mechanism_detail::Pose(articulation.weaponFromFeedRoot))return {};
    auto pose=articulation;
    const auto pouchFromRoot=feed_interaction_detail::Multiply(pose.weaponFromFeedRoot,
        feed_interaction_detail::Inverse(pose.weaponFromPouch));
    pose.weaponFromPouch=weaponFromPouch;
    pose.weaponFromFeedRoot=feed_interaction_detail::Multiply(pouchFromRoot,weaponFromPouch);
    return ComposeBc2M60FeedPresentation(supply,pose);
}
}
