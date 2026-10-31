#pragma once
#include <array>
#include <string_view>
#include "fvr/interaction/ShellReloadAudio.h"
#include "Bc2ShotgunAudioProfile.h"
namespace fvr::bc2 {
enum class ShellReloadSound : unsigned { Unrelated, InsertLoop, Finish };
inline ShellReloadSound ShellReloadSoundFor(const ShotgunAudioProfile* profile,
    const ShotgunAnimationGuid& guid)noexcept {
    const auto role=ShotgunAudioRoleFor(profile,guid);
    return role==ShotgunAudioRole::InsertLoop?ShellReloadSound::InsertLoop:
        role==ShotgunAudioRole::Finish?ShellReloadSound::Finish:ShellReloadSound::Unrelated;
}
// Exact authored SP_02 animation-sound bindings. Reload_in has no sound row;
// pump, fire, other weapons and unrecognized animation GUIDs are not inferred.
inline ShellReloadSound ShellReloadSoundFor(std::string_view asset,
    const std::array<unsigned char,16>& guid)noexcept {
    if(asset!="SPAS12_sp")return ShellReloadSound::Unrelated;
    return ShellReloadSoundFor(&LegacySpasAudio,guid);
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
