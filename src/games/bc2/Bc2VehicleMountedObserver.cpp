#include "Bc2VehicleMountedObserver.h"
#include "Bc2ReloadFlow.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <exception>
#include <ostream>
#include <span>

namespace fvr::bc2 {namespace {
struct Reader {
    const VehicleRouteMemory& memory;VehicleMountedSample& row;bool okay=true;
    bool Bytes(std::uint64_t at,void* out,std::size_t bytes)noexcept {
        if(at<0x10000||at>UINT32_MAX||bytes>UINT32_MAX-at||!memory.read||
           !memory.read(memory.context,unsigned(at),out,bytes)){
            okay=false;if(!row.firstReadFailure)row.firstReadFailure=at<=UINT32_MAX?unsigned(at):UINT32_MAX;return false;
        }return true;
    }
    unsigned Word(std::uint64_t at)noexcept {unsigned out=0;Bytes(at,&out,4);return out;}
    unsigned Byte(std::uint64_t at)noexcept {unsigned char out=0;Bytes(at,&out,1);return out;}
    bool Equal(std::uint64_t at,unsigned expected)noexcept {
        const auto value=Word(at);if(okay&&value==expected)return true;
        row.failedAddress=at<=UINT32_MAX?unsigned(at):UINT32_MAX;row.expected=expected;row.observed=value;return false;
    }
    bool Type(unsigned at,const char* name)const noexcept {
        return memory.type&&memory.type(memory.context,at,name);
    }
};
unsigned Associated(const CampaignVehicleSnapshot& p,unsigned n)noexcept {
    std::array<bool,256> visited{};
    while(n<p.count){if(visited[n])return 0;visited[n]=true;
        if(p.rows[n].entry)return p.rows[n].entry;n=(p.rows[n].packed>>8)&255;
    }return 0;
}
bool Fresh(const VehicleShotSource& s,std::int64_t now)noexcept {
    return s.input&&s.space&&s.observationEpoch&&s.observedNs>0&&s.deadlineNs>s.observedNs&&
        s.deadlineNs-s.observedNs<=150000000&&now>=s.observedNs&&now<s.deadlineNs&&
        std::isfinite(s.fire)&&s.fire>=0&&s.fire<=1;
}
bool SameCall(const OriginalFiringUpdate& a,const OriginalFiringUpdate& b)noexcept {
    return a.firing==b.firing&&a.context==b.context&&a.caller==b.caller&&a.argument==b.argument;
}
bool Signature(const VehicleMountedSample& a,const VehicleMountedSample& b)noexcept {
    return a.reason==b.reason&&a.owner==b.owner&&a.component==b.component&&a.mounted==b.mounted&&
        a.effects==b.effects&&a.firing==b.firing&&a.data==b.data&&a.primary==b.primary&&a.listener==b.listener&&
        a.candidates==b.candidates&&a.matched==b.matched&&a.firstReadFailure==b.firstReadFailure&&
        a.failedAddress==b.failedAddress&&a.expected==b.expected&&a.observed==b.observed&&
        a.contextRead==b.contextRead&&a.contextDecoded==b.contextDecoded&&a.stateRead==b.stateRead&&
        a.flags==b.flags&&a.contextBytes==b.contextBytes&&a.state==b.state&&a.next==b.next&&
        a.loadedRaw==b.loadedRaw&&a.byte8a==b.byte8a;
}
}
VehicleMountedBinding VerifyVehicleMountedCode(const VehicleRouteMemory& m,unsigned image)noexcept {
    // Shared proof covers the complete mounted Update, 6e9000 consumer,
    // listener +24 branch, constructor joins and independent Shoot target.
    if(!VerifyVehicleShotCode(m,image).verified)return {};
    std::array<unsigned char,204> bytes{};
    if(!m.read(m.context,image+0x3d4a40,bytes.data(),bytes.size()))return {};
    std::uint64_t hash=14695981039346656037ull;
    for(auto v:bytes){hash^=v;hash*=1099511628211ull;}
    if(hash!=0xc92904bf6c903a8dull)return {};
    // Redundant exact callsite closure documents the ABI used by the hook.
    constexpr std::array<unsigned char,14> expected{0x6a,0x00,0x8d,0x44,0x24,0x24,0x50,0x8b,0xcb,0xe8,0x01,0x42,0xf1,0xff};
    std::array<unsigned char,14> call{};
    if(!m.read(m.context,image+0x3d4df1,call.data(),call.size())||call!=expected)return {};
    return {image,true};
}
VehicleMountedSample ReadVehicleMounted(const VehicleRouteMemory& memory,const VehicleMountedBinding& b,
    const VehicleShotSource& source,const VehicleRouteSnapshot& seat,const OriginalFiringUpdate& call,
    std::int64_t now,bool stack)noexcept {
    VehicleMountedSample out;out.nowNs=now;out.owner=seat.identity;
    const auto fail=[&](VehicleMountedReason reason){out.reason=reason;return out;};
    if(!b.verified||b.image<0x10000||b.image>UINT32_MAX-0x1800000)return fail(VehicleMountedReason::Unverified);
    if(call.caller!=b.image+0x3d4dff||call.argument)return fail(VehicleMountedReason::Caller);
    if(!Fresh(source,now))return fail(VehicleMountedReason::Source);
    const auto& p=source.profile;
    if(p.profile.role!=VehicleSeatRole::Gunner||seat.slot!=1||!p.weapons||!p.count||p.count>p.rows.size()||
       p.wordCount>p.words.size())return fail(VehicleMountedReason::Profile);
    if(seat.identity!=p.owner||seat.player!=source.owner.player||seat.weak!=source.owner.weak||
       seat.fingerprint!=p.profile.routeFingerprint)return fail(VehicleMountedReason::Owner);
    if(!RevalidateCampaignVehicleProfile(memory,p,seat))return fail(VehicleMountedReason::Changed);
    Reader r{memory,out};
    for(unsigned n=0;n<p.count;++n){
        if(Associated(p,n)!=seat.identity.entry)continue;
        const auto component=p.rows[n].component;
        if(r.Word(component)!=b.image+0x1030d38)continue;
        const auto mounted=r.Word(std::uint64_t(component)+0xd0);++out.candidates;
        if(std::uint64_t(mounted)+4!=call.firing)continue;
        ++out.matched;out.component=component;out.mounted=mounted;out.firing=call.firing;
    }
    if(!r.okay)return fail(VehicleMountedReason::CandidateRead);
    if(out.matched!=1){out.expected=1;out.observed=out.matched;return fail(VehicleMountedReason::CandidateCount);}
    out.componentData=r.Word(std::uint64_t(out.component)+12);
    out.effects=r.Word(std::uint64_t(out.mounted)+0x134);
    out.data=r.Word(std::uint64_t(out.componentData)+216);
    out.primary=r.Word(std::uint64_t(out.data)+64);
    if(!r.okay)return fail(VehicleMountedReason::Join);
    if(!r.Type(out.component,"ClientWeaponComponent")||!r.Type(out.effects,"ClientWeaponFiringEffects")||
       !r.Type(out.data,"WeaponFiringData")||!r.Type(out.primary,"FiringFunctionData"))return fail(VehicleMountedReason::Type);
    const auto table=r.Word(out.mounted),effectsTable=r.Word(out.effects);
    out.listener=out.effects<=UINT32_MAX-0xd0?out.effects+0xd0:0;
    const auto listenerTable=r.Word(out.listener);
    if(!r.Equal(std::uint64_t(table)+8,b.image+0x3d4c10)||
       !r.Equal(std::uint64_t(table)+12,b.image+0x3bf840)||
       !r.Equal(std::uint64_t(out.component)+0xcc,out.effects)||
       !r.Equal(std::uint64_t(out.mounted)+0x13c,out.componentData)||
       !r.Equal(std::uint64_t(out.firing)+8,out.data)||
       !r.Equal(std::uint64_t(out.effects)+16,out.data)||
       !r.Equal(std::uint64_t(out.primary)+0x4c,8)||
       !r.Equal(std::uint64_t(effectsTable)+0x4c,b.image+0x41c170)||
       !r.Equal(std::uint64_t(listenerTable)+12,b.image+0x408220))return fail(VehicleMountedReason::Join);
    const auto begin=r.Word(std::uint64_t(out.firing)+0x18),end=r.Word(std::uint64_t(out.firing)+0x1c);
    if(!r.okay||begin<0x10000||end<begin||(end-begin)%4||(end-begin)/4>64)return fail(VehicleMountedReason::Listener);
    unsigned listeners=0;for(unsigned at=begin;at<end;at+=4)if(r.Word(at)==out.listener)++listeners;
    if(!r.okay||listeners!=1){out.expected=1;out.observed=listeners;return fail(VehicleMountedReason::Listener);}
    if(!stack||call.context%4)return fail(VehicleMountedReason::ContextStack);
    out.contextRead=r.Bytes(call.context,out.context.data(),sizeof(out.context));
    if(!out.contextRead)return fail(VehicleMountedReason::ContextRead);
    const auto context=std::as_bytes(std::span{out.context});
    out.dtBits=out.context[6];out.rawToken=out.context[7];out.scalarBits=out.context[8];out.flags=out.context[11];
    for(unsigned i=0;i<5;++i)out.contextBytes[i]=std::to_integer<unsigned char>(context[0x24+i]);
    out.contextDecoded=DecodeReloadUpdateContext(context).has_value();
    out.state=r.Word(std::uint64_t(out.firing)+0x3c);out.next=r.Word(std::uint64_t(out.firing)+0x44);
    out.timerBits=r.Word(std::uint64_t(out.firing)+0x50);out.loadedRaw=r.Word(std::uint64_t(out.firing)+0x7c);
    out.heatBits=r.Word(std::uint64_t(out.firing)+0x8c);out.byte8a=r.Byte(std::uint64_t(out.firing)+0x8a);
    out.stateRead=r.okay;if(!out.stateRead)return fail(VehicleMountedReason::StateRead);
    if(!r.Equal(std::uint64_t(out.component)+0xd0,out.mounted)||
       !r.Equal(std::uint64_t(out.component)+0xcc,out.effects)||
       !r.Equal(std::uint64_t(out.mounted)+0x134,out.effects)||
       !r.Equal(std::uint64_t(out.mounted)+0x13c,out.componentData)||
       !r.Equal(std::uint64_t(out.firing)+8,out.data)||
       !r.Equal(std::uint64_t(out.effects)+16,out.data)||
       !r.Equal(std::uint64_t(out.data)+64,out.primary)||
       !r.Equal(std::uint64_t(out.firing)+0x18,begin)||!r.Equal(std::uint64_t(out.firing)+0x1c,end)||
       !RevalidateCampaignVehicleProfile(memory,p,seat))return fail(VehicleMountedReason::Changed);
    out.reason=out.contextDecoded?VehicleMountedReason::Okay:VehicleMountedReason::ContextShape;return out;
}
bool VehicleMountedSame(const VehicleMountedSample& a,const VehicleMountedSample& b)noexcept {
    return a.reason==VehicleMountedReason::Okay&&b.reason==VehicleMountedReason::Okay&&a.owner==b.owner&&
        a.component==b.component&&a.componentData==b.componentData&&a.mounted==b.mounted&&a.effects==b.effects&&
        a.firing==b.firing&&a.data==b.data&&a.primary==b.primary&&a.listener==b.listener;
}
bool VehicleMountedObserver::Configure(VehicleMountedBinding binding,VehicleMountedApi api)noexcept {
    if(configured_||!binding.verified||!binding.image||!api.context||!api.memory.read||!api.memory.type||
       !api.source||!api.current||!api.seat||!api.clock||!api.stack)return false;
    binding_=binding;api_=api;configured_=true;return true;
}
OriginalFiringObserver VehicleMountedObserver::Callbacks()noexcept {
    return {this,[](void* p,const OriginalFiringUpdate& c)noexcept{return static_cast<VehicleMountedObserver*>(p)->Enter(c);},
        [](void* p,const OriginalFiringUpdate& c,const OriginalFiringToken& t)noexcept{static_cast<VehicleMountedObserver*>(p)->Leave(c,t);},nullptr};
}
void VehicleMountedObserver::Registration(bool accepted)noexcept {
    registered_.store(accepted,std::memory_order_release);
    if(accepted)everRegistered_.store(true,std::memory_order_release);else registrationRejected_.store(true,std::memory_order_release);
}
void VehicleMountedObserver::Removal(bool removed)noexcept {
    if(removed){registered_.store(false,std::memory_order_release);removed_.store(true,std::memory_order_release);}
}
VehicleMountedSample VehicleMountedObserver::Sample(const VehicleShotSource& source,const OriginalFiringUpdate& call)noexcept {
    VehicleMountedSample row;row.nowNs=api_.clock(api_.context);
    if(!api_.current(api_.context,source)){row.reason=VehicleMountedReason::Changed;return row;}
    VehicleRouteSnapshot seat;
    if(!api_.seat(api_.context,source,seat)){row.reason=VehicleMountedReason::Owner;return row;}
    row=ReadVehicleMounted(api_.memory,binding_,source,seat,call,row.nowNs,api_.stack(api_.context,call.context,0x30));
    row.nowNs=api_.clock(api_.context);
    if(!api_.current(api_.context,source))row.reason=VehicleMountedReason::Changed;
    else if(!Fresh(source,row.nowNs))row.reason=VehicleMountedReason::Source;
    return row;
}
OriginalFiringToken VehicleMountedObserver::Enter(const OriginalFiringUpdate& call)noexcept {
    callbacks_.fetch_add(1,std::memory_order_relaxed);
    if(!configured_||!registered_.load(std::memory_order_acquire)||call.caller!=binding_.image+0x3d4dff)return {};
    callerCalls_.fetch_add(1,std::memory_order_relaxed);
    if(exhausted_.load(std::memory_order_acquire))return {};
    unsigned index=0;
    for(;index<slots_.size();++index){unsigned expected=0;
        if(slots_[index].state.compare_exchange_strong(expected,1,std::memory_order_acquire))break;
    }
    if(index==slots_.size()){slotDrops_.fetch_add(1,std::memory_order_relaxed);return {};}
    auto& slot=slots_[index];
    const auto release=[&]{slot.state.store(0,std::memory_order_release);return OriginalFiringToken{};};
    if(!api_.source(api_.context,slot.source)){noSource_.fetch_add(1,std::memory_order_relaxed);return release();}
    if(slot.source.profile.profile.role!=VehicleSeatRole::Gunner){notPrivateSeat_.fetch_add(1,std::memory_order_relaxed);return release();}
    const unsigned bucket=slot.source.fire>0?1u:0u;
    const auto seen=sourceCalls_[bucket].fetch_add(1,std::memory_order_relaxed);
    // Strict callback read budgets, independent of record retention and Fire.
    if(seen>=(bucket?512u:4096u)){budgetDrops_[bucket].fetch_add(1,std::memory_order_relaxed);return release();}
    const auto serial=serial_.fetch_add(1,std::memory_order_relaxed);
    if(serial==UINT64_MAX){exhausted_.store(true,std::memory_order_release);return release();}
    const auto& source=slot.source;slot.row={};slot.row.serial=serial+1;
    slot.row.input=source.input;slot.row.space=source.space;slot.row.epoch=source.observationEpoch;
    slot.row.sourceOwner=source.owner;slot.row.sourceSeat=source.profile.owner;slot.row.routeFingerprint=source.profile.profile.routeFingerprint;
    slot.row.sourceNs=source.observedNs;slot.row.deadlineNs=source.deadlineNs;slot.row.sourceFire=source.fire;slot.row.call=call;
    slot.row.before=Sample(source,call);slot.uncaught=std::uncaught_exceptions();
    // Keep rejected entries through leave as well; the original still runs.
    inFlight_.fetch_add(1,std::memory_order_relaxed);slot.state.store(2,std::memory_order_release);
    OriginalFiringToken token;token.words[0]=index+1;token.words[1]=serial+1;return token;
}
void VehicleMountedObserver::Leave(const OriginalFiringUpdate& call,const OriginalFiringToken& token)noexcept {
    if(!token.words[0])return;
    if(token.words[0]>slots_.size()){badTokens_.fetch_add(1,std::memory_order_relaxed);return;}
    auto& slot=slots_[std::size_t(token.words[0]-1)];
    unsigned expected=2;
    if(!slot.state.compare_exchange_strong(expected,3,std::memory_order_acquire)){
        badTokens_.fetch_add(1,std::memory_order_relaxed);return;
    }
    if(slot.row.serial!=token.words[1]||!SameCall(slot.row.call,call)){
        // Restore the active slot for its actual matching leave; never borrow it.
        badTokens_.fetch_add(1,std::memory_order_relaxed);slot.state.store(2,std::memory_order_release);return;
    }
    slot.row.scopeReturned=std::uncaught_exceptions()==slot.uncaught;
    if(!slot.row.scopeReturned)unwound_.fetch_add(1,std::memory_order_relaxed);
    slot.row.after=Sample(slot.source,call);
    slot.row.same=slot.row.scopeReturned&&VehicleMountedSame(slot.row.before,slot.row.after);
    if(slot.row.same)matched_.fetch_add(1,std::memory_order_relaxed);
    if(slot.row.before.reason==VehicleMountedReason::Okay){
        ((slot.row.before.flags&1)?contextFireOne_:contextFireZero_).fetch_add(1,std::memory_order_relaxed);
        (slot.row.before.contextBytes[0]?byte24Nonzero_:byte24Zero_).fetch_add(1,std::memory_order_relaxed);
        const unsigned sourceBucket=slot.row.sourceFire>0?1u:0u;
        nativeFireBySource_[sourceBucket][slot.row.before.flags&1].fetch_add(1,std::memory_order_relaxed);
        flag24BySource_[sourceBucket][slot.row.before.contextBytes[0]?1u:0u].fetch_add(1,std::memory_order_relaxed);
    }
    Record(slot.row);leaves_.fetch_add(1,std::memory_order_relaxed);
    inFlight_.fetch_sub(1,std::memory_order_relaxed);slot.state.store(0,std::memory_order_release);
}
void VehicleMountedObserver::Record(const Row& row)noexcept {
    if(journalLock_.test_and_set(std::memory_order_acquire)){journalContention_.fetch_add(1,std::memory_order_relaxed);return;}
    auto& j=journal_;++j.beforeReasons[unsigned(row.before.reason)];++j.afterReasons[unsigned(row.after.reason)];
    const unsigned bucket=row.same?(row.sourceFire>0?1u:0u):2u;++j.seen[bucket];
    bool same=false;
    if(bucket!=1&&j.kept[bucket]){const auto& prior=j.rows[bucket][j.kept[bucket]-1];
        same=prior.epoch==row.epoch&&prior.space==row.space&&prior.sourceFire==row.sourceFire&&
            prior.call.firing==row.call.firing&&prior.call.caller==row.call.caller&&prior.call.argument==row.call.argument&&
            prior.scopeReturned==row.scopeReturned&&Signature(prior.before,row.before)&&Signature(prior.after,row.after);
    }
    if(same)++j.collapsed[bucket];
    else if(j.kept[bucket]==j.rows[bucket].size())++j.dropped[bucket];
    else j.rows[bucket][j.kept[bucket]++]=row;
    journalLock_.clear(std::memory_order_release);
}
void VehicleMountedObserver::Report(std::ostream& out)const {
    Journal copy;const bool available=!journalLock_.test_and_set(std::memory_order_acquire);
    if(available){copy=journal_;journalLock_.clear(std::memory_order_release);}
    out<<"{\"private_sp04_seat1\":true,\"map_verified_by_observer\":false,\"read_only\":true,\"new_hook\":false,\"input_copy\":false"
       <<",\"shoot_callback_attribution_independent\":true,\"discharge_verified\":false,\"impact_verified\":false"
       <<",\"configured\":"<<(configured_?"true":"false")<<",\"binding_verified\":"<<(binding_.verified?"true":"false")
       <<",\"registered_once\":"<<(everRegistered_.load()?"true":"false")<<",\"registration_rejected\":"<<(registrationRejected_.load()?"true":"false")
       <<",\"removed_after_drain\":"<<(removed_.load()?"true":"false")<<",\"serial_exhausted\":"<<(exhausted_.load()?"true":"false")
       <<",\"snapshot_available\":"<<(available?"true":"false")
       <<",\"callbacks\":"<<callbacks_.load()<<",\"expected_caller_calls\":"<<callerCalls_.load()<<",\"no_current_source\":"<<noSource_.load()
       <<",\"not_private_gunner\":"<<notPrivateSeat_.load()<<",\"source_calls\":["<<sourceCalls_[0].load()<<','<<sourceCalls_[1].load()<<']'
       <<",\"read_budget_drops\":["<<budgetDrops_[0].load()<<','<<budgetDrops_[1].load()<<']'
       <<",\"slot_drops\":"<<slotDrops_.load()<<",\"scope_leaves\":"<<leaves_.load()<<",\"bad_tokens\":"<<badTokens_.load()
       <<",\"in_flight\":"<<inFlight_.load()<<",\"matched_update_scopes\":"<<matched_.load()<<",\"unwound_scopes\":"<<unwound_.load()
       <<",\"context_fire_counts\":["<<contextFireZero_.load()<<','<<contextFireOne_.load()<<']'
       <<",\"byte24_counts\":["<<byte24Zero_.load()<<','<<byte24Nonzero_.load()<<']'
       <<",\"native_fire_by_source\":[["<<nativeFireBySource_[0][0].load()<<','<<nativeFireBySource_[0][1].load()
       <<"],["<<nativeFireBySource_[1][0].load()<<','<<nativeFireBySource_[1][1].load()<<"]]"
       <<",\"byte24_by_source\":[["<<flag24BySource_[0][0].load()<<','<<flag24BySource_[0][1].load()
       <<"],["<<flag24BySource_[1][0].load()<<','<<flag24BySource_[1][1].load()<<"]]"
       <<",\"journal_contention\":"<<journalContention_.load();
    if(!available){out<<",\"records\":[]}";return;}
    const auto values=[&](const char* name,const auto& items){out<<",\""<<name<<"\":[";bool comma=false;
        for(const auto value:items){if(comma)out<<',';comma=true;out<<value;}out<<']';};
    values("before_reasons",copy.beforeReasons);values("after_reasons",copy.afterReasons);
    values("seen_by_cohort",copy.seen);values("kept_by_cohort",copy.kept);values("collapsed_by_cohort",copy.collapsed);values("dropped_by_cohort",copy.dropped);
    const auto sample=[&](const VehicleMountedSample& s){
        out<<"{\"reason\":"<<unsigned(s.reason)<<",\"ns\":"<<s.nowNs<<",\"actor\":"<<s.owner.actor<<",\"actor_generation\":"<<s.owner.actorGeneration
          <<",\"seat_generation\":"<<s.owner.seatGeneration<<",\"vehicle\":"<<s.owner.controlled<<",\"entry\":"<<s.owner.entry
          <<",\"router\":"<<s.owner.router<<",\"cache\":"<<s.owner.cache<<",\"component\":"<<s.component<<",\"component_data\":"<<s.componentData
          <<",\"mounted\":"<<s.mounted<<",\"effects\":"<<s.effects<<",\"firing\":"<<s.firing<<",\"firing_data\":"<<s.data
          <<",\"primary\":"<<s.primary<<",\"listener\":"<<s.listener<<",\"candidates\":"<<s.candidates<<",\"matched\":"<<s.matched
          <<",\"first_read_failure\":"<<s.firstReadFailure<<",\"failed_address\":"<<s.failedAddress<<",\"expected\":"<<s.expected<<",\"observed\":"<<s.observed
          <<",\"context_read\":"<<(s.contextRead?"true":"false")<<",\"context_decoded\":"<<(s.contextDecoded?"true":"false")
          <<",\"state_read\":"<<(s.stateRead?"true":"false")<<",\"dt_bits\":"<<s.dtBits<<",\"raw_token\":"<<s.rawToken
          <<",\"scalar_bits\":"<<s.scalarBits<<",\"flags\":"<<s.flags<<",\"state\":"<<s.state<<",\"next\":"<<s.next
          <<",\"timer_bits\":"<<s.timerBits<<",\"loaded_raw\":"<<s.loadedRaw<<",\"heat_bits\":"<<s.heatBits<<",\"byte8a\":"<<s.byte8a;
        float dt=0;std::memcpy(&dt,&s.dtBits,4);out<<",\"dt_seconds\":";
        if(s.contextRead&&std::isfinite(dt))out<<dt;else out<<"null";
        values("context_words",s.context);values("bytes24_through28",s.contextBytes);out<<'}';
    };
    out<<",\"records\":[";bool comma=false;
    for(unsigned bucket=0;bucket<3;++bucket)for(unsigned n=0;n<copy.kept[bucket];++n){const auto& r=copy.rows[bucket][n];
        if(comma)out<<',';comma=true;
        out<<"{\"cohort\":"<<bucket<<",\"serial\":"<<r.serial<<",\"input\":"<<r.input<<",\"space\":"<<r.space<<",\"epoch\":"<<r.epoch
          <<",\"expected_manager\":"<<r.sourceOwner.manager<<",\"expected_player\":"<<r.sourceOwner.player
          <<",\"expected_soldier\":"<<r.sourceOwner.soldier<<",\"expected_weak\":"<<r.sourceOwner.weak
          <<",\"expected_actor\":"<<r.sourceSeat.actor<<",\"expected_actor_generation\":"<<r.sourceSeat.actorGeneration
          <<",\"expected_seat_generation\":"<<r.sourceSeat.seatGeneration<<",\"expected_vehicle\":"<<r.sourceSeat.controlled
          <<",\"expected_entry\":"<<r.sourceSeat.entry<<",\"expected_router\":"<<r.sourceSeat.router
          <<",\"expected_cache\":"<<r.sourceSeat.cache<<",\"route_fingerprint\":"<<r.routeFingerprint
          <<",\"source_ns\":"<<r.sourceNs<<",\"deadline_ns\":"<<r.deadlineNs<<",\"source_fire\":";
        if(std::isfinite(r.sourceFire))out<<r.sourceFire;else out<<"null";
        out<<",\"call_firing\":"<<r.call.firing<<",\"context\":"<<r.call.context<<",\"caller\":"<<r.call.caller<<",\"argument\":"<<r.call.argument
          <<",\"scope_returned\":"<<(r.scopeReturned?"true":"false")<<",\"same_identity\":"<<(r.same?"true":"false")<<",\"before\":";
        sample(r.before);out<<",\"after\":";sample(r.after);out<<'}';
    }out<<"]}";
}
}
