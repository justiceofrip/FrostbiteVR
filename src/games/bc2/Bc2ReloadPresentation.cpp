#include "Bc2ReloadPresentation.h"
#include "Bc2SightContact.h"
#include <algorithm>
#include <cstring>
namespace fvr::bc2 {
namespace {
// Derived pose calibration, not a game mesh/animation asset. Canonical metres.
// native-trace-20261001-161937-497 row58; relative to its LeftHand wrist.
const std::array<math::Matrix4,15> WristFromFinger{{
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
const math::Matrix4 ShellFromCenter={{{{1.f,-6.85506856e-15f,1.73336975e-14f,0.f},{6.85506856e-15f,1.f,-1.38777889e-15f,0.f},{-1.73336975e-14f,1.38777889e-15f,1.f,0.f},{-1.522567716e-14f,0.00033545875f,0.0009458064656f,1.f}}}};
math::Matrix4 Units(math::Matrix4 m,float units)noexcept {
    for(unsigned n=0;n<3;++n)m.values[3][n]*=units;return m;
}
}
std::optional<math::Matrix4> SpasShellBoneAtCenter(const math::Matrix4& center,float units)noexcept {
    if(!std::isfinite(units)||units<=0||!interaction::reload_insertion_detail::Rigid(center))return {};
    return interaction::Multiply(Units(*interaction::InverseRigid(ShellFromCenter),units),center);
}
namespace bc2_reload_detail {
std::optional<Bc2ReloadPresentationBinding> DerivePresentationBinding(const RigSnapshot& rig) {
    const auto size=rig.names.size();
    if(!size||size>1024||rig.parents.size()!=size||rig.inverseBind.size()!=size||rig.evaluatedWorld.size()!=size)return {};
    const auto named=[&](std::string_view name)->std::optional<std::uint32_t>{
        const auto it=std::find(rig.names.begin(),rig.names.end(),name);
        if(it==rig.names.end()||std::find(it+1,rig.names.end(),name)!=rig.names.end())return {};
        return std::uint32_t(it-rig.names.begin());
    };
    const auto root=named("jntWpn_1"),shell=named("jntWpn_7"),wrist=named("LeftHand");
    if(!root||!shell||!wrist||rig.weaponBone!=*root||rig.parents[*shell]!=std::int32_t(*root)||
       std::find(rig.parents.begin(),rig.parents.end(),std::int32_t(*shell))!=rig.parents.end())return {};
    // Entire graph is bounded and acyclic, even for unrelated branches.
    for(std::size_t n=0;n<size;++n){auto at=std::int32_t(n);std::size_t steps=0;
        while(at!=-1){if(at<0||std::size_t(at)>=size||++steps>size)return {};at=rig.parents[at];}}
    Bc2ReloadPresentationBinding out;out.weapon=*root;out.shell=*shell;out.wrist=*wrist;
    const std::array<const char*,5> fingers{"Thumb","Index","Middle","Ring","Pinky"};
    unsigned slot=0;
    for(const auto name:fingers){auto parent=*wrist;for(unsigned joint=1;joint<=3;++joint){
        const auto at=named(std::string("LeftHand")+name+std::to_string(joint));
        if(!at||rig.parents[*at]!=std::int32_t(parent))return {};
        out.fingers[slot++]=*at;parent=*at;
    }}
    // An unaccounted hand descendant would retain a stale attachment; reject it.
    for(std::size_t n=0;n<size;++n){if(n==*wrist)continue;auto at=rig.parents[n];
        while(at!=-1&&at!=std::int32_t(*wrist))at=rig.parents[at];
        if(at==std::int32_t(*wrist)&&std::find(out.fingers.begin(),out.fingers.end(),n)==out.fingers.end())return {};}
    out.fingerprint=SightRigFingerprint(rig.names,rig.parents,rig.inverseBind);if(!out.fingerprint)return {};
    return out;
}
}
namespace bc2_reload_detail {
bool KnownHiddenShell(const RigSnapshot& rig,const Bc2ReloadPresentationBinding& binding)noexcept {
    const auto index=binding.shell;
    if(index>=rig.names.size()||index>=rig.parents.size()||index>=rig.nativeWorld.size()||index>=rig.nativeEvaluated.size()||
       rig.names[index]!="jntWpn_7"||binding.weapon!=rig.weaponBone||rig.parents[index]!=std::int32_t(binding.weapon)||
       std::find(rig.parents.begin(),rig.parents.end(),std::int32_t(index))!=rig.parents.end()||
       std::count(rig.nativeHiddenLeaves.begin(),rig.nativeHiddenLeaves.end(),index)!=1)return false;
    const auto collapsed=[](const std::array<std::byte,64>& bytes){
        // xyz only: the original SIMD padding is opaque and preserved.
        for(unsigned row=0;row<4;++row)for(unsigned col=0;col<3;++col){float value=0;
            std::memcpy(&value,bytes.data()+row*16+col*4,4);
            if(!std::isfinite(value)||(row<3&&std::abs(value-(row==col?.0001f:0.f))>1e-7f))return false;
        }return true;
    };
    return collapsed(rig.nativeWorld[index])&&collapsed(rig.nativeEvaluated[index]);
}
std::optional<RigPosePlan> BuildShellVisibilityPalette(const RigSnapshot& rig,
    const Bc2ReloadPresentationBinding& binding,std::span<const interaction::BoneWrite> writes,
    std::optional<std::uint32_t> ownedShell) {
    if(!ownedShell)return BuildRigPosePlan(rig,writes);
    const auto derived=DerivePresentationBinding(rig);
    if(!derived||derived->fingerprint!=binding.fingerprint||derived->shell!=binding.shell||derived->weapon!=binding.weapon||
       derived->wrist!=binding.wrist||derived->fingers!=binding.fingers||*ownedShell!=binding.shell||!KnownHiddenShell(rig,binding))return {};
    auto privateRig=rig;
    std::erase(privateRig.nativeHiddenLeaves,binding.shell);
    // Source matrices, native before bytes, identity and every other exclusion
    // remain exactly original. Only the measured target supplies a rigid pose.
    return BuildRigPosePlan(privateRig,writes);
}
}
std::optional<Bc2ReloadPresentationBinding> BindBc2ReloadPresentation(
    const RigSnapshot& rig,std::string_view asset,std::string_view mesh) {
    if(asset!=SpasReloadAsset||mesh!=SpasReloadMesh||
       SightRigFingerprint(rig.names,rig.parents,rig.inverseBind)!=SpasReloadRig)return {};
    return bc2_reload_detail::DerivePresentationBinding(rig);
}
Bc2ReloadPresentationPlan BuildBc2ReloadPresentation(const RigSnapshot& rig,
    const Bc2ReloadPresentationBinding& binding,const Bc2ReloadPresentationObservation& s,
    const Bc2ReloadTargets& target) {
    using namespace interaction;
    const auto reject=[](Bc2ReloadPresentationReason reason){Bc2ReloadPresentationPlan p;p.reason=reason;return p;};
    if(!s.enabled)return reject(Bc2ReloadPresentationReason::Disabled);
    if(s.assetName!=SpasReloadAsset||s.meshPath!=SpasReloadMesh)return reject(Bc2ReloadPresentationReason::UnsupportedAsset);
    if(target.identity!=s.identity||!target.inputSequence||target.inputSequence!=s.inputSequence||
       (s.carried?(target.nativeCycle||s.nativeCycle):(!target.nativeCycle||target.nativeCycle!=s.nativeCycle))||target.observedNs<=0||s.nowNs<target.observedNs||
       target.deadlineNs<=s.nowNs||target.deadlineNs-target.observedNs>100000000ll||
       target.shellClaim.owner!=target.identity.owner||target.weaponClaim.owner!=target.identity.owner||
       target.shellClaim.item!=target.identity.item||target.weaponClaim.item!=target.identity.weapon||
       !target.shellClaim.id||!target.weaponClaim.id||target.shellClaim.id==target.weaponClaim.id||
       target.shellClaim.hand!=InteractionHand::Left||target.weaponClaim.hand!=InteractionHand::Right||
       target.shellClaim.kind!=HandClaimKind::AmmoObject||target.weaponClaim.kind!=HandClaimKind::GunHold)
        return reject(Bc2ReloadPresentationReason::StaleOwnership);
    const auto derived=bc2_reload_detail::DerivePresentationBinding(rig);
    if(!derived||derived->fingerprint!=binding.fingerprint||derived->weapon!=binding.weapon||
       derived->shell!=binding.shell||derived->wrist!=binding.wrist||derived->fingers!=binding.fingers)
        return reject(Bc2ReloadPresentationReason::InvalidBinding);
    const bool hidden=std::find(rig.nativeHiddenLeaves.begin(),rig.nativeHiddenLeaves.end(),binding.shell)!=rig.nativeHiddenLeaves.end();
    const bool ownedVisibility=hidden&&s.allowOwnedShellVisibility&&bc2_reload_detail::KnownHiddenShell(rig,binding);
    if(hidden&&!ownedVisibility)
        return reject(Bc2ReloadPresentationReason::NativeShellHidden);
    if(!s.selectedMeshIdentityVerified||!s.sectionOwnershipVerified||(!s.shellSectionVisible&&!ownedVisibility))
        return reject(Bc2ReloadPresentationReason::VisibilityUnverified);
    using namespace reload_insertion_detail;
    if(!std::isfinite(s.unitsPerMetre)||s.unitsPerMetre<=0||!Rigid(s.placedWeaponWorld)||
       !Rigid(target.weaponFromShellCenterMeters)||!Rigid(target.weaponFromLeftWristMeters)||
       (!ownedVisibility&&!Rigid(rig.evaluatedWorld[binding.shell])))return reject(Bc2ReloadPresentationReason::InvalidGeometry);
    const auto shellPose=SpasShellBoneAtCenter(Multiply(Units(target.weaponFromShellCenterMeters,s.unitsPerMetre),s.placedWeaponWorld),s.unitsPerMetre);
    if(!shellPose)return reject(Bc2ReloadPresentationReason::InvalidGeometry);
    const auto shell=*shellPose;
    const auto wrist=Multiply(Units(target.weaponFromLeftWristMeters,s.unitsPerMetre),s.placedWeaponWorld);
    Bc2ReloadPresentationPlan out;out.writes.reserve(17);
    out.writes.push_back({binding.shell,shell});out.writes.push_back({binding.wrist,wrist});
    for(unsigned n=0;n<binding.fingers.size();++n)
        out.writes.push_back({binding.fingers[n],Multiply(Units(WristFromFinger[n],s.unitsPerMetre),wrist)});
    if(ownedVisibility)out.ownedShellVisibility=binding.shell;
    out.palette=bc2_reload_detail::BuildShellVisibilityPalette(rig,binding,out.writes,out.ownedShellVisibility);
    if(!out.palette)return reject(Bc2ReloadPresentationReason::InvalidGeometry);
    return out;
}
}
