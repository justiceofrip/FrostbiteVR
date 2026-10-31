#pragma once
#include "Bc2ReloadInteraction.h"
#include "Bc2ShotgunReloadRegistry.h"
#include "Bc2SelectedMeshes1p.h"
#include "Bc2ShotgunAudioProfile.h"
#include "fvr/interaction/AuthoredHandPose.h"
#include <algorithm>
#ifdef BC2_SHOTGUN_EQUIPMENT_HEADER
#include BC2_SHOTGUN_EQUIPMENT_HEADER
#endif
namespace fvr::bc2 {
namespace shotgun_equipment_detail {
inline const std::array<math::Matrix4,15> SpasWristFromFinger{{
    {{{{0.7137475541f,0.1789647602f,0.6771527654f,0.f},{-0.2884533148f,0.9561159728f,0.0513497357f,0.f},{-0.6382468966f,-0.231977667f,0.7340484867f,0.f},{0.0293302181f,0.004928418345f,0.005807010901f,1.f}}}},
    {{{{0.6847702852f,0.2456200553f,0.6861199269f,0.f},{-0.3120105821f,0.9496490947f,-0.02856274076f,0.f},{-0.6585887056f,-0.1945176719f,0.7269276427f,0.f},{0.06857386852f,0.01477415697f,0.04306436213f,1.f}}}},
    {{{{0.7246573185f,0.3456363804f,0.5961604407f,0.f},{-0.3906985226f,0.91870662f,-0.05772946853f,0.f},{-0.5676498082f,-0.191084843f,0.800787188f,0.f},{0.09374119725f,0.02381214399f,0.0682624772f,1.f}}}},
    {{{{0.4893205471f,0.02018282798f,0.871870398f,0.f},{-0.1946888833f,0.9770303664f,0.08664813534f,0.f},{-0.8500951039f,-0.2121419969f,0.4820104775f,0.f},{0.04571029303f,0.03556059795f,0.06102597445f,1.f}}}},
    {{{{0.3064671338f,-0.9507390121f,-0.04661451043f,0.f},{0.4457128379f,0.1000585353f,0.8895665279f,0.f},{-0.8410813478f,-0.2933993438f,0.4544213891f,0.f},{0.06863200617f,0.03651605777f,0.1018913919f,1.f}}}},
    {{{{0.07097657699f,-0.9173587052f,-0.3916826975f,0.f},{0.5320109313f,-0.2973435958f,0.7928125083f,0.f},{-0.8437576326f,-0.2646503391f,0.46694043f,0.f},{0.07824586692f,0.006695343981f,0.1004641284f,1.f}}}},
    {{{{0.3761477264f,-0.5753370786f,0.7262919799f,0.f},{-0.1601878228f,0.7316759326f,0.66256336f,0.f},{-0.9126076798f,-0.3655646558f,0.1830562576f,0.f},{0.02344436661f,0.03434542056f,0.0705539785f,1.f}}}},
    {{{{0.199368961f,-0.7885277385f,-0.5817867961f,0.f},{0.3549343357f,-0.4952839147f,0.7929157557f,0.f},{-0.9133858538f,-0.3645787958f,0.181131325f,0.f},{0.04168169333f,0.006432591574f,0.1058003364f,1.f}}}},
    {{{{-0.2990906678f,0.2969783658f,-0.9068342414f,0.f},{0.2740321089f,-0.883573765f,-0.3797414957f,0.f},{-0.9140302308f,-0.3620786937f,0.182887019f,0.f},{0.04790132252f,-0.01827861444f,0.08757358182f,1.f}}}},
    {{{{0.2623528759f,-0.5169413803f,0.8148266226f,0.f},{-0.1960595959f,0.7982366511f,0.5695425986f,0.f},{-0.9448447554f,-0.3091755248f,0.1080683702f,0.f},{0.003981623856f,0.02266820003f,0.07430389787f,1.f}}}},
    {{{{0.2736555508f,-0.8689100639f,-0.4124414249f,0.f},{0.2603448065f,-0.3458861152f,0.9014340589f,0.f},{-0.9259229746f,-0.3540592054f,0.1315626032f,0.f},{0.0153254355f,0.0002680329964f,0.1096616715f,1.f}}}},
    {{{{-0.2148755928f,0.1880642364f,-0.9583631837f,0.f},{0.3598865809f,-0.8969845394f,-0.2567101858f,0.f},{-0.9079152963f,-0.4000627353f,0.1250583596f,0.f},{0.02346054953f,-0.02565966221f,0.09733631933f,1.f}}}},
    {{{{0.2421172593f,-0.7556401667f,0.6085942699f,0.f},{-0.1565657367f,0.5886085787f,0.7931122612f,0.f},{-0.9575314495f,-0.2873109552f,0.02420479212f,0.f},{-0.01345622102f,0.006074312475f,0.07385977808f,1.f}}}},
    {{{{0.09923279455f,-0.5779284295f,-0.8100315439f,0.f},{0.3034424185f,-0.7577002976f,0.5777651793f,0.f},{-0.9476684435f,-0.3031310319f,0.1001791992f,0.f},{-0.004561033202f,-0.02169548063f,0.09622818643f,1.f}}}},
    {{{{-0.3028821176f,0.6742706458f,-0.6735140433f,0.f},{0.2073555731f,-0.6431548759f,-0.7371262455f,0.f},{-0.9301968187f,-0.3629191242f,0.05498630956f,0.f},{-0.002278421625f,-0.03528071206f,0.07714936715f,1.f}}}}
}};
inline const math::Matrix4 SpasShellFromCenter={{{{1.f,-6.85506856e-15f,1.73336975e-14f,0.f},{6.85506856e-15f,1.f,-1.38777889e-15f,0.f},{-1.73336975e-14f,1.38777889e-15f,1.f,0.f},{-1.522567716e-14f,0.00033545875f,0.0009458064656f,1.f}}}};

inline const interaction::AuthoredHandShape SpasShellHand=[] {
    interaction::AuthoredHandShape out{};out.counts={3,3,3,3,3};
    for(unsigned n=0;n<15;++n)out.wristFromJoint[n/3][n%3]=SpasWristFromFinger[n];
    return out;
}();
}
// This explicit compatibility row preserves the original SPAS interaction.
// It is never substituted for another configuration's authored geometry.
inline const ShotgunReloadEquipmentProfile* DefaultSpasShotgunEquipment()noexcept {
    static const auto insertion=SpasReloadInsertionProfile();
    static const ShotgunReloadEquipmentProfile equipment{&ShotgunReloadRegistrations[12],
        SpasReloadMesh,"legacy-spas-native-row58-row62",SpasReloadRig,&insertion,nullptr,true};
    return &equipment;
}
inline std::span<const ShotgunReloadEquipmentProfile> EnrolledShotgunEquipment()noexcept {
#ifdef BC2_SHOTGUN_EQUIPMENT_HEADER
    return generated::ShotgunEquipmentProfiles;
#else
    return {DefaultSpasShotgunEquipment(),1};
#endif
}
inline const ShotgunReloadEquipmentProfile* KnownShotgunEquipment(const ShotgunReloadEquipmentProfile* p)noexcept {
    if(!p||p==DefaultSpasShotgunEquipment())return DefaultSpasShotgunEquipment();
    for(const auto& row:EnrolledShotgunEquipment())if(p==&row)return p;
    return nullptr;
}
inline const ShotgunAudioProfile* ShotgunEquipmentAudio(const ShotgunReloadEquipmentProfile* requested)noexcept {
    const auto* equipment=KnownShotgunEquipment(requested);if(!equipment)return nullptr;
    if(equipment==DefaultSpasShotgunEquipment())return &LegacySpasAudio;
#ifdef BC2_SHOTGUN_EQUIPMENT_HEADER
    return generated::FindShotgunAudioProfile(equipment);
#else
    return nullptr;
#endif
}
struct Bc2ShotgunShellGeometry {
    const ShotgunReloadEquipmentProfile* equipment=nullptr;
    std::string_view bone,parent;
    math::Matrix4 centerInBone{};
    const interaction::AuthoredHandShape* handShape=nullptr;
    std::span<const std::string_view> hiddenDescendants{};
};
inline std::optional<Bc2ShotgunShellGeometry> ShotgunShellGeometry(const ShotgunReloadEquipmentProfile* requested)noexcept {
    const auto* e=KnownShotgunEquipment(requested);if(!e)return {};
    if(e==DefaultSpasShotgunEquipment())return Bc2ShotgunShellGeometry{e,"jntWpn_7","jntWpn_1",
        shotgun_equipment_detail::SpasShellFromCenter,&shotgun_equipment_detail::SpasShellHand,{}};
#ifdef BC2_SHOTGUN_EQUIPMENT_HEADER
    if(const auto* g=generated::FindShotgunShellGeometry(e))return Bc2ShotgunShellGeometry{e,g->bone,g->parent,g->centerInBone,g->handShape,g->hiddenDescendants};
#endif
    return {};
}
inline bool ShotgunEquipmentMatches(const ShotgunReloadEquipmentProfile* requested,
    std::string_view asset,std::string_view mesh,std::uint64_t rig,std::string_view config={})noexcept {
    const auto* e=KnownShotgunEquipment(requested);
    return e&&e->physicalReviewed&&e->native&&e->native->config&&e->shellInsertion&&
        asset==e->native->config->assetName&&mesh==e->meshPath&&rig==e->rigFingerprint&&
        ((e==DefaultSpasShotgunEquipment()&&config.empty())||config==e->native->config->assetPath);
}
// Exact authored selection only. This does not promote Candidate native data.
inline const ShotgunReloadEquipmentProfile* FindShotgunEquipment(std::string_view config,
    std::string_view asset,std::string_view mesh,std::uint64_t rig)noexcept {
    if(config.empty())return nullptr;
    const ShotgunReloadEquipmentProfile* found=nullptr;
    for(const auto& row:EnrolledShotgunEquipment())if(ShotgunEquipmentMatches(&row,asset,mesh,rig,config)){
        if(found)return nullptr;found=&row;
    }
    return found;
}
inline const SelectedMeshAsset* ShotgunEquipmentSelected(const SelectedMeshesSnapshot& s,const ReloadStateOwner& owner,
    const ShotgunReloadEquipmentProfile* requested,std::int64_t now)noexcept {
    const auto* e=KnownShotgunEquipment(requested);
    if(!e||!e->native||!e->native->config||s.owner!=owner||!s.sequence||s.observedNs<=0||s.observedNs>now||
        s.deadlineNs<=now||s.deadlineNs-s.observedNs>250000000||s.stateCount!=1||!s.soleConfiguredArray||s.states[0].count>8)return nullptr;
    // Legacy SPAS fixtures predate Config publication. Explicit generated rows
    // require the repeated exact Config path and weapon identity every time.
    if(e!=DefaultSpasShotgunEquipment()&&(!s.configurationPathVerified||
        !ReloadDescriptorText(s.weaponName,e->native->config->assetName)))return nullptr;
    if((s.configurationPathVerified||e!=DefaultSpasShotgunEquipment())&&
        !ReloadDescriptorText(s.configurationPath,e->native->config->assetPath))return nullptr;
    const SelectedMeshAsset* found=nullptr;
    for(unsigned n=0;n<s.states[0].count;++n){const auto& m=s.states[0].meshes[n];
        if(ReloadDescriptorText(m.assetPath,e->meshPath)){
            if(e==DefaultSpasShotgunEquipment()&&m.kind!=SelectedMeshKind::Spas12)return nullptr;
            if(found)return nullptr;found=&m;}}
    return found;
}
inline bool ShotgunInteractionMatches(const Bc2ReloadInteractionSample& s)noexcept {
    return s.selectedMeshIdentityVerified&&ShotgunEquipmentMatches(s.equipment,s.assetName,s.meshPath,s.rigFingerprint,s.configurationPath);
}
} // namespace fvr::bc2
