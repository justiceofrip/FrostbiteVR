#pragma once
#include "fvr/graphics/FrameBridgeDiagnostics.h"
#include <atomic>
#include <ostream>
namespace fvr::graphics {
// Diagnostic values only. Neither this row nor its retention authorizes depth,
// publication, resource reuse or a native call. No COM objects are retained.
enum class DepthClearFilter:unsigned {None,InvalidScope,Context,NullView,NotTexture,Dimensions,Format,Count};
enum class DepthClearFailure:unsigned {None,Flags,DepthValue,Stencil,TooManyClears,SourceChanged,WorldCapture};
inline constexpr const char* DepthClearFilterNames[]{"none","scope_already_invalid","context_mismatch","null_dsv",
    "resource_not_texture2d","dimensions_mismatch","format_mismatch"};
inline constexpr const char* DepthClearFailureNames[]{"none","clear_flags","clear_depth_value","clear_stencil",
    "too_many_matching_clears","source_changed","world_capture_rejected"};
struct DepthClearSample {
    std::uintptr_t context=0,view=0,source=0;
    unsigned flags=0,depthBits=0,stencil=0,width=0,height=0,format=0,samples=0,quality=0,mips=0,array=0;
    bool hasDescriptor=false;
};
struct DepthEyeFailureRow {
    FrameBridgeTimingKey key{};
    unsigned slot=0,slotCount=1,eye=0,width=0,height=0,thread=0,expectedThread=0;
    std::uintptr_t expectedContext=0,retainedSource=0;
    std::int64_t beginQpc=0,endQpc=0;
    std::uint64_t allHooksBegin=0,allHooksEnd=0,unscopedBegin=0,unscopedEnd=0;
    unsigned scopedHooks=0,matchingHooks=0,clears=0,evaluated=0,passed=0;
    std::array<unsigned,unsigned(DepthClearFilter::Count)> filters{};
    DepthClearFilter firstFilter=DepthClearFilter::None;
    DepthClearFailure invalidCause=DepthClearFailure::None;
    DepthClearSample firstHook{},firstTexture{},firstIgnored{},invalidClear{};
    unsigned captureAttemptedMask=0,captureAcceptedMask=0,bridgeFailureStage=0;
    std::int32_t bridgeFailureCode=0;
    void Filter(DepthClearFilter reason,const DepthClearSample& sample)noexcept {
        ++filters[unsigned(reason)];
        if(firstFilter==DepthClearFilter::None){firstFilter=reason;firstIgnored=sample;}
    }
    bool Test(unsigned bit,bool value)noexcept {
        evaluated|=1u<<bit;if(value)passed|=1u<<bit;return value;
    }
    const char* FailureClass()const noexcept {
        if(invalidCause!=DepthClearFailure::None)return DepthClearFailureNames[unsigned(invalidCause)];
        if(!scopedHooks)return "no_scoped_clear_calls";
        if(!matchingHooks)return "no_matching_clear";
        constexpr const char* names[]{"invalid_sequence","matching_clear_count","missing_source",
            "tracked_state_changed","request_changed","native_frame_changed","final_capture_rejected"};
        for(unsigned i=0;i<7;++i)if((evaluated&(1u<<i))&&!(passed&(1u<<i)))return names[i];
        return "unclassified";
    }
};
class FirstDepthEyeFailure {
public:
    bool Pending()const noexcept{return state_.load(std::memory_order_acquire)==0;}
    // Until the first failed eye, count actual hook entries even if its TLS is
    // absent. Differences are process-wide observations, NOT eye attribution.
    void Hook(bool scoped)noexcept {
        if(!Pending())return;
        allHooks_.fetch_add(1,std::memory_order_relaxed);
        if(!scoped)unscoped_.fetch_add(1,std::memory_order_relaxed);
    }
    void Begin(DepthEyeFailureRow& row)const noexcept {
        row.allHooksBegin=allHooks_.load(std::memory_order_relaxed);
        row.unscopedBegin=unscoped_.load(std::memory_order_relaxed);
    }
    void Observe(DepthEyeFailureRow row)noexcept {
        unsigned empty=0;if(!state_.compare_exchange_strong(empty,1,std::memory_order_acq_rel))return;
        row.allHooksEnd=allHooks_.load(std::memory_order_relaxed);
        row.unscopedEnd=unscoped_.load(std::memory_order_relaxed);
        row_=row;state_.store(2,std::memory_order_release);
    }
    bool Snapshot(DepthEyeFailureRow& out,bool callbacksDrained)const noexcept {
        out={};if(!callbacksDrained||state_.load(std::memory_order_acquire)!=2)return false;
        out=row_;return true;
    }
    void Write(std::ostream& out,bool callbacksDrained)const {
        const auto state=state_.load(std::memory_order_acquire);
        out<<"{\"schema\":\"fvr.first-depth-eye-failure.v1\",\"available\":"
            <<(callbacksDrained&&state!=1?"true":"false");
        if(!callbacksDrained||state==1){out<<'}';return;}
        out<<",\"observed\":"<<(state==2?"true":"false");if(state!=2){out<<'}';return;}
        const auto& r=row_;const auto& k=r.key;
        out<<",\"reason\":\""<<r.FailureClass()<<"\",\"begin_qpc\":"<<r.beginQpc<<",\"end_qpc\":"<<r.endQpc
           <<",\"clock\":\"qpc_ticks\",\"eye\":"<<r.eye<<",\"thread\":"<<r.thread
           <<",\"expected_graphics_thread\":"<<r.expectedThread<<",\"expected_context\":"<<r.expectedContext
           <<",\"width\":"<<r.width<<",\"height\":"<<r.height<<",\"source_slot\":"<<r.slot<<",\"source_slot_count\":"<<r.slotCount
           <<",\"lease\":{\"channel\":"<<k.channel<<",\"request\":"<<k.request<<",\"owner\":"<<k.owner
           <<",\"frame\":"<<k.frame<<",\"device\":"<<k.device<<",\"tracking\":"<<k.tracking<<",\"space\":"<<k.space
           <<",\"predicted_ns\":"<<k.predictedNs<<"},\"scoped_hook_calls\":"<<r.scopedHooks
           <<",\"matching_hook_calls\":"<<r.matchingHooks<<",\"matching_clear_sequence_count\":"<<r.clears
           <<",\"process_hook_calls_begin\":"<<r.allHooksBegin<<",\"process_hook_calls_end\":"<<r.allHooksEnd
           <<",\"process_unscoped_calls_begin\":"<<r.unscopedBegin<<",\"process_unscoped_calls_end\":"<<r.unscopedEnd
           <<",\"process_counts_are_eye_attribution\":false,\"filters\":{";
        for(unsigned i=1;i<unsigned(DepthClearFilter::Count);++i){if(i>1)out<<',';out<<'"'<<DepthClearFilterNames[i]<<"\":"<<r.filters[i];}
        const auto sample=[&](const DepthClearSample& s){out<<"{\"context\":"<<s.context<<",\"dsv\":"<<s.view
            <<",\"source\":"<<s.source<<",\"flags\":"<<s.flags<<",\"depth_float_bits\":"<<s.depthBits<<",\"stencil\":"<<s.stencil
            <<",\"descriptor_present\":"<<(s.hasDescriptor?"true":"false")<<",\"width\":"<<s.width<<",\"height\":"<<s.height
            <<",\"format\":"<<s.format<<",\"samples\":"<<s.samples<<",\"quality\":"<<s.quality
            <<",\"mips\":"<<s.mips<<",\"array_size\":"<<s.array<<'}';};
        out<<"},\"first_filter\":\""<<DepthClearFilterNames[unsigned(r.firstFilter)]<<"\",\"first_hook\":";sample(r.firstHook);
        out<<",\"first_texture\":";sample(r.firstTexture);out<<",\"first_ignored\":";sample(r.firstIgnored);
        out<<",\"invalid_clear\":";sample(r.invalidClear);
        out<<",\"retained_source\":"<<r.retainedSource<<",\"invalid_cause\":\""<<DepthClearFailureNames[unsigned(r.invalidCause)]
           <<"\",\"end_evaluated_mask\":"<<r.evaluated<<",\"end_passed_mask\":"<<r.passed
           <<",\"end_predicates\":[\"not_invalid\",\"two_matching_clears\",\"source_present\",\"tracked_state_1\",\"same_request\",\"same_native_frame\",\"final_capture_accepted\"]"
           <<",\"capture_stage_order\":[\"world\",\"final\"],\"capture_attempted_mask\":"<<r.captureAttemptedMask
           <<",\"capture_accepted_mask\":"<<r.captureAcceptedMask<<",\"bridge_failure_stage_at_rejection\":"<<r.bridgeFailureStage
           <<",\"bridge_failure_code_at_rejection\":"<<r.bridgeFailureCode
           <<",\"bridge_false_reason_available\":false,\"diagnostic_only\":true}";
    }
private:
    std::atomic<unsigned> state_=0;
    std::atomic<std::uint64_t> allHooks_=0,unscoped_=0;
    DepthEyeFailureRow row_{};
};
}
