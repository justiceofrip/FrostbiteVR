#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <ostream>
#include <span>

namespace fvr::bc2 {
// Inspection-only evidence. No pose, input, ownership or freshness authority.
// Every matrix is serialized as exact uint32 float bits (including padding).
using PumpVisualMatrix=std::array<std::uint32_t,16>;
using PumpVisualPacked=std::array<std::uint32_t,12>;
inline constexpr unsigned PumpVisualPartCount=6;
enum PumpVisualFlag:std::uint32_t {
    PvEnabled=1u<<0,PvTarget=1u<<1,PvRecovery=1u<<2,PvFresh=1u<<3,
    PvAttachmentPending=1u<<4,PvHandRoleReady=1u<<5,PvSupport=1u<<6,
    PvSight=1u<<7,PvReload=1u<<8,PvMagazine=1u<<9,PvPlacedRigid=1u<<10,
    PvOwnerMatches=1u<<11,PvCalibration=1u<<12,PvRestEntered=1u<<13,
    PvActiveEntered=1u<<14,PvRestPart=1u<<15,PvRestWrist=1u<<16,
    PvActiveBuilt=1u<<17,PvAttached=1u<<18,PvPublished=1u<<19,
    PvRightTracked=1u<<20,PvLeftTracked=1u<<21,PvPartBinding=1u<<22,
    PvFingerprintMatches=1u<<23,PvBuilderFresh=1u<<24,PvBuilderTargetFresh=1u<<25
};
struct PumpVisualPart {
    std::array<char,24> name{};std::uint32_t index=UINT32_MAX;
    std::int32_t parent=-1;bool found=false,hidden=false,requested=false,finalKnown=false;
    PumpVisualMatrix nativeWorld{},inverseBind{},nativeSkin{},requestedWorld{},finalSkin{};
};
struct PumpVisualSource {
    std::uint64_t id=0,generation=0,space=0,ownerGeneration=0,equipmentGeneration=0;
    std::int64_t observedNs=0,deadlineNs=0,pumpObservedNs=0,pumpDeadlineNs=0,builderNs=0;
    std::uint64_t pumpInput=0,rigFingerprint=0,calibrationFingerprint=0,cycle=0,shot=0,targetInput=0,gunClaim=0,mechanismClaim=0;
    std::uint32_t soldier=0,weak=0,weapon=0,animation=0,skeleton=0,pose=0,evaluated=0,count=0;
    std::uint32_t flags=0,stage=0,unitsBits=0,travelBits=0,fixedCount=0,companionCount=0;
    bool companionReturning=false;
    bool complete=false;PumpVisualMatrix placed{};std::array<PumpVisualPart,PumpVisualPartCount> parts{};
};
struct PumpVisualCopy {
    std::uint64_t sourceId=0,drawSerial=0,destination=0,inputGeneration=0;
    std::int64_t observedNs=0;std::uint32_t callback=0,soldier=0,weak=0,sourceA=0,sourceB=0,count=0;
    bool poseAdmitted=false,changed=false,packedRead=false,packedMatch=false,sourceUnchanged=false;
    bool pumpRetained=false;std::uint32_t inputMask=0,packedMask=0;
    std::array<PumpVisualPacked,PumpVisualPartCount> input{},packed{};
};
inline PumpVisualMatrix PumpVisualBits(const void* bytes)noexcept {
    PumpVisualMatrix value{};std::memcpy(value.data(),bytes,sizeof(value));return value;
}
inline bool PumpVisualInput(PumpVisualCopy& out,unsigned part,unsigned index,
    std::span<const std::array<std::byte,64>> palette)noexcept {
    if(part>=PumpVisualPartCount||index>=palette.size())return false;
    // The proved native packer transposes XYZ, omitting all four padding cells.
    for(unsigned col=0;col<3;++col)for(unsigned row=0;row<4;++row)
        std::memcpy(&out.input[part][col*4+row],palette[index].data()+row*16+col*4,4);
    out.inputMask|=1u<<part;return true;
}
inline bool PumpVisualOutput(PumpVisualCopy& out,unsigned part,unsigned index,
    std::span<const std::byte> packed)noexcept {
    if(part>=PumpVisualPartCount||std::uint64_t(index)*48+48>packed.size())return false;
    std::memcpy(out.packed[part].data(),packed.data()+std::size_t(index)*48,48);
    out.packedMask|=1u<<part;return true;
}

template<unsigned SourceCapacity=1024,unsigned CopyCapacity=2048> class PumpVisualJournal {
public:
    static constexpr std::int64_t WindowNs=4000000000ll;
    // The first fresh selected-pump Fire observation fixes this immutable clock.
    // Subsequent packets, failures and shots never restart or extend the window.
    std::uint64_t Begin(PumpVisualSource& source,bool freshPumpFire)noexcept {
        std::unique_lock lock(mutex_,std::try_to_lock);
        if(!lock.owns_lock()){++busy_;return 0;}
        if(!data_.beginNs&&freshPumpFire&&source.observedNs>0&&source.observedNs<=INT64_MAX-WindowNs){
            data_.beginNs=source.observedNs;data_.endNs=source.observedNs+WindowNs;
            data_.triggerGeneration=source.generation;data_.triggerWeapon=source.weapon;
        }
        if(!InWindow(source.observedNs))return 0;
        if(data_.sourceCount==SourceCapacity){++data_.sourceDropped;return 0;}
        source.id=++data_.sourceCount;data_.sources[data_.sourceCount-1]=source;return source.id;
    }
    void Finish(const PumpVisualSource& source)noexcept {
        if(!source.id)return;
        std::unique_lock lock(mutex_,std::try_to_lock);
        if(!lock.owns_lock()){++busy_;return;}
        if(source.id<=data_.sourceCount&&!data_.sources[std::size_t(source.id-1)].complete){
            data_.sources[std::size_t(source.id-1)]=source;data_.sources[std::size_t(source.id-1)].complete=true;
        }
    }
    void Copy(const PumpVisualCopy& value)noexcept {
        std::unique_lock lock(mutex_,std::try_to_lock);
        if(!lock.owns_lock()){++busy_;return;}
        if(!InWindow(value.observedNs))return;
        if(data_.copyCount==CopyCapacity){++data_.copyDropped;return;}
        data_.copies[data_.copyCount++]=value;
    }
    struct Data {
        std::int64_t beginNs=0,endNs=0;std::uint64_t triggerGeneration=0;
        std::uint32_t triggerWeapon=0,sourceCount=0,copyCount=0,sourceDropped=0,copyDropped=0;
        std::array<PumpVisualSource,SourceCapacity> sources{};
        std::array<PumpVisualCopy,CopyCapacity> copies{};
    };
    std::unique_ptr<Data> Snapshot()const {
        auto snapshot=std::make_unique<Data>();
        {std::lock_guard lock(mutex_);*snapshot=data_;}return snapshot;
    }
    void Report(std::ostream& out)const {
        // Copy bounded storage to heap, then serialize without holding the lock.
        const auto data=Snapshot();const auto& d=*data;
        const auto array=[&](const auto& values){out<<'[';bool comma=false;for(const auto v:values){if(comma)out<<',';comma=true;out<<v;}out<<']';};
        out<<"{\"enabled\":true,\"window_ns\":"<<WindowNs<<",\"source_capacity\":"<<SourceCapacity
           <<",\"copy_capacity\":"<<CopyCapacity<<",\"begin_ns\":"<<d.beginNs<<",\"end_ns\":"<<d.endNs
           <<",\"trigger_generation\":"<<d.triggerGeneration<<",\"trigger_weapon\":"<<d.triggerWeapon
           <<",\"source_dropped\":"<<d.sourceDropped<<",\"copy_dropped\":"<<d.copyDropped<<",\"busy\":"<<busy_.load()
           <<",\"native_frame_eye_association\":false,\"matrix_encoding\":\"uint32_float_bits\",\"sources\":[";
        for(unsigned n=0;n<d.sourceCount;++n){if(n)out<<',';const auto& s=d.sources[n];
            out<<"{\"id\":"<<s.id<<",\"generation\":"<<s.generation<<",\"space\":"<<s.space
               <<",\"owner_generation\":"<<s.ownerGeneration<<",\"equipment_generation\":"<<s.equipmentGeneration
               <<",\"observed_ns\":"<<s.observedNs<<",\"deadline_ns\":"<<s.deadlineNs<<",\"builder_ns\":"<<s.builderNs
               <<",\"pump_input\":"<<s.pumpInput<<",\"pump_observed_ns\":"<<s.pumpObservedNs<<",\"pump_deadline_ns\":"<<s.pumpDeadlineNs
               <<",\"soldier\":"<<s.soldier<<",\"weak\":"<<s.weak<<",\"weapon\":"<<s.weapon
               <<",\"animation\":"<<s.animation<<",\"skeleton\":"<<s.skeleton<<",\"pose\":"<<s.pose
               <<",\"evaluated\":"<<s.evaluated<<",\"count\":"<<s.count<<",\"rig_fingerprint\":"<<s.rigFingerprint
               <<",\"calibration_fingerprint\":"<<s.calibrationFingerprint<<",\"flags\":"<<s.flags<<",\"stage\":"<<s.stage
               <<",\"cycle\":"<<s.cycle<<",\"shot\":"<<s.shot<<",\"target_input\":"<<s.targetInput
               <<",\"gun_claim\":"<<s.gunClaim<<",\"mechanism_claim\":"<<s.mechanismClaim
               <<",\"units_bits\":"<<s.unitsBits<<",\"travel_bits\":"<<s.travelBits<<",\"fixed_count\":"<<s.fixedCount<<",\"companion_count\":"<<s.companionCount<<",\"companion_returning\":"<<s.companionReturning<<",\"complete\":"<<s.complete
               <<",\"placed\":";array(s.placed);out<<",\"parts\":[";
            for(unsigned p=0;p<PumpVisualPartCount;++p){if(p)out<<',';const auto& v=s.parts[p];out<<"{\"name\":\""<<v.name.data()<<"\",\"index\":"<<v.index
                <<",\"parent\":"<<v.parent<<",\"found\":"<<v.found<<",\"hidden\":"<<v.hidden<<",\"requested\":"<<v.requested
                <<",\"final_known\":"<<v.finalKnown<<",\"native_world\":";array(v.nativeWorld);out<<",\"inverse_bind\":";array(v.inverseBind);
                out<<",\"native_skin\":";array(v.nativeSkin);out<<",\"requested_world\":";array(v.requestedWorld);
                out<<",\"final_skin\":";array(v.finalSkin);out<<'}';}out<<"]}";
        }
        out<<"],\"copies\":[";
        for(unsigned n=0;n<d.copyCount;++n){if(n)out<<',';const auto& c=d.copies[n];
            out<<"{\"source_id\":"<<c.sourceId<<",\"draw_serial\":"<<c.drawSerial<<",\"callback\":"<<c.callback
               <<",\"observed_ns\":"<<c.observedNs<<",\"generation\":"<<c.inputGeneration<<",\"destination\":"<<c.destination
               <<",\"soldier\":"<<c.soldier<<",\"weak\":"<<c.weak<<",\"source_a\":"<<c.sourceA<<",\"source_b\":"<<c.sourceB
               <<",\"count\":"<<c.count<<",\"pose_admitted\":"<<c.poseAdmitted<<",\"changed\":"<<c.changed
               <<",\"packed_read\":"<<c.packedRead<<",\"packed_match\":"<<c.packedMatch<<",\"source_unchanged\":"<<c.sourceUnchanged
               <<",\"pump_retained\":"<<c.pumpRetained<<",\"input_mask\":"<<c.inputMask<<",\"packed_mask\":"<<c.packedMask<<",\"input\":[";
            for(unsigned p=0;p<PumpVisualPartCount;++p){if(p)out<<',';array(c.input[p]);}out<<"],\"packed\":[";
            for(unsigned p=0;p<PumpVisualPartCount;++p){if(p)out<<',';array(c.packed[p]);}out<<"]}";
        }out<<"]}";
    }
private:
    bool InWindow(std::int64_t now)const noexcept {return data_.beginNs&&now>=data_.beginNs&&now<data_.endNs;}
    mutable std::mutex mutex_;std::atomic<std::uint64_t> busy_=0;Data data_{};
};
}
