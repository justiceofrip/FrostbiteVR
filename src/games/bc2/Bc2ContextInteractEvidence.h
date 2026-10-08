#pragma once
#include <array>
#include <cstdint>
#include <ostream>
namespace fvr::bc2 {
// Read-only dispatch journal. A cache readback is not proof that the native
// interaction target accepted a pickup/entry/exit; those require owner receipts.
struct ContextInteractRecord {
 std::uint64_t input=0,space=0,actorGeneration=0,equipGeneration=0;
 std::int64_t observedNs=0,deadlineNs=0,nowNs=0;
 std::uint32_t player=0,soldier=0,weak=0,controlled=0,entry=0,cache=0,action=0,actionMask=0;
 std::uint64_t routeFingerprint=0;bool contextAliasVerified=false;
 bool onFoot=false,sourceFresh=false,playing=false,controllerHeld=false,semanticHeld=false;
 bool committed=false,ownerCurrent=false,readback=false,nativeHeld=false,reloadBlocked=false;
};
class ContextInteractEvidence {
public:
 bool NeedsReadback(bool requested)const noexcept{return requested||lastHeld_;}
 void Observe(const ContextInteractRecord& r)noexcept {
  ++gathers_;if(r.controllerHeld)++requested_;if(r.semanticHeld)++semantic_;
  if(r.committed&&r.semanticHeld){++committed_;if(r.ownerCurrent&&r.readback&&r.nativeHeld)++readbacks_;else ++unverified_;}
  const bool changed=!previous_||r.onFoot!=last_.onFoot||r.controlled!=last_.controlled||r.entry!=last_.entry||
   r.actorGeneration!=last_.actorGeneration||r.equipGeneration!=last_.equipGeneration||r.space!=last_.space||
   r.controllerHeld!=last_.controllerHeld||r.semanticHeld!=last_.semanticHeld||r.committed!=last_.committed||
   r.sourceFresh!=last_.sourceFresh||r.playing!=last_.playing||r.reloadBlocked!=last_.reloadBlocked||
   r.actionMask!=last_.actionMask||r.contextAliasVerified!=last_.contextAliasVerified||r.routeFingerprint!=last_.routeFingerprint||
   (r.controllerHeld&&(r.ownerCurrent!=last_.ownerCurrent||r.readback!=last_.readback||r.nativeHeld!=last_.nativeHeld));
  const bool heldSample=r.controllerHeld&&r.nowNs>=recordedNs_&&r.nowNs-recordedNs_>=100000000;
  if(changed||heldSample){records_[next_]=r;next_=(next_+1)%unsigned(records_.size());if(count_<records_.size())++count_;else ++dropped_;recordedNs_=r.nowNs;}
  last_=r;lastHeld_=r.controllerHeld;previous_=true;
 }
 void Report(std::ostream& o)const {
  o<<"{\"binding\":\"left_primary\",\"on_foot_default_action\":27,\"verified_on_foot_alias_action\":16,\"exit_action\":16,\"native_interaction_acceptance_verified\":false"
   <<",\"gathers\":"<<gathers_<<",\"controller_held_gathers\":"<<requested_<<",\"semantic_held_gathers\":"<<semantic_
   <<",\"committed_held_gathers\":"<<committed_<<",\"verified_held_readbacks\":"<<readbacks_<<",\"unverified_held_commits\":"<<unverified_
   <<",\"dropped\":"<<dropped_<<",\"records\":[";
  for(unsigned n=0;n<count_;++n){const auto& r=records_[(next_+unsigned(records_.size())-count_+n)%records_.size()];if(n)o<<',';
   o<<"{\"input\":"<<r.input<<",\"space\":"<<r.space<<",\"actor_generation\":"<<r.actorGeneration<<",\"equip_generation\":"<<r.equipGeneration
    <<",\"observed_ns\":"<<r.observedNs<<",\"deadline_ns\":"<<r.deadlineNs<<",\"now_ns\":"<<r.nowNs
    <<",\"player\":"<<r.player<<",\"soldier\":"<<r.soldier<<",\"weak\":"<<r.weak<<",\"controlled\":"<<r.controlled<<",\"entry\":"<<r.entry<<",\"cache\":"<<r.cache<<",\"action\":"<<r.action
    <<",\"action_mask\":"<<r.actionMask<<",\"context_alias_verified\":"<<r.contextAliasVerified<<",\"route_fingerprint\":"<<r.routeFingerprint
    <<",\"on_foot\":"<<r.onFoot<<",\"source_fresh\":"<<r.sourceFresh<<",\"playing\":"<<r.playing<<",\"controller_held\":"<<r.controllerHeld<<",\"semantic_held\":"<<r.semanticHeld
    <<",\"committed\":"<<r.committed<<",\"owner_current\":"<<r.ownerCurrent<<",\"readback\":"<<r.readback<<",\"native_held\":"<<r.nativeHeld<<",\"reload_blocked\":"<<r.reloadBlocked<<'}';
  }o<<"]}";
 }
private:
 std::array<ContextInteractRecord,64> records_{};unsigned count_=0,next_=0,dropped_=0;
 std::uint64_t gathers_=0,requested_=0,semantic_=0,committed_=0,readbacks_=0,unverified_=0;
 std::int64_t recordedNs_=0;bool lastHeld_=false,previous_=false;ContextInteractRecord last_{};
};
}
