#include "SupportGrip.h"
#include <algorithm>
#include <cmath>
#include <limits>
namespace fvr::interaction {
namespace {
math::Quaternion Product(math::Quaternion a,math::Quaternion b){
 return {a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,
 a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w,a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z};
}
math::Quaternion Normalize(math::Quaternion q){const float n=std::sqrt(q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w);return {q.x/n,q.y/n,q.z/n,q.w/n};}
math::Quaternion Inverse(math::Quaternion q){return {-q.x,-q.y,-q.z,q.w};}
math::Vec3 Rotate(math::Quaternion q,math::Vec3 p){const auto v=Product(Product(q,{p.x,p.y,p.z,0}),Inverse(q));return {v.x,v.y,v.z};}
bool Finite(math::Vec3 p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);}
}
SupportGripResult SupportGrip::Update(const SupportGripOwner& owner,const InputFrame& input,const SupportGripContact& contact,bool cancel,bool handBusy)noexcept {
 SupportGripResult out;out.input=input;const bool previouslyHeld=holding_;
 const auto drop=[&](SupportRelease reason){out.reason=reason;holding_=armed_=false;token_=0;out.holding=out.engaged=false;out.released=previouslyHeld;return out;};
 const auto wait=[&](SupportRelease reason){const bool pending=armed_&&!previouslyHeld;out=drop(reason);armed_=pending;return out;};
 if(!owner.actor||!owner.generation||!owner.equipped||!ValidInput(input)||!input.focused||!input.headValid||
 !input.hands[0].gripTracked||!input.hands[1].gripTracked||!input.hands[1].aimTracked||
 !(input.hands[0].active&Squeeze))return drop(SupportRelease::Tracking);
 if(cancel)return drop(SupportRelease::Action);
 const bool transition=owner_!=owner||space_!=input.spaceGeneration||units_!=input.worldUnitsPerMeter||
 input.generation<generation_||input.predictedNs<time_||(time_&&input.predictedNs-time_>250000000);
 if(transition){holding_=armed_=false;token_=0;out.released=previouslyHeld;out.reason=SupportRelease::Identity;}
 const bool fresh=input.generation!=generation_;
 owner_=owner;space_=input.spaceGeneration;units_=input.worldUnitsPerMeter;generation_=input.generation;time_=input.predictedNs;
 const float squeeze=input.hands[0].squeeze;
 if(handBusy||!contact.valid||!std::isfinite(contact.distanceMeters)||contact.distanceMeters<0||!Finite(contact.wristOffsetMeters)){
  // No pose/contact/claim is authorized here. A genuinely new tracked neutral
  // packet is useful even while ammunition owns the hand or contact is absent.
  const bool neutral=squeeze<=.35f&&(fresh||armed_);
  out=wait(handBusy?SupportRelease::Action:SupportRelease::Contact);armed_|=neutral;return out;
 }
 if(squeeze<=.35f){holding_=false;token_=0;out.reason=SupportRelease::Button;if(fresh)armed_=true;out.released=previouslyHeld;return out;}
 if(contact.distanceMeters>(holding_?.28f:.16f))return wait(SupportRelease::Distance);
 const auto left=math::MakeRelativePose(input.referenceHead,input.hands[0].grip),right=math::MakeRelativePose(input.referenceHead,input.hands[1].grip);
 if(!left||!right)return drop(SupportRelease::Tracking);
 math::Vec3 direction{left->position.x-right->position.x+contact.wristOffsetMeters.x,
 left->position.y-right->position.y+contact.wristOffsetMeters.y,left->position.z-right->position.z+contact.wristOffsetMeters.z};
 const float length=std::hypot(direction.x,direction.y,direction.z);
 if(!std::isfinite(length)||length<.1f||length>1.2f)return wait(SupportRelease::Separation);
 direction={direction.x/length,direction.y/length,direction.z/length};
 if(!holding_){if(!armed_||!fresh||squeeze<.7f)return out;if(nextToken_==std::numeric_limits<std::uint64_t>::max())return drop(SupportRelease::Identity);token_=++nextToken_;localDirection_=Rotate(Inverse(right->orientation),direction);holding_=true;armed_=false;out.engaged=true;}
 const auto from=Rotate(right->orientation,localDirection_);
 const float dot=std::clamp(from.x*direction.x+from.y*direction.y+from.z*direction.z,-1.f,1.f);
 // Crossed hands must not select an arbitrary 180-degree rotation axis.
 if(dot<-.5f)return drop(SupportRelease::Crossed);
 const auto delta=Normalize({from.y*direction.z-from.z*direction.y,from.z*direction.x-from.x*direction.z,from.x*direction.y-from.y*direction.x,1+dot});
 const auto reference=Normalize(input.referenceHead.orientation),worldDelta=Product(Product(reference,delta),Inverse(reference));
 out.input.hands[1].grip.orientation=Normalize(Product(worldDelta,Normalize(input.hands[1].grip.orientation)));
 out.input.hands[1].aim.orientation=Normalize(Product(worldDelta,Normalize(input.hands[1].aim.orientation)));
 out.holding=true;out.token=token_;out.correctionRadians=std::acos(dot);return out;
}
}
