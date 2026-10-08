#pragma once
#include "TrackingMath.h"
#include <cstdint>
namespace fvr::interaction {
struct GripAttachmentOwner {
    std::uint64_t actor=0,generation=0,item=0,skeleton=0,space=0;
    bool operator==(const GripAttachmentOwner&)const=default;
};
// Acquired item-local contact is immutable for one physical grab. Native reload
// animation cannot move it. The adapter owns tracking/freshness and grasp tokens.
class GripAttachment {
public:
    std::optional<math::Matrix4> Update(const GripAttachmentOwner& owner,
        std::uint64_t token,const math::Matrix4& authoredLocal) noexcept {
        if(!token||!owner.actor||!owner.generation||!owner.item||!owner.skeleton||!owner.space){
            Reset();return {};
        }
        if(owner!=owner_||token!=token_){
            Reset();
            if(!InverseAnimatedTransform(authoredLocal))return {};
            owner_=owner;token_=token;local_=authoredLocal;
        }
        return local_;
    }
    void Reset()noexcept {owner_={};token_=0;local_.reset();}
private:
    GripAttachmentOwner owner_{};std::uint64_t token_=0;
    std::optional<math::Matrix4> local_;
};
}
