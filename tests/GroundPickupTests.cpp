#include "fvr/interaction/GroundPickup.h"
#include "Bc2GroundPickup.h"
#include <cstdio>
#include <stdexcept>
using namespace fvr::interaction;
#define CHECK(v) do { if(!(v)) throw std::runtime_error(#v); } while(false)
namespace {
constexpr GroundPickupCapabilities available{true,true,true,true,true,true,true};
constexpr std::int64_t start=1000000000;
PickupWindow Window(std::uint64_t seq=1,std::int64_t now=start){return {seq,now,now+100000000};}
PickupAmmoLedger Ammo(std::uint64_t rounds){PickupAmmoLedger a;a.count=1;a.entries[0]={7,rounds};return a;}
struct Fixture {
    GroundPickup policy{available};HandInteraction hands;
    PickupInventory inventory;WorldPickupLease world;HandClaim hand;
    GroundPickupPreviewRequest preview;GroundPickupSwapRequest request;
    std::int64_t now=start;
    Fixture(){
        inventory.actor={10,2,30,4};inventory.inventory=50;inventory.generation=6;inventory.revision=7;
        inventory.source=Window();inventory.capacity=2;inventory.count=2;
        inventory.bundles[0]={{100,1},{101,1},0,1,{{{102,1}}}};
        inventory.bundles[1]={{200,1},{201,1},1,2,{{{202,1},{203,1}}}};
        inventory.ammo=Ammo(80);
        world={{10,2,500,9},{501,9},{502,9},Window(),Ammo(40)};
        HandInteractionSample sample{{30,4,8,12},1,now,now+100000000,now,true,{true,true},{false,false}};
        CHECK(hands.Update(sample).inputValid);
        HandClaimRequest q{sample.owner,InteractionHand::Left,HandClaimKind::BodyInventory,world.interaction,
            {{900,1},sample.sequence,sample.deadlineNs,true},1,0};
        auto result=hands.Acquire(sample,q);CHECK(result.accepted&&result.claim);hand=*result.claim;
    }
    PickupSlotBinding Binding()const{return {3,inventory.bundles[0].item,0,inventory.revision,inventory.source};}
    void Begin(){auto r=policy.Begin(inventory,world,hand,now);CHECK(r);preview=*r;}
    GroundPickupPreviewReceipt PreviewReceipt()const{return {preview.id,inventory.actor,world.key,hand.token,Window(2,now),true,true,true};}
    void Hold(){Begin();CHECK(policy.Preview(PreviewReceipt(),now));}
    void Stow(){Hold();auto r=policy.Stow(inventory,world,hand,Binding(),now);CHECK(r);request=*r;}
    void Dispatch(PickupDispatch d=PickupDispatch::Accepted){Stow();CHECK(policy.DispatchAllowed(request.id,inventory,world,hand,now));CHECK(policy.Dispatched(request.id,d,now));}
    GroundPickupExchangeReceipt ExchangeReceipt()const{
        auto after=inventory;after.revision++;after.source=Window(3,now+1);after.ammo=Ammo(90);
        // Replace one one-member root with a three-member linked weapon bundle.
        after.bundles[0]={{600,1},world.contents,0,3,{{{601,1},{602,1},{603,1}}}};
        WorldPickupLease drop{{10,2,700,1},{701,1},inventory.bundles[0].contents,Window(3,now+1),Ammo(30)};
        return {request.id,inventory.actor,world.key,world.contents,{600,1},inventory.bundles[0].item,drop,after,true,true};
    }
    GroundPickupCleanupReceipt CleanupReceipt(bool exchanged)const{
        return {preview.id,request.id,inventory.actor,world.key,exchanged?BodyItemKey{600,1}:BodyItemKey{},
            exchanged?3u:0u,Window(4,now+2),exchanged?PickupCleanup::HolsterAcquired:PickupCleanup::RestoreWorld,true,true,true};
    }
};
void DefaultUnavailable(){Fixture f;GroundPickup disabled;CHECK(!disabled.Begin(f.inventory,f.world,f.hand,f.now));CHECK(!fvr::bc2::GroundPickupCapabilities().Ready());}
void RealHandOwnership(){Fixture f;auto wrong=f.hand;wrong.token.kind=HandClaimKind::AmmoObject;CHECK(!f.policy.Begin(f.inventory,f.world,wrong,f.now));wrong=f.hand;wrong.token.owner.actorGeneration++;CHECK(!f.policy.Begin(f.inventory,f.world,wrong,f.now));f.Begin();}
void NoAutomaticInventoryCommit(){Fixture f;f.Hold();CHECK(!f.policy.Pending());CHECK(!f.policy.Acquired());CHECK(f.policy.Phase()==GroundPickupPhase::Temporary);CHECK(f.inventory.count==2&&f.inventory.ammo.entries[0].rounds==80);}
void ActualPreviewRequired(){Fixture f;f.Begin();CHECK(!f.policy.Stow(f.inventory,f.world,f.hand,f.Binding(),f.now));auto r=f.PreviewReceipt();r.pairedHeld=false;CHECK(!f.policy.Preview(r,f.now));r=f.PreviewReceipt();r.actionsSuppressed=false;CHECK(!f.policy.Preview(r,f.now));r=f.PreviewReceipt();r.worldConcealed=false;CHECK(!f.policy.Preview(r,f.now));CHECK(f.policy.Preview(f.PreviewReceipt(),f.now));}
void LinkedBundleAndAmmoConservation(){Fixture f;f.Dispatch();auto r=f.ExchangeReceipt();CHECK(f.policy.Exchange(r,f.now+1));CHECK(f.policy.Phase()==GroundPickupPhase::HolsteringAcquired);CHECK(f.policy.Acquired()==std::optional(BodyItemKey{600,1}));CHECK(!f.policy.Reset());auto finish=f.CleanupReceipt(true);finish.pairedPresentation=false;CHECK(!f.policy.Cleanup(finish,f.now+2));CHECK(f.policy.Cleanup(f.CleanupReceipt(true),f.now+2));CHECK(f.policy.Phase()==GroundPickupPhase::Complete);CHECK(!f.policy.PresentationAuthority(f.inventory.actor,f.now+2));CHECK(f.policy.Reset());}
void ExactChosenSlot(){Fixture f;f.Hold();auto b=f.Binding();b.item=f.inventory.bundles[1].item;CHECK(!f.policy.Stow(f.inventory,f.world,f.hand,b,f.now));b=f.Binding();b.inventoryRevision++;CHECK(!f.policy.Stow(f.inventory,f.world,f.hand,b,f.now));b=f.Binding();b.source.sequence++;CHECK(!f.policy.Stow(f.inventory,f.world,f.hand,b,f.now));CHECK(f.policy.Stow(f.inventory,f.world,f.hand,f.Binding(),f.now));}
void WorldGenerationAndContentsChange(){for(int mode=0;mode<3;++mode){Fixture f;f.Hold();auto w=f.world;if(mode==0)++w.key.entityGeneration;else if(mode==1)++w.key.worldGeneration;else ++w.contents.generation;CHECK(!f.policy.Stow(f.inventory,w,f.hand,f.Binding(),f.now));CHECK(f.policy.Phase()==GroundPickupPhase::RestoringWorld);}}
void NativePickupRaceInvalidatesPreview(){Fixture f;f.Hold();auto i=f.inventory;i.revision++;i.bundles[0].item.generation++;CHECK(!f.policy.Stow(i,f.world,f.hand,f.Binding(),f.now));CHECK(!f.policy.Pending());}
void NoExtraPermanentWeapon(){Fixture f;f.Dispatch();auto r=f.ExchangeReceipt();r.after.capacity++;CHECK(!f.policy.Exchange(r,f.now+1));r=f.ExchangeReceipt();r.after.count=3;r.after.capacity=3;r.after.bundles[2]={{800,1},{801,1},2,1,{{{802,1}}}};CHECK(!f.policy.Exchange(r,f.now+1));CHECK(!f.policy.Acquired());}
void UntouchedWeaponAndDropExact(){Fixture f;f.Dispatch();auto r=f.ExchangeReceipt();r.after.bundles[1].contents.generation++;CHECK(!f.policy.Exchange(r,f.now+1));r=f.ExchangeReceipt();r.droppedOwned.generation++;CHECK(!f.policy.Exchange(r,f.now+1));r=f.ExchangeReceipt();r.droppedWorld.contents.generation++;CHECK(!f.policy.Exchange(r,f.now+1));r=f.ExchangeReceipt();r.droppedWorld.key=f.world.key;CHECK(!f.policy.Exchange(r,f.now+1));CHECK(f.policy.Exchange(f.ExchangeReceipt(),f.now+1));}
void NoFreeOrLostAmmunition(){Fixture f;f.Dispatch();for(int delta:{-1,1}){auto r=f.ExchangeReceipt();r.after.ammo.entries[0].rounds=std::uint64_t(90+delta);CHECK(!f.policy.Exchange(r,f.now+1));}auto r=f.ExchangeReceipt();r.droppedWorld.ammo.entries[0].kind=8;CHECK(!f.policy.Exchange(r,f.now+1));r=f.ExchangeReceipt();r.after.ammo.count=2;r.after.ammo.entries[1]=r.after.ammo.entries[0];CHECK(!f.policy.Exchange(r,f.now+1));CHECK(f.policy.Exchange(f.ExchangeReceipt(),f.now+1));}
void UnknownDoesNotRollback(){Fixture f;f.Dispatch(PickupDispatch::Unknown);CHECK(!f.policy.Reset());f.policy.Cancel();CHECK(f.policy.Phase()==GroundPickupPhase::ReconcileRequired);CHECK(!f.policy.Cleanup(f.CleanupReceipt(false),f.now+2));auto r=f.ExchangeReceipt();r.after.source=Window(3,f.now+3);r.droppedWorld.source=r.after.source;CHECK(f.policy.Exchange(r,f.now+3));}
void UnknownRequiresNonExecutionReceipt(){Fixture f;f.Dispatch(PickupDispatch::Unknown);auto i=f.inventory;i.source=Window(3,f.now+1);auto w=f.world;w.source=Window(3,f.now+1);GroundPickupNoDispatchReceipt no{f.request.id,f.inventory.actor,false,true};CHECK(!f.policy.ProveNotStarted(no,i,w,f.now+1));no.noNativeExchangeStarted=true;no.nativeCallsDrained=false;CHECK(!f.policy.ProveNotStarted(no,i,w,f.now+1));no.nativeCallsDrained=true;CHECK(f.policy.ProveNotStarted(no,i,w,f.now+1));CHECK(f.policy.Cleanup(f.CleanupReceipt(false),f.now+2));}
void ReleaseBeforeSwapHasNoNativeCommand(){Fixture f;f.Hold();f.policy.Cancel();CHECK(!f.policy.Pending());CHECK(f.policy.Phase()==GroundPickupPhase::RestoringWorld);CHECK(f.policy.Cleanup(f.CleanupReceipt(false),f.now+2));CHECK(f.policy.Phase()==GroundPickupPhase::Cancelled);}
void DispatchChecksLifetimeAndFreshSource(){Fixture f;f.Stow();auto w=f.world;w.key.entityGeneration++;CHECK(!f.policy.DispatchAllowed(f.request.id,f.inventory,w,f.hand,f.now));CHECK(!f.policy.DispatchAllowed(f.request.id,f.inventory,f.world,f.hand,f.now+100000001));f.policy.Tick(f.now+100000002);CHECK(f.policy.Phase()==GroundPickupPhase::ReconcileRequired);CHECK(!f.policy.PresentationAuthority(f.inventory.actor,f.now+100000002));CHECK(!f.policy.Reset());}
void NotStartedRestoresWithoutPickup(){Fixture f;f.Dispatch(PickupDispatch::NotStarted);CHECK(f.policy.Phase()==GroundPickupPhase::RestoringWorld);CHECK(!f.policy.Acquired());CHECK(f.policy.Cleanup(f.CleanupReceipt(false),f.now+2));}
void StaleReceiptsAndDuplicateDispatch(){Fixture f;f.Dispatch();CHECK(!f.policy.Dispatched(f.request.id,PickupDispatch::Accepted,f.now));auto r=f.ExchangeReceipt();r.request++;CHECK(!f.policy.Exchange(r,f.now+1));r=f.ExchangeReceipt();r.nativeCallsDrained=false;CHECK(!f.policy.Exchange(r,f.now+1));r=f.ExchangeReceipt();r.after.source.sequence=f.request.source.sequence;CHECK(!f.policy.Exchange(r,f.now+1));CHECK(f.policy.Exchange(f.ExchangeReceipt(),f.now+1));CHECK(!f.policy.Exchange(f.ExchangeReceipt(),f.now+1));}
void SceneRetirementDoesNotRestoreStaleEntity(){Fixture f;f.Dispatch(PickupDispatch::Unknown);GroundPickupRetirementReceipt r{f.inventory.actor,f.preview.id,f.request.id,true,false,true};CHECK(!f.policy.Retire(r));r.nativeCallsDrained=true;r.oldActor.worldGeneration++;CHECK(!f.policy.Retire(r));r.oldActor=f.inventory.actor;CHECK(f.policy.Retire(r));CHECK(!f.policy.PresentationAuthority(f.inventory.actor,f.now));CHECK(f.policy.Reset());}
void DuplicatePreviewCannotExtendDeadline(){Fixture f;f.Hold();auto r=f.PreviewReceipt();r.source.deadlineNs++;CHECK(!f.policy.Preview(r,f.now));CHECK(!f.policy.PresentationAuthority(f.inventory.actor,f.now+100000001));}
void MalformedInventoryAndPointerAlias(){for(int mode=0;mode<4;++mode){Fixture f;auto i=f.inventory;if(mode==0)i.bundles[1].members[0]=i.bundles[0].members[0];if(mode==1)i.bundles[1].slot=i.bundles[0].slot;if(mode==2)i.bundles[1].item.id=i.bundles[0].item.id;if(mode==3)i.ammo.entries[0].rounds=UINT64_MAX;CHECK(!f.policy.Begin(i,f.world,f.hand,f.now));}}
void DuplicateNativeSequenceImmutable(){for(int which=0;which<2;++which){Fixture f;f.Hold();auto i=f.inventory;auto w=f.world;if(which==0)++i.source.deadlineNs;else ++w.source.deadlineNs;CHECK(!f.policy.Stow(i,w,f.hand,f.Binding(),f.now));CHECK(f.policy.Phase()==GroundPickupPhase::RestoringWorld);}}
void ClockRollbackDoesNotDiscardPendingNative(){Fixture f;f.Dispatch();f.policy.Tick(f.now-1);CHECK(f.policy.Phase()==GroundPickupPhase::ReconcileRequired);CHECK(!f.policy.Reset());CHECK(f.policy.Exchange(f.ExchangeReceipt(),f.now+1));}
void ForeignOwnerHasNoSuppressionAuthority(){Fixture f;f.Hold();auto owner=f.inventory.actor;owner.actorGeneration++;CHECK(!f.policy.PresentationAuthority(owner,f.now));CHECK(f.policy.PresentationAuthority(f.inventory.actor,f.now));f.policy.Cancel();CHECK(f.policy.PresentationAuthority(f.inventory.actor,f.now));f.policy.Tick(f.now+100000001);CHECK(!f.policy.PresentationAuthority(f.inventory.actor,f.now+100000001));}
void PostSwapHolsterReceiptIsSeparate(){Fixture f;f.Dispatch();CHECK(f.policy.Exchange(f.ExchangeReceipt(),f.now+1));auto r=f.CleanupReceipt(true);r.holster++;CHECK(!f.policy.Cleanup(r,f.now+2));r=f.CleanupReceipt(true);r.acquired.generation++;CHECK(!f.policy.Cleanup(r,f.now+2));r=f.CleanupReceipt(true);r.operation=PickupCleanup::RestoreWorld;CHECK(!f.policy.Cleanup(r,f.now+2));CHECK(f.policy.Cleanup(f.CleanupReceipt(true),f.now+2));}
}
int main(){
    using Test=void(*)();const Test tests[]={DefaultUnavailable,RealHandOwnership,NoAutomaticInventoryCommit,ActualPreviewRequired,
        LinkedBundleAndAmmoConservation,ExactChosenSlot,WorldGenerationAndContentsChange,NativePickupRaceInvalidatesPreview,
        NoExtraPermanentWeapon,UntouchedWeaponAndDropExact,NoFreeOrLostAmmunition,UnknownDoesNotRollback,
        UnknownRequiresNonExecutionReceipt,ReleaseBeforeSwapHasNoNativeCommand,DispatchChecksLifetimeAndFreshSource,
        NotStartedRestoresWithoutPickup,StaleReceiptsAndDuplicateDispatch,SceneRetirementDoesNotRestoreStaleEntity,
        DuplicatePreviewCannotExtendDeadline,MalformedInventoryAndPointerAlias,DuplicateNativeSequenceImmutable,
        ClockRollbackDoesNotDiscardPendingNative,ForeignOwnerHasNoSuppressionAuthority,PostSwapHolsterReceiptIsSeparate};
    for(std::size_t i=0;i<std::size(tests);++i)try{tests[i]();}catch(const std::exception& e){std::printf("FAIL group %zu: %s\n",i+1,e.what());return 1;}
    std::printf("PASS %zu GroundPickup groups\n",std::size(tests));return 0;
}
