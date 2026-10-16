#include "Bc2VehicleShotObserver.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <ostream>
namespace fvr::bc2 {namespace {
struct Code {unsigned rva,size;std::uint64_t hash;};
const Code Proofs[]{
#include "Bc2VehicleShotProof.inc"
};
struct Reader {
    const VehicleRouteMemory& m;bool okay=true;
    VehicleShotDiagnostic* diagnostic=nullptr;
    unsigned Word(std::uint64_t at)noexcept {unsigned v=0;
        if(!m.read||at<0x10000||at+4>UINT32_MAX||!m.read(m.context,unsigned(at),&v,4)){
            okay=false;if(diagnostic&&!diagnostic->firstReadFailure)diagnostic->firstReadFailure=at;}
        return v;}
    bool Type(unsigned at,const char* name)const noexcept{return m.type&&m.type(m.context,at,name);}
};
unsigned Associated(const CampaignVehicleSnapshot& s,unsigned n)noexcept {
    std::array<bool,256> seen{};
    while(n<s.count){if(seen[n])return 0;seen[n]=true;if(s.rows[n].entry)return s.rows[n].entry;n=(s.rows[n].packed>>8)&255;}
    return 0;
}
}
VehicleShotBinding VerifyVehicleShotCode(const VehicleRouteMemory& m,unsigned image)noexcept {
    if(!image||image>UINT32_MAX-0x1800000||!m.read)return {};
    for(const auto& p:Proofs){std::uint64_t hash=14695981039346656037ull;std::array<unsigned char,256> bytes{};
        for(unsigned offset=0;offset<p.size;){const auto n=std::min(unsigned(bytes.size()),p.size-offset);
            if(!m.read(m.context,image+p.rva+offset,bytes.data(),n))return {};
            for(unsigned i=0;i<n;++i){hash^=bytes[i];hash*=1099511628211ull;}offset+=n;}
        if(hash!=p.hash)return {};}
    return {image,true};
}
VehicleShotSample ReadVehicleShot(const VehicleRouteMemory& m,const VehicleShotBinding& b,
    const VehicleShotSource& source,const VehicleRouteSnapshot& seat,const VehicleShotCall& call,std::int64_t now)noexcept {
    VehicleShotSample out;out.nowNs=now;out.owner=seat.identity;
    const auto fail=[&](VehicleShotReason why){out.reason=why;return out;};
    if(!b.verified||!b.image||b.image>UINT32_MAX-0x1800000)return fail(VehicleShotReason::Unverified);
    if(!source.input||!source.space||source.observedNs<=0||source.deadlineNs<=source.observedNs||
       now<source.observedNs||now>=source.deadlineNs||!std::isfinite(source.fire)||source.fire<0||source.fire>1)return fail(VehicleShotReason::Stale);
    const auto& p=source.profile;
    if(!p.weapons||!p.count||p.count>p.rows.size()||seat.identity!=p.owner||seat.player!=source.owner.player||seat.weak!=source.owner.weak||
       seat.fingerprint!=p.profile.routeFingerprint)return fail(VehicleShotReason::Owner);
    if(call.caller!=b.image+0x408258)return fail(VehicleShotReason::Caller);
    if(!RevalidateCampaignVehicleProfile(m,p,seat))return fail(VehicleShotReason::Changed);
    auto& diagnostic=out.diagnostic;Reader r{m,true,&diagnostic};unsigned matches=0;
    const auto leaf=[&](VehicleShotLeaf value,std::uint64_t at,unsigned expected,unsigned observed,bool forceForeign=false){
        diagnostic.leaf=value;diagnostic.address=at;diagnostic.expected=expected;diagnostic.observed=observed;
        return fail(forceForeign||r.okay?VehicleShotReason::Foreign:VehicleShotReason::Read);
    };
    for(unsigned n=0;n<p.count;++n){const auto component=p.rows[n].component;
        if(Associated(p,n)!=seat.identity.entry)continue;
        ++diagnostic.associated;const auto componentTable=r.Word(component);
        if(componentTable!=b.image+0x1030d38)continue;
        const auto mounted=r.Word(std::uint64_t(component)+0xd0);
        const auto effects=r.Word(std::uint64_t(mounted)+0x134);++diagnostic.weaponCandidates;
        if(diagnostic.candidateCount<diagnostic.candidates.size())
            diagnostic.candidates[diagnostic.candidateCount++]={component,componentTable,mounted,effects};
        if(effects!=call.effects)continue;
        ++matches;out.component=component;out.mounted=mounted;out.effects=call.effects;
    }
    diagnostic.matches=matches;
    if(!r.okay)return leaf(VehicleShotLeaf::CandidateRead,diagnostic.firstReadFailure,0,0);
    if(matches!=1)return leaf(VehicleShotLeaf::CandidateCount,0,1,matches);
    if(out.effects<0x10000||out.effects>UINT32_MAX-0x65c)return leaf(VehicleShotLeaf::EffectsExtent,0,0,out.effects);
    const auto data=r.Word(std::uint64_t(out.component)+12),table=r.Word(out.mounted);
    out.firing=r.Word(std::uint64_t(data)+216);out.primary=r.Word(std::uint64_t(out.firing)+64);out.config=call.config;
    const auto effectsTable=r.Word(out.effects),listenerTable=r.Word(std::uint64_t(out.effects)+0xd0);
    diagnostic.componentData=data;diagnostic.mountedTable=table;
    diagnostic.effectsTable=effectsTable;diagnostic.listenerTable=listenerTable;
    if(!r.Type(out.component,"ClientWeaponComponent"))return leaf(VehicleShotLeaf::ComponentType,out.component,1,0);
    if(!r.Type(out.effects,"ClientWeaponFiringEffects"))return leaf(VehicleShotLeaf::EffectsType,out.effects,1,0);
    if(!r.Type(out.firing,"WeaponFiringData"))return leaf(VehicleShotLeaf::FiringType,out.firing,1,0);
    if(!r.Type(out.primary,"FiringFunctionData"))return leaf(VehicleShotLeaf::PrimaryType,out.primary,1,0);
    const auto equal=[&](VehicleShotLeaf value,std::uint64_t at,unsigned expected){
        const auto observed=r.Word(at);if(observed==expected)return true;
        diagnostic.leaf=value;diagnostic.address=at;diagnostic.expected=expected;diagnostic.observed=observed;return false;
    };
    if(!equal(VehicleShotLeaf::MountedUpdate,std::uint64_t(table)+8,b.image+0x3d4c10)||
       !equal(VehicleShotLeaf::MountedInput,std::uint64_t(table)+12,b.image+0x3bf840)||
       !equal(VehicleShotLeaf::MountedFiring,std::uint64_t(out.mounted)+12,out.firing)||
       !equal(VehicleShotLeaf::EffectsFiring,std::uint64_t(out.effects)+16,out.firing)||
       !equal(VehicleShotLeaf::FireAction,std::uint64_t(out.primary)+0x4c,8))return fail(r.okay?VehicleShotReason::Foreign:VehicleShotReason::Read);
    if(std::uint64_t(out.primary)+0xa0!=call.config)return leaf(VehicleShotLeaf::ShotConfig,std::uint64_t(out.primary)+0xa0,unsigned(std::uint64_t(out.primary)+0xa0),call.config);
    if(!equal(VehicleShotLeaf::EffectsShoot,std::uint64_t(effectsTable)+0x4c,b.image+0x41c170)||
       !equal(VehicleShotLeaf::ListenerCallback,std::uint64_t(listenerTable)+12,b.image+0x408220))return fail(r.okay?VehicleShotReason::Foreign:VehicleShotReason::Read);
    // The mounted constructor registers this exact effects subobject as a
    // listener of the embedded firing instance at mounted+4.
    const auto begin=r.Word(std::uint64_t(out.mounted)+4+0x18),end=r.Word(std::uint64_t(out.mounted)+4+0x1c);
    if(begin<0x10000||end<begin||(end-begin)%4||(end-begin)/4>64)return leaf(VehicleShotLeaf::ListenerShape,std::uint64_t(out.mounted)+4+0x18,begin,end,true);
    unsigned listeners=0;for(unsigned at=begin;at<end;at+=4)if(r.Word(at)==out.effects+0xd0)++listeners;
    if(listeners!=1)return leaf(VehicleShotLeaf::ListenerMembership,begin,1,listeners);
    out.queueBegin=r.Word(std::uint64_t(out.effects)+0x650);out.queueEnd=r.Word(std::uint64_t(out.effects)+0x654);
    const auto capacity=r.Word(std::uint64_t(out.effects)+0x658);out.queueTarget=r.Word(std::uint64_t(call.config)+0x44);
    if(out.queueEnd<out.queueBegin||capacity<out.queueEnd||(out.queueEnd-out.queueBegin)%0xb0||
       (out.queueEnd-out.queueBegin)/0xb0>128||(out.queueBegin&&out.queueBegin<0x10000)||(!out.queueBegin&&capacity))return fail(VehicleShotReason::Queue);
    out.queueCount=(out.queueEnd-out.queueBegin)/0xb0;
    if(r.Word(std::uint64_t(out.component)+0xd0)!=out.mounted||r.Word(std::uint64_t(out.mounted)+0x134)!=out.effects||
       r.Word(std::uint64_t(out.mounted)+12)!=out.firing||r.Word(std::uint64_t(out.effects)+16)!=out.firing||
       r.Word(std::uint64_t(out.firing)+64)!=out.primary||r.Word(std::uint64_t(out.effects)+0x650)!=out.queueBegin||
       r.Word(std::uint64_t(out.effects)+0x654)!=out.queueEnd||!RevalidateCampaignVehicleProfile(m,p,seat))return fail(VehicleShotReason::Changed);
    out.reason=r.okay?VehicleShotReason::Okay:VehicleShotReason::Read;return out;
}
bool VehicleShotSame(const VehicleShotSample& a,const VehicleShotSample& b)noexcept {
    return a.reason==VehicleShotReason::Okay&&b.reason==VehicleShotReason::Okay&&a.owner==b.owner&&
      a.component==b.component&&a.mounted==b.mounted&&a.effects==b.effects&&a.firing==b.firing&&a.primary==b.primary&&a.config==b.config;
}
bool VehicleShotQueueGrowth(const VehicleRouteMemory& m,const VehicleShotSample& a,const VehicleShotSample& b)noexcept {
    if(!VehicleShotSame(a,b)||!a.queueTarget||a.queueTarget!=b.queueTarget||b.queueCount<=a.queueCount||b.queueCount>128)return false;
    Reader r{m};for(unsigned i=a.queueCount;i<b.queueCount;++i)if(r.Word(std::uint64_t(b.queueBegin)+i*0xb0)!=b.queueTarget)return false;
    return r.okay&&r.Word(std::uint64_t(b.effects)+0x650)==b.queueBegin&&r.Word(std::uint64_t(b.effects)+0x654)==b.queueEnd;
}
void VehicleShotObserver::Invalidate()noexcept {
    if(observationEpoch_.fetch_add(1,std::memory_order_acq_rel)==UINT64_MAX)
        epochExhausted_.store(true,std::memory_order_release);
}
bool VehicleShotObserver::Current(const VehicleShotSource& s)const noexcept {
    return s.observationEpoch&&!epochExhausted_.load(std::memory_order_acquire)&&
        s.observationEpoch==observationEpoch_.load(std::memory_order_acquire);
}
void VehicleShotObserver::Publish(const VehicleShotSource& s){
    std::unique_lock lock(mutex_,std::try_to_lock);
    if(!lock.owns_lock()){publishContention_.fetch_add(1,std::memory_order_relaxed);return;}
    source_=s;source_.observationEpoch=observationEpoch_.load(std::memory_order_acquire);
    hasSource_=Current(source_);
}
std::optional<VehicleShotSource> VehicleShotObserver::Source(){
    std::unique_lock lock(mutex_,std::try_to_lock);
    if(!lock.owns_lock()){sourceContention_.fetch_add(1,std::memory_order_relaxed);return {};}
    return hasSource_&&Current(source_)?std::optional{source_}:std::nullopt;
}
void VehicleShotObserver::Record(const VehicleShotSource& s,const VehicleShotCall& call,const VehicleShotSample& a,const VehicleShotSample& b,bool queueGrowth){
    std::unique_lock lock(mutex_,std::try_to_lock);
    if(!lock.owns_lock()){recordContention_.fetch_add(1,std::memory_order_relaxed);return;}
    // A transition during native observation cannot inherit the old labels.
    if(!Current(s)){invalidatedRecords_.fetch_add(1,std::memory_order_relaxed);return;}
    auto& j=journal_;++j.total;
    if(a.reason!=VehicleShotReason::Okay||b.reason!=VehicleShotReason::Okay){
        const unsigned bucket=s.fire>0?1u:0u;if(j.rejectionSeen[bucket]!=UINT32_MAX)++j.rejectionSeen[bucket];
        if(j.rejectionKept[bucket]<4)
            j.firstRejected[bucket*4+j.rejectionKept[bucket]++]={s.input,s.space,s.observedNs,s.deadlineNs,s.fire,call,a,b,false};
    }
    if(a.reason!=VehicleShotReason::Okay){++j.rejectedBefore[unsigned(a.reason)];return;}
    if(b.reason!=VehicleShotReason::Okay)++j.rejectedAfter[unsigned(b.reason)];
    if(VehicleShotSame(a,b))++j.matched;
    const bool observedGrowth=queueGrowth&&VehicleShotSame(a,b);if(observedGrowth)++j.queueGrowth;
    if(j.count==j.rows.size()){++j.dropped;return;}j.rows[j.count++]={s.input,s.space,s.observedNs,s.deadlineNs,s.fire,call,a,b,observedGrowth};
}
void VehicleShotObserver::Report(std::ostream& out,bool verified)const {
    Snapshot snapshot;bool available=false;
    {std::unique_lock lock(mutex_,std::try_to_lock);
        if(lock.owns_lock()){snapshot=journal_;available=true;}
        else reportContention_.fetch_add(1,std::memory_order_relaxed);}
    // Never invoke an allocator/stream callback while the journal is locked.
    out<<"{\"read_only\":true,\"new_hook\":false,\"binding_verified\":"<<(verified?"true":"false")
      <<",\"snapshot_available\":"<<(available?"true":"false")
      <<",\"publish_contention\":"<<publishContention_.load(std::memory_order_relaxed)
      <<",\"source_contention\":"<<sourceContention_.load(std::memory_order_relaxed)
      <<",\"record_contention\":"<<recordContention_.load(std::memory_order_relaxed)
      <<",\"report_contention\":"<<reportContention_.load(std::memory_order_relaxed)
      <<",\"invalidated_records\":"<<invalidatedRecords_.load(std::memory_order_relaxed)
      <<",\"queue_attribution_verified\":false,\"projectile_spawn_verified\":false,\"impact_verified\":false";
    if(!available){out<<",\"records\":[]}";return;}
    const auto& j=snapshot;
    out<<",\"callbacks\":"<<j.total<<",\"matched_callbacks\":"<<j.matched
      <<",\"observed_queue_growth\":"<<j.queueGrowth<<",\"dropped\":"<<j.dropped;
    const auto reasons=[&](const char* name,const auto& values){out<<",\""<<name<<"\":[";for(unsigned i=0;i<values.size();++i){if(i)out<<',';out<<values[i];}out<<']';};
    reasons("rejected_before",j.rejectedBefore);reasons("rejected_after",j.rejectedAfter);out<<",\"records\":[";
    for(unsigned n=0;n<j.count;++n){const auto& r=j.rows[n];if(n)out<<',';out<<"{\"input\":"<<r.input<<",\"space\":"<<r.space
      <<",\"source_ns\":"<<r.observed<<",\"deadline_ns\":"<<r.deadline<<",\"source_fire\":"<<r.fire<<",\"caller\":"<<r.call.caller
      <<",\"context\":"<<r.call.context<<",\"before_reason\":"<<unsigned(r.before.reason)<<",\"after_reason\":"<<unsigned(r.after.reason)
      <<",\"before_ns\":"<<r.before.nowNs<<",\"after_ns\":"<<r.after.nowNs<<",\"actor\":"<<r.before.owner.actor
      <<",\"actor_generation\":"<<r.before.owner.actorGeneration<<",\"seat_generation\":"<<r.before.owner.seatGeneration
      <<",\"vehicle\":"<<r.before.owner.controlled<<",\"entry\":"<<r.before.owner.entry<<",\"component\":"<<r.before.component
      <<",\"mounted\":"<<r.before.mounted<<",\"effects\":"<<r.before.effects<<",\"firing\":"<<r.before.firing
      <<",\"primary\":"<<r.before.primary<<",\"config\":"<<r.before.config<<",\"queue_before\":"<<r.before.queueCount
      <<",\"queue_after\":"<<r.after.queueCount<<",\"queue_target\":"<<r.after.queueTarget<<",\"observed_queue_growth\":"<<(r.queueGrowth?"true":"false")<<'}';}
    out<<"],\"rejection_seen_by_source_fire\":["<<j.rejectionSeen[0]<<','<<j.rejectionSeen[1]
      <<"],\"rejection_kept_by_source_fire\":["<<j.rejectionKept[0]<<','<<j.rejectionKept[1]<<"],\"first_rejections\":[";
    const auto details=[&](const VehicleShotSample& s){const auto& d=s.diagnostic;
        out<<"{\"reason\":"<<unsigned(s.reason)<<",\"leaf\":"<<unsigned(d.leaf)<<",\"now_ns\":"<<s.nowNs
          <<",\"actor\":"<<s.owner.actor<<",\"actor_generation\":"<<s.owner.actorGeneration
          <<",\"seat_generation\":"<<s.owner.seatGeneration<<",\"vehicle\":"<<s.owner.controlled<<",\"entry\":"<<s.owner.entry
          <<",\"component\":"<<s.component<<",\"mounted\":"<<s.mounted<<",\"effects\":"<<s.effects
          <<",\"firing\":"<<s.firing<<",\"primary\":"<<s.primary<<",\"config\":"<<s.config
          <<",\"address\":"<<d.address<<",\"expected\":"<<d.expected<<",\"observed\":"<<d.observed
          <<",\"first_read_failure\":"<<d.firstReadFailure<<",\"associated\":"<<d.associated
          <<",\"weapon_candidates\":"<<d.weaponCandidates<<",\"matched_candidates\":"<<d.matches
          <<",\"component_data\":"<<d.componentData<<",\"mounted_table\":"<<d.mountedTable
          <<",\"effects_table\":"<<d.effectsTable<<",\"listener_table\":"<<d.listenerTable<<",\"candidates\":[";
        for(unsigned n=0;n<d.candidateCount;++n){const auto& c=d.candidates[n];if(n)out<<',';
            out<<"{\"component\":"<<c.component<<",\"table\":"<<c.table<<",\"mounted\":"<<c.mounted<<",\"effects\":"<<c.effects<<'}';}
        out<<"]}";
    };
    bool comma=false;for(unsigned bucket=0;bucket<2;++bucket)for(unsigned n=0;n<j.rejectionKept[bucket];++n){
        const auto& r=j.firstRejected[bucket*4+n];if(comma)out<<',';comma=true;
        out<<"{\"input\":"<<r.input<<",\"space\":"<<r.space<<",\"source_ns\":"<<r.observed<<",\"deadline_ns\":"<<r.deadline
          <<",\"source_fire\":"<<r.fire<<",\"caller\":"<<r.call.caller<<",\"context\":"<<r.call.context
          <<",\"call_effects\":"<<r.call.effects<<",\"call_config\":"<<r.call.config<<",\"before\":";details(r.before);
        out<<",\"after\":";details(r.after);out<<'}';}
    out<<"]}";
}
}
