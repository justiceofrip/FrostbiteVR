#include "fvr/interaction/ControllerInput.h"
#include <cmath>
#include <algorithm>
namespace fvr::interaction {
namespace {
bool PoseOkay(const math::Pose& p) noexcept {return bool(math::MakeRelativePose(p,p));}
bool Axis(float f,float lo,float hi) noexcept{return std::isfinite(f)&&f>=lo&&f<=hi;}
}
bool ValidInput(const InputFrame& f) noexcept {
    if(!f.generation||!f.spaceGeneration||f.predictedNs<=0||!Axis(f.worldUnitsPerMeter,.01f,1000.f))return false;
    if(f.headValid&&(!PoseOkay(f.referenceHead)||!PoseOkay(f.head)))return false;
    for(const auto& h:f.hands){
        if((h.touchActive&~TouchComponents)||(h.touched&~h.touchActive))return false;
        if((h.active&~Components)||(h.held&~Buttons)||(h.held&~h.active))return false;
        if(!Axis(h.stickX,-1,1)||!Axis(h.stickY,-1,1)||!Axis(h.trigger,0,1)||!Axis(h.squeeze,0,1))return false;
        if(!(h.active&Stick)&&(h.stickX||h.stickY))return false;
        if(!(h.active&Trigger)&&h.trigger)return false;
        if(!(h.active&Squeeze)&&h.squeeze)return false;
        if((h.gripTracked&&!PoseOkay(h.grip))||(h.aimTracked&&!PoseOkay(h.aim)))return false;
        if(!f.focused&&(h.active||h.held||h.touchActive||h.touched||h.gripTracked||h.aimTracked))return false;
    }
    return true;
}
ActionOutput ControllerActions::Update(const InputFrame& f,const InputOwner& owner,std::int64_t now) noexcept {
    ActionSample left{};left.owner=owner.owner;left.generation=f.generation;left.frameNs=now;left.timeNs=f.predictedNs;
    left.playing=owner.playing;left.bindingsVerified=owner.bindingsVerified;
    const auto& l=f.hands[0];const auto& r=f.hands[1];
    const bool fresh=now>0&&f.predictedNs>0&&f.predictedNs<=now+50000000&&f.predictedNs>=now-100000000;
    const bool transition=ownerGeneration_!=owner.generation||spaceGeneration_!=f.spaceGeneration;
    if(transition){heading_=0;weaponDirection_=0;}
    ownerGeneration_=owner.generation;spaceGeneration_=f.spaceGeneration;
    left.focused=f.focused&&fresh&&ValidInput(f)&&owner.generation&&!transition;
    auto right=left;left.tracked=f.headValid&&l.gripTracked;right.tracked=f.headValid&&r.aimTracked;
    if(left.focused&&left.tracked){
        const float length=std::hypot(l.stickX,l.stickY);
        if(length>.18f){const float strength=((std::min)(length,1.f)-.18f)/.82f;left.forward=l.stickY/length*strength;left.strafe=l.stickX/length*strength;}
        if(const auto pose=math::MakeRelativePose(f.referenceHead,f.head)){
            const auto& q=pose->orientation;
            const float x=-2*(q.x*q.z+q.w*q.y),z=1-2*(q.x*q.x+q.y*q.y);
            if(std::hypot(x,z)>.05f)heading_=std::atan2(x,z);
        }
        const float c=std::cos(heading_),sn=std::sin(heading_),forward=left.forward,strafe=left.strafe;
        left.forward=forward*c-strafe*sn;left.strafe=forward*sn+strafe*c;
        // Left trigger drives finger posing only. Optic-specific proximity ADS,
        // when verified, must request its own semantic/native aiming state.
        if(l.held&Primary)left.held|=Use;
        if(l.held&Secondary)left.held|=Crouch;
        if((l.held&StickClick)&&!(r.held&StickClick))left.held|=Sprint;
        if(l.held&MenuClick)left.held|=Menu;
    }
    if(!right.focused||!right.tracked)weaponDirection_=0;
    if(right.focused&&right.tracked){
        // Vertical flick selects once, then needs the stick centered again.
        // Keep the semantic hold through equip/space transitions so neutral
        // arming cannot reissue a held flick. Diagonal snap turns do not select.
        if(std::abs(r.stickY)<.35f)weaponDirection_=0;
        else if(!weaponDirection_&&std::abs(r.stickY)>=.75f&&std::abs(r.stickY)>std::abs(r.stickX)+.15f)weaponDirection_=r.stickY>0?1:-1;
        if(weaponDirection_)right.held|=weaponDirection_>0?NextWeapon:PreviousWeapon;
        right.turn=weaponDirection_?0:r.stickX;
        if(r.trigger>=.75f)right.held|=Fire;
        if(r.held&Primary)right.held|=Jump;
        if(r.held&Secondary)right.held|=Reload;
    }
    // Each controller releases and requires neutral independently. Losing the
    // off-hand must neither cancel gun input nor re-arm a held trigger on return.
    const auto a=policies_[0].Update(left),b=policies_[1].Update(right);
    ActionOutput out;out.owner=a.active?a.owner:(b.active?b.owner:(a.owner?a.owner:b.owner));
    for(const auto& part:{a,b})if(part.owner==out.owner){
        out.active|=part.active;out.forward+=part.forward;out.strafe+=part.strafe;out.turnDegrees+=part.turnDegrees;
        out.held|=part.held;out.pressed|=part.pressed;out.released|=part.released;
    }
    return out;
}
bool RecenterGesture::Update(const InputFrame& f) noexcept {
    ++evidence_.samples;
    const bool buttons=(f.hands[0].active&StickClick)&&(f.hands[1].active&StickClick);
    const bool left=(f.hands[0].held&StickClick)!=0,right=(f.hands[1].held&StickClick)!=0;
    if(buttons)++evidence_.buttonsActive;if(left)++evidence_.leftHeld;if(right)++evidence_.rightHeld;
    if(left&&right){++evidence_.bothHeld;if(!f.hands[0].gripTracked||!f.hands[1].gripTracked)++evidence_.bothHeldWithoutHandPose;}
    if(!ValidInput(f)||!f.focused||!f.headValid||!buttons||f.spaceGeneration!=space_||f.predictedNs<last_){
        if(since_)++evidence_.interruptions;
        space_=f.spaceGeneration;last_=f.predictedNs;since_=0;armed_=false;return false;
    }
    if(f.predictedNs==last_)return false;
    const auto lastSample=last_;last_=f.predictedNs;
    const bool l=(f.hands[0].held&StickClick)!=0,r=(f.hands[1].held&StickClick)!=0;
    if(!l&&!r){armed_=true;since_=0;return false;}
    if(!armed_||!l||!r){since_=0;return false;}
    if(f.predictedNs-lastSample>150000000){if(since_)++evidence_.interruptions;armed_=false;since_=0;return false;}
    if(!since_){since_=f.predictedNs;++evidence_.starts;}
    evidence_.maxHoldNs=(std::max)(evidence_.maxHoldNs,std::uint64_t(f.predictedNs-since_));
    if(f.predictedNs-since_<1000000000)return false;
    ++evidence_.completed;armed_=false;since_=0;return true;
}
std::optional<math::Pose> UprightReference(const math::Pose& p) noexcept {
    if(!PoseOkay(p))return {};
    const auto& q=p.orientation;
    const float x=2*(q.x*q.z+q.w*q.y),z=1-2*(q.x*q.x+q.y*q.y);
    if(std::hypot(x,z)<.05f)return {}; // Looking vertically cannot define forward.
    const float yaw=std::atan2(x,z);auto out=p;
    out.orientation={0,std::sin(yaw*.5f),0,std::cos(yaw*.5f)};return out;
}
std::optional<math::Pose> ExplicitRecenterReference(const math::Pose& head,const math::Pose& previous)noexcept {
    if(const auto current=UprightReference(head))return current;
    if(!PoseOkay(head))return {};
    auto retained=UprightReference(previous);if(!retained)return {};
    retained->position=head.position;return retained;
}
}
