#pragma once
#include <array>
#include <string_view>
#include "fvr/interaction/ShellReloadAudio.h"
namespace fvr::bc2 {
enum class ShellReloadSound : unsigned { Unrelated, InsertLoop, Finish };
// Exact authored SP_02 animation-sound bindings. Reload_in has no sound row;
// pump, fire, other weapons and unrecognized animation GUIDs are not inferred.
inline ShellReloadSound ShellReloadSoundFor(std::string_view asset,
    const std::array<unsigned char,16>& guid)noexcept {
    if(asset!="SPAS12_sp")return ShellReloadSound::Unrelated;
    constexpr std::array<unsigned char,16> reload{0xf3,0x0a,0x49,0xaa,0x6d,0xe7,0xde,0x42,0x81,0xbb,0x2c,0x3d,0xce,0x52,0x0f,0x84};
    constexpr std::array<unsigned char,16> finish{0x2f,0x23,0xe2,0x23,0xbd,0x2b,0xe8,0x48,0x87,0x42,0x95,0xbc,0x0d,0x15,0xe7,0xb3};
    return guid==reload?ShellReloadSound::InsertLoop:guid==finish?ShellReloadSound::Finish:ShellReloadSound::Unrelated;
}
inline bool ShellReloadLocalStart(bool start,ShellReloadSound sound,bool ownerMatched,
    unsigned callbackFirstPerson,unsigned ownerFlags,bool manual)noexcept {
    return start&&sound!=ShellReloadSound::Unrelated&&ownerMatched&&callbackFirstPerson==1&&(ownerFlags&1)&&manual;
}
template<std::size_t Capacity>
bool ShellReloadSuppressStart(interaction::ShellReloadAudioSuppression<Capacity>& ownership,
    ShellReloadSound sound,const interaction::ShellReloadAudioIdentity& identity,bool ready)noexcept {
    if(sound==ShellReloadSound::InsertLoop)return ownership.Start(identity,ready);
    return sound==ShellReloadSound::Finish&&ready&&ownership.Allows(identity);
}
}
