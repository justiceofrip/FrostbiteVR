#include "Bc2ReloadProducerBinding.h"
#include "Bc2ReloadDrawGeometry.h"
#include <algorithm>
#include <cmath>
#include <cstring>
namespace fvr::bc2 {
thread_local Bc2ReloadProducerBinding::Scope* Bc2ReloadProducerBinding::current_=nullptr;
namespace {
bool Key(const ReloadProducerView& k)noexcept{return k.world&&k.request&&k.view&&k.eye<2;}
bool Owner(const ReloadProducerOwner& o)noexcept{return o.actor&&o.weak&&o.weapon&&o.ownerGeneration&&o.space;}
bool Source(const ReloadPackedSource& s,std::int64_t now)noexcept {
    return Owner(s.owner)&&s.rigPose&&s.rigFingerprint==0xa7f219a1426216abull&&s.inputGeneration&&
        s.boneCount>=6&&s.boneCount<=1024&&(s.shellNamed||s.opticNamed)&&
        (!s.shellNamed||s.shellIndex<s.boneCount)&&(!s.opticNamed||s.opticIndex<s.boneCount)&&
        !(s.shellNamed&&s.opticNamed&&s.shellIndex==s.opticIndex)&&
        (!s.selectedMeshIdentityVerified||s.selectedMeshes1p)&&s.observedNs>0&&s.observedNs<=now&&
        s.deadlineNs>now&&s.deadlineNs-s.observedNs<=250000000ll;
}
bool SamePacked(std::span<const std::byte> source,std::span<const std::byte> packed,unsigned count)noexcept {
    if(source.size()!=std::size_t(count)*64||packed.size()!=std::size_t(count)*48)return false;
    for(unsigned bone=0;bone<count;++bone)for(unsigned col=0;col<3;++col)for(unsigned row=0;row<4;++row){
        const auto* value=source.data()+bone*64+row*16+col*4;float f=0;std::memcpy(&f,value,4);
        if(!std::isfinite(f)||std::memcmp(value,packed.data()+bone*48+col*16+row*4,4))return false;
    }return true;
}
}
Bc2ReloadProducerBinding::Scope::Scope(Bc2ReloadProducerBinding& owner,ReloadProducerView key,bool verified,std::int64_t now)noexcept:
    recorder_(owner),previous_(current_),key_(key),beginNs_(now),admitted_(owner.Enabled()&&verified&&Key(key)&&now>0){
    // Even a rejected/nested scope masks its parent's proof on this thread.
    current_=this;if(!admitted_)++recorder_.scopeRejected_;
}
Bc2ReloadProducerBinding::Scope::~Scope(){
    if(current_==this)current_=previous_;else ++recorder_.scopeRejected_;
}
void Bc2ReloadProducerBinding::Scope::Complete(bool exact,std::int64_t endNs)noexcept {
    if(completed_){bad_=true;++recorder_.scopeRejected_;return;}completed_=true;
    if(!recorder_.Enabled()||current_!=this||!admitted_||!exact||bad_||first_||pairs_!=1||endNs<beginNs_||endNs<lastPackNs_||
       !Source(source_,endNs)){++recorder_.scopeRejected_;return;}
    recorder_.Publish(result_);
}
void Bc2ReloadProducerBinding::ObservePacked(std::uint64_t serial,bool first,const ReloadPackedSource& source,
    std::uint64_t destination,std::int64_t packNs,std::span<const std::byte> input,std::span<const std::byte> packed)noexcept {
    if(!Enabled())return;auto* scope=current_;
    if(!scope||&scope->recorder_!=this||!scope->admitted_||scope->completed_){++outsideScope_;return;}
    if(scope->bad_)return;
    const auto bad=[&]{scope->bad_=true;++invalidPack_;};
    if(!serial||!destination||packNs<scope->beginNs_||packNs<scope->lastPackNs_||!Source(source,packNs)||!SamePacked(input,packed,source.boneCount)){bad();return;}
    scope->lastPackNs_=packNs;const auto hash=ReloadDrawHash(packed);
    if(first){
        if(scope->first_||scope->pairs_){scope->bad_=true;++ambiguous_;return;}
        scope->first_=true;scope->pairSerial_=serial;scope->source_=source;
        auto& r=scope->result_;r.key=scope->key_;r.packedHash=hash;r.boneCount=source.boneCount;r.packDestinations[0]=destination;
        auto& p=r.evidence;p.request=r.key.request;p.nativeFrame=r.key.nativeFrame;
        p.rigPose=source.rigPose;p.rigFingerprint=source.rigFingerprint;p.selectedMeshes1p=source.selectedMeshes1p;
        p.actor=source.owner.actor;p.weak=source.owner.weak;p.weapon=source.owner.weapon;
        p.ownerGeneration=source.owner.ownerGeneration;p.space=source.owner.space;p.inputGeneration=source.inputGeneration;
        p.observedNs=source.observedNs;p.deadlineNs=source.deadlineNs;
        p.selectedMeshIdentityVerified=source.selectedMeshIdentityVerified;
        p.opticSelected=source.opticSelected;p.physicalEquipmentGeneration=source.physicalEquipmentGeneration;
        p.shellHidden=source.shellHidden;p.packedShellValid=source.shellNamed;p.packedOpticValid=source.opticNamed;
        if(source.shellNamed)std::copy_n(packed.data()+source.shellIndex*48,48,p.packedShell.begin());
        if(source.opticNamed)std::copy_n(packed.data()+source.opticIndex*48,48,p.packedOptic.begin());
        p.holdCycle=source.holdCycle;p.holdBeginNs=source.holdBeginNs;p.holdDeadlineNs=source.holdDeadlineNs;
    }else{
        if(!scope->first_||scope->pairSerial_!=serial||scope->source_!=source||scope->result_.packedHash!=hash||
           scope->result_.packDestinations[0]==destination){bad();return;}
        scope->first_=false;scope->pairs_=1;scope->result_.packDestinations[1]=destination;
        scope->result_.evidence.exactRequestAssociation=true;++pairs_;
    }
}
void Bc2ReloadProducerBinding::Publish(const ReloadProducerSnapshot& value)noexcept {
    std::unique_lock lock(mutex_,std::try_to_lock);if(!lock){++busy_;return;}
    for(auto& s:slots_)if(s.used&&s.value.key==value.key){s.ambiguous=true;++ambiguous_;return;}
    slots_[next_]={value,true,false};next_=(next_+1)%Slots;++published_;
}
std::optional<ReloadProducerSnapshot> Bc2ReloadProducerBinding::Read(const ReloadProducerView& key,const ReloadProducerOwner& owner,std::int64_t now)noexcept {
    std::unique_lock lock(mutex_,std::try_to_lock);if(!lock){++busy_;return {};}
    if(!Enabled()||!Key(key)||!Owner(owner)||now<=0){++readMisses_;return {};}
    for(const auto& s:slots_)if(s.used&&s.value.key==key){const auto& p=s.value.evidence;
        const ReloadProducerOwner expected{p.actor,p.weak,p.weapon,p.ownerGeneration,p.space};
        if(s.ambiguous||owner!=expected||p.observedNs>now||p.deadlineNs<=now){++readMisses_;return {};}
        return s.value; // Complete immutable value; neither slots nor native pointers escape.
    }++readMisses_;return {};
}
Bc2ReloadProducerBinding::Counters Bc2ReloadProducerBinding::Statistics()const noexcept {
    return {outsideScope_.load(),invalidPack_.load(),pairs_.load(),ambiguous_.load(),scopeRejected_.load(),busy_.load(),published_.load(),readMisses_.load()};
}
void Bc2ReloadProducerBinding::Report(std::ostream& out)noexcept {
    const auto c=Statistics();
    std::unique_lock lock(mutex_,std::try_to_lock);
    out<<"{\"outside_scope\":"<<c.outsideScope<<",\"invalid_pack\":"<<c.invalidPack<<",\"pairs\":"<<c.pairs
       <<",\"ambiguous\":"<<c.ambiguous<<",\"scope_rejected\":"<<c.scopeRejected<<",\"busy\":"<<c.busy
       <<",\"published\":"<<c.published<<",\"read_misses\":"<<c.readMisses<<",\"snapshot_complete\":"<<(lock?"true":"false")<<",\"records\":[";
    bool first=true;if(lock)for(const auto& slot:slots_)if(slot.used){
        if(!first)out<<',';first=false;const auto& s=slot.value;const auto& k=s.key;const auto& p=s.evidence;
        out<<"{\"world\":"<<k.world<<",\"request\":"<<k.request<<",\"view\":"<<k.view<<",\"native_frame\":"<<k.nativeFrame<<",\"eye\":"<<k.eye
           <<",\"ambiguous\":"<<(slot.ambiguous?"true":"false")<<",\"pack_destinations\":["<<s.packDestinations[0]<<','<<s.packDestinations[1]
           <<"],\"packed_palette_hash\":"<<s.packedHash<<",\"bones\":"<<s.boneCount<<",\"actor\":"<<p.actor<<",\"weak\":"<<p.weak
           <<",\"weapon\":"<<p.weapon<<",\"owner_generation\":"<<p.ownerGeneration<<",\"space\":"<<p.space<<",\"input_generation\":"<<p.inputGeneration
           <<",\"rig_pose\":"<<p.rigPose<<",\"observed_ns\":"<<p.observedNs<<",\"deadline_ns\":"<<p.deadlineNs
           <<",\"packed_shell_valid\":"<<(p.packedShellValid?"true":"false")<<",\"packed_optic_valid\":"<<(p.packedOpticValid?"true":"false")
           <<",\"selected_mesh_verified\":"<<(p.selectedMeshIdentityVerified?"true":"false")<<'}';
    }out<<"]}";
}
Bc2ReloadProducerBinding& ReloadProducerBinding()noexcept {static Bc2ReloadProducerBinding binding;return binding;}
}
