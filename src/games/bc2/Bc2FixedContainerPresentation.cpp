#include "Bc2FixedContainerPresentation.h"
#include "Bc2SightContact.h"
#include "Bc2BodyAmmoRenderer.h"
#include <algorithm>
#include <cmath>

namespace fvr::bc2 {
using namespace interaction;
namespace {
bool Fresh(std::int64_t a,std::int64_t b,std::int64_t now)noexcept{return a>0&&a<=now&&now<b&&b-a<=100000000;}
math::Matrix4 World(math::Matrix4 local,const math::Matrix4& placed,float units,bool geometry=false)noexcept {
    for(unsigned n=0;n<3;++n)local.values[3][n]*=units;
    if(geometry)for(unsigned i=0;i<3;++i)for(unsigned j=0;j<3;++j)local.values[i][j]*=units;
    return Multiply(local,placed);
}
}
bool FixedContainerRigMatches(const FixedContainerEquipmentProfile& e,const RigSnapshot& rig)noexcept {
    if(!KnownFixedContainerEquipment(&e)||rig.names.size()!=rig.parents.size()||rig.names.size()!=rig.inverseBind.size()||
       rig.names.empty()||rig.names.size()>1024||SightRigFingerprint(rig.names,rig.parents,rig.inverseBind)!=e.rigFingerprint)return false;
    for(const auto& p:e.parts)if(p.boneIndex>=rig.names.size()||rig.names[p.boneIndex]!=p.bone||
       std::count(rig.names.begin(),rig.names.end(),p.bone)!=1)return false;
    return true;
}
bool FixedContainerTrackingFresh(const Bc2FixedContainerTracking& t,std::int64_t now)noexcept {
    const auto* e=KnownFixedContainerEquipment(t.source.equipment);const auto& a=t.source.ammunition;const auto& i=t.input;const auto& g=t.gun;
    return t.enabled&&e&&t.lifetime&&t.revision&&FixedContainerHandOwner(t.source.binding,i.owner)&&
        AmmoResourceBindingFresh(t.source.binding,t.source.binding.owner,now)&&a.context==t.source.binding.context&&
        a.coherentIdleVerified&&a.sequence&&Fresh(a.observedNs,a.deadlineNs,now)&&a.counts.capacity==e->interaction.capacity&&
        a.counts.loaded>=0&&a.counts.loaded<=a.counts.capacity&&a.counts.reserve>=0&&a.counts.reserve<=1000000&&
        i.sequence&&i.focused&&i.tracked[0]&&i.tracked[1]&&!i.released[1]&&Fresh(i.observedNs,i.deadlineNs,now)&&
        g.token.id&&g.token.owner==i.owner&&g.token.kind==HandClaimKind::GunHold&&g.token.hand==InteractionHand::Right&&
        g.token.item==HandInteractionKey{a.context.resource.weapon,i.owner.equipGeneration}&&g.inputSequence==i.sequence&&
        g.deadlineNs>now&&g.deadlineNs<=i.deadlineNs&&t.partCount==e->parts.size()&&t.partCount<=t.parts.size();
}
bool FixedContainerTrackingRetained(const Bc2FixedContainerTracking& a,const Bc2FixedContainerTracking& b,std::int64_t now)noexcept {
    if(!FixedContainerTrackingFresh(a,now)||!FixedContainerTrackingFresh(b,now)||a.source.equipment!=b.source.equipment||
       a.source.binding.context!=b.source.binding.context||a.source.binding.owner!=b.source.binding.owner||a.lifetime!=b.lifetime||
       a.revision>b.revision||a.step!=b.step||a.targetStep!=b.targetStep||a.blocksFire!=b.blocksFire||
       a.carried!=b.carried||a.pending!=b.pending||a.gun.token!=b.gun.token||a.partCount!=b.partCount||
       (a.source.ammunition.counts.loaded>0)!=(b.source.ammunition.counts.loaded>0)||
       a.source.binding.inventory!=b.source.binding.inventory||a.source.binding.switching!=b.source.binding.switching||
       a.source.binding.data!=b.source.binding.data||a.source.binding.persistence!=b.source.binding.persistence||
       a.input.sequence>b.input.sequence||a.input.observedNs>b.input.observedNs||bool(a.hand)!=bool(b.hand))return false;
    if(a.hand&&a.hand->token!=b.hand->token)return false;
    if(!feed_interaction_detail::Same(a.target,b.target)||!feed_interaction_detail::Same(a.carriedSupply,b.carriedSupply))return false;
    for(unsigned n=0;n<a.partCount;++n)if(a.parts[n].values!=b.parts[n].values)return false;
    return true;
}
Bc2FixedContainerRawContact BuildFixedContainerRawContact(const Bc2FixedContainerTracking& t,const RigSnapshot& rig,
    const math::Matrix4& wrist,const math::Matrix4& placed,float units,std::int64_t now)noexcept {
    if(!FixedContainerTrackingFresh(t,now)||!FixedContainerRigMatches(*t.source.equipment,rig)||
       rig.identity.soldier!=t.source.binding.owner.soldier||rig.identity.weak!=t.source.binding.owner.weak||
       !std::isfinite(units)||units<=0||units>1000||!InverseRigid(wrist))return {};
    const auto inverse=InverseRigid(placed);if(!inverse)return {};
    Bc2FixedContainerRawContact out;out.nativeOwner=t.source.binding.owner;out.rig=rig.identity;out.equipment=t.source.equipment;out.input=t.input;
    out.weaponFromWrist=Multiply(wrist,*inverse);out.weaponWorldMeters=placed;
    for(unsigned n=0;n<3;++n){out.weaponFromWrist.values[3][n]/=units;out.weaponWorldMeters.values[3][n]/=units;}
    out.valid=bool(InverseRigid(out.weaponFromWrist));out.grip=t.grip;out.gripPressed=t.gripPressed;out.pouchContact=t.pouchContact;return out;
}
Bc2FixedContainerPresentation BuildFixedContainerPresentation(const Bc2FixedContainerTracking& t,const RigSnapshot& rig,
    const math::Matrix4& placed,float units,std::int64_t now)noexcept {
    Bc2FixedContainerPresentation out;
    if(!FixedContainerTrackingFresh(t,now)||!FixedContainerRigMatches(*t.source.equipment,rig)||
       rig.identity.soldier!=t.source.binding.owner.soldier||rig.identity.weak!=t.source.binding.owner.weak||
       !std::isfinite(units)||units<=0||units>1000||!InverseRigid(placed))return out;
    const auto& e=*t.source.equipment;
    unsigned supplyIndex=0;
    for(unsigned n=0;n<e.parts.size();++n){const auto& p=e.parts[n];
        if(!InverseRigid(t.parts[n]))return {};
        if(p.supplyPart){
            const auto ordinal=++supplyIndex;
            out.hiddenSupplyBones.push_back(p.boneIndex);
            // MP412's metal speedloader is only a carried object. The rounds
            // remain visible in the cylinder even during a partial top-up.
            if(t.source.ammunition.counts.loaded>0&&(e.interaction.kind!=FixedContainerKind::Cylinder||p.bone==e.supplyRoot))
                out.supply.push_back({p.bone,World(t.parts[n],placed,units,true),ordinal});
            if(t.carried&&t.carriedSupply&&t.hand&&t.hand->token.kind==HandClaimKind::AmmoObject&&
               t.hand->token.owner==t.input.owner&&t.hand->inputSequence<=t.input.sequence&&t.hand->deadlineNs>now)
                out.supply.push_back({p.bone,World(Multiply(p.supplyFromPart,*t.carriedSupply),placed,units,true),ordinal+2});
        }else out.parts.push_back({p.boneIndex,World(t.parts[n],placed,units)});
    }
    const unsigned step=unsigned(t.targetStep);
    if(t.target&&t.hand&&step<4&&t.hand->token.owner==t.input.owner&&t.hand->deadlineNs>now&&
       t.hand->inputSequence<=t.input.sequence&&!t.input.released[0]){
        auto wrist=*t.target;
        if(t.targetStep==FixedContainerStep::Insert)
            wrist=Multiply(e.interaction.geometry[2].motion.placement.sourceFromGrip,wrist);
        out.wrist=World(wrist,placed,units);out.handShape=e.handShapes[step];
    }
    out.valid=true;return out;
}
std::vector<graphics::BodyPropInstance> FixedContainerHostInstances(const Bc2FixedContainerTracking& before,
    const Bc2FixedContainerTracking& current,const RigSnapshot& rig,const math::Matrix4& placed,float units,std::int64_t now){
    if(!FixedContainerTrackingRetained(before,current,now))return {};
    const auto presentation=BuildFixedContainerPresentation(before,rig,placed,units,now);if(!presentation.valid)return {};
    const auto& e=*before.source.equipment;std::vector<graphics::BodyPropInstance> out;
    for(const auto& part:presentation.supply){
        graphics::BodyPropInstance instance{MakeBodyAmmoGeometryKey(e.asset,e.mesh,part.bone,e.rigFingerprint),part.world,0,INT64_MAX};
        instance.actorGeneration=before.input.owner.actorGeneration;
        instance.equipmentGeneration=graphics::BodyPropWireEpoch(graphics::BodyPropSourceKind::Ammo,before.input.owner.equipGeneration,part.ordinal);
        instance.spaceGeneration=before.input.owner.space;
        for(const auto* t:{&before,&current}){
            instance.observedNs=(std::max)({instance.observedNs,t->input.observedNs,t->source.binding.observedNs,t->source.ammunition.observedNs});
            instance.deadlineNs=(std::min)({instance.deadlineNs,t->input.deadlineNs,t->gun.deadlineNs,t->source.binding.deadlineNs,t->source.ammunition.deadlineNs});
            if(t->hand)instance.deadlineNs=(std::min)(instance.deadlineNs,t->hand->deadlineNs);
        }
        if(!graphics::BodyPropFresh(instance,now))return {};out.push_back(instance);
    }
    return out;
}
std::optional<FixedContainerRenderSource> BuildFixedContainerRenderSource(const Bc2FixedContainerTracking& tracking,
    const RigSnapshot& rig,const math::Matrix4& placed,float units,std::int64_t now){
    const auto instances=FixedContainerHostInstances(tracking,tracking,rig,placed,units,now);
    if(instances.empty()||instances.size()>4)return {};
    return FixedContainerRenderSource{std::make_shared<const Bc2FixedContainerTracking>(tracking),instances};
}
std::vector<graphics::BodyPropInstance> RetainFixedContainerHostInstances(const FixedContainerRenderSource& a,
    const FixedContainerRenderSource& b,std::int64_t now){
    if(!a.tracking||!b.tracking||!FixedContainerTrackingRetained(*a.tracking,*b.tracking,now)||
       a.instances.empty()||a.instances.size()>4||a.instances.size()!=b.instances.size())return {};
    auto out=a.instances;
    for(unsigned n=0;n<out.size();++n){auto& p=out[n];const auto& q=b.instances[n];
        if(!graphics::SameBodyPropSource(p,q)||!graphics::BodyPropFresh(p,now)||!graphics::BodyPropFresh(q,now))return {};
        p.observedNs=(std::max)(p.observedNs,q.observedNs);p.deadlineNs=(std::min)(p.deadlineNs,q.deadlineNs);
        if(!graphics::BodyPropFresh(p,now))return {};
    }return out;
}
}
