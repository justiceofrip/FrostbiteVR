#include "Bc2PhysicalBolt.h"
namespace fvr::bc2 {
using namespace interaction;
namespace {
bool Mapped(const ReloadStateOwner& n,const HandInteractionSample& s,HandInteractionKey item)noexcept {
    return n.player>=0x10000&&n.soldier>=0x10000&&n.weak>=0x10000&&n.weapon>=0x10000&&n.equipGeneration&&
        n.actorGeneration==s.owner.actorGeneration&&n.space==s.owner.space&&
        s.owner.actor==((std::uint64_t(n.weak)<<32)|n.soldier)&&item.id==n.weapon&&item.generation==s.owner.equipGeneration;
}
bool CurrentGun(const HandClaim& gun,const HandInteractionSample& input,HandInteractionKey item)noexcept {
    return gun.token.kind==HandClaimKind::GunHold&&gun.token.owner==input.owner&&gun.token.item==item&&
        gun.inputSequence==input.sequence&&gun.deadlineNs>input.nowNs&&gun.deadlineNs<=input.deadlineNs;
}
}
bool BoltCalibrationValid(const Bc2BoltCalibration& c)noexcept {
    return c.nativeJoined&&!c.asset.empty()&&!c.mesh.empty()&&!c.partName.empty()&&c.rigFingerprint&&
        weapon_cycle_detail::Profile(c.profile)&&c.profile.family==WeaponCycleFamily::Bolt&&
        feed_mechanism_detail::Pose(c.wristFromPart);
}
Bc2PhysicalBolt::Bc2PhysicalBolt(std::shared_ptr<const Bc2BoltCalibration> c,Bc2PhysicalBoltApi api)noexcept:
    calibration_(std::move(c)),api_(api){}
void Bc2PhysicalBolt::Cancel(const HandInteractionSample& input,HandInteraction& hands)noexcept {
    if(api_.cancel)api_.cancel(api_.context);
    hands.Update(input);blocks_=blocks_||bool(cycle_)||bool(pending_);
    if(cycle_){BoltCustodySample loss;loss.source=input;loss.lease=*cycle_;custody_.Update(loss,hands);}
}
Bc2PhysicalBoltResult Bc2PhysicalBolt::Tick(const Bc2PhysicalBoltSample& s,HandInteraction& hands,std::uint64_t& intent)noexcept {
    Bc2PhysicalBoltResult out;
    if(!calibration_||!BoltCalibrationValid(*calibration_)||!api_.control||!api_.view||!api_.ack||!api_.cancel)return out;
    const auto& p=calibration_->profile;
    if(s.asset==calibration_->asset&&s.mesh==calibration_->mesh&&Mapped(s.nativeOwner,s.input,s.item)&&
       s.input.focused&&s.input.tracked[0]&&s.input.tracked[1]&&
       weapon_cycle_detail::Window(s.input.observedNs,s.input.deadlineNs,s.input.nowNs))
        out.tracking={true,s.nativeOwner,s.input,calibration_};
    auto view=api_.view(api_.context,s.input.nowNs);
    // A ready receipt settles only its exact previously submitted transaction.
    // It remains consumable after the physical owner or tracking has changed.
    if(view&&view->ready){
        if(!acknowledged_&&custody_.ReconcileReady(*view->ready,s.input.nowNs))acknowledged_=view->ready;
        if(acknowledged_&&api_.ack(api_.context,*acknowledged_)){
            out.settled=acknowledged_;acknowledged_.reset();pending_.reset();settled_=true;
        }
        view=api_.view(api_.context,s.input.nowNs);
    }
    const auto existingGun=custody_.Gun();
    const auto gunHand=existingGun?existingGun->hand:p.hands.mechanism;
    const bool releaseExchange=s.custodyTransfer&&s.custodyTransfer->releaseDepartingGun&&
        (!existingGun||custody_.Custody()==BoltCustodyPhase::AwaitingCustody||
         (!cycle_&&custody_.Custody()==BoltCustodyPhase::Returned))&&view&&view->held&&
        view->native.owner==s.nativeOwner&&view->held->owner==s.input.owner&&view->held->item==s.item&&
        view->held->mechanism==HandInteractionKey{p.id,s.item.generation}&&
        weapon_cycle_detail::Window(view->held->observedNs,view->held->deadlineNs,s.input.nowNs)&&
        s.input.released[weapon_cycle_detail::HandIndex(p.hands.mechanism)]&&
        !s.input.released[weapon_cycle_detail::HandIndex(p.hands.gun)];
    const bool safe=!s.cancel&&s.asset==calibration_->asset&&s.mesh==calibration_->mesh&&Mapped(s.nativeOwner,s.input,s.item)&&
        (!(cycle_||existingGun)||s.nativeOwner==owner_)&&s.input.focused&&s.input.tracked[0]&&s.input.tracked[1]&&
        (!s.input.released[weapon_cycle_detail::HandIndex(gunHand)]||releaseExchange)&&
        weapon_cycle_detail::Window(s.input.observedNs,s.input.deadlineNs,s.input.nowNs)&&
        feed_mechanism_detail::Pose(s.wristWorld[weapon_cycle_detail::HandIndex(gunHand)]);
    if(!safe){
        api_.cancel(api_.context);hands.Update(s.input);
        if(cycle_){BoltCustodySample loss;loss.source=s.input;loss.lease=*cycle_;custody_.Update(loss,hands);}
        blocks_=blocks_||bool(cycle_)||bool(pending_)||(view&&view->blocksFire);
        out.blocksFire=blocks_;out.ownsGunCustody=bool(existingGun);return out;
    }
    // The initial ordinary gun claim must already be current. Thereafter the
    // custody coordinator renews its own hold, before any dependent mechanism.
    const auto ordinary=hands.Current(gunHand);
    const bool departing=releaseExchange&&ordinary&&ordinary->token==s.custodyTransfer->gun&&
        ordinary->token.owner==s.input.owner&&ordinary->token.item==s.item&&ordinary->inputSequence<s.input.sequence&&
        ordinary->deadlineNs>s.input.nowNs;
    if(!existingGun&&(!ordinary||(!CurrentGun(*ordinary,s.input,s.item)&&!departing))){
        blocks_=blocks_||bool(cycle_)||(view&&view->blocksFire);out.blocksFire=blocks_;return out;
    }
    Bc2NativeCycleControl control{s.nativeOwner,s.input,s.item,{p.id,s.item.generation},{},true};
    if(!api_.control(api_.context,control)){
        blocks_=true;out.blocksFire=true;out.ownsGunCustody=bool(existingGun);hands.Update(s.input);return out;
    }
    view=api_.view(api_.context,s.input.nowNs);
    out.tracking={true,s.nativeOwner,s.input,calibration_};
    if(view&&view->held&&(view->native.owner!=s.nativeOwner||view->held->owner!=s.input.owner||
       view->held->item!=s.item||view->held->mechanism!=control.mechanism)){
        api_.cancel(api_.context);blocks_=true;out.blocksFire=true;out.tracking.enabled=false;return out;
    }
    // Keep a completed returned attachment until the next explicit custody
    // exchange. Merely observing the next held shot cannot move the gun back
    // to an unrelated calibrated-rig attachment for an intervening frame.
    if(!cycle_&&view&&view->held&&(!existingGun||s.custodyTransfer)){
        if(!custody_.Arm(p,*view->held,calibration_->wristFromPart,s.input.nowNs)){
            blocks_=true;out.blocksFire=true;return out;
        }
        cycle_=view->held;owner_=s.nativeOwner;
    }
    if(!cycle_){
        blocks_=!view||view->blocksFire;
        if(existingGun&&custody_.Custody()==BoltCustodyPhase::Returned){
            const auto hand=weapon_cycle_detail::HandIndex(existingGun->hand);
            const auto contact=s.gunContacts[hand].eligible?s.gunContacts[hand]:s.gunContact;
            const auto retained=custody_.MaintainReturned(s.input,s.wristWorld[hand],contact,hands);
            out.tracking.weapon=retained.weapon;out.tracking.custody=retained.custody;
            out.tracking.mechanismPhase=retained.physical.cycle.phase;
            out.tracking.gun=hands.Current(existingGun->hand);
            out.ownsGunCustody=bool(retained.weapon);blocks_=blocks_||retained.blocksFire;
        }
        out.blocksFire=blocks_;return out;
    }
    if(view&&view->held&&!weapon_cycle_detail::Same(*cycle_,*view->held)){
        api_.cancel(api_.context);blocks_=true;out.blocksFire=true;out.ownsGunCustody=bool(existingGun);return out;
    }
    if(s.custodyTransfer){
        if(custody_.Custody()==BoltCustodyPhase::AwaitingCustody)
            out.custodyChange=custody_.EnterCustody(s.input,*s.custodyTransfer,s.weaponWorld,
                s.wristWorld[weapon_cycle_detail::HandIndex(p.hands.gun)],hands);
        else if(custody_.Custody()==BoltCustodyPhase::Manipulating)
            out.custodyChange=custody_.ReturnCustody(s.input,*s.custodyTransfer,
                s.wristWorld[weapon_cycle_detail::HandIndex(p.hands.gun)],
                s.wristWorld[weapon_cycle_detail::HandIndex(p.hands.mechanism)],hands);
    }
    const auto& raw=s.raw;
    const bool rawOkay=raw.valid&&raw.nativeOwner==s.nativeOwner&&raw.rig.soldier==s.nativeOwner.soldier&&raw.rig.weak==s.nativeOwner.weak&&
        raw.rigFingerprint==calibration_->rigFingerprint&&raw.input.owner==s.input.owner&&raw.input.sequence<=s.input.sequence&&
        raw.input.observedNs<=s.input.observedNs&&weapon_cycle_detail::Window(raw.input.observedNs,raw.input.deadlineNs,s.input.nowNs)&&
        feed_mechanism_detail::Pose(raw.mechanismWristInWeapon);
    BoltCustodySample sample;sample.source=s.input;sample.lease=view&&view->held?*view->held:*cycle_;
    sample.mechanismGrip=s.grip;sample.gunContact=s.gunContact;
    const auto currentGun=custody_.Gun();
    if(currentGun&&s.gunContacts[weapon_cycle_detail::HandIndex(currentGun->hand)].eligible)
        sample.gunContact=s.gunContacts[weapon_cycle_detail::HandIndex(currentGun->hand)];
    sample.gunWristInWorld=s.wristWorld[weapon_cycle_detail::HandIndex(currentGun?currentGun->hand:p.hands.mechanism)];
    if(rawOkay){sample.contactSource=raw.input;sample.mechanismWristInWeapon=raw.mechanismWristInWeapon;
        sample.mechanismContact={sample.lease.mechanism,raw.input.sequence,raw.input.deadlineNs,true};}
    if(intent!=UINT64_MAX)sample.acquireIntent=++intent;
    const auto result=custody_.Update(sample,hands);
    out.tracking.held=sample.lease;out.tracking.target=result.physical.target;out.tracking.weapon=result.weapon;
    out.tracking.custody=result.custody;
    out.tracking.mechanismPhase=result.physical.cycle.phase;
    out.tracking.mechanism=hands.Current(p.hands.mechanism);
    out.tracking.gun=currentGun?hands.Current(currentGun->hand):std::nullopt;
    out.ownsMechanism=result.physical.ownsMechanism;
    out.ownsGunCustody=result.custody==BoltCustodyPhase::Manipulating||result.custody==BoltCustodyPhase::Returned;
    blocks_=!view||view->blocksFire||result.blocksFire||bool(pending_)||bool(acknowledged_);out.blocksFire=blocks_;
    if(result.physical.release){
        pending_=result.physical.release;control.release=pending_;
        // Failure is ambiguous, not proof of non-dispatch. Keep the receipt and
        // do not reissue it or fabricate RejectNative on a later frame.
        api_.control(api_.context,control);out.tracking.target.reset();out.ownsMechanism=false;
    }
    // Publish the actual returned custody/current gun for the completion tick
    // before retiring the cycle. Consumers must observe this concrete handoff.
    if(settled_&&result.custody==BoltCustodyPhase::Returned){cycle_.reset();settled_=false;}
    return out;
}
}
