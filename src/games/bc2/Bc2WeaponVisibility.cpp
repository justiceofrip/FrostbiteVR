#include "Bc2WeaponVisibility.h"
#include "Bc2WeaponVisibilityCoverage.h"
#include "Bc2VisibilityDescriptorData.h"
#include "Bc2SightContact.h"
#include <algorithm>
#include <cmath>
#include <cstring>
namespace fvr::bc2 {
namespace {
std::uint32_t NameHash(std::string_view name)noexcept {
    std::uint32_t h=5381;for(unsigned char c:name){if(c>='A'&&c<='Z')c+=32;h=(h*33)^c;}return h;
}
std::span<const std::string_view> Profile(const SelectedMeshesSnapshot& s,const ReloadStateOwner& o,std::int64_t now,bool diagnostic=false)noexcept {
    using namespace weapon_visibility_detail;
    if(diagnostic){
        const auto* descriptor=ResolveVisibilityDescriptor(s,o,VisibilityDescriptors,now,true);
        if(descriptor&&descriptor->rigFingerprint==KnownRig)return descriptor->weightedNames;
    }
    if(s.owner!=o||!s.sequence||s.observedNs<=0||s.observedNs>now||s.deadlineNs<=now||s.deadlineNs-s.observedNs>200000000||
        s.stateCount!=1||s.soleConfiguredArray<0x10000||s.states[0].array!=s.soleConfiguredArray||s.weaponData<0x10000)return {};
    const auto end=std::find(s.weaponName.begin(),s.weaponName.end(),'\0');if(end==s.weaponName.end())return {};
    const std::string_view asset(s.weaponName.data(),std::size_t(end-s.weaponName.begin()));
    const auto mesh=[&](SelectedMeshKind kind,std::string_view path){const auto* m=FindSelectedMesh(s,o,kind,now);
        if(!m||m->address<0x10000||m->typeInfo<0x10000||m->namePointer<0x10000)return false;
        const auto end=std::find(m->assetPath.begin(),m->assetPath.end(),'\0');
        return end!=m->assetPath.end()&&std::string_view(m->assetPath.data(),std::size_t(end-m->assetPath.begin()))==path;};
    if(asset=="SPAS12_sp"&&s.states[0].count==1&&mesh(SelectedMeshKind::Spas12,"Objects/Weapons/Handheld/UL_shg_SPAS12/UL_shg_SPAS-12_Mesh"))return SpasBones;
    if((asset=="XM8_sp_s"||asset=="40mmgl")&&s.states[0].count==2&&
        mesh(SelectedMeshKind::Xm8,"Objects/Weapons/Handheld/US_rgl_XM8/US_rgl_XM8_Mesh")&&
        mesh(SelectedMeshKind::Acog4x,"Objects/Weapons/Unlock/ACOG_4X/US_ACOG_4X_Mesh"))return Xm8Bones;
    const auto* descriptor=ResolveVisibilityDescriptor(s,o,VisibilityDescriptors,now,diagnostic);
    return descriptor&&descriptor->rigFingerprint==KnownRig?descriptor->weightedNames:std::span<const std::string_view>{};
}
}
std::span<const std::string_view> ResolveWeaponVisibilityNames(const SelectedMeshesSnapshot& s,
    const ReloadStateOwner& owner,std::int64_t now,bool diagnostic)noexcept {
    return Profile(s,owner,now,diagnostic);
}
namespace weapon_visibility_detail {
std::optional<std::vector<std::uint32_t>> ResolveWeightedBones(std::span<const std::string> names,
    std::span<const std::string_view> weighted) {
    if(names.empty()||names.size()>1024||weighted.empty()||weighted.size()>64)return {};
    std::vector<std::uint32_t> result;result.reserve(weighted.size());
    for(const auto name:weighted){
        const auto at=std::find(names.begin(),names.end(),name);
        if(at==names.end()||std::find(at+1,names.end(),name)!=names.end())return {};
        const auto index=std::uint32_t(at-names.begin());
        if(std::find(result.begin(),result.end(),index)!=result.end())return {};
        const auto hash=NameHash(name);
        if(std::find(ArmWeightedHashes.begin(),ArmWeightedHashes.end(),hash)!=ArmWeightedHashes.end())return {};
        // Section skin names are hash-linked. Reject a second native name
        // colliding with a referenced weapon hash, including case aliases.
        for(std::size_t n=0;n<names.size();++n)if(n!=index&&NameHash(names[n])==hash)return {};
        result.push_back(index);
    }return result;
}
std::optional<std::vector<std::array<std::byte,64>>> CollapseWeightedPalette(
    std::span<const std::array<std::byte,64>> source,std::span<const std::uint32_t> indices) {
    if(source.empty()||source.size()>1024||indices.empty()||indices.size()>64)return {};
    for(std::size_t n=0;n<indices.size();++n){if(indices[n]>=source.size())return {};
        if(std::find(indices.begin(),indices.begin()+n,indices[n])!=indices.begin()+n)return {};
        for(unsigned row=0;row<4;++row)for(unsigned col=0;col<3;++col){float v=0;
            std::memcpy(&v,source[indices[n]].data()+row*16+col*4,4);if(!std::isfinite(v))return {};}}
    std::vector<std::array<std::byte,64>> copy(source.begin(),source.end());
    // A final skin transform with all12 consumed scalars zero maps every vertex
    // to the same point regardless of bind position, weight or attachment pose.
    // Do not use merely a tiny scale; that can leave visible subpixel triangles.
    for(const auto index:indices)for(unsigned row=0;row<4;++row)std::memset(copy[index].data()+row*16,0,12);
    return copy;
}
}
WeaponVisibilityPlan BuildWeaponVisibilityPalette(const RigSnapshot& rig,
    std::span<const std::array<std::byte,64>> ordinary,const WeaponVisibilityRequest& r,std::int64_t now) {
    using namespace weapon_visibility_detail;
    const auto reject=[](WeaponVisibilityReason why){WeaponVisibilityPlan p;p.reason=why;return p;};
    if(!r.enabled)return reject(WeaponVisibilityReason::Disabled);
    if(r.authorizationDeadlineNs&&r.authorizationDeadlineNs<=now)return reject(WeaponVisibilityReason::StaleInput);
    const auto& i=r.input;const auto& o=r.nativeOwner;
    if(!r.request||!i.sequence||i.observedNs<=0||i.observedNs>now||i.deadlineNs<=now||i.deadlineNs-i.observedNs>150000000||
        !i.focused||!i.tracked[0]||!i.tracked[1])return reject(WeaponVisibilityReason::StaleInput);
    if(o.player<0x10000||o.soldier<0x10000||o.weak<0x10000||o.weapon<0x10000||!o.actorGeneration||!o.equipGeneration||!o.space||
        i.owner.actor!=((std::uint64_t(o.weak)<<32)|o.soldier)||i.owner.actorGeneration!=o.actorGeneration||
        !i.owner.equipGeneration||i.owner.space!=o.space||rig.identity.soldier!=o.soldier||rig.identity.weak!=o.weak)
        return reject(WeaponVisibilityReason::OwnerMismatch);
    if(!r.selected)return reject(WeaponVisibilityReason::UnsupportedMeshes);
    const bool diagnostic=r.authorizationDeadlineNs>now&&r.authorizationDeadlineNs-now<=15000000000ll;
    const auto names=ResolveWeaponVisibilityNames(*r.selected,o,now,diagnostic);if(names.empty())return reject(WeaponVisibilityReason::UnsupportedMeshes);
    if(SightRigFingerprint(rig.names,rig.parents,rig.inverseBind)!=KnownRig||rig.identity.count!=rig.names.size()||
        rig.nativeEvaluated.size()!=rig.names.size()||ordinary.size()!=rig.names.size()||!rig.identity.evaluatedMatrices)
        return reject(WeaponVisibilityReason::UnverifiedRig);
    const auto indices=ResolveWeightedBones(rig.names,names);if(!indices)return reject(WeaponVisibilityReason::WeightedHandOverlap);
    const auto collapsed=CollapseWeightedPalette(ordinary,*indices);if(!collapsed)return reject(WeaponVisibilityReason::InvalidPalette);
    WeaponVisibilityPlan p;p.reason=WeaponVisibilityReason::None;p.rig=rig.identity;p.nativeOwner=o;p.request=r.request;
    p.inputSequence=i.sequence;p.physicalEquipGeneration=i.owner.equipGeneration;p.meshSequence=r.selected->sequence;
    p.observedNs=i.observedNs;p.deadlineNs=std::min(i.deadlineNs,r.selected->deadlineNs);p.inputDeadlineNs=i.deadlineNs;
    if(r.authorizationDeadlineNs)p.deadlineNs=std::min(p.deadlineNs,r.authorizationDeadlineNs);
    p.hidden=r.hide;p.weightedBones=*indices;p.selected=r.selected;
    p.originalNative=rig.nativeEvaluated;p.ordinary.assign(ordinary.begin(),ordinary.end());
    p.privatePalette=r.hide?*collapsed:p.ordinary;
    return p;
}
bool WeaponVisibilityCurrent(const WeaponVisibilityPlan& p,const WeaponVisibilityRequest& r,std::int64_t now)noexcept {
    if(p.reason!=WeaponVisibilityReason::None||!r.enabled||!r.request||r.request!=p.request||r.hide!=p.hidden||
        (r.authorizationDeadlineNs&&(now>=r.authorizationDeadlineNs||p.deadlineNs>r.authorizationDeadlineNs))||
        r.nativeOwner!=p.nativeOwner||p.observedNs<=0||p.observedNs>now||p.deadlineNs<=now||
        r.input.sequence<p.inputSequence||r.input.observedNs<p.observedNs||r.input.observedNs>now||
        (r.input.sequence==p.inputSequence&&(r.input.observedNs!=p.observedNs||r.input.deadlineNs!=p.inputDeadlineNs))||
        r.input.deadlineNs<=now||r.input.deadlineNs-r.input.observedNs>150000000||!r.input.focused||
        !r.input.tracked[0]||!r.input.tracked[1]||r.input.owner.actor!=((std::uint64_t(p.nativeOwner.weak)<<32)|p.nativeOwner.soldier)||
        r.input.owner.actorGeneration!=p.nativeOwner.actorGeneration||r.input.owner.equipGeneration!=p.physicalEquipGeneration||
        r.input.owner.space!=p.nativeOwner.space||!p.selected||!r.selected||r.selected->sequence<p.meshSequence||
        r.selected->owner!=p.nativeOwner||r.selected->weaponData!=p.selected->weaponData||
        r.selected->weaponName!=p.selected->weaponName||r.selected->stateTypeInfo!=p.selected->stateTypeInfo||
        r.selected->meshTypeInfo!=p.selected->meshTypeInfo||
        r.selected->soleConfiguredArray!=p.selected->soleConfiguredArray||r.selected->states!=p.selected->states||
        ResolveWeaponVisibilityNames(*r.selected,p.nativeOwner,now,r.authorizationDeadlineNs>now&&r.authorizationDeadlineNs-now<=15000000000ll).empty())return false;
    return true;
}
}
