#pragma once
#include "fvr/interaction/BoltWeaponCustody.h"
#include <string_view>
namespace fvr::bc2 {
// Exact authored M95 BoltAction content. This header grants no native authority.
// Native runtime/state mapping remains false until measured original callbacks
// and a state-labelled part anchor are joined by the game adapter.
inline constexpr bool M95NativeBoltHandleVerified=false;
inline constexpr std::string_view M95BoltAsset="Objects/Weapons/Handheld/BU_sni_M95/SP_sni_M95";
inline constexpr std::string_view M95BoltMesh="Objects/Weapons/Handheld/BU_sni_M95/BU_sni_M95_Mesh";
inline constexpr std::uint64_t M95BoltRigFingerprint=0xa7f219a1426216abull;
inline constexpr unsigned M95BoltRigidTriangles=556;
inline constexpr float M95BoltStroke=.1484906094f;
inline constexpr float M95BoltUnlock=-.7456515125f;
// Frame .925s in named 1p_BoltAction; window [.891667,.941667]s.
// Wrist relative to moving part max residual .002923786m / .018579508rad;
// fingers .010464612rad; knuckle-to-part-vertex distance <=.015442252m.
inline const math::Matrix4 M95AuthoredPartFromWrist={{{{-0.982491488f,-0.03613356397f,-0.1827699143f,0.f},{0.08084192568f,-0.9665288047f,-0.2434885065f,0.f},{-0.1678542793f,-0.2540008569f,0.9525274304f,0.f},{-0.039270613f,-0.06850900575f,0.03727409071f,1.f}}}};
// First authored frame is an offline reference, NOT a semantic native closed pose.
inline const math::Matrix4 M95AuthoredReferencePart={{{{0.9999999991f,4.311970263e-05f,5.392844817e-06f,0.f},{-4.311967355e-05f,0.9999999991f,-5.393077359e-06f,0.f},{-5.393077359e-06f,5.392844816e-06f,1.f,0.f},{0.005220633154f,0.04712387638f,-0.2058009052f,1.f}}}};
struct M95AuthoredFinger {std::string_view name;math::Matrix4 wristFromFinger;};
inline const std::array<M95AuthoredFinger,15> M95BoltFingers{{
    {"RightHandThumb1",{{{{0.5084160212f,-0.6239764479f,-0.5934362155f,0.f},{0.543488687f,0.7670682612f,-0.3409198289f,0.f},{0.6679320298f,-0.1491967666f,0.7291139338f,0.f},{0.0292502325f,0.004965701606f,-0.005829925183f,1.f}}}}},
    {"RightHandThumb2",{{{{0.06426834407f,-0.4745802116f,-0.877862861f,0.f},{0.4811237913f,0.7854286294f,-0.3893863962f,0.f},{0.874293702f,-0.3973354891f,0.2788100281f,0.f},{0.05721311352f,-0.02935300284f,-0.03846891686f,1.f}}}}},
    {"RightHandThumb3",{{{{-0.5123821785f,-0.1604475513f,-0.8436356361f,0.f},{0.3919688741f,0.8303923054f,-0.3959911878f,0.f},{0.7640843572f,-0.5335777379f,-0.3625877725f,0.f},{0.05957553731f,-0.04679797675f,-0.07073805562f,1.f}}}}},
    {"RightHandIndex1",{{{{0.2035632462f,-0.7670103154f,-0.6084876177f,0.f},{0.2409501127f,0.6416327331f,-0.7281829983f,0.f},{0.9489494444f,0.00161613495f,0.3154240639f,0.f},{0.04561219737f,0.03559557721f,-0.06105295941f,1.f}}}}},
    {"RightHandIndex2",{{{{-0.1855080596f,-0.8794547072f,0.438344816f,0.f},{0.2316396842f,-0.4726540859f,-0.8502594732f,0.f},{0.9549501644f,-0.05619193029f,0.291397753f,0.f},{0.05515743456f,-0.0003701264277f,-0.08958541183f,1.f}}}}},
    {"RightHandIndex3",{{{{-0.4356861735f,0.05375922969f,0.8984917937f,0.f},{0.00657536019f,-0.9979980847f,0.06290141121f,0.f},{0.9000746207f,0.03331318233f,0.4344604804f,0.f},{0.04934080013f,-0.02794556728f,-0.07584104108f,1.f}}}}},
    {"RightHandMiddle1",{{{{0.2479384945f,-0.7695718111f,-0.5884604749f,0.f},{0.2045027504f,0.6353152983f,-0.7446832191f,0.f},{0.9469451558f,0.0642938506f,0.3148986706f,0.f},{0.0233905483f,0.03437005728f,-0.07057954371f,1.f}}}}},
    {"RightHandMiddle2",{{{{-0.01942872075f,-0.9671970097f,0.2532833774f,0.f},{0.3188354138f,-0.2460983607f,-0.9153029967f,0.f},{0.9476109453f,0.06297254411f,0.3131580352f,0.f},{0.03542385257f,-0.002979898288f,-0.09913954588f,1.f}}}}},
    {"RightHandMiddle3",{{{{-0.2483232463f,-0.4506550666f,0.857464621f,0.f},{0.1905604159f,-0.8906206523f,-0.4128941529f,0.f},{0.9497485421f,0.06086759839f,0.3070388286f,0.f},{0.03481476938f,-0.03330116587f,-0.09119920592f,1.f}}}}},
    {"RightHandRing1",{{{{0.2780134077f,-0.6637642144f,-0.6943526574f,0.f},{-0.07099087796f,0.7066749692f,-0.703967885f,0.f},{0.9579503329f,0.2450052154f,0.149343912f,0.f},{0.003914479632f,0.02270486951f,-0.07432132959f,1.f}}}}},
    {"RightHandRing2",{{{{0.270442153f,-0.9619722577f,-0.03834602017f,0.f},{0.1742302351f,0.0880767262f,-0.980758031f,0.f},{0.9468394093f,0.2585572774f,0.1914243121f,0.f},{0.01597613248f,-0.006092639203f,-0.1044459224f,1.f}}}}},
    {"RightHandRing3",{{{{0.1266922765f,-0.8071535025f,0.5765867589f,0.f},{0.3433385055f,-0.5096582088f,-0.7889025167f,0.f},{0.9306276042f,0.2979122919f,0.2125571185f,0.f},{0.0240476592f,-0.03480334383f,-0.105590385f,1.f}}}}},
    {"RightHandPinky1",{{{{0.2328196573f,-0.2456210969f,-0.9409916492f,0.f},{-0.4647006837f,0.8218757304f,-0.3295050201f,0.f},{0.8543115835f,0.5139947087f,0.07720853427f,0.f},{-0.01353873685f,0.006115453318f,-0.07387898117f,1.f}}}}},
    {"RightHandPinky2",{{{{0.4917379057f,-0.6498297743f,-0.579581829f,0.f},{-0.1935251803f,0.5674071401f,-0.8003731267f,0.f},{0.8489651563f,0.5057374831f,0.1532571746f,0.f},{-0.004984338684f,-0.002909302945f,-0.1084534554f,1.f}}}}},
    {"RightHandPinky3",{{{{0.5613515455f,-0.8260910077f,-0.04957912215f,0.f},{0.1553462841f,0.1640270092f,-0.9741471512f,0.f},{0.8128665169f,0.5391370765f,0.2204069835f,0.f},{0.006597331938f,-0.01821443667f,-0.1221040724f,1.f}}}}}
}};
// Pure geometry policy. A caller supplies an independently verified native
// closed pose for runtime use; offline tests explicitly use authored reference.
inline interaction::WeaponCycleProfile M95BoltProfile(const math::Matrix4& closedPart,std::uint64_t revision)noexcept {
    interaction::WeaponCycleProfile p;p.id=0x4d3935424f4c5401ull;p.revision=revision;
    p.family=interaction::WeaponCycleFamily::Bolt;p.hands={interaction::InteractionHand::Right,interaction::InteractionHand::Left};
    p.axis={0,0,1};p.closedContact=closedPart;p.stroke=M95BoltStroke;p.unlockRadians=M95BoltUnlock;
    // Explicit gesture tolerances, not measured native thresholds.
    p.frontTolerance=p.rearTolerance=.004f;p.contactRadius=.04f;p.lateralTolerance=.015f;
    p.rotationTolerance=.035f;p.maxStepMeters=.025f;p.endpointDwellNs=30000000;p.maximumCycleNs=30000000000ll;
    return p;
}
inline std::optional<math::Matrix4> M95BoltPartFromRawWrist(const math::Matrix4& rawWrist)noexcept {
    if(!interaction::feed_mechanism_detail::Pose(rawWrist))return {};
    const auto inverse=interaction::InverseRigid(M95AuthoredPartFromWrist);if(!inverse)return {};
    return interaction::Multiply(*inverse,rawWrist);
}
inline math::Matrix4 M95BoltWristFromPart(const math::Matrix4& part)noexcept {
    return interaction::Multiply(M95AuthoredPartFromWrist,part);
}
}
