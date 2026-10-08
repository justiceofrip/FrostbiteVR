#include "FiringPoseHistory.h"
namespace fvr::interaction {
std::optional<FiringPoseSample> FiringPoseHistory::Resolve(const FiringPoseKey& key,
 const FiringPoseSample& sample,std::int64_t now) noexcept {
 if(!key.actor||!key.ownerGeneration||!key.equipped||!key.space||!sample.generation||
    now<=0||sample.deadline<=now||!InverseAnimatedTransform(sample.muzzle))return {};
 if(key.actor!=owner_.actor||key.ownerGeneration!=owner_.ownerGeneration||
    key.equipped!=owner_.equipped||key.space!=owner_.space){Reset();owner_=key;}
 for(const auto& entry:entries_)if(entry.created&&entry.key==key){
  // An expired event must never be moved to a newer animation/tracking pose.
  if(now<entry.created||now>=entry.sample.deadline)return {};
  return entry.sample;
 }
 entries_[next_]={key,sample,now};next_=(next_+1)%unsigned(entries_.size());return sample;
}
}
