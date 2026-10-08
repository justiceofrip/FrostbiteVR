#include "Bc2SelectedMeshesObservation.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <limits>

namespace fvr::bc2 {
bool SelectedMeshesObservation::Install(std::span<const std::byte> bytes,const engine::PeImage& pe,
    std::uint32_t base,ReloadStateMemory memory,bool enabled,bool persistent)noexcept {
    std::lock_guard lock(mutex_);
    ++generation_;binding_.reset();published_.store({});carried_={};counters_={};lastOwner_={};lastSequence_=0;lastObservedNs_=0;
    identities_={};identityCount_=0;identityDropped_=0;
    if(!enabled)return false;
    counters_.status=SelectedObservationStatus::BindingUnavailable;
    if(!memory.read||bytes.empty())return false;
    try {
        auto owned=std::make_shared<BindingStorage>();
        owned->executable.assign(bytes.begin(),bytes.end());
        const auto found=DiscoverSelectedMeshes1p(owned->executable,pe);
        if(!found||base!=found->preferredBase)return false;
        owned->binding=*found;owned->imageBase=base;owned->memory=memory;
        binding_=std::move(owned);counters_.persistent=persistent;counters_.status=SelectedObservationStatus::Ready;return true;
    }catch(...){return false;}
}
void SelectedMeshesObservation::Clear()noexcept {
    std::lock_guard lock(mutex_);++generation_;++counters_.clears;published_.store({});carried_={};
    // Preserve attempt/time high-water marks; Clear cannot enable duplicate
    // source packets or reset the bounded diagnostic read budget.
    counters_.status=binding_?SelectedObservationStatus::Cancelled:SelectedObservationStatus::Disabled;
}
bool SelectedMeshesObservation::Observe(const ReloadStateSnapshot& source,std::int64_t now)noexcept {
    if(observing_.test_and_set()){
        std::lock_guard lock(mutex_);++counters_.busy;return false;
    }
    struct Admission {std::atomic_flag& flag;~Admission(){flag.clear();}} admission{observing_};
    std::shared_ptr<const BindingStorage> binding;std::uint64_t generation=0;
    const auto& o=source.owner;
    {
        std::lock_guard lock(mutex_);
        if(!binding_)return false;
        if(!o.player||!o.soldier||!o.weak||!o.weapon||!o.actorGeneration||!o.equipGeneration||!o.space||
            !source.sequence||source.observedNs<=0||source.observedNs>now||
            source.observedNs>std::numeric_limits<std::int64_t>::max()-LeaseNs||now-source.observedNs>=LeaseNs||
            !source.config.weaponData||!source.inventory||!source.config.assetName[0]||
            !std::memchr(source.config.assetName.data(),0,source.config.assetName.size())){
            ++counters_.stale;counters_.status=SelectedObservationStatus::InvalidSource;published_.store({});return false;
        }
        if(source.observedNs<lastObservedNs_||(o==lastOwner_&&source.sequence<=lastSequence_)){
            ++counters_.stale;return false; // stale queued work cannot replace newer immutable publication
        }
        if(!SourceObservationDue(lastObservedNs_,source.observedNs)){
            ++counters_.throttled;counters_.status=SelectedObservationStatus::Throttled;
            if(o!=lastOwner_)published_.store({});return false;
        }
        if(!counters_.persistent&&counters_.attempts>=MaxObservations){counters_.status=SelectedObservationStatus::BudgetExhausted;published_.store({});return false;}
        lastOwner_=o;lastSequence_=source.sequence;lastObservedNs_=source.observedNs;
        ++counters_.attempts;generation=generation_;binding=binding_;
        const auto previous=published_.load();
        if(previous&&previous->owner!=o)published_.store({});
    }
    const auto started=std::chrono::steady_clock::now();
    const auto result=ReadSelectedMeshes1p(binding->memory,binding->binding,binding->imageBase,o,source.sequence,
        source.observedNs,source.observedNs+LeaseNs,true);
    const auto elapsed=std::max<std::int64_t>(0,std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now()-started).count());
    std::lock_guard lock(mutex_);
    if(binding_!=binding)return false;
    counters_.lastDurationNs=std::uint64_t(elapsed);counters_.totalDurationNs+=std::uint64_t(elapsed);
    counters_.maxDurationNs=std::max(counters_.maxDurationNs,std::uint64_t(elapsed));
    counters_.lastReadCalls=result.readCalls;counters_.lastReadBytes=result.readBytes;
    counters_.readCalls+=result.readCalls;counters_.readBytes+=result.readBytes;counters_.readerStatus=result.status;
    if(generation_!=generation)return false;
    if(!result.snapshot){counters_.status=SelectedObservationStatus::ReadRejected;published_.store({});return false;}
    const auto& s=*result.snapshot;
    const auto pathEnd=std::find(source.config.assetPath.begin(),source.config.assetPath.end(),'\0');
    const bool pathMismatch=s.configurationPathVerified&&source.config.assetPath[0]&&
        (pathEnd==source.config.assetPath.end()||
         std::string_view(source.config.assetPath.data(),std::size_t(pathEnd-source.config.assetPath.begin()))!=
         std::string_view(s.configurationPath.data()));
    if(s.weaponData!=source.config.weaponData||s.inventory!=source.inventory||s.selectedSlot!=source.selectedSlot||
        std::strcmp(s.weaponName.data(),source.config.assetName.data())||pathMismatch){
        counters_.status=SelectedObservationStatus::SourceMismatch;published_.store({});return false;
    }
    // Duration measured on an independent monotonic clock can only shorten
    // acceptance; never restamp the original observation or extend its deadline.
    if(elapsed>=s.deadlineNs-now){counters_.status=SelectedObservationStatus::ExpiredDuringRead;published_.store({});return false;}
    try {
        const auto snapshot=std::make_shared<const SelectedMeshesSnapshot>(s);
        const auto* previous=identityCount_?identities_[identityCount_-1].snapshot.get():nullptr;
        const bool same=previous&&identities_[identityCount_-1].generation==generation_&&
            previous->owner==s.owner&&previous->weaponData==s.weaponData&&previous->stateTypeInfo==s.stateTypeInfo&&previous->meshTypeInfo==s.meshTypeInfo&&
            previous->inventory==s.inventory&&previous->selectedSlot==s.selectedSlot&&previous->weaponName==s.weaponName&&
            previous->soleConfiguredArray==s.soleConfiguredArray&&previous->stateCount==s.stateCount&&previous->states==s.states&&
            previous->configurationPathPointer==s.configurationPathPointer&&previous->configurationPathVerified==s.configurationPathVerified&&previous->configurationPath==s.configurationPath;
        if(same){auto& row=identities_[identityCount_-1];row.snapshot=snapshot;++row.observations;}
        else{
            if(identityCount_==IdentityCapacity){std::move(identities_.begin()+1,identities_.end(),identities_.begin());--identityCount_;++identityDropped_;}
            identities_[identityCount_++]={snapshot,generation_,s.sequence,1};
        }
        published_.store(snapshot);
    }
    catch(...){counters_.status=SelectedObservationStatus::ReadRejected;published_.store({});return false;}
    ++counters_.published;counters_.status=SelectedObservationStatus::Observed;return true;
}
std::shared_ptr<const SelectedMeshesSnapshot> SelectedMeshesObservation::Read(const ReloadStateOwner& owner,std::int64_t now)const noexcept {
    auto current=published_.load();
    if(!current||current->owner!=owner||now<current->observedNs||now>=current->deadlineNs)return {};
    return current;
}
std::shared_ptr<const SelectedMeshesSnapshot> SelectedMeshesObservation::ReadCurrent(std::int64_t now)const noexcept {
    auto current=published_.load();
    if(!current||now<current->observedNs||now>=current->deadlineNs)return {};
    return current;
}
std::shared_ptr<const CarriedMeshesSnapshot> SelectedMeshesObservation::ReadCarried(const ReloadStateOwner& owner,
    std::uint32_t inventory,std::uint32_t slot,std::uint32_t weapon,std::uint32_t data,std::uint32_t persistence,
    std::uint64_t sequence,std::int64_t observed,std::int64_t now)noexcept {
    if(!sequence||observed<=0||observed>now||now-observed>=100000000||observed>INT64_MAX-LeaseNs)return {};
    std::shared_ptr<const BindingStorage> binding;std::uint64_t generation=0;
    {
        std::lock_guard lock(mutex_);if(!binding_)return {};
        for(const auto& old:carried_)if(old&&old->weapon==weapon&&old->nativeSlot==slot&&old->persistence==persistence&&
            old->configured.owner==owner&&old->configured.inventory==inventory&&old->configured.weaponData==data&&
            old->configured.observedNs<=now&&now-old->configured.observedNs<CadenceNs&&old->configured.deadlineNs>now)return old;
        binding=binding_;generation=generation_;
    }
    const auto begin=std::chrono::steady_clock::now();
    const auto r=ReadCarriedMeshes1p(binding->memory,binding->binding,binding->imageBase,owner,inventory,slot,weapon,data,persistence,
        sequence,observed,observed+LeaseNs,true);
    const auto elapsed=std::max<std::int64_t>(0,std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-begin).count());
    std::lock_guard lock(mutex_);if(generation_!=generation||binding_!=binding||!r.snapshot||elapsed>=r.snapshot->configured.deadlineNs-now)return {};
    try {
        auto out=std::make_shared<const CarriedMeshesSnapshot>(*r.snapshot);
        auto at=std::find_if(carried_.begin(),carried_.end(),[&](const auto& c){return !c||c->weapon==weapon||c->configured.owner!=owner||c->configured.deadlineNs<=now;});
        if(at==carried_.end())return {};*at=out;return out;
    }catch(...){return {};}
}
SelectedObservationCounters SelectedMeshesObservation::Counters()const noexcept {
    std::lock_guard lock(mutex_);return counters_;
}
void SelectedMeshesObservation::Report(std::ostream& out)const {
    const auto c=Counters();const auto latest=published_.load();
    std::array<IdentitySample,IdentityCapacity> identities;unsigned count=0;std::uint64_t dropped=0;
    {std::lock_guard lock(mutex_);identities=identities_;count=identityCount_;dropped=identityDropped_;}
    const auto text=[&](const auto& chars){out<<'"';for(const unsigned char ch:chars){if(!ch)break;if(ch=='"'||ch=='\\')out<<'\\';if(ch<32)out<<'?';else out<<char(ch);}out<<'"';};
    out<<"{\"configured_only\":true,\"active_state_verified\":false,\"submitted_skin_verified\":false,\"render_authority\":false"
        <<",\"persistent\":"<<(c.persistent?"true":"false")
        <<",\"status\":"<<unsigned(c.status)<<",\"reader_status\":"<<unsigned(c.readerStatus)
        <<",\"attempts\":"<<c.attempts<<",\"published\":"<<c.published<<",\"clears\":"<<c.clears
        <<",\"throttled\":"<<c.throttled<<",\"stale\":"<<c.stale<<",\"busy\":"<<c.busy
        <<",\"read_calls\":"<<c.readCalls<<",\"read_bytes\":"<<c.readBytes
        <<",\"last_read_calls\":"<<c.lastReadCalls<<",\"last_read_bytes\":"<<c.lastReadBytes
        <<",\"total_duration_ns\":"<<c.totalDurationNs<<",\"max_duration_ns\":"<<c.maxDurationNs
        <<",\"last_duration_ns\":"<<c.lastDurationNs<<",\"max_observations\":"<<MaxObservations
        <<",\"lease_ns\":"<<LeaseNs<<",\"cached_sequence\":"<<(latest?latest->sequence:0)
        <<",\"cached_observed_ns\":"<<(latest?latest->observedNs:0)<<",\"cached_deadline_ns\":"<<(latest?latest->deadlineNs:0)
        <<",\"cached_configured_array\":"<<(latest?latest->soleConfiguredArray:0)
        <<",\"configuration_identity_capacity\":"<<IdentityCapacity<<",\"configuration_identity_dropped\":"<<dropped
        <<",\"configuration_identity_samples\":[";
    for(unsigned n=0;n<count;++n){if(n)out<<',';const auto& row=identities[n];const auto& s=*row.snapshot;const auto& o=s.owner;
        out<<"{\"historical_only\":true,\"generation\":"<<row.generation<<",\"first_sequence\":"<<row.firstSequence<<",\"observations\":"<<row.observations
            <<",\"sequence\":"<<s.sequence<<",\"observed_ns\":"<<s.observedNs<<",\"deadline_ns\":"<<s.deadlineNs
            <<",\"owner\":{\"player\":"<<o.player<<",\"soldier\":"<<o.soldier<<",\"weak\":"<<o.weak<<",\"weapon\":"<<o.weapon
            <<",\"actor_generation\":"<<o.actorGeneration<<",\"equip_generation\":"<<o.equipGeneration<<",\"space\":"<<o.space<<'}'
            <<",\"weapon_data\":"<<s.weaponData<<",\"state_type_info\":"<<s.stateTypeInfo<<",\"mesh_type_info\":"<<s.meshTypeInfo
            <<",\"inventory\":"<<s.inventory<<",\"selected_slot\":"<<s.selectedSlot<<",\"configured_array\":"<<s.soleConfiguredArray
            <<",\"configuration_path_pointer\":"<<s.configurationPathPointer<<",\"configuration_path_verified\":"<<(s.configurationPathVerified?"true":"false")<<",\"configuration_path\":";
        text(s.configurationPath);
        const auto& binding=s.operationBinding;
        out<<",\"operation_binding\":{\"content_fingerprint_fnv64\":"<<binding.executableFingerprint
            <<",\"executable_bytes\":"<<binding.executableBytes<<",\"image_base\":"<<binding.imageBase<<",\"image_bytes\":"<<binding.imageBytes<<",\"code\":[";
        for(unsigned k=0;k<4;++k){if(k)out<<',';out<<"{\"rva\":"<<binding.codeRva[k]<<",\"bytes\":"<<binding.codeBytes[k]<<",\"fingerprint_fnv64\":"<<binding.codeFingerprint[k]<<'}';}
        out<<"]},\"weapon_name\":";text(s.weaponName);out<<",\"state_count\":"<<unsigned(s.stateCount)<<",\"sole_mesh_paths\":[";
        if(s.stateCount==1)for(unsigned m=0;m<s.states[0].count&&m<8;++m){if(m)out<<',';text(s.states[0].meshes[m].assetPath);}
        out<<"]}";
    }out<<"]}";
}
} // namespace fvr::bc2
