#include "Bc2MagazineDetached.h"
#include <limits>
namespace fvr::bc2 {
namespace {
using namespace interaction;using namespace reload_insertion_detail;
bool SameInput(const HandInteractionSample& a,const HandInteractionSample& b)noexcept {
 return a.owner==b.owner&&a.sequence==b.sequence&&a.observedNs==b.observedNs&&a.deadlineNs==b.deadlineNs&&
  a.focused==b.focused&&a.tracked==b.tracked&&a.released==b.released;
}
math::Matrix4 Attached(const ReloadInsertionProfile& p)noexcept {
 return Multiply(*InverseRigid(p.itemFromInsertion),Multiply(TravelPose(p,p.travelMeters),p.weaponFromEntry));
}
}
void Bc2MagazineDetached::ResetRecognition(bool newOwner)noexcept {
 history_={};historyNext_=0;lastGeometry_=0;neutral_=false;pulled_=false;last_={};lastCommittedReserve_.reset();
 if(newOwner){lastInput_=lastPrepared_=0;recognitionNative_.reset();recognitionOwner_.reset();recognitionWeapon_={};recognitionFamily_.reset();}
}
void Bc2MagazineDetached::Cancel(unsigned reason)noexcept {
 if(gate_.BlocksActions()&&gate_.Phase()!=MagazineDetachPhase::Recovering){cancelReason_=reason;cancelInput_=sample_.input.sequence;cancelNs_=sample_.input.nowNs;}
 gate_.Invalidate();
}
MagazinePhysicalResult Bc2MagazineDetached::Prepare(const MagazinePhysicalSample& s,const std::optional<Bc2AmmoReserveLease>& reserve,
 std::uint64_t tick,std::uint32_t cache,HandInteraction& hands)noexcept {
 MagazinePhysicalResult out;if(!enabled_)return out;
 sample_=s;const auto gun=hands.Current(InteractionHand::Right);
 native_={s.family,reserve.value_or(Bc2AmmoReserveLease{}),s.input,gun.value_or(HandClaim{}),s.meshes,tick,cache,s.raw.nativeMagazineAttached};
 if(!reserve){++preReadMissing_;
  if(gate_.BlocksActions()&&lastCommittedReserve_){auto retained=native_;retained.reserve=*lastCommittedReserve_;
   if(gate_.Demand(retained)){native_.reserve=*lastCommittedReserve_;++preReadDeferred_;}
  }
 }
 const auto& r=native_.reserve;
 replacement_=gate_.ReplacementDemand(s.nativeOwner,s.input,tick,cache);
 if(replacement_){out.blocksWeaponActions=true;return out;}
 const MagazineTracking baseline{s.family,true,s.nativeOwner,s.input,s.meshes,r,0,{}};
 if(!gate_.BlocksActions()&&MagazineTrackingFresh(baseline,s.input.nowNs)&&
  (!recognitionNative_||*recognitionNative_!=s.nativeOwner||!recognitionOwner_||*recognitionOwner_!=s.input.owner||recognitionWeapon_!=s.family.binding.weapon||recognitionFamily_!=s.family.binding)){
  ResetRecognition(true);recognitionNative_=s.nativeOwner;recognitionOwner_=s.input.owner;recognitionWeapon_=s.family.binding.weapon;recognitionFamily_=s.family.binding;
 }
 const auto equipment=s.family.binding.profile;
 if(!equipment||!equipment->Ready()){
  if(gate_.BlocksActions())Cancel(5);out.blocksWeaponActions=gate_.BlocksActions();return out;
 }
 const bool fresh=s.input.sequence>lastPrepared_;
 if(fresh){lastPrepared_=s.input.sequence;history_[historyNext_]=Raw{s.input,native_.gun,held_};historyNext_=(historyNext_+1)%history_.size();}
 const auto& config=equipment->geometry->interaction;const auto attached=Attached(config.insertion);
 const bool raw=s.raw.valid&&s.raw.owner==s.nativeOwner&&s.raw.rigFingerprint==equipment->geometry->rigFingerprint&&
  s.originalHandEvidence&&SameInput(s.raw.inputEvidence,*s.originalHandEvidence)&&s.raw.inputEvidence.deadlineNs>s.input.nowNs&&
  Rigid(s.raw.rawLeftWristWorldMeters)&&Rigid(s.raw.weaponWorldMeters);
 bool remembered=false;for(const auto& h:history_)if(h&&SameInput(h->input,s.raw.inputEvidence)&&h->gun.token==native_.gun.token)remembered=true;
 if(!gate_.BlocksActions()&&fresh){
  if(!s.gripPressed)neutral_=true;
  else if(neutral_){neutral_=false;
   if(!s.cancel&&!s.ejectPressed&&raw&&remembered&&s.raw.nativeMagazineAttached&&!hands.Current(InteractionHand::Left)&&
    Distance(Multiply(s.raw.rawLeftWristWorldMeters,*InverseRigid(s.raw.weaponWorldMeters)),Multiply(config.insertion.itemFromHand,attached))<=config.grabRadiusMeters)
     gate_.Begin(native_);
  }
 }
 if(gate_.BlocksActions()&&(s.cancel||!s.input.focused||!s.input.tracked[0]||!s.input.tracked[1]||s.input.released[1]||
  s.asset!=equipment->geometry->asset||!gate_.Demand(native_)))Cancel(s.cancel?1u:!s.input.focused?2u:(!s.input.tracked[0]||!s.input.tracked[1])?3u:s.input.released[1]?4u:s.asset!=equipment->geometry->asset?5u:reserve?6u:7u);
 out.blocksWeaponActions=gate_.BlocksActions();out.tracking={s.family,true,s.nativeOwner,s.input,s.meshes,r,0,{}};
 if(!MagazineTrackingFresh(out.tracking,s.input.nowNs))out.tracking={};
 return out;
}
std::optional<HolsterSuppressionRequest> Bc2MagazineDetached::Demand()const noexcept {
 if(!enabled_)return {};if(replacement_)return replacement_;
 if(const auto fresh=gate_.Demand(native_))return fresh;
 return gate_.RecoveryDemand(sample_.nativeOwner,sample_.input,native_.nativeTick,native_.cache);
}
MagazinePhysicalResult Bc2MagazineDetached::Commit(const std::optional<HolsterSuppressionReceipt>& receipt,
 const std::optional<MagazineDetachPairReceipt>& pair,const std::optional<Bc2AmmoReserveLease>& post,
 HandInteraction& hands,std::uint64_t& intent,std::int64_t committedNs,std::int64_t nowNs)noexcept {
 MagazinePhysicalResult out;if(!enabled_)return out;
 if(nowNs>=sample_.input.nowNs){sample_.input.nowNs=nowNs;native_.input.nowNs=nowNs;}
 else{Cancel(8);out.blocksWeaponActions=gate_.BlocksActions();return out;}
 const auto& s=sample_;const auto& in=s.input;
 out.blocksWeaponActions=gate_.BlocksActions();
 if(!gate_.BlocksActions()){
  // The renderer needs fresh ordinary tracking to publish the NEXT raw grip
  // contact. Idle publication grants no private magazine pose or suppression.
  if(post)out.tracking={s.family,true,s.nativeOwner,in,s.meshes,*post,0,{}};
  if(!MagazineTrackingFresh(out.tracking,in.nowNs))out.tracking={};return out;
 }
 if(replacement_){if(receipt&&gate_.RetireReplacedOwner(*replacement_,*receipt)){
   if(held_)hands.Release(in,held_->token);held_.reset();insertion_.reset();ResetRecognition(true);out.blocksWeaponActions=false;
  }return out;}
 if(!post){
  ++postReadMissing_;const auto demand=Demand();const auto auth=gate_.Authorization();
  const auto hand=hands.Current(InteractionHand::Left);
  // Repeat only an already-issued Removed pose while its ORIGINAL evidence
  // is live and this Gather actually suppressed the exact current owner.
  // No contact, claim renewal, motion, gate Commit or seat advances here.
  if(gate_.Phase()==MagazineDetachPhase::Detached&&s.gripPressed&&!in.released[0]&&!s.cancel&&
   receipt&&demand&&HolsterSuppressionCurrent(*receipt,*demand)&&auth&&last_.tracking.detach&&
   MagazineDetachAuthorizationRetained(*last_.tracking.detach,*auth,in.nowNs)&&
   last_.tracking.target&&last_.tracking.target->role==MagazinePropRole::Removed&&held_&&hand&&
   hand->token==held_->token&&hand->token==last_.tracking.target->handClaim&&hand->deadlineNs>in.nowNs&&
   native_.gun.token==last_.tracking.target->gunClaim&&s.family.binding==last_.tracking.family.binding){
   out.tracking=last_.tracking;out.tracking.inputEvidence=in;out.tracking.retainedVisualSuppression=*receipt;
   if(!MagazineTargetFresh(out.tracking,in.nowNs))out.tracking={};
   out.ownsLeftHand=bool(out.tracking.target);
  }
  RecordVisualGap(out.tracking.target?1u:2u);return out;
 }
 RecordVisualGap(0);
 native_.reserve=*post;
 if(!receipt){Cancel(9);return out;}
 if(!gate_.Commit(native_,*receipt,committedNs)){cancelReason_=10;cancelInput_=in.sequence;cancelNs_=in.nowNs;return out;}
 lastCommittedReserve_=*post;
 auto auth=gate_.Authorization();if(!auth)return out;
 const auto equipment=s.family.binding.profile; // Gate Commit proved exact original profile and current evidence.
 const auto& config=equipment->geometry->interaction;const auto attached=Attached(config.insertion);
 const auto attachedHand=Multiply(config.insertion.itemFromHand,attached);
 out.tracking={s.family,true,s.nativeOwner,in,s.meshes,native_.reserve,auth->request,{}};
 out.tracking.detach=std::make_shared<const MagazineDetachAuthorization>(*auth);
 const auto target=[&](MagazinePropRole role,const math::Matrix4& item,const math::Matrix4& wrist,const Raw* raw=nullptr){
  MagazinePropTarget t;t.role=role;t.owner=in.owner;t.weapon=auth->original.weapon;t.profile=auth->original.profile;t.originalMagazine=auth->original;
  t.gunClaim=native_.gun.token;t.trackingEpoch=in.owner.space;t.inputSequence=raw?raw->input.sequence:in.sequence;t.nativeCycle=auth->request;
  t.observedNs=raw?raw->input.observedNs:in.observedNs;t.deadlineNs=std::min({raw?raw->input.deadlineNs:in.deadlineNs,native_.reserve.deadlineNs,native_.gun.deadlineNs});
  t.weaponFromItemMeters=item;t.weaponFromHandMeters=wrist;
  if(role==MagazinePropRole::Removed&&held_){t.item=held_->token.item;t.handClaim=held_->token;t.handTarget=true;t.deadlineNs=std::min(t.deadlineNs,held_->deadlineNs);}
  return t;
 };
 if(gate_.Phase()==MagazineDetachPhase::Returning||gate_.Phase()==MagazineDetachPhase::Recovering){
  if(held_)hands.Release(in,held_->token);held_.reset();insertion_.reset();out.tracking.target=target(MagazinePropRole::Attached,attached,attachedHand);
  if(pair&&gate_.Finish(native_,*pair)){out.blocksWeaponActions=false;out.tracking={};ResetRecognition(false);}
  lastInput_=std::max(lastInput_,in.sequence);return out;
 }
 const Raw* raw=nullptr;
 if(s.raw.valid&&s.raw.owner==s.nativeOwner&&s.raw.rigFingerprint==equipment->geometry->rigFingerprint&&s.originalHandEvidence&&
  SameInput(s.raw.inputEvidence,*s.originalHandEvidence)&&s.raw.inputEvidence.deadlineNs>in.nowNs&&
  Rigid(s.raw.rawLeftWristWorldMeters)&&Rigid(s.raw.weaponWorldMeters))
   for(const auto& h:history_)if(h&&SameInput(h->input,s.raw.inputEvidence)&&h->gun.token==native_.gun.token){raw=&*h;break;}
 const bool fresh=in.sequence>lastInput_;lastInput_=std::max(lastInput_,in.sequence);
 if(!s.gripPressed||in.released[0]){Cancel(11);++releases_;if(held_)hands.Release(in,held_->token);held_.reset();return out;}
 if(!fresh){out=last_;out.blocksWeaponActions=true;out.tracking.detach=std::make_shared<const MagazineDetachAuthorization>(*auth);
  out.tracking.inputEvidence=in;out.tracking.reserve=native_.reserve;
  if(!MagazineTargetFresh(out.tracking,in.nowNs))out.tracking.target.reset();return out;}
 if(!held_){
  if(!raw||intent==std::numeric_limits<std::uint64_t>::max()){Cancel(12);return out;}
  const auto claim=hands.AcquireFrom(in,raw->input,{in.owner,InteractionHand::Left,HandClaimKind::Mechanism,auth->original.weapon,
    {{0x42433243475250ull,1},raw->input.sequence,raw->input.deadlineNs,true},++intent,native_.gun.token.id});
  if(!claim.accepted||!claim.claim){Cancel(13);return out;}held_=claim.claim;
  grab_=previous_=Multiply(s.raw.rawLeftWristWorldMeters,*InverseRigid(s.raw.weaponWorldMeters));itemFromGrip_=Multiply(grab_,*InverseRigid(attached));
  auto profile=config.insertion;profile.itemFromHand=itemFromGrip_;insertion_.emplace(profile);pulled_=false;++grabs_;
 }
 const auto renewed=hands.Renew(in,held_->token,{{0x42433243475250ull,1},in.sequence,std::min(in.deadlineNs,native_.reserve.deadlineNs),true});
 if(!renewed.accepted||!renewed.claim){Cancel(14);return out;}held_=renewed.claim;
 for(auto& h:history_)if(h&&h->input.sequence==in.sequence)h->held=held_;
 if(!raw||s.raw.inputEvidence.sequence<=lastGeometry_){out=last_;out.blocksWeaponActions=true;
  out.tracking.detach=std::make_shared<const MagazineDetachAuthorization>(*auth);out.tracking.inputEvidence=in;out.tracking.reserve=native_.reserve;
  if(!MagazineTargetFresh(out.tracking,in.nowNs))out.tracking.target.reset();return out;}
 const auto wrist=Multiply(s.raw.rawLeftWristWorldMeters,*InverseRigid(s.raw.weaponWorldMeters));
 // The full/zero-reserve path follows the same extraction/carry boundary as
 // DetachableMagazine. Returning to the well still uses its own rail checks.
 if(!pulled_&&(Distance(wrist,previous_)>config.maxPullStepMeters||Angle(wrist,previous_)>config.insertion.maxStepRadians)){Cancel(15);return out;}
 previous_=wrist;lastGeometry_=s.raw.inputEvidence.sequence;
 const auto axis=Multiply(TravelPose(config.insertion,1),config.insertion.weaponFromEntry);float along=0;
 for(unsigned n=0;n<3;++n)along-=(wrist.values[3][n]-grab_.values[3][n])*(axis.values[3][n]-config.insertion.weaponFromEntry.values[3][n]);
 pulled_|=along>=config.pullMeters;
 const auto item=Multiply(*InverseRigid(itemFromGrip_),wrist);out.tracking.target=target(MagazinePropRole::Removed,item,wrist,raw);
 if(pulled_&&raw->held&&raw->held->token==held_->token&&raw->held->deadlineNs>in.nowNs){
  ReloadInsertionSample contact;contact.identity={in.owner,auth->original.weapon,auth->original.item,in.owner.space};contact.profile=auth->original.profile;
  contact.itemClaim=*raw->held;contact.weaponClaim=raw->gun;contact.sequence=contact.geometrySequence=raw->input.sequence;
  contact.observedNs=raw->input.observedNs;contact.deadlineNs=std::min(raw->input.deadlineNs,native_.reserve.deadlineNs);contact.nowNs=in.nowNs;
  contact.focused=in.focused;contact.itemTracked=contact.weaponTracked=contact.held=contact.eligible=true;
  contact.itemSource=ReloadInsertionItemSource::RetainedMagazine;contact.weaponFromHand=wrist;
  out.interaction.insertion=insertion_->Update(contact);
  if(out.interaction.insertion.guidedItem)out.tracking.target=target(MagazinePropRole::Removed,*out.interaction.insertion.guidedItem,
   Multiply(itemFromGrip_,*out.interaction.insertion.guidedItem),raw);
  if(out.interaction.insertion.seat){
   if(!gate_.Return(native_,*out.interaction.insertion.seat)){Cancel(16);out.tracking.target.reset();return out;}
   ++seats_;hands.Release(in,held_->token);held_.reset();out.tracking.target.reset();out.tracking.detach.reset();
  }
 }
 if(out.tracking.target&&out.tracking.target->role==MagazinePropRole::Removed&&out.interaction.insertion.phase==ReloadInsertionPhase::Free){
  out.tracking.removalFrame=MagazineRemovalFrame(out.tracking,s.raw);
  if(!out.tracking.removalFrame)out.tracking.target.reset();
 }
 out.interaction.original=auth->original;out.ownsLeftHand=held_.has_value();
 if(!MagazineTargetFresh(out.tracking,in.nowNs))out.tracking.target.reset();last_=out;return out;
}
void Bc2MagazineDetached::RecordVisualGap(unsigned kind)noexcept {
 if(kind==1)++visualRetained_;if(kind==2)++visualRejected_;
 if(kind==visualGapKind_)return;visualGapKind_=kind;const auto auth=gate_.Authorization();
 visualGaps_[visualGapTotal_%visualGaps_.size()]={kind,unsigned(gate_.Phase()),sample_.input.sequence,auth?auth->request:0,
  sample_.input.nowNs,last_.tracking.target?last_.tracking.target->deadlineNs:0};++visualGapTotal_;
}
void Bc2MagazineDetached::Report(std::ostream& o)const {
 o<<"\"magazine_detached\":{\"enabled\":"<<enabled_<<",\"phase\":"<<unsigned(gate_.Phase())<<",\"blocks\":"<<gate_.BlocksActions()
  <<",\"grabs\":"<<grabs_<<",\"seats\":"<<seats_<<",\"released\":"<<releases_<<",\"returned\":"<<gate_.Returned()<<",\"recovered\":"<<gate_.Recovered()
  <<",\"pre_read_missing\":"<<preReadMissing_<<",\"pre_read_deferred\":"<<preReadDeferred_<<",\"post_read_missing\":"<<postReadMissing_
  <<",\"cancel_reason\":"<<cancelReason_<<",\"cancel_input\":"<<cancelInput_<<",\"cancel_ns\":"<<cancelNs_
  <<",\"visual_retained\":"<<visualRetained_<<",\"visual_rejected\":"<<visualRejected_<<",\"visual_gap_total\":"<<visualGapTotal_
  <<",\"visual_gap_overwritten\":"<<(visualGapTotal_>visualGaps_.size()?visualGapTotal_-visualGaps_.size():0)<<",\"visual_gaps\":[";
 const auto count=std::min<std::uint64_t>(visualGapTotal_,visualGaps_.size());for(std::uint64_t n=0;n<count;++n){
  const auto& r=visualGaps_[(visualGapTotal_-count+n)%visualGaps_.size()];if(n)o<<',';
  o<<"{\"kind\":"<<r.kind<<",\"phase\":"<<r.phase<<",\"input\":"<<r.input<<",\"request\":"<<r.request<<",\"now_ns\":"<<r.now
   <<",\"original_deadline_ns\":"<<r.deadline<<'}';
 }o<<"]}";
}
}
