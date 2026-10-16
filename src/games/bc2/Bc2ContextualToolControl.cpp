#include "Bc2ContextualToolAdapter.h"
#include <cmath>
namespace fvr::bc2 {namespace {
using namespace interaction;
bool Source(const InputFrame& in,const HandInteractionSample& s,std::int64_t now)noexcept {
    if(!ValidInput(in)||!in.focused||!in.headValid||!s.focused||!s.tracked[0]||!s.tracked[1]||
       !s.sequence||in.generation!=s.sequence||in.spaceGeneration!=s.owner.space||s.observedNs<=0||
       s.observedNs>now||s.deadlineNs<=now||s.deadlineNs-s.observedNs>100000000||
       !GameplayOwnsInput(in.menu,now))return false;
    for(const auto& h:in.hands)if(!h.gripTracked||(h.active&(Trigger|Squeeze))!=(Trigger|Squeeze))return false;
    return true;
}
bool Neutral(const InputFrame& in)noexcept {
    for(const auto& h:in.hands)if(h.trigger>.1f||h.held||std::hypot(h.stickX,h.stickY)>.1f)return false;return true;
}
bool SelectedTool(const ContextualToolObservation& o)noexcept {
    return o.inventory.count>4&&o.inventory.count<=o.inventory.items.size()&&o.inventory.selected==4&&
        o.inventory.owner.weapon==o.tool.item.weapon&&o.inventory.items[4]==o.tool.item&&o.tool.item.category==12;
}
}
ToolControlLevels ContextualToolControlGate::Update(const ContextualToolObservation& o,const InputFrame& in,
    const HandInteractionSample& s,const HandClaimToken& hold,const HandInteraction& hands)noexcept {
    const auto deny=[&](){Suspend();return ToolControlLevels{};};const auto current=hands.Current(InteractionHand::Right);
    const auto& owner=o.inventory.owner;
    if(!SelectedTool(o)||!Source(in,s,s.nowNs)||s.owner.actor!=((std::uint64_t(owner.weak)<<32)|owner.soldier)||
       s.owner.actorGeneration!=owner.actorGeneration||s.owner.space!=owner.space||s.released[1]||
       !current||current->token!=hold||hold.owner!=s.owner||hold.kind!=HandClaimKind::GunHold||hold.hand!=InteractionHand::Right||
       hold.item.id!=o.tool.item.weapon||!hold.item.generation||current->inputSequence!=s.sequence||current->deadlineNs<=s.nowNs||
       !hands.HasRecordedEvidence(s,InteractionHand::Left)||!hands.HasRecordedEvidence(s,InteractionHand::Right))return deny();
    const std::array triggers{in.hands[0].trigger,in.hands[1].trigger},squeezes{in.hands[0].squeeze,in.hands[1].squeeze};
    const std::array active{in.hands[0].active,in.hands[1].active},held{in.hands[0].held,in.hands[1].held};
    const Identity identity{owner,o.tool,hold};
    const auto remember=[&](){sequence_=s.sequence;observed_=s.observedNs;deadline_=s.deadlineNs;predicted_=in.predictedNs;
        triggers_=triggers;squeezes_=squeezes;active_=active;held_=held;};
    if(!identity_||*identity_!=identity){if(s.sequence<=sequence_)return deny();identity_=identity;armed_=false;remember();return {};}
    if(s.sequence<sequence_||s.observedNs<observed_||(s.sequence==sequence_&&
       (s.observedNs!=observed_||s.deadlineNs!=deadline_||in.predictedNs!=predicted_||triggers!=triggers_||squeezes!=squeezes_||active!=active_||held!=held_)))return deny();
    if(s.sequence>sequence_){if(s.observedNs-observed_>100000000)armed_=false;
        remember();if(Neutral(in))armed_=true;}
    return {armed_&&triggers[0]>=.75f,armed_&&triggers[1]>=.75f};
}
}
