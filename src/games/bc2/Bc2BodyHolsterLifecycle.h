#pragma once
#include "Bc2BodyHolster.h"
#include <ostream>
namespace fvr::bc2 {
// An inactive, empty semantic output is the native input policy's neutral
// rearm barrier. It cannot dispatch input, but is not a body Draw/cancel.
inline bool BodyHolsterInputRearm(const Bc2BodyHolster& holster,const ReloadStateOwner& owner,
    const interaction::ActionOutput& out,bool equipmentPending)noexcept {
    return holster.PendingNativeInputGap(owner)&&owner.soldier>=0x10000&&out.owner==owner.soldier&&!out.active&&!equipmentPending&&
        !out.held&&!out.pressed&&!out.released&&out.forward==0&&out.strafe==0&&out.turnDegrees==0;
}
enum class BodyHolsterLifecycleReason:unsigned {
    External,InputUnavailable,OwnerUnavailable,NotOnFoot,SessionStop,DiagnosticFailed,
    SelectionRejected,FreePoseAllocationFailed,TrackingPublicationUnavailable,ProbePublicationFailed,
    InputRearm,InputApplyRejected,NativeInputGapAccepted,NativeInputGapRejected,Count
};
inline const char* BodyHolsterLifecycleName(BodyHolsterLifecycleReason r)noexcept {
    constexpr const char* names[]={"external","input_unavailable","owner_unavailable","not_on_foot","session_stop","diagnostic_failed",
        "selection_rejected","free_pose_allocation_failed","tracking_publication_unavailable","probe_publication_failed",
        "input_rearm","input_apply_rejected","native_input_gap_accepted","native_input_gap_rejected"};
    return unsigned(r)<unsigned(BodyHolsterLifecycleReason::Count)?names[unsigned(r)]:"invalid";
}
class BodyHolsterLifecycleLog {
public:
    void Observe(BodyHolsterLifecycleReason reason,bool preserve,BodyHolsterPhase before,BodyHolsterPhase after,
        const interaction::HandInteractionSample& input,std::uint64_t nativeEquip)noexcept {
        if(unsigned(reason)>=counts_.size())return;++counts_[unsigned(reason)];++total_;
        Row row{reason,preserve,before,after,input.sequence,nativeEquip,input.owner.actor,input.owner.space,input.nowNs};
        if(havePrevious_){const auto& last=previous_;
            if(last.reason==row.reason&&last.preserve==row.preserve&&last.before==row.before&&last.after==row.after&&
                last.nativeEquip==row.nativeEquip&&last.actor==row.actor&&last.space==row.space)return;}
        previous_=row;havePrevious_=true;
        if(size_==rows_.size()){++dropped_;return;}rows_[size_++]=row;
    }
    void Invalidate(Bc2BodyHolster& holster,BodyHolsterLifecycleReason reason,bool preserve,
        const interaction::HandInteractionSample& input,std::uint64_t nativeEquip)noexcept {
        // Failed semantic Apply cannot invent a shoulder Draw. Retain only
        // committed empty intent; recovery still requires exact current identity,
        // fresh native suppression and a new paired visibility receipt.
        preserve=preserve||reason==BodyHolsterLifecycleReason::InputApplyRejected;
        const auto before=holster.Phase();holster.Invalidate(preserve);Observe(reason,preserve,before,holster.Phase(),input,nativeEquip);
    }
    void Report(std::ostream& out)const {
        out<<"{\"schema\":1,\"capacity\":64,\"total\":"<<total_<<",\"dropped\":"<<dropped_<<",\"counts\":{";
        for(unsigned n=0;n<counts_.size();++n){if(n)out<<',';out<<'\"'<<BodyHolsterLifecycleName(BodyHolsterLifecycleReason(n))<<"\":"<<counts_[n];}
        out<<"},\"rows\":[";for(unsigned n=0;n<size_;++n){if(n)out<<',';const auto& r=rows_[n];
            out<<"{\"reason\":\""<<BodyHolsterLifecycleName(r.reason)<<"\",\"preserve_intent\":"<<(r.preserve?"true":"false")
               <<",\"before\":"<<unsigned(r.before)<<",\"after\":"<<unsigned(r.after)<<",\"input\":"<<r.input<<",\"native_equip\":"<<r.nativeEquip
               <<",\"actor\":"<<r.actor<<",\"space\":"<<r.space<<",\"source_now_ns\":"<<r.now<<'}';}out<<"]}";
    }
private:
    struct Row {BodyHolsterLifecycleReason reason{};bool preserve=false;BodyHolsterPhase before{},after{};
        std::uint64_t input=0,nativeEquip=0,actor=0,space=0;std::int64_t now=0;};
    std::array<std::uint64_t,unsigned(BodyHolsterLifecycleReason::Count)> counts_{};
    std::array<Row,64> rows_{};unsigned size_=0;std::uint64_t total_=0,dropped_=0;
    Row previous_{};bool havePrevious_=false;
};
}
