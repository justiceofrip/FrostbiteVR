#include "fvr/interaction/TrackedRig.h"
#include <cmath>
namespace fvr::interaction {
namespace {
math::Vec3 Transform(math::Vec3 p,const math::Matrix4& m,float w){
    return {p.x*m.values[0][0]+p.y*m.values[1][0]+p.z*m.values[2][0]+w*m.values[3][0],
            p.x*m.values[0][1]+p.y*m.values[1][1]+p.z*m.values[2][1]+w*m.values[3][1],
            p.x*m.values[0][2]+p.y*m.values[1][2]+p.z*m.values[2][2]+w*m.values[3][2]};
}
bool Finite(math::Vec3 p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);}
}

std::optional<math::Matrix4> BindAnatomyFrame(math::Vec3 head,math::Vec3 left,math::Vec3 right){
    if(!Finite(head)||!Finite(left)||!Finite(right))return {};
    const float x=right.x-left.x,z=right.z-left.z,length=std::hypot(x,z);
    if(!std::isfinite(length)||length<.01f)return {};
    math::Matrix4 out{};out.values[0]={x/length,0,z/length,0};out.values[1]={0,1,0,0};
    out.values[2]={-z/length,0,x/length,0};out.values[3]={head.x,head.y,head.z,1};return out;
}
std::optional<math::Matrix4> TrackedAimFrame(const InputFrame& input,const math::Matrix4& body){
    if(!ValidInput(input)||!input.focused||!input.headValid||!input.hands[1].aimTracked||!InverseRigid(body))return {};
    auto aim=math::MakeRelativePose(input.referenceHead,input.hands[1].aim);if(!aim)return {};
    aim->position={};const auto view=math::MakeLhViewFromOpenXRPose(*aim);if(!view)return {};
    const auto camera=InverseRigid(*view);if(!camera)return {};
    auto out=Multiply(*camera,body);out.values[3]={0,0,0,1};return out;
}
std::optional<math::Matrix4> PhysicalTorsoFrame(const InputFrame& input,const math::Matrix4& body){
    if(!ValidInput(input)||!input.focused||!input.headValid||!InverseRigid(body))return {};
    const auto head=math::MakeRelativePose(input.referenceHead,input.head);if(!head)return {};
    // Only anatomy follows physical heading. Controller/weapon targets continue
    // to use the original tracking body, so this cannot double-rotate either.
    // Project the relative quaternion onto gravity (its yaw twist): the closest
    // upright rotation remains defined when looking straight up/down, unlike a
    // projected forward vector. A fully upside-down swing has no unique twist;
    // retain the virtual body heading at that singular orientation.
    const float yawLength=std::hypot(head->orientation.y,head->orientation.w);
    math::Pose heading{};
    if(yawLength>1e-6f)heading.orientation={0,head->orientation.y/yawLength,0,head->orientation.w/yawLength};
    const auto view=math::MakeLhViewFromOpenXRPose(heading);if(!view)return {};
    const auto local=InverseRigid(*view);if(!local)return {};
    auto out=Multiply(*local,body);
    // Translation stays in the original XR reference basis, independent of yaw.
    for(unsigned axis=0;axis<3;++axis)out.values[3][axis]+=input.worldUnitsPerMeter*(head->position.x*body.values[0][axis]+head->position.y*body.values[1][axis]-head->position.z*body.values[2][axis]);
    return out;
}

std::optional<math::Matrix4> TrackedBodyFrame::Update(const TrackedRigOwner& owner,const InputFrame& input,
    const math::Matrix4& body,math::Vec3 actor,math::Vec3 consumed){
    if(!owner.actor||!owner.generation||!owner.skeleton||!ValidInput(input)||!input.focused||!input.headValid||!Finite(actor)||!Finite(consumed))return {};
    const auto inverse=InverseRigid(body);if(!inverse)return {};
    // Only upright yaw bases belong here. Tilting a gravity-aligned frame must
    // not rotate a stance-height offset into horizontal hand movement.
    if(std::abs(body.values[1][1]-1)>.001f||std::abs(body.values[0][1])>.001f||std::abs(body.values[2][1])>.001f)return {};
    if(owner.actor!=owner_.actor||owner.generation!=owner_.generation||owner.skeleton!=owner_.skeleton||space_!=input.spaceGeneration||units_!=input.worldUnitsPerMeter){
        const math::Vec3 delta{body.values[3][0]-actor.x,0,body.values[3][2]-actor.z};offset_=Transform(delta,*inverse,0);
    }
    const auto offset=Transform(offset_,body,0);auto out=body;
    out.values[3][0]=actor.x+offset.x;out.values[3][2]=actor.z+offset.z;
    // OpenXR local forward is -Z; this canonical body matrix uses +Z.
    for(unsigned axis=0;axis<3;++axis)out.values[3][axis]-=input.worldUnitsPerMeter*(consumed.x*body.values[0][axis]-consumed.z*body.values[2][axis]);
    owner_=owner;space_=input.spaceGeneration;units_=input.worldUnitsPerMeter;return out;
}

