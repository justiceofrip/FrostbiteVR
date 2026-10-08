#include "fvr/interaction/AmmoSupply.h"
#include "Test.h"
using namespace fvr::interaction;
namespace {
constexpr std::int64_t Ms=1000000;
AmmoSupplyConfig Config(){return {InteractionHand::Left,1000,{1001,1},{-.25f,-.3f,0},.15f,200*Ms};}
struct Fixture {
    HandInteraction hands;
    AmmoSupply supply{Config()};
    AmmoSupplySample s{};
    std::optional<HandClaim> gun{};
    std::uint64_t intent=0,rightIntent=0;
    explicit Fixture(AmmoSupplyConfig config=Config()):supply(config){
        s.input={{1,2,3,4},1,1000*Ms,1100*Ms,1000*Ms,true,{true,true},{true,false}};
        s.source={{{1,2,3,4},{5,3},{6,1},{7,1},8},ReloadInsertionFamily::SingleShell,4,1,1,1000*Ms,1100*Ms,true};
        s.geometrySequence=1;s.trackingEpoch=8;s.bodyFromHand=reload_insertion_detail::Identity();
        s.bodyFromHand.values[3][0]=-.25f;s.bodyFromHand.values[3][1]=-.3f;
    }
    void Next(bool pressed){
        s.input.nowNs+=10*Ms;s.input.observedNs=s.input.nowNs;s.input.deadlineNs=s.input.nowNs+100*Ms;++s.input.sequence;
        s.geometrySequence=s.input.sequence;s.gripPressed=pressed;s.input.released[0]=!pressed;s.intent=++intent;
        ++s.source.sequence;s.source.observedNs=s.input.nowNs;s.source.deadlineNs=s.input.deadlineNs;
    }
    AmmoSupplyResult Tick(){
        hands.Update(s.input);
        if(gun&&gun->token.owner!=s.input.owner)gun.reset();
        if(!gun){auto r=hands.Acquire(s.input,{s.input.owner,InteractionHand::Right,HandClaimKind::GunHold,
            s.source.identity.weapon,{{1002,1},s.input.sequence,s.input.deadlineNs,true},++rightIntent,0});gun=r.claim;}
        else {auto r=hands.Renew(s.input,gun->token,{{1002,1},s.input.sequence,s.input.deadlineNs,true});gun=r.claim;}
        return supply.Update(s,hands);
    }
    AmmoSupplyResult Grab(){Tick();Next(true);return Tick();}
    ReloadInsertionSeat Seat()const {
        const auto& h=*supply.Held();
        return {31,s.input.sequence,{h.identity.owner,h.identity.weapon,h.item,h.identity.trackingEpoch},h.identity.profile,
            h.claim.token,gun->token,h.family==ReloadInsertionFamily::SingleShell?ReloadOperation::InsertRound:ReloadOperation::SeatMagazine};
    }
    ManualReloadRequest Request()const {
        const auto& i=s.source.identity;return {41,{i.owner.actor,i.owner.actorGeneration,i.weapon.id,i.owner.equipGeneration,i.owner.space},
            s.source.family==ReloadInsertionFamily::SingleShell?ReloadOperation::InsertRound:ReloadOperation::SeatMagazine,0,0};
    }
    std::optional<AmmoSupplyReservation> Reserve(){return supply.Reserve(s,hands,Seat(),Request(),12);}
    AmmoSupplyReceipt Receipt(const AmmoSupplyReservation& r,ReloadAcknowledgement status=ReloadAcknowledgement::Applied){
        auto after=s.source;after.identity=r.identity;after.sequence=std::max(after.sequence,r.sourceSequence)+1;
        after.observedNs=s.input.nowNs;after.deadlineNs=s.input.nowNs+100*Ms;
        after.reserveUnits=r.reserveBefore-(status==ReloadAcknowledgement::Applied?r.units:0);
        return {r,{r.request,{r.identity.owner.actor,r.identity.owner.actorGeneration,r.identity.weapon.id,
            r.identity.owner.equipGeneration,r.identity.owner.space},r.operation,status},after,200,s.input.nowNs,s.input.nowNs+100*Ms,true};
    }
};
int RealEdgeBodyPouchAndReserve(){
    Fixture f;
    f.s.gripPressed=true;f.s.input.released[0]=false;f.s.intent=++f.intent;
    CHECK(!f.Tick().held); // startup-held cannot acquire
    f.Next(false);f.Tick();CHECK(!f.Tick().held); // a duplicate neutral preserves existing neutral proof
    f.Next(true);const auto r=f.Tick();CHECK(r.acquired&&r.held&&r.held->units==1&&f.s.source.reserveUnits==4);
    CHECK(r.held->claim.token.kind==HandClaimKind::AmmoObject&&r.held->claim.token.prerequisiteClaim==0);
    const auto item=r.held->item;const auto claim=r.held->claim.token;
    f.Next(true);f.s.bodyFromHand.values[3][0]=100;CHECK(f.Tick().held->item==item); // carried, no pouch tether
    f.Next(false);CHECK(f.Tick().released==item&&!f.supply.Held());CHECK(!f.hands.Current(InteractionHand::Left));
    CHECK(f.hands.Current(InteractionHand::Right)->token==f.gun->token&&claim.id!=f.gun->token.id);
    CHECK(f.s.source.reserveUnits==4);return 0;
}
int AlternateContactSharesProvider(){
    auto c=Config();c.alternateContact=AmmoSupplyContact{{-.25f,-.7f,0},.15f};
    Fixture f(c);f.s.bodyFromHand.values[3][1]=-.7f;const auto first=f.Grab();
    CHECK(first.acquired&&first.held&&first.held->claim.token.contact==c.pouch);
    const auto held=first.held->item;f.Next(true);f.s.bodyFromHand.values[3][1]=-.3f;
    CHECK(!f.Tick().acquired&&f.supply.Held()->item==held); // Moving between contacts never spawns twice.
    f.Next(false);f.Tick();f.Next(true);const auto second=f.Tick();
    CHECK(second.acquired&&second.held->item.id==held.id&&second.held->item.generation>held.generation);
    CHECK(second.held->identity.pool==first.held->identity.pool&&f.s.source.reserveUnits==4);
    return 0;
}
int AlternateContactCannotBypassReservation(){
    auto c=Config();c.alternateContact=AmmoSupplyContact{{-.25f,-.7f,0},.15f};Fixture f(c);
    f.s.source.reserveUnits=1;CHECK(f.Grab().held);const auto pending=f.Reserve();CHECK(pending);
    f.supply.Cancel(f.s.input,f.hands);f.Next(false);f.s.bodyFromHand.values[3][1]=-.7f;f.Tick();
    f.Next(true);CHECK(f.Tick().reason==AmmoSupplyReason::ReserveExhausted);
    CHECK(!f.supply.Held()&&f.supply.Pending()==pending&&f.s.source.reserveUnits==1);
    ++f.s.input.owner.equipGeneration;++f.s.source.identity.owner.equipGeneration;
    ++f.s.source.identity.weapon.generation;f.Next(false);f.Tick();f.Next(true);
    CHECK(f.Tick().reason==AmmoSupplyReason::PendingOtherOwner&&f.supply.Pending()==pending);
    return 0;
}
int AlternateContactsKeepExactGeometryAndEdge(){
    auto c=Config();c.alternateContact=AmmoSupplyContact{{-.25f,-.7f,0},.15f};
    Fixture f(c);f.s.bodyFromHand.values[3][1]=-.5f;CHECK(f.Grab().reason==AmmoSupplyReason::OutsidePouch);
    f.Next(true);f.s.bodyFromHand.values[3][1]=-.7f;CHECK(!f.Tick().held);
    f.Next(false);f.Tick();f.Next(true);CHECK(f.Tick().held);
    const auto claim=f.supply.Held()->claim.token;f.s.input.focused=false;CHECK(!f.Tick().held);
    f.s.input.focused=true;CHECK(!f.Tick().held);f.Next(true);CHECK(!f.Tick().held);
    f.Next(false);f.Tick();f.Next(true);CHECK(f.Tick().held&&f.supply.Held()->claim.token!=claim);
    for(unsigned n=0;n<3;++n){auto invalid=c;
        if(n==0)invalid.alternateContact->radiusMeters=0;
        if(n==1)invalid.alternateContact->radiusMeters=std::numeric_limits<float>::infinity();
        if(n==2)invalid.alternateContact->centerMeters[0]=std::numeric_limits<float>::quiet_NaN();
        Fixture bad(invalid);CHECK(bad.Tick().reason==AmmoSupplyReason::InvalidConfig);}
    return 0;
}
int ContactFailureNeedsNewEdge(){
    for(unsigned mode=0;mode<4;++mode){
        Fixture f;f.Tick();f.Next(true);
        if(mode==0)f.s.bodyFromHand.values[3][0]=10;
        if(mode==1)--f.s.geometrySequence;
        if(mode==2)f.s.bodyFromHand.values[0][0]=2;
        if(mode==3)f.s.source.reserveUnits=0;
        CHECK(!f.Tick().held);
        f.Next(true);f.s.bodyFromHand=reload_insertion_detail::Identity();f.s.bodyFromHand.values[3][0]=-.25f;f.s.bodyFromHand.values[3][1]=-.3f;f.s.source.reserveUnits=4;
        CHECK(!f.Tick().held); // moving into the pouch with held squeeze cannot retry
        f.Next(false);f.Tick();f.Next(true);CHECK(f.Tick().held);
    }return 0;
}
int NoStealingSupportOrSight(){
    for(auto kind:{HandClaimKind::WeaponSupport,HandClaimKind::Sight}){
        Fixture f;f.Tick();f.Next(true);f.hands.Update(f.s.input);
        auto other=f.hands.Acquire(f.s.input,{f.s.input.owner,InteractionHand::Left,kind,f.s.source.identity.weapon,
            {{2001,1},f.s.input.sequence,f.s.input.deadlineNs,true},++f.intent,f.gun->token.id});
        CHECK(other.accepted);f.s.intent=++f.intent;
        CHECK(f.supply.Update(f.s,f.hands).reason==AmmoSupplyReason::HandUnavailable);
        CHECK(f.hands.Current(InteractionHand::Left)->token==other.claim->token);
        f.hands.Release(f.s.input,other.claim->token);f.Next(true);CHECK(!f.Tick().held);
        f.Next(false);f.Tick();f.Next(true);CHECK(f.Tick().held);
    }return 0;
}
int IdentityAndSafetyInvalidation(){
    for(unsigned mode=0;mode<8;++mode){
        Fixture f;CHECK(f.Grab().held);const auto first=f.supply.Held()->item;
        f.Next(true);
        switch(mode){
        case 0:++f.s.input.owner.equipGeneration;++f.s.source.identity.owner.equipGeneration;++f.s.source.identity.weapon.generation;break;
        case 1:++f.s.source.identity.profile.generation;break;
        case 2:++f.s.trackingEpoch;++f.s.source.identity.trackingEpoch;break;
        case 3:f.s.input.tracked[0]=false;break;
        case 4:f.s.input.focused=false;break;
        case 5:f.s.source.verified=false;break;
        case 6:f.s.source.deadlineNs=f.s.input.nowNs;break;
        case 7:++f.s.source.identity.pool.generation;break;
        }
        CHECK(!f.Tick().held&&!f.supply.Held());
        f.Next(true);f.s.input.tracked[0]=f.s.input.focused=f.s.source.verified=true;CHECK(!f.Tick().held);
        f.Next(false);f.Tick();f.Next(true);CHECK(f.Tick().held&&f.supply.Held()->item!=first);
    }return 0;
}
int DuplicateAndStalePackets(){
    Fixture f;CHECK(f.Grab().held);const auto deadline=f.supply.Held()->claim.deadlineNs;
    ++f.s.source.sequence;f.s.source.deadlineNs+=10*Ms; // native update between controller packets
    CHECK(f.Tick().held&&f.supply.Held()->claim.deadlineNs==deadline);
    f.s.input.tracked[0]=false;CHECK(!f.Tick().held);
    f.s.input.tracked[0]=true;CHECK(!f.Tick().held); // cannot restore a cancelled duplicate
    f.Next(true);CHECK(!f.Tick().held);f.Next(false);f.Tick();f.Next(true);CHECK(f.Tick().held);
    f.s.source.reserveUnits=99;CHECK(!f.Tick().held); // changed native count without new source revision
    f.Next(true);CHECK(!f.Tick().held);return 0;
}
int ReservationReplacementAndExactConsumption(){
    Fixture f;CHECK(f.Grab().held);auto a=f.Reserve();CHECK(a&&f.supply.Pending());
    CHECK(!f.Reserve()); // exact seat cannot submit twice
    f.Next(false);f.Tick();f.Next(true);CHECK(f.Tick().held);const auto b=*f.supply.Held();CHECK(b.item!=a->item);
    auto receipt=f.Receipt(*a);CHECK(f.supply.Resolve(f.s.input,f.hands,receipt).consumed==a);
    CHECK(f.supply.Held()->item==b.item&&f.hands.Current(InteractionHand::Left)->token==b.claim.token);
    CHECK(!f.supply.Pending()&&!f.supply.Resolve(f.s.input,f.hands,receipt).accepted);
    // Stale pre-completion count is fenced, despite still being age-fresh.
    CHECK(!f.Tick().held);f.s.source=receipt.currentReserve;
    f.Next(false);f.Tick();f.Next(true);CHECK(f.Tick().held);
    CHECK(f.s.source.reserveUnits==3);return 0;
}
int ExhaustionAndUnresolvedCannotMint(){
    Fixture f;f.s.source.reserveUnits=1;CHECK(f.Grab().held);const auto a=f.Reserve();CHECK(a);
    f.supply.Cancel(f.s.input,f.hands);CHECK(f.supply.Pending()==a&&!f.supply.Held());
    f.Next(false);f.Tick();f.Next(true);CHECK(f.Tick().reason==AmmoSupplyReason::ReserveExhausted);
    // Time passing is not evidence that a submitted native command had no effect.
    f.s.input.nowNs+=10000*Ms;f.Next(false);f.Tick();f.Next(true);CHECK(!f.Tick().held&&f.supply.Pending()==a);
    auto receipt=f.Receipt(*a);CHECK(f.supply.Resolve(f.s.input,f.hands,receipt).accepted);
    f.s.source=receipt.currentReserve;f.Next(false);f.Tick();f.Next(true);CHECK(!f.Tick().held&&f.s.source.reserveUnits==0);
    return 0;
}
int PendingEquipChangeAndWrongReceipts(){
    Fixture f;CHECK(f.Grab().held);auto a=f.Reserve();CHECK(a);
    for(unsigned mode=0;mode<13;++mode){
        auto receipt=f.Receipt(*a);
        switch(mode){
        case 0:receipt.verified=false;break;case 1:++receipt.reservation.item.generation;break;
        case 2:++receipt.reservation.claim.id;break;case 3:++receipt.reservation.request;break;
        case 4:++receipt.acknowledgement.owner.equipGeneration;break;case 5:receipt.nativeEvent=0;break;
        case 6:receipt.currentReserve.sequence=a->sourceSequence;break;
        case 7:receipt.currentReserve.reserveUnits=a->reserveBefore;break;
        case 8:receipt.deadlineNs=f.s.input.nowNs;break;
        case 9:--receipt.observedNs;receipt.observedNs=a->startedNs-1;break;
        case 10:++receipt.currentReserve.identity.trackingEpoch;break;
        case 11:receipt.acknowledgement.status=ReloadAcknowledgement::None;break;
        case 12:++receipt.acknowledgement.request;break;
        }
        CHECK(!f.supply.Resolve(f.s.input,f.hands,receipt).accepted&&f.supply.Pending()==a);
    }
    f.Next(true);++f.s.input.owner.equipGeneration;++f.s.source.identity.owner.equipGeneration;++f.s.source.identity.weapon.generation;
    CHECK(!f.Tick().held);f.Next(false);f.Tick();f.Next(true);CHECK(f.Tick().reason==AmmoSupplyReason::PendingOtherOwner);
    // Old transaction may reconcile after tracking/equip changes, but cannot touch new ownership.
    auto receipt=f.Receipt(*a);CHECK(f.supply.Resolve(f.s.input,f.hands,receipt).accepted&&!f.supply.Pending());
    f.Next(false);f.Tick();f.Next(true);CHECK(f.Tick().held);return 0;
}
int MagazineExplicitUnitsAndNoEffect(){
    Fixture f;f.s.source.family=ReloadInsertionFamily::Magazine;f.s.source.objectUnits=20;f.s.source.reserveUnits=30;
    CHECK(f.Grab().held&&f.supply.Held()->units==20);auto a=f.Reserve();CHECK(a&&a->operation==ReloadOperation::SeatMagazine);
    f.Next(false);f.Tick();f.Next(true);CHECK(!f.Tick().held); // no second 20-round resource from 30 reserve
    auto noEffect=f.Receipt(*a,ReloadAcknowledgement::Rejected);
    auto result=f.supply.Resolve(f.s.input,f.hands,noEffect);CHECK(result.accepted&&!result.consumed&&result.releasedReservation==a);
    f.s.source=noEffect.currentReserve;f.Next(false);f.Tick();f.Next(true);CHECK(f.Tick().held&&f.s.source.reserveUnits==30);
    return 0;
}
int ActualInsertionConsumesSuppliedClaim(){
    Fixture f;CHECK(f.Grab().held);
    ReloadInsertionProfile p{};p.id=6;p.revision=1;p.family=ReloadInsertionFamily::SingleShell;
    p.itemFromHand=p.itemFromInsertion=p.weaponFromEntry=reload_insertion_detail::Identity();
    p.travelMeters=.1f;p.captureDistanceMeters=.03f;p.releaseDistanceMeters=.05f;p.captureAngleRadians=.3f;p.releaseAngleRadians=.6f;
    p.seatToleranceMeters=.001f;p.maxStepMeters=.045f;p.maxStepRadians=.5f;p.alignmentNs=40*Ms;p.seatDwellNs=20*Ms;
    p.maxSampleGapNs=100*Ms;p.maxGuidedNs=2000*Ms;ReloadInsertion insertion(p);
    ReloadInsertionResult r;
    for(float z:{-.06f,-.025f,.01f,.045f,.08f,.1f,.1f,.1f}){
        f.Next(true);CHECK(f.Tick().held);const auto& h=*f.supply.Held();
        ReloadInsertionSample s{{h.identity.owner,h.identity.weapon,h.item,h.identity.trackingEpoch},h.identity.profile,h.claim,*f.gun,
            f.s.input.sequence,f.s.input.sequence,f.s.input.observedNs,f.s.input.deadlineNs,f.s.input.nowNs,true,true,true,true,true,false,
            reload_insertion_detail::Identity()};s.weaponFromHand.values[3][2]=z;r=insertion.Update(s);
    }
    CHECK(r.seat&&r.seat->itemClaim==f.supply.Held()->claim.token);
    auto reserved=f.supply.Reserve(f.s,f.hands,*r.seat,f.Request(),12);CHECK(reserved&&f.s.source.reserveUnits==4);
    CHECK(f.supply.Held()&&f.supply.Pending());return 0;
}
int DeathRebaselineAndLateOldAck(){
    Fixture f;CHECK(f.Grab().held);const auto old=f.Reserve();CHECK(old);
    const auto oldItem=old->item;
    f.Next(true);++f.s.input.owner.actorGeneration;f.s.source.identity.owner=f.s.input.owner;
    ++f.s.source.identity.pool.generation;f.s.source.reserveUnits=5;CHECK(!f.Tick().held);
    AmmoSupplyRebaseline rebase{*old,f.s.source,1,f.s.input.nowNs,f.s.input.deadlineNs,true};
    const auto retired=f.supply.Rebaseline(f.s.input,f.hands,rebase);
    CHECK(retired.accepted&&!retired.consumed&&retired.releasedReservation==old&&!f.supply.Pending());
    f.Next(true);CHECK(!f.Tick().held);f.Next(false);f.Tick();f.Next(true);CHECK(f.Tick().held);
    const auto newer=f.supply.Held()->item;CHECK(newer.generation>oldItem.generation);
    auto seat=f.Seat();++seat.id;auto request=f.Request();++request.id;
    const auto next=f.supply.Reserve(f.s,f.hands,seat,request,13);CHECK(next&&next->item==newer);
    CHECK(!f.supply.Resolve(f.s.input,f.hands,f.Receipt(*old)).accepted);
    CHECK(!f.supply.Rebaseline(f.s.input,f.hands,rebase).accepted);
    CHECK(f.supply.Pending()==next&&f.supply.Held()->item==newer);return 0;
}
int SameOwnerNeedsDrainAndSettledCount(){
    Fixture f;f.s.source.reserveUnits=1;CHECK(f.Grab().held);const auto old=f.Reserve();CHECK(old);
    f.supply.Cancel(f.s.input,f.hands);f.Next(false);f.Tick();
    auto baseline=f.s.source;baseline.reserveUnits=0;++baseline.sequence;
    AmmoSupplyRebaseline proof{*old,baseline,1,f.s.input.nowNs,f.s.input.deadlineNs,true};
    for(unsigned mode=0;mode<6;++mode){
        auto bad=proof;
        switch(mode){
        case 0:bad.verifiedNativeCycleDrained=false;break;
        case 1:bad.retirement=0;break;
        case 2:++bad.reservation.cycle;break;
        case 3:bad.currentReserve.observedNs=proof.observedNs-1;break;
        case 4:bad.deadlineNs=f.s.input.nowNs;break;
        case 5:bad.currentReserve.sequence=old->sourceSequence;break;
        }
        CHECK(!f.supply.Rebaseline(f.s.input,f.hands,bad).accepted&&f.supply.Pending()==old);
    }
    const auto reconciled=f.supply.Rebaseline(f.s.input,f.hands,proof);
    CHECK(reconciled.accepted&&!reconciled.consumed&&!f.supply.Pending());
    f.s.source=baseline;f.Next(false);f.Tick();f.Next(true);CHECK(!f.Tick().held&&f.s.source.reserveUnits==0);
    // Reconnect can retire old tracking identity using the same proved boundary.
    Fixture g;CHECK(g.Grab().held);auto p=g.Reserve();CHECK(p);
    g.Next(true);++g.s.trackingEpoch;++g.s.source.identity.trackingEpoch;CHECK(!g.Tick().held);
    const AmmoSupplyRebaseline reconnect{*p,g.s.source,1,g.s.input.nowNs,g.s.input.deadlineNs,true};
    CHECK(g.supply.Rebaseline(g.s.input,g.hands,reconnect).accepted);
    g.Next(false);g.Tick();g.Next(true);CHECK(g.Tick().held);return 0;
}
int NativeBaselinePreservesReplacementAndSilenceDisarms(){
    Fixture f;CHECK(f.Grab().held);auto p=f.Reserve();CHECK(p);
    f.Next(false);f.Tick();f.Next(true);CHECK(f.Tick().held);const auto replacement=*f.supply.Held();
    auto baseline=f.s.source;--baseline.reserveUnits;++baseline.sequence;
    const AmmoSupplyRebaseline proof{*p,baseline,1,f.s.input.nowNs,f.s.input.deadlineNs,true};
    auto result=f.supply.Rebaseline(f.s.input,f.hands,proof);CHECK(result.accepted&&!result.consumed);
    CHECK(f.supply.Held()->item==replacement.item&&f.hands.Current(InteractionHand::Left)->token==replacement.claim.token);
    Fixture g;g.Tick();g.s.input.nowNs+=1000*Ms;g.Next(true);CHECK(!g.Tick().held);
    g.Next(false);g.Tick();g.Next(true);CHECK(g.Tick().held);return 0;
}
int RetainedPreviousPacketSeat(){
    Fixture f;CHECK(f.Grab().held);auto original=f.s;const auto seat=f.Seat();const auto request=f.Request();
    const auto oldLease=f.supply.Held()->claim.deadlineNs;
    f.Next(true);f.s.source.reserveUnits=3;CHECK(f.Tick().held);
    const auto currentLease=f.supply.Held()->claim.deadlineNs;CHECK(currentLease>oldLease);
    original.input.nowNs=f.s.input.nowNs; // processing time is not restamped observation evidence
    const auto reserved=f.supply.ReserveFrom(f.s,f.hands,original,seat,request,12);
    CHECK(reserved&&reserved->reserveBefore==3&&reserved->sourceSequence==f.s.source.sequence&&
        reserved->sourceSequence>original.source.sequence&&reserved->claim==seat.itemClaim);
    CHECK(f.supply.Held()->claim.inputSequence==f.s.input.sequence&&f.supply.Held()->claim.deadlineNs==currentLease);
    CHECK(original.input.deadlineNs==oldLease);return 0;
}
int PreviousPacketCannotBeRestampedOrReplaceClaims(){
    for(unsigned mode=0;mode<10;++mode){
        Fixture f;CHECK(f.Grab().held);auto original=f.s;auto seat=f.Seat();const auto request=f.Request();
        f.Next(true);CHECK(f.Tick().held);
        switch(mode){
        case 0:++original.input.observedNs;break;
        case 1:++original.input.deadlineNs;break;
        case 2:original.geometrySequence=f.s.input.sequence;break;
        case 3:original.bodyFromHand.values[3][0]+=.01f;break;
        case 4:++original.source.sequence;break;
        case 5:++seat.itemClaim.id;break;
        case 6:f.Next(false);f.Tick();f.Next(true);CHECK(f.Tick().held);break;
        case 7:f.hands.Release(f.s.input,f.gun->token);f.gun.reset();CHECK(f.Tick().held);break;
        case 8:f.s.input.nowNs=original.input.deadlineNs-10*Ms;f.Next(true);CHECK(f.Tick().held);break;
        case 9:++original.intent;break;
        }
        const auto held=f.supply.Held()->claim.token;
        CHECK(!f.supply.ReserveFrom(f.s,f.hands,original,seat,request,12));
        CHECK(f.supply.Held()&&f.supply.Held()->claim.token==held&&!f.supply.Pending());
    }return 0;
}
int PhysicalCarrySurvivesFreshShortSourceObservations(){
 for(auto family:{ReloadInsertionFamily::SingleShell,ReloadInsertionFamily::Magazine}){
  Fixture f;f.s.source.family=family;f.s.source.objectUnits=family==ReloadInsertionFamily::SingleShell?1:3;
  f.Tick();f.Next(true);f.s.source.deadlineNs=f.s.input.nowNs+5*Ms;
  auto r=f.Tick();CHECK(r.acquired&&r.held);const auto item=r.held->item;const auto claim=r.held->claim.token;
  const auto original=f.s;const auto seat=f.Seat();const auto request=f.Request();
  f.Next(true);f.s.source.deadlineNs=f.s.input.nowNs+5*Ms;
  r=f.Tick();CHECK(f.s.input.nowNs>original.source.deadlineNs&&r.held&&r.held->item==item&&r.held->claim.token==claim&&!r.acquired);
  CHECK(r.held->claim.deadlineNs==f.s.input.deadlineNs&&r.held->units==f.s.source.objectUnits);
  // Expired original source cannot authorize an N-1 seat even though the
  // same physical object and its current source remain valid.
  CHECK(!f.supply.ReserveFrom(f.s,f.hands,original,seat,request,12)&&!f.supply.Pending());
  const auto deadline=r.held->claim.deadlineNs;
  ++f.s.input.nowNs;++f.s.source.sequence;f.s.source.observedNs=f.s.input.nowNs;f.s.source.deadlineNs=f.s.input.nowNs+5*Ms;
  r=f.Tick();CHECK(r.held&&r.held->claim.token==claim&&r.held->claim.deadlineNs==deadline&&!r.acquired);
  f.s.input.nowNs=f.s.source.deadlineNs;r=f.Tick();CHECK(!r.held&&r.reason==AmmoSupplyReason::InvalidSource);
  CHECK(!f.hands.Current(InteractionHand::Left)&&!f.supply.Pending()&&f.s.source.reserveUnits==4);
 }
 return 0;
}
int FreshSourceNeverRevivesExpiredPhysicalClaim(){
 Fixture f;CHECK(f.Grab().held);const auto held=*f.supply.Held();
 CHECK(f.hands.Renew(f.s.input,held.claim.token,{held.claim.token.contact,f.s.input.sequence,f.s.input.nowNs+5*Ms,true}).accepted);
 f.Next(true);const auto r=f.Tick();CHECK(!r.held&&r.reason==AmmoSupplyReason::ClaimLost&&!f.hands.Current(InteractionHand::Left));
 CHECK(!f.supply.Pending()&&f.s.source.reserveUnits==4);return 0;
}
}
int ReleaseSubmittedPreservesPendingResourceAndOtherClaims(){
 Fixture f;CHECK(f.Grab().held);const auto reservation=f.Reserve();CHECK(reservation);
 auto wrong=*reservation;++wrong.request;
 CHECK(!f.supply.ReleaseSubmitted(f.s.input,f.hands,wrong)&&f.supply.Held());
 CHECK(f.supply.ReleaseSubmitted(f.s.input,f.hands,*reservation));
 CHECK(!f.supply.Held()&&!f.hands.Current(InteractionHand::Left)&&f.supply.Pending()==reservation);
 CHECK(f.s.source.reserveUnits==4&&!f.supply.ReleaseSubmitted(f.s.input,f.hands,*reservation));
 f.Next(true);f.Tick();CHECK(!f.supply.Held());
 f.Next(false);f.Tick();f.Next(true);f.Tick();CHECK(f.supply.Held());
 const auto replacement=f.supply.Held()->claim.token;
 CHECK(!f.supply.ReleaseSubmitted(f.s.input,f.hands,*reservation));
 CHECK(f.hands.Current(InteractionHand::Left)->token==replacement);
 const auto receipt=f.Receipt(*reservation);
 CHECK(f.supply.Resolve(f.s.input,f.hands,receipt).consumed==reservation);
 CHECK(f.hands.Current(InteractionHand::Left)->token==replacement);return 0;
}
int TerminalOutcomeDoesNotRenewResource(){
 for(bool applied:{false,true})for(unsigned bad=0;bad<8;++bad){Fixture f;CHECK(f.Grab().held);const auto reservation=f.Reserve();CHECK(reservation);
    CHECK(f.supply.ReleaseSubmitted(f.s.input,f.hands,*reservation));
    const auto old=f.Receipt(*reservation,applied?ReloadAcknowledgement::Applied:ReloadAcknowledgement::Rejected);
    AmmoSupplyTerminalReceipt outcome{*reservation,old.acknowledgement,200,f.s.input.nowNs,4,applied?3u:4u,true};
    f.s.input.nowNs+=500*Ms;f.supply.Cancel(f.s.input,f.hands);
    if(bad==1)outcome.nativeFinalVerified=false;if(bad==2)++outcome.reservation.request;
    if(bad==3)++outcome.acknowledgement.owner.equipGeneration;if(bad==4)outcome.completedNs=f.s.input.nowNs+1;
    if(bad==5)++outcome.reserveAfter;if(bad==6)outcome.event=0;if(bad==7)outcome.acknowledgement.status=ReloadAcknowledgement::None;
    const auto result=f.supply.SettleTerminal(f.s.input,f.hands,outcome);
    CHECK(result.accepted==(bad==0));CHECK(bool(f.supply.Pending())==(bad!=0));
    if(bad)continue;
    CHECK(bool(result.consumed)==applied);CHECK(!f.supply.SettleTerminal(f.s.input,f.hands,outcome).accepted);
    CHECK(!f.supply.Held());f.Next(false);f.s.source.reserveUnits=applied?3:4;CHECK(!f.Tick().held);
    f.Next(true);CHECK(f.Tick().held);
 }return 0;
}
int main(){CHECK(TerminalOutcomeDoesNotRenewResource()==0);CHECK(ReleaseSubmittedPreservesPendingResourceAndOtherClaims()==0);if(PhysicalCarrySurvivesFreshShortSourceObservations()||FreshSourceNeverRevivesExpiredPhysicalClaim())return 1;
    if(AlternateContactSharesProvider()||AlternateContactCannotBypassReservation()||AlternateContactsKeepExactGeometryAndEdge()||RealEdgeBodyPouchAndReserve()||ContactFailureNeedsNewEdge()||NoStealingSupportOrSight()||IdentityAndSafetyInvalidation()||
        DuplicateAndStalePackets()||ReservationReplacementAndExactConsumption()||ExhaustionAndUnresolvedCannotMint()||
        PendingEquipChangeAndWrongReceipts()||MagazineExplicitUnitsAndNoEffect()||ActualInsertionConsumesSuppliedClaim()||
        DeathRebaselineAndLateOldAck()||SameOwnerNeedsDrainAndSettledCount()||NativeBaselinePreservesReplacementAndSilenceDisarms()||
        RetainedPreviousPacketSeat()||PreviousPacketCannotBeRestampedOrReplaceClaims())return 1;
    std::puts("Eighteen ammo supply groups passed, including retained N-1 evidence and current safety/resource ownership; no native calls or count writes.");return 0;
}
