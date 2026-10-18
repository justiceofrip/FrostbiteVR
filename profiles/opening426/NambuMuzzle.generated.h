#pragma once
#include "Bc2MuzzleAdmission.h"
#include <array>
namespace fvr::bc2::generated_muzzle {
// Exact installed SP_01 source and measured static flash; explicit candidate
// writes only when this header is selected. Native downstream acceptance pending.
inline constexpr std::array<MuzzleEnrollmentProfile,1> MuzzleEnrollments{{{
    "Nambu100","Objects/Weapons/Handheld/Beach/Nambu100/Nambu100",
    "Objects/Weapons/Handheld/Beach/Nambu100/Nambu100_Mesh",
    "ee265e653724b868925794f6e9c0425f8bf665b6c584021e4b1a98b503a247ce",
    "23fb8934e675d693459704c39be4e391afc2fb0b50367472d16e1ba7e054127d",
    "24ca715ffcaf75cedc25712d8c43e80a5bc7a51b72eb750593d5cb7725cc0120",
    "5e9779f47e5f567f20315f1b2afb448321adc16822256a2f69a0d472a9677b0a",
    "docs/NAMBU-AIM-426.md",0xa7f219a1426216abULL,
    math::Matrix4{{{{{1,0,0,0}},{{0,1,0,0}},{{0,0,1,0}},{{.000631691306f,.109905012f,-.884455979f,1}}}}},
    MuzzleAdmission::Candidate,{0,0,600},{0,0,1},{0,0,0},0,1,1,true
}}};
}
