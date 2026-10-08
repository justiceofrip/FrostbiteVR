#pragma once
#include "Bc2PersistentBodyEquipment.h"
#include "fvr/ipc/StagedFrameProducer.h"
#include <atomic>
namespace fvr::bc2 {
struct BodyCarriedPairKey {
    std::uint64_t channel=0,request=0,owner=0,frame=0,device=0,tracking=0,space=0;
    bool operator==(const BodyCarriedPairKey&)const=default;
    bool Valid()const noexcept{return channel&&request&&owner&&frame&&device&&tracking&&space;}
};
inline BodyCarriedPairKey CarriedPairKey(const ipc::FrameLease& lease)noexcept {
    return {lease.channelGeneration,lease.requestId,lease.native.owner,lease.native.frameId,
        lease.native.deviceEpoch,lease.tracking.generation,lease.tracking.spaceGeneration};
}
struct BodyCarriedPairSource {BodyCarriedPairKey key{};BodyCarriedRenderBatch original;};
inline bool BodyCarriedPairCurrent(const BodyCarriedPairSource& pair,const BodyCarriedPairKey& key,
    const BodyCarriedRenderBatch& current,std::int64_t now)noexcept {
    if(!key.Valid()||pair.key!=key||!pair.original.inventory||!current.inventory||
       pair.original.inventory->physicalOwner.space!=key.space||
       current.inventory->sequence<pair.original.inventory->sequence||
       current.inventory->observedNs<pair.original.inventory->observedNs)return false;
    // Reuse the actual typed consumer's complete owner/config/deadline checks.
    // This temporary packet stays on the CPU and grants no new source lease.
    graphics::BodyPropEye validation;
    return AppendBodyCarriedHostInstances(pair.original,current,now,key.space,validation);
}
// One immutable source/world batch per owned image pair. A later gather may
// validate it, but cannot change the second eye's world pose. All callbacks
// share the same atomic snapshot; only eye zero may establish a pair source.
class BodyCarriedPair final {
public:
    void Reset()noexcept {source_.store({},std::memory_order_release);}
    std::shared_ptr<const BodyCarriedPairSource> Read(const BodyCarriedPairKey& key,unsigned eye,
        const BodyCarriedRenderBatch& current,std::int64_t now)noexcept {
        if(eye>1||!key.Valid()||!BodyCarriedBatchFresh(current,now))return {};
        auto source=source_.load(std::memory_order_acquire);
        if(!source){
            // Controller input and render tracking use separate counters. The
            // actual first-eye input remains in original.inventory->sequence;
            // never compare it numerically to key.tracking or replace it later.
            if(eye!=0||current.inventory->physicalOwner.space!=key.space)return {};
            try {
                auto proposed=std::make_shared<const BodyCarriedPairSource>(BodyCarriedPairSource{key,current});
                if(source_.compare_exchange_strong(source,proposed,std::memory_order_acq_rel,std::memory_order_acquire))source=std::move(proposed);
            }catch(...){return {};}
        }
        return source&&BodyCarriedPairCurrent(*source,key,current,now)?source:std::shared_ptr<const BodyCarriedPairSource>{};
    }
private:
    std::atomic<std::shared_ptr<const BodyCarriedPairSource>> source_;
};
inline bool AppendBodyCarriedPair(const BodyCarriedPairSource& original,const BodyCarriedPairKey& key,
    const BodyCarriedRenderBatch& current,std::int64_t now,graphics::BodyPropEye& eye)noexcept {
    return BodyCarriedPairCurrent(original,key,current,now)&&
        AppendBodyCarriedHostInstances(original.original,current,now,key.space,eye);
}
} // namespace fvr::bc2
