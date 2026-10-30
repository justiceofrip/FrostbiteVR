#include "Bc2FeedAmmoSupply.h"
#include <limits>

namespace fvr::bc2 {
using namespace interaction;
std::optional<AmmoSupplySource> Bc2FeedBodySource(const Bc2FeedBodyAmmo& f,
    const HandInteractionSample& input,std::int64_t now)noexcept {
    const auto* row=FindBc2FeedRegistration(f.registrationKey);const auto& v=f.native;const auto& o=v.identity.owner;
    if(!row||FindBc2FeedRegistration(v.config)!=row||!f.lifetime||
       !feed_mechanism_detail::Supply(f.supply,row->mechanism.container)||!FeedEquipmentViewFresh(v,now)||
       f.supply!=FeedSupplyKey{{v.binding.data,v.binding.context.resource.weaponGeneration},{o.weapon,v.binding.context.resource.weaponGeneration}}||
       input.owner.actor!=((std::uint64_t(o.weak)<<32)|o.soldier)||input.owner.actorGeneration!=o.actorGeneration||
       !input.owner.equipGeneration||input.owner.space!=o.space||v.counts.capacity!=row->config.values.baseCapacity||
       v.counts.loaded>=v.counts.capacity||v.counts.reserve<=0)return {};
    AmmoSupplySource source;
    source.identity={input.owner,{o.weapon,input.owner.equipGeneration},{row->key,row->mechanism.revision},
        {v.binding.inventory,v.binding.context.resource.weaponGeneration},input.owner.space};
    // Magazine is the portable multi-round pickup shape, never a native
    // magazine request. The typed feed command retains its own operation.
    source.family=ReloadInsertionFamily::Magazine;source.reserveUnits=unsigned(v.counts.reserve);
    source.objectUnits=unsigned((std::min)(v.counts.capacity-v.counts.loaded,v.counts.reserve));
    source.sequence=v.sequence;source.observedNs=(std::max)(v.observedNs,v.binding.observedNs);
    source.deadlineNs=(std::min)(v.deadlineNs,v.binding.deadlineNs);source.verified=true;return source;
}
bool Bc2FeedBodyRetained(const Bc2FeedBodyAmmo& a,const Bc2FeedBodyAmmo& b)noexcept {
    return a.registrationKey==b.registrationKey&&a.lifetime==b.lifetime&&a.supply==b.supply&&
        FeedEquipmentSameItem(a.native,b.native)&&a.native.sequence<=b.native.sequence&&
        a.native.observedNs<=b.native.observedNs&&a.native.counts==b.native.counts&&
        (a.native.sequence!=b.native.sequence||a.native==b.native);
}
AmmoSupplyConfig Bc2FeedSupplyConfig()noexcept {
    auto config=ChestAmmoSupply();config.itemNamespace=0x4243324645454441ull;return config;
}
bool Bc2FeedSupplyContact(const math::Matrix4& pose)noexcept {
    if(!feed_mechanism_detail::Pose(pose))return false;const auto c=Bc2FeedSupplyConfig();
    const auto inside=[&](const AmmoSupplyContact& contact){double d=0;
        for(unsigned n=0;n<3;++n){const auto x=double(pose.values[3][n])-contact.centerMeters[n];d+=x*x;}
        return d<=double(contact.radiusMeters)*contact.radiusMeters;};
    return inside({c.pouchCenterMeters,c.pouchRadiusMeters})||(c.alternateContact&&inside(*c.alternateContact));
}
bool Bc2FeedAmmoSupply::Update(const Bc2FeedBodyAmmo& body,const HandInteractionSample& input,
    const std::optional<math::Matrix4>& pose,bool grip,bool acquire,HandInteraction& hands,std::uint64_t& intent)noexcept {
    const auto source=Bc2FeedBodySource(body,input,input.nowNs);
    const auto gun=hands.Current(InteractionHand::Right);
    if(!source||!gun||gun->token.kind!=HandClaimKind::GunHold||gun->token.owner!=input.owner||
       gun->token.item!=source->identity.weapon||gun->inputSequence!=input.sequence||gun->deadlineNs<=input.nowNs||
       !input.focused||!input.tracked[0]||!input.tracked[1]||input.released[1]){Suspend(input,hands);return false;}
    AmmoSupplySample sample;sample.input=input;sample.source=*source;sample.trackingEpoch=input.owner.space;
    sample.gripPressed=grip;if(pose){sample.bodyFromHand=*pose;sample.geometrySequence=input.sequence;}
    if(acquire&&!custody_&&intent!=UINT64_MAX)sample.intent=++intent;
    const auto result=acquire&&!custody_?pickup_.Update(sample,hands):pickup_.WaitHeld(sample,hands);
    if(result.acquired&&result.held){custody_=Bc2FeedSupplyCustody{body,*source,*result.held,body,input.owner,{}};return true;}
    return false;
}
void Bc2FeedAmmoSupply::Suspend(const HandInteractionSample& input,HandInteraction& hands)noexcept {
    (void)pickup_.Cancel(input,hands); // Typed reserve custody survives loss of pose/hand.
}
bool Bc2FeedAmmoSupply::Ready(const Bc2FeedBodyAmmo& body,const HandInteractionSample& input)const noexcept {
    if(!custody_||custody_->pending)return false;const auto source=Bc2FeedBodySource(body,input,input.nowNs);
    return source&&Bc2FeedBodyRetained(custody_->context,body)&&source->identity.owner==custody_->handOwner&&
        source->objectUnits==custody_->object.units&&source->reserveUnits==custody_->source.reserveUnits;
}
bool Bc2FeedAmmoSupply::Migrate(const FeedContextMigration& migration,const Bc2FeedBodyAmmo& body,
    const HandInteractionSample& input)noexcept {
    if(!custody_)return true;if(custody_->pending||pickup_.Held())return false;
    const auto& previous=custody_->context;const auto& a=previous.native;const auto& b=body.native;
    const auto* row=FindBc2FeedRegistration(body.registrationKey);
    if(!row||!Bc2FeedBodySource(body,input,input.nowNs)||previous.registrationKey!=body.registrationKey||
       previous.lifetime!=body.lifetime||previous.supply!=body.supply||a.binding.context.resource!=b.binding.context.resource||
       a.binding.inventory!=b.binding.inventory||a.binding.switching!=b.binding.switching||a.binding.data!=b.binding.data||
       a.binding.persistence!=b.binding.persistence||a.counts!=b.counts||
       !feed_mechanism_detail::Migration(migration,a.binding.context.resource,FeedEquipmentOwner(a.identity.owner),
            FeedEquipmentOwner(b.identity.owner),input.nowNs,row->mechanism.maxAgeNs))return false;
    custody_->context=body;custody_->handOwner=input.owner;return true;
}
bool Bc2FeedAmmoSupply::Bind(const FeedAmmunitionCommand& command,const Bc2FeedBodyAmmo& body,
    const HandInteractionSample& input,HandInteraction& hands)noexcept {
    if(!Ready(body,input)||command.authority.effect!=FeedAmmunitionEffect::RefillLoaded||
       command.authority.operation!=FeedPhysicalOperation::AttachContainer||command.request.lifetime!=body.lifetime||
       command.before.owner!=FeedEquipmentOwner(body.native.identity.owner)||command.before.lifetime!=body.lifetime||
       command.authority.owner!=command.before.owner||command.authority.lifetime!=body.lifetime||
       !command.request.id||!command.mutationRequired||command.loadedSupplyAfter!=body.supply||
       command.before.counts!=body.native.counts||command.before.sequence!=body.native.sequence||
       command.authority.supply!=body.supply||command.authority.rounds!=int(custody_->object.units)||
       command.after.reserve!=command.before.counts.reserve-command.authority.rounds||
       command.after.loaded!=command.before.counts.loaded+command.authority.rounds)return false;
    custody_->pending=command;Suspend(input,hands);return true;
}
bool Bc2FeedAmmoSupply::Complete(const FeedAmmunitionReceipt& receipt)noexcept {
    if(!custody_||!custody_->pending||receipt.command!=*custody_->pending||!receipt.authorityVerified||
       !receipt.copiesVerified||!receipt.authorityInvocation||receipt.completedNs<receipt.command.request.requestedNs||
       receipt.acknowledgement.request!=receipt.command.request)return false;
    if(receipt.acknowledgement.status==FeedReloadAcknowledgement::Applied){
        if(receipt.post.counts!=receipt.command.after)return false;custody_.reset();return true;
    }
    if(receipt.acknowledgement.status!=FeedReloadAcknowledgement::Rejected||!receipt.noTransferEntry||
       receipt.post.counts!=receipt.command.before.counts)return false;
    custody_->pending.reset();return true; // No-entry keeps the very same pickup.
}
}
