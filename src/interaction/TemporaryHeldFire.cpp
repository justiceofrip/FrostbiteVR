#include "fvr/interaction/TemporaryHeldFire.h"
#include <algorithm>
#include <cmath>
namespace fvr::interaction { namespace {
template<class T>bool Key(T k)noexcept{return k.id&&k.generation;}
bool Window(PickupWindow w,std::int64_t now)noexcept {
    return w.sequence&&w.observedNs>0&&w.observedNs<=now&&now<=w.deadlineNs&&
        w.deadlineNs>w.observedNs&&w.deadlineNs-w.observedNs<=150000000;
}
bool Pose(const math::Pose& a,const math::Pose& b)noexcept {
    return a.position.x==b.position.x&&a.position.y==b.position.y&&a.position.z==b.position.z&&
        a.orientation.x==b.orientation.x&&a.orientation.y==b.orientation.y&&
        a.orientation.z==b.orientation.z&&a.orientation.w==b.orientation.w;
}
bool Ledger(const PickupAmmoLedger& a)noexcept {
    if(a.count>a.entries.size())return false;
    for(std::uint32_t i=0;i<a.count;++i){if(!a.entries[i].kind||a.entries[i].rounds>INT32_MAX)return false;
        for(std::uint32_t j=0;j<i;++j)if(a.entries[i].kind==a.entries[j].kind)return false;}
    for(std::size_t i=a.count;i<a.entries.size();++i)if(a.entries[i]!=PickupAmmo{})return false;
    return true;
}
std::uint64_t Rounds(const PickupAmmoLedger& a,std::uint64_t kind)noexcept {
    for(std::uint32_t i=0;i<a.count;++i)if(a.entries[i].kind==kind)return a.entries[i].rounds;return 0;
}
bool NativeShape(const TemporaryHeldNativeReceipt& r,std::int64_t now)noexcept {
    const auto& id=r.identity;const auto& a=id.actor;const auto& p=id.projection;const auto& t=id.target;
    if(!Key(id.lease)||!Key(id.heldItem)||!Key(id.activationClear)||!Key(t.weapon)||!Key(t.clientFiring)||
       !Key(t.serverFiring)||!Key(t.ammunitionAuthority)||!a.world||!a.worldGeneration||!a.actor||!a.actorGeneration||
       id.originalWorldItem.world!=a.world||id.originalWorldItem.worldGeneration!=a.worldGeneration||
       !id.originalWorldItem.entity||!id.originalWorldItem.entityGeneration||
       id.controlOwner.actor!=a.actor||id.controlOwner.actorGeneration!=a.actorGeneration||
       !id.controlOwner.equipGeneration||!id.controlOwner.space||!id.inputEpoch||!id.activationInput||
       id.activatedNs<=0||id.activatedNs>r.source.observedNs||!Window(r.source,now)||!r.ammunitionRevision||
       !r.currentFireTarget||!r.ammunitionBound||!r.nativePlaying||!r.heldPresentationPaired||!r.permanentWeaponSuppressed||
       !r.activationTriggerCleared||!r.triggerRouteOwned||
       !p.permanent.inventory||!p.permanent.generation||!p.permanent.assignmentRevision||!p.nativeInventoryRevision||
       !Key(p.restoration)||!p.assignmentsUnchanged)return false;
    if(p.mode==TemporaryNativeEquip::ExternalActiveEntity)return !p.displaced&&p.borrowedSlot==0;
    return p.mode==TemporaryNativeEquip::BorrowedNativeSlot&&p.displaced&&Key(*p.displaced)&&p.displacedStillOwned;
}
bool InventoryShape(const PickupInventory& inventory)noexcept {
    if(!inventory.actor.world||!inventory.actor.worldGeneration||!inventory.actor.actor||!inventory.actor.actorGeneration||
       !inventory.inventory||!inventory.generation||!inventory.revision||!inventory.capacity||
       inventory.capacity>inventory.bundles.size()||inventory.count>inventory.capacity||!Ledger(inventory.ammo))return false;
    for(std::uint32_t i=0;i<inventory.count;++i){const auto& item=inventory.bundles[i];
        if(!Key(item.item)||!Key(item.contents)||!item.memberCount||item.memberCount>item.members.size())return false;
        for(std::uint32_t j=0;j<i;++j)if(item.item.id==inventory.bundles[j].item.id||item.slot==inventory.bundles[j].slot)return false;
        for(std::uint32_t n=0;n<item.memberCount;++n){
            if(!Key(item.members[n]))return false;
            for(std::uint32_t j=0;j<=i;++j){const auto count=j==i?n:inventory.bundles[j].memberCount;
                for(std::uint32_t k=0;k<count;++k)if(item.members[n].id==inventory.bundles[j].members[k].id)return false;}
        }
        for(std::size_t n=item.memberCount;n<item.members.size();++n)if(item.members[n]!=HandInteractionKey{})return false;
    }
    for(std::size_t i=inventory.count;i<inventory.bundles.size();++i)if(inventory.bundles[i]!=PickupBundle{})return false;
    return true;
}
}
bool TemporaryHeldFire::ValidNative(const TemporaryHeldNativeReceipt& r,std::int64_t now)const noexcept {
    return capabilities_.Ready(r.identity.projection.mode)&&NativeShape(r,now);
}
bool TemporaryHeldFire::ValidInputSource(const TemporaryHeldFireSample& s)const noexcept {
    if(!s.native||!s.gunClaim)return false;const auto& id=s.native->identity;const auto& c=*s.gunClaim;
    const auto& input=s.input;const auto& hand=s.hand;
    if(!ValidInput(input)||!input.focused||!input.headValid||!hand.focused||!s.inputEpoch||s.inputEpoch!=id.inputEpoch||
       input.spaceGeneration!=id.controlOwner.space||hand.owner!=id.controlOwner||hand.sequence!=input.generation||
       !Window({hand.sequence,hand.observedNs,hand.deadlineNs},hand.nowNs)||
       input.predictedNs>hand.nowNs+50000000||input.predictedNs<hand.nowNs-100000000||
       c.token.owner!=hand.owner||c.token.kind!=HandClaimKind::GunHold||c.token.item!=id.heldItem||!c.token.id||
       (c.token.hand!=InteractionHand::Left&&c.token.hand!=InteractionHand::Right)||
       c.inputSequence!=hand.sequence||c.deadlineNs<hand.nowNs||c.deadlineNs>hand.deadlineNs||
       hand.sequence<id.activationInput||hand.observedNs<id.activatedNs)return false;
    const auto index=static_cast<std::size_t>(c.token.hand);const auto& controller=input.hands[index];
    return hand.tracked[index]&&controller.gripTracked&&controller.aimTracked&&(controller.active&Trigger)!=0;
}
bool TemporaryHeldFire::ValidAim(const TemporaryHeldFireSample& s)const noexcept {
    if(!s.aim||!s.native||!s.gunClaim)return false;const auto& a=*s.aim;const auto& id=s.native->identity;
    const auto& raw=s.input.hands[static_cast<std::size_t>(s.gunClaim->token.hand)];
    return a.lease==id.lease&&a.target==id.target&&a.claim==s.gunClaim->token&&a.inputEpoch==s.inputEpoch&&
        a.space==s.input.spaceGeneration&&a.inputSource==PickupWindow{s.hand.sequence,s.hand.observedNs,s.hand.deadlineNs}&&
        Window(a.inputSource,s.hand.nowNs)&&Pose(a.rawGrip,raw.grip)&&Pose(a.rawAim,raw.aim)&&
        a.nativeAimBound&&bool(InverseRigid(a.trackedMuzzle));
}
TemporaryFireResult TemporaryHeldFire::Reject(TemporaryFireReason why)noexcept {
    TemporaryFireResult out;out.reason=why;if(identity_)out.revoked=identity_;
    armed_=held_=false;needsTransition_=true;return out;
}
TemporaryFireResult TemporaryHeldFire::Suspend()noexcept{return Reject(TemporaryFireReason::MissingEvidence);}
TemporaryFireResult TemporaryHeldFire::Update(const TemporaryHeldFireSample& s)noexcept {
    const auto now=s.hand.nowNs;
    if(now<=0||now>INT64_MAX-150000000||now<lastNow_)return Reject(TemporaryFireReason::ClockReversed);
    lastNow_=now;
    if(!s.native||!s.gunClaim||!s.aim)return Reject(TemporaryFireReason::MissingEvidence);
    if(!capabilities_.Ready(s.native->identity.projection.mode))return Reject(TemporaryFireReason::Disabled);
    if(!ValidNative(*s.native,now))return Reject(TemporaryFireReason::InvalidNative);
    if(!ValidInputSource(s))return Reject(TemporaryFireReason::InvalidInput);
    if(!ValidAim(s))return Reject(TemporaryFireReason::InvalidAim);
    const auto& id=s.native->identity;const bool changed=!identity_||*identity_!=id||!lastClaim_||*lastClaim_!=s.gunClaim->token;
    if(identity_&&identity_->lease==id.lease&&*identity_!=id)return Reject(TemporaryFireReason::InvalidNative);
    const auto& raw=s.input.hands[static_cast<std::size_t>(s.gunClaim->token.hand)];
    const PickupWindow packet{s.hand.sequence,s.hand.observedNs,s.hand.deadlineNs};
    if(!changed&&lastNative_){const auto& old=*lastNative_;
        if(s.native->source.sequence<old.source.sequence||s.native->source.observedNs<old.source.observedNs||
           s.native->ammunitionRevision<old.ammunitionRevision||
           (s.native->source.sequence==old.source.sequence&&
            (s.native->source!=old.source||s.native->ammunitionRevision!=old.ammunitionRevision)))return Reject(TemporaryFireReason::StalePacket);
    }
    if(!changed&&(packet.sequence<lastInput_.sequence||packet.observedNs<lastInput_.observedNs||
       (packet.sequence==lastInput_.sequence&&(packet!=lastInput_||raw.trigger!=lastTrigger_||
        !Pose(raw.grip,lastGrip_)||!Pose(raw.aim,lastAim_)))))return Reject(TemporaryFireReason::StalePacket);
    const bool newPacket=changed||packet.sequence>lastInput_.sequence;
    const bool longGap=!changed&&lastInput_.observedNs&&packet.observedNs-lastInput_.observedNs>150000000;
    TemporaryFireResult out;
    if(changed){if(identity_)out.revoked=identity_;identity_=id;lastClaim_=s.gunClaim->token;armed_=held_=false;needsTransition_=true;}
    lastNative_=s.native;
    if(newPacket){lastInput_=packet;lastTrigger_=raw.trigger;lastGrip_=raw.grip;lastAim_=raw.aim;}
    TemporaryFireCommand command{id,packet.sequence,s.native->ammunitionRevision,
        std::min({packet.deadlineNs,s.native->source.deadlineNs,s.gunClaim->deadlineNs}),s.aim->trackedMuzzle,false,false};
    out.command=command;
    if(needsTransition_||longGap){
        armed_=held_=false;
        // An invalidation followed by a duplicate input cannot count as release.
        if(newPacket)needsTransition_=false;
        out.reason=changed?TemporaryFireReason::IdentityChanged:TemporaryFireReason::AwaitingRelease;return out;
    }
    if(!armed_){
        if(newPacket&&packet.sequence>id.activationInput&&packet.observedNs>id.activatedNs&&raw.trigger<=.1f)armed_=true;
        out.armed=armed_;out.reason=TemporaryFireReason::AwaitingRelease;return out;
    }
    const bool fire=raw.trigger>=.75f;
    out.command->triggerHeld=fire;out.command->triggerPressed=newPacket&&fire&&!held_;
    held_=fire;out.armed=true;out.reason=TemporaryFireReason::None;return out;
}
bool ValidateTemporarySettlement(const TemporaryHeldNativeReceipt& original,const PickupInventory& before,
    const WorldPickupLease& held,const TemporaryHeldSettlementReceipt& settled,std::int64_t now)noexcept {
    const auto& id=original.identity;const auto& after=settled.permanentAfter;const auto& returned=settled.heldAfter;
    if(!NativeShape(original,original.source.observedNs)||!InventoryShape(before)||!InventoryShape(after)||
       !Window(before.source,before.source.observedNs)||!Window(held.source,held.source.observedNs)||
       before.source.observedNs>now||held.source.observedNs>now||
       !Key(held.contents)||!Key(held.interaction)||
       settled.identity!=id||!Key(settled.nativeConsumptionReceipt)||!settled.triggerCleared||
       !settled.nativeCallsDrained||!settled.projectionSettled||before.actor!=id.actor||after.actor!=id.actor||
       held.key!=id.originalWorldItem||held.interaction!=id.heldItem||returned.key!=held.key||
       returned.interaction!=held.interaction||!Key(returned.contents)||
       before.inventory!=id.projection.permanent.inventory||before.generation!=id.projection.permanent.generation||
       before.revision!=id.projection.permanent.assignmentRevision||
       after.inventory!=before.inventory||after.generation!=before.generation||after.revision<before.revision||
       !before.capacity||before.capacity>before.bundles.size()||before.count>before.capacity||
       after.count!=before.count||after.capacity!=before.capacity||after.bundles!=before.bundles||
       !Window(after.source,now)||!Window(returned.source,now)||
       after.source.observedNs<before.source.observedNs||returned.source.observedNs<held.source.observedNs||
       after.source.observedNs<original.source.observedNs||returned.source.observedNs<original.source.observedNs||
       after.source.sequence<=before.source.sequence||returned.source.sequence<=held.source.sequence)return false;
    if(id.projection.mode==TemporaryNativeEquip::BorrowedNativeSlot){
        const auto displaced=std::find_if(before.bundles.begin(),before.bundles.begin()+before.count,
            [&](const PickupBundle& item){return item.item==*id.projection.displaced&&item.slot==id.projection.borrowedSlot;});
        if(displaced==before.bundles.begin()+before.count)return false;
    }
    for(const auto* ledger:{&before.ammo,&held.ammo,&after.ammo,&returned.ammo,&settled.nativeConsumed})if(!Ledger(*ledger))return false;
    for(const auto* ledger:{&before.ammo,&held.ammo,&after.ammo,&returned.ammo,&settled.nativeConsumed})
        for(std::uint32_t i=0;i<ledger->count;++i){const auto kind=ledger->entries[i].kind;
            if(Rounds(before.ammo,kind)+Rounds(held.ammo,kind)!=
               Rounds(after.ammo,kind)+Rounds(returned.ammo,kind)+Rounds(settled.nativeConsumed,kind))return false;}
    return true;
}
}
