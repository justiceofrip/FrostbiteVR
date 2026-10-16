#pragma once
#include "Bc2PumpGeometry.h"
namespace fvr::bc2::bc2_pump_detail {
// Derived fixed geometry, not a complete animation clip or native capability.
// SPAS mesh SHA256 2d32588bc089d72f35365676ebfd551403dda42f5ee63d6f46ffd90e44640e27
// HandsIKPose SHA256 e8290c4bed7e5810d08eaeadcc5c1e98439a286a5c19ed95c104c8a4f53c8c3a
// Review report SHA256 a3ce628e6c680f20294e69e0f8d50061c490e682f14983d716d86ec052b56262
using FixedPart=Bc2PumpFixedPart;
inline const std::array<FixedPart,4> SpasFixedParts{{
{"jntWpn_6","jntWpn_1",math::Matrix4{{{{{1.f,-6.6396541e-16f,3.30681663e-17f,0.f}},{{-6.6396541e-16f,1.f,-1.60709255e-16f,0.f}},{{3.30681663e-17f,-1.60709255e-16f,1.f,-0.f}},{{-7.64844898e-09f,-0.0369246826f,-0.300233006f,1.f}}}}}},
{"jntWpn_12","jntWpn_1",math::Matrix4{{{{{0.99986825f,0.00835137956f,0.0139189659f,0.f}},{{-0.00835137956f,0.999965125f,-5.81251128e-05f,0.f}},{{-0.0139189659f,-5.81251128e-05f,0.999903125f,-0.f}},{{-1.24362769e-08f,-0.0226417575f,-0.335617959f,1.f}}}}}},
{"jntWpn_13","jntWpn_12",math::Matrix4{{{{{0.99986825f,0.00835137956f,0.0139189659f,0.f}},{{-0.00835137956f,0.999965125f,-5.81251128e-05f,0.f}},{{-0.0139189659f,-5.81251128e-05f,0.999903125f,-0.f}},{{0.00380779356f,0.032104574f,-0.641999445f,1.f}}}}}},
//408 observed side loop remains on its measured receiver attachment.
{"jntWpn_5","jntWpn_1",math::Matrix4{{{{{1.f,-6.6396541e-16f,3.30681663e-17f,0.f}},{{-6.6396541e-16f,1.f,-1.60709255e-16f,0.f}},{{3.30681663e-17f,-1.60709255e-16f,1.f,-0.f}},{{0.0223097634f,-0.0223100875f,-0.336929023f,1.f}}}}}},
}};
}
