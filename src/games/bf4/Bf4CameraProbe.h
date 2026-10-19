#pragma once
// Read-only camera discovery (RE task R3), developer sessions only. Static analysis
// (docs/BF4-CAMERA-RE.md) found the GameRenderer global (+0x2672378), the per-frame view
// copy it keeps at [renderer+0x60] and the view state its vtable slot 19 returns
// ([renderer+0x70] on the analysed build); this probe checks those candidates live and
// logs whether each looks like a camera. It never writes.
//
// Platform-neutral half: the plausibility rules, unit-tested. The Windows half reads
// memory through VirtualQuery-checked copies and feeds raw bytes here.
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>

namespace fvr::bf4 {

struct Matrix4f {
    std::array<float, 16> m{}; // row-major rows: right, up, forward, translation (Frostbite)
};

// A rigid camera transform: finite, rotation rows unit length and orthogonal (|dot| small)
// and a translation inside +-100 km. The w lanes are ignored: Frostbite's LinearTransform
// is four 16-byte Vec3 rows and the view setter (+0xB8A0A0) compares only x, y, z.
inline bool IsRigidTransform(const Matrix4f& t, float tolerance = 0.01f) {
    for (float v : t.m)
        if (!std::isfinite(v)) return false;
    auto row = [&](int r) { return std::array<float, 3>{t.m[r * 4], t.m[r * 4 + 1], t.m[r * 4 + 2]}; };
    auto dot = [](const std::array<float, 3>& a, const std::array<float, 3>& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; };
    for (int r = 0; r < 3; ++r)
        if (std::fabs(dot(row(r), row(r)) - 1.0f) > tolerance) return false;
    if (std::fabs(dot(row(0), row(1))) > tolerance || std::fabs(dot(row(0), row(2))) > tolerance ||
        std::fabs(dot(row(1), row(2))) > tolerance)
        return false;
    for (int i = 12; i < 15; ++i)
        if (std::fabs(t.m[i]) > 100000.0f) return false;
    return true;
}

struct ViewSample {
    Matrix4f at00, at40, at420; // candidate matrices in the render view
    float fovY = 0, nearPlane = 0, farPlane = 0, aspect = 0; // +0xB4, +0xBC, +0xC0, +0xC4
};

// Plausible per the settings path: FOV-Y in degrees (BF4 clamps 50..105 vertical, ADS can
// go lower), 0 < near < far, aspect of a real screen.
inline bool PlausibleProjection(const ViewSample& s) {
    return std::isfinite(s.fovY) && s.fovY > 5.0f && s.fovY < 170.0f && std::isfinite(s.nearPlane) &&
           s.nearPlane > 0.0f && s.nearPlane < 10.0f && std::isfinite(s.farPlane) && s.farPlane > s.nearPlane &&
           s.farPlane < 1.0e7f && std::isfinite(s.aspect) && s.aspect > 0.5f && s.aspect < 5.0f;
}

inline ViewSample ParseViewSample(const std::uint8_t* view /* >= 0x460 bytes */) {
    ViewSample s;
    std::memcpy(s.at00.m.data(), view + 0x00, 64);
    std::memcpy(s.at40.m.data(), view + 0x40, 64);
    std::memcpy(s.at420.m.data(), view + 0x420, 64);
    std::memcpy(&s.fovY, view + 0xB4, 4);
    std::memcpy(&s.nearPlane, view + 0xBC, 4);
    std::memcpy(&s.farPlane, view + 0xC0, 4);
    std::memcpy(&s.aspect, view + 0xC4, 4);
    return s;
}

// x64 "mov rax, [rcx+disp8]; ret" (48 8B 41 xx C3): returns disp8, or -1 for any other
// code. On the analysed build GameRenderer slot 19 is this shape with disp 0x70.
inline int FieldGetterOffset(const std::uint8_t* code /* >= 5 bytes */) {
    return code[0] == 0x48 && code[1] == 0x8B && code[2] == 0x41 && code[4] == 0xC3 ? code[3] : -1;
}

} // namespace fvr::bf4
