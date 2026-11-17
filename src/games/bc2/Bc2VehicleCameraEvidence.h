#pragma once
#include "fvr/interaction/VehicleTracking.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <cmath>

namespace fvr::bc2 {
struct VehicleCameraEvidenceKey {
    interaction::VehicleTrackingOwner owner{};
    unsigned frame=0;
    std::uint64_t space=0,tracking=0;
    std::int64_t predictedNs=0;
    bool operator==(const VehicleCameraEvidenceKey&)const=default;
};
// Caller serializes this value-only journal. Preserve the first sample and
// the newest Capacity-1 samples in time order. Owner/space boundaries bypass
// the cadence; physical head motion does not. Omitted samples are explicit,
// so bounded retention cannot be mistaken for continuous ride coverage.
template<class Record,std::size_t Capacity=16>
class VehicleCameraEvidence {
    static_assert(Capacity>=2);
public:
    static constexpr std::uint64_t MinimumIntervalMs=500;
    static constexpr std::size_t MaxRecords=Capacity;
    bool Due(const VehicleCameraEvidenceKey& key,std::uint64_t nowMs)const noexcept {
        if(!key.frame)return false;
        if(!count_)return true;
        for(std::size_t i=0;i<count_;++i)if(key==At(i).key)return false;
        const auto& previous=At(count_-1);
        if(key.owner!=previous.key.owner||key.space!=previous.key.space)return true;
        return nowMs>previous.ms&&nowMs-previous.ms>=MinimumIntervalMs;
    }
    bool Observe(const Record& record)noexcept {
        if(!Due(record.key,record.ms))return false;
        std::size_t slot=0;
        if(count_<Capacity)slot=Slot(count_++);
        else {
            slot=Slot(1);
            ++omitted_;
            if(records_[slot].appliedMask!=3)++omittedIncomplete_;
            begin_=(begin_+1)%(Capacity-1);
        }
        records_[slot]=record;
        records_[slot].appliedMask=0;
        ++samples_;
        return true;
    }
    // As with ViewBasisEvidence, enrichment joins owned identity, never the
    // latest journal index. The caller supplies the current live owner/space
    // under the same lock; a saved plan key alone is not current ownership.
    // Repeated eye callbacks cannot overwrite evidence.
    template<class Update>
    bool Enrich(const VehicleCameraEvidenceKey& key,const interaction::VehicleCameraSample& current,unsigned eye,Update update)noexcept {
        if(eye>1)return false;
        if(!current.live||current.owner!=key.owner||current.space!=key.space){++unmatchedEyes_;return false;}
        for(std::size_t i=0;i<count_;++i){
            auto& row=records_[Slot(i)];
            if(row.key!=key)continue;
            if(row.appliedMask&(1u<<eye)){++duplicateEyes_;return false;}
            update(row);row.appliedMask|=1u<<eye;
            if(row.appliedMask==3)++complete_;
            return true;
        }
        ++unmatchedEyes_;return false;
    }
    std::size_t Size()const noexcept{return count_;}
    bool Contains(const VehicleCameraEvidenceKey& key)const noexcept {
        for(std::size_t i=0;i<count_;++i)if(At(i).key==key)return true;return false;
    }
    const Record& At(std::size_t index)const noexcept{return records_[Slot(index)];}
    std::uint64_t Samples()const noexcept{return samples_;}
    std::uint64_t Omitted()const noexcept{return omitted_;}
    std::uint64_t Incomplete()const noexcept{return samples_-complete_;}
    std::uint64_t OmittedIncomplete()const noexcept{return omittedIncomplete_;}
    std::uint64_t RetainedIncomplete()const noexcept{return Incomplete()-omittedIncomplete_;}
    std::uint64_t DuplicateEyes()const noexcept{return duplicateEyes_;}
    std::uint64_t UnmatchedEyes()const noexcept{return unmatchedEyes_;}
private:
    std::size_t Slot(std::size_t index)const noexcept{return index?1+(begin_+index-1)%(Capacity-1):0;}
    std::array<Record,Capacity> records_{};
    std::size_t count_=0,begin_=0;
    std::uint64_t samples_=0,omitted_=0,complete_=0,omittedIncomplete_=0,duplicateEyes_=0,unmatchedEyes_=0;
};

// A readback association, never a downstream consumer or propulsion receipt.
struct VehicleCameraPulseInput {
    interaction::VehicleTrackingOwner owner{};
    std::uint64_t space=0,seatGeneration=0,gather=0,input=0,ms=0;
    std::int64_t observedNs=0,deadlineNs=0;
    unsigned mask=0;std::array<float,4> axes{};
};
enum class VehicleCameraPulseEnd:unsigned {Open,Neutral,Interrupted,Replaced,TimedOut};
// Fixed first/pre, first-after-nonzero and first-after-zero samples. The idle
// pre-sample follows the existing 500ms camera cadence; its age is explicit.
// Completed episodes survive arbitrarily long neutral periods. Caller holds
// the existing camera mutex; no allocation, native read or I/O occurs here.
template<class Record,std::size_t Capacity=4>
class VehicleCameraPulseEvidence {
    static_assert(Capacity>0);
public:
    static constexpr std::size_t MaxPulses=Capacity;
    static constexpr std::uint64_t MaximumPreAgeMs=500,MaximumPulseMs=2000;
    struct Pulse {
        std::uint64_t sequence=0;VehicleCameraPulseInput began{},lastActive{},neutral{};
        VehicleCameraPulseEnd end=VehicleCameraPulseEnd::Open;
        unsigned sampleMask=0;std::array<Record,3> samples{};
    };
    void Invalidate()noexcept {
        if(open_){auto& pulse=Current();if(pulse.end==VehicleCameraPulseEnd::Open)pulse.end=VehicleCameraPulseEnd::Interrupted;}
        open_=haveInput_=havePre_=false;
    }
    bool Input(VehicleCameraPulseInput input)noexcept {
        if(!Valid(input)){Invalidate();++invalidInputs_;return false;}
        if(haveInput_&&!SameSource(input,input_))Invalidate();
        if(haveInput_&&(input.input<input_.input||input.ms<input_.ms||
           (input.input==input_.input&&(input.observedNs!=input_.observedNs||input.deadlineNs!=input_.deadlineNs)))){
            Invalidate();++invalidInputs_;return false;
        }
        const bool active=Active(input),wasActive=haveInput_&&Active(input_);
        if(active&&!wasActive){
            if(open_){Current().end=VehicleCameraPulseEnd::Replaced;open_=false;}
            const auto slot=(begin_+count_)%Capacity;
            if(count_==Capacity){begin_=(begin_+1)%Capacity;++omitted_;}else ++count_;
            auto& pulse=rows_[slot];pulse={};pulse.sequence=++samples_;pulse.began=pulse.lastActive=input;open_=true;
            if(havePre_&&pre_.key.owner==input.owner&&pre_.key.space==input.space&&pre_.ms<=input.ms&&
               input.ms-pre_.ms<=MaximumPreAgeMs){pulse.samples[0]=pre_;pulse.sampleMask=1;}
        }else if(active&&open_){
            auto& pulse=Current();pulse.lastActive=input;
            if(input.ms-pulse.began.ms>MaximumPulseMs){pulse.end=VehicleCameraPulseEnd::TimedOut;open_=false;++timedOut_;}
        }else if(!active&&wasActive&&open_){
            Current().neutral=input;Current().end=VehicleCameraPulseEnd::Neutral;
        }
        if(open_&&input.ms-Current().began.ms>MaximumPulseMs){Current().end=VehicleCameraPulseEnd::TimedOut;open_=false;++timedOut_;}
        if(active)havePre_=false;
        input_=input;haveInput_=true;return true;
    }
    bool Due(const VehicleCameraEvidenceKey& key,std::uint64_t ms,std::int64_t nowNs)const noexcept {
        if(!Ready(key,ms,nowNs)||!open_)return false;
        const auto& pulse=Current();
        const unsigned slot=Active(input_)?1:2;
        return !(pulse.sampleMask&(1u<<slot))&&(slot==1||pulse.end==VehicleCameraPulseEnd::Neutral);
    }
    bool Observe(const Record& record,std::int64_t nowNs,bool baseline)noexcept {
        if(!Ready(record.key,record.ms,nowNs))return false;
        bool retained=false;
        if(Due(record.key,record.ms,nowNs)){
            auto& pulse=Current();const unsigned slot=Active(input_)?1:2;
            pulse.samples[slot]=record;pulse.samples[slot].appliedMask=0;pulse.sampleMask|=1u<<slot;retained=true;
            if(slot==2)open_=false;
        }
        if(baseline&&!Active(input_)){
            pre_=record;pre_.appliedMask=0;havePre_=true;retained=true;
        }
        return retained;
    }
    template<class Update>
    bool Enrich(const VehicleCameraEvidenceKey& key,const interaction::VehicleCameraSample& current,unsigned eye,Update update)noexcept {
        if(eye>1||!haveInput_||!current.live||current.owner!=key.owner||current.space!=key.space)return false;
        bool matched=false;
        const auto enrich=[&](Record& row){if(row.key!=key)return;matched=true;
            if(row.appliedMask&(1u<<eye)){++duplicateEyes_;return;}
            update(row);row.appliedMask|=1u<<eye;};
        if(havePre_)enrich(pre_);
        for(std::size_t i=0;i<count_;++i){auto& pulse=rows_[(begin_+i)%Capacity];if(!SameSource(pulse.began,input_))continue;
            for(unsigned slot=0;slot<3;++slot)if(pulse.sampleMask&(1u<<slot))enrich(pulse.samples[slot]);}
        return matched;
    }
    std::size_t Size()const noexcept{return count_;}
    const Pulse& At(std::size_t index)const noexcept{return rows_[(begin_+index)%Capacity];}
    std::uint64_t Samples()const noexcept{return samples_;}
    std::uint64_t Omitted()const noexcept{return omitted_;}
    std::uint64_t InvalidInputs()const noexcept{return invalidInputs_;}
    std::uint64_t TimedOut()const noexcept{return timedOut_;}
    std::uint64_t DuplicateEyes()const noexcept{return duplicateEyes_;}
private:
    static bool SameSource(const VehicleCameraPulseInput& a,const VehicleCameraPulseInput& b)noexcept {
        return a.owner==b.owner&&a.space==b.space&&a.seatGeneration==b.seatGeneration&&a.mask==b.mask;
    }
    static bool Active(const VehicleCameraPulseInput& input)noexcept {
        for(unsigned n=0;n<4;++n)if((input.mask&(1u<<n))&&input.axes[n]!=0)return true;return false;
    }
    static bool Valid(const VehicleCameraPulseInput& input)noexcept {
        if(!input.owner.actor||!input.owner.generation||!input.owner.vehicle||!input.owner.seat||!input.space||
           !input.seatGeneration||!input.gather||!input.input||!input.ms||!input.mask||(input.mask&~15u)||
           input.observedNs<=0||input.deadlineNs<=input.observedNs||input.deadlineNs-input.observedNs>100000000)return false;
        for(unsigned n=0;n<4;++n)if(!std::isfinite(input.axes[n])||(!(input.mask&(1u<<n))&&input.axes[n]!=0))return false;
        return true;
    }
    bool Ready(const VehicleCameraEvidenceKey& key,std::uint64_t ms,std::int64_t nowNs)const noexcept {
        return haveInput_&&key.frame&&key.owner==input_.owner&&key.space==input_.space&&key.tracking>=input_.input&&
            ms>=input_.ms&&nowNs>=input_.observedNs&&nowNs<input_.deadlineNs&&
            (!open_||(ms>=Current().began.ms&&ms-Current().began.ms<=MaximumPulseMs));
    }
    Pulse& Current()noexcept{return rows_[(begin_+count_-1)%Capacity];}
    const Pulse& Current()const noexcept{return rows_[(begin_+count_-1)%Capacity];}
    std::array<Pulse,Capacity> rows_{};std::size_t begin_=0,count_=0;
    VehicleCameraPulseInput input_{};Record pre_{};bool haveInput_=false,havePre_=false,open_=false;
    std::uint64_t samples_=0,omitted_=0,invalidInputs_=0,timedOut_=0,duplicateEyes_=0;
};
}
