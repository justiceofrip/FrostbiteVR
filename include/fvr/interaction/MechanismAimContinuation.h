#pragma once
#include "ControllerInput.h"
#include "HandInteraction.h"
#include <algorithm>
#include <cmath>

namespace fvr::interaction {
// Presentation continuity across an explicit support -> mechanism exchange.
// This owns no hand, gesture progress, ammunition or native readiness. The
// caller supplies the arbiter's actual current claims after Update/Transfer.
class MechanismAimContinuation {
public:
    bool Capture(const InputFrame& raw,const InputFrame& guided,const HandInteractionSample& safety,
        const HandClaim& support,const HandClaim& gun)noexcept {
        Reset();
        if(!Current(raw,safety)||!ValidInput(guided)||!SameExceptOrientation(raw,guided)||
           !Pair(safety,support,gun,HandClaimKind::WeaponSupport))return false;
        grip_=Product(Inverse(Unit(raw.hands[1].grip.orientation)),Unit(guided.hands[1].grip.orientation));
        aim_=Product(Inverse(Unit(raw.hands[1].aim.orientation)),Unit(guided.hands[1].aim.orientation));
        support_=support.token;supportInput_=support.inputSequence;gun_=gun.token;last_=safety;reference_=raw.referenceHead;
        units_=raw.worldUnitsPerMeter;predicted_=raw.predictedNs;
        seedDeadline_=std::min({safety.deadlineNs,support.deadlineNs,gun.deadlineNs});seeded_=true;return true;
    }
    std::optional<InputFrame> Apply(const InputFrame& raw,const HandInteractionSample& safety,
        const HandClaim& mechanism,const HandClaim& gun)noexcept {
        const auto reject=[&]()->std::optional<InputFrame>{Reset();return {};};
        if(returnAt_||!seeded_||!Current(raw,safety)||!Pair(safety,mechanism,gun,HandClaimKind::Mechanism)||
           gun.token!=gun_||safety.owner!=last_.owner||raw.worldUnitsPerMeter!=units_||!Pose(raw.referenceHead,reference_)||
           safety.nowNs<last_.nowNs||safety.sequence<last_.sequence||safety.observedNs<last_.observedNs||
           raw.predictedNs<predicted_||
           (safety.sequence==last_.sequence&&(safety.observedNs!=last_.observedNs||safety.deadlineNs!=last_.deadlineNs||raw.predictedNs!=predicted_)))return reject();
        if(!mechanism_){
            // The original support seed admits this one new companion only
            // while its own evidence remains fresh. A later unrelated squeeze
            // cannot revive the old mapping after the support hand let go.
            if(seedDeadline_<=safety.nowNs||mechanism.token.id<=support_.id||
               mechanism.inputSequence<supportInput_)return reject();
            mechanism_=mechanism.token;
        }else if(mechanism.token!=*mechanism_)return reject();
        auto out=raw;
        // Right-local correction follows fresh controller rotation. A frozen
        // world quaternion would strand aiming when the player turns the gun.
        out.hands[1].grip.orientation=Unit(Product(Unit(raw.hands[1].grip.orientation),grip_));
        out.hands[1].aim.orientation=Unit(Product(Unit(raw.hands[1].aim.orientation),aim_));
        last_=safety;predicted_=raw.predictedNs;return out;
    }
    // Adapter-observed physical completion leaves a short gap before fresh
    // rendered contact can acquire support. Retain only the aiming transform
    // during that gap; this supplies no hand claim, contact or native outcome.
    // submittedNs must come from the same physical consumer's pending return.
    std::optional<InputFrame> ApplyReturning(const InputFrame& raw,const HandInteractionSample& safety,
        const HandClaim& gun,std::int64_t submittedNs)noexcept {
        const auto reject=[&]()->std::optional<InputFrame>{Reset();return {};};
        if(!seeded_||!mechanism_||!Current(raw,safety)||gun.token!=gun_||gun.token.owner!=safety.owner||
           gun.inputSequence!=safety.sequence||gun.deadlineNs<=safety.nowNs||gun.deadlineNs>safety.deadlineNs||
           safety.owner!=last_.owner||raw.worldUnitsPerMeter!=units_||!Pose(raw.referenceHead,reference_)||
           safety.nowNs<last_.nowNs||safety.nowNs>=last_.deadlineNs||safety.sequence<last_.sequence||
           safety.observedNs<last_.observedNs||raw.predictedNs<predicted_||
           (safety.sequence==last_.sequence&&(safety.observedNs!=last_.observedNs||safety.deadlineNs!=last_.deadlineNs||raw.predictedNs!=predicted_))||
           submittedNs<=0||submittedNs>safety.nowNs||safety.nowNs-submittedNs>200000000||
           (returnAt_?submittedNs!=returnAt_:submittedNs<last_.observedNs))return reject();
        returnAt_=submittedNs;
        auto out=raw;
        out.hands[1].grip.orientation=Unit(Product(Unit(raw.hands[1].grip.orientation),grip_));
        out.hands[1].aim.orientation=Unit(Product(Unit(raw.hands[1].aim.orientation),aim_));
        last_=safety;predicted_=raw.predictedNs;return out;
    }
    void Reset()noexcept{seeded_=false;mechanism_.reset();support_={};gun_={};last_={};supportInput_=0;seedDeadline_=0;predicted_=0;returnAt_=0;}
    bool Active()const noexcept{return mechanism_.has_value();}
private:
    static math::Quaternion Product(math::Quaternion a,math::Quaternion b)noexcept {
        return {a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,
            a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w,a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z};
    }
    static math::Quaternion Inverse(math::Quaternion q)noexcept{return {-q.x,-q.y,-q.z,q.w};}
    static math::Quaternion Unit(math::Quaternion q)noexcept {
        const float n=std::sqrt(q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w);return {q.x/n,q.y/n,q.z/n,q.w/n};
    }
    static bool Vec(math::Vec3 a,math::Vec3 b)noexcept{return a.x==b.x&&a.y==b.y&&a.z==b.z;}
    static bool Quat(math::Quaternion a,math::Quaternion b)noexcept{return a.x==b.x&&a.y==b.y&&a.z==b.z&&a.w==b.w;}
    static bool Pose(const math::Pose& a,const math::Pose& b)noexcept{return Vec(a.position,b.position)&&Quat(a.orientation,b.orientation);}
    static bool Hand(const ControllerState& a,const ControllerState& b,bool orientations)noexcept {
        return Vec(a.grip.position,b.grip.position)&&Vec(a.aim.position,b.aim.position)&&
            (!orientations||(Quat(a.grip.orientation,b.grip.orientation)&&Quat(a.aim.orientation,b.aim.orientation)))&&
            a.gripTracked==b.gripTracked&&a.aimTracked==b.aimTracked&&a.active==b.active&&a.held==b.held&&
            a.touchActive==b.touchActive&&a.touched==b.touched&&a.stickX==b.stickX&&a.stickY==b.stickY&&a.trigger==b.trigger&&a.squeeze==b.squeeze;
    }
    static bool SameExceptOrientation(const InputFrame& a,const InputFrame& b)noexcept {
        return a.generation==b.generation&&a.spaceGeneration==b.spaceGeneration&&a.predictedNs==b.predictedNs&&
            a.focused==b.focused&&a.headValid==b.headValid&&a.floorRelative==b.floorRelative&&a.worldUnitsPerMeter==b.worldUnitsPerMeter&&
            Pose(a.referenceHead,b.referenceHead)&&Pose(a.head,b.head)&&Hand(a.hands[0],b.hands[0],true)&&Hand(a.hands[1],b.hands[1],false);
    }
    static bool Current(const InputFrame& input,const HandInteractionSample& s)noexcept {
        return ValidInput(input)&&s.owner.actor&&s.owner.actorGeneration&&s.owner.equipGeneration&&s.owner.space&&
            s.sequence==input.generation&&s.owner.space==input.spaceGeneration&&s.observedNs>0&&s.observedNs<=s.nowNs&&
            s.deadlineNs>s.nowNs&&s.deadlineNs-s.observedNs<=150000000&&s.focused&&s.tracked[0]&&s.tracked[1]&&
            !s.released[0]&&!s.released[1]&&input.focused&&input.headValid&&input.hands[0].gripTracked&&input.hands[1].gripTracked&&
            input.hands[1].aimTracked&&(input.hands[0].active&Squeeze)&&(input.hands[1].active&Squeeze)&&
            input.hands[0].squeeze>.35f;
    }
    static bool Pair(const HandInteractionSample& s,const HandClaim& left,const HandClaim& gun,HandClaimKind kind)noexcept {
        const auto claim=[&](const HandClaim& c){return c.token.id&&c.token.owner==s.owner&&c.token.item.id&&c.token.item.generation&&
            c.inputSequence&&c.inputSequence<=s.sequence&&c.deadlineNs>s.nowNs&&c.deadlineNs<=s.deadlineNs;};
        return claim(left)&&claim(gun)&&left.token.hand==InteractionHand::Left&&left.token.kind==kind&&
            gun.token.hand==InteractionHand::Right&&gun.token.kind==HandClaimKind::GunHold&&gun.inputSequence==s.sequence&&
            left.token.item==gun.token.item&&left.token.prerequisiteClaim==gun.token.id;
    }
    HandClaimToken support_{},gun_{};std::optional<HandClaimToken> mechanism_;
    HandInteractionSample last_{};math::Pose reference_{};math::Quaternion grip_{},aim_{};
    std::uint64_t supportInput_=0;std::int64_t seedDeadline_=0,predicted_=0,returnAt_=0;float units_=0;bool seeded_=false;
};
}
