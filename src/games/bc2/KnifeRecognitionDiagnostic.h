#pragma once
#include "fvr/interaction/MeleeStroke.h"
#include <array>
#include <limits>
#include <ostream>
namespace fvr::interaction {
enum class KnifeRecognitionGate:unsigned {None,Unknown,Admission,Duplicate,SourceOrder,Phase,
    ReturnWanted,HeldProof,ClaimTransfer,ClaimLost,ClaimAcquire,ClaimForeign,ClaimRenew,Motion,NativeGate};
struct KnifeRecognitionRecord {
    HandInteractionSample source{};HandInteractionKey knife{};
    MeleePoint tip{},direction{};MeleeStrokeObservation motion{};
    KnifeRecognitionGate gate=KnifeRecognitionGate::None;
    std::uint64_t claim=0;
    unsigned phaseBefore=0,phaseAfter=0,prerequisites=0,command=0;
    bool recognizerReady=false;
};
// Gather-owned, opt-in observation only. No allocation, locks, clock reads or
// native reads in Record. Report follows the existing stopped coordinator report.
class KnifeRecognitionDiagnostic {
public:
    static constexpr unsigned Capacity=128;
    void Enable()noexcept{enabled_=true;}
    void Record(const KnifeRecognitionRecord& row)noexcept {
        if(!enabled_||row.gate==KnifeRecognitionGate::Duplicate||
           !((row.phaseBefore>=3&&row.phaseBefore<=6)||(row.phaseAfter>=3&&row.phaseAfter<=6)))return;
        const bool moving=row.motion.metrics&&row.motion.distance>0&&
            (row.motion.after==MeleeStrokePhase::Travelling||row.motion.decision==MeleeStrokeDecision::Intent);
        if(previous_&&Same(*previous_,row)&&(!moving||row.source.sequence==previous_->source.sequence))return;
        previous_=row;if(seen_!=UINT64_MAX)++seen_;
        if(kept_<rows_.size())rows_[kept_++]=row;else if(dropped_!=UINT64_MAX)++dropped_;
    }
    void NativeGate(const HandInteractionSample& source,unsigned phase)noexcept {
        KnifeRecognitionRecord row;row.source=source;row.phaseBefore=row.phaseAfter=phase;
        row.gate=KnifeRecognitionGate::NativeGate;Record(row);
    }
    void Report(std::ostream& out)const {
        out<<"{\"enabled\":"<<enabled_<<",\"capacity\":"<<Capacity<<",\"seen\":"<<seen_
           <<",\"kept\":"<<kept_<<",\"dropped\":"<<dropped_<<",\"rows\":[";
        for(unsigned n=0;n<kept_;++n){if(n)out<<',';const auto& r=rows_[n];const auto& s=r.source;const auto& m=r.motion;
            out<<"{\"input\":"<<s.sequence<<",\"observed_ns\":"<<s.observedNs<<",\"deadline_ns\":"<<s.deadlineNs
               <<",\"now_ns\":"<<s.nowNs<<",\"actor\":"<<s.owner.actor<<",\"actor_generation\":"<<s.owner.actorGeneration
               <<",\"equip_generation\":"<<s.owner.equipGeneration<<",\"space\":"<<s.owner.space
               <<",\"knife\":"<<r.knife.id<<",\"knife_generation\":"<<r.knife.generation<<",\"claim\":"<<r.claim
               <<",\"gate\":"<<unsigned(r.gate)<<",\"prerequisites\":"<<r.prerequisites
               <<",\"phase_before\":"<<r.phaseBefore<<",\"phase_after\":"<<r.phaseAfter
               <<",\"motion_before\":"<<unsigned(m.before)<<",\"motion_after\":"<<unsigned(m.after)
               <<",\"decision\":"<<unsigned(m.decision)<<",\"invalid\":"<<m.invalid<<",\"recognizer_ready\":"<<r.recognizerReady<<",\"command\":"<<r.command
               <<",\"previous_input\":"<<m.previousSequence<<",\"previous_observed_ns\":"<<m.previousObservedNs
               <<",\"elapsed_ns\":"<<m.elapsedNs<<",\"quiet_ns\":"<<m.quietNs<<",\"stroke_ns\":"<<m.strokeNs
               <<",\"metrics\":"<<m.metrics<<",\"distance\":";Number(out,m.distance);out<<",\"speed\":";Number(out,m.speed);
            out<<",\"alignment\":";Number(out,m.alignment);out<<",\"travel\":";Number(out,m.travel);
            out<<",\"tip\":[";Number(out,r.tip.x);out<<',';Number(out,r.tip.y);out<<',';Number(out,r.tip.z);
            out<<"],\"direction\":[";Number(out,r.direction.x);out<<',';Number(out,r.direction.y);out<<',';Number(out,r.direction.z);out<<"]}";
        }out<<"]}";
    }
    unsigned Kept()const noexcept{return kept_;}
    std::uint64_t Seen()const noexcept{return seen_;}
    std::uint64_t Dropped()const noexcept{return dropped_;}
private:
    static bool Same(const KnifeRecognitionRecord& a,const KnifeRecognitionRecord& b)noexcept {
        return a.source.owner==b.source.owner&&a.knife==b.knife&&a.claim==b.claim&&a.gate==b.gate&&
            a.phaseBefore==b.phaseBefore&&a.phaseAfter==b.phaseAfter&&a.prerequisites==b.prerequisites&&
            a.motion.before==b.motion.before&&a.motion.after==b.motion.after&&a.motion.decision==b.motion.decision&&a.motion.invalid==b.motion.invalid&&
            a.command==b.command&&a.recognizerReady==b.recognizerReady;
    }
    static void Number(std::ostream& out,float value){if(std::isfinite(value))out<<value;else out<<"null";}
    std::array<KnifeRecognitionRecord,Capacity> rows_{};std::optional<KnifeRecognitionRecord> previous_;
    std::uint64_t seen_=0,dropped_=0;unsigned kept_=0;bool enabled_=false;
};
static_assert(sizeof(KnifeRecognitionDiagnostic)<65536);
}
