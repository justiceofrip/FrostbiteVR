#include "fvr/interaction/GroundPickup.h"
#include <algorithm>
#include <limits>

namespace fvr::interaction { namespace {
template<class T>bool Key(T k)noexcept{return k.id&&k.generation;}
bool Actor(PickupActor a)noexcept{return a.world&&a.worldGeneration&&a.actor&&a.actorGeneration;}
bool WorldKey(WorldPickupKey k)noexcept{return k.world&&k.worldGeneration&&k.entity&&k.entityGeneration;}
bool InWorld(WorldPickupKey k,PickupActor a)noexcept{return k.world==a.world&&k.worldGeneration==a.worldGeneration;}
bool Claim(const HandClaim& c,const PickupInventory& i,const WorldPickupLease& w,std::int64_t now)noexcept {
    return c.token.id&&c.token.kind==HandClaimKind::BodyInventory&&c.token.item==w.interaction&&
        c.token.owner.actor==i.actor.actor&&c.token.owner.actorGeneration==i.actor.actorGeneration&&
        c.token.owner.equipGeneration&&c.token.owner.space&&c.inputSequence&&c.deadlineNs>=now&&
        c.deadlineNs-now<=150000000&&
        (c.token.hand==InteractionHand::Left||c.token.hand==InteractionHand::Right);
}
}
bool GroundPickup::Fresh(PickupWindow w,std::int64_t now)noexcept {
    return now>=0&&w.sequence&&w.observedNs>=0&&w.observedNs<=now&&now<=w.deadlineNs&&
        w.deadlineNs>w.observedNs&&w.deadlineNs-w.observedNs<=150000000;
}
bool GroundPickup::Ledger(const PickupAmmoLedger& a)noexcept {
    if(a.count>a.entries.size())return false;
    for(std::uint32_t n=0;n<a.count;++n){
        if(!a.entries[n].kind||a.entries[n].rounds>INT32_MAX)return false;
        for(std::uint32_t j=0;j<n;++j)if(a.entries[j].kind==a.entries[n].kind)return false;
    }
    for(std::size_t n=a.count;n<a.entries.size();++n)if(a.entries[n]!=PickupAmmo{})return false;
    return true;
}
bool GroundPickup::Inventory(const PickupInventory& i)noexcept {
    if(!Actor(i.actor)||!i.inventory||!i.generation||!i.revision||!i.capacity||
       i.capacity>i.bundles.size()||i.count>i.capacity||!Ledger(i.ammo))return false;
    for(std::uint32_t n=0;n<i.count;++n){const auto& b=i.bundles[n];
        if(!Key(b.item)||!Key(b.contents)||!b.memberCount||b.memberCount>b.members.size())return false;
        for(std::uint32_t j=0;j<n;++j)if(i.bundles[j].item.id==b.item.id||i.bundles[j].slot==b.slot)return false;
        for(std::uint32_t m=0;m<b.memberCount;++m){
            if(!Key(b.members[m]))return false;
            for(std::uint32_t j=0;j<=n;++j){const auto& other=i.bundles[j];
                const auto bound=j==n?m:other.memberCount;
                for(std::uint32_t k=0;k<bound;++k)if(b.members[m].id==other.members[k].id)return false;
            }
        }
        for(std::size_t m=b.memberCount;m<b.members.size();++m)if(b.members[m]!=HandInteractionKey{})return false;
    }
    for(std::size_t n=i.count;n<i.bundles.size();++n)if(i.bundles[n]!=PickupBundle{})return false;
    return true;
}
bool GroundPickup::World(const WorldPickupLease& w)noexcept {
    return WorldKey(w.key)&&Key(w.interaction)&&Key(w.contents)&&Ledger(w.ammo);
}
bool GroundPickup::Conserved(const PickupAmmoLedger& before,const PickupAmmoLedger& incoming,
    const PickupAmmoLedger& after,const PickupAmmoLedger& dropped)noexcept {
    if(!Ledger(before)||!Ledger(incoming)||!Ledger(after)||!Ledger(dropped))return false;
    const auto rounds=[](const PickupAmmoLedger& a,std::uint64_t kind){
        for(std::uint32_t i=0;i<a.count;++i)if(a.entries[i].kind==kind)return a.entries[i].rounds;
        return std::uint64_t(0);
    };
    for(const auto* ledger:{&before,&incoming,&after,&dropped})for(std::uint32_t i=0;i<ledger->count;++i){
        const auto kind=ledger->entries[i].kind;
        if(rounds(before,kind)+rounds(incoming,kind)!=rounds(after,kind)+rounds(dropped,kind))return false;
    }
    return true;
}
bool GroundPickup::Clock(std::int64_t now)noexcept {
    if(now<0||now<lastNow_){Cancel();reason_=GroundPickupReason::InvalidEvidence;return false;}
    lastNow_=now;return true;
}
bool GroundPickup::SameBaseline(const PickupInventory& i,const WorldPickupLease& w,std::int64_t now)const noexcept {
    if(!Inventory(i)||!World(w)||!Fresh(i.source,now)||!Fresh(w.source,now)||
       i.source.sequence<before_.source.sequence||w.source.sequence<incoming_.source.sequence)return false;
    if((i.source.sequence==before_.source.sequence&&i.source!=before_.source)||
       (w.source.sequence==incoming_.source.sequence&&w.source!=incoming_.source)||
       i.source.observedNs<before_.source.observedNs||w.source.observedNs<incoming_.source.observedNs)return false;
    auto inventory=i;inventory.source=before_.source;
    auto world=w;world.source=incoming_.source;
    return inventory==before_&&world==incoming_;
}
std::optional<GroundPickupPreviewRequest> GroundPickup::Begin(const PickupInventory& i,
    const WorldPickupLease& w,const HandClaim& claim,std::int64_t now)noexcept {
    if(phase_!=GroundPickupPhase::Idle||!capabilities_.Ready()){
        reason_=GroundPickupReason::Unavailable;return std::nullopt;
    }
    if(!Clock(now)||!Inventory(i)||!World(w)||!Fresh(i.source,now)||!Fresh(w.source,now)||!InWorld(w.key,i.actor)){
        reason_=GroundPickupReason::InvalidEvidence;return std::nullopt;
    }
    if(!Claim(claim,i,w,now)){reason_=GroundPickupReason::HandBusy;return std::nullopt;}
    for(std::uint32_t n=0;n<i.count;++n)if(i.bundles[n].contents==w.contents){
        reason_=GroundPickupReason::InvalidEvidence;return std::nullopt;
    }
    if(serial_==UINT64_MAX){reason_=GroundPickupReason::Unavailable;return std::nullopt;}
    before_=i;incoming_=w;claim_=claim;
    preview_=GroundPickupPreviewRequest{++serial_,i.actor,w.key,claim.token,i.source};
    phase_=GroundPickupPhase::PreparingPreview;reason_=GroundPickupReason::None;return preview_;
}
bool GroundPickup::Preview(const GroundPickupPreviewReceipt& r,std::int64_t now)noexcept {
    if(!Clock(now)||!preview_||(phase_!=GroundPickupPhase::PreparingPreview&&phase_!=GroundPickupPhase::Temporary)||
       r.request!=preview_->id||r.actor!=preview_->actor||r.worldItem!=preview_->worldItem||r.claim!=claim_.token||
       !Fresh(r.source,now)||r.source.observedNs<preview_->source.observedNs||
       !r.worldConcealed||!r.pairedHeld||!r.actionsSuppressed||
       (presentation_&&(r.source.sequence<presentation_->source.sequence||
        (r.source.sequence==presentation_->source.sequence&&r.source!=presentation_->source)))){
        reason_=GroundPickupReason::StaleReceipt;return false;
    }
    presentation_=r;phase_=GroundPickupPhase::Temporary;reason_=GroundPickupReason::None;return true;
}
bool GroundPickup::PresentationAuthority(const PickupActor& actor,std::int64_t now)const noexcept {
    return presentation_&&actor==presentation_->actor&&Fresh(presentation_->source,now)&&
        phase_!=GroundPickupPhase::Complete&&phase_!=GroundPickupPhase::Cancelled&&phase_!=GroundPickupPhase::Idle;
}
std::optional<GroundPickupSwapRequest> GroundPickup::Stow(const PickupInventory& i,const WorldPickupLease& w,
    const HandClaim& claim,const PickupSlotBinding& binding,std::int64_t now)noexcept {
    if(!Clock(now)||phase_!=GroundPickupPhase::Temporary||!preview_||!PresentationAuthority(i.actor,now)){
        reason_=GroundPickupReason::PreviewPending;return std::nullopt;
    }
    if(!SameBaseline(i,w,now)||claim.token!=claim_.token||!Claim(claim,i,w,now)){
        Cancel();reason_=GroundPickupReason::ChangedEvidence;return std::nullopt;
    }
    const PickupBundle* old=nullptr;
    for(std::uint32_t n=0;n<i.count;++n)if(i.bundles[n].item==binding.item)old=&i.bundles[n];
    if(!binding.holster||!old||binding.nativeSlot!=old->slot||binding.inventoryRevision!=i.revision||
       !Fresh(binding.source,now)||binding.source.sequence!=i.source.sequence||
       binding.source.observedNs!=i.source.observedNs){reason_=GroundPickupReason::InvalidSlot;return std::nullopt;}
    if(serial_==UINT64_MAX){Cancel();reason_=GroundPickupReason::Unavailable;return std::nullopt;}
    // No slot late-resolution: chosen holster/contact must bind this exact item.
    swap_=GroundPickupSwapRequest{++serial_,preview_->id,i.actor,w.key,w.contents,
        i.inventory,i.generation,i.revision,old->slot,binding.holster,binding.item,i.source};
    phase_=GroundPickupPhase::AwaitingDispatch;reason_=GroundPickupReason::None;return swap_;
}
bool GroundPickup::DispatchAllowed(std::uint64_t request,const PickupInventory& i,const WorldPickupLease& w,
    const HandClaim& claim,std::int64_t now)noexcept {
    return Clock(now)&&phase_==GroundPickupPhase::AwaitingDispatch&&swap_&&swap_->id==request&&
        SameBaseline(i,w,now)&&claim.token==claim_.token&&Claim(claim,i,w,now)&&PresentationAuthority(i.actor,now);
}
bool GroundPickup::Dispatched(std::uint64_t request,PickupDispatch disposition,std::int64_t now)noexcept {
    if(!Clock(now)||!swap_||request!=swap_->id||phase_!=GroundPickupPhase::AwaitingDispatch)return false;
    if(disposition==PickupDispatch::NotStarted){phase_=GroundPickupPhase::RestoringWorld;return true;}
    if(disposition==PickupDispatch::Accepted){phase_=GroundPickupPhase::AwaitingNative;return true;}
    phase_=GroundPickupPhase::ReconcileRequired;reason_=GroundPickupReason::UnknownDispatch;return true;
}
bool GroundPickup::Exchange(const GroundPickupExchangeReceipt& r,std::int64_t now)noexcept {
    if(!Clock(now)||!swap_||(phase_!=GroundPickupPhase::AwaitingNative&&phase_!=GroundPickupPhase::ReconcileRequired))return false;
    const auto& q=*swap_;const auto& a=r.after;
    const auto fail=[&](){reason_=GroundPickupReason::ExchangeUnproved;return false;};
    if(r.request!=q.id||r.actor!=q.actor||r.consumed!=q.incoming||r.incomingContents!=q.contents||
       !r.incomingRetired||!r.nativeCallsDrained||r.droppedOwned!=q.outgoing||!Key(r.acquired)||
       !Inventory(a)||!Fresh(a.source,now)||a.actor!=q.actor||a.inventory!=q.inventory||
       a.generation!=q.inventoryGeneration||a.revision<=q.revision||a.source.sequence<=q.source.sequence||
       a.source.observedNs<q.source.observedNs||a.capacity!=before_.capacity||a.count!=before_.count||
       !World(r.droppedWorld)||!Fresh(r.droppedWorld.source,now)||!InWorld(r.droppedWorld.key,q.actor)||
       r.droppedWorld.key==q.incoming||r.droppedWorld.source.observedNs<q.source.observedNs)return fail();
    const PickupBundle* old=nullptr;const PickupBundle* next=nullptr;
    for(std::uint32_t n=0;n<before_.count;++n){
        const auto& b=before_.bundles[n];
        if(b.item==r.acquired)return fail();
        if(b.slot==q.nativeSlot){old=&b;continue;}
        const auto found=std::find(a.bundles.begin(),a.bundles.begin()+a.count,b);
        if(found==a.bundles.begin()+a.count)return fail();
    }
    for(std::uint32_t n=0;n<a.count;++n)if(a.bundles[n].slot==q.nativeSlot)next=&a.bundles[n];
    if(!old||!next||old->item!=q.outgoing||next->item!=r.acquired||next->contents!=q.contents||
       r.droppedWorld.contents!=old->contents||
       !Conserved(before_.ammo,incoming_.ammo,a.ammo,r.droppedWorld.ammo))return fail();
    acquired_=r.acquired;exchangeObservedNs_=a.source.observedNs;
    phase_=GroundPickupPhase::HolsteringAcquired;reason_=GroundPickupReason::None;return true;
}
bool GroundPickup::ProveNotStarted(const GroundPickupNoDispatchReceipt& r,const PickupInventory& i,
    const WorldPickupLease& w,std::int64_t now)noexcept {
    if(!Clock(now)||!swap_||swap_->id!=r.request||swap_->actor!=r.actor||phase_!=GroundPickupPhase::ReconcileRequired||
       !r.noNativeExchangeStarted||!r.nativeCallsDrained||
       !SameBaseline(i,w,now)||i.source.sequence<=swap_->source.sequence||i.source.observedNs<swap_->source.observedNs)return false;
    phase_=GroundPickupPhase::RestoringWorld;reason_=GroundPickupReason::None;return true;
}
void GroundPickup::Cancel()noexcept {
    switch(phase_){
    case GroundPickupPhase::PreparingPreview:case GroundPickupPhase::Temporary:
        phase_=GroundPickupPhase::RestoringWorld;break;
    case GroundPickupPhase::AwaitingDispatch:case GroundPickupPhase::AwaitingNative:
        // Once a request is exposed, cancellation cannot infer it was unused.
        phase_=GroundPickupPhase::ReconcileRequired;break;
    default:break;
    }
}
void GroundPickup::Tick(std::int64_t now)noexcept {
    if(!Clock(now))return;
    if(preview_&&!PresentationAuthority(preview_->actor,now)&&
       phase_!=GroundPickupPhase::Idle&&phase_!=GroundPickupPhase::Complete&&phase_!=GroundPickupPhase::Cancelled){
        Cancel();reason_=GroundPickupReason::Expired;
    }
}
bool GroundPickup::Cleanup(const GroundPickupCleanupReceipt& r,std::int64_t now)noexcept {
    if(!Clock(now)||!preview_||r.preview!=preview_->id||r.actor!=preview_->actor||r.worldItem!=preview_->worldItem||
       !Fresh(r.source,now)||r.source.observedNs<preview_->source.observedNs||
       !r.temporaryReleased||!r.pairedPresentation||!r.actionsRestored)return false;
    if(phase_==GroundPickupPhase::RestoringWorld){
        if(r.operation!=PickupCleanup::RestoreWorld||r.exchange!=(swap_?swap_->id:0)||r.acquired!=BodyItemKey{}||r.holster)return false;
        phase_=GroundPickupPhase::Cancelled;
    }else if(phase_==GroundPickupPhase::HolsteringAcquired){
        if(!swap_||!acquired_||r.operation!=PickupCleanup::HolsterAcquired||r.exchange!=swap_->id||
           r.acquired!=*acquired_||r.holster!=swap_->holster||r.source.observedNs<exchangeObservedNs_)return false;
        phase_=GroundPickupPhase::Complete;
    }else return false;
    presentation_.reset();reason_=GroundPickupReason::None;return true;
}
bool GroundPickup::Retire(const GroundPickupRetirementReceipt& r)noexcept {
    if(!preview_||r.oldActor!=preview_->actor||r.preview!=preview_->id||r.exchange!=(swap_?swap_->id:0)||
       !r.actorGenerationRetired||!r.nativeCallsDrained||!r.temporaryReleased)return false;
    phase_=GroundPickupPhase::Cancelled;presentation_.reset();return true;
}
bool GroundPickup::Reset()noexcept {
    if(phase_!=GroundPickupPhase::Idle&&phase_!=GroundPickupPhase::Complete&&phase_!=GroundPickupPhase::Cancelled)return false;
    phase_=GroundPickupPhase::Idle;reason_=GroundPickupReason::None;
    before_={};incoming_={};claim_={};preview_.reset();presentation_.reset();swap_.reset();acquired_.reset();
    exchangeObservedNs_=0;return true;
}
}
