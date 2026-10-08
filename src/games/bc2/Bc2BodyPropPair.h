#pragma once
#include "Bc2BodyAmmoHost.h"
#include "Bc2BodyCarriedPair.h"
namespace fvr::bc2 {
struct BodyPropSources {
    std::optional<BodyAmmoRenderSource> ammo;
    std::optional<BodyHolsteredRenderSource> holstered;
    std::optional<BodyCarriedRenderBatch> carried;
};
struct BodyPropPairSource {BodyCarriedPairKey key{};BodyPropSources original;};

// Both current reads must validate the original typed authority. Preserve its
// world transform and intersect all three leases; missing evidence hides only
// that source and never renews the original first-eye snapshot.
inline graphics::BodyPropEye BuildBodyPropPairEye(const BodyPropPairSource& pair,
    const BodyPropSources& before,const BodyPropSources& after,graphics::BodyPropEye eye,std::int64_t now)noexcept {
    eye.count=0;eye.instances={};const auto& original=pair.original;
    if(!pair.key.Valid()||!graphics::RigidPropClipTransform(interaction::reload_insertion_detail::Identity(),eye.view,eye.projection))return eye;
    const auto append=[&](std::optional<graphics::BodyPropInstance> a,std::optional<graphics::BodyPropInstance> b){
        if(!a||!b||eye.count==graphics::MaxBodyProps||!graphics::SameBodyPropSource(*a,*b)||a->spaceGeneration!=pair.key.space)return;
        a->observedNs=std::max(a->observedNs,b->observedNs);a->deadlineNs=std::min(a->deadlineNs,b->deadlineNs);
        if(graphics::BodyPropFresh(*a,now))eye.instances[eye.count++]=*a;
    };
    if(original.ammo&&before.ammo&&after.ammo)
        append(BodyAmmoHostInstance(*original.ammo,*before.ammo,now),BodyAmmoHostInstance(*original.ammo,*after.ammo,now));
    if(original.holstered&&before.holstered&&after.holstered)
        append(BodyHolsteredHostInstance(*original.holstered,*before.holstered,now),BodyHolsteredHostInstance(*original.holstered,*after.holstered,now));
    if(original.carried&&before.carried&&after.carried){graphics::BodyPropEye a,b;
        if(AppendBodyCarriedHostInstances(*original.carried,*before.carried,now,pair.key.space,a)&&
           AppendBodyCarriedHostInstances(*original.carried,*after.carried,now,pair.key.space,b)&&a.count==b.count&&
           a.count<=graphics::MaxBodyProps-eye.count)
            for(unsigned n=0;n<a.count;++n)append(a.instances[n],b.instances[n]);
    }
    return eye;
}

// An optional earlier source may disappear between eyes. Join by the complete
// namespaced wire identity, not dense packet offset, then compact BOTH eyes in
// the same order. A missing/revoked source cannot shift an unrelated item.
inline std::array<graphics::BodyPropEye,2> IntersectBodyPropPair(
    const graphics::BodyPropEye& first,const graphics::BodyPropEye& second,std::uint64_t space,std::int64_t now)noexcept {
    std::array<graphics::BodyPropEye,2> out{first,second};
    for(auto& e:out){e.count=0;e.instances={};} // Preserve each eye's view and optional ammo HUD sample.
    const auto valid=[&](const graphics::BodyPropEye& e){
        if(e.count>graphics::MaxBodyProps||e.reserved)return false;
        for(unsigned n=0;n<e.count;++n)for(unsigned k=0;k<n;++k)
            if(graphics::SameBodyPropSource(e.instances[n],e.instances[k]))return false;
        return true;
    };
    if(!space||!valid(first)||!valid(second))return out;
    for(unsigned a=0;a<first.count;++a)for(unsigned b=0;b<second.count;++b){
        auto item=first.instances[a];const auto& current=second.instances[b];
        if(!graphics::SameBodyPropSource(item,current)||item.spaceGeneration!=space||
           item.world.values!=current.world.values||!graphics::BodyPropFresh(item,now)||!graphics::BodyPropFresh(current,now))continue;
        item.observedNs=std::max(item.observedNs,current.observedNs);item.deadlineNs=std::min(item.deadlineNs,current.deadlineNs);
        if(graphics::BodyPropFresh(item,now))for(auto& e:out)e.instances[e.count++]=item;
        break;
    }
    return out;
}

class BodyPropPair final {
public:
    void Reset()noexcept {source_.store({},std::memory_order_release);first_.store({},std::memory_order_release);}
    std::shared_ptr<const BodyPropPairSource> Begin(const BodyCarriedPairKey& key,unsigned eye,const BodyPropSources& before)noexcept {
        if(eye>1||!key.Valid())return {};
        auto source=source_.load(std::memory_order_acquire);
        if(!source&&eye==0)try {
            auto proposed=std::make_shared<const BodyPropPairSource>(BodyPropPairSource{key,before});
            if(source_.compare_exchange_strong(source,proposed,std::memory_order_acq_rel,std::memory_order_acquire))source=std::move(proposed);
        }catch(...){return {};}
        return source&&source->key==key?source:std::shared_ptr<const BodyPropPairSource>{};
    }
    std::optional<std::array<graphics::BodyPropEye,2>> End(const std::shared_ptr<const BodyPropPairSource>& source,
        const BodyCarriedPairKey& key,unsigned eye,const BodyPropSources& before,const BodyPropSources& after,
        graphics::BodyPropEye view,std::int64_t now)noexcept {
        if(!source||eye>1||source->key!=key||source_.load(std::memory_order_acquire)!=source)return {};
        const auto props=BuildBodyPropPairEye(*source,before,after,view,now);
        if(eye==0){
            try {auto complete=std::make_shared<const FirstEye>(FirstEye{source,props});std::shared_ptr<const FirstEye> absent;
                first_.compare_exchange_strong(absent,complete,std::memory_order_release,std::memory_order_relaxed);
            }catch(...){}
            return {};
        }
        const auto first=first_.load(std::memory_order_acquire);
        if(!first||first->source!=source||source_.load(std::memory_order_acquire)!=source)return {};
        return IntersectBodyPropPair(first->props,props,key.space,now);
    }
private:
    struct FirstEye {std::shared_ptr<const BodyPropPairSource> source;graphics::BodyPropEye props;};
    std::atomic<std::shared_ptr<const BodyPropPairSource>> source_;
    std::atomic<std::shared_ptr<const FirstEye>> first_;
};
} // namespace fvr::bc2
