#pragma once
#include <array>
#include <string_view>
namespace fvr::bc2 {
using ShotgunAnimationGuid=std::array<unsigned char,16>;
enum class ShotgunAudioRole:unsigned { Unrelated,Pump,InsertLoop,Finish };
// Exact authored animation identities select generic physical cues extracted
// from the owner's SPAS assets. They do not claim each gun's original timbre.
// A zero role GUID is explicitly unsupported and passes its native sound.
struct ShotgunAudioProfile {
    ShotgunAnimationGuid pump{},insertion{},finish{};
    std::string_view provenance;
};
inline constexpr ShotgunAudioProfile LegacySpasAudio{
    {0x1b,0xf4,0x9c,0x2d,0xba,0x3a,0x22,0x4a,0x82,0xa8,0x64,0x05,0x1c,0x18,0x04,0x32},
    {0xf3,0x0a,0x49,0xaa,0x6d,0xe7,0xde,0x42,0x81,0xbb,0x2c,0x3d,0xce,0x52,0x0f,0x84},
    {0x2f,0x23,0xe2,0x23,0xbd,0x2b,0xe8,0x48,0x87,0x42,0x95,0xbc,0x0d,0x15,0xe7,0xb3},
    "accepted-spas-animation-sound-bindings"};
inline bool ShotgunAudioRoleSupported(const ShotgunAnimationGuid& guid)noexcept {
    return guid!=ShotgunAnimationGuid{};
}
inline ShotgunAudioRole ShotgunAudioRoleFor(const ShotgunAudioProfile* profile,const ShotgunAnimationGuid& guid)noexcept {
    if(!profile||profile->provenance.empty()||!ShotgunAudioRoleSupported(guid))return ShotgunAudioRole::Unrelated;
    if(guid==profile->pump)return ShotgunAudioRole::Pump;
    if(guid==profile->insertion)return ShotgunAudioRole::InsertLoop;
    if(guid==profile->finish)return ShotgunAudioRole::Finish;
    return ShotgunAudioRole::Unrelated;
}
}
