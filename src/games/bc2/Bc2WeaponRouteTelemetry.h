#pragma once
#include "Bc2MagazineEquipmentProfile.h"
#include "Bc2AmmoResourceReadEvidence.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <ostream>

namespace fvr::bc2 {
// Observations only. These stages never grant contact, native admission or an
// ammunition receipt. Counts are bounded in memory; the existing diagnostic
// writer decides when to serialize a snapshot.
enum class WeaponMagazineEntryStage:unsigned {
 Disabled,Input,Cancelled,ProfileMissing,ProfileUnavailable,Family,SelectedMesh,
 ResourceRead,RawMissing,RawOwner,Rig,HandEvidence,Geometry,GunClaim,Consumer,
 Contact,Active,Request,Count
};
inline constexpr std::array<const char*,unsigned(WeaponMagazineEntryStage::Count)> WeaponMagazineEntryStageNames{
 "disabled","input","cancelled","profile_missing","profile_unavailable","family","selected_mesh",
 "resource_read","raw_missing","raw_owner","rig","hand_evidence","geometry","gun_claim","consumer",
 "contact","active","request"};
struct WeaponMagazineEntryObservation {
 const MagazineEquipmentProfile* profile=nullptr;
 std::string_view asset;
 WeaponMagazineEntryStage stage=WeaponMagazineEntryStage::Disabled;
 ReloadStateOwner owner{};
 std::uint64_t input=0,rig=0;
 std::int64_t nowNs=0;
 unsigned phase=0,interactionReason=0,resourceReadReason=~0u;
 bool grip=false,eject=false;
};
struct WeaponMagazineEntryRow {
 const MagazineEquipmentProfile* profile=nullptr;
 std::array<char,64> asset{};
 std::array<std::uint64_t,unsigned(WeaponMagazineEntryStage::Count)> stages{};
 std::array<std::uint64_t,unsigned(AmmoResourceReadReason::Invalid)+1> resourceReads{};
 WeaponMagazineEntryStage first=WeaponMagazineEntryStage::Disabled,last=WeaponMagazineEntryStage::Disabled;
 ReloadStateOwner owner{};
 std::uint64_t samples=0,gripSamples=0,ejectSamples=0,input=0,rig=0;
 std::int64_t firstNs=0,lastNs=0;
 unsigned phase=0,interactionReason=0,resourceReadReason=~0u;
};
struct WeaponMagazineEntrySnapshot {
 static constexpr std::size_t Capacity=MaxGeneratedMagazineProfiles+18;
 std::array<WeaponMagazineEntryRow,Capacity> rows{};
 std::size_t count=0;
 std::uint64_t dropped=0;
};
class WeaponMagazineEntryTelemetry {
public:
 static constexpr auto Capacity=WeaponMagazineEntrySnapshot::Capacity;
 void Observe(const WeaponMagazineEntryObservation& observation)noexcept {
  Guard guard(gate_);if(!guard.held){Drop();return;}
  auto& rows=data_.rows;auto& count=data_.count;
  const auto asset=observation.asset.substr(0,rows[0].asset.size()-1);
  const auto matches=[&](std::size_t n){return rows[n].profile==observation.profile&&std::string_view(rows[n].asset.data())==asset;};
  auto index=lastRow_;
  if(index>=count||!matches(index)){
   for(index=0;index<count&&!matches(index);++index){}
   if(index==count){if(count==Capacity){Drop();return;}
    auto& row=rows[count++];row.profile=observation.profile;
    std::copy(asset.begin(),asset.end(),row.asset.begin());
    row.first=observation.stage;row.firstNs=observation.nowNs;}
   lastRow_=index;
  }
  auto& row=rows[index];Increment(row.samples);Increment(row.stages[unsigned(observation.stage)]);
  if(observation.grip)Increment(row.gripSamples);if(observation.eject)Increment(row.ejectSamples);
  row.last=observation.stage;row.lastNs=observation.nowNs;row.owner=observation.owner;
  row.input=observation.input;row.rig=observation.rig;row.phase=observation.phase;
  row.interactionReason=observation.interactionReason;row.resourceReadReason=observation.resourceReadReason;
  if(observation.resourceReadReason<row.resourceReads.size())Increment(row.resourceReads[observation.resourceReadReason]);
 }
 std::optional<WeaponMagazineEntrySnapshot> Snapshot()const noexcept {
  Guard guard(gate_);if(!guard.held)return {};
  auto result=data_;result.dropped=dropped_.load(std::memory_order_relaxed);return result;
 }
 void Report(std::ostream& out)const {
  const auto snapshot=Snapshot();if(!snapshot){out<<"{\"snapshot_busy\":true}";return;}
  const auto& rows=snapshot->rows;const auto count=snapshot->count;
  const auto quoted=[&](std::string_view value){out<<'"';for(const unsigned char c:value){
   if(c=='"'||c=='\\')out<<'\\'<<char(c);else if(c<32)out<<'?';else out<<char(c);}out<<'"';};
  out<<"{\"dropped\":"<<snapshot->dropped<<",\"stage_names\":[";
  for(unsigned n=0;n<WeaponMagazineEntryStageNames.size();++n){if(n)out<<',';quoted(WeaponMagazineEntryStageNames[n]);}
  out<<"],\"rows\":[";
  for(std::size_t n=0;n<count;++n){if(n)out<<',';const auto& row=rows[n];
   out<<"{\"profile\":";if(row.profile)out<<std::uint64_t(row.profile->nativeId);else out<<"null";
   out<<",\"asset\":";quoted(row.asset.data());out<<",\"configuration\":";
   quoted(row.profile&&row.profile->native?row.profile->native->configuration.assetPath:std::string_view{});
   out<<",\"samples\":"<<row.samples<<",\"grip_samples\":"<<row.gripSamples<<",\"eject_samples\":"<<row.ejectSamples
      <<",\"first\":"<<unsigned(row.first)<<",\"last\":"<<unsigned(row.last)<<",\"first_ns\":"<<row.firstNs<<",\"last_ns\":"<<row.lastNs
      <<",\"input\":"<<row.input<<",\"weapon\":"<<row.owner.weapon<<",\"soldier\":"<<row.owner.soldier
      <<",\"actor_generation\":"<<row.owner.actorGeneration<<",\"equip_generation\":"<<row.owner.equipGeneration
      <<",\"space\":"<<row.owner.space<<",\"rig\":"<<row.rig<<",\"phase\":"<<row.phase
      <<",\"interaction_reason\":"<<row.interactionReason<<",\"resource_read_reason\":"<<row.resourceReadReason<<",\"stages\":[";
   for(unsigned i=0;i<row.stages.size();++i){if(i)out<<',';out<<row.stages[i];}out<<"],\"resource_reads\":[";
   for(unsigned i=0;i<row.resourceReads.size();++i){if(i)out<<',';out<<row.resourceReads[i];}out<<"]}";
  }out<<"]}";
 }
private:
 struct Guard {std::atomic_flag& gate;bool held;
  explicit Guard(std::atomic_flag& g)noexcept:gate(g),held(!g.test_and_set(std::memory_order_acquire)){}
  ~Guard(){if(held)gate.clear(std::memory_order_release);}};
 mutable std::atomic_flag gate_=ATOMIC_FLAG_INIT;
 std::atomic<std::uint64_t> dropped_{0};
 WeaponMagazineEntrySnapshot data_{};
 void Drop()noexcept{auto n=dropped_.load(std::memory_order_relaxed);if(n!=UINT64_MAX)dropped_.compare_exchange_strong(n,n+1,std::memory_order_relaxed);}
 static void Increment(std::uint64_t& value)noexcept{if(value!=UINT64_MAX)++value;}
 std::size_t lastRow_=Capacity;
};
} // namespace fvr::bc2
