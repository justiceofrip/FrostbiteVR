#pragma once
#include "fvr/interaction/FeedInteraction.h"
#include "Bc2ReloadConfigDescriptor.h"
#include <span>
#include <string_view>

namespace fvr::bc2 {
// Authored geometry uses canonical row-vector metres relative to its named
// weapon root. Geometry alone grants neither native binding nor stage policy.
inline constexpr unsigned MaxBc2FeedChainBones=8,MaxBc2FeedCoverCompanions=4;
inline constexpr unsigned MaxBc2FeedParts=2+MaxBc2FeedChainBones+MaxBc2FeedCoverCompanions;
static_assert(MaxBc2FeedParts<=16); // Part carry masks remain uint16_t.
enum class Bc2FeedPoseId : unsigned {
    Resting,PouchDetachSource,PouchWithdrawn,PouchAttachSource,
    PouchAttachedFeedUnseated,FeedSeatSource,FeedSeated,Count
};
struct Bc2FeedPose {
    double authoredSeconds=0;
    math::Matrix4 weaponFromPouch{},weaponFromFeedRoot{};
    // Explicit adjacent parent-local articulation, never fixed root offsets.
    // Only chainCount-1 entries are active.
    std::array<math::Matrix4,MaxBc2FeedChainBones-1> parentFromChainChild{};
    // Same-frame authored LeftHand wrist, local to the pouch except at
    // FeedSeatSource (local to the terminal). PouchWithdrawn also records its
    // same-frame grip for same-container reattachment. No native scripted
    // grip constraint is asserted.
    math::Matrix4 sourceFromWrist{};
    unsigned chainCount=0;
};
struct Bc2FeedChainBone {
    std::string_view bone,parent;
    unsigned index=0,parentIndex=0;
};
struct Bc2FeedCoverCompanion {
    Bc2FeedChainBone bone{};
    // Same authored reference frame as weaponFromCoverReference. A rigid
    // sibling follows that cover's delta, with its own measured bone frame.
    math::Matrix4 weaponFromReference{};
};
struct Bc2FeedGeometry {
    std::string_view asset,configurationPath,meshPath;
    std::uint64_t rigFingerprint=0;
    std::string_view meshSha256,lodSha256,skeletonSha256,clipSha256;
    std::array<Bc2FeedPose,static_cast<unsigned>(Bc2FeedPoseId::Count)> poses{};
    std::array<Bc2FeedChainBone,MaxBc2FeedChainBones> chain{};
    math::Matrix4 weaponFromHinge{},hingeFromOpeningContact{},hingeFromClosingContact{};
    float coverTravelRadians=0;
    unsigned chainCount=0;
    Bc2FeedChainBone rootBone{},pouchBone{},coverBone{};
    math::Matrix4 weaponFromCoverReference{};
    std::array<Bc2FeedCoverCompanion,MaxBc2FeedCoverCompanions> coverCompanions{};
    unsigned coverCompanionCount=0;
};
[[nodiscard]] inline unsigned Bc2FeedPartCount(const Bc2FeedGeometry& geometry)noexcept {
    return 2+geometry.chainCount+geometry.coverCompanionCount;
}
using Bc2M60FeedPoseId=Bc2FeedPoseId;
using Bc2M60FeedPose=Bc2FeedPose;
using Bc2M60FeedChainBone=Bc2FeedChainBone;
using Bc2M60FeedGeometry=Bc2FeedGeometry;
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
[[nodiscard]] inline Bc2FeedGeometryAvailability Bc2FeedOperationAvailability(
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
inline math::Matrix4 Terminal(const Bc2FeedPose& pose)noexcept {
    if(!pose.chainCount||pose.chainCount>MaxBc2FeedChainBones)return {};
    auto world=pose.weaponFromFeedRoot;
    for(unsigned i=0;i+1<pose.chainCount;++i)world=interaction::feed_interaction_detail::Multiply(pose.parentFromChainChild[i],world);
    return world;
}
constexpr math::Matrix4 MeasuredFrame(std::array<float,16> values)noexcept {
    math::Matrix4 out{};
    for(unsigned i=0;i<4;++i)for(unsigned j=0;j<4;++j)out.values[i][j]=values[i*4+j];
    return out;
}
// M60 provenance: handoff/tasks/CHAT14/20261030-profile18/proof01.json.
// BEGIN MEASURED FRAMES
inline const std::array<Bc2M60FeedPose,7> M60MeasuredPoses=[] {
    std::array<Bc2M60FeedPose,7> poses{{
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
    for(auto& pose:poses)pose.chainCount=4;
    return poses;
}();
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
    M60HingeFrame,M60OpeningContact,M60ClosingContact,M60CoverTravel,4,
    {"jntWpn_1","",10,0},{"jntWpn_6","jntWpn_1",41,10},{"jntWpn_9","jntWpn_1",34,10},
    MeasuredFrame({1.f,5.3931e-6f,5.3928e-6f,0.f,-5.3928e-6f,.9999999991f,-4.31197e-5f,0.f,
        -5.3931e-6f,4.31197e-5f,.9999999991f,0.f,2.63e-8f,.0745346174f,-.4183450341f,1.f})
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
[[nodiscard]] inline bool ValidateBc2FeedPose(const Bc2FeedPose& pose,unsigned chainCount)noexcept {
    using interaction::feed_mechanism_detail::Pose;
    if(!chainCount||chainCount>MaxBc2FeedChainBones||pose.chainCount!=chainCount||
       !std::isfinite(pose.authoredSeconds)||pose.authoredSeconds<0||
       !Pose(pose.weaponFromPouch)||!Pose(pose.weaponFromFeedRoot)||!Pose(pose.sourceFromWrist))return false;
    for(unsigned i=0;i+1<chainCount;++i)if(!Pose(pose.parentFromChainChild[i]))return false;
    return true;
}
[[nodiscard]] inline bool ValidateBc2FeedGeometry(const Bc2FeedGeometry& g)noexcept {
    using interaction::feed_mechanism_detail::Pose;
    if(g.asset.empty()||g.configurationPath.empty()||g.meshPath.empty()||!g.rigFingerprint||
       !g.chainCount||g.chainCount>MaxBc2FeedChainBones||g.coverCompanionCount>MaxBc2FeedCoverCompanions||g.rootBone.bone.empty()||
       !Pose(g.weaponFromHinge)||!Pose(g.hingeFromOpeningContact)||!Pose(g.hingeFromClosingContact)||
       !Pose(g.weaponFromCoverReference)||!std::isfinite(g.coverTravelRadians)||
       std::abs(g.coverTravelRadians)<.001f||std::abs(g.coverTravelRadians)>6.283185307f)return false;
    const auto validBone=[](const Bc2FeedChainBone& b){return !b.bone.empty()&&!b.parent.empty()&&
        b.index<4096&&b.parentIndex<4096&&b.index!=b.parentIndex&&b.bone!=b.parent;};
    if(g.rootBone.index>=4096||!validBone(g.pouchBone)||!validBone(g.coverBone))return false;
    std::array<const Bc2FeedChainBone*,MaxBc2FeedParts+1> bones{};
    bones[0]=&g.rootBone;bones[1]=&g.pouchBone;bones[2]=&g.coverBone;
    for(unsigned i=0;i<g.chainCount;++i){
        const auto& b=g.chain[i];const auto& parent=i?g.chain[i-1]:g.rootBone;
        if(!validBone(b)||b.parent!=parent.bone||b.parentIndex!=parent.index)return false;
        bones[i+3]=&b;
    }
    for(unsigned i=0;i<g.coverCompanionCount;++i){
        const auto& companion=g.coverCompanions[i];const auto& b=companion.bone;
        if(!validBone(b)||b.parent!=g.rootBone.bone||b.parentIndex!=g.rootBone.index||
           !Pose(companion.weaponFromReference))return false;
        bones[g.chainCount+3+i]=&b;
    }
    for(unsigned i=0;i<Bc2FeedPartCount(g)+1;++i)for(unsigned j=0;j<i;++j)
        if(bones[i]->bone==bones[j]->bone||bones[i]->index==bones[j]->index)return false;
    for(const auto& pose:g.poses)if(!ValidateBc2FeedPose(pose,g.chainCount))return false;
    return true;
}
[[nodiscard]] inline std::optional<interaction::FeedMotionProfile> BuildBc2FeedMotion(
    const Bc2FeedGeometry& geometry,interaction::FeedPhysicalOperation operation,
    const Bc2FeedInteractionPolicy& policy)noexcept {
    using namespace interaction;
    if(!ValidateBc2FeedGeometry(geometry)||
       Bc2FeedOperationAvailability(operation)!=Bc2FeedGeometryAvailability::MeasuredAuthoredGeometry)return {};
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
        Bc2FeedPoseId source,destination;
        if(operation==FeedPhysicalOperation::DetachContainer){
            source=Bc2FeedPoseId::PouchDetachSource;destination=Bc2FeedPoseId::PouchWithdrawn;
        }else if(operation==FeedPhysicalOperation::AttachContainer){
            source=Bc2FeedPoseId::PouchWithdrawn;destination=Bc2FeedPoseId::PouchAttachedFeedUnseated;
        }else {source=Bc2FeedPoseId::PouchAttachedFeedUnseated;destination=Bc2FeedPoseId::FeedSeated;}
        const auto& a=geometry.poses[static_cast<unsigned>(source)];
        const auto& b=geometry.poses[static_cast<unsigned>(destination)];
        const bool feed=operation==FeedPhysicalOperation::SeatFeed;
        out.kind=FeedMotionKind::FreePlacement;
        // Keep the authored terminal-local wrist reference while the object
        // remains at the prior stage's actual retained destination.
        const auto& grip=feed?geometry.poses[static_cast<unsigned>(Bc2FeedPoseId::FeedSeatSource)].sourceFromWrist:a.sourceFromWrist;
        out.placement={feed?bc2_feed_profile_detail::Terminal(a):a.weaponFromPouch,
            feed?bc2_feed_profile_detail::Terminal(b):b.weaponFromPouch,grip};
    }
    return feed_interaction_detail::Motion(out)?std::optional<FeedMotionProfile>{out}:std::nullopt;
}
// Freeze this result for the next gesture. A preceding accepted placement may
// end anywhere inside the policy tolerance, so use its actual retained object
// pose as the next source. Only this mod-owned source changes; the receiver
// destination and authored bone-local wrist reference remain measured data.
[[nodiscard]] inline std::optional<interaction::FeedMotionProfile> BuildBc2FeedMotionFromRetainedPose(
    const Bc2FeedGeometry& geometry,interaction::FeedPhysicalOperation operation,
    const Bc2FeedInteractionPolicy& policy,const Bc2FeedPose& retained)noexcept {
    using namespace interaction;
    auto motion=BuildBc2FeedMotion(geometry,operation,policy);
    if(!motion||motion->kind!=FeedMotionKind::FreePlacement||
       !feed_mechanism_detail::Pose(retained.weaponFromPouch)||!feed_mechanism_detail::Pose(retained.weaponFromFeedRoot))return {};
    if(!ValidateBc2FeedPose(retained,geometry.chainCount))return {};
    motion->placement.weaponFromSource=operation==FeedPhysicalOperation::SeatFeed?
        bc2_feed_profile_detail::Terminal(retained):retained.weaponFromPouch;
    return feed_interaction_detail::Motion(*motion)?motion:std::nullopt;
}
[[nodiscard]] inline interaction::FeedInteractionProfile BuildBc2FeedInteractionProfile(
    const Bc2FeedGeometry& geometry,std::uint64_t profileId,std::uint64_t profileRevision,
    const Bc2FeedInteractionPolicy& policy)noexcept {
    interaction::FeedInteractionProfile out;
    if(!profileId||!profileRevision)return out;
    out.profileId=profileId;out.profileRevision=profileRevision;
    for(unsigned i=1;i<out.operations.size();++i){
        const auto motion=BuildBc2FeedMotion(geometry,static_cast<interaction::FeedPhysicalOperation>(i),policy);
        if(motion)out.operations[i]=*motion;
    }
    return out;
}
struct Bc2FeedPresentation {
    interaction::FeedSupplyKey supply{};
    math::Matrix4 weaponFromPouch{};
    std::array<math::Matrix4,MaxBc2FeedChainBones> weaponFromChain{};
    unsigned chainCount=0;
};
using Bc2M60FeedPresentation=Bc2FeedPresentation;
// Consumer retains one articulated pose per exact supply lifetime, including
// stow/tracking loss. Its free-placement target updates the selected object;
// child articulation must be supplied explicitly, never replaced by a single
// belt mesh or silently flattened to fixed root offsets. This composes poses,
// not a constraint solver, animation interpolator, or custody state machine.
[[nodiscard]] inline std::optional<Bc2FeedPresentation> ComposeBc2FeedPresentation(
    interaction::FeedSupplyKey supply,const Bc2FeedPose& pose)noexcept {
    using namespace interaction;
    if(!feed_mechanism_detail::Supply(supply,true)||!ValidateBc2FeedPose(pose,pose.chainCount))return {};
    Bc2FeedPresentation out;out.supply=supply;out.weaponFromPouch=pose.weaponFromPouch;out.chainCount=pose.chainCount;
    out.weaponFromChain[0]=pose.weaponFromFeedRoot;
    for(unsigned i=0;i+1<pose.chainCount;++i){
        if(!feed_mechanism_detail::Pose(pose.parentFromChainChild[i]))return {};
        out.weaponFromChain[i+1]=feed_interaction_detail::Multiply(pose.parentFromChainChild[i],out.weaponFromChain[i]);
        if(!feed_mechanism_detail::Pose(out.weaponFromChain[i+1]))return {};
    }
    return out;
}
// Solve backwards through every active current joint to keep the terminal at
// the user's actual grip offset. The pouch is independently retained.
// This does not choose articulation or infer a native feed-seat completion.
[[nodiscard]] inline std::optional<Bc2FeedPresentation> PlaceBc2FeedTerminal(
    interaction::FeedSupplyKey supply,const Bc2FeedPose& articulation,
    const math::Matrix4& weaponFromTerminal)noexcept {
    using namespace interaction;
    if(!feed_mechanism_detail::Pose(weaponFromTerminal)||!ValidateBc2FeedPose(articulation,articulation.chainCount))return {};
    auto pose=articulation;auto parent=weaponFromTerminal;
    for(unsigned i=pose.chainCount-1;i>0;--i){
        const auto& local=pose.parentFromChainChild[i-1];
        if(!feed_mechanism_detail::Pose(local))return {};
        parent=feed_interaction_detail::Multiply(feed_interaction_detail::Inverse(local),parent);
    }
    pose.weaponFromFeedRoot=parent;
    return ComposeBc2FeedPresentation(supply,pose);
}
// Coupled carry for the caller's explicit same-container policy. Preserve the
// current authored feed-root-to-pouch relation and all active distinct joints.
// The caller supplies current articulation; this does not select/interpolate
// a clip or reinterpret a pouch target as an anatomical hand target.
[[nodiscard]] inline std::optional<Bc2FeedPresentation> PlaceBc2FeedPouch(
    interaction::FeedSupplyKey supply,const Bc2FeedPose& articulation,
    const math::Matrix4& weaponFromPouch)noexcept {
    using namespace interaction;
    if(!feed_mechanism_detail::Pose(weaponFromPouch)||!feed_mechanism_detail::Pose(articulation.weaponFromPouch)||
       !feed_mechanism_detail::Pose(articulation.weaponFromFeedRoot))return {};
    auto pose=articulation;
    const auto pouchFromRoot=feed_interaction_detail::Multiply(pose.weaponFromFeedRoot,
        feed_interaction_detail::Inverse(pose.weaponFromPouch));
    pose.weaponFromPouch=weaponFromPouch;
    pose.weaponFromFeedRoot=feed_interaction_detail::Multiply(pouchFromRoot,weaponFromPouch);
    return ComposeBc2FeedPresentation(supply,pose);
}
// Legacy entry points retain their exact M60 restriction. Shared consumers use
// the generic helpers with geometry obtained from the selected registration.
inline Bc2FeedGeometryAvailability Bc2M60FeedOperationAvailability(interaction::FeedPhysicalOperation op)noexcept {
    return Bc2FeedOperationAvailability(op);
}
inline std::optional<interaction::FeedMotionProfile> BuildBc2M60FeedMotion(const Bc2FeedGeometry& g,
    interaction::FeedPhysicalOperation op,const Bc2FeedInteractionPolicy& p)noexcept {
    return &g==&bc2_feed_profile_detail::M60Geometry?BuildBc2FeedMotion(g,op,p):std::nullopt;
}
inline std::optional<interaction::FeedMotionProfile> BuildBc2M60FeedMotionFromRetainedPose(const Bc2FeedGeometry& g,
    interaction::FeedPhysicalOperation op,const Bc2FeedInteractionPolicy& p,const Bc2FeedPose& retained)noexcept {
    return &g==&bc2_feed_profile_detail::M60Geometry?BuildBc2FeedMotionFromRetainedPose(g,op,p,retained):std::nullopt;
}
inline interaction::FeedInteractionProfile BuildBc2M60FeedInteractionProfile(const Bc2FeedGeometry& g,
    std::uint64_t id,std::uint64_t revision,const Bc2FeedInteractionPolicy& p)noexcept {
    return &g==&bc2_feed_profile_detail::M60Geometry?BuildBc2FeedInteractionProfile(g,id,revision,p):interaction::FeedInteractionProfile{};
}
inline std::optional<Bc2FeedPresentation> ComposeBc2M60FeedPresentation(interaction::FeedSupplyKey key,const Bc2FeedPose& p)noexcept {
    return p.chainCount==4?ComposeBc2FeedPresentation(key,p):std::nullopt;
}
inline std::optional<Bc2FeedPresentation> PlaceBc2M60FeedTerminal(interaction::FeedSupplyKey key,
    const Bc2FeedPose& p,const math::Matrix4& target)noexcept {
    return p.chainCount==4?PlaceBc2FeedTerminal(key,p,target):std::nullopt;
}
inline std::optional<Bc2FeedPresentation> PlaceBc2M60FeedPouch(interaction::FeedSupplyKey key,
    const Bc2FeedPose& p,const math::Matrix4& target)noexcept {
    return p.chainCount==4?PlaceBc2FeedPouch(key,p,target):std::nullopt;
}

enum class Bc2FeedNativeClass : std::uint8_t {Unsupported,AutomaticMagazineZeroBoltTransfer};
// Registry storage and returned rows are immutable. Geometry remains authored
// evidence; only an explicit independently reviewed native class admits this
// exact configuration. The profile digest is NOT a magazine registry digest.
struct Bc2FeedRegistration {
    std::uint64_t key=0;
    std::string_view profileDigest;
    Bc2FeedGeometry geometry{};
    Bc2FeedInteractionPolicy interactionPolicy{};
    interaction::FeedMechanismProfile mechanism{};
    ReloadConfigDescriptor config{};
    Bc2FeedNativeClass nativeClass=Bc2FeedNativeClass::Unsupported;
    bool enabled=false,reviewed=false;
};
inline constexpr unsigned MaxGeneratedBc2FeedProfiles=128;
[[nodiscard]] inline std::optional<std::uint64_t> Bc2FeedProfileKey(std::string_view digest)noexcept {
    if(digest.size()!=64)return {};
    std::uint64_t key=0;
    for(unsigned i=0;i<64;++i){const char c=digest[i];unsigned n;
        if(c>='0'&&c<='9')n=unsigned(c-'0');else if(c>='a'&&c<='f')n=10+unsigned(c-'a');else return {};
        if(i<16)key=(key<<4)|n;
    }
    return key?std::optional<std::uint64_t>{key}:std::nullopt;
}
namespace bc2_feed_profile_detail {
inline constexpr std::array<ReloadTimingWord,6> M60ReloadTiming{{
    {0x10,std::bit_cast<std::uint32_t>(.7f)},{0x14,0},{0x18,std::bit_cast<std::uint32_t>(7.8f)},
    {0x20,1},{0x24,2},{0x2c,0}}};
inline const Bc2FeedRegistration M60Registration=[] {
    Bc2FeedRegistration out;
    out.key=0x35cbdca6154b3c90ull;
    out.profileDigest="35cbdca6154b3c903553c2343ff1132c2c6ab3ce3726265a7f2c7d76e3c545c5";
    out.geometry=M60Geometry;
    out.interactionPolicy={.95f,.15f,.02f,.18f,.08f,.35f,60000000,10000000000ll,100000000};
    auto& m=out.mechanism;m.family=interaction::FeedFamily::Belt;m.id=0x4d363046454544ull;m.revision=1;m.ammunitionKind=1;
    m.cover=m.container=m.containerNeedsOpen=m.feedNeedsOpen=m.sameContainerReplenishment=m.coupledContainerDetach=true;
    m.maxAgeNs=100000000;m.contacts[0]={1,.08f,.65f};m.contacts[2]={2,.08f,.65f};m.contacts[3]={3,.08f,.65f};
    out.config={M60Geometry.asset,M60Geometry.configurationPath,
        {2,1,8,29,100,2,0,7.8f,.7f,0,0,0,false,false},M60ReloadTiming,ReloadDescriptorAdmission::ReviewedNative};
    out.nativeClass=Bc2FeedNativeClass::AutomaticMagazineZeroBoltTransfer;out.enabled=out.reviewed=true;
    return out;
}();
inline bool SameSelector(const Bc2FeedRegistration& a,const Bc2FeedRegistration& b)noexcept {
    // A configuration cannot have competing geometry/native-policy rows even
    // if the two candidates claim different meshes or rig fingerprints.
    return (a.geometry.asset==b.geometry.asset&&a.geometry.configurationPath==b.geometry.configurationPath)||
        (a.config.assetName==b.config.assetName&&a.config.assetPath==b.config.assetPath);
}
inline bool ExactGeometry(const Bc2FeedRegistration& row,std::string_view asset,
    std::string_view configuration,std::string_view mesh,std::uint64_t rig)noexcept {
    const auto& g=row.geometry;return g.asset==asset&&g.configurationPath==configuration&&g.meshPath==mesh&&g.rigFingerprint==rig;
}
}
[[nodiscard]] inline bool ValidateBc2FeedRegistration(const Bc2FeedRegistration& row)noexcept {
    const auto key=Bc2FeedProfileKey(row.profileDigest);const auto& g=row.geometry;const auto& p=row.mechanism;
    const auto& c=row.config;const auto& v=c.values;
    if(!key||*key!=row.key||!row.enabled||!row.reviewed||
       row.nativeClass!=Bc2FeedNativeClass::AutomaticMagazineZeroBoltTransfer||
       c.admission!=ReloadDescriptorAdmission::ReviewedNative||c.assetName!=g.asset||c.assetPath!=g.configurationPath||
       c.assetName.size()>=64||c.assetPath.size()>=192||!ValidateBc2FeedGeometry(g)||
       !interaction::ValidateFeedMechanismProfile(p)||p.maxAgeNs>100000000||!p.cover||p.latch||!p.container||p.charge||
       !p.containerNeedsOpen||!p.feedNeedsOpen||p.detachNeedsUnseated||!p.sameContainerReplenishment||!p.coupledContainerDetach||
       v.fireLogicType!=2||v.reloadType!=1||v.fireInputAction!=8||v.reloadInputAction!=29||
       v.baseCapacity<=0||v.baseCapacity>1000000||v.numberOfMagazines<=0||v.numberOfMagazines>1000000||
       v.reloadDelay!=0||!std::isfinite(v.reloadTime)||v.reloadTime<=0||
       !std::isfinite(v.reloadThreshold)||v.reloadThreshold<=0||v.reloadThreshold>1||
       v.postReloadTime!=0||v.boltDelay!=0||v.boltTime!=0||v.holdBoltUntilFireRelease||v.holdBoltUntilZoomRelease||
       c.timing.size()!=6)return false;
    for(const auto hash:{g.meshSha256,g.lodSha256,g.skeletonSha256,g.clipSha256})
        if(hash.size()!=64||std::any_of(hash.begin(),hash.end(),[](char x){return !(x>='0'&&x<='9')&&!(x>='a'&&x<='f');}))return false;
    const std::array<ReloadTimingWord,6> expected{{
        {0x10,std::bit_cast<std::uint32_t>(v.reloadThreshold)},{0x14,0},{0x18,std::bit_cast<std::uint32_t>(v.reloadTime)},
        {0x20,1},{0x24,2},{0x2c,0}}};
    for(unsigned i=0;i<expected.size();++i){const auto& word=c.timing[i];
        if(word.offset!=expected[i].offset||word.expected!=expected[i].expected||
           (word.comparison!=ReloadTimingComparison::Bits&&
            !(word.comparison==ReloadTimingComparison::Float&&(i==0||i==1||i==2||i==5))))return false;
    }
    if(row.interactionPolicy.maxInputGapNs>p.maxAgeNs)return false;
    for(const auto op:{interaction::FeedPhysicalOperation::OpenCover,interaction::FeedPhysicalOperation::CloseCover,
        interaction::FeedPhysicalOperation::DetachContainer,interaction::FeedPhysicalOperation::AttachContainer,
        interaction::FeedPhysicalOperation::SeatFeed})if(!BuildBc2FeedMotion(g,op,row.interactionPolicy))return false;
    return true;
}
// Pure span overloads support offline validation without enrolling fixtures.
[[nodiscard]] inline const Bc2FeedRegistration* SelectBc2FeedRegistration(
    std::span<const Bc2FeedRegistration> rows,std::uint64_t key)noexcept {
    if(!key||rows.size()>MaxGeneratedBc2FeedProfiles+1)return nullptr;
    const Bc2FeedRegistration* found=nullptr;
    for(const auto& row:rows)if(row.key==key){if(found)return nullptr;found=&row;}
    if(!found||!ValidateBc2FeedRegistration(*found))return nullptr;
    for(const auto& row:rows)if(&row!=found&&(row.key==key||row.profileDigest==found->profileDigest||
        bc2_feed_profile_detail::SameSelector(row,*found)))return nullptr;
    return found;
}
[[nodiscard]] inline const Bc2FeedRegistration* SelectBc2FeedRegistration(
    std::span<const Bc2FeedRegistration> rows,const ReloadObservedConfig& config)noexcept {
    if(rows.size()>MaxGeneratedBc2FeedProfiles+1)return nullptr;
    const Bc2FeedRegistration* found=nullptr;
    for(const auto& row:rows)if(MatchesReloadDescriptor(config,row.config)){
        if(found||SelectBc2FeedRegistration(rows,row.key)!=&row)return nullptr;found=&row;
    }
    return found;
}
[[nodiscard]] inline const Bc2FeedRegistration* SelectBc2FeedRegistration(std::span<const Bc2FeedRegistration> rows,
    std::string_view asset,std::string_view configuration,std::string_view mesh,std::uint64_t rig)noexcept {
    if(rows.size()>MaxGeneratedBc2FeedProfiles+1)return nullptr;
    const Bc2FeedRegistration* found=nullptr;
    for(const auto& row:rows)if(bc2_feed_profile_detail::ExactGeometry(row,asset,configuration,mesh,rig)){
        if(found||SelectBc2FeedRegistration(rows,row.key)!=&row)return nullptr;found=&row;
    }
    return found;
}
// The optional generated header is compiled once in Bc2FeedRegistry.cpp.
// Its immutable table validates once, not on every input/render callback.
[[nodiscard]] std::span<const Bc2FeedRegistration> RegisteredBc2FeedProfiles()noexcept;
[[nodiscard]] const Bc2FeedRegistration* FindBc2FeedRegistration(std::uint64_t key)noexcept;
[[nodiscard]] const Bc2FeedRegistration* FindBc2FeedRegistration(const ReloadObservedConfig& config)noexcept;
[[nodiscard]] const Bc2FeedRegistration* FindBc2FeedRegistration(std::string_view asset,
    std::string_view configuration,std::string_view mesh,std::uint64_t rig)noexcept;
[[nodiscard]] inline const Bc2FeedRegistration* FindBc2FeedRegistration(std::uint64_t key,
    const ReloadObservedConfig& config,std::string_view asset,std::string_view configuration,
    std::string_view mesh,std::uint64_t rig)noexcept {
    const auto* row=FindBc2FeedRegistration(key);
    return row&&MatchesReloadDescriptor(config,row->config)&&
        bc2_feed_profile_detail::ExactGeometry(*row,asset,configuration,mesh,rig)?row:nullptr;
}
}
