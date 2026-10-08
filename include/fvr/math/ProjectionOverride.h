#pragma once
#include <array>
#include <cmath>
#include <optional>
#include <span>
namespace fvr::math {
// Row-vector matrices. A depth-biased effect may replace clip Z while inheriting
// X/Y/W from its parent eye. Preserve that Z column exactly when changing eyes.
inline std::optional<std::array<float,16>> RetargetDepthProjection(
    std::span<const float,16> effect,std::span<const float,16> source,
    std::span<const float,16> target)noexcept {
    std::array<float,16> result{};
    for(unsigned i=0;i<16;++i){
        if(!std::isfinite(effect[i])||!std::isfinite(source[i])||!std::isfinite(target[i]))return {};
        if(i%4!=2&&std::abs(effect[i]-source[i])>1e-6f)return {};
        result[i]=i%4==2?effect[i]:target[i];
    }
    return result;
}
// An authored first-person FOV may differ from the world FOV. Use the target
// eye's perspective X/Y/W while retaining the weapon pass's native clip Z.
inline std::optional<std::array<float,16>> RetargetFirstPersonProjection(
    std::span<const float,16> weapon,std::span<const float,16> eye)noexcept {
    for(unsigned i=0;i<16;++i)if(!std::isfinite(weapon[i])||!std::isfinite(eye[i]))return {};
    if(weapon[0]<=0||weapon[5]<=0||eye[0]<=0||eye[5]<=0||std::abs(weapon[11])!=1||weapon[11]!=eye[11]||weapon[15]!=0||eye[15]!=0)return {};
    for(unsigned i:{1u,3u,4u,7u,12u,13u})if(weapon[i]!=0||eye[i]!=0)return {};
    std::array<float,16> out{};for(unsigned i=0;i<16;++i)out[i]=i%4==2?weapon[i]:eye[i];return out;
}
}