std::optional<TrackedRigPose> TrackedRig::Update(const TrackedRigOwner& owner,const InputFrame& input,
    const math::Matrix4& body,const math::Matrix4& nativeLeft,const math::Matrix4& nativeRight,const math::Matrix4& nativeWeapon,const std::array<ArmAnchor,2>& nativeArms,std::optional<math::Matrix4> anatomyBody,std::optional<math::Matrix4> nativeTorso,std::optional<math::Matrix4> weaponOrientation,std::optional<AuthoredGripAttachment> authoredGrip){
    if(!owner.actor||!owner.generation||!owner.equipped||!owner.skeleton||!ValidInput(input)||!input.focused||!input.headValid)return {};
    const auto anatomy=anatomyBody.value_or(body);const auto inverseAnatomy=InverseRigid(anatomy);
    const auto inverseBody=InverseRigid(body),inverseRight=InverseAnimatedTransform(nativeRight);
    if(!inverseAnatomy||!inverseBody||!inverseRight||!InverseAnimatedTransform(nativeLeft)||!InverseAnimatedTransform(nativeWeapon))return {};
    for(const auto& a:nativeArms)if(!Finite(a.shoulder)||!Finite(a.poleDirection))return {};
    std::optional<math::Matrix4> authoredWeaponInRight;
    if(authoredGrip){
        auto wrist=authoredGrip->rightInWeapon;
        if(!InverseRigid(wrist)||std::hypot(wrist.values[3][0],wrist.values[3][1],wrist.values[3][2])>1.5f)return {};
        for(unsigned axis=0;axis<3;++axis)wrist.values[3][axis]*=input.worldUnitsPerMeter;
        authoredWeaponInRight=InverseAnimatedTransform(wrist);if(!authoredWeaponInRight)return {};
    }
    // Calibrate orientation/anatomy, never initial controller translation.
    // The body argument is the SAME eye-reference origin used by the renderer.
    const bool actorReset=owner.actor!=owner_.actor||owner.generation!=owner_.generation||owner.skeleton!=owner_.skeleton||units_!=input.worldUnitsPerMeter;
    const bool reset=actorReset||space_!=input.spaceGeneration;
    auto attachments=attachment_;auto anchors=anchors_;auto calibrated=calibrated_;auto positions=calibrationPosition_;
    if(reset){calibrated={};torsoReady_=false;for(unsigned n=0;n<2;++n)anchors[n]={Transform(nativeArms[n].shoulder,*inverseAnatomy,1),Transform(nativeArms[n].poleDirection,*inverseAnatomy,0)};}
    std::array<math::Matrix4,2> target{nativeLeft,nativeRight};
    TrackedRigPose out;
    if(nativeTorso){
        if(!InverseAnimatedTransform(*nativeTorso))return {};
        if(reset||!torsoReady_)torsoAttachment_=Multiply(*nativeTorso,*inverseAnatomy);
        out.torso=Multiply(torsoAttachment_,anatomy);torsoReady_=true;
    }
    if(actorReset||owner.equipped!=owner_.equipped||owner.equipmentGeneration!=owner_.equipmentGeneration){weaponReady_=false;attachmentPending_=true;equipStarted_=attachmentStableSince_=input.predictedNs;}
    // Loss of an explicit source retires its relation, including within the
    // same native item. It cannot leak into the fallback acquisition window.
    if(authoredAttachment_&&!authoredWeaponInRight){weaponReady_=false;attachmentPending_=true;equipStarted_=attachmentStableSince_=input.predictedNs;}
    authoredAttachment_=bool(authoredWeaponInRight);
    if(authoredWeaponInRight){weaponAttachment_=*authoredWeaponInRight;weaponReady_=true;attachmentPending_=false;}
    if(!authoredWeaponInRight&&input.hands[1].gripTracked&&(!weaponReady_||attachmentPending_)){
        const auto current=Multiply(nativeWeapon,*inverseRight);
        bool stable=weaponReady_;
        for(unsigned r=0;r<4;++r)for(unsigned c=0;c<3;++c)
            if(std::abs(current.values[r][c]-attachmentCandidate_.values[r][c])>(r==3?.003f*input.worldUnitsPerMeter:.01f))stable=false;
        if(!stable){attachmentCandidate_=current;attachmentStableSince_=input.predictedNs;}
        // Native equip identity changes before the new animation has settled.
        // This authored relation must settle even without a measured aim profile.
        // Aim-axis admission and grip acquisition are independent capabilities.
        // Follow its authored hand attachment during that transition, then retain
        // the stable relation through recoil. Reference recenter never relearns it.
        weaponAttachment_=current;weaponReady_=true;
        if(attachmentPending_&&input.predictedNs-equipStarted_>=600000000&&input.predictedNs-attachmentStableSince_>=150000000)attachmentPending_=false;
    }
    out.weaponAttachmentPending=attachmentPending_;out.weaponAttachmentAuthored=authoredAttachment_;
    for(unsigned n=0;n<2;++n){
        out.arms[n]={Transform(anchors[n].shoulder,anatomy,1),Transform(anchors[n].poleDirection,anatomy,0)};
        if(!input.hands[n].gripTracked)continue; // Never read an untracked pose.
        auto relative=math::MakeRelativePose(input.referenceHead,input.hands[n].grip);if(!relative)return {};
        const auto position=relative->position;
        const auto localHead=math::MakeRelativePose(input.referenceHead,input.head);if(!localHead)return {};
        if(std::hypot(position.x-localHead->position.x,position.y-localHead->position.y,position.z-localHead->position.z)>1.5f)continue;
        relative->position.x*=input.worldUnitsPerMeter;relative->position.y*=input.worldUnitsPerMeter;relative->position.z*=input.worldUnitsPerMeter;
        const auto view=math::MakeLhViewFromOpenXRPose(*relative);if(!view)return {};
        const auto grip=InverseRigid(*view);if(!grip)return {};
        if(!calibrated[n]){
            const auto local=Multiply(target[n],*inverseBody);
            attachments[n]=Multiply(local,*InverseRigid(*grip));
            // Wrist positions are absolute controller positions relative to the
            // eye origin. Startup/reconnect hand placement must not become an offset.
            for(unsigned axis=0;axis<3;++axis)attachments[n].values[3][axis]=0;
            positions[n]=position;calibrated[n]=true;out.calibratedHands[n]=true;out.calibrated=true;
        }
        auto local=Multiply(attachments[n],*grip);
        for(unsigned axis=0;axis<3;++axis)local.values[3][axis]=attachments[n].values[3][axis]+grip->values[3][axis];
        target[n]=Multiply(local,body);out.tracked[n]=true;
    }
    if(!out.tracked[0]&&!out.tracked[1])return {};
    out.left=target[0];out.right=target[1];
    // Lock the gun attachment to the tracked wrist through recoil/pump animation.
    // Native child animation is retained by the adapter subtree retarget.
    out.weapon=out.tracked[1]?Multiply(weaponAttachment_,out.right):nativeWeapon;
    if(out.tracked[1]&&weaponOrientation){
        if(!InverseRigid(*weaponOrientation))return {};
        const auto wristFromWeapon=InverseAnimatedTransform(weaponAttachment_);if(!wristFromWeapon)return {};
        auto gun=*weaponOrientation;gun.values[3]={0,0,0,1};
        const auto wrist=Multiply(*wristFromWeapon,gun);
        for(unsigned axis=0;axis<3;++axis)gun.values[3][axis]=out.right.values[3][axis]-wrist.values[3][axis];
        out.weapon=gun;out.right=Multiply(*wristFromWeapon,gun);
    }
    if(!InverseAnimatedTransform(out.left)||!InverseAnimatedTransform(out.right)||!InverseAnimatedTransform(out.weapon))return {};
    owner_=owner;space_=input.spaceGeneration;units_=input.worldUnitsPerMeter;
    attachment_=attachments;calibrationPosition_=positions;anchors_=anchors;calibrated_=calibrated;
    return out;
}
}
