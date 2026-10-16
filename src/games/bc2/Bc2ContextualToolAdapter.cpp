#include "Bc2ContextualToolAdapter.h"
#include "Bc2ContextualToolInteraction.h"
#include <cmath>
namespace fvr::bc2 {namespace {
using namespace interaction;
bool Contact(const HandContactProof& p,const HandInteractionSample& s)noexcept {
    return p.eligible&&p.key.id&&p.key.generation&&p.inputSequence==s.sequence&&p.deadlineNs>s.nowNs&&p.deadlineNs<=s.deadlineNs;
}
bool SelectedTool(const ContextualToolObservation& o)noexcept {
    return o.inventory.count>4&&o.inventory.count<=o.inventory.items.size()&&o.inventory.selected==4&&
        o.inventory.owner.weapon==o.tool.item.weapon&&o.inventory.items[4]==o.tool.item&&o.tool.item.category==12;
}
bool Idle(const ReloadStateSnapshot& s)noexcept {
    for(const auto& b:s.branches)if(b.currentState!=2||b.nextState!=2||!std::isfinite(b.phaseTimer)||b.phaseTimer<0||b.phaseTimer>=1000000||(b.flagsA8&8))return false;
    return true;
}
}
bool Bc2ContextualToolAdapter::Configure(const WeaponModeMemory& m,const ContextualToolBinding& b,const ReloadStateBinding& firing)noexcept {
    if(Active())return false;configured_=false;if(!b.verified||!m.read||!m.type)return false;
    const auto checked=VerifyContextualToolBinding(m,b.imageBase,b.imageSize);
    if(!checked||!ValidateReloadStateLive({m.context,m.read,m.type},firing,b.imageBase))return false;
    memory_=m;binding_=*checked;firingBinding_=firing;configured_=true;return true;
}
std::optional<ContextualToolObservation> Bc2ContextualToolAdapter::Read(const ReloadStateOwner& o)const noexcept {
    return configured_?ReadContextualTool(memory_,binding_,o):std::nullopt;
}
bool Bc2ContextualToolAdapter::WantsHeadAim(const ReloadStateOwner& owner,const InputFrame& in,
    const HandInteractionSample& s,const Bc2BodyHolster& body)const noexcept {
    if(!lifetime_.Anchor()||!lifetime_.Operation()||body.TemporaryToolOperation()!=lifetime_.Operation()||!ContextualToolSourceCurrent(in,s,s.nowNs)||
       lifetime_.Revoked()||phase_==ContextualToolPhase::Returning||phase_==ContextualToolPhase::Rehiding)return false;
    const auto current=Read(owner);
    return current&&lifetime_.ConfigurationCurrent(*current)&&SelectedTool(*current);
}
void Bc2ContextualToolAdapter::Release(const HandInteractionSample& s,HandInteraction& hands)noexcept {
    if(hold_)hands.Release(s,*hold_);hold_.reset();controls_.Suspend();
}
void Bc2ContextualToolAdapter::SuspendHold(const HandInteractionSample& s,HandInteraction& hands)noexcept {
    Release(s,hands);
    if(phase_==ContextualToolPhase::Held){phase_=ContextualToolPhase::AwaitNeutral;boundarySequence_=(std::max)(boundarySequence_,s.sequence);regripArmed_=false;}
}
void Bc2ContextualToolAdapter::Missing(const HandInteractionSample& s,HandInteraction& hands)noexcept {
    SuspendHold(s,hands);toggle_.Missing();regripArmed_=false;
}
void Bc2ContextualToolAdapter::Retire(const HandInteractionSample& s,HandInteraction& hands,bool unresolved,std::uint64_t currentBodyOperation)noexcept {
    if(lifetime_.Operation()&&lifetime_.Operation()==currentBodyOperation)return;
    if(unresolved)++unresolvedRetirements_;Release(s,hands);toggle_.Missing();lifetime_.ObserveBody(currentBodyOperation);toolKey_={};regripArmed_=false;phase_=ContextualToolPhase::Idle;
}
struct Bc2ContextualToolAdapter::WriteContext {
    Bc2ContextualToolAdapter& adapter;const BodyDrawSample& sample;BodyHolsterSample& post;
    Bc2BodyHolster& body;HandInteraction& hands;const ContextualToolObservation& observation;
    ContextualToolBoundary boundary;
    const std::optional<ContextualToolAim>& aim;
};
bool Bc2ContextualToolAdapter::Current(WriteContext& c,const ToolInputRequest& r)const noexcept {
    if(!c.boundary.now||!c.boundary.cache.current)return false;
    const auto now=c.boundary.now(c.boundary.clockContext);const auto& s=c.sample;
    if(now<s.hand.nowNs||!ContextualToolSourceCurrent(s.input,s.hand,now)||c.body.TemporaryToolOperation()!=lifetime_.Operation()||r.operation!=lifetime_.Operation()||
       !c.boundary.cache.current(c.boundary.cache.context,r.scope))return false;
    auto safety=s.hand;safety.nowNs=now;
    if(!c.hands.Update(safety).inputValid||!c.hands.HasRecordedEvidence(s.hand,InteractionHand::Right))return false;
    const auto claim=c.hands.Current(InteractionHand::Right);
    if(r.kind==ToolInputKind::Return){if(claim||!s.hand.released[1]||s.input.hands[1].squeeze>.35f)return false;}
    else if(!claim||claim->token!=r.custody||claim->inputSequence!=s.hand.sequence||claim->deadlineNs<=now)return false;
    const auto observation=Read(s.owner);if(!observation||*observation!=c.observation)return false;
    if(r.kind==ToolInputKind::Equip){if(!ResolveStaticToolEquip(*observation))return false;}
    else if(r.kind==ToolInputKind::Return){if(!ResolveStaticToolReturn(*observation,r.target))return false;}
    else if(!SelectedTool(*observation))return false;
    // Actual selected owner, not a fabricated owner of an unselected tool.
    const auto native=ReadReloadState({memory_.context,memory_.read,memory_.type},firingBinding_,binding_.imageBase,
        s.owner,s.hand.sequence,now);
    if(!native.snapshot||native.snapshot->inventory!=observation->inventory.inventory||native.snapshot->selectedSlot!=r.from.slot||
       native.snapshot->config.weaponData!=r.from.data)return false;
    if(r.kind==ToolInputKind::Controls){
        if(!c.aim||!ContextualToolAimCurrent(*c.aim,s.owner,s.hand,lifetime_.Operation(),now)||
           !c.boundary.aimCurrent||!c.boundary.aimCurrent(c.boundary.clockContext,*c.aim))return false;
        if(native.snapshot->config.fireInputAction!=8||native.snapshot->config.firingData!=observation->tool.firing||
           native.snapshot->config.assetPath!=observation->tool.asset)return false;
    }else if(!Idle(*native.snapshot))return false;
    // A slow coherent read cannot renew the original packet or menu lease.
    const auto after=c.boundary.now(c.boundary.clockContext);
    const auto finalClaim=c.hands.Current(InteractionHand::Right);
    const bool claimCurrent=r.kind==ToolInputKind::Return?!finalClaim:
        finalClaim&&finalClaim->token==r.custody&&finalClaim->deadlineNs>after;
    const bool aimCurrent=r.kind!=ToolInputKind::Controls||(c.aim&&ContextualToolAimCurrent(*c.aim,s.owner,s.hand,lifetime_.Operation(),after)&&
        c.boundary.aimCurrent&&c.boundary.aimCurrent(c.boundary.clockContext,*c.aim));
    return aimCurrent&&claimCurrent&&after>=now&&ContextualToolSourceCurrent(s.input,s.hand,after)&&c.body.TemporaryToolOperation()==lifetime_.Operation()&&
        c.boundary.cache.current(c.boundary.cache.context,r.scope);
}
std::optional<ToolInputReceipt> Bc2ContextualToolAdapter::Tick(const BodyDrawSample& s,BodyHolsterSample& post,
    BodyHolsterResult& result,const std::optional<ContextualToolAttachment>& rendered,
    const std::optional<ContextualToolAim>& aim,bool mechanics,std::span<std::byte> cache,
    ContextualToolBoundary boundary,Bc2BodyInventory& inventory,Bc2BodyHolster& body,HandInteraction& hands,std::uint64_t& intent)noexcept {
    if(!configured_)return {};
    if(lifetime_.Operation()&&body.LastTemporaryToolReturn()==lifetime_.Operation()&&!body.TemporaryToolActive()&&result.freeRight&&
       BodyFreeRightCurrent(*result.freeRight,post,hands)){
        ++returned_;Retire(s.hand,hands,false,body.TemporaryToolOperation());return {};
    }
    if(lifetime_.Operation()&&body.TemporaryToolOperation()!=lifetime_.Operation()){Retire(s.hand,hands,true,body.TemporaryToolOperation());return {};}
    const auto observation=Read(s.owner);
    const bool wasRevoked=lifetime_.Revoked();
    const auto selection=lifetime_.Observe(s.owner,observation);
    if(lifetime_.Revoked()){
        if(!wasRevoked){++metadataRevocations_;Release(s.hand,hands);toggle_.Missing();regripArmed_=false;}
        // Never forget a shared operation on a metadata-only change. Pending
        // native commands keep their phase until an actual outcome resolves them.
        if(phase_==ContextualToolPhase::Held||phase_==ContextualToolPhase::AwaitNeutral)
            phase_=ContextualToolPhase::Cleanup;
    }
    if(!observation){++readGaps_;Missing(s.hand,hands);return {};}
    if((phase_==ContextualToolPhase::Returning||phase_==ContextualToolPhase::Cleanup)&&
       selection==ContextualToolSelection::Original){
        body.RequestTemporaryToolRehide(lifetime_.Operation());phase_=ContextualToolPhase::Rehiding;
    }
    if(!ContextualToolInteractionCurrent(s,*observation,phase_==ContextualToolPhase::Held,lifetime_.Revoked(),
       lifetime_.Operation(),body.TemporaryToolOperation(),hold_,hands)||!mechanics){Missing(s.hand,hands);return {};}
    hands.Update(s.hand);
    if(phase_==ContextualToolPhase::Selecting&&selection==ContextualToolSelection::Tool){
        Release(s.hand,hands);phase_=lifetime_.Revoked()?ContextualToolPhase::Cleanup:ContextualToolPhase::AwaitNeutral;boundarySequence_=s.hand.sequence;regripArmed_=true;++selected_;
    }
    if(phase_==ContextualToolPhase::Rehiding||phase_==ContextualToolPhase::Returning||phase_==ContextualToolPhase::Selecting)return {};
    const auto currentKey=inventory.ItemKey(observation->tool.item.weapon);
    const auto gesture=toggle_.Update(s.input,s.hand,currentKey?HandInteractionKey{currentKey->id,currentKey->generation}:HandInteractionKey{},Active());
    const bool attachment=rendered&&currentKey&&SelectedTool(*observation)&&
        rendered->equipment.data==observation->tool.item.data&&rendered->equipment.persistence==observation->tool.item.persistence&&
        ContextualToolAttachmentCurrent(*rendered,s.owner,s.hand,boundarySequence_,s.hand.nowNs);
    const HandContactProof heldContact=attachment?HandContactProof{{0x4243325447524950ull,currentKey->generation},s.hand.sequence,
        (std::min)(s.hand.deadlineNs,rendered->input.deadlineNs),true}:HandContactProof{};
    ToolInputKind kind=ToolInputKind::Controls;ToolControlLevels levels;
    if(phase_==ContextualToolPhase::Idle){
        if(gesture.kind!=ContextualToolToggleKind::Draw||s.hand.sequence<=drawSequence_||!ContextualToolQuiet(s.input,true)||!Contact(gesture.intent,s.hand)||
           s.input.hands[1].squeeze<.65f||s.hand.released[1]||!result.freeRight||body.Phase()!=BodyHolsterPhase::Empty||
           !ResolveStaticToolEquip(*observation)||observation->lastPrimary!=observation->inventory.selected||
           observation->primary.item!=observation->inventory.items[observation->inventory.selected]||intent==UINT64_MAX)return {};
        drawSequence_=s.hand.sequence;const auto key=inventory.ItemKey(observation->tool.item.weapon);const auto operation=body.NextTemporaryToolOperation();
        if(!key||!operation)return {};
        const auto claimed=hands.Acquire(s.hand,{s.hand.owner,InteractionHand::Right,HandClaimKind::BodyInventory,
            {key->id,key->generation},gesture.intent,++intent,0});
        if(!claimed.claim)return {};hold_=claimed.claim->token;
        if(!lifetime_.Begin(operation,*observation)){Release(s.hand,hands);return {};}
        if(!inventory.BeginTemporaryTool(memory_,s,post,body,*key,operation,*result.freeRight,*hold_,hands)){
            lifetime_.ObserveBody(body.TemporaryToolOperation());Release(s.hand,hands);return {};
        }
        toolKey_=*key;kind=ToolInputKind::Equip;phase_=ContextualToolPhase::Selecting;
        result.freeRight.reset();result.allowAutomaticGunHold=false;result.blockWeaponActions=true;
    }else if(selection==ContextualToolSelection::Tool&&gesture.kind==ContextualToolToggleKind::Return&&ContextualToolQuiet(s.input,true)&&Contact(gesture.intent,s.hand)&&
             s.hand.released[1]&&s.input.hands[1].squeeze<=.35f&&lifetime_.Anchor()&&ResolveStaticToolReturn(*observation,lifetime_.Anchor()->inventory.items[lifetime_.Anchor()->inventory.selected])){
        Release(s.hand,hands);if(hands.Current(InteractionHand::Right))return {};kind=ToolInputKind::Return;
    }else {
        if(lifetime_.Revoked()){Release(s.hand,hands);return {};}
        if(s.hand.released[1])SuspendHold(s.hand,hands);
        if(phase_==ContextualToolPhase::AwaitNeutral&&s.hand.released[1]&&s.input.hands[1].squeeze<=.35f)
            regripArmed_=true;
        if(!SelectedTool(*observation)||!attachment||!aim||!ContextualToolAimCurrent(*aim,s.owner,s.hand,lifetime_.Operation(),s.hand.nowNs)){SuspendHold(s.hand,hands);return {};}
        if(phase_==ContextualToolPhase::Held){const auto claim=hands.Current(InteractionHand::Right);
            if(!hold_||!claim||claim->token!=*hold_){SuspendHold(s.hand,hands);return {};}
            const auto renewal=ContextualToolRenewalContact(heldContact,*claim);
            if(!Contact(renewal,s.hand)||!hands.Renew(s.hand,*hold_,renewal).accepted){
                SuspendHold(s.hand,hands);return {};}}
        if(phase_==ContextualToolPhase::AwaitNeutral){
            if(s.hand.sequence<=boundarySequence_)return {};
            if(s.hand.released[1]&&s.input.hands[1].squeeze<=.35f)regripArmed_=true;
            if(!regripArmed_||!ContextualToolQuiet(s.input)||s.hand.released[1]||s.input.hands[1].squeeze<.65f||!Contact(heldContact,s.hand)||intent==UINT64_MAX)return {};
            const auto claimed=hands.Acquire(s.hand,{s.hand.owner,InteractionHand::Right,HandClaimKind::GunHold,
                {toolKey_.id,toolKey_.generation},heldContact,++intent,0});
            if(!claimed.claim)return {};hold_=claimed.claim->token;phase_=ContextualToolPhase::Held;regripArmed_=false;
        }
        if(!hold_)return {};levels=controls_.Update(*observation,s.input,s.hand,*hold_,hands);
    }
    const auto demand=body.Demand(post);
    if(!demand||!post.suppression||!HolsterSuppressionCurrent(*post.suppression,*demand)||serial_==UINT64_MAX){
        if(kind==ToolInputKind::Equip){Release(s.hand,hands);body.RequestTemporaryToolRehide(lifetime_.Operation());phase_=ContextualToolPhase::Rehiding;}
        return {};
    }
    ToolInputRequest request;request.scope=*demand;request.operation=lifetime_.Operation();request.serial=++serial_;request.kind=kind;
    request.from=observation->inventory.items[observation->inventory.selected];
    request.target=kind==ToolInputKind::Return?lifetime_.Anchor()->inventory.items[lifetime_.Anchor()->inventory.selected]:observation->tool.item;
    if(kind!=ToolInputKind::Return&&hold_)request.custody=*hold_;request.zoom=levels.zoom;request.fire=levels.fire;
    WriteContext context{*this,s,post,body,hands,*observation,boundary,aim};ContextualToolInputOverride writer;
    const ToolInputValidation validation{&context,[](void* p,const ToolInputRequest& r)noexcept{
        auto& c=*static_cast<WriteContext*>(p);return c.adapter.Current(c,r);}};
    const auto receipt=writer.Apply(cache,request,validation)?writer.Commit():std::nullopt;
    if(!receipt){++rejected_;controls_.Suspend();
        if(kind==ToolInputKind::Equip){Release(s.hand,hands);body.RequestTemporaryToolRehide(lifetime_.Operation());phase_=ContextualToolPhase::Rehiding;}
        return {};
    }
    // This cache now contains the authorized exception, so every outward copy
    // of a standard full suppression receipt must be discarded by the caller.
    post.suppression.reset();result.freeRight.reset();result.allowAutomaticGunHold=false;result.blockWeaponActions=true;
    if(kind!=ToolInputKind::Controls&&!lifetime_.RememberCommit(*receipt))++commitRecordFailures_;
    if(kind==ToolInputKind::Equip)++equipCommits_;
    else if(kind==ToolInputKind::Return){++returnCommits_;phase_=ContextualToolPhase::Returning;}
    else ++controlCommits_;
    return receipt;
}
void Bc2ContextualToolAdapter::PublishProbe(const BodyDrawSample& s,const BodyHolsterSample& post,
    const BodyHolsterResult& result,const Bc2BodyHolster& body,const HandInteraction& hands)noexcept {
    ContextualToolProbeWitness w;
    const auto observation=Read(s.owner);
    if(!observation||!ContextualToolSourceCurrent(s.input,s.hand,s.hand.nowNs)){probe_.Publish(w);return;}
    w.observedNs=std::uint64_t(s.hand.observedNs);w.deadlineNs=std::uint64_t(s.hand.deadlineNs);
    w.input=s.hand.sequence;w.space=s.hand.owner.space;w.actor=s.hand.owner.actor;w.actorGeneration=s.hand.owner.actorGeneration;
    w.weak=s.owner.weak;w.inventory=observation->inventory.inventory;
    w.originalWeapon=observation->primary.item.weapon;w.originalData=observation->primary.item.data;
    w.toolWeapon=observation->tool.item.weapon;w.toolData=observation->tool.item.data;w.toolAsset=observation->tool.asset;
    w.selectedSlot=observation->inventory.selected;w.bodyOperation=body.TemporaryToolOperation();w.lastBodyReturn=body.LastTemporaryToolReturn();
    w.phase=unsigned(phase_);w.equipCommits=equipCommits_;w.selectedObservations=selected_;w.returnCommits=returnCommits_;w.hideCompletions=returned_;
    w.revoked=lifetime_.Revoked();
    w.empty=body.Phase()==BodyHolsterPhase::Empty&&!body.TemporaryToolActive()&&result.freeRight&&BodyFreeRightCurrent(*result.freeRight,post,hands);
    const auto claim=hands.Current(InteractionHand::Right);
    w.held=phase_==ContextualToolPhase::Held&&SelectedTool(*observation)&&lifetime_.ConfigurationCurrent(*observation)&&
        w.bodyOperation&&w.bodyOperation==lifetime_.Operation()&&hold_&&claim&&claim->token==*hold_&&
        claim->token.kind==HandClaimKind::GunHold&&claim->inputSequence==s.hand.sequence&&claim->deadlineNs>s.hand.nowNs;
    if(w.held)w.holdToken=hold_->id;
    probe_.Publish(w);
}
void Bc2ContextualToolAdapter::Report(std::ostream& out)const {
    out<<"{\"phase\":"<<unsigned(phase_)<<",\"body_operation\":"<<lifetime_.Operation()<<",\"equip_cache_commits\":"<<equipCommits_
       <<",\"actual_selected\":"<<selected_<<",\"control_cache_commits\":"<<controlCommits_<<",\"return_cache_commits\":"<<returnCommits_
       <<",\"new_hide_completions\":"<<returned_<<",\"rejected_writes\":"<<rejected_<<",\"read_gaps\":"<<readGaps_
       <<",\"unresolved_lifetime_retirements\":"<<unresolvedRetirements_
       <<",\"metadata_revocations\":"<<metadataRevocations_<<",\"actions_revoked\":"<<(lifetime_.Revoked()?"true":"false")
       <<",\"commit_record_failures\":"<<commitRecordFailures_
       <<",\"witness_publish_misses\":"<<probe_.PublishMisses()<<",\"witness_read_misses\":"<<probe_.ReadMisses();
    const auto command=[&out](const std::optional<ToolInputReceipt>& receipt){
        if(!receipt){out<<"null";return;}
        const auto& r=receipt->request;const auto& s=r.scope;const auto& o=s.owner;const auto& i=s.input;
        out<<"{\"operation\":"<<r.operation<<",\"serial\":"<<r.serial<<",\"kind\":"<<unsigned(r.kind)
           <<",\"cache\":"<<s.cache<<",\"request\":"<<s.request<<",\"native_tick\":"<<s.nativeTick
           <<",\"player\":"<<o.player<<",\"soldier\":"<<o.soldier<<",\"weak\":"<<o.weak<<",\"weapon\":"<<o.weapon
           <<",\"actor_generation\":"<<o.actorGeneration<<",\"equip_generation\":"<<o.equipGeneration<<",\"space\":"<<o.space
           <<",\"input\":"<<i.sequence<<",\"observed_ns\":"<<i.observedNs<<",\"deadline_ns\":"<<i.deadlineNs<<",\"admission_ns\":"<<i.nowNs
           <<",\"from\":"<<r.from.weapon<<",\"target\":"<<r.target.weapon<<",\"claim\":"<<r.custody.id
           <<",\"native_consumption_proved\":false}";
    };
    out<<",\"equip_commit\":";command(lifetime_.EquipCommit());out<<",\"return_commit\":";command(lifetime_.ReturnCommit());out<<"}";
}
}
